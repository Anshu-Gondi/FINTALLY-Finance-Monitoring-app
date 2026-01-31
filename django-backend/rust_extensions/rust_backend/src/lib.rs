#![allow(unsafe_op_in_unsafe_fn)]

use pyo3::prelude::*;
use std::collections::HashMap;

/// Aggregates timestamps + prices into N-minute buckets (low memory version)
#[pyfunction]
fn aggregate_by_interval(
    timestamps: Vec<String>,
    prices: Vec<f64>,
    interval_minutes: u32
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{ DateTime, Timelike, Utc };

    // Store in HashMap to minimize allocations, key = (hour, bucket)
    let mut map: HashMap<(u32, u32), (f64, f64)> = HashMap::new();
    let bucket_size = interval_minutes.max(1); // avoid zero division

    for (ts, price) in timestamps.iter().zip(prices.iter()) {
        if let Ok(dt) = ts.parse::<DateTime<Utc>>() {
            let hour = dt.hour();
            let bucket = (dt.minute() / bucket_size) * bucket_size; // group into N-minute intervals
            let entry = map.entry((hour, bucket)).or_insert((0.0, 0.0));

            if *price > 0.0 {
                entry.0 += price;
            } else {
                entry.1 += price;
            }
        }
    }

    // Convert to Vec and sort manually (uses minimal memory)
    let mut results: Vec<_> = map.into_iter().collect();
    results.sort_by(|a, b| a.0.cmp(&b.0));

    let formatted: Vec<(String, f64, f64, f64)> = results
        .into_iter()
        .map(|((hour, minute), (income, expense))| {
            let total = income + expense.abs();
            (format!("{:02}:{:02}", hour, minute), income, expense.abs(), total)
        })
        .collect();

    Ok(formatted)
}

/// Aggregates totals and counts by category (low-memory + limit support)
#[pyfunction]
fn aggregate_by_category(
    categories: Vec<String>,
    prices: Vec<f64>,
    limit: Option<usize> // <-- new optional limit
) -> PyResult<Vec<(String, f64, u32)>> {
    let mut map: HashMap<String, (f64, u32)> = HashMap::new();

    // aggregate totals + counts
    for (cat, price) in categories.iter().zip(prices.iter()) {
        if cat.is_empty() {
            continue;
        }
        let entry = map.entry(cat.clone()).or_insert((0.0, 0));
        entry.0 += price.abs();
        entry.1 += 1;
    }

    // convert to vector and sort descending by total
    let mut results: Vec<_> = map.into_iter().collect();
    results.sort_by(|a, b| { b.1.0.partial_cmp(&a.1.0).unwrap_or(std::cmp::Ordering::Equal) });

    // apply limit if provided
    let limited = if let Some(lim) = limit {
        results.into_iter().take(lim).collect::<Vec<_>>()
    } else {
        results
    };

    // format for Python return
    let formatted: Vec<(String, f64, u32)> = limited
        .into_iter()
        .map(|(cat, (total, count))| (cat, total, count))
        .collect();

    Ok(formatted)
}

/// Aggregates income/expense totals by date (low-memory + optional day-buckets)
#[pyfunction]
fn aggregate_by_day(
    dates: Vec<String>,
    prices: Vec<f64>,
    bucket_days: Option<u32> // group by every N days if provided
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{ NaiveDate, TimeZone, Utc };

    let mut map: HashMap<NaiveDate, (f64, f64)> = HashMap::new();

    for (ds, price) in dates.iter().zip(prices.iter()) {
        if
            let Ok(dt) = Utc.datetime_from_str(ds, "%Y-%m-%d %H:%M:%S%.f UTC").or_else(|_|
                ds.parse::<chrono::DateTime<Utc>>()
            )
        {
            let date = dt.date_naive();
            let entry = map.entry(date).or_insert((0.0, 0.0));
            if *price > 0.0 {
                entry.0 += price;
            } else {
                entry.1 += price;
            }
        }
    }

    // Optional grouping by every N days
    let mut grouped: HashMap<u32, (f64, f64)> = HashMap::new();
    if let Some(n) = bucket_days {
        // Map dates into day-number buckets (e.g., every 2 days)
        let mut dates_sorted: Vec<_> = map.keys().cloned().collect();
        dates_sorted.sort();

        for (i, d) in dates_sorted.iter().enumerate() {
            let bucket_idx = (i as u32) / n;
            let (inc, exp) = map[d];
            let entry = grouped.entry(bucket_idx).or_insert((0.0, 0.0));
            entry.0 += inc;
            entry.1 += exp;
        }

        let mut out: Vec<(String, f64, f64, f64)> = grouped
            .into_iter()
            .map(|(i, (inc, exp))| (format!("Group {}", i + 1), inc, exp.abs(), inc + exp.abs()))
            .collect();

        out.sort_by(|a, b| a.0.cmp(&b.0));
        Ok(out)
    } else {
        // Default: group by each calendar day
        let mut dates_sorted: Vec<_> = map.into_iter().collect();
        dates_sorted.sort_by_key(|(d, _)| *d);

        let result: Vec<(String, f64, f64, f64)> = dates_sorted
            .into_iter()
            .map(|(d, (inc, exp))| {
                (d.format("%Y-%m-%d").to_string(), inc, exp.abs(), inc + exp.abs())
            })
            .collect();

        Ok(result)
    }
}

