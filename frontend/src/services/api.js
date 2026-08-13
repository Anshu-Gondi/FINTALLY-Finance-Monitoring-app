/**
 * api.js — FinTally API service layer
 * Synchronized with high-performance Axum Rust backend specifications.
 */

const BASE = import.meta.env.VITE_API_URL ?? "http://localhost:8080";

// ─── CORE HELPERS ───────────────────────────────────────────────────────────

function authHeaders(extra = {}) {
  const token = localStorage.getItem("token");
  return {
    ...(token ? { Authorization: `Bearer ${token}` } : {}),
    ...extra,
  };
}

async function handleResponse(res) {
  const json = await res.json().catch(() => ({}));
  if (!res.ok) {
    const msg = json?.detail?.message ?? json?.detail ?? json?.error ?? json?.message ?? res.statusText;
    throw new Error(msg || `HTTP ${res.status}`);
  }
  return json;
}

function jsonPost(url, body) {
  return fetch(url, {
    method: "POST",
    headers: authHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify(body),
  }).then(handleResponse);
}

function get(url) {
  return fetch(url, { headers: authHeaders() }).then(handleResponse);
}

function del(url) {
  return fetch(url, { method: "DELETE", headers: authHeaders() }).then(handleResponse);
}

// ─── AUTHENTICATION MODULE ──────────────────────────────────────────────────

export const auth = {
  signup: (name, email, password) =>
    jsonPost(`${BASE}/api/signup`, { name, email, password }),

  login: (email, password) =>
    jsonPost(`${BASE}/api/login`, { email, password }),

  googleAuth: (token) =>
    jsonPost(`${BASE}/api/google-auth`, { token }),
};

// ─── TRANSACTIONS MODULE ────────────────────────────────────────────────────

export const transactionApi = {
  list: (page = 1) =>
    get(`${BASE}/api/transaction?page=${page}`),

  create: (fields, receiptFile = null) => {
    const form = new FormData();
    Object.entries(fields).forEach(([k, v]) => {
      if (v !== null && v !== undefined) form.append(k, v);
    });
    if (receiptFile) form.append("receipt", receiptFile);
    return fetch(`${BASE}/api/transaction`, {
      method: "POST",
      headers: authHeaders(),
      body: form,
    }).then(handleResponse);
  },

  update: (id, fields, receiptFile = null) => {
    const form = new FormData();
    Object.entries(fields).forEach(([k, v]) => {
      if (v !== null && v !== undefined) form.append(k, v);
    });
    if (receiptFile) form.append("receipt", receiptFile);
    return fetch(`${BASE}/api/transaction/${id}`, {
      method: "PUT", // 🔥 Ensure Axum router has .put() configured for this endpoint
      headers: authHeaders(),
      body: form,
    }).then(handleResponse);
  },

  delete: (id) => del(`${BASE}/api/transaction/${id}`),

  downloadReceipt: async (id) => {
    const res = await fetch(`${BASE}/api/transaction/receipt/${id}`, {
      headers: authHeaders(),
    });
    if (!res.ok) throw new Error(`Receipt download failed: HTTP ${res.status}`);
    return res.blob();
  },
};

// ─── BUDGET MODULE ──────────────────────────────────────────────────────────

export const budgetApi = {
  list: () => get(`${BASE}/api/budget`),
  summary: () => get(`${BASE}/api/budget/summary`),
  save: (body) => jsonPost(`${BASE}/api/budget`, body),
  delete: (id) => del(`${BASE}/api/budget/${id}`),
};

// ─── EMI ENGINE MODULE ──────────────────────────────────────────────────────

export const emiApi = {
  calculate: (body) => jsonPost(`${BASE}/api/emi/calculate`, body),
  check: (body) => jsonPost(`${BASE}/api/emi/check`, body),
  create: (body) => jsonPost(`${BASE}/api/emi/create`, body),
  delete: (id) => del(`${BASE}/api/emi/${id}`),
};

// ─── SYSTEM FEEDBACK ────────────────────────────────────────────────────────

