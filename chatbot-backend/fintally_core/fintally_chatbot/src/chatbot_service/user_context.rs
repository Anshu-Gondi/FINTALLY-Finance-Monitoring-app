use std::sync::Arc;
use std::time::Duration;
use uuid::Uuid;
use sqlx::PgPool;

// Import your underlying analytical database engines
use analytics_engine::analytics_service::{
    financial_health_score,
    emi_pressure,
    spending_patterns,
    net_worth_analysis_service,
    savings_optimization_analysis,
    cashflow_forecast,
    income_stability_analysis,
    burn_rate_analysis,
};

/// Timeout threshold per individual analytical calculation (4.0 seconds)
const FETCH_TIMEOUT: Duration = Duration::from_millis(4000);

/// Handles executing analytical queries with deterministic execution safe guards.
async fn run_safe_fetch<F, T>(label: &'static str, future: F, default: T) -> T
where
    F: std::future::Future<Output = Result<T, sqlx::Error>>,
{
    match tokio::time::timeout(FETCH_TIMEOUT, future).await {
        Ok(Ok(data)) => data,
        Ok(Err(e)) => {
            eprintln!("[USER-CONTEXT-WARN] Metric fetch failed for {label}: {e}");
            default
        }
        Err(_) => {
            eprintln!("[USER-CONTEXT-WARN] Metric fetch timed out for {label} after 4s");
            default
        }
    }
}

/// Compiles concurrent metrics into a single natural language block for systemic model injection.
pub async fn get_user_context(pool: &PgPool, user_id: Uuid) -> String {
    // 1. Core structural futures definitions without blocking execution borders
    let health_fut = run_safe_fetch("health_score", financial_health_score(pool, user_id), None);
    let emi_fut = run_safe_fetch("emi_pressure", emi_pressure(pool, user_id), None);
    let patterns_fut = run_safe_fetch("spending", spending_patterns(pool, user_id), Vec::new());
    let networth_fut = run_safe_fetch("net_worth", net_worth_analysis_service(pool, user_id), None);
    let savings_fut = run_safe_fetch("savings", savings_optimization_analysis(pool, user_id), None);
    let cashflow_fut = run_safe_fetch("cashflow", cashflow_forecast(pool, user_id, vec![30, 90]), Vec::new());
    let stability_fut = run_safe_fetch("stability", income_stability_analysis(pool, user_id), None);
    let burn_fut = run_safe_fetch("burn_rate", burn_rate_analysis(pool, user_id), None);

    // 2. Drive concurrent evaluation pipelines to resolve simultaneously across threads
    let (
        health_res,
        emi_res,
        patterns_res,
        networth_res,
        savings_res,
        cashflow_res,
        stability_res,
        burn_res,
    ) = tokio::join!(
        health_fut,
        emi_fut,
        patterns_fut,
        networth_fut,
        savings_fut,
        cashflow_fut,
        stability_fut,
        burn_fut
    );

    // 3. Systematically construct plain-text layout parameters to optimize prompt structure
    let mut lines = Vec::new();
    lines.push("=== USER FINANCIAL SNAPSHOT ===".to_string());

    // Financial Health
    if let Some((score, _savings_rate, _stability, _burn, risk)) = health_res {
        lines.push(format!("Financial Health: {:.0}/100 (risk: {})", score, risk));
    }

    // Net Worth Analysis
    if let Some((assets, liabilities, net)) = networth_res {
        lines.push(format!(
            "Net Worth: ₹{:.0} (assets ₹{:.0}, liabilities ₹{:.0})",
            net, assets, liabilities
        ));
    }

    // Savings Optimization Rates
    if let Some((rate, _fin_score)) = savings_res {
        lines.push(format!("Savings Rate: {:.1}%", rate));
    }

    // EMI Burden
    if let Some((monthly_emi, emi_ratio, _surv_score, risk_label)) = emi_res {
        if monthly_emi > 0.0 {
            lines.push(format!(
                "Monthly EMI Burden: ₹{:.0} ({:.1}% of income, {})",
                monthly_emi, emi_ratio * 100.0, risk_label
            ));
        }
    }

    // Income Predictability Index
    if let Some((_volatility, predictability)) = stability_res {
        lines.push(format!("Income Predictability: {:.0}/100", predictability));
    }

    // Capital Burn Rate
    if let Some((burn_rate, days_left, _days_elapsed)) = burn_res {
        if burn_rate > 0.0 {
            let days_str = if days_left < 9999 {
                format!("{}d left", days_left)
            } else {
                "no budget set".to_string()
            };
            lines.push(format!("Burn Rate: ₹{:.0}/day ({} in budget)", burn_rate, days_str));
        }
    }

    // Cashflow Horizons Forecast Vectors
    for (horizon, balance) in cashflow_res {
        let sign = if balance >= 0.0 { "+" } else { "" };
        lines.push(format!("Cashflow ({}d): {}₹{:.0}", horizon, sign, balance));
    }

    // Categorized Spending Constraints (Slices top 5 records)
    if !patterns_res.is_empty() {
        let top_patterns = patterns_res.iter().take(5);
        let cat_elements: Vec<String> = top_patterns
            .map(|(cat, pct)| format!("{} {:.0}%", cat, pct))
            .collect();
        lines.push(format!("Top Spending: {}", cat_elements.join(", ")));
    }

    // Return an empty string if all data fetches failed to protect context budget space
    if lines.len() == 1 {
        return String::new();
    }

    lines.push("=== END SNAPSHOT ===".to_string());
    lines.join("\n")
}

/// Wraps context layers for system prompt injection.
pub fn format_context_for_prompt(context: &str) -> String {
    if context.trim().is_empty() {
        return String::new();
    }
    format!(
        "\n\nHere is the current user's real financial data. \
        Use these numbers when answering — do not make up figures:\n{}\n",
        context
    )
}