/// Aggregates income & expense by month (lifetime summary)
#[pyfunction]
fn aggregate_by_month(
    dates: Vec<String>,
    prices: Vec<f64>
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{ DateTime, Datelike, Utc };
    let mut map: HashMap<String, (f64, f64)> = HashMap::new();

    for (date_str, price) in dates.iter().zip(prices.iter()) {
        if
            let Ok(dt) = DateTime::parse_from_rfc3339(date_str).or_else(|_|
                DateTime::parse_from_str(date_str, "%Y-%m-%d %H:%M:%S%.f %z")
            )
        {
            let dt_utc = dt.with_timezone(&Utc);
            let key = format!("{}-{:02}", dt_utc.year(), dt_utc.month());

            let entry = map.entry(key).or_insert((0.0, 0.0));
            if *price > 0.0 {
                entry.0 += *price; // income
            } else {
                entry.1 += price.abs(); // expense
            }
        }
    }

    let mut result: Vec<_> = map.into_iter().collect();
    result.sort_by_key(|(key, _)| key.clone());

    let formatted: Vec<(String, f64, f64, f64)> = result
        .into_iter()
        .map(|(period, (inc, exp))| (period, inc, exp, inc + exp))
        .collect();

    Ok(formatted)
}

/// Finds the maximum and minimum transactions from given (datetime, price) pairs
#[pyfunction]
fn find_min_max(
    dates: Vec<String>,
    prices: Vec<f64>
) -> PyResult<(Option<(String, f64)>, Option<(String, f64)>)> {
    use chrono::{ DateTime, Utc };
    if dates.is_empty() || prices.is_empty() {
        return Ok((None, None));
    }

    let mut max_val = f64::NEG_INFINITY;
    let mut min_val = f64::INFINITY;
    let mut max_date = String::new();
    let mut min_date = String::new();

    for (date_str, price) in dates.iter().zip(prices.iter()) {
        if price > &max_val {
            max_val = *price;
            max_date = date_str.clone();
        }
        if price < &min_val {
            min_val = *price;
            min_date = date_str.clone();
        }
    }

    let max_result = if max_val.is_finite() { Some((max_date, max_val)) } else { None };

    let min_result = if min_val.is_finite() { Some((min_date, min_val)) } else { None };

    Ok((min_result, max_result))
}

/// Aggregates income/expense trends by week or month (for trend_summary)
#[pyfunction]
fn aggregate_trend(
    dates: Vec<String>,
    prices: Vec<f64>,
    mode: &str // "monthly" or "weekly"
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{ DateTime, Datelike, Utc };
    let mut map: HashMap<String, (f64, f64)> = HashMap::new();

    for (date_str, price) in dates.iter().zip(prices.iter()) {
        if
            let Ok(dt) = DateTime::parse_from_rfc3339(date_str).or_else(|_|
                DateTime::parse_from_str(date_str, "%Y-%m-%d %H:%M:%S%.f %z")
            )
        {
            let dt_utc = dt.with_timezone(&Utc);
            let key = match mode {
                "weekly" => {
                    let week_info = dt_utc.iso_week();
                    let year = week_info.year();
                    let week = week_info.week();
                    format!("{}-W{:02}", year, week)
                }
                _ => format!("{}-{:02}", dt_utc.year(), dt_utc.month()),
            };

            let entry = map.entry(key).or_insert((0.0, 0.0));
            if *price > 0.0 {
                entry.0 += *price;
            } else {
                entry.1 += price.abs();
            }
        }
    }

    let mut result: Vec<_> = map.into_iter().collect();
    result.sort_by_key(|(k, _)| k.clone());

    let formatted: Vec<(String, f64, f64, f64)> = result
        .into_iter()
        .map(|(period, (inc, exp))| (period, inc, exp, inc + exp))
        .collect();

    Ok(formatted)
}

