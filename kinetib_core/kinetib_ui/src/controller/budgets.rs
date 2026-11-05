//! Budget form parsing and presentation helpers.
//!
//! Amounts are parsed into integer minor units without floating-point math.

use crate::MainWindow;
use kinetib_ipc_protocol::{BudgetRecord, CreateBudgetRequest, UpdateBudgetRequest};
use std::time::{SystemTime, UNIX_EPOCH};

pub(super) struct BudgetForm {
    pub category: String,
    pub amount_minor_units: i64,
    pub period_start_unix: i64,
    pub period_end_unix: Option<i64>,
}

pub(super) fn parse_form(
    category: String,
    amount_major: String,
    period_start: String,
    period_end: String,
) -> Result<BudgetForm, &'static str> {
    let category = category.trim().to_owned();
    if category.is_empty() || category.len() > 64 {
        return Err("Enter a budget category containing 1 to 64 UTF-8 bytes.");
    }

    let amount_minor_units = parse_major_to_minor(&amount_major)
        .ok_or("Enter a positive amount with at most two decimal places, such as 1250.75.")?;

    let period_start_unix = period_start
        .trim()
        .parse::<i64>()
        .map_err(|_| "Enter the period start as a Unix timestamp in seconds.")?;

    let period_end_unix = if period_end.trim().is_empty() {
        None
    } else {
        Some(
            period_end
                .trim()
                .parse::<i64>()
                .map_err(|_| "Enter the period end as a Unix timestamp or leave it blank.")?,
        )
    };

    if period_end_unix.is_some_and(|end| end < period_start_unix) {
        return Err("The period end cannot be earlier than its start.");
    }

    Ok(BudgetForm {
        category,
        amount_minor_units,
        period_start_unix,
        period_end_unix,
    })
}

/// Exact decimal-to-minor-unit conversion. No binary floating point is used.
pub(super) fn parse_major_to_minor(input: &str) -> Option<i64> {
    let value = input.trim();
    if value.is_empty() || (value.starts_with('-') || value.starts_with('+')) {
        return None;
    }

    let mut parts = value.split('.');
    let whole = parts.next()?;
    let fractional = parts.next().unwrap_or("");
    if parts.next().is_some()
        || whole.is_empty()
        || !whole.bytes().all(|byte| byte.is_ascii_digit())
        || fractional.len() > 2
        || !fractional.bytes().all(|byte| byte.is_ascii_digit())
    {
        return None;
    }

    let major = whole.parse::<i64>().ok()?;
    let fraction = match fractional.len() {
        0 => 0_i64,
        1 => fractional.parse::<i64>().ok()?.checked_mul(10)?,
        2 => fractional.parse::<i64>().ok()?,
        _ => return None,
    };

    let minor = major.checked_mul(100)?.checked_add(fraction)?;
    (minor > 0).then_some(minor)
}

pub(super) fn create_request(form: BudgetForm) -> CreateBudgetRequest {
    CreateBudgetRequest {
        category: form.category,
        amount_minor_units: form.amount_minor_units,
        period_start_unix: form.period_start_unix,
        period_end_unix: form.period_end_unix,
    }
}

pub(super) fn update_request(
    id: i64,
    expected_revision: i64,
    form: BudgetForm,
) -> UpdateBudgetRequest {
    UpdateBudgetRequest {
        id,
        expected_revision,
        category: form.category,
        amount_minor_units: form.amount_minor_units,
        period_start_unix: form.period_start_unix,
        period_end_unix: form.period_end_unix,
    }
}

pub(super) fn row_label(record: &BudgetRecord) -> String {
    let end = record
        .period_end_unix
        .map(|value| value.to_string())
        .unwrap_or_else(|| "open".to_owned());
    format!(
        "{} · limit {} · spent {} · remaining {} · utilization {}.{:02}%\nPeriod {} to {} · revision {}",
        clean_text(&record.category, 64),
        format_minor(record.amount_minor_units),
        format_minor(record.spent_minor_units),
        format_minor(record.remaining_minor_units),
        record.utilization_percent_x100 / 100,
        record.utilization_percent_x100 % 100,
        record.period_start_unix,
        end,
        record.revision,
    )
}

pub(super) fn reset_form(ui: &MainWindow) {
    ui.set_budget_edit_id("".into());
    ui.set_budget_edit_revision("".into());
    ui.set_budget_form_category("General".into());
    ui.set_budget_form_amount("".into());
    ui.set_budget_form_start_unix(current_unix_seconds().to_string().into());
    ui.set_budget_form_end_unix("".into());
}

pub(super) fn populate_form(ui: &MainWindow, record: &BudgetRecord) {
    ui.set_budget_edit_id(record.id.to_string().into());
    ui.set_budget_edit_revision(record.revision.to_string().into());
    ui.set_budget_form_category(record.category.clone().into());
    ui.set_budget_form_amount(format_minor(record.amount_minor_units).into());
    ui.set_budget_form_start_unix(record.period_start_unix.to_string().into());
    ui.set_budget_form_end_unix(
        record
            .period_end_unix
            .map(|value| value.to_string())
            .unwrap_or_default()
            .into(),
    );
}

pub(super) fn format_minor(value: i64) -> String {
    format!("{}.{:02}", value / 100, value % 100)
}

fn clean_text(value: &str, max_bytes: usize) -> String {
    let mut output = String::with_capacity(value.len().min(max_bytes));
    for character in value.chars() {
        let character = if character.is_control() {
            ' '
        } else {
            character
        };
        if output.len().saturating_add(character.len_utf8()) > max_bytes {
            break;
        }
        output.push(character);
    }
    output
}

pub(super) fn clear_state(ui: &MainWindow) {
    ui.set_budget_rows(slint::ModelRc::new(slint::VecModel::from(Vec::<
        slint::SharedString,
    >::new())));
    ui.set_budget_editor_open(false);
    ui.set_budget_delete_confirmation_open(false);
    ui.set_budget_delete_id("".into());
    ui.set_budget_delete_revision("".into());
    reset_form(ui);
}

fn current_unix_seconds() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs().min(i64::MAX as u64) as i64)
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::{format_minor, parse_form, parse_major_to_minor};

    #[test]
    fn decimal_amounts_convert_exactly_to_minor_units() {
        assert_eq!(parse_major_to_minor("1250.75"), Some(125_075));
        assert_eq!(parse_major_to_minor("12.5"), Some(1_250));
        assert_eq!(parse_major_to_minor("12"), Some(1_200));
        assert_eq!(format_minor(125_075), "1250.75");
    }

    #[test]
    fn amount_parser_rejects_ambiguous_or_overflowing_values() {
        for value in [
            "", "0", "0.00", "-1", "+1", "1.234", "1,000", "NaN", "1.2.3",
        ] {
            assert_eq!(parse_major_to_minor(value), None, "{value}");
        }
        assert_eq!(parse_major_to_minor("92233720368547758.08"), None);
    }

    #[test]
    fn budget_form_validates_category_and_period() {
        assert!(parse_form("Food".into(), "100".into(), "10".into(), "9".into()).is_err());
        assert!(parse_form(" ".into(), "100".into(), "1".into(), "".into()).is_err());
        assert!(parse_form("Food".into(), "100".into(), "1".into(), "".into()).is_ok());
    }
}
