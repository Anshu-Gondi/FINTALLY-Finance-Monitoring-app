# `fin_ocr_native`

High-performance, thermal-aware C++17 OCR, document, and chart vision engine designed to transform raw images, PDFs, receipts, tabular documents, and financial charts into structured processing context for downstream systems and LLM pipelines.

The engine is implemented as a native C++ subsystem with explicit module boundaries, deterministic image-processing stages, structured chart semantics, and a C-compatible FFI boundary for integration with Rust systems.

---

## 🔑 Current Capabilities

The project is actively evolving. Some workflows are production-oriented, while chart and PDF workflows are still under development.

### JPEG Tabular Image Processing

The currently usable document workflow focuses on **JPEG images containing tabular data**.

The engine is intended for:

* medium to medium-high quality tabular images
* high-quality tabular images
* structured financial/table-like layouts
* OCR-oriented preprocessing and extraction

Current tabular-image processing typically achieves approximately **70–90% usable extraction quality**, depending on image quality, table structure, text size, compression, and layout complexity.

This range is an observed capability target for supported tabular JPEG inputs and should not be interpreted as a universal accuracy guarantee.

Current primary input:

```text
JPEG tabular images
```

---

## 📊 Financial Chart Processing

Chart processing is currently in an **early development stage**.

The chart subsystem has already been modularized into dedicated processing stages and can currently perform structured chart-label recognition and chart geometry analysis.

### Current chart capability

The X-axis label workflow is currently the most mature semantic portion.

The engine can:

* detect chart candidates
* identify chart label regions
* recognize X-axis labels
* order X-axis labels spatially
* canonicalize constrained category labels
* construct structured `ChartLabel` records
* detect chart axes
* detect chart objects
* associate labels with chart structures
* begin converting chart geometry into structured chart analysis

The chart workflow is still being expanded and should currently be treated as an **early-stage chart interpretation pipeline**, rather than a fully general-purpose chart extraction system.

Current development direction:

```text
Chart image
    │
    ▼
Chart color isolation
    │
    ▼
Label recognition
    │
    ├── X-axis label recognition
    ├── Y-axis label recognition
    └── structured OCR
    │
    ▼
Axis detection
    │
    ▼
Object detection
    │
    ▼
Association
    │
    ▼
Chart interpretation
```

---

## 📄 PDF OCR

PDF processing infrastructure is present, including PDF page handling and rasterization, but the complete PDF OCR workflow is **not yet considered production-ready**.

Full PDF OCR capability is planned for a forthcoming development stage.

Current direction:

```text
PDF
  ↓
PDF page extraction / rasterization
  ↓
OCR preprocessing
  ↓
document OCR
```

The PDF OCR workflow will be expanded and validated before being considered a mature capability.

---

## 🔑 Features

### Document and OCR Processing

* JPEG image processing for supported tabular-document workflows.
* PDF page rasterization infrastructure.
* Grayscale, luminance, contrast, resize, and binary image operations.
* Tesseract-backed OCR processing.
* Line-oriented OCR recognition.
* Connected-component segmentation.
* Structured OCR processing pipelines.

### Financial Chart Processing

The chart subsystem is modularized into dedicated processing stages:

```text
ChartColorIsolator
        │
        ├───────────────┐
        │               │
        ▼               ▼
 Label Recognition   Axis Detection
        │               │
        └───────┬───────┘
                ▼
         Object Detection
                │
                ▼
        Association Engine
                │
                ▼
       Interpreter Engine
                │
                ▼
          Chart Analysis
```

The chart pipeline operates on structured data rather than parsing formatted OCR strings.

### Modular Label Recognition

Chart label recognition is separated into dedicated components for:

* candidate generation
* connected-component segmentation
* text normalization
* category-axis detection
* category scoring
* category sequence modeling
* category recovery
* Y-axis candidate scoring
* Y-axis sequence modeling
* Y-axis fragment consolidation
* Y-axis OCR
* Y-axis parsing
* Y-axis value recovery
* generic candidate OCR
* OCR buffer construction
* duplicate-label filtering
* label formatting

The public façade is:

```cpp
fin_ocr::chart::label::ChartLabelRecognizer
```

while orchestration is delegated to:

```cpp
fin_ocr::chart::label::LabelEngine
```

### Structured Chart Semantics

Chart processing is represented through structured types including:

```text
ChartCoordinateSystem
ChartObjectSet
ChartAssociationResult
ChartAnalysis
ChartDataPoint
ChartMetadata
```

This allows downstream stages to consume structured chart information directly without reparsing presentation strings.

### Chart Association

The association layer separates:

