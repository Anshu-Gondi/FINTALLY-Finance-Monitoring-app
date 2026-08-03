import Navbar from "../../Shared Components/Navbar/Navbar";
import Footer from "../../Shared Components/Footer/Footer";
import profileImg from "../../assets/profile-preview.jpeg";
import "./About.css";

export default function About() {
  return (
    <div className="about-bg min-h-screen flex flex-col text-white">
      <Navbar />

      <main className="about-section flex-1 px-6 py-12">
        <div className="about-container max-w-6xl mx-auto">

          {/* Hardware Evolution Timeline Banner */}
          <div className="hardware-badge-banner">
            <div className="hardware-timeline-grid">
              <div className="hardware-node legacy">
                <span className="node-tag">PREVIOUS RIG (2022 – JULY 2026)</span>
                <div className="hardware-spec-text">Intel Celeron N4020 · 2C/2T · 8GB RAM · CPU-Only</div>
                <p className="node-desc">Where FinTally was born (Sept 2025). Built under extreme constraints.</p>
              </div>
              <div className="hardware-arrow">➔</div>
              <div className="hardware-node current">
                <span className="node-tag neon-text-cyan">CURRENT RIG (UPGRADED JULY 17, 2026)</span>
                <div className="hardware-spec-text highlight-spec">Intel i5-14450HX (16 Threads) · RTX 4050 (6GB VRAM)</div>
                <p className="node-desc">High-throughput CUDA acceleration + multithreaded native execution.</p>
              </div>
            </div>
          </div>

          {/* Core Profile Overview */}
          <div className="about-hero-grid mt-8">
            <div className="about-image">
              <img src={profileImg} alt="Anshu Gondi" className="profile-img" />
              <div className="social-links-dock">
                <a
                  href="https://www.linkedin.com/in/anshu-gondi-5a53a1350"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="social-btn linkedin-btn"
                >
                  LinkedIn
                </a>
                <a
                  href="https://www.youtube.com/@ag_youtube"
                  target="_blank"
                  rel="noopener noreferrer"
                  className="social-btn youtube-btn"
                >
                  YouTube
                </a>
                <a
                  href="mailto:agondi982@gmail.com"
                  className="social-btn email-btn"
                >
                  Email
                </a>
              </div>
            </div>

            <div className="about-content">
              <h1 className="neon-title">Anshu Gondi</h1>
              <p className="subtitle-tag font-mono text-cyan-400">
                Full-Stack Engineer with Systems Depth // React to Rust
              </p>
              <p className="status-tag font-mono text-xs text-gray-400 mb-4">
                B.Tech 1st Year · India · Open for Full-Stack, Backend & Systems Engineering Internships
              </p>

              <p className="about-text">
                Hi, I’m <span className="highlight">Anshu Gondi</span> — the creator of{" "}
                <span className="highlight">FinTally</span>. I ship complete products across frontend, backend, database layers, and native systems architecture in Rust & C++.
              </p>

              <p className="about-text">
                FinTally started back in <strong>September 2025</strong>, engineered from the ground up on a low-spec 2022 Celeron N4020. Upgraded on <strong>July 17, 2026</strong> to an <strong>i5-14450HX with RTX 4050 GPU</strong>, FinTally's pure Rust backend core now fully leverages multithreaded execution and high-performance native pipelines.
              </p>
            </div>
          </div>

          {/* Architecture Principles Grid */}
          <section className="about-card-section mt-12">
            <h2 className="section-neon-title">// How I Build</h2>
            <div className="grid grid-cols-1 md:grid-cols-2 gap-6 mt-6">
              <div className="neon-card">
                <h3 className="card-title">⚡ End-to-End Ownership</h3>
                <p className="card-desc">
                  React → Axum (Rust) → PostgreSQL / Redis / MinIO → Docker + GCP.
                </p>
              </div>
              <div className="neon-card">
                <h3 className="card-title">🎯 Deterministic Financial Logic</h3>
                <p className="card-desc">
                  Pure Rust owns all FinTally calculations, numerics, and aggregations. Zero interpreter overhead.
                </p>
              </div>
              <div className="neon-card">
                <h3 className="card-title">🏎️ Performance Under Constraint</h3>
                <p className="card-desc">
                  Profile first. Rust + C++ FFI for hot paths, backed by zero-cost abstractions and memory safety.
                </p>
              </div>
              <div className="neon-card">
                <h3 className="card-title">🛡️ Controlled ML Boundaries</h3>
                <p className="card-desc">
                  ML components built with Candle and custom math engines are strictly validated before touching core state.
                </p>
              </div>
            </div>
          </section>

          {/* Featured Projects Showcase */}
          <section className="about-card-section mt-12">
            <h2 className="section-neon-title">// Featured Projects</h2>
            <div className="projects-grid mt-6 gap-8 grid grid-cols-1">

              {/* FinTally (Primary Focus - Pure Rust Stable) */}
              <div className="project-neon-card primary-featured">
                <div className="project-header">
                  <h3 className="project-title">💰 FinTally <span className="flag-tag">[PRIMARY PLATFORM]</span></h3>
                  <span className="project-badge">Pure Rust Stable + Active Dev</span>
                </div>
                <p className="project-desc">
                  FinTally is my flagship personal finance platform built with a <strong>Pure Rust (Axum + Tokio + Candle)</strong> backend stack. It delivers absolute memory safety, lightning-fast native speeds, and strict separation between deterministic financial logs and intelligence engines.
                </p>

                <div className="dev-expansion-box mt-4 p-4 rounded-lg bg-black/40 border border-cyan-500/30">
                  <h4 className="text-cyan-400 font-bold text-sm mb-2">🚀 Active Development Roadmap & Testing:</h4>
                  <ul className="list-disc list-inside text-gray-300 text-xs space-y-1">
                    <li><strong>Custom OCR Engine:</strong> Building a dedicated receipt and invoice text-extraction pipeline from scratch for offline processing.</li>
                    <li><strong>Advanced Math Tools:</strong> Integrating specialized mathematical computation modules for rigorous forecasting and statistical analysis.</li>
                    <li><strong>Comprehensive Testing:</strong> Rigorous unit, integration, and concurrency testing suites across all numerical and parsing engines.</li>
                  </ul>
                </div>

                <div className="benchmark-table-wrapper my-4">
                  <table className="benchmark-table">
                    <thead>
                      <tr>
                        <th>Operation (10k items)</th>
                        <th>Interpreted Legacy Baseline</th>
                        <th>Pure Rust Backend</th>
                        <th>Performance Gain</th>
                      </tr>
                    </thead>
                    <tbody>
                      <tr>
                        <td>Aggregation</td>
                        <td>1.2 s</td>
                        <td className="neon-text-green font-bold">0.04 s</td>
                        <td className="neon-text-cyan font-bold">30× Faster</td>
                      </tr>
                      <tr>
                        <td>Monthly Summary</td>
                        <td>0.8 s</td>
                        <td className="neon-text-green font-bold">0.025 s</td>
                        <td className="neon-text-cyan font-bold">32× Faster</td>
                      </tr>
                    </tbody>
                  </table>
                </div>
                <p className="tech-stack-text">
                  <strong>Stack:</strong> React, Axum (Rust), Tokio, Candle, PostgreSQL, Docker, C++ (FFI)
                </p>
              </div>

              {/* CampusVision (Other Project) */}
              <div className="project-neon-card secondary-project">
                <div className="project-header">
                  <h3 className="project-title">🏫 CampusVision <span className="flag-tag">[OTHER PROJECT]</span></h3>
                  <span className="project-badge dev">In Development</span>
                </div>
                <p className="project-desc">
                  A full-stack attendance and computer-vision platform featuring a three-stage anti-fraud verification pipeline (YuNet/SFace Face Recognition → Emotion-8-FerPlus Liveness Check → CCTV Cross-Verification). Utilizes RTX 4050 GPU CUDA acceleration for real-time video batching.
                </p>
                <p className="tech-stack-text mt-4">
                  <strong>Stack:</strong> React Native, React, Axum (Rust), C++ (FFI), HNSW Index, ONNX Runtime (CUDA Execution Provider), MinIO, Docker
                </p>
              </div>

            </div>
          </section>

          {/* Technical Stack Section */}
          <section className="about-card-section my-12">
            <h2 className="section-neon-title">// Technical Stack</h2>
            <div className="stack-grid grid grid-cols-1 md:grid-cols-3 gap-4 mt-6">
              <div className="stack-box">
                <span className="stack-label">Languages</span>
                <p>Rust · C++ · TypeScript · JavaScript · SQL · CUDA</p>
              </div>
              <div className="stack-box">
                <span className="stack-label">Frontend & Mobile</span>
                <p>React · React Native · TailwindCSS</p>
              </div>
              <div className="stack-box">
                <span className="stack-label">Backend & Systems</span>
                <p>Axum · Actix Web · Tokio · Node.js · C++ FFI</p>
              </div>
              <div className="stack-box">
                <span className="stack-label">Native & Acceleration</span>
                <p>Candle · ONNX Runtime (CUDA) · HNSW · TensorRT · opencv-rs</p>
              </div>
              <div className="stack-box">
                <span className="stack-label">Data & Infrastructure</span>
                <p>PostgreSQL · Redis · MinIO · Docker · GCP · Linux (WSL2)</p>
              </div>
              <div className="stack-box">
                <span className="stack-label">Engineering Ethos</span>
                <p className="italic text-cyan-400">Master constraints. Leverage modern hardware. Refactor intentionally.</p>
              </div>
            </div>
          </section>

        </div>
      </main>

      <Footer />
    </div>
  );
}
