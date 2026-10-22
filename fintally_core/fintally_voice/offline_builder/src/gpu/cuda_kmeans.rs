//! CUDA nearest-centroid assignment for offline K-means.
//!
//! The GPU data matrix is uploaded once per language into device memory and is
//! kept resident for all K-means iterations. Upload uses two reusable page-locked
//! host staging buffers and two CUDA streams: the CPU can fill the alternate
//! staging buffer while the previous host-to-device DMA transfer is in flight.
//!
//! DMA does NOT bypass PCIe latency. It provides a transfer mechanism that can
//! overlap copies and computation when the hardware, pinned memory, streams, and
//! scheduling permit it. The design therefore avoids re-uploading the full
//! feature matrix each iteration; only assignments/distances return to the host.
//!
//! This module uses NVRTC to compile a small kernel at runtime. A CUDA driver and
//! a compatible `libnvrtc` must be present. Initialization failures are handled
//! by `kmeans.rs` as a logged CPU fallback; failures after launch are returned.

use std::sync::Arc;

use anyhow::{ensure, Context, Result};
use cudarc::driver::{
    CudaContext, CudaFunction, CudaModule, CudaSlice, CudaStream, LaunchConfig, PinnedHostSlice,
    PushKernelArg,
};
use cudarc::nvrtc::compile_ptx;

use crate::features::{FeatureVector, FEATURE_DIMENSION};

const BLOCK_THREADS: usize = 256;
/// One pinned stage is 8 MiB of FP32 features (65,536 * 32 * 4 bytes). Two
/// buffers allow CPU packing for the next chunk to overlap the previous DMA.
const STAGING_FRAMES: usize = 65_536;
const BYTES_PER_MIB: usize = 1024 * 1024;
const VRAM_RESERVE_MIB: usize = 512;

const ASSIGNMENT_KERNEL: &str = r#"
extern "C" __global__ void assign_nearest_centroid(
    const float* __restrict__ points,
    const float* __restrict__ centroids,
    unsigned int* __restrict__ labels,
    float* __restrict__ squared_distances,
    unsigned long long sample_count,
    unsigned int cluster_count)
{
    const unsigned long long sample =
        (unsigned long long)blockIdx.x * (unsigned long long)blockDim.x +
        (unsigned long long)threadIdx.x;
    if (sample >= sample_count) return;

    const float* x = points + sample * 32ULL;
    float best_distance = __int_as_float(0x7f800000);
    unsigned int best_cluster = 0;

    // 32 is the fixed FinTally Voice acoustic feature dimension. Keeping it
    // compile-time constant encourages NVRTC to unroll and schedule the inner loop.
    for (unsigned int cluster = 0; cluster < cluster_count; ++cluster) {
        const float* center = centroids + (unsigned long long)cluster * 32ULL;
        float distance = 0.0F;
        #pragma unroll
        for (int dimension = 0; dimension < 32; ++dimension) {
            const float delta = x[dimension] - center[dimension];
            distance += delta * delta;
        }
        // Strict < preserves the same lowest-ID tie-breaking rule as the CPU path.
        if (distance < best_distance) {
            best_distance = distance;
            best_cluster = cluster;
        }
    }

    labels[sample] = best_cluster;
    squared_distances[sample] = best_distance;
}
"#;

/// One long-lived CUDA engine. Kernel compilation and device/context setup occur
/// once per training run rather than once per language or per K-means iteration.
pub struct CudaKMeans {
    context: Arc<CudaContext>,
    stream: Arc<CudaStream>,
    // Kept alive explicitly for a clear module/function lifetime relationship.
    _module: Arc<CudaModule>,
    assign_kernel: CudaFunction,
}

