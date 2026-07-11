use rayon::prelude::*;
use serde::{Deserialize, Serialize};

// ─── Internal Helpers ────────────────────────────────────────────────────────

#[inline]
fn safe_mul_add(a: i64, b: i64, c: i64) -> i64 {
    let result = (a as i128) * (b as i128) + (c as i128);
    result.clamp(i64::MIN as i128, i64::MAX as i128) as i64
}

#[inline]
fn should_parallelize(n: usize) -> bool {
    let cores = rayon::current_num_threads();
    n >= cores * 20_000
}

#[inline]
fn safe_f64_to_i64(x: f64) -> i64 {
    if !x.is_finite() {
        return 0;
    }
    if x > (i64::MAX as f64) {
        i64::MAX
    } else if x < (i64::MIN as f64) {
        i64::MIN
    } else {
        x.round() as i64
    }
}

// ─── Struct Definitions for Web API JSON Serialization ───────────────────────

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct BudgetProjectionResult {
    pub projected_spent: Vec<i64>,
    pub usage_percent: Vec<f64>,
    pub warning_flag: Vec<u8>,
}

// ─── Public Calculation Functions ────────────────────────────────────────────

pub fn compound_interest_batch(
    principals: &[i64],
    rates: &[f64],
    years: &[i32],
    compounds: &[i32],
) -> Result<Vec<i64>, String> {
    let n = principals.len();
    if rates.len() != n || years.len() != n || compounds.len() != n {
        return Err("All input lists must have the same length".to_string());
    }

    let compute = |i: usize| -> i64 {
        let p = principals[i];
        let r = rates[i];
        let y = years[i];
        let c = compounds[i];

        if !r.is_finite() || p <= 0 || r < 0.0 || y <= 0 || c <= 0 {
            return 0;
        }

        let rate = r * 0.01;
        let x = rate / (c as f64);
        let exp_arg = ((c * y) as f64) * x.ln_1p();

        if exp_arg > 700.0 {
            return i64::MAX;
        }

        let amount = (p as f64) * exp_arg.exp();
        safe_f64_to_i64(amount)
    };

    let result = if should_parallelize(n) {
        (0..n).into_par_iter().map(compute).collect()
    } else {
        (0..n).map(compute).collect()
    };

    Ok(result)
}

pub fn emi_batch(principals: &[i64], rates: &[f64], months: &[i32]) -> Result<Vec<i64>, String> {
    let n = principals.len();
    if rates.len() != n || months.len() != n {
        return Err("All input lists must have the same length".to_string());
    }

    let compute = |i: usize| -> i64 {
        let p = principals[i];
        let m = months[i];

        if p <= 0 || m <= 0 {
            return 0;
        }

        let raw_r = rates[i];
        if !raw_r.is_finite() {
            return 0;
        }

        let r = (raw_r * 0.01) / 12.0;
        if r == 0.0 {
            return p / (m as i64);
        }

        let exp_arg = (m as f64) * r.ln_1p();
        if exp_arg > 700.0 {
            return i64::MAX;
        }

        let factor = exp_arg.exp();
        let emi = ((p as f64) * r * factor) / (factor - 1.0);

        safe_f64_to_i64(emi)
    };

    let result = if should_parallelize(n) {
        (0..n).into_par_iter().map(compute).collect()
    } else {
        (0..n).map(compute).collect()
    };

    Ok(result)
}

pub fn sip_batch(
    monthly: &[i64],
    rates: &[f64],
    years: &[i32],
    compounds: &[i32],
) -> Result<Vec<i64>, String> {
    let n = monthly.len();
    if rates.len() != n || years.len() != n || compounds.len() != n {
        return Err("All input lists must have the same length".to_string());
    }

    let compute = |i: usize| -> i64 {
        let m = monthly[i];
        let y = years[i];
        let c = compounds[i];

        if m <= 0 || y <= 0 || c <= 0 {
            return 0;
        }

        let total_periods = (y * c) as f64;
        let monthly_sum = (y * 12) as i64;
        let raw_r = rates[i];

        if !raw_r.is_finite() {
            return 0;
        }

        let rate = (raw_r * 0.01) / (c as f64);
        if rate == 0.0 {
            return m * monthly_sum;
        }

        let exp_arg = total_periods * rate.ln_1p();
        if exp_arg > 700.0 {
            return i64::MAX;
        }

        let growth = exp_arg.exp();
        let fv = (m as f64) * ((growth - 1.0) / rate) * (1.0 + rate);

        safe_f64_to_i64(fv)
    };

    let result = if should_parallelize(n) {
        (0..n).into_par_iter().map(compute).collect()
    } else {
        (0..n).map(compute).collect()
    };

    Ok(result)
}

pub fn budget_projection_batch(
    principals: &[i64],
    budgets: &[i64],
    spent: &[i64],
    months: &[i32],
) -> Result<BudgetProjectionResult, String> {
    let n = principals.len();
    if budgets.len() != n || spent.len() != n || months.len() != n {
        return Err("All input lists must have the same length".to_string());
    }

    let compute = |i: usize| -> (i64, f64, u8) {
        let projected = safe_mul_add(principals[i], months[i] as i64, spent[i]);
        let usage = if budgets[i] > 0 {
            ((projected as f64) / (budgets[i] as f64)) * 100.0
        } else {
            100.0
        };

        let flag = if usage >= 100.0 { 2u8 } else if usage >= 80.0 { 1u8 } else { 0u8 };
        (projected, usage, flag)
    };

    let results: Vec<(i64, f64, u8)> = if should_parallelize(n) {
        (0..n).into_par_iter().map(compute).collect()
    } else {
        (0..n).map(compute).collect()
    };

    let mut projected_spent = Vec::with_capacity(n);
    let mut usage_percent = Vec::with_capacity(n);
    let mut warning_flag = Vec::with_capacity(n);

    for (p, u, f) in results {
        projected_spent.push(p);
        usage_percent.push(u);
        warning_flag.push(f);
    }

    Ok(BudgetProjectionResult {
        projected_spent,
        usage_percent,
        warning_flag,
    })
}