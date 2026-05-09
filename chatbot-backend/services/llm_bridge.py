"""
services/llm_bridge.py

Orchestrates: FastAPI ↔ User Context ↔ TinyLlama ↔ Rust tools

Flow per request:
  1. Fetch user's real financial snapshot (analytics_service, concurrent)
  2. Build system prompt with that snapshot injected
  3. First LLM pass → detect tool call or plain response
  4. If tool call → execute via Rust fintally_chatbot bindings
  5. Second LLM pass → explain result in context of user's actual situation
  6. Stream tokens back

Key design: user_context is fetched ONCE per request, before the LLM.
            It costs ~one concurrent MongoDB round-trip and makes every
            single answer personalized with real numbers.
"""

import json
import re
import asyncio
import logging
from typing import AsyncGenerator, Optional

import python_llama
from services.user_context import get_user_context, format_context_for_prompt

logger = logging.getLogger(__name__)


# ──────────────────────────────────────────────────────────────────────────────
# Tool registry — mirrors tools.rs ToolName exactly
# ──────────────────────────────────────────────────────────────────────────────

TOOL_REGISTRY = {
    "calculate_emi": {
        "description": "Calculate monthly EMI",
        "required": ["principal", "annual_rate", "tenure_months"],
    },
    "assess_loan": {
        "description": "Assess loan eligibility",
        "required": ["request", "policy"],
    },
    "emergency_fund": {
        "description": "Calculate recommended emergency fund size",
        "required": ["monthly_expense"],
    },
    "savings_projection": {
        "description": "Project savings growth over months",
        "required": ["months"],
    },
    "calculate_tax": {
        "description": "Calculate tax on an amount",
        "required": ["amount", "profile"],
    },
    "generate_investment_plan": {
        "description": "Generate investment allocation plan",
        "required": ["investable_amount", "profile"],
    },
    "generate_cashflow": {
        "description": "Generate monthly cashflow allocation",
        "required": ["monthly_income", "profile"],
    },
    "generate_budget": {
        "description": "Generate a monthly budget",
        "required": ["monthly_income", "profile"],
    },
    "stat_analysis": {
        "description": "Analyze financial health and generate alerts",
        "required": ["profile"],
    },
}

# ──────────────────────────────────────────────────────────────────────────────
# Prompt builder
# ──────────────────────────────────────────────────────────────────────────────

_SYSTEM_BASE = """\
You are FinTally, a personal finance assistant for Indian users.
You speak plainly, use ₹ for amounts, and give practical advice.

When a user asks for a calculation, respond ONLY with a tool call in this exact format:
<tool_call>{"tool": "TOOL_NAME", "args": {ARGUMENTS}}</tool_call>

Available tools:
- calculate_emi: needs principal (number), annual_rate (number), tenure_months (integer)
- assess_loan: needs request (object: monthly_income, requested_emi, credit_score, purpose["Personal"/"Home"/"Education"/"Auto"], is_joint), policy ("salaried"/"self_employed")
- emergency_fund: needs monthly_expense (number)
- savings_projection: needs months (integer)
- calculate_tax: needs amount (number), profile ("salaried"/"self_employed")
- generate_investment_plan: needs investable_amount (number), profile ("young_professional"/"family_with_dependents"/"retiree_income_focused"/"single_parent")
- generate_cashflow: needs monthly_income (number), profile (same as above)
- generate_budget: needs monthly_income (number), profile (same as above)
- stat_analysis: needs profile (same as above)

For general questions, answer conversationally without a tool call.

Examples:
User: EMI for 5 lakh at 8.5% for 5 years?
Assistant: <tool_call>{"tool": "calculate_emi", "args": {"principal": 500000, "annual_rate": 8.5, "tenure_months": 60}}</tool_call>

User: How much emergency fund should I keep if I spend 35000/month?
Assistant: <tool_call>{"tool": "emergency_fund", "args": {"monthly_expense": 35000}}</tool_call>

User: What is ELSS?
Assistant: ELSS (Equity Linked Savings Scheme) is a tax-saving mutual fund with a 3-year lock-in period.\
"""