* label classification
* category association
* series association
* object association
* stacked-object relationships
* dual-axis relationships

The top-level API is:

```cpp
fin_ocr::chart::association::AssociationEngine
```

### Chart Interpretation

The interpreter layer determines and builds structured chart information including:

* chart family/type
* stacked classification
* percentage-stacked classification
* clustered classification
* combo classification
* axis-to-value mapping
* chart-family data extraction
* structured chart analysis
* semantic confidence

The top-level API is:

```cpp
fin_ocr::chart::interpreter::InterpreterEngine
```

### SIMD and Native Processing

The build system supports configurable native CPU optimization and SIMD-oriented processing.

Configuration options include:

* native CPU targeting
* AVX2
* AVX-512
* LTO / Thin LTO
* compiler warnings as errors

Low-level image and tensor operations are isolated from higher-level OCR and chart semantic logic.

### Thermal-Aware Processing

The engine includes a thermal sensor interface for Linux systems and can expose CPU thermal information through the native runtime.

### C FFI

The native engine exposes C-compatible entry points suitable for Rust and Python integration.

The native library can be built as either:

```text
libfin_ocr_native.a
```

or a shared library depending on the selected CMake configuration.

---

# 📋 Prerequisites & Tooling

* **Language:** C++17
* **Compiler:** GCC 9+, Clang 10+, or MSVC 2019+
* **Build System:** CMake 3.16+
* **Build Generators:** Ninja or Make
* **Pkg-Config:** optional
* **PDFium:** system installation or CMake-managed dependency
* **Tesseract OCR:** required for OCR processing
* **GoogleTest:** used for tests
* **Google Benchmark:** used for benchmarks

---

# 🛠️ Build Configuration

The following CMake options can be configured during generation:

| CMake Option             | Default | Description                                    |
| ------------------------ | ------- | ---------------------------------------------- |
| `FIN_BUILD_SHARED`       | `OFF`   | Build shared library instead of static library |
| `FIN_ENABLE_NATIVE`      | `ON`    | Enable native CPU microarchitecture targeting  |
| `FIN_ENABLE_AVX2`        | `ON`    | Enable AVX2 SIMD paths                         |
| `FIN_ENABLE_AVX512`      | `OFF`   | Enable AVX-512 SIMD paths                      |
| `FIN_ENABLE_LTO`         | `OFF`   | Enable Link-Time Optimization / Thin LTO       |
| `FIN_WARNINGS_AS_ERRORS` | `OFF`   | Treat compiler warnings as errors              |

### LTO / Rust FFI

LTO / Thin LTO is intentionally **disabled by default**.

The native library is designed to participate in a larger Rust FFI build, so the default configuration favors predictable native-library linking and integration behavior over enabling whole-program optimization automatically.

LTO can still be enabled explicitly for builds where the complete native/Rust toolchain has been validated:

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFIN_ENABLE_LTO=ON
```

When enabling LTO, validate both:

```text
C++ native build
        +
Rust FFI integration
```

rather than evaluating the native library in isolation.

---

# 🚀 Quick Start

## 1. Configure a Release build

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFIN_ENABLE_NATIVE=ON
```

## 2. Build

```bash
cmake --build build --config Release
```

For Make-based builds:

```bash
cmake -B build \
    -DCMAKE_BUILD_TYPE=Release

cmake --build build --parallel 2
```

## 3. Build the shared library

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DFIN_BUILD_SHARED=ON

cmake --build build --config Release
```

## 4. Use a local PDFium installation

```bash
cmake -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPDFIUM_ROOT_DIR="/opt/pdfium"

cmake --build build --config Release
```

---

# 🧪 Tests

The project uses GoogleTest for native unit and integration testing.

Build the test target:

```bash
cmake --build build --target fin_ocr_tests
```

Run the complete CTest suite:

```bash
ctest --test-dir build --output-on-failure
```

The test sources are located in:

```text
tests/
├── CMakeLists.txt
├── test_dataset.cpp
├── test_engine.cpp
└── test_tensor_ops.cpp
```

---

# 📊 Benchmarks

The project uses Google Benchmark for performance measurements.

Available benchmark programs include:

```text
bench_runner
bench_dataset
bench_dataset_diagnostic
bench_matrix_matcher
bench_tensor_ops
```

Build them with:

```bash
cmake --build build --target \
    bench_runner \
    bench_dataset \
    bench_dataset_diagnostic \
    bench_matrix_matcher \
    bench_tensor_ops
