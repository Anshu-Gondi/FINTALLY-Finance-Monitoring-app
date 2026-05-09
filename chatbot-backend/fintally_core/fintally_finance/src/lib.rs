use pyo3::prelude::*;
use pyo3::exceptions::PyValueError;
use rayon::prelude::*;

// ─── helpers ────────────────────────────────────────────────────────────────

/// Saturating multiply-add for i64.
/// Equivalent to your SafeMulAdd — uses i128 internally (always available
/// in Rust, no Windows/Linux ifdef needed).
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

// ─── compound interest ───────────────────────────────────────────────────────

/// Calculate compound interest for a batch of inputs.
///
/// Args:
///     principals: list[int]   — principal amounts (integer currency units)
///     rates:      list[float] — annual interest rates as percentages (e.g. 10.0 = 10%)
///     years:      list[int]   — investment duration in years
///     compounds:  list[int]   — compounding frequency per year (12 = monthly)
///
/// Returns:
///     list[int] — final amounts after compound interest, rounded
///
/// Raises:
///     ValueError: if any list lengths differ
///     TypeError:  if inputs are not lists

#[pyfunction]
fn compound_interest_batch(
    principals: Vec<i64>,
    rates: Vec<f64>,
    years: Vec<i32>,
    compounds: Vec<i32>
) -> PyResult<Vec<i64>> {
    let n = principals.len();

    if rates.len() != n || years.len() != n || compounds.len() != n {
        return Err(PyValueError::new_err("All input lists must have the same length"));
    }

    let compute = |i: usize| -> i64 {
        let p = principals[i];
        let r = rates[i];
        let y = years[i];
        let c = compounds[i];

        if !r.is_finite() {
            return 0;
        }

        if p <= 0 || r < 0.0 || y <= 0 || c <= 0 {
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

    let result = Python::with_gil(|py| {
        py.allow_threads(|| {
            if should_parallelize(n) {
                (0..n).into_par_iter().map(compute).collect()
            } else {
                (0..n).map(compute).collect()
            }
        })
    });

    Ok(result)
}

// ─── EMI ─────────────────────────────────────────────────────────────────────

/// Calculate EMI (Equated Monthly Installment) for a batch of loans.
///
/// Args:
///     principals: list[int]   — loan principal amounts
///     rates:      list[float] — annual interest rates as percentages
///     months:     list[int]   — loan tenure in months
///
/// Returns:
///     list[int] — monthly EMI amounts, rounded
///
/// Raises:
///     ValueError: if list lengths differ
#[pyfunction]
fn emi_batch(principals: Vec<i64>, rates: Vec<f64>, months: Vec<i32>) -> PyResult<Vec<i64>> {
    let n = principals.len();

    if rates.len() != n || months.len() != n {
        return Err(PyValueError::new_err("All input lists must have the same length"));
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
            // integer division matches C++ P[i] / M[i]
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

    let result = Python::with_gil(|py| {
        py.allow_threads(|| {
            if should_parallelize(n) {
                (0..n).into_par_iter().map(compute).collect()
            } else {
                (0..n).map(compute).collect()
            }
        })
    });

    Ok(result)
}

// ─── SIP ─────────────────────────────────────────────────────────────────────

/// Calculate SIP (Systematic Investment Plan) future value for a batch.
///
/// Args:
///     monthly:   list[int]   — monthly investment amounts
///     rates:     list[float] — annual interest rates as percentages
///     years:     list[int]   — investment duration in years
///     compounds: list[int]   — compounding frequency per year
///
/// Returns:
///     list[int] — future values, rounded
///
/// Raises:
///     ValueError: if list lengths differ
#[pyfunction]
fn sip_batch(
    monthly: Vec<i64>,
    rates: Vec<f64>,
    years: Vec<i32>,
    compounds: Vec<i32>
) -> PyResult<Vec<i64>> {
    let n = monthly.len();

    if rates.len() != n || years.len() != n || compounds.len() != n {
        return Err(PyValueError::new_err("All input lists must have the same length"));
    }

    let compute = |i: usize| -> i64 {
        let m = monthly[i];
        let y = years[i];
        let c = compounds[i];

        if m <= 0 || y <= 0 || c <= 0 {
            return 0;
        }

        let total_periods = (y * c) as f64; // periods match compounding frequency
        let monthly_sum = (y * 12) as i64; // always months for zero-rate case
        let raw_r = rates[i];

        if !raw_r.is_finite() {
            return 0;
        }

        let rate = (raw_r * 0.01) / (c as f64); // rate per period

        if rate == 0.0 {
            return m * monthly_sum; // simple sum of monthly deposits
        }

        let exp_arg = total_periods * rate.ln_1p();

        if exp_arg > 700.0 {
            return i64::MAX;
        }

        let growth = exp_arg.exp();
        let fv = (m as f64) * ((growth - 1.0) / rate) * (1.0 + rate);

        safe_f64_to_i64(fv)
    };

    let result = Python::with_gil(|py| {
        py.allow_threads(|| {
            if should_parallelize(n) {
                (0..n).into_par_iter().map(compute).collect()
            } else {
                (0..n).map(compute).collect()
            }
        })
    });

    Ok(result)
}

// ─── budget projection ───────────────────────────────────────────────────────

/// Result type returned by budget_projection_batch.
/// Each field is a list of the same length as the inputs.
#[pyclass]
pub struct BudgetProjectionResult {
    #[pyo3(get)]
    pub projected_spent: Vec<i64>,
    #[pyo3(get)]
    pub usage_percent: Vec<f64>,
    #[pyo3(get)]
    pub warning_flag: Vec<u8>,
}

/// Calculate budget projections for a batch.
///
/// warning_flag values:
///     0 — usage < 80%   (healthy)
///     1 — usage 80–99%  (approaching limit)
///     2 — usage >= 100% (over budget)
///
/// Args:
///     principals: list[int] — monthly spend rate
///     budgets:    list[int] — total budget amounts
///     spent:      list[int] — amount already spent
///     months:     list[int] — months remaining to project
///
/// Returns:
///     BudgetProjectionResult with projected_spent, usage_percent, warning_flag
///
/// Raises:
///     ValueError: if list lengths differ
#[pyfunction]
fn budget_projection_batch(
    principals: Vec<i64>,
    budgets: Vec<i64>,
    spent: Vec<i64>,
    months: Vec<i32>
) -> PyResult<BudgetProjectionResult> {
    let n = principals.len();

    if budgets.len() != n || spent.len() != n || months.len() != n {
        return Err(PyValueError::new_err("All input lists must have the same length"));
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

    let results: Vec<(i64, f64, u8)> = Python::with_gil(|py| {
        py.allow_threads(|| {
            if should_parallelize(n) {
                (0..n).into_par_iter().map(compute).collect()
            } else {
                (0..n).map(compute).collect()
            }
        })
    });

    // split (this is fine, don't over-optimize prematurely)
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

// ─── module export ───────────────────────────────────────────────────────────

#[pymodule]
fn fintally_finance(_py: Python<'_>, m: &PyModule) -> PyResult<()> {
    m.add_function(wrap_pyfunction!(compound_interest_batch, m)?)?;
    m.add_function(wrap_pyfunction!(emi_batch, m)?)?;
    m.add_function(wrap_pyfunction!(sip_batch, m)?)?;
    m.add_function(wrap_pyfunction!(budget_projection_batch, m)?)?;
    m.add_class::<BudgetProjectionResult>()?;
    Ok(())
}