def _build_system_prompt(user_context: str) -> str:
    """Inject user's real financial snapshot into the system prompt."""
    ctx_block = format_context_for_prompt(user_context)
    return _SYSTEM_BASE + ctx_block


def _build_prompt(user_message: str, chat_history: list[dict], user_context: str) -> str:
    system = _build_system_prompt(user_context)
    parts = [f"<|system|>\n{system}\n</s>"]

    # Last 6 messages only — TinyLlama context window is tight (1024 tokens)
    for msg in chat_history[-6:]:
        tag = "<|user|>" if msg["role"] == "user" else "<|assistant|>"
        parts.append(f"{tag}\n{msg['content']}\n</s>")

    parts.append(f"<|user|>\n{user_message}\n</s>")
    parts.append("<|assistant|>")
    return "\n".join(parts)


def _build_explain_prompt(
    user_message: str,
    tool_name: str,
    tool_result: dict,
    chat_history: list[dict],
    user_context: str,
) -> str:
    """
    Second-pass prompt: LLM explains the tool result.
    Critically: user context is still in scope so the explanation
    can reference the user's actual situation.
    e.g. "Your EMI of ₹10,331 takes your total burden to 42% of income,
    which is in the High Risk zone per your current profile."
    """
    system = _build_system_prompt(user_context)
    result_str = json.dumps(tool_result, indent=2)

    instruction = (
        f"The tool '{tool_name}' returned:\n{result_str}\n\n"
        f"Explain this clearly to the user. "
        f"Use ₹ for amounts. Be concise. "
        f"Where relevant, relate it to their financial snapshot above "
        f"(e.g. how the EMI affects their existing burden, "
        f"how an investment fits their savings rate, etc.)."
    )

    parts = [f"<|system|>\n{system}\n</s>"]
    for msg in chat_history[-4:]:
        tag = "<|user|>" if msg["role"] == "user" else "<|assistant|>"
        parts.append(f"{tag}\n{msg['content']}\n</s>")

    parts.append(f"<|user|>\n{user_message}\n</s>")
    parts.append(f"<|assistant|>\n[Calculation done]\n</s>")
    parts.append(f"<|user|>\n{instruction}\n</s>")
    parts.append("<|assistant|>")
    return "\n".join(parts)


# ──────────────────────────────────────────────────────────────────────────────
# Tool call detection
# ──────────────────────────────────────────────────────────────────────────────

_TOOL_CALL_RE = re.compile(r"<tool_call>\s*(\{.*?\})\s*</tool_call>", re.DOTALL)
_RAW_JSON_RE  = re.compile(r'\{"tool":\s*"[^"]+",\s*"args":\s*\{.*?\}\}', re.DOTALL)


def _extract_tool_call(text: str) -> Optional[dict]:
    match = _TOOL_CALL_RE.search(text)
    raw = match.group(1) if match else None

    if raw is None:
        m = _RAW_JSON_RE.search(text)
        raw = m.group(0) if m else None

    if raw is None:
        return None

    try:
        parsed = json.loads(raw)
        if "tool" in parsed and "args" in parsed:
            return parsed
    except json.JSONDecodeError:
        logger.warning(f"tool call JSON parse failed: {raw[:200]}")

    return None


# ──────────────────────────────────────────────────────────────────────────────
# Rust tool executor
# ──────────────────────────────────────────────────────────────────────────────

async def _execute_rust_tool(tool_name: str, args: dict) -> dict:
    """
    Bridge to execute_tool_async in fintally_chatbot/tools.rs via PyO3 bindings.
    Falls back with an error dict if bindings aren't loaded.
    """
    try:
        from fintally_chatbot.finance import assistant as rust_assistant  # type: ignore
        result = await rust_assistant.execute_tool_async(tool_name, args)
        return result
    except ImportError:
        logger.error("fintally_chatbot Rust bindings not loaded")
        return {"error": "Calculation engine unavailable"}
    except Exception as e:
        logger.error(f"Rust tool '{tool_name}' failed: {e}")
        return {"error": str(e)}