/// Reusable device-resident data and pinned output buffers for a single language.
pub struct CudaDataset {
    sample_count: usize,
    cluster_count: usize,
    points_device: CudaSlice<f32>,
    centroids_device: CudaSlice<f32>,
    labels_device: CudaSlice<u32>,
    distances_device: CudaSlice<f32>,
    // Cacheable pinned memory is intentional for DtoH outputs because the CPU
    // reads these buffers after CUDA completes. The input stages below are
    // write-combined, matching the CPU-write/GPU-read direction.
    host_labels: PinnedHostSlice<u32>,
    host_distances: PinnedHostSlice<f32>,
    host_centroids: PinnedHostSlice<f32>,
}

impl CudaKMeans {
    pub fn new() -> Result<Self> {
        let context = CudaContext::new(0).context("opening CUDA device 0 failed")?;
        let ptx = compile_ptx(ASSIGNMENT_KERNEL)
            .context("NVRTC could not compile the K-means assignment kernel")?;
        let module = context
            .load_module(ptx)
            .context("loading K-means PTX module failed")?;
        let assign_kernel = module
            .load_function("assign_nearest_centroid")
            .context("loading CUDA nearest-centroid kernel failed")?;
        let stream = context.default_stream();
        Ok(Self {
            context,
            stream,
            _module: module,
            assign_kernel,
        })
    }

