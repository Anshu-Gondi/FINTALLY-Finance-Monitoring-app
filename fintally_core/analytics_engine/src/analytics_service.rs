use std::collections::HashMap;
use chrono::{ DateTime, Utc, Duration, Datelike, Timelike };
use sqlx::{ PgPool, Row };
use uuid::Uuid; // Assuming IDs are UUIDs or Strings in Postgres. Change type if needed.
use serde::{Serialize, Deserialize};

// Import domain models from your crate
use fintally_db::models::{
    AnalyticsPoint,
    AnalyticsResult,
    CategoryPoint,
    CategoryResult,
    EmiPressureResult,
    CashflowForecastPoint,
    CashflowForecastResult,
    BudgetBreachResult,
    RecurringAnomaly,
    TransactionAnomaly,
    TransactionAnomalyResult,
    CategoryDriftPoint,
    CategoryDriftResult,
    RecurringImpactResult,
    BudgetUtilizationResult,
    BurnRateResult,
    IncomeStabilityResult,
    SavingsOptimizationResult,
    NetWorthResult,
    FinancialHealthScoreResult,
    SpendingPatternPoint,
    SpendingPatternResult,
    GoalProjectionResult,
    RecurringFrequency,
};

// Import computational functions from your pure Rust engine
use crate::analytics_aggregator::{
    aggregate_by_interval,
    aggregate_by_category,
    aggregate_by_day,
    aggregate_by_month,
    aggregate_trend,
    find_min_max,
    predict_budget_breach,
    emi_survivability_score,
    emi_monthly_pressure,
    category_drift,
    recurring_impact,
    budget_utilization,
    budget_burn_rate,
    income_stability,
    savings_metrics,
    net_worth_analysis,
    cashflow_forecast as rust_cashflow_forecast,
    detect_anomalies
};

// ---------------- HELPER UTILITIES ----------------
fn safe_float(v: f64, default: f64) -> f64 {
    if v.is_nan() || v.is_infinite() { default } else { v }
}

// ---------------- DAILY SUMMARY ----------------
pub async fn daily_summary(
    pool: &PgPool,
    user_id: Uuid,
    interval: u32
) -> Result<AnalyticsResult, sqlx::Error> {
    let now = Utc::now();
    let start = Utc::now().with_time(chrono::NaiveTime::from_hms_opt(0, 0, 0).unwrap()).unwrap();

    let rows = sqlx
        ::query(
            r#"
        SELECT datetime, price FROM transactions
        WHERE user_id = $1 AND datetime >= $2 AND datetime <= $3
        "#
        )
        .bind(user_id)
        .bind(start)
        .bind(now)
        .fetch_all(pool).await?;

    let mut timestamps = Vec::with_capacity(rows.len());
    let mut prices = Vec::with_capacity(rows.len());

    for r in rows {
        let dt: DateTime<Utc> = r.get("datetime");
        let price: f64 = r.get("price");
        timestamps.push(dt.to_rfc3339());
        prices.push(price);
    }

    let computed = aggregate_by_interval(timestamps, prices, interval);

    let data = computed
        .into_iter()
        .map(|(period, income, expense, total)| {
            AnalyticsPoint { period, income, expense, total }
        })
        .collect();

    Ok(AnalyticsResult { data, meta: None, warnings: None })
}

// ---------------- PERIOD SUMMARY ----------------
pub async fn period_summary(
    pool: &PgPool,
    user_id: Uuid,
    range_str: &str,
    bucket_days: Option<u32>
) -> Result<AnalyticsResult, sqlx::Error> {
    let now = Utc::now();
    let start = match range_str {
        "weekly" => now - Duration::days(6),
        "monthly" =>
            now
                .with_day(1)
                .unwrap()
                .with_time(chrono::NaiveTime::from_hms_opt(0, 0, 0).unwrap())
                .unwrap(),
        _ => {
            return Err(sqlx::Error::Protocol("Invalid range specified".into()));
        }
    };

    let rows = sqlx
        ::query(
            r#"
        SELECT datetime, price FROM transactions
        WHERE user_id = $1 AND datetime >= $2 AND datetime <= $3
        "#
        )
        .bind(user_id)
        .bind(start)
        .bind(now)
        .fetch_all(pool).await?;

    let mut dates = Vec::with_capacity(rows.len());
    let mut prices = Vec::with_capacity(rows.len());

    for r in rows {
        let dt: DateTime<Utc> = r.get("datetime");
        let price: f64 = r.get("price");
        dates.push(dt.to_rfc3339());
        prices.push(price);
    }

    let computed = aggregate_by_day(dates, prices, bucket_days);

    let data = computed
        .into_iter()
        .map(|(period, income, expense, total)| {
            AnalyticsPoint { period, income, expense, total }
        })
        .collect();

    Ok(AnalyticsResult { data, meta: None, warnings: None })
}