/// Calculates budget utilization and remaining amount
#[pyfunction]
fn budget_utilization(budget_amount: f64, prices: Vec<f64>) -> PyResult<(f64, f64, f64)> {
    let spent: f64 = prices
        .iter()
        .map(|p| p.abs())
        .sum();

    let remaining = (budget_amount - spent).max(0.0);
    let usage_percent = if budget_amount > 0.0 { (spent / budget_amount) * 100.0 } else { 0.0 };

    Ok((spent, remaining, usage_percent))
}

/// Estimates daily burn rate and days until budget exhaustion
#[pyfunction]
fn budget_burn_rate(
    total_spent: f64,
    days_elapsed: u32,
    budget_remaining: f64
) -> PyResult<(f64, Option<f64>)> {
    if days_elapsed == 0 {
        return Ok((0.0, None));
    }

    let burn_rate = total_spent / (days_elapsed as f64);

    let days_left = if burn_rate > 0.0 { Some(budget_remaining / burn_rate) } else { None };

    Ok((burn_rate, days_left))
}

/// Computes recurring expense totals and projected monthly impact
#[pyfunction]
fn recurring_impact(
    prices: Vec<f64>,
    frequencies: Vec<String> // Daily, Weekly, Monthly
) -> PyResult<(f64, f64)> {
    let mut monthly_total = 0.0;

    for (price, freq) in prices.iter().zip(frequencies.iter()) {
        let factor = match freq.as_str() {
            "Daily" => 30.0,
            "Weekly" => 4.0,
            _ => 1.0,
        };
        monthly_total += price.abs() * factor;
    }

    let yearly_projection = monthly_total * 12.0;

    Ok((monthly_total, yearly_projection))
}

/// Detects month-over-month category drift
#[pyfunction]
fn category_drift(
    previous: HashMap<String, f64>,
    current: HashMap<String, f64>
) -> PyResult<Vec<(String, f64)>> {
    let mut drift = Vec::new();

    for (cat, curr_val) in current {
        if let Some(prev_val) = previous.get(&cat) {
            if *prev_val > 0.0 {
                let change = ((curr_val - prev_val) / prev_val) * 100.0;
                drift.push((cat, change));
            }
        }
    }

    drift.sort_by(|a, b| b.1.partial_cmp(&a.1).unwrap());
    Ok(drift)
}

/// Computes monthly EMI burden from EMI transactions
/// Returns: Vec<(YYYY-MM, emi_total)>
#[pyfunction]
fn emi_monthly_pressure(
    dates: Vec<String>,
    principals: Vec<f64>,
    annual_rates: Vec<f64>,
    tenures: Vec<u32>
) -> PyResult<Vec<(String, f64)>> {
    use chrono::{ DateTime, Datelike, Utc };
    use std::collections::HashMap;

    let mut map: HashMap<String, f64> = HashMap::new();

    for ((date_str, principal), (rate, tenure)) in dates
        .iter()
        .zip(principals.iter())
        .zip(annual_rates.iter().zip(tenures.iter())) {
        if
            let Ok(dt) = DateTime::parse_from_rfc3339(date_str).or_else(|_|
                DateTime::parse_from_str(date_str, "%Y-%m-%d %H:%M:%S%.f %z")
            )
        {
            let dt = dt.with_timezone(&Utc);
            let key = format!("{}-{:02}", dt.year(), dt.month());

            // EMI formula
            let r = rate / 12.0 / 100.0;
            let n = *tenure as f64;
            let emi = if r > 0.0 {
                (principal * r * (1.0 + r).powf(n)) / ((1.0 + r).powf(n) - 1.0)
            } else {
                principal / n
            };

            *map.entry(key).or_insert(0.0) += emi;
        }
    }

    let mut out: Vec<_> = map.into_iter().collect();
    out.sort_by_key(|(k, _)| k.clone());

    Ok(out)
}