    /// Upload each language's training vectors once. `points` contains references
    /// into `FeatureSets`; the only contiguous copy is the transfer staging chunk,
    /// not a second full-size CPU matrix.
    pub fn prepare_dataset(
        &self,
        points: &[&FeatureVector],
        cluster_count: usize,
    ) -> Result<CudaDataset> {
        ensure!(
            FEATURE_DIMENSION == 32,
            "CUDA assignment kernel requires 32 features, but FEATURE_DIMENSION is {}",
            FEATURE_DIMENSION
        );
        ensure!(
            !points.is_empty(),
            "cannot upload an empty CUDA K-means dataset"
        );
        ensure!(
            cluster_count >= 2,
            "CUDA K-means needs at least two clusters"
        );
        ensure!(
            cluster_count <= u32::MAX as usize,
            "CUDA cluster count does not fit u32"
        );

        let staging_chunks = points.len().div_ceil(STAGING_FRAMES);
        let padded_sample_count = staging_chunks
            .checked_mul(STAGING_FRAMES)
            .context("padded CUDA sample count overflow")?;
        let padded_float_count = padded_sample_count
            .checked_mul(FEATURE_DIMENSION)
            .context("padded CUDA feature matrix size overflow")?;
        let point_bytes = padded_float_count
            .checked_mul(std::mem::size_of::<f32>())
            .context("CUDA feature matrix byte count overflow")?;
        let output_bytes = points
            .len()
            .checked_mul(std::mem::size_of::<u32>() + std::mem::size_of::<f32>())
            .context("CUDA output byte count overflow")?;
        let centroid_bytes = cluster_count
            .checked_mul(FEATURE_DIMENSION)
            .and_then(|elements| elements.checked_mul(std::mem::size_of::<f32>()))
            .context("CUDA centroid byte count overflow")?;
        let requested_bytes = point_bytes
            .checked_add(output_bytes)
            .and_then(|value| value.checked_add(centroid_bytes))
            .context("CUDA allocation estimate overflow")?;

        let (free_bytes, total_bytes) = self
            .context
            .mem_get_info()
            .context("querying CUDA memory failed")?;
        // Don't consume all VRAM: the user's display, driver, other processes,
        // kernel workspaces, and CUDA runtime allocations need breathing room.
        let reserve = (VRAM_RESERVE_MIB * BYTES_PER_MIB).min(free_bytes / 3);
        let allocatable = free_bytes.saturating_sub(reserve);
        ensure!(
            requested_bytes <= allocatable,
            "language needs about {:.2} MiB of device memory, but only {:.2} MiB is available after a safety reserve (free {:.2} MiB / total {:.2} MiB)",
            requested_bytes as f64 / BYTES_PER_MIB as f64,
            allocatable as f64 / BYTES_PER_MIB as f64,
            free_bytes as f64 / BYTES_PER_MIB as f64,
            total_bytes as f64 / BYTES_PER_MIB as f64,
        );

        // SAFETY: every element in this padded allocation is overwritten by
        // one of the staging copies before any kernel reads the matrix.
        let mut points_device = unsafe { self.stream.alloc::<f32>(padded_float_count) }
            .context("allocating resident CUDA feature matrix failed")?;

        // Pinned host allocation is bounded at two 8 MiB chunks, not N*D values.
        // CudaContext owns the page locking and frees it when each buffer drops.
        let mut staging_a = unsafe {
            self.context
                .alloc_pinned::<f32>(STAGING_FRAMES * FEATURE_DIMENSION)
        }
        .context("allocating first pinned CUDA upload stage failed")?;
        let mut staging_b = unsafe {
            self.context
                .alloc_pinned::<f32>(STAGING_FRAMES * FEATURE_DIMENSION)
        }
        .context("allocating second pinned CUDA upload stage failed")?;

        let copy_stream_a = self
            .context
            .new_stream()
            .context("creating CUDA upload stream A failed")?;
        let copy_stream_b = self
            .context
            .new_stream()
            .context("creating CUDA upload stream B failed")?;

        for chunk_index in 0..staging_chunks {
            let first_sample = chunk_index * STAGING_FRAMES;
            let stream_slot = chunk_index % 2;
            let device_start = first_sample * FEATURE_DIMENSION;
            let device_end = device_start + STAGING_FRAMES * FEATURE_DIMENSION;
            let mut device_chunk = points_device
                .try_slice_mut(device_start..device_end)
                .context("creating device feature upload view failed")?;

            if stream_slot == 0 {
                fill_staging(&mut staging_a, points, first_sample)?;
                copy_stream_a
                    .memcpy_htod(&staging_a, &mut device_chunk)
                    .context("enqueuing pinned host-to-device DMA for upload stage A failed")?;
            } else {
                fill_staging(&mut staging_b, points, first_sample)?;
                copy_stream_b
                    .memcpy_htod(&staging_b, &mut device_chunk)
                    .context("enqueuing pinned host-to-device DMA for upload stage B failed")?;
            }
            // While one stream transfers its pinned stage, the CPU prepares the
            // alternate stage. Reuse of either buffer synchronizes that buffer's
            // prior transfer before the CPU writes to it again.
        }

        copy_stream_a
            .synchronize()
            .context("waiting for CUDA upload stream A failed")?;
        copy_stream_b
            .synchronize()
            .context("waiting for CUDA upload stream B failed")?;
        drop(staging_a);
        drop(staging_b);

        // SAFETY: each device output is completely written by the assignment
        // kernel before its contents are copied to the host.
        let centroids_device =
            unsafe { self.stream.alloc::<f32>(cluster_count * FEATURE_DIMENSION) }
                .context("allocating device centroid buffer failed")?;
        let labels_device = unsafe { self.stream.alloc::<u32>(points.len()) }
            .context("allocating device assignment buffer failed")?;
        let distances_device = unsafe { self.stream.alloc::<f32>(points.len()) }
            .context("allocating device squared-distance buffer failed")?;

        // Flag 0 requests page-locked host memory without write-combined caching.
        // CUDA writes the results and the CPU reads them, so ordinary host caching
        // is better here than the write-combined policy used for upload stages.
        // cudarc 0.19.8: use its supported pinned-host allocation API.
        let host_labels = unsafe { self.context.alloc_pinned::<u32>(points.len()) }
            .context("allocating pinned assignment output failed")?;

        let host_distances = unsafe { self.context.alloc_pinned::<f32>(points.len()) }
            .context("allocating pinned distance output failed")?;
        let host_centroids = unsafe {
            self.context
                .alloc_pinned::<f32>(cluster_count * FEATURE_DIMENSION)
        }
        .context("allocating pinned centroid staging buffer failed")?;

        Ok(CudaDataset {
            sample_count: points.len(),
            cluster_count,
            points_device,
            centroids_device,
            labels_device,
            distances_device,
            host_labels,
            host_distances,
            host_centroids,
        })
    }