// ---------------- LIFETIME ANALYSIS ----------------
pub async fn lifetime_analysis(
    pool: &PgPool,
    user_id: Uuid
) -> Result<AnalyticsResult, sqlx::Error> {
    // Let SQL group and sum by year-month directly
    let rows = sqlx::query!(
        r#"
        SELECT
            TO_CHAR(datetime, 'YYYY-MM') as "period!",
            COALESCE(SUM(CASE WHEN price > 0 THEN price ELSE 0 END), 0)::float8 as "income!",
            COALESCE(SUM(CASE WHEN price < 0 THEN ABS(price) ELSE 0 END), 0)::float8 as "expense!"
        FROM transactions
        WHERE user_id = $1
        GROUP BY TO_CHAR(datetime, 'YYYY-MM')
        ORDER BY "period!" ASC
        "#,
        user_id
    )
    .fetch_all(pool)
    .await?;

    // Map rows directly into your domain models
    let data = rows
        .into_iter()
        .map(|r| {
            let total = r.income + r.expense;
            AnalyticsPoint {
                period: r.period,
                income: r.income,
                expense: r.expense,
                total
            }
        })
        .collect();

    Ok(AnalyticsResult { data, meta: None, warnings: None })
}

// ---------------- MIN MAX TRANSACTION ----------------
pub async fn min_max_transaction(
    pool: &PgPool,
    user_id: Uuid
) -> Result<AnalyticsResult, sqlx::Error> {
    // 1. Fetch transactions
    let rows = sqlx::query(
        r#"
        SELECT datetime, price
        FROM transactions
        WHERE user_id = $1
        "#
    )
    .bind(user_id)
    .fetch_all(pool)
    .await?;

    if rows.is_empty() {
        return Ok(AnalyticsResult { data: vec![], meta: None, warnings: None });
    }

    let mut dates = Vec::with_capacity(rows.len());
    let mut prices = Vec::with_capacity(rows.len());

    for r in rows {
        let dt: DateTime<Utc> = r.get("datetime");
        let price: f64 = r.get("price");
        dates.push(dt.to_rfc3339());
        prices.push(price);
    }

    // 2. Pass dates and prices into your pure Rust computational engine
    let (min_opt, max_opt) = find_min_max(dates, prices);

    let mut data = Vec::new();

    // Add Min Transaction Point
    if let Some((date, price)) = min_opt {
        data.push(AnalyticsPoint {
            period: format!("Min ({})", &date[..10]), // formatted date key for XAxis
            income: if price > 0.0 { price } else { 0.0 },
            expense: if price < 0.0 { price.abs() } else { 0.0 },
            total: price,
        });
    }

    // Add Max Transaction Point
    if let Some((date, price)) = max_opt {
        data.push(AnalyticsPoint {
            period: format!("Max ({})", &date[..10]),
            income: if price > 0.0 { price } else { 0.0 },
            expense: if price < 0.0 { price.abs() } else { 0.0 },
            total: price,
        });
    }

    Ok(AnalyticsResult { data, meta: None, warnings: None })
}

// ---------------- CATEGORY SUMMARY ----------------
pub async fn category_summary(
    pool: &PgPool,
    user_id: Uuid,
    start: Option<DateTime<Utc>>,
    end: Option<DateTime<Utc>>,
    tx_type: Option<&str>,
    keyword: Option<&str>,
    limit: Option<usize>
) -> Result<CategoryResult, sqlx::Error> {
    let rows = sqlx
        ::query(
            r#"
        SELECT category, price FROM transactions
        WHERE user_id = $1
          AND ($2::timestamptz IS NULL OR datetime >= $2)
          AND ($3::timestamptz IS NULL OR datetime <= $3)
          AND ($4::text IS NULL OR description ILIKE $4)
          AND ($5::text IS NULL OR ($5 = 'income' AND price > 0) OR ($5 = 'expense' AND price < 0))
        "#
        )
        .bind(user_id)
        .bind(start)
        .bind(end)
        .bind(keyword.map(|k| format!("%{}%", k)))
        .bind(tx_type)
        .fetch_all(pool).await?;

    let mut categories = Vec::with_capacity(rows.len());
    let mut prices = Vec::with_capacity(rows.len());

    for r in rows {
        let cat: String = r.get("category");
        let price: f64 = r.get("price");
        categories.push(cat);
        prices.push(price);
    }

    let computed = aggregate_by_category(categories, prices, limit);

    let data = computed
        .into_iter()
        .map(|(category, total, count)| { CategoryPoint { category, total, count: count as i32 } })
        .collect();

    Ok(CategoryResult { data, meta: None, warnings: None })
}