/// Detects anomalous transactions using robust MAD method
#[pyfunction]
fn detect_anomalies(
    prices: Vec<f64>,
    threshold: f64 // usual: 2.0 or 3.0
) -> PyResult<Vec<f64>> {
    if prices.len() < 2 {
        return Ok(vec![]);
    }

    // Compute median
    let mut sorted = prices.clone();
    sorted.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let mid = sorted.len() / 2;
    let median = if sorted.len() % 2 == 0 {
        (sorted[mid - 1] + sorted[mid]) / 2.0
    } else {
        sorted[mid]
    };

    // Compute MAD
    let mut deviations: Vec<f64> = prices.iter().map(|x| (x - median).abs()).collect();
    deviations.sort_by(|a, b| a.partial_cmp(b).unwrap());
    let mid_dev = deviations.len() / 2;
    let mad = if deviations.len() % 2 == 0 {
        (deviations[mid_dev - 1] + deviations[mid_dev]) / 2.0
    } else {
        deviations[mid_dev]
    };

    // Handle MAD = 0 (all values identical)
    if mad == 0.0 {
        return Ok(vec![]);
    }

    // Thresholding
    let anomalies: Vec<f64> = prices
        .into_iter()
        .filter(|x| ((x - median).abs() / mad) > threshold)
        .collect();

    Ok(anomalies)
}

/// Predicts if a budget will be breached before end date
#[pyfunction]
fn predict_budget_breach(
    dates: Vec<String>,
    prices: Vec<f64>,
    budget_amount: f64,
    start_date: String,
    end_date: String
) -> PyResult<(bool, f64, Option<u32>)> {
    use chrono::{ DateTime, Utc };
    use pyo3::exceptions::PyValueError;

    let mut total_spent = 0.0;
    let mut first: Option<DateTime<Utc>> = None;
    let mut last: Option<DateTime<Utc>> = None;

    for (ds, price) in dates.iter().zip(prices.iter()) {
        if *price >= 0.0 {
            continue;
        }

        let dt = ds
            .parse::<DateTime<Utc>>()
            .map_err(|e| PyValueError::new_err(format!("Invalid date: {}", e)))?;

        total_spent += price.abs();
        first = Some(first.map_or(dt, |f| f.min(dt)));
        last = Some(last.map_or(dt, |l| l.max(dt)));
    }

    let (Some(first), Some(last)) = (first, last) else {
        return Ok((false, 0.0, None));
    };

    let days_used = (last - first).num_days().max(1) as f64;
    let daily_burn = total_spent / days_used;

    let end = end_date
        .parse::<DateTime<Utc>>()
        .map_err(|e| PyValueError::new_err(format!("Invalid end_date: {}", e)))?;

    let remaining_days = (end - Utc::now()).num_days().max(0) as f64;

    let projected_total = total_spent + daily_burn * remaining_days;
    let will_breach = projected_total > budget_amount;

    let days_until_breach = if will_breach && daily_burn > 0.0 {
        Some(((budget_amount - total_spent) / daily_burn).max(0.0) as u32)
    } else {
        None
    };

    Ok((will_breach, projected_total, days_until_breach))
}

