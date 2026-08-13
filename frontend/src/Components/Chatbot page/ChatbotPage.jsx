import Navbar from "../../Shared Components/Navbar/Navbar";
import Footer from "../../Shared Components/Footer/Footer";
import { useState, useEffect, useRef } from "react";
import ReactMarkdown from "react-markdown";
import remarkGfm from "remark-gfm";
import remarkMath from "remark-math";
import rehypeKatex from "rehype-katex";
import "katex/dist/katex.min.css"; // Required for rendering math formulas
import { chatApi } from "../../services/api";
import "./Chatbot.css";

// LaTeX preprocessor to convert bracketed formulas into KaTeX blocks
const preprocessMarkdown = (content) => {
  if (!content) return "";

  let cleaned = content;

  if (cleaned.trim().startsWith("{")) {
    try {
      const parsed = JSON.parse(cleaned);
      if (parsed.conversational_response) {
        cleaned = parsed.conversational_response;
      }
    } catch (e) {
      cleaned = cleaned
        .replace(/\{\s*"thought"\s*:\s*".*?"\s*,\s*"tool_call"\s*:\s*null\s*,\s*"conversational_response"\s*:\s*"/gs, "")
        .replace(/^\{\s*"thought"[\s\S]*?"conversational_response"\s*:\s*"/i, "")
        .replace(/"\s*\}$/, "");
    }
  }

  return cleaned
    .replace(/\\\[\s*([\s\S]*?)\s*\\\]/g, "\n\n$$$1$$\n\n")
    .replace(/\\\(\s*([\s\S]*?)\s*\\\)/g, "$$1$")
    .replace(/^[ \t]*\\+[ \t]*$/gm, "")
    .replace(/\\+(\s*(\$\$|\$))/g, "$1")
    .replace(/\[\s*([\s\S]*?(?:\\times|\\frac|\\text|=|\\approx|\^|\/|\+|\{|\})[\s\S]*?)\s*\]/g, "\n\n$$$1$$\n\n")
    .replace(/\(\s*([a-zA-Z0-9])\s*\)/g, "$$$1$");
};

const ChatbotPage = () => {
  const [messages, setMessages] = useState([
    {
      id: "welcome",
      role: "assistant",
      content: "SYSTEM ONLINE // FINTALLY AI READY. ENTER FINANCIAL QUERY OR SELECT A QUICK COMMAND BELOW_",
    },
  ]);
  const [input, setInput] = useState("");
  const [isGenerating, setIsGenerating] = useState(false);
  const [systemStatus, setSystemStatus] = useState("");
  const [elapsedTime, setElapsedTime] = useState(0);
  const [showGuidelines, setShowGuidelines] = useState(false);

  // ── Session & History State Management ──────────────────────────────────────
  const [sessions, setSessions] = useState([]);
  const [activeSessionId, setActiveSessionId] = useState(null);
  const [showHistoryDrawer, setShowHistoryDrawer] = useState(false);

  const messagesEndRef = useRef(null);
  const timerRef = useRef(null);

  // Initial session listing load
  useEffect(() => {
    fetchSessions();
  }, []);

  const fetchSessions = async () => {
    try {
      const data = await chatApi.getSessions();
      setSessions(data.sessions || []);
    } catch (err) {
      console.error("[CHAT_HISTORY] Failed to load sessions:", err);
    }
  };

  const handleSelectSession = async (sessionId) => {
    if (isGenerating) return;
    setActiveSessionId(sessionId);
    setSystemStatus(`LOADING SESSION #${sessionId}...`);

    try {
      const data = await chatApi.getHistory(sessionId, 50);
      if (data.history && data.history.length > 0) {
        setMessages(
          data.history.map((m, idx) => ({
            id: `hist_${idx}_${Date.now()}`,
            role: m.role,
            content: m.content,
            toolCalled: m.metadata?.tool_called,
            toolResult: m.metadata?.tool_result,
          }))
        );
      } else {
        setMessages([
          {
            id: "welcome",
            role: "assistant",
            content: `SESSION #${sessionId} LOADED. NO PRIOR MESSAGES FOUND.`,
          },
        ]);
      }
    } catch (err) {
      console.error("[CHAT_HISTORY] Load error:", err);
    } finally {
      setSystemStatus("");
    }
  };

  const handleStartNewChat = () => {
    if (isGenerating) return;
    setActiveSessionId(null);
    setMessages([
      {
        id: "welcome",
        role: "assistant",
        content: "NEW CHAT SESSION INITIALIZED. ENTER FINANCIAL QUERY_",
      },
    ]);
  };

  const handleDeleteSession = async (e, sessionId) => {
    e.stopPropagation();
    if (isGenerating) return;

    try {
      await chatApi.deleteSession(sessionId);
      setSessions((prev) => prev.filter((s) => s !== sessionId));
      if (activeSessionId === sessionId) {
        handleStartNewChat();
      }
    } catch (err) {
      console.error("[CHAT_HISTORY] Delete session failed:", err);
    }
  };

  const handleClearAllHistory = async () => {
    if (isGenerating) return;
    if (!window.confirm("ARE YOU SURE YOU WANT TO PURGE ALL CHAT SESSIONS?")) return;

    try {
      await chatApi.clearHistory();
      setSessions([]);
      handleStartNewChat();
    } catch (err) {
      console.error("[CHAT_HISTORY] Clear all history failed:", err);
    }
  };

  // Auto-scroll to bottom
  useEffect(() => {
    messagesEndRef.current?.scrollIntoView({ behavior: "smooth" });
  }, [messages, systemStatus]);

  // Generation timer
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

  const handleSendMessage = async (e, customPrompt = null) => {
    if (e) e.preventDefault();
    const userMessage = (customPrompt || input).trim();
    if (!userMessage || isGenerating) return;

    setInput("");
    setIsGenerating(true);
    setSystemStatus("INITIALIZING CONNECTION TO RUST BACKEND...");

    const userMsgId = Date.now().toString();
    const assistantMsgId = (Date.now() + 1).toString();

    setMessages((prev) => [
      ...prev,
      { id: userMsgId, role: "user", content: userMessage },
    ]);

    let rawAccumulator = "";
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
          session_id: activeSessionId,
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
        buffer += decoder.decode(value, { stream: !done });

        if (done) break;

        const lines = buffer.split("\n");
        buffer = lines.pop() || "";

        for (const line of lines) {
          const cleanLine = line.trim();
          if (!cleanLine.startsWith("data:")) continue;

          let rawPayload = cleanLine.replace(/^data:\s*/, "").trim();
          if (!rawPayload) continue;

          rawPayload = rawPayload.replace(/\\n/g, "\n");

          if (rawPayload === "[DONE]") {
            setSystemStatus("");
            break;
          }
          if (rawPayload === "[CONTEXT_LOADED]") {
            setSystemStatus("CONTEXT SNAPSHOT INJECTED into LLM...");
            continue;
          }
          if (rawPayload.startsWith("[TOOL_CALL:")) {
            currentToolCall = rawPayload.slice(11, -1).trim();
            setSystemStatus(`RUNNING RUST TOOL ENGINE: ${currentToolCall.toUpperCase()}...`);
            continue;
          }
          if (rawPayload.startsWith("[TOOL_RESULT:")) {
            try {
              currentToolResult = JSON.parse(rawPayload.slice(13, -1).trim());
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

          rawAccumulator += rawPayload;
          setSystemStatus("STREAMING RESPONSE TO CLIENT...");

          setMessages((prev) => {
            const filtered = prev.filter((m) => m.id !== assistantMsgId);
            return [
              ...filtered,
              {
                id: assistantMsgId,
                role: "assistant",
                content: rawAccumulator,
                toolCalled: currentToolCall,
                toolResult: currentToolResult,
              },
            ];
          });
        }
      }

      // Refresh sessions list after successful response
      fetchSessions();
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
      return <div className="chatbot-tool-card error-card">CALC_ERR // {result.error}</div>;
    }

    switch (toolName) {
      case "calculate_emi":
        return (
          <div className="chatbot-tool-card emi-card">
            <div className="tool-card-title">📊 EMI BREAKDOWN DATA</div>
            <div className="tool-metric-row">
              <span>MONTHLY EMI:</span>
              <strong className="neon-text-green">₹{result.monthly_emi?.toLocaleString("en-IN")}</strong>
            </div>
            <div className="tool-metric-row">
              <span>TOTAL REPAYMENT:</span>
              <span className="neon-text-blue">₹{result.total_repayment?.toLocaleString("en-IN")}</span>
            </div>
          </div>
        );
      case "emergency_fund":
        return (
          <div className="chatbot-tool-card emergency-card">
            <div className="tool-card-title">🛡️ EMERGENCY FUND RECOMMENDATION</div>
            <div className="tool-metric-row">
              <span>MINIMUM COVERAGE (6 Mo):</span>
              <strong className="neon-text-green">₹{result.recommended_minimum_size?.toLocaleString("en-IN")}</strong>
            </div>
            <div className="tool-metric-row">
              <span>OPTIMAL COVERAGE (12 Mo):</span>
              <span className="neon-text-blue">₹{result.recommended_optimal_size?.toLocaleString("en-IN")}</span>
            </div>
          </div>
        );
      case "generate_investment_plan":
        return (
          <div className="chatbot-tool-card strategy-card">
            <div className="tool-card-title">📈 PORTFOLIO ALLOCATION STRATEGY</div>
            {result.allocation && (
              <div className="tool-metric-group">
                <div className="tool-metric-row">
                  <span>EQUITY:</span> <strong>{result.allocation.equity}</strong>
                </div>
                <div className="tool-metric-row">
                  <span>DEBT:</span> <strong>{result.allocation.debt}</strong>
                </div>
                <div className="tool-metric-row">
                  <span>GOLD:</span> <strong>{result.allocation.gold}</strong>
                </div>
              </div>
            )}
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
    <>
      <Navbar />
      <div className="chatbot-page-wrapper">
        <div className="chatbot-box-wrapper">
          {/* Header Panel */}
          <div className="chatbot-window-header">
            <div className="header-title-block">
              <span className={`live-dot ${isGenerating ? "processing" : ""}`}></span>
              <h2 className="chatbot-title">
                {activeSessionId ? `SESSION #${activeSessionId}` : "FINTALLY_CORE_ASSISTANT v1.02"}
              </h2>
            </div>
            <div className="header-actions">
              <button
                type="button"
                className="history-toggle-btn"
                onClick={() => setShowHistoryDrawer(!showHistoryDrawer)}
              >
                {showHistoryDrawer ? "✖ CLOSE HISTORY" : "📜 HISTORY"}
              </button>
              <button
                type="button"
                className="guidelines-toggle-btn"
                onClick={() => setShowGuidelines(!showGuidelines)}
              >
                {showGuidelines ? "✖ CLOSE GUIDELINES" : "📖 LLM GUIDELINES"}
              </button>
              <span className={`engine-badge ${isGenerating ? "busy" : ""}`}>
                {isGenerating ? `CPU_BUSY [${elapsedTime}s]` : "RUST_ENGINE_ACTIVE"}
              </span>
            </div>
          </div>

          {/* Chat Sessions History Drawer */}
          {showHistoryDrawer && (
            <div className="chat-history-drawer">
              <div className="history-drawer-header">
                <span className="history-drawer-title">🗄️ CHAT SESSIONS LOG</span>
                <div className="history-drawer-actions">
                  <button onClick={handleStartNewChat} className="new-chat-btn">
                    + NEW SESSION
                  </button>
                  <button onClick={handleClearAllHistory} className="purge-all-btn">
                    PURGE ALL
                  </button>
                </div>
              </div>
              <div className="history-session-grid">
                {sessions.length === 0 ? (
                  <div className="no-history-text">NO STORED SESSIONS FOUND.</div>
                ) : (
                  sessions.map((sessId) => (
                    <div
                      key={sessId}
                      className={`session-chip ${activeSessionId === sessId ? "active" : ""}`}
                      onClick={() => handleSelectSession(sessId)}
                    >
                      <span className="session-chip-label">Session #{sessId}</span>
                      <button
                        className="session-delete-btn"
                        onClick={(e) => handleDeleteSession(e, sessId)}
                        title="Delete Session"
                      >
                        ✖
                      </button>
                    </div>
                  ))
                )}
              </div>
            </div>
          )}

          {/* LLM Guidelines Accordion/Drawer */}
          {showGuidelines && (
            <div className="llm-guidelines-drawer">
              <h3 className="guidelines-title">⚡ SYSTEM GUIDELINES & CAPABILITIES</h3>
              <div className="guidelines-grid">
                <div className="guideline-card">
                  <h4>💡 Prompting Guidelines</h4>
                  <ul>
                    <li>Specify exact numeric inputs for precise calculations.</li>
                    <li>For EMI: provide principal, annual interest rate, and tenure in months.</li>
                    <li>For Budgeting: mention monthly income and profile type.</li>
                  </ul>
                </div>
                <div className="guideline-card">
                  <h4>🛠️ Integrated Engine Tools</h4>
                  <ul>
                    <li><code>calculate_emi</code> - Loan EMI & repayment schedule</li>
                    <li><code>generate_budget</code> - Income allocation & savings</li>
                    <li><code>emergency_fund</code> - 6 to 12 month coverage analysis</li>
                    <li><code>calculate_tax</code> - Tax liability under Indian regimes</li>
                  </ul>
                </div>
                <div className="guideline-card">
                  <h4>📐 Math Formatting</h4>
                  <ul>
                    <li>Inline math uses <code>$variable$</code> formatting.</li>
                    <li>Block equations use standard KaTeX <code>$$ equation $$</code>.</li>
                  </ul>
                </div>
              </div>
            </div>
          )}

          {/* Viewport Display Panel */}
          <div className="chatbot-messages-viewport">
            {messages.map((msg) => (
              <div key={msg.id} className={`chatbot-msg-row ${msg.role}`}>
                <div className="chatbot-msg-bubble">
                  <div className="msg-meta-tag">{msg.role.toUpperCase()} {"//"}</div>

                  {msg.thought && (
                    <details className="thought-accordion">
                      <summary className="thought-summary">💭 INTERNAL REASONING</summary>
                      <p className="thought-text">{msg.thought}</p>
                    </details>
                  )}

                  {msg.toolCalled && !msg.toolResult && (
                    <div className="tool-running-badge">
                      <span className="pulse-dot"></span> EXECUTING ENGINE TOOL: <strong>{msg.toolCalled}</strong>
                    </div>
                  )}

                  <div className="msg-body-content markdown-container">
                    {msg.content ? (
                      <ReactMarkdown
                        remarkPlugins={[remarkGfm, remarkMath]}
                        rehypePlugins={[rehypeKatex]}
                      >
                        {preprocessMarkdown(msg.content)}
                      </ReactMarkdown>
                    ) : isGenerating && msg.role === "assistant" ? (
                      "Processing input data..."
                    ) : null}
                  </div>

                  {msg.toolCalled && msg.toolResult && (
                    <div className="tool-card-mount-point">
                      {renderToolDataCard(msg.toolCalled, msg.toolResult)}
                    </div>
                  )}
                </div>
              </div>
            ))}

            {isGenerating && (
              <div className="chatbot-status-bar animate-pulse">
                <span className="status-loader">&gt;&gt;</span>{" "}
                {systemStatus || "PROCESSING QUERY ON CPU..."}{" "}
                <span className="status-timer">({elapsedTime}s)</span>
              </div>
            )}
            <div ref={messagesEndRef} />
          </div>

          {/* Quick Command Suggestion Chips */}
          <div className="quick-commands-bar">
            <button
              disabled={isGenerating}
              onClick={(e) => handleSendMessage(e, "Calculate EMI for ₹5,00,000 loan at 8.5% interest for 36 months")}
            >
              📊 EMI Calculator
            </button>
            <button
              disabled={isGenerating}
              onClick={(e) => handleSendMessage(e, "Calculate recommended emergency fund for ₹35,000 monthly expense")}
            >
              🛡️ Emergency Fund
            </button>
            <button
              disabled={isGenerating}
              onClick={(e) => handleSendMessage(e, "Generate monthly budget for ₹65,000 income as a young professional")}
            >
              💼 Monthly Budget
            </button>
          </div>

          {/* Input Dock */}
          <form onSubmit={handleSendMessage} className="chatbot-form-input-dock">
            <input
              type="text"
              className="neonInput input"
              value={input}
              onChange={(e) => setInput(e.target.value)}
              placeholder="ENTER FINANCIAL QUERY..."
              disabled={isGenerating}
              maxLength={2000}
            />
            <button
              type="submit"
              className="chatbot-submit-btn button"
              disabled={isGenerating || !input.trim()}
            >
              {isGenerating ? `${elapsedTime}s...` : "EXECUTE"}
            </button>
          </form>
        </div>
      </div>
      <Footer />
    </>
  );
};

export default ChatbotPage;