// ---------------- TREND SUMMARY ----------------
pub async fn trend_summary(
    pool: &PgPool,
    user_id: Uuid,
    range_str: &str
) -> Result<AnalyticsResult, sqlx::Error> {
    let now = Utc::now();
    let (start, mode) = match range_str {
        "6months" => (now - Duration::days(30 * 6), "monthly"),
        "12weeks" => (now - Duration::weeks(12), "weekly"),
        _ => {
            return Err(sqlx::Error::Protocol("Invalid trend range parameter".into()));
        }
    };

    let rows = sqlx
        ::query(
            r#"
        SELECT datetime, price FROM transactions
        WHERE user_id = $1 AND datetime >= $2 AND datetime <= $3
        "#
        )
        .bind(user_id)
        .bind(start)
        .bind(now)
        .fetch_all(pool).await?;

    let mut dates = Vec::with_capacity(rows.len());
    let mut prices = Vec::with_capacity(rows.len());

    for r in rows {
        let dt: DateTime<Utc> = r.get("datetime");
        let price: f64 = r.get("price");
        dates.push(dt.to_rfc3339());
        prices.push(price);
    }

    let computed = aggregate_trend(dates, prices, mode);

    let data = computed
        .into_iter()
        .map(|(period, income, expense, total)| {
            AnalyticsPoint { period, income, expense, total }
        })
        .collect();

    Ok(AnalyticsResult { data, meta: None, warnings: None })
}

// ---------------- EMI PRESSURE ----------------
pub async fn emi_pressure(pool: &PgPool, user_id: Uuid) -> Result<EmiPressureResult, sqlx::Error> {
    let emi_rows = sqlx::query(
        r#"
        SELECT datetime, ABS(price) as emi_amount
        FROM transactions
        WHERE user_id = $1 AND is_recurring = true AND price < 0
        "#
    )
    .bind(user_id)
    .fetch_all(pool).await?;

    let mut dates = Vec::with_capacity(emi_rows.len());
    let mut principals = Vec::with_capacity(emi_rows.len());
    let mut rates = Vec::with_capacity(emi_rows.len());
    let mut tenures = Vec::with_capacity(emi_rows.len());

    for r in emi_rows {
        let dt: DateTime<Utc> = r.get("datetime");
        let emi_amount: f64 = r.get("emi_amount");

        dates.push(dt.to_rfc3339());
        principals.push(emi_amount); // Use monthly amount directly
        rates.push(0.0);             // Default 0 rate if precalculated
        tenures.push(12);            // Default tenure
    }

    let monthly_breakdown = emi_monthly_pressure(dates, principals, rates, tenures);
    let total_emi: f64 = monthly_breakdown.iter().map(|(_, v)| v).sum();

    let income_row = sqlx::query(
        r#"SELECT COALESCE(SUM(price), 0.0) as total_inc FROM transactions WHERE user_id = $1 AND price > 0"#
    )
    .bind(user_id)
    .fetch_one(pool).await?;

    let monthly_income: f64 = income_row.get("total_inc");
    let (score, label) = emi_survivability_score(monthly_income, total_emi);
    let emi_ratio = if monthly_income > 0.0 { total_emi / monthly_income } else { 0.0 };

    Ok(EmiPressureResult {
        monthly_emi: safe_float(total_emi, 0.0),
        emi_ratio: safe_float(emi_ratio, 0.0),
        survivability_score: score as f64,
        risk_level: label,
    })
}

