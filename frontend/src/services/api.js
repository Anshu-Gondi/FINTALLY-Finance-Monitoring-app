/**
 * api.js — FinTally API service layer
 *
 * Single source of truth for every backend call.
 * All endpoints derived directly from the FastAPI router definitions.
 *
 * Base URL comes from VITE_API_URL (e.g. http://localhost:8000).
 * Router prefixes (/api, /api/transaction, etc.) are baked in here.
 */

const BASE = import.meta.env.VITE_API_URL ?? "http://localhost:8000";

// ─── helpers ────────────────────────────────────────────────────────────────

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
    const msg =
      json?.detail?.message ?? json?.detail ?? json?.message ?? res.statusText;
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

function jsonPut(url, body) {
  return fetch(url, {
    method: "PUT",
    headers: authHeaders({ "Content-Type": "application/json" }),
    body: JSON.stringify(body),
  }).then(handleResponse);
}

function get(url) {
  return fetch(url, { headers: authHeaders() }).then(handleResponse);
}

function del(url) {
  return fetch(url, { method: "DELETE", headers: authHeaders() }).then(
    handleResponse,
  );
}

// ─── Auth  (/api/signup  /api/login  /api/google-auth) ───────────────────────

export const auth = {
  signup: (name, email, password) =>
    jsonPost(`${BASE}/api/signup`, { name, email, password }),

  login: (email, password) =>
    jsonPost(`${BASE}/api/login`, { email, password }),

  googleAuth: (token) =>
    jsonPost(`${BASE}/api/google-auth`, { token }),
};

// ─── Transactions  (/api/transaction/*) ──────────────────────────────────────

export const transactionApi = {
  /** Paginated list. Returns { success, data: Transaction[] } */
  list: (page = 1) =>
    get(`${BASE}/api/transaction?page=${page}`),

  /**
   * Create — multipart/form-data (supports optional receipt file).
   * @param {object} fields  - { name, price, description, datetime, category, isRecurring, recurringFrequency }
   * @param {File|null} receiptFile
   */
  create: (fields, receiptFile = null) => {
    const form = new FormData();
    Object.entries(fields).forEach(([k, v]) => {
      if (v !== null && v !== undefined) form.append(k, v);
    });
    if (receiptFile) form.append("receipt", receiptFile);
    return fetch(`${BASE}/api/transaction/`, {
      method: "POST",
      headers: authHeaders(),          // no Content-Type — browser sets multipart boundary
      body: form,
    }).then(handleResponse);
  },

  /**
   * Update — multipart/form-data.
   * Only the fields you pass will be updated (partial update).
   */
  update: (id, fields, receiptFile = null) => {
    const form = new FormData();
    Object.entries(fields).forEach(([k, v]) => {
      if (v !== null && v !== undefined) form.append(k, v);
    });
    if (receiptFile) form.append("receipt", receiptFile);
    return fetch(`${BASE}/api/transaction/${id}`, {
      method: "PUT",
      headers: authHeaders(),
      body: form,
    }).then(handleResponse);
  },

  delete: (id) => del(`${BASE}/api/transaction/${id}`),

  /**
   * Download receipt PDF as a Blob (caller creates an object URL).
   */
  downloadReceipt: async (id) => {
    const res = await fetch(`${BASE}/api/transaction/receipt/${id}`, {
      headers: authHeaders(),
    });
    if (!res.ok) throw new Error(`Receipt failed: HTTP ${res.status}`);
    return res.blob();
  },
};

// ─── Budget  (/api/budget/*) ─────────────────────────────────────────────────

export const budgetApi = {
  /** Returns { success, data: Budget[] } */
  list: () => get(`${BASE}/api/budget/`),

  /** Returns { success, data: BudgetSummary[] } — includes usagePercent & status */
  summary: () => get(`${BASE}/api/budget/summary`),

  /**
   * Create or update (backend does upsert on overlapping period + category).
   * @param {{ amount, category?, startDate?, endDate?, isRecurring? }} body
   */
  save: (body) => jsonPost(`${BASE}/api/budget/`, body),

  delete: (id) => del(`${BASE}/api/budget/${id}`),
};

// ─── EMI  (/api/emi/*) ───────────────────────────────────────────────────────

export const emiApi = {
  /**
   * Pure math — returns { emi, totalPayable, totalInterest }.
   * Cached on server for 10 min.
   * @param {{ principal: number, annualRate: number, months: number }} body
   */
  calculate: (body) => jsonPost(`${BASE}/api/emi/calculate`, body),

  /**
   * Math + budget affordability check.
   * @param {{ principal, annualRate, months, category? }} body
   */
  check: (body) => jsonPost(`${BASE}/api/emi/check`, body),

  /**
   * Create EMI as a recurring monthly transaction (fails if not affordable).
   * @param {{ principal, annualRate, months, category?, name? }} body
   */
  create: (body) => jsonPost(`${BASE}/api/emi/create`, body),
};

// ─── Feedback  (/api/feedback) ───────────────────────────────────────────────

export const feedbackApi = {
  submit: (name, email, message) =>
    jsonPost(`${BASE}/api/feedback`, { name, email, message }),
};

// ─── Analytics  (no /api prefix — router has no prefix) ─────────────────────

export const analyticsApi = {
  dailySummary: (interval = 1) =>
    get(`${BASE}/daily-summary?interval=${interval}`),

  periodSummary: (range = "weekly", bucketDays = null) => {
    const url = new URL(`${BASE}/period-summary`);
    url.searchParams.set("range", range);
    if (bucketDays) url.searchParams.set("bucket_days", bucketDays);
    return get(url.toString());
  },

  lifetimeAnalysis: () => get(`${BASE}/lifetime-analysis`),

  categorySummary: ({ start, end, type, keyword, limit } = {}) => {
    const url = new URL(`${BASE}/category-summary`);
    if (start)   url.searchParams.set("start", start);
    if (end)     url.searchParams.set("end", end);
    if (type)    url.searchParams.set("type", type);
    if (keyword) url.searchParams.set("keyword", keyword);
    if (limit)   url.searchParams.set("limit", limit);
    return get(url.toString());
  },

  trendSummary: (range = "6months") =>
    get(`${BASE}/trend-summary?range=${range}`),

  minMaxTransaction: () => get(`${BASE}/min-max-transaction`),

  emiPressure: () => get(`${BASE}/emi-pressure`),

  cashflowForecast: (horizons = [30, 60, 90]) => {
    const params = horizons.map((h) => `horizons=${h}`).join("&");
    return get(`${BASE}/cashflow-forecast?${params}`);
  },

  budgetBreach: (endDate, simulations = 2000) =>
    get(`${BASE}/budget-breach?end_date=${endDate}&simulations=${simulations}`),

  recurringAnomalies: () => get(`${BASE}/recurring-anomalies`),

  anomalies: (threshold = 2.5) =>
    get(`${BASE}/anomalies?threshold=${threshold}`),

  categoryDrift: () => get(`${BASE}/category-drift`),

  recurringImpact: () => get(`${BASE}/recurring-impact`),

  budgetUtilization: () => get(`${BASE}/budget-utilization`),

  burnRate: () => get(`${BASE}/burn-rate`),

  incomeStability: () => get(`${BASE}/income-stability`),

  savingsOptimization: () => get(`${BASE}/savings-optimization`),

  netWorth: () => get(`${BASE}/net-worth`),

  financialHealthScore: () => get(`${BASE}/financial-health-score`),

  spendingPatterns: () => get(`${BASE}/spending-patterns`),

  goalProjection: (targetAmount) =>
    get(`${BASE}/goal-projection?target_amount=${targetAmount}`),
};