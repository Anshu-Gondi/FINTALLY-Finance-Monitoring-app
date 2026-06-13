// src/Components/Chatbot page/ChatbotPage.jsx
import { useState, useEffect, useRef } from "react";
import { chatApi } from "../../services/api";
import "./Chatbot.css";

const ChatbotPage = () => {
  const [messages, setMessages] = useState([
    {
      id: "welcome",
      role: "assistant",
      content: "SYSTEM ONLINE // FINTALLY AI READY. ENTER FINANCIAL QUERY_",
    },
  ]);
  const [input, setInput] = useState("");
  const [isGenerating, setIsGenerating] = useState(false);
  const [systemStatus, setSystemStatus] = useState(""); 
  
  const messagesEndRef = useRef(null);

  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages, systemStatus]);

  const handleSendMessage = async (e) => {
    e.preventDefault();
    if (!input.trim() || isGenerating) return;

    const userMessage = input.trim();
    setInput("");
    setIsGenerating(true);
    setSystemStatus("PINGING CORES...");

    setMessages((prev) => [...prev, { id: Date.now().toString(), role: "user", content: userMessage }]);

    const assistantMsgId = (Date.now() + 1).toString();
    let currentAssistantText = "";
    let currentToolCall = null;
    let currentToolResult = null;

    try {
      const token = localStorage.getItem("token");
      const response = await fetch(chatApi.getStreamUrl(), {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          ...(token ? { Authorization: `Bearer ${token}` } : {}),
        },
        body: JSON.stringify({
          message: userMessage,
          session_id: null, // Ready for tracking sessions when scaled
          max_tokens: 512,
        }),
      });

      if (!response.ok) throw new Error(`HTTP_${response.status}`);

      const reader = response.body.getReader();
      const decoder = new TextDecoder("utf-8");
      let buffer = "";

      while (true) {
        const { value, done } = await reader.read();
        if (done) break;

        buffer += decoder.decode(value, { stream: true });
        const lines = buffer.split("\n");
        buffer = lines.pop(); 

        for (const line of lines) {
          const cleanLine = line.trim();
          if (!cleanLine.startsWith("data:")) continue;
          
          const rawPayload = cleanLine.replace("data:", "").trim();
          if (!rawPayload) continue;

          if (rawPayload === "[DONE]") {
            setSystemStatus("");
            break;
          }
          if (rawPayload === "[CONTEXT_LOADED]") {
            setSystemStatus("SNAPSHOT INJECTED...");
            continue;
          }
          if (rawPayload.startsWith("[TOOL_CALL:")) {
            currentToolCall = rawPayload.slice(11, -1);
            setSystemStatus(`RUST_ENGINE_RUNNING: ${currentToolCall.toUpperCase()}...`);
            continue;
          }
          if (rawPayload.startsWith("[TOOL_RESULT:")) {
            try {
              currentToolResult = JSON.loads(rawPayload.slice(13, -1));
            } catch (err) {
              console.error("Payload breakdown fail", err);
            }
            continue;
          }
          if (rawPayload.startsWith("[ERROR:")) {
            setSystemStatus(`CORE_FLR: ${rawPayload.slice(7, -1)}`);
            continue;
          }

          currentAssistantText += rawPayload;
          setSystemStatus(""); 

          setMessages((prev) => {
            // Replaced unused 'filtered' assignment with a direct inline exclusion array mapping
            return [
              ...prev.filter((m) => m.id !== assistantMsgId),
              {
                id: assistantMsgId,
                role: "assistant",
                content: currentAssistantText,
                toolCalled: currentToolCall,
                toolResult: currentToolResult,
              },
            ];
          });
        }
      }
    } catch (err) {
      setMessages((prev) => [
        ...prev,
        { id: Date.now().toString(), role: "assistant", content: `NETWORK_CRASH: ${err.message}` },
      ]);
    } finally {
      setIsGenerating(false);
      setSystemStatus("");
    }
  };

  const renderToolDataCard = (toolName, result) => {
    if (!result) return null;
    if (result.error) return <div className="chatbot-tool-card error-card">CALC_ERR // {result.error}</div>;

    switch (toolName) {
      case "calculate_emi":
        return (
          <div className="chatbot-tool-card emi-card">
            <div className="tool-card-title">📊 EMI BREAKDOWN DATA</div>
            <div className="tool-metric-row">
              <span>MONTHLY EMI:</span>
              <strong className="neon-text-green">₹{result.emi?.toLocaleString('en-IN')}</strong>
            </div>
            <div className="tool-metric-row">
              <span>INTEREST COMPONENT:</span>
              <span className="neon-text-blue">₹{result.total_interest?.toLocaleString('en-IN')}</span>
            </div>
          </div>
        );
      case "emergency_fund":
        return (
          <div className="chatbot-tool-card emergency-card">
            <div className="tool-card-title">🛡️ CONTINGENCY RESERVE SIZE</div>
            <div className="giant-neon-value">₹{result.recommended_fund?.toLocaleString('en-IN')}</div>
            <small className="label">TARGET CAPACITY BASED ON EXPENSE MULTIPLIER</small>
          </div>
        );
      default:
        return (
          <div className="chatbot-tool-card generic-card">
            <details>
              <summary>⚡ METRIC_TRACE ({toolName})</summary>
              <pre>{JSON.stringify(result, null, 2)}</pre>
            </details>
          </div>
        );
    }
  };

  return (
    <div className="chatbot-page-wrapper">
      <div className="chatbot-box-wrapper">
        {/* Terminal Header */}
        <div className="chatbot-window-header">
          <div className="header-title-block">
            <span className="live-dot"></span>
            <h2 className="chatbot-title">FINTALLY_CORE_ASSISTANT v1.02</h2>
          </div>
          <span className="engine-badge">RUST_BINDINGS_ACTIVE</span>
        </div>

        {/* Output Screen */}
        <div className="chatbot-messages-viewport">
          {messages.map((msg) => (
            <div key={msg.id} className={`chatbot-msg-row ${msg.role}`}>
              <div className="chatbot-msg-bubble">
                <div className="msg-meta-tag">{msg.role.toUpperCase()} {"//"}</div>
                <div className="msg-body-content">{msg.content}</div>
                {msg.toolCalled && msg.toolResult && (
                  <div className="tool-card-mount-point">
                    {renderToolDataCard(msg.toolCalled, msg.toolResult)}
                  </div>
                )}
              </div>
            </div>
          ))}
          
          {systemStatus && (
            <div className="chatbot-status-bar animate-pulse">
              <span className="status-loader">&gt;&gt;</span> {systemStatus}
            </div>
          )}
          <div ref={messagesEndRef} />
        </div>

        {/* Input Interface */}
        <form onSubmit={handleSendMessage} className="chatbot-form-input-dock">
          <input
            type="text"
            className="neonInput"
            value={input}
            onChange={(e) => setInput(e.target.value)}
            placeholder="ENTER LOGICAL FIN-QUERY STRINGS..."
            disabled={isGenerating}
            maxLength={2000}
          />
          <button type="submit" className="chatbot-submit-btn" disabled={isGenerating || !input.trim()}>
            {isGenerating ? "..." : "EXECUTE"}
          </button>
        </form>
      </div>
    </div>
  );
};

export default ChatbotPage;