```

Run the synthetic engine benchmark:

```bash
./build/benchmarks/bench_runner
```

Run the dataset benchmark:

```bash
./build/benchmarks/bench_dataset
```

Run the dataset diagnostic benchmark:

```bash
./build/benchmarks/bench_dataset_diagnostic
```

Run matrix matching benchmarks:

```bash
./build/benchmarks/bench_matrix_matcher
```

Run tensor-operation benchmarks:

```bash
./build/benchmarks/bench_tensor_ops
```

---

# 📁 Project Structure

```text
ocr_engine/
│
├── CMakeLists.txt
├── Readme.md
│
├── benchmarks/
│   ├── CMakeLists.txt
│   ├── bench_dataset.cpp
│   ├── bench_dataset_diagnostic.cpp
│   ├── bench_engine.cpp
│   ├── bench_matrix_matcher.cpp
│   └── bench_tensor_ops.cpp
│
├── cmake/
│   ├── CompilerOptions.cmake
│   ├── Dependencies.cmake
│   └── Sanitizers.cmake
│
├── data/
│   ├── financial-graphs-and-charts-in-excel.jpg
│   ├── financial_balance_sheet_image.pdf
│   ├── financial_balance_sheet_image.webp
│   ├── financial_chart_test_image.webp
│   ├── financial_chart_test_image_2.webp
│   ├── financial_mulit_receipt_image.webp
│   ├── financial_receipt_image.jpg
│   └── ground_truth/
│
├── debug_output/
│
├── diagnostic_output/
│
├── include/
│   ├── ffi_bridge.h
│   ├── tensor_ops.hpp
│   ├── thermal_sensor.hpp
│   │
│   └── fin_ocr/
│       ├── chart/
│       ├── core/
│       ├── document/
│       ├── image/
│       ├── line/
│       ├── matrix/
│       ├── matrix_matcher.hpp
│       ├── pdf/
│       ├── pipeline/
│       ├── segmentation/
│       └── tesseract/
│
├── src/
│   ├── CMakeLists.txt
│   │
│   ├── chart/
│   │   ├── CMakeLists.txt
│   │   ├── chart_color_isolator.cpp
│   │   │
│   │   ├── axis/
│   │   ├── coordinate/
│   │   ├── object/
│   │   ├── association/
│   │   ├── interpreter/
│   │   ├── label/
│   │   └── kernel/
│   │
│   ├── core/
│   ├── document/
│   ├── ffi_bridge.cpp
│   ├── image/
│   ├── line/
│   ├── matrix/
│   ├── matrix_matcher.cpp
│   ├── pdf/
│   ├── pipeline/
│   ├── segmentation/
│   ├── tesseract/
│   └── thermal_sensor.cpp
│
├── tests/
│   ├── CMakeLists.txt
│   ├── test_dataset.cpp
│   ├── test_engine.cpp
│   └── test_tensor_ops.cpp
│
└── valgrind.supp
```

---

# 📐 Chart Architecture

The chart subsystem is organized around explicit module ownership.

```text
include/fin_ocr/chart/
│
├── axis/
│   ├── axis_types.hpp
│   └── axis_detector.hpp
│
├── coordinate/
│   └── coordinate_system.hpp
│
├── object/
│   ├── object_types.hpp
│   └── object_detector.hpp
│
├── association/
│   ├── association_types.hpp
│   ├── association_engine.hpp
│   ├── association_geometry.hpp
│   ├── association_storage.hpp
│   │
│   ├── object/
│   ├── stacked/
│   ├── dual_axis/
│   └── label/
│
├── interpreter/
│   ├── interpreter_types.hpp
│   ├── interpreter_engine.hpp
│   ├── interpreter_geometry.hpp
│   ├── interpreter_lookup.hpp
│   ├── axis_value_mapper.hpp
│   ├── chart_type_classifier.hpp
│   ├── stack_classifier.hpp
│   │
│   └── data/
│       ├── bar_data_builder.hpp
│       ├── line_data_builder.hpp
│       ├── radial_data_builder.hpp
│       ├── scatter_data_builder.hpp
│       ├── waterfall_data_builder.hpp
│       ├── funnel_data_builder.hpp
│       └── treemap_data_builder.hpp
│
└── label/
    ├── label_types.hpp
    ├── label_recognizer.hpp
    ├── label_engine.hpp
    ├── label_geometry.hpp
    ├── label_filter.hpp
    ├── label_merger.hpp
    ├── label_output.hpp
    │
    ├── text/
    │   └── text_utils.hpp
    │
    ├── candidate/
    │   ├── candidate_builder.hpp
    │   └── candidate_segmenter.hpp
    │
    ├── category/
    │   ├── category_zone.hpp
    │   ├── category_scorer.hpp
    │   ├── category_sequence.hpp
    │   ├── category_canonicalizer.hpp
    │   └── category_recovery.hpp
    │
    ├── y_axis/
    │   ├── y_axis_types.hpp
    │   ├── y_axis_scorer.hpp
    │   ├── y_axis_sequence.hpp
    │   ├── y_axis_fragment_merger.hpp
    │   ├── y_axis_ocr.hpp
    │   ├── y_axis_parser.hpp
    │   └── y_axis_value_recovery.hpp
    │
    └── recognition/
        ├── candidate_ocr.hpp
        └── ocr_buffer.hpp