/// Forecasts cashflow for next N days using recurring transactions
/// Returns: Vec<(date, income, expense, net)>
#[pyfunction]
fn cashflow_forecast(
    start_dates: Vec<String>,
    prices: Vec<f64>,
    frequencies: Vec<String>, // "Daily", "Weekly", "Monthly"
    horizon_days: u32
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{ DateTime, Duration, Utc };
    use std::collections::HashMap;

    let mut map: HashMap<String, (f64, f64)> = HashMap::new();
    let now = Utc::now();

    for ((date_str, price), freq) in start_dates.iter().zip(prices.iter()).zip(frequencies.iter()) {
        let Ok(dt_fixed) = DateTime::parse_from_rfc3339(date_str).or_else(|_|
            DateTime::parse_from_str(date_str, "%Y-%m-%d %H:%M:%S%.f %z")
        ) else {
            continue;
        };

        let mut dt: DateTime<Utc> = dt_fixed.with_timezone(&Utc);

        while dt <= now + Duration::days(horizon_days as i64) {
            if dt >= now {
                let key = dt.format("%Y-%m-%d").to_string();
                let entry = map.entry(key).or_insert((0.0, 0.0));

                if *price > 0.0 {
                    entry.0 += *price;
                } else {
                    entry.1 += price.abs();
                }
            }

            dt = match freq.as_str() {
                "Daily" => dt + Duration::days(1),
                "Weekly" => dt + Duration::weeks(1),
                "Monthly" => dt + Duration::days(30),
                _ => {
                    break;
                }
            };
        }
    }

    let mut out: Vec<(String, f64, f64, f64)> = map
        .into_iter()
        .map(|(d, (inc, exp))| (d, inc, exp, inc - exp))
        .collect();

    out.sort_by_key(|(d, _, _, _)| d.clone());
    Ok(out)
}

/// Computes EMI survivability score (0–100)
#[pyfunction]
fn emi_survivability_score(monthly_income: f64, monthly_emi: f64) -> PyResult<(u8, String)> {
    if monthly_income <= 0.0 {
        return Ok((0, "No income detected".into()));
    }

    let ratio = monthly_emi / monthly_income;
    let (score, label) = if ratio <= 0.2 {
        (95, "Safe")
    } else if ratio <= 0.35 {
        (80, "Manageable")
    } else if ratio <= 0.5 {
        (55, "High Risk")
    } else {
        (25, "Critical")
    };

    Ok((score, label.into()))
}

/// Detects anomalies in recurring transactions
#[pyfunction]
fn detect_recurring_anomalies(
    dates: Vec<String>,
    prices: Vec<f64>,
    expected_frequency_days: u32,
    tolerance_pct: f64 // e.g. 0.2 = 20%
) -> PyResult<Vec<(String, String)>> {
    use chrono::{ DateTime, Duration, Utc };

    let mut anomalies = vec![];
    let mut prev_date: Option<DateTime<Utc>> = None;
    let mut prev_price: Option<f64> = None;

    for (ds, price) in dates.iter().zip(prices.iter()) {
        if let Ok(dt) = ds.parse::<DateTime<Utc>>() {
            if let Some(pd) = prev_date {
                let gap = (dt - pd).num_days().abs() as u32;
                if gap > expected_frequency_days + 2 {
                    anomalies.push((ds.clone(), "Missed recurrence".into()));
                }
            }

            if let Some(pp) = prev_price {
                let delta = (price - pp).abs() / pp.abs().max(1.0);
                if delta > tolerance_pct {
                    anomalies.push((ds.clone(), "Amount anomaly".into()));
                }
            }

            prev_date = Some(dt);
            prev_price = Some(*price);
        }
    }

    Ok(anomalies)
}

#[pymodule]
fn rust_backend(_py: Python, m: &PyModule) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(aggregate_by_interval, m)?)?;
    m.add_function(wrap_pyfunction!(aggregate_by_category, m)?)?;
    m.add_function(wrap_pyfunction!(aggregate_by_month, m)?)?;
    m.add_function(wrap_pyfunction!(find_min_max, m)?)?;
    m.add_function(wrap_pyfunction!(aggregate_by_day, m)?)?;
    m.add_function(wrap_pyfunction!(aggregate_trend, m)?)?;
    m.add_function(wrap_pyfunction!(budget_utilization, m)?)?;
    m.add_function(wrap_pyfunction!(budget_burn_rate, m)?)?;
    m.add_function(wrap_pyfunction!(recurring_impact, m)?)?;
    m.add_function(wrap_pyfunction!(category_drift, m)?)?;
    m.add_function(wrap_pyfunction!(detect_anomalies, m)?)?;
    m.add_function(wrap_pyfunction!(emi_monthly_pressure, m)?)?;
    m.add_function(wrap_pyfunction!(cashflow_forecast, m)?)?;
    m.add_function(wrap_pyfunction!(predict_budget_breach, m)?)?;
    m.add_function(wrap_pyfunction!(emi_survivability_score, m)?)?;
    m.add_function(wrap_pyfunction!(detect_recurring_anomalies, m)?)?;
    Ok(())
}
