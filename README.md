# FINTALLY - Finance Monitoring App 💰

**A high-performance full-stack finance monitoring system powered by FastAPI, React, and Rust (PyO3).**

* **Author:** Anshu Gondi
* **Repository:** https://github.com/Anshu-Gondi/FINTALLY-Finance-Monitoring-app
* **Status:** Active Development 🚀

---

## 📱 Project Overview

**FINTALLY** is a full-stack finance monitoring platform designed for:

* Expense & income tracking
* Budget management
* EMI planning
* AI-powered financial insights

The system is optimized for **performance-critical workloads** using a Rust core integrated via PyO3.

---

## ⚠️ Migration Notice (IMPORTANT)

This project has been **fully migrated from a multi-backend architecture (Node.js + Django + C++) to a unified FastAPI backend**.

### Why this change?

* Reduced architectural complexity
* Better async performance (FastAPI)
* Cleaner integration with Rust (PyO3)
* Easier deployment and scaling

👉 Legacy Node.js and Django services have been **removed**.

---

## 🏗️ Current Architecture

Frontend (React)
→ FastAPI Backend (Python)
→ Rust Core (PyO3 bindings)
→ Database (MongoDB / future extensible)

---

## 🛠️ Technology Stack

### Frontend

* React (Vite)
* Chart.js, Recharts
* pnpm

### Backend (Unified)

* FastAPI (Python)
* Async-first architecture
* Modular routers (auth, transaction, analytics, chatbot)

### Rust Core (PyO3) 🚀

Location: `chatbot-backend/fintally_core/`

* High-performance financial computations
* LLM + analytics engine
* Memory-safe, CPU-efficient execution
* Python bindings via PyO3

---

## 📁 Project Structure (Simplified)

```
frontend/
chatbot-backend/
  ├── main.py
  ├── routers/
  ├── services/
  ├── schemas/
  ├── fintally_core/   # Rust (PyO3)
```

---

## 🚀 Installation

### 1. Clone

```bash
git clone https://github.com/Anshu-Gondi/FINTALLY-Finance-Monitoring-app.git
cd FINTALLY-Finance-Monitoring-app
```

---

### 2. Backend (FastAPI)

```bash
cd chatbot-backend
python -m venv venv
venv\Scripts\activate   # Windows

pip install -r requirements.txt
```

---

### 3. Rust Setup (REQUIRED for PyO3)

Install Rust:
https://rustup.rs/

Verify:

```bash
rustc --version
```

Build PyO3 modules:

```bash
cd fintally_core
maturin develop
```

---

### 4. Run Backend

```bash
cd chatbot-backend
uvicorn main:app --reload
```

---

### 5. Frontend

```bash
cd frontend
pnpm install
pnpm dev
```

---

## ⚡ Performance Focus

* Rust core for compute-heavy operations
* Async FastAPI for I/O efficiency
* Minimal overhead architecture

---

## 📌 Notes

* Ensure Rust toolchain is installed before running PyO3 modules
* Virtual environment recommended (venv / conda)
* Docker setup is planned for future releases

---

## 🎯 Future Enhancements

* Dockerized deployment
* GPU acceleration for ML
* Advanced financial analytics
* Real-time streaming

---

## 📄 License

MIT License

---

## 👤 Author

Anshu Gondi
GitHub: https://github.com/Anshu-Gondi

---

**Version:** 3.0 (FastAPI + Rust Unified Architecture)
