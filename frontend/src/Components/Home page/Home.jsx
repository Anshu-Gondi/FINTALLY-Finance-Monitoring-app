import Navbar from "../../Shared Components/Navbar/Navbar";
import Footer from "../../Shared Components/Footer/Footer";
import chatbotPreview from "../../assets/chatbot-preview.png";
import { Link } from "react-router-dom";
import "./Home.css";

export default function Home() {
    return (
        <div className="home-bg min-h-screen flex flex-col text-white">
            <Navbar />

            {/* Hero Section */}
            <section className="flex-1 flex flex-col items-center justify-center text-center px-4 animate-fadeInUp hero-section">
                <h1 className="hero-title neon-glow">
                    Track Your Finances Like a Pro
                </h1>
                <p className="hero-subtitle fade-delay">
                    Manage your income, expenses, and insights in one sleek neon interface.
                    Stay on top of your money with smart analytics and easy tracking.
                </p>
                <div className="flex gap-4 fade-delay-2">
                    <Link to="/signup" className="neon-btn-primary">
                        Get Started
                    </Link>
                    <Link to="/login" className="neon-btn-outline">
                        Login
                    </Link>
                </div>
            </section>

            {/* Features Section */}
            <section className="py-16 px-6 text-center features-section">
                <h2 className="section-title neon-glow">Why Use FinTally?</h2>
                <div className="grid md:grid-cols-3 gap-8 max-w-6xl mx-auto">
                    <div className="feature-card">
                        <h3 className="text-xl font-semibold mb-4">💰 Expense Tracking</h3>
                        <p className="text-gray-300">
                            Keep tabs on every transaction with real-time updates and clear categorization.
                        </p>
                    </div>
                    <div className="feature-card">
                        <h3 className="text-xl font-semibold mb-4">📊 Smart Insights</h3>
                        <p className="text-gray-300">
                            AI-powered analytics help you understand spending patterns and save smarter.
                        </p>
                    </div>
                    <div className="feature-card">
                        <h3 className="text-xl font-semibold mb-4">🔒 Secure & Private</h3>
                        <p className="text-gray-300">
                            Your financial data is encrypted and protected, giving you peace of mind.
                        </p>
                    </div>
                </div>
            </section>

            {/* Cold Start Notice */}
            <section className="cold-start-notice text-center py-6 px-4">
                <p className="text-gray-400 max-w-3xl mx-auto text-sm leading-relaxed">
                    ⚡ <span className="text-[#00f0ff] font-semibold">Heads up!</span>
                    Since FinTally is currently running on a free hosting plan, the server may take
                    <span className="text-[#00f0ff]"> 2–5 minutes </span> to start after being inactive for a while.
                    This helps us keep the app free for everyone in the early stages without charging any subscription fees.
                    Once it’s warmed up, it will run at full speed 🚀
                </p>
            </section>

            {/* Chatbot Future Feature Section */}
            <section className="py-16 px-6 text-center chatbot-section">
                <h2 className="section-title neon-glow">🚀 Coming Soon: FinTally AI Chatbot</h2>
                <p className="text-gray-300 max-w-3xl mx-auto mt-4 mb-8">
                    We’re working on an AI-powered financial assistant that will help you manage your money,
                    answer your questions, and give smart recommendations in real-time.
                    Until then, enjoy our <Link to="/insights" className="text-[#00f0ff] hover:underline">Insights</Link> and <Link to="/transactions" className="text-[#00f0ff] hover:underline">Transactions</Link> pages!
                </p>
                <img
                    src={chatbotPreview}
                    alt="Chatbot Preview"
                    className="chatbot-img mx-auto"
                />
            </section>

            {/* Call to Action */}
            <section className="cta-section text-center">
                <h2 className="cta-title neon-glow">
                    Ready to take control of your money?
                </h2>
                <Link to="/signup" className="neon-btn-primary animate-pulse">
                    Join Now
                </Link>
            </section>

            <Footer />
        </div>
    );
}