    /// Run one full-data assignment pass. The CUDA kernel is grid-parallel over
    /// all frames; input stays on-device between iterations. This is not LLM-style
    /// continuous batching: K-means needs global assignments before each update.
    pub fn assign<'a>(
        &self,
        dataset: &'a mut CudaDataset,
        centroids: &[FeatureVector],
    ) -> Result<(&'a [u32], &'a [f32])> {
        ensure!(
            centroids.len() == dataset.cluster_count,
            "CUDA centroid count changed during training"
        );

        {
            let host = dataset
                .host_centroids
                .as_mut_slice()
                .context("accessing pinned centroid staging buffer failed")?;
            for (cluster, centroid) in centroids.iter().enumerate() {
                let start = cluster * FEATURE_DIMENSION;
                host[start..start + FEATURE_DIMENSION].copy_from_slice(centroid);
            }
        }
        self.stream
            .memcpy_htod(&dataset.host_centroids, &mut dataset.centroids_device)
            .context("copying centroids to the GPU failed")?;

        let sample_count = u64::try_from(dataset.sample_count)
            .context("sample count does not fit CUDA u64 argument")?;
        let cluster_count = u32::try_from(dataset.cluster_count)
            .context("cluster count does not fit CUDA u32 argument")?;
        let block_count = dataset.sample_count.div_ceil(BLOCK_THREADS);
        let grid_x = u32::try_from(block_count).context("CUDA launch grid is too large")?;
        let launch_config = LaunchConfig {
            grid_dim: (grid_x, 1, 1),
            block_dim: (BLOCK_THREADS as u32, 1, 1),
            shared_mem_bytes: 0,
        };

        let mut launch = self.stream.launch_builder(&self.assign_kernel);
        launch.arg(&dataset.points_device);
        launch.arg(&dataset.centroids_device);
        launch.arg(&mut dataset.labels_device);
        launch.arg(&mut dataset.distances_device);
        launch.arg(&sample_count);
        launch.arg(&cluster_count);
        // SAFETY: The kernel's pointer argument order, element types, and lengths
        // correspond to the CudaSlice/u64/u32 arguments above. Its indexing bounds
        // guard ensures each thread writes exactly one sample row within N.
        unsafe { launch.launch(launch_config) }
            .context("launching CUDA nearest-centroid kernel failed")?;

        self.stream
            .memcpy_dtoh(&dataset.labels_device, &mut dataset.host_labels)
            .context("copying CUDA labels to pinned host memory failed")?;
        self.stream
            .memcpy_dtoh(&dataset.distances_device, &mut dataset.host_distances)
            .context("copying CUDA distances to pinned host memory failed")?;

        // PinnedHostSlice::as_slice synchronizes with the transfer event, so these
        // references are valid for CPU reduction. The buffers remain owned by the
        // dataset and are reused on the next iteration.
        let labels = dataset
            .host_labels
            .as_slice()
            .context("waiting for pinned labels failed")?;
        let distances = dataset
            .host_distances
            .as_slice()
            .context("waiting for pinned distances failed")?;
        Ok((labels, distances))
    }
}

fn fill_staging(
    stage: &mut PinnedHostSlice<f32>,
    points: &[&FeatureVector],
    first_sample: usize,
) -> Result<()> {
    let output = stage
        .as_mut_slice()
        .context("accessing pinned feature upload stage failed")?;
    for row in 0..STAGING_FRAMES {
        let sample_index = first_sample + row;
        let start = row * FEATURE_DIMENSION;
        let destination = &mut output[start..start + FEATURE_DIMENSION];
        if let Some(point) = points.get(sample_index) {
            destination.copy_from_slice(&point[..]);
        } else {
            // The final device allocation is padded to a staging-chunk multiple.
            // Zero the unused tail so every allocated element is initialized.
            destination.fill(0.0);
        }
    }
    Ok(())
}
