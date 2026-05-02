import Navbar from "../../Shared Components/Navbar/Navbar";
import Footer from "../../Shared Components/Footer/Footer";
import { useState } from "react";
import { feedbackApi } from "../../services/api";
import "./Feedback.css";

export default function Feedback() {
  const [formData, setFormData] = useState({ name: "", email: "", message: "" });
  const [submitted, setSubmitted] = useState(false);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(false);

  function handleChange(e) {
    setFormData({ ...formData, [e.target.name]: e.target.value });
  }

  async function handleSubmit(e) {
    e.preventDefault();
    setError("");
    setLoading(true);
    try {
      await feedbackApi.submit(formData.name, formData.email, formData.message);
      setSubmitted(true);
      setFormData({ name: "", email: "", message: "" });
    } catch (err) {
      setError(err.message || "Something went wrong");
    } finally {
      setLoading(false);
    }
  }

  return (
    <div className="feedback-bg min-h-screen flex flex-col text-white">
      <Navbar />

      <section className="feedback-section flex-1 px-6 py-12">
        <h1 className="neon-title text-center mb-6">We Value Your Feedback</h1>
        <p className="feedback-subtitle text-center">
          Your insights help us improve FinTally and make it even better for you.
        </p>

        <form className="feedback-form mx-auto mt-8" onSubmit={handleSubmit}>
          <input
            type="text"
            name="name"
            placeholder="Your Name"
            className="feedback-input"
            value={formData.name}
            onChange={handleChange}
            required
          />
          <input
            type="email"
            name="email"
            placeholder="Your Email"
            className="feedback-input"
            value={formData.email}
            onChange={handleChange}
            required
          />
          <textarea
            name="message"
            placeholder="Your Feedback"
            className="feedback-textarea"
            value={formData.message}
            onChange={handleChange}
            required
          />

          {error && <p className="has-text-danger mt-2">{error}</p>}

          <button type="submit" className="feedback-btn" disabled={loading}>
            {loading ? "Sending…" : "Send Feedback"}
          </button>

          {submitted && (
            <p className="feedback-success">✅ Thank you for your feedback!</p>
          )}
        </form>
      </section>

      <Footer />
    </div>
  );
}