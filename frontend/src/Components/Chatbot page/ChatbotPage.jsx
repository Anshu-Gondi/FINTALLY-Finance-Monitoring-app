import { useState, useRef, useEffect } from "react";
import "./Chatbot.css";

const API_URL = import.meta.env.VITE_API_URL;

function ChatbotPage() {
    const [messages, setMessages] = useState([
        { sender: "bot", text: "Hello! I'm your finance assistant. How can I help you today?" }
    ]);
    const [input, setInput] = useState("");
    const messagesEndRef = useRef(null);
    const [loading, setLoading] = useState(false);

    const token = localStorage.getItem("token");

    useEffect(() => {
        messagesEndRef.current?.scrollIntoView({ behavior: "smooth" });
    }, [messages]);

    const handleSend = async () => {
        if (!input.trim()) return;

        const userMessage = { sender: "user", text: input };
        setMessages(prev => [...prev, userMessage]);
        setInput("");
        setLoading(true);

        try {
            const res = await fetch(`${API_URL}/chat`, {
                method: "POST",
                headers: {
                    "Content-Type": "application/json",
                    ...(token ? { Authorization: `Bearer ${token}` } : {})
                },
                body: JSON.stringify({ query: input })
            });

            let data;
            try {
                data = await res.json();
            } catch (parseError) {
                console.error("❌ Failed to parse JSON:", parseError);
                const rawText = await res.text(); // ✅ await is inside async function now
                setMessages(prev => [
                    ...prev,
                    { sender: "bot", text: `⚠️ Invalid JSON from backend. Raw: ${rawText}` }
                ]);
                return;
            }

            console.log("📩 API Response:", data);

            const botReply =
                data.response ??
                data.message ??
                JSON.stringify(data, null, 2) ??
                "⚠️ No valid reply from backend.";

            setMessages(prev => [...prev, { sender: "bot", text: botReply }]);
        } catch (error) {
            console.error("❌ Chatbot API error:", error);
            setMessages(prev => [
                ...prev,
                { sender: "bot", text: `⚠️ Server error: ${error.message}` }
            ]);
        } finally {
            setLoading(false);
        }
    };

    const handleKeyPress = (e) => {
        if (e.key === "Enter") {
            handleSend();
        }
    };

    return (
        <div className="chatbot-container">
            <div className="chat-header">💬 Finance Assistant</div>

            <div className="chat-messages">
                {messages.map((msg, idx) => (
                    <div key={idx} className={`message ${msg.sender}`}>
                        {msg.text}
                    </div>
                ))}
                {loading && <div className="message bot typing">...</div>}
                <div ref={messagesEndRef}></div>
            </div>

            <div className="chat-input">
                <input
                    type="text"
                    placeholder="Ask me about your finances..."
                    value={input}
                    onChange={(e) => setInput(e.target.value)}
                    onKeyPress={handleKeyPress}
                />
                <button onClick={handleSend}>Send</button>
            </div>
        </div>
    );
}

export default ChatbotPage;
