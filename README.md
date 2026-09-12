# FINTALLY - Finance Monitoring App 💰

**A high-performance full-stack finance monitoring and AI insights system powered by Pure Rust, C++ FFI, and React.**

* **Author:** Anshu Gondi
* **Repository:** [https://github.com/Anshu-Gondi/FINTALLY-Finance-Monitoring-app](https://github.com/Anshu-Gondi/FINTALLY-Finance-Monitoring-app)
* **Status:** Active Development 🚀

---

## 📱 Project Overview

**FINTALLY** is a high-performance, privacy-focused finance monitoring platform engineered for ultra-fast analytics and AI-assisted financial planning:

* Expense & income tracking
* Smart budget management & EMI planning
* On-device RAG engine & local LLM chatbot
* Real-time financial analytics & math engine

---

## ⚠️ Architecture Evolution & Migration Notice

This project has been upgraded to a **pure Rust Cargo Workspace with C++ FFI integration** and an **Axum async web server**.

### Key Improvements

* **Zero Python Overhead:** Removed FastAPI, Python runtime, and PyO3 bindings in favor of a native, memory-safe Rust workspace.
* **Direct C++ FFI & CUDA Acceleration:** Native C++ integrations for vision engine execution, fast buffer handling, and CUDA-accelerated tensor computation (Candle).
* **Unified Workspace:** Monorepo architecture separating web API, database, finance math, vector search, and LLM chatbot engines into specialized crates.

---

## 🏗️ Architecture

```text
[ React JS Frontend (Vite + pnpm) ]
               │
               ▼ (HTTP / WebSockets)
[ Axum Web Server (`fintally_axum`) ]
               │
 ┌─────────────┼────────────────────────┬────────────────────────┐
 ▼             ▼                        ▼                        ▼
[`fintally_chatbot`]  [`fintally_finance`]  [`analytics_engine`]  [`fintally_db`]
 ├── RAG / Vector Engine (USearch)
 ├── LLM / Candle CUDA
 └── C++ FFI (Vision Engine & PDF Binarization)

```

---

## 🛠️ Technology Stack

### Frontend

* **Framework:** React JS (Vite)
* **Visualization:** Chart.js, Recharts
* **Package Manager:** `pnpm`

### Backend (`fintally_core` Workspace)

* **Web Framework:** Axum (`tokio` async runtime)
* **Machine Learning / AI:** Candle (`candle-core`, `candle-transformers` with CUDA enabled)
* **Vector Store & RAG:** USearch, custom hybrid chunking & TF-IDF/Cosine scoring
* **C++ Integration:** C++ FFI bindings (Vision engine, PDF processing, high-performance memory buffers)
* **Database:** `fintally_db`

---

## 🖥️ Development Hardware Specs

* **Current Dev Rig (Post July 17, 2026):**
* **CPU:** Intel Core i5-14450HX (14th Gen)
* **RAM:** 24 GB
* **GPU:** NVIDIA GeForce RTX 4050 (6 GB VRAM, 60W TGP)
* **Power Supply:** 175W


* **Legacy Dev Setup:**
* 4-year-old legacy hardware (used for primary development until July 17, 2026)



---

## 📁 Project Structure

```text
FINTALLY-Finance-Monitoring-app/
├── .github/
│   └── workflows/
│       ├── lint.yml
│       └── release-build.yml
├── fintally_core/                # Pure Rust Workspace Backend
│   ├── analytics_engine/        # High-performance financial analytics
│   ├── fintally_axum/            # Axum REST API & WebSocket service
│   ├── fintally_chatbot/        # RAG, LLM engine, USearch, C++ FFI
│   ├── fintally_db/             # Persistence & state management
│   ├── fintally_finance/        # Core budget, cashflow, & EMI rules
│   └── Cargo.toml                # Workspace root manifest
├── frontend/                    # React JS Frontend
├── .dockerignore
├── .gitignore
├── LICENSE                      # Apache 2.0 License
└── README.md

```

---

## 🚀 Getting Started

### Prerequisites

1. **Rust Toolchain:** Install via [rustup.rs](https://rustup.rs/) (edition 2021)
2. **Node.js & pnpm:** For frontend dependency management
3. **C++ Compiler & CUDA Toolkit** (Optional, required for GPU acceleration & FFI extensions)

---

### 1. Build and Run Backend (Rust)

```bash
# Navigate to the Rust workspace
cd fintally_core

# Run test suite (using cargo nextest or standard cargo test)
cargo test

# Run the Axum web server
cargo run -p fintally_axum --release

```

---

### 2. Build and Run Frontend (React)

```bash
# Navigate to frontend directory
cd frontend

# Install dependencies
pnpm install

# Start development server
pnpm dev

```

---

## ⚡ Performance & Quality Safeguards

* **Parallelized Test Runner:** Tested via `cargo-nextest` across all 5 workspace crates.
* **SIMD & Quantization:** Q4_0 and Q8_0 vector quantization for efficient memory utilization.
* **Release Profile:** LTO (`fat`), opt-level 3, binary stripping enabled for zero-cost abstraction runtime performance.

---

## 🗺️ Future Roadmap & Planned Features

* **Native Slint Frontend Rewrite:** Planning to rewrite the frontend using **Slint GUI framework (Rust/C++)** to completely eliminate the Web/Node runtime overhead and deliver native desktop performance.
* **Pure C Voice Detection Engine:** Adding an ultra-lightweight custom **pure C extension** for real-time audio voice detection and prompting into the local LLM engine.
* **Micro-Device Resource Target:** Architected to run with lightweight resource footprints optimized for **Raspberry Pi** or **ESP32** class hardware constraints.
* **Low-Latency SIMD Acceleration:** Leveraging modern x86 CPU SIMD instructions (**SSE4.2 through AVX-512**) across the Rust & C FFI extensions to guarantee near-zero output latency during voice signal processing and inference.

---

## 📄 License

Licensed under the **Apache License, Version 2.0** (the "License"); you may not use this file except in compliance with the License. You may obtain a copy of the License at:

[http://www.apache.org/licenses/LICENSE-2.0](http://www.apache.org/licenses/LICENSE-2.0)

Unless required by applicable law or agreed to in writing, software distributed under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the License for the specific language governing permissions and limitations under the License.

Copyright (c) 2026 **Anshu Gondi**.

---

## 👤 Author

**Anshu Gondi**

* GitHub: [@Anshu-Gondi](https://github.com/Anshu-Gondi)

---

**Version:** 4.0 (Pure Rust Workspace + C++ FFI + Axum + React JS)
