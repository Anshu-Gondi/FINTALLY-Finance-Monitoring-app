# `fin_ocr_native`

High-performance, thermal-aware C++17 ingestion and vision preprocessing engine designed to prepare raw documents (PDFs, receipts, financial charts) into structured context streams for LLM prompt windows.

---

## 🔑 Features

* **Dual Extraction Pipeline:** Direct PDF UTF-8 string extraction via PDFium combined with spatial raster density layout analysis.
* **SIMD Vision Preprocessing:** Hardware-accelerated image decoding (`stb_image`) and color-space isolation (AVX2/AVX-512).
* **Thermal-Aware Degradation:** Monitors Linux sysfs CPU thermal zones to dynamically shift processing intensity under heavy load.
* **C-FFI Ready:** Exports clean, position-independent C symbols (`-fPIC`, hidden C++ visibility) designed for Rust/Python FFI bindings.

---

## 📋 Prerequisites & Tooling

* **Compiler:** GCC 9+, Clang 10+, or MSVC 2019+ (C++17 compliant)
* **Build System:** CMake `3.16` or higher, Ninja (recommended) or Make
* **Pkg-Config (Optional):** Used to locate system-installed `pdfium`. If missing, CMake automatically fetches prebuilt binaries.

---

## 🛠️ Build Configuration Options

Configure these flags during the `cmake` generation step using `-D<OPTION>=<ON|OFF>`:

| CMake Option | Default | Description |
| --- | --- | --- |
| `FIN_BUILD_SHARED` | `OFF` | Build shared library (`.so`/`.dll`) instead of static (`.a`/`.lib`) |
| `FIN_ENABLE_NATIVE` | `ON` | Enable `-march=native` CPU microarchitecture targeting |
| `FIN_ENABLE_AVX2` | `ON` | Vectorized AVX2 SIMD instructions |
| `FIN_ENABLE_AVX512` | `OFF` | Vectorized AVX-512 SIMD instructions |
| `FIN_ENABLE_LTO` | `ON` | Enable Link-Time Optimization (IPO/LTO) |
| `FIN_WARNINGS_AS_ERRORS` | `OFF` | Treat compiler warnings as errors (`-Werror` / `/WX`) |

---

## 🚀 Quick Start & Building

### 1. Build Static Library (Default)

```bash
# Generate build directory
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFIN_ENABLE_NATIVE=ON

# Compile the target
cmake --build build --config Release

```

### 2. Build Shared Library for FFI Linking

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFIN_BUILD_SHARED=ON

cmake --build build --config Release

```

### 3. Build with Local Custom PDFium

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPDFIUM_ROOT_DIR="/opt/pdfium"

cmake --build build --config Release

```

---

## 🧪 Running Tests & Benchmarks

CMake automatically fetches **GoogleTest** (`v1.14.0`) and **Google Benchmark** (`v1.8.3`) during build generation.

### Run Unit & Dataset Tests

```bash
# Compile test executables
cmake --build build --target unit_tests dataset_tests

# Option A: Run via CTest
ctest --test-dir build --output-on-failure

# Option B: Run test executables directly
./build/unit_tests
./build/dataset_tests

```

### Run Benchmarks

```bash
# Compile benchmark executables
cmake --build build --target bench_runner bench_dataset

# Run synthetic engine performance benchmark
./build/bench_runner

# Run real-dataset processing benchmark
./build/bench_dataset

```

---

## 📁 Project Structure

```text
├── cpp_engine/
│   ├── debug_output/         # Diagnostic outputs (receipt PPM/PNG files)
│   ├── include/              # Public headers
│   │   ├── ffi_bridge.h      # C-FFI public interface
│   │   ├── tensor_ops.hpp    # SIMD tensor and image processing operations
│   │   └── thermal_sensor.hpp # Thermal monitor header
│   ├── src/                  # Internal C++ implementation source files
│   │   ├── ffi_bridge.cpp    # C-FFI entry points
│   │   ├── ocr_pipeline.cpp  # Vision & PDF parsing pipeline
│   │   └── thermal_sensor.cpp # Sysfs thermal telemetry monitor
│   ├── tests/                # GoogleTest test suites
│   │   ├── test_dataset.cpp  # Real-document integration tests
│   │   └── test_engine.cpp   # Unit testing logic
│   ├── CMakeLists.txt        # Main CMake build configuration
│   ├── massif.out            # Valgrind Massif memory profiling output
│   ├── massif_dataset.out    # Dataset memory profiling output
│   ├── Readme.md             # Project documentation
│   └── valgrind.supp         # Valgrind suppression definitions

```

---

## 🔗 Rust FFI Integration Example

Link `libfin_ocr_native.a` directly inside a Rust pipeline:

```rust
// Cargo build dependency interface
#[link(name = "fin_ocr_native", kind = "static")]
extern "C" {
    fn fin_process_document(data: *const u8, length: usize) -> *const std::os::raw::c_char;
    fn fin_free_string(ptr: *const std::os::raw::c_char);
}

pub fn process_pdf(bytes: &[u8]) -> String {
    unsafe {
        let raw_ptr = fin_process_document(bytes.as_ptr(), bytes.len());
        let text = std::ffi::CStr::from_ptr(raw_ptr).to_string_lossy().into_owned();
        fin_free_string(raw_ptr);
        text
    }
}

```