// ---------------- CASHFLOW FORECAST ----------------
pub async fn cashflow_forecast(
    pool: &PgPool,
    user_id: Uuid,
    horizons: Vec<i32>
) -> Result<CashflowForecastResult, sqlx::Error> {
    let rows = sqlx
        ::query(
            r#"
        SELECT datetime, price, recurring_frequency::text as freq
        FROM transactions
        WHERE user_id = $1 AND is_recurring = true
        "#
        )
        .bind(user_id)
        .fetch_all(pool).await?;

    let mut dates = Vec::with_capacity(rows.len());
    let mut prices = Vec::with_capacity(rows.len());
    let mut freqs = Vec::with_capacity(rows.len());

    for r in rows {
        let dt: DateTime<Utc> = r.get("datetime");
        let price: f64 = r.get("price");
        let freq: Option<String> = r.get("freq");

        dates.push(dt.to_rfc3339());
        prices.push(price);

        let mapped_freq = match freq.as_deref() {
            Some("Daily") => RecurringFrequency::Daily,
            Some("Weekly") => RecurringFrequency::Weekly,
            _ => RecurringFrequency::Monthly,
        };
        freqs.push(mapped_freq);
    }

    let mut points = Vec::new();
    let mut sorted_horizons = horizons.clone();
    sorted_horizons.sort_unstable();

    for horizon in sorted_horizons {
        let daily_rows = rust_cashflow_forecast(
            dates.clone(),
            prices.clone(),
            freqs.clone(),
            horizon as u32
        );
        let cumulative_balance: f64 = daily_rows
            .into_iter()
            .map(|(_, _, _, net)| net)
            .sum();
        points.push(CashflowForecastPoint {
            horizon_days: horizon,
            expected_balance: safe_float(cumulative_balance, 0.0),
        });
    }

    Ok(CashflowForecastResult { points })
}

// ---------------- BUDGET BREACH PREDICTION ----------------
pub async fn budget_breach_prediction(
    pool: &PgPool,
    user_id: Uuid,
    end_date: DateTime<Utc>,
    simulations: usize
) -> Result<BudgetBreachResult, sqlx::Error> { // 👈 Fixed error type
    let budget_opt = sqlx::query(
        r#"SELECT amount, start_date FROM budgets WHERE user_id = $1 AND category = 'Overall' LIMIT 1"#
    )
    .bind(user_id)
    .fetch_optional(pool).await?;

    let budget = match budget_opt {
        Some(b) => b,
        None => {
            return Ok(BudgetBreachResult {
                breach_probability: 0.0,
                expected_spend: 0.0,
                p50_days_to_breach: None,
            });
        }
    };

    let budget_amount: f64 = budget.get("amount");
    let budget_start: DateTime<Utc> = budget
        .get::<Option<DateTime<Utc>>, _>("start_date")
        .unwrap_or_else(Utc::now);
    let horizon_days = (end_date - budget_start).num_days().max(1) as u32;

    let rows = sqlx::query(
        r#"SELECT datetime, price FROM transactions WHERE user_id = $1 AND datetime >= $2 AND datetime <= $3"#
    )
    .bind(user_id)
    .bind(budget_start)
    .bind(end_date)
    .fetch_all(pool).await?;

    let mut dates = Vec::with_capacity(rows.len());
    let mut prices = Vec::with_capacity(rows.len());
    for r in rows {
        let dt: DateTime<Utc> = r.get("datetime");
        let price: f64 = r.get("price");
        dates.push(dt.to_rfc3339());
        prices.push(price);
    }

    let (prob, expected, p50) = predict_budget_breach(
        dates,
        prices,
        budget_amount,
        horizon_days,
        Some(simulations)
    ).map_err(|e| sqlx::Error::Protocol(e.to_string()))?; // 👈 Fixed map_err

    Ok(BudgetBreachResult {
        breach_probability: safe_float(prob, 0.0),
        expected_spend: safe_float(expected, 0.0),
        p50_days_to_breach: p50.map(|d| d as i32),
    })
}

