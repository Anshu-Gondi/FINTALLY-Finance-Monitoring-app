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
  const [elapsedTime, setElapsedTime] = useState(0);

  const messagesEndRef = useRef(null);
  const timerRef = useRef(null);

  // Auto-scroll to bottom on new messages or status changes
  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages, systemStatus]);

  // Stopwatch for CPU execution tracking
  useEffect(() => {
    if (isGenerating) {
      setElapsedTime(0);
      timerRef.current = setInterval(() => {
        setElapsedTime((prev) => prev + 1);
      }, 1000);
    } else {
      clearInterval(timerRef.current);
    }
    return () => clearInterval(timerRef.current);
  }, [isGenerating]);

  const handleSendMessage = async (e) => {
    e.preventDefault();
    if (!input.trim() || isGenerating) return;

    const userMessage = input.trim();
    setInput("");
    setIsGenerating(true);
    setSystemStatus("INITIALIZING CONNECTION TO RUST BACKEND...");

    const userMsgId = Date.now().toString();
    const assistantMsgId = (Date.now() + 1).toString();

    setMessages((prev) => [
      ...prev,
      { id: userMsgId, role: "user", content: userMessage },
    ]);

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
          session_id: null,
          max_tokens: 512,
        }),
      });

      if (!response.ok) throw new Error(`HTTP_${response.status}`);

      setSystemStatus("EVALUATING PROMPT & GRAPH (CPU MODE)...");

      const reader = response.body.getReader();
      const decoder = new TextDecoder("utf-8");
      let buffer = "";

      while (true) {
        const { value, done } = await reader.read();
        if (done) break;

        buffer += decoder.decode(value, { stream: true });
        const lines = buffer.split("\n");
        buffer = lines.pop() || "";

        for (const line of lines) {
          const cleanLine = line.trim();
          if (!cleanLine.startsWith("data:")) continue;

          let rawPayload = cleanLine.replace(/^data:\s*/, "").trim();
          if (!rawPayload) continue;

          // Unescape explicit newlines from backend string conversion
          rawPayload = rawPayload.replace(/\\n/g, "\n");

          // Control Signal Handlers
          if (rawPayload === "[DONE]") {
            setSystemStatus("");
            break;
          }
          if (rawPayload === "[CONTEXT_LOADED]") {
            setSystemStatus("CONTEXT SNAPSHOT INJECTED into LLM...");
            continue;
          }
          if (rawPayload.startsWith("[TOOL_CALL:")) {
            currentToolCall = rawPayload.slice(11, -1);
            setSystemStatus(`RUNNING RUST TOOL ENGINE: ${currentToolCall.toUpperCase()}...`);
            continue;
          }
          if (rawPayload.startsWith("[TOOL_RESULT:")) {
            try {
              currentToolResult = JSON.parse(rawPayload.slice(13, -1));
              setSystemStatus("TOOL DATA RECEIVED. SYNTHESIZING RESPONSE...");
            } catch (err) {
              console.error("Tool result parse error:", err);
            }
            continue;
          }
          if (rawPayload.startsWith("[ERROR:")) {
            setSystemStatus(`CORE FAILURE: ${rawPayload.slice(7, -1)}`);
            continue;
          }

          // Accumulate Model Text Output
          currentAssistantText += rawPayload;
          setSystemStatus("STREAMING RESPONSE TO CLIENT...");

          setMessages((prev) => {
            const filtered = prev.filter((m) => m.id !== assistantMsgId);
            return [
              ...filtered,
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
        {
          id: Date.now().toString(),
          role: "assistant",
          content: `CRITICAL ERROR: ${err.message}`,
        },
      ]);
    } finally {
      setIsGenerating(false);
      setSystemStatus("");
    }
  };

  const renderToolDataCard = (toolName, result) => {
    if (!result) return null;
    if (result.error) {
      return (
        <div className="chatbot-tool-card error-card">
          CALC_ERR // {result.error}
        </div>
      );
    }

    switch (toolName) {
      case "calculate_emi":
        return (
          <div className="chatbot-tool-card emi-card">
            <div className="tool-card-title">📊 EMI BREAKDOWN DATA</div>
            <div className="tool-metric-row">
              <span>MONTHLY EMI:</span>
              <strong className="neon-text-green">
                ₹{result.emi?.toLocaleString("en-IN")}
              </strong>
            </div>
            <div className="tool-metric-row">
              <span>TOTAL INTEREST:</span>
              <span className="neon-text-blue">
                ₹{result.total_interest?.toLocaleString("en-IN")}
              </span>
            </div>
          </div>
        );
      case "emergency_fund":
        return (
          <div className="chatbot-tool-card emergency-card">
            <div className="tool-card-title">🛡️ CONTINGENCY RESERVE</div>
            <div className="giant-neon-value">
              ₹{result.recommended_fund?.toLocaleString("en-IN")}
            </div>
            <small className="label">RECOMMENDED RESERVE CAPACITY</small>
          </div>
        );
      default:
        return (
          <div className="chatbot-tool-card generic-card">
            <details>
              <summary>⚡ METRIC TRACE ({toolName})</summary>
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
            <span
              className={`live-dot ${isGenerating ? "processing" : ""}`}
            ></span>
            <h2 className="chatbot-title">FINTALLY_CORE_ASSISTANT v1.02</h2>
          </div>
          <span className={`engine-badge ${isGenerating ? "busy" : ""}`}>
            {isGenerating ? `CPU_BUSY [${elapsedTime}s]` : "RUST_ENGINE_ACTIVE"}
          </span>
        </div>

        {/* Messages Viewport */}
        <div className="chatbot-messages-viewport">
          {messages.map((msg) => (
            <div key={msg.id} className={`chatbot-msg-row ${msg.role}`}>
              <div className="chatbot-msg-bubble">
                <div className="msg-meta-tag">
                  {msg.role.toUpperCase()} {"//"}
                </div>
                <div className="msg-body-content">{msg.content}</div>
                {msg.toolCalled && msg.toolResult && (
                  <div className="tool-card-mount-point">
                    {renderToolDataCard(msg.toolCalled, msg.toolResult)}
                  </div>
                )}
              </div>
            </div>
          ))}

          {/* Always Display Status Indicator When Generating */}
          {isGenerating && (
            <div className="chatbot-status-bar animate-pulse">
              <span className="status-loader">&gt;&gt;</span>{" "}
              {systemStatus || "PROCESSING QUERY ON CPU..."}{" "}
              <span className="status-timer">({elapsedTime}s)</span>
            </div>
          )}
          <div ref={messagesEndRef} />
        </div>

        {/* Input Dock */}
        <form onSubmit={handleSendMessage} className="chatbot-form-input-dock">
          <input
            type="text"
            className="neonInput"
            value={input}
            onChange={(e) => setInput(e.target.value)}
            placeholder="ENTER FINANCIAL QUERY..."
            disabled={isGenerating}
            maxLength={2000}
          />
          <button
            type="submit"
            className="chatbot-submit-btn"
            disabled={isGenerating || !input.trim()}
          >
            {isGenerating ? `${elapsedTime}s...` : "EXECUTE"}
          </button>
        </form>
      </div>
    </div>
  );
};

export default ChatbotPage;