```

---

# 🏗️ Chart Source Architecture

The source tree follows the same ownership boundaries as the public headers:

```text
src/chart/
│
├── chart_color_isolator.cpp
│
├── axis/
│   ├── CMakeLists.txt
│   └── ...
│
├── coordinate/
│   ├── CMakeLists.txt
│   └── ...
│
├── object/
│   ├── CMakeLists.txt
│   └── ...
│
├── association/
│   ├── CMakeLists.txt
│   ├── association_engine.cpp
│   ├── association_geometry.cpp
│   ├── association_storage.cpp
│   │
│   ├── object/
│   ├── stacked/
│   ├── dual_axis/
│   └── label/
│
├── interpreter/
│   ├── CMakeLists.txt
│   ├── interpreter_engine.cpp
│   ├── interpreter_geometry.cpp
│   ├── interpreter_lookup.cpp
│   ├── axis_value_mapper.cpp
│   ├── chart_type_classifier.cpp
│   ├── stack_classifier.cpp
│   │
│   └── data/
│
└── label/
    ├── CMakeLists.txt
    ├── label_recognizer.cpp
    ├── label_engine.cpp
    ├── label_geometry.cpp
    ├── label_filter.cpp
    ├── label_merger.cpp
    ├── label_output.cpp
    │
    ├── text/
    ├── candidate/
    ├── category/
    ├── y_axis/
    └── recognition/
```

---

# 🧩 Module Responsibilities

## `chart/axis`

Owns chart-axis detection and axis-specific types.

```text
axis_types.hpp
    ↓
AxisKind
AxisTick
ChartAxis
```

```text
axis_detector.hpp
    ↓
ChartAxisDetector
```

---

## `chart/coordinate`

Owns the complete chart coordinate system.

```text
ChartCoordinateSystem
```

This provides the spatial reference used by object detection, association, and interpretation.

---

## `chart/object`

Owns chart-object types and object detection.

Supported object families include:

```text
bars
paths
radial slices
scatter / bubble points
waterfall steps
funnel stages
treemap nodes
generic chart objects
```

---

## `chart/association`

Converts independently detected labels and objects into relationships.

```text
ChartLabel[]
        +
ChartObjectSet
        +
ChartCoordinateSystem
        ↓
AssociationEngine
        ↓
ChartAssociationResult
```

The association system is further divided into:

```text
object/
stacked/
dual_axis/
label/
```

---

## `chart/interpreter`

Consumes structured associations and converts them into chart semantics and data.

```text
ChartCoordinateSystem
ChartObjectSet
ChartAssociationResult
        ↓
InterpreterEngine
        ↓
ChartAnalysis
```

Family-specific builders are isolated under:

```text
interpreter/data/
```

---

## `chart/label`

Owns structured chart text recognition.

The architecture separates:

```text
candidate generation
        ↓
zone classification
        ↓
sequence modeling
        ↓
OCR
        ↓
validation
        ↓
recovery
        ↓
deduplication
        ↓
structured ChartLabel[]
```

The public façade is:

```cpp
fin_ocr::chart::label::ChartLabelRecognizer
```

The orchestration layer is:

```cpp
fin_ocr::chart::label::LabelEngine
```

Presentation formatting is isolated in:

```cpp
fin_ocr::chart::label::output
```

with:

```cpp
format_invalid_chart_buffer()
format_empty_labels()
format_labels()
```

The semantic pipeline does not parse the resulting `[CHART_TEXT_DATA]` string.

---

# 🔄 Vision Pipeline

The chart processing path is integrated into `VisionPipeline`.

```text
Input bytes
     │
     ▼
Image/PDF decoding
     │
     ▼
ChartColorIsolator
     │
     ▼
ChartLabelRecognizer
     │
     ├── LabelEngine
     │      ├── candidate
     │      ├── category
     │      ├── y_axis
     │      └── recognition
     │
     ▼
ChartCoordinateSystem
     │
     ▼
ChartObjectSet
     │
     ▼
AssociationEngine
     │
     ▼
ChartAssociationResult
     │
     ▼
InterpreterEngine
     │
     ▼
