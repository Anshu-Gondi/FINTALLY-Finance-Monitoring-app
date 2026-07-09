use std::time::Duration;
use uuid::Uuid;
use sqlx::PgPool;

// Import your underlying analytical database engines and their concrete result types
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
use fintally_db::models::{
    FinancialHealthScoreResult,
    EmiPressureResult,
    SpendingPatternResult,
    NetWorthResult,
    SavingsOptimizationResult,
    CashflowForecastResult,
    IncomeStabilityResult,
    BurnRateResult,
    SpendingPatternPoint,
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
    // 1. Core structural futures definitions passing explicit structural fallbacks
    let health_fut = run_safe_fetch("health_score", financial_health_score(pool, user_id), FinancialHealthScoreResult {
        score: 0.0,
        savings_rate: 0.0,
        income_stability: 0.0,
        burn_rate: 0.0,
        risk_level: "UNKNOWN".to_string(),
    });
    let emi_fut = run_safe_fetch("emi_pressure", emi_pressure(pool, user_id), EmiPressureResult {
        monthly_emi: 0.0,
        emi_ratio: 0.0,
        survivability_score: 0.0,
        risk_level: "UNKNOWN".to_string(),
    });
    let patterns_fut = run_safe_fetch("spending", spending_patterns(pool, user_id), SpendingPatternResult { patterns: Vec::new() });
    let networth_fut = run_safe_fetch("net_worth", net_worth_analysis_service(pool, user_id), NetWorthResult {
        total_assets: 0.0,
        total_liabilities: 0.0,
        net_worth: 0.0,
    });
    let savings_fut = run_safe_fetch("savings", savings_optimization_analysis(pool, user_id), SavingsOptimizationResult {
        saving_rate_percent: 0.0,
        financial_health_score: 0.0,
    });
    let cashflow_fut = run_safe_fetch("cashflow", cashflow_forecast(pool, user_id, vec![30, 90]), CashflowForecastResult { points: Vec::new() });
    let stability_fut = run_safe_fetch("stability", income_stability_analysis(pool, user_id), IncomeStabilityResult {
        income_volatility: 0.0,
        salary_predictability_score: 0.0,
    });
    let burn_fut = run_safe_fetch("burn_rate", burn_rate_analysis(pool, user_id), BurnRateResult {
        daily_burn_rate: 0.0,
        days_until_exhaustion: 0,
        days_elapsed: 0,
    });

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
    if health_res.score > 0.0 {
        lines.push(format!("Financial Health: {:.0}/100 (risk: {})", health_res.score, health_res.risk_level));
    }

    // Net Worth Analysis
    if networth_res.total_assets > 0.0 || networth_res.total_liabilities > 0.0 {
        lines.push(format!(
            "Net Worth: ₹{:.0} (assets ₹{:.0}, liabilities ₹{:.0})",
            networth_res.net_worth, networth_res.total_assets, networth_res.total_liabilities
        ));
    }

    // Savings Optimization Rates
    if savings_res.saving_rate_percent > 0.0 {
        lines.push(format!("Savings Rate: {:.1}%", savings_res.saving_rate_percent));
    }

    // EMI Burden
    if emi_res.monthly_emi > 0.0 {
        lines.push(format!(
            "Monthly EMI Burden: ₹{:.0} ({:.1}% of income, {})",
            emi_res.monthly_emi, emi_res.emi_ratio * 100.0, emi_res.risk_level
        ));
    }

    // Income Predictability Index
    if states_exist_check(&stability_res) {
        lines.push(format!("Income Predictability: {:.0}/100", stability_res.salary_predictability_score));
    }

    // Capital Burn Rate
    if burn_res.daily_burn_rate > 0.0 {
        let days_str = if burn_res.days_until_exhaustion < 9999 {
            format!("{}d left", burn_res.days_until_exhaustion)
        } else {
            "no budget set".to_string()
        };
        lines.push(format!("Burn Rate: ₹{:.0}/day ({} in budget)", burn_res.daily_burn_rate, days_str));
    }

    // Cashflow Horizons Forecast Vectors
    for point in cashflow_res.points {
        let sign = if point.expected_balance >= 0.0 { "+" } else { "" };
        lines.push(format!("Cashflow ({}d): {}₹{:.0}", point.horizon_days, sign, point.expected_balance));
    }

    // Categorized Spending Constraints
    if !patterns_res.patterns.is_empty() {
        let top_patterns = patterns_res.patterns.iter().take(5);
        let cat_elements: Vec<String> = top_patterns
            .map(|p: &SpendingPatternPoint| format!("{} {:.0}%", p.category, p.percent))
            .collect();
        lines.push(format!("Top Spending: {}", cat_elements.join(", ")));
    }

    if lines.len() == 1 {
        return String::new();
    }

    lines.push("=== END SNAPSHOT ===".to_string());
    lines.join("\n")
}

/// Helper function to confirm validity of stability response structures
fn states_exist_check(res: &IncomeStabilityResult) -> bool {
    res.salary_predictability_score > 0.0
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