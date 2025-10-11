#![allow(unsafe_op_in_unsafe_fn)]

use pyo3::prelude::*;
use std::collections::HashMap;

/// Aggregates timestamps + prices into N-minute buckets (low memory version)
#[pyfunction]
fn aggregate_by_interval(
    timestamps: Vec<String>,
    prices: Vec<f64>,
    interval_minutes: u32,
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{DateTime, Timelike, Utc};

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
            (
                format!("{:02}:{:02}", hour, minute),
                income,
                expense.abs(),
                total,
            )
        })
        .collect();

    Ok(formatted)
}

/// Aggregates totals and counts by category (low-memory + limit support)
#[pyfunction]
fn aggregate_by_category(
    categories: Vec<String>,
    prices: Vec<f64>,
    limit: Option<usize>, // <-- new optional limit
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
    results.sort_by(|a, b| {
        b.1 .0
            .partial_cmp(&a.1 .0)
            .unwrap_or(std::cmp::Ordering::Equal)
    });

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
    bucket_days: Option<u32>, // group by every N days if provided
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{NaiveDate, TimeZone, Utc};

    let mut map: HashMap<NaiveDate, (f64, f64)> = HashMap::new();

    for (ds, price) in dates.iter().zip(prices.iter()) {
        if let Ok(dt) = Utc
            .datetime_from_str(ds, "%Y-%m-%d %H:%M:%S%.f UTC")
            .or_else(|_| ds.parse::<chrono::DateTime<Utc>>())
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
                (
                    d.format("%Y-%m-%d").to_string(),
                    inc,
                    exp.abs(),
                    inc + exp.abs(),
                )
            })
            .collect();

        Ok(result)
    }
}

/// Aggregates income & expense by month (lifetime summary)
#[pyfunction]
fn aggregate_by_month(
    dates: Vec<String>,
    prices: Vec<f64>,
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{DateTime, Datelike, Utc};
    let mut map: HashMap<String, (f64, f64)> = HashMap::new();

    for (date_str, price) in dates.iter().zip(prices.iter()) {
        if let Ok(dt) = DateTime::parse_from_rfc3339(date_str)
            .or_else(|_| DateTime::parse_from_str(date_str, "%Y-%m-%d %H:%M:%S%.f %z"))
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
fn find_min_max(dates: Vec<String>, prices: Vec<f64>) -> PyResult<(Option<(String, f64)>, Option<(String, f64)>)> {
    use chrono::{DateTime, Utc};
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

    let max_result = if max_val.is_finite() {
        Some((max_date, max_val))
    } else {
        None
    };

    let min_result = if min_val.is_finite() {
        Some((min_date, min_val))
    } else {
        None
    };

    Ok((min_result, max_result))
}

/// Aggregates income/expense trends by week or month (for trend_summary)
#[pyfunction]
fn aggregate_trend(
    dates: Vec<String>,
    prices: Vec<f64>,
    mode: &str,  // "monthly" or "weekly"
) -> PyResult<Vec<(String, f64, f64, f64)>> {
    use chrono::{DateTime, Datelike, Utc};
    let mut map: HashMap<String, (f64, f64)> = HashMap::new();

    for (date_str, price) in dates.iter().zip(prices.iter()) {
        if let Ok(dt) = DateTime::parse_from_rfc3339(date_str)
            .or_else(|_| DateTime::parse_from_str(date_str, "%Y-%m-%d %H:%M:%S%.f %z"))
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

#[pymodule]
fn rust_backend(_py: Python, m: &PyModule) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(aggregate_by_interval, m)?)?;
    m.add_function(wrap_pyfunction!(aggregate_by_category, m)?)?;
    m.add_function(wrap_pyfunction!(aggregate_by_month, m)?)?;
    m.add_function(wrap_pyfunction!(find_min_max, m)?)?;
    m.add_function(wrap_pyfunction!(aggregate_by_day, m)?)?;
    m.add_function(wrap_pyfunction!(aggregate_trend, m)?)?;
    Ok(())
}