ChartAnalysis
```

The formatted OCR compatibility payload is produced separately through the label output layer.

---

# 🧠 Design Principles

The engine follows several implementation principles.

### Structured Data First

OCR and chart processing use structured C++ records as the source of semantic information.

Formatted text is treated as transport/presentation data rather than an internal semantic interchange format.

### Explicit Ownership

Each major type and subsystem has a defined owner.

For example:

```text
AxisKind
    → axis_types.hpp

ChartCoordinateSystem
    → coordinate_system.hpp

ChartAssociationResult
    → association_types.hpp

ChartAnalysis
    → interpreter_types.hpp
```

### Layered Responsibility

The engine separates:

```text
image processing
OCR
geometry
candidate detection
semantic association
chart interpretation
output formatting
```

A module should not take ownership of work belonging to another layer.

### Deterministic Processing

Geometry and structural decisions are intended to remain deterministic and independently testable.

### Profiling Before Optimization

Performance work should be guided by measurements rather than assumptions.

The intended optimization cycle is:

```text
Build
  ↓
Measure
  ↓
Profile
  ↓
Refactor
  ↓
Re-measure
```

### Bounded Memory

Temporary buffers, candidate counts, OCR buffers, and chart processing structures should remain bounded wherever practical.

### Native Performance

Low-level image, matrix, and OCR operations are kept separate from high-level chart semantics so that performance-critical implementations can be optimized independently.

### FFI Stability

The C-compatible boundary should remain narrow and predictable.

Internal C++ modularization should not unnecessarily change the external ABI consumed by Rust or Python.

---

# 🔗 Rust FFI Integration

The native C interface is exposed through:

```text
include/ffi_bridge.h
src/ffi_bridge.cpp
```

A Rust integration can link the native static library:

```rust
#[link(name = "fin_ocr_native", kind = "static")]
unsafe extern "C" {
    fn fin_process_document(
        data: *const u8,
        length: usize,
    ) -> *const std::os::raw::c_char;

    fn fin_free_string(
        ptr: *const std::os::raw::c_char,
    );
}
```

A Rust wrapper can then convert the returned C string into an owned Rust `String`.

The FFI boundary should remain narrow and stable; internal C++ module organization should not leak unnecessarily into the external ABI.

---

# 📂 Dataset and Diagnostics

Example input data is kept under:

```text
data/
```

Ground-truth files are stored under:

```text
data/ground_truth/
```

Debug and diagnostic processing outputs are kept separate:

```text
debug_output/
diagnostic_output/
```

This allows OCR output, processed images, and expected/predicted text to be inspected without mixing them with source code.

---

# 🔬 Performance and Memory Analysis

The project contains:

```text
valgrind.supp
```

for Valgrind suppression handling.

Performance work should use the dedicated benchmark programs instead of relying exclusively on end-to-end application timing.

Important areas for measurement include:

```text
image preprocessing
connected components
glyph matching
OCR
chart candidate segmentation
chart label recognition
chart association
chart interpretation
tensor/image operations
```

Performance measurements should be interpreted together with input quality and workflow maturity.

---

# 📌 Current Development Status

The engine currently has three broad capability stages:

```text
┌─────────────────────────────────────────────────────────┐
│ JPEG TABULAR OCR                                        │
│                                                         │
│ Supported workflow                                      │
│ Typical quality range: ~70–90% for supported inputs     │
└─────────────────────────────────────────────────────────┘

                        ↓

┌─────────────────────────────────────────────────────────┐
│ FINANCIAL CHART OCR                                     │
│                                                         │
│ Early-stage workflow                                    │
│ X-axis label recognition currently the strongest area   │
└─────────────────────────────────────────────────────────┘

                        ↓

┌─────────────────────────────────────────────────────────┐
│ PDF OCR                                                 │
│                                                         │
│ Infrastructure present                                  │
│ Full OCR workflow still under development               │
└─────────────────────────────────────────────────────────┘
```

The project is being developed incrementally, with correctness, modularity, measurable performance, and stable FFI boundaries prioritized over prematurely claiming complete coverage.

---

# 📌 Architecture Status

The chart processing stack has been migrated from large monolithic chart classes toward explicit subsystem ownership:

```text
chart_color_isolator
        │
        ├── axis
        ├── coordinate
        ├── object
        ├── label
        ├── association
        └── interpreter
```

The label recognizer has likewise been decomposed into dedicated candidate, category, Y-axis, OCR, filtering, recovery, and output modules.

This structure is intended to make the engine easier to:

* test independently
* profile independently
* optimize independently
* maintain
* extend with new chart families
* isolate low-level performance work
* preserve stable external interfaces while internal implementations evolve