// ---------------- RECURRING ANOMALIES ----------------
pub async fn recurring_anomalies(
    pool: &PgPool,
    user_id: Uuid
) -> Result<Vec<RecurringAnomaly>, sqlx::Error> {
    let rows = sqlx
        ::query(
            r#"SELECT description, price FROM transactions WHERE user_id = $1 AND is_recurring = true"#
        )
        .bind(user_id)
        .fetch_all(pool).await?;

    if rows.is_empty() {
        return Ok(vec![]);
    }

    let mut groups: HashMap<String, Vec<f64>> = HashMap::new();
    for r in rows {
        let desc: String = r.get("description");
        let price: f64 = r.get("price");
        groups.entry(desc).or_default().push(price);
    }

    let mut result = Vec::new();
    for (desc, price_list) in groups {
        if price_list.len() < 2 {
            continue;
        }
        let mean: f64 = price_list.iter().sum::<f64>() / (price_list.len() as f64);
        if mean == 0.0 {
            continue;
        }

        for p in price_list {
            let deviation = ((p - mean).abs() / mean.abs()) * 100.0;
            if deviation > 20.0 {
                let severity = (deviation / 100.0).min(1.0);
                result.push(RecurringAnomaly {
                    description: desc.clone(),
                    severity,
                    deviation_percent: deviation,
                });
                break;
            }
        }
    }
    Ok(result)
}