# ──────────────────────────────────────────────────────────────────────────────
# Main streaming generator
# ──────────────────────────────────────────────────────────────────────────────

async def chat_stream(
    user_id: str,
    user_message: str,
    chat_history: list[dict],
    max_tokens: int = 512,
) -> AsyncGenerator[str, None]:
    """
    Main entry point for the chat router.

    Yields string chunks for SSE. Protocol chunks:
      [TOOL_CALL:name]     — tool being called (show spinner in UI)
      [TOOL_RESULT:{json}] — raw result (UI can render as a card)
      [CONTEXT_LOADED]     — emitted after user context is fetched (optional UI indicator)
      [ERROR:msg]          — failure
    Regular text chunks are LLM tokens to display directly.
    """

    # ── Phase 1: Fetch user financial context ─────────────────────────────────
    user_context = await _safe_fetch_context(user_id)
    if user_context:
        yield "[CONTEXT_LOADED]"

    # ── Phase 2: First LLM pass ───────────────────────────────────────────────
    prompt = _build_prompt(user_message, chat_history, user_context)

    try:
        first_pass = await asyncio.to_thread(
            lambda: list(python_llama.stream_generate(prompt, max_tokens))
        )
    except Exception as e:
        logger.error(f"LLM first pass failed: {e}")
        yield f"[ERROR:LLM generation failed — {e}]"
        return

    first_text = "".join(first_pass)
    logger.debug(f"LLM first pass: {first_text[:300]}")

    # ── Phase 3: Tool call detection ──────────────────────────────────────────
    tool_call = _extract_tool_call(first_text)

    if tool_call is None:
        # Plain response — stream directly
        for token in first_pass:
            yield token
        return

    # ── Phase 4: Execute Rust tool ────────────────────────────────────────────
    tool_name = tool_call["tool"]
    tool_args = tool_call["args"]

    logger.info(f"Tool: {tool_name}, args: {tool_args}")
    yield f"[TOOL_CALL:{tool_name}]"

    if tool_name not in TOOL_REGISTRY:
        yield f"[ERROR:Unknown tool '{tool_name}']"
        yield "I'm not sure how to help with that specific calculation."
        return

    tool_result = await _execute_rust_tool(tool_name, tool_args)
    yield f"[TOOL_RESULT:{json.dumps(tool_result)}]"

    if "error" in tool_result:
        yield f"Sorry, I ran into an issue: {tool_result['error']}"
        return

    # ── Phase 5: Second LLM pass — explain result in user's context ───────────
    explain_prompt = _build_explain_prompt(
        user_message, tool_name, tool_result, chat_history, user_context
    )

    try:
        explain_tokens = await asyncio.to_thread(
            lambda: list(python_llama.stream_generate(explain_prompt, max_tokens))
        )
    except Exception as e:
        logger.error(f"LLM explain pass failed: {e}")
        yield f"\nResult: {json.dumps(tool_result, indent=2)}"
        return

    for token in explain_tokens:
        yield token


# ──────────────────────────────────────────────────────────────────────────────
# Helpers
# ──────────────────────────────────────────────────────────────────────────────

async def _safe_fetch_context(user_id: str) -> str:
    """Fetch context with a hard timeout — chat must not stall if analytics is slow."""
    try:
        return await asyncio.wait_for(get_user_context(user_id), timeout=6.0)
    except asyncio.TimeoutError:
        logger.warning(f"user_context fetch timed out for user {user_id}")
        return ""
    except Exception as e:
        logger.warning(f"user_context fetch failed for user {user_id}: {e}")
        return ""


async def chat_once(
    user_id: str,
    user_message: str,
    chat_history: list[dict],
    max_tokens: int = 512,
) -> str:
    """Non-streaming variant. Collects full response as string."""
    chunks = []
    async for chunk in chat_stream(user_id, user_message, chat_history, max_tokens):
        if not (chunk.startswith("[TOOL_") or chunk.startswith("[ERROR") or chunk.startswith("[CONTEXT")):
            chunks.append(chunk)
    return "".join(chunks)