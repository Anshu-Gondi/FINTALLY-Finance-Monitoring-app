//! Optional GPU acceleration for offline numerical training.
//!
//! Compile with `--features cuda` to expose the CUDA driver/NVRTC implementation.
//! Without that Cargo feature, the builder has no CUDA dependency at runtime and
//! `kmeans.rs` uses its Rayon CPU implementation.

#[cfg(feature = "cuda")]
pub mod cuda_kmeans;