// ---------------- TRANSACTION ANOMALIES (MAD METHOD) ----------------
pub async fn transaction_anomalies(
    pool: &PgPool,
    user_id: Uuid,
    threshold: f64,
) -> Result<TransactionAnomalyResult, sqlx::Error> {
    let rows = sqlx::query(r#"SELECT datetime, price FROM transactions WHERE user_id = $1"#)
        .bind(user_id)
        .fetch_all(pool)
        .await?;

    if rows.len() < 2 {
        return Ok(TransactionAnomalyResult {
            threshold,
            anomalies: vec![],
            count: 0,
        });
    }

    let mut dates = Vec::with_capacity(rows.len());
    let mut prices = Vec::with_capacity(rows.len());

    for r in rows {
        let dt: DateTime<Utc> = r.get("datetime");
        let price: f64 = r.get("price");
        dates.push(dt.to_rfc3339());
        prices.push(price);
    }

    // Pass owned vectors into the updated detection function
    let detected = detect_anomalies(dates, prices, threshold);

    let anomalies = detected
        .into_iter()
        .map(|(datetime, price, zscore)| TransactionAnomaly {
            datetime,
            price: safe_float(price, 0.0),
            zscore: safe_float(zscore, 0.0),
        })
        .collect::<Vec<_>>();

    let count = anomalies.len() as i32;
    Ok(TransactionAnomalyResult {
        threshold,
        anomalies,
        count,
    })
}

// ---------------- CATEGORY DRIFT ----------------
pub async fn category_drift_analysis(
    pool: &PgPool,
    user_id: Uuid
) -> Result<CategoryDriftResult, sqlx::Error> {
    let rows = sqlx
        ::query(r#"SELECT category, price, datetime FROM transactions WHERE user_id = $1"#)
        .bind(user_id)
        .fetch_all(pool).await?;

    let now = Utc::now();
    let mut prev = HashMap::new();
    let mut curr = HashMap::new();

    for r in rows {
        let cat: String = r.get("category");
        let price: f64 = r.get("price");
        let dt: DateTime<Utc> = r.get("datetime");

        let val = price.abs();
        if dt.year() == now.year() && dt.month() == now.month() {
            *curr.entry(cat).or_insert(0.0) += val;
        } else {
            *prev.entry(cat).or_insert(0.0) += val;
        }
    }

    let drift = category_drift(prev, curr);
    let category_drift_points = drift
        .into_iter()
        .map(|(category, change)| { CategoryDriftPoint { category, percent_change: change } })
        .collect();

    Ok(CategoryDriftResult { category_drift: category_drift_points })
}

// ---------------- RECURRING IMPACT ----------------
pub async fn recurring_impact_analysis(
    pool: &PgPool,
    user_id: Uuid
) -> Result<RecurringImpactResult, sqlx::Error> {
    let rows = sqlx
        ::query(
            r#"SELECT price, recurring_frequency::text as freq FROM transactions WHERE user_id = $1 AND is_recurring = true"#
        )
        .bind(user_id)
        .fetch_all(pool).await?;

    let mut prices = Vec::with_capacity(rows.len());
    let mut freqs = Vec::with_capacity(rows.len());

    for r in rows {
        let price: f64 = r.get("price");
        let freq: Option<String> = r.get("freq");

        prices.push(price);
        let mapped_freq = match freq.as_deref() {
            Some("Daily") => RecurringFrequency::Daily,
            Some("Weekly") => RecurringFrequency::Weekly,
            _ => RecurringFrequency::Monthly,
        };
        freqs.push(mapped_freq);
    }

    let (monthly, yearly) = recurring_impact(prices, freqs);
    Ok(RecurringImpactResult {
        monthly_recurring_cost: monthly,
        yearly_projection: yearly,
    })
}
// ---------------- BUDGET UTILIZATION ----------------
pub async fn budget_utilization_analysis(
    pool: &PgPool,
    user_id: Uuid
) -> Result<BudgetUtilizationResult, sqlx::Error> {
    let budget_opt = sqlx
        ::query(r#"SELECT amount FROM budgets WHERE user_id = $1 AND category = 'Overall' LIMIT 1"#)
        .bind(user_id)
        .fetch_optional(pool).await?;

    let amount = match budget_opt {
        Some(b) => b.get::<f64, _>("amount"),
        None => {
            return Ok(BudgetUtilizationResult {
                budget_amount: 0.0,
                spent: 0.0,
                remaining: 0.0,
                usage_percent: 0.0,
            });
        }
    };

    let tx_rows = sqlx
        ::query(r#"SELECT price FROM transactions WHERE user_id = $1 AND price < 0"#)
        .bind(user_id)
        .fetch_all(pool).await?;

    let prices: Vec<f64> = tx_rows
        .into_iter()
        .map(|r| r.get::<f64, _>("price"))
        .collect();
    let (spent, remaining, usage_percent) = budget_utilization(amount, prices);

    Ok(BudgetUtilizationResult {
        budget_amount: amount,
        spent: safe_float(spent, 0.0),
        remaining: safe_float(remaining, 0.0),
        usage_percent: safe_float(usage_percent, 0.0),
    })
}

// ---------------- BURN RATE ----------------
pub async fn burn_rate_analysis(
    pool: &PgPool,
    user_id: Uuid
) -> Result<BurnRateResult, sqlx::Error> {
    let budget_opt = sqlx
        ::query(
            r#"SELECT amount, start_date FROM budgets WHERE user_id = $1 AND category = 'Overall' LIMIT 1"#
        )
        .bind(user_id)
        .fetch_optional(pool).await?;

    let budget = match budget_opt {
        Some(b) => b,
        None => {
            return Ok(BurnRateResult {
                daily_burn_rate: 0.0,
                days_until_exhaustion: 0,
                days_elapsed: 0,
            });
        }
    };

    let start = budget.get::<Option<DateTime<Utc>>, _>("start_date").unwrap_or_else(Utc::now);
    let amount = budget.get::<f64, _>("amount");

    let tx_rows = sqlx
        ::query(r#"SELECT price FROM transactions WHERE user_id = $1 AND datetime >= $2"#)
        .bind(user_id)
        .bind(start)
        .fetch_all(pool).await?;

    let prices: Vec<f64> = tx_rows
        .into_iter()
        .map(|r| r.get::<f64, _>("price"))
        .collect();
    let (spent, remaining, _) = budget_utilization(amount, prices);

    let days_elapsed = (Utc::now() - start).num_days().max(1) as u32;
    let (burn_rate, days_left) = budget_burn_rate(spent, days_elapsed, remaining);
    let days_left_int = days_left.map(|d| d as i32).unwrap_or(9999);

    Ok(BurnRateResult {
        daily_burn_rate: safe_float(burn_rate, 0.0),
        days_until_exhaustion: days_left_int,
        days_elapsed: days_elapsed as i32,
    })
}

// ---------------- INCOME STABILITY ----------------
pub async fn income_stability_analysis(
    pool: &PgPool,
    user_id: Uuid
) -> Result<IncomeStabilityResult, sqlx::Error> {
    let rows = sqlx
        ::query(r#"SELECT price FROM transactions WHERE user_id = $1 AND price > 0"#)
        .bind(user_id)
        .fetch_all(pool).await?;

    let incomes: Vec<f64> = rows
        .into_iter()
        .map(|r| r.get::<f64, _>("price"))
        .collect();
    let (volatility, predictability) = income_stability(incomes);

    Ok(IncomeStabilityResult {
        income_volatility: safe_float(volatility, 0.0),
        salary_predictability_score: safe_float(predictability, 0.0),
    })
}

// ---------------- SAVINGS OPTIMIZATION ----------------
pub async fn savings_optimization_analysis(
    pool: &PgPool,
    user_id: Uuid
) -> Result<SavingsOptimizationResult, sqlx::Error> {
    let rows = sqlx
        ::query(r#"SELECT price FROM transactions WHERE user_id = $1"#)
        .bind(user_id)
        .fetch_all(pool).await?;

    let mut income = 0.0;
    let mut expenses = 0.0;

    for r in rows {
        let price: f64 = r.get("price");
        if price > 0.0 {
            income += price;
        } else {
            expenses += price.abs();
        }
    }

    let (rate, score) = savings_metrics(income, expenses);
    Ok(SavingsOptimizationResult {
        saving_rate_percent: safe_float(rate, 0.0),
        financial_health_score: safe_float(score, 0.0),
    })
}

// ---------------- NET WORTH ANALYSIS ----------------
pub async fn net_worth_analysis_service(
    pool: &PgPool,
    user_id: Uuid
) -> Result<NetWorthResult, sqlx::Error> {
    let rows = sqlx
        ::query(r#"SELECT price FROM transactions WHERE user_id = $1"#)
        .bind(user_id)
        .fetch_all(pool).await?;

    let mut assets = Vec::new();
    let mut liabilities = Vec::new();

    for r in rows {
        let price: f64 = r.get("price");
        if price > 0.0 {
            assets.push(price);
        } else {
            liabilities.push(price.abs());
        }
    }

    let (total_assets, total_liabilities, net_worth) = net_worth_analysis(assets, liabilities);
    Ok(NetWorthResult { total_assets, total_liabilities, net_worth })
}

// ---------------- FINANCIAL HEALTH SCORE ----------------
pub async fn financial_health_score(
    pool: &PgPool,
    user_id: Uuid
) -> Result<FinancialHealthScoreResult, sqlx::Error> {
    let savings = savings_optimization_analysis(pool, user_id).await?;
    let stability = income_stability_analysis(pool, user_id).await?;

    let burn_rate = match burn_rate_analysis(pool, user_id).await {
        Ok(b) => b.daily_burn_rate,
        Err(_) => 0.0,
    };

    let volatility_score = (100.0 - stability.income_volatility * 100.0).max(0.0);
    let burn_score = 100.0 / (1.0 + burn_rate);

    let score = safe_float(
        savings.saving_rate_percent * 0.4 +
            stability.salary_predictability_score * 0.3 +
            volatility_score * 0.2 +
            burn_score * 0.1,
        0.0
    );

    let risk_level = (
        if score > 75.0 {
            "low"
        } else if score > 50.0 {
            "medium"
        } else {
            "high"
        }
    ).to_string();

    Ok(FinancialHealthScoreResult {
        score,
        savings_rate: savings.saving_rate_percent,
        income_stability: stability.salary_predictability_score,
        burn_rate,
        risk_level,
    })
}

// ---------------- SPENDING PATTERNS ----------------
pub async fn spending_patterns(
    pool: &PgPool,
    user_id: Uuid
) -> Result<SpendingPatternResult, sqlx::Error> {
    let cat_res = category_summary(pool, user_id, None, None, Some("expense"), None, None).await?;
    let total: f64 = cat_res.data
        .iter()
        .map(|p| p.total.abs())
        .sum();

    let patterns = cat_res.data
        .into_iter()
        .map(|p| {
            let percent = if total > 0.0 { (p.total.abs() / total) * 100.0 } else { 0.0 };
            SpendingPatternPoint {
                category: p.category,
                percent: safe_float(percent, 0.0),
            }
        })
        .collect();

    Ok(SpendingPatternResult { patterns })
}

// ---------------- GOAL PROJECTION ----------------
pub async fn goal_projection(
    pool: &PgPool,
    user_id: Uuid,
    target_amount: f64
) -> Result<GoalProjectionResult, sqlx::Error> {
    let rows = sqlx
        ::query(r#"SELECT price FROM transactions WHERE user_id = $1"#)
        .bind(user_id)
        .fetch_all(pool).await?;

    let mut income = 0.0;
    let mut expenses = 0.0;
    for r in rows {
        let price: f64 = r.get("price");
        if price > 0.0 {
            income += price;
        } else {
            expenses += price.abs();
        }
    }

    let monthly_savings = (income - expenses).max(0.0);
    let nw = net_worth_analysis_service(pool, user_id).await?;

    let months_to_goal = if monthly_savings <= 0.0 {
        -1.0
    } else {
        safe_float((target_amount - nw.net_worth).max(0.0) / monthly_savings, 0.0)
    };

    Ok(GoalProjectionResult {
        current_savings: nw.net_worth,
        monthly_savings,
        target_amount,
        months_to_goal,
    })
}