export const feedbackApi = {
  submit: (name, email, message) =>
    jsonPost(`${BASE}/api/feedback`, { name, email, message }),
};

// ─── CORE ANALYTICS MODULE ──────────────────────────────────────────────────

export const analyticsApi = {
  dailySummary: (interval = 1) =>
    get(`${BASE}/api/analytics/daily?interval=${interval}`),

  periodSummary: (range = "weekly", bucketDays = null) => {
    const url = new URL(`${BASE}/api/analytics/period`);
    url.searchParams.set("range", range);
    if (bucketDays) url.searchParams.set("bucket_days", bucketDays);
    return get(url.toString());
  },

  lifetimeAnalysis: () => get(`${BASE}/api/analytics/lifetime`),

  categorySummary: ({ start, end, type, keyword, limit } = {}) => {
    const url = new URL(`${BASE}/api/analytics/category`);
    if (start)   url.searchParams.set("start", start);
    if (end)     url.searchParams.set("end", end);
    if (type)    url.searchParams.set("type", type);
    if (keyword) url.searchParams.set("keyword", keyword);
    if (limit)   url.searchParams.set("limit", limit);
    return get(url.toString());
  },

  trendSummary: (range = "6months") =>
    get(`${BASE}/api/analytics/trend?range=${range}`),

  minMaxTransaction: () => get(`${BASE}/api/analytics/min-max`),
  emiPressure: () => get(`${BASE}/api/analytics/emi-pressure`),

  cashflowForecast: (horizons = [30, 60, 90]) => {
    const params = horizons.map((h) => `horizons=${h}`).join("&");
    return get(`${BASE}/api/analytics/cashflow-forecast?${params}`);
  },

  budgetBreach: (endDate, simulations = 2000) =>
    get(`${BASE}/api/analytics/budget-breach?end_date=${endDate}&simulations=${simulations}`),

  recurringAnomalies: () => get(`${BASE}/api/analytics/anomalies/recurring`),
  anomalies: (threshold = 1.3) =>
    get(`${BASE}/api/analytics/anomalies/transactions?threshold=${threshold}`),

  categoryDrift: () => get(`${BASE}/api/analytics/drift`),
  recurringImpact: () => get(`${BASE}/api/analytics/recurring-impact`),
  budgetUtilization: () => get(`${BASE}/api/analytics/utilization`),
  burnRate: () => get(`${BASE}/api/analytics/burn-rate`),
  incomeStability: () => get(`${BASE}/api/analytics/income-stability`),
  savingsOptimization: () => get(`${BASE}/api/analytics/savings-optimization`),
  netWorth: () => get(`${BASE}/api/analytics/net-worth`),
  financialHealthScore: () => get(`${BASE}/api/analytics/health-score`),
  spendingPatterns: () => get(`${BASE}/api/analytics/spending-patterns`),
  goalProjection: (targetAmount) =>
    get(`${BASE}/api/analytics/goal-projection?target_amount=${targetAmount}`),
};

// ─── CHATBOT ENGINE MODULE ──────────────────────────────────────────────────

export const chatApi = {
  // Fetch list of active/past chat session IDs
  getSessions: () => get(`${BASE}/api/chat/sessions`),

  // Fetch full history for a session or general chat history
  getHistory: (sessionId = null, limit = 50) => {
    const url = new URL(`${BASE}/api/chat/history`);
    if (sessionId) url.searchParams.set("session_id", sessionId);
    url.searchParams.set("limit", limit);
    return get(url.toString());
  },

  // Delete a specific chat session by session ID
  deleteSession: (sessionId) => del(`${BASE}/api/chat/sessions/${sessionId}`),

  // Clear all history or history for a specific query session
  clearHistory: (sessionId = null) => {
    const url = new URL(`${BASE}/api/chat/history`);
    if (sessionId) url.searchParams.set("session_id", sessionId);
    return del(url.toString());
  },

  // Base streaming SSE endpoint URL
  getStreamUrl: () => `${BASE}/api/chat`,
};
