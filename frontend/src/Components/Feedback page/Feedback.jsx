import Navbar from "../../Shared Components/Navbar/Navbar";
import Footer from "../../Shared Components/Footer/Footer";
import { useState } from "react";
import "./Feedback.css";

const API_URL = import.meta.env.VITE_API_URL;

export default function Feedback() {
    const [formData, setFormData] = useState({ name: "", email: "", message: "" });
    const [submitted, setSubmitted] = useState(false);

    function handleChange(e) {
        setFormData({ ...formData, [e.target.name]: e.target.value });
    }

    function handleSubmit(e) {
        e.preventDefault();

        fetch(`${API_URL}/feedback`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify(formData)
        })
            .then((res) => res.json())
            .then((data) => {
                if (data.success) {
                    setSubmitted(true);
                    setFormData({ name: "", email: "", message: "" });
                } else {
                    alert(data.error || "Something went wrong");
                }
            })
            .catch((err) => {
                console.error("Error submitting feedback:", err);
                alert("Server error");
            });
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
                    ></textarea>

                    <button type="submit" className="feedback-btn">Send Feedback</button>

                    {submitted && (
                        <p className="feedback-success">✅ Thank you for your feedback!</p>
                    )}
                </form>
            </section>

            <Footer />
        </div>
    );
}
