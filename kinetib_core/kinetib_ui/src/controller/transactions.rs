//! Shared transaction form validation and UI-only formatting.
//!
//! This module never talks to storage. The platform controllers below call the
//! backend and keep transaction DTOs here only as short-lived UI state.

use crate::MainWindow;
use kinetib_ipc_protocol::{
    CreateTransactionRequest, FieldChange, RecurringFrequency, TransactionRecord,
    UpdateTransactionRequest,
};
use std::time::{SystemTime, UNIX_EPOCH};

pub(super) struct TransactionForm {
    pub name: String,
    pub price: f64,
    pub category: String,
    pub description: String,
    pub datetime_unix_seconds: i64,
    pub is_recurring: bool,
    pub recurring_frequency: Option<RecurringFrequency>,
}

pub(super) fn parse_form(
    name: String,
    amount: String,
    category: String,
    description: String,
    timestamp: String,
    is_recurring: bool,
    frequency: String,
) -> Result<TransactionForm, &'static str> {
    let name = name.trim().to_owned();
    let category = category.trim().to_owned();
    if name.is_empty() || name.len() > 128 {
        return Err("Enter a transaction name (maximum 128 UTF-8 bytes).");
    }
    if category.is_empty() || category.len() > 64 {
        return Err("Enter a category (maximum 64 UTF-8 bytes).");
    }
    if description.len() > 512 {
        return Err("Description must be 512 UTF-8 bytes or fewer.");
    }

    let price = amount
        .trim()
        .parse::<f64>()
        .ok()
        .filter(|value| value.is_finite())
        .ok_or("Enter a valid finite amount, such as 1250.75.")?;
    let datetime_unix_seconds = timestamp
        .trim()
        .parse::<i64>()
        .map_err(|_| "Enter a valid Unix timestamp in whole seconds.")?;

    let recurring_frequency = if is_recurring {
        Some(match frequency.trim().to_ascii_lowercase().as_str() {
            "daily" => RecurringFrequency::Daily,
            "weekly" => RecurringFrequency::Weekly,
            "monthly" => RecurringFrequency::Monthly,
            _ => return Err("Recurring frequency must be daily, weekly or monthly."),
        })
    } else {
        None
    };

    Ok(TransactionForm {
        name,
        price,
        category,
        description,
        datetime_unix_seconds,
        is_recurring,
        recurring_frequency,
    })
}

pub(super) fn create_request(form: TransactionForm) -> CreateTransactionRequest {
    CreateTransactionRequest {
        name: form.name,
        price: form.price,
        description: form.description,
        datetime_unix_seconds: form.datetime_unix_seconds,
        category: form.category,
        is_recurring: form.is_recurring,
        recurring_frequency: form.recurring_frequency,
    }
}

pub(super) fn update_request(id: i64, form: TransactionForm) -> UpdateTransactionRequest {
    UpdateTransactionRequest {
        id,
        name: FieldChange::Set(form.name),
        price: FieldChange::Set(form.price),
        description: FieldChange::Set(form.description),
        datetime_unix_seconds: FieldChange::Set(form.datetime_unix_seconds),
        category: FieldChange::Set(form.category),
        is_recurring: FieldChange::Set(form.is_recurring),
        recurring_frequency: match form.recurring_frequency {
            Some(frequency) => FieldChange::Set(frequency),
            None => FieldChange::Clear,
        },
    }
}

pub(super) fn row_label(record: &TransactionRecord) -> String {
    let name = clean_display_text(&record.name, 128);
    let category = clean_display_text(&record.category, 64);
    let description = clean_display_text(&record.description, 120);
    let amount = if record.price.is_finite() {
        format!("{:.2}", record.price)
    } else {
        "unavailable".to_owned()
    };
    let recurring = match record.recurring_frequency {
        Some(RecurringFrequency::Daily) => "daily",
        Some(RecurringFrequency::Weekly) => "weekly",
        Some(RecurringFrequency::Monthly) => "monthly",
        None => "no",
    };

    if description.is_empty() {
        format!(
            "#{id}  {name}    {amount}\n{category} · Unix time {timestamp} · recurring: {recurring}",
            id = record.id,
            timestamp = record.datetime_unix_seconds,
        )
    } else {
        format!(
            "#{id}  {name}    {amount}\n{category} · Unix time {timestamp} · recurring: {recurring} · {description}",
            id = record.id,
            timestamp = record.datetime_unix_seconds,
        )
    }
}

fn clean_display_text(value: &str, max_bytes: usize) -> String {
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

pub(super) fn reset_form(ui: &MainWindow) {
    ui.set_transaction_edit_id("".into());
    ui.set_transaction_form_name("".into());
    ui.set_transaction_form_amount("".into());
    ui.set_transaction_form_category("General".into());
    ui.set_transaction_form_description("".into());
    ui.set_transaction_form_timestamp(current_unix_seconds().to_string().into());
    ui.set_transaction_form_recurring(false);
    ui.set_transaction_form_frequency("monthly".into());
}

pub(super) fn populate_form(ui: &MainWindow, record: &TransactionRecord) {
    ui.set_transaction_edit_id(record.id.to_string().into());
    ui.set_transaction_form_name(record.name.clone().into());
    ui.set_transaction_form_amount(format!("{:.2}", record.price).into());
    ui.set_transaction_form_category(record.category.clone().into());
    ui.set_transaction_form_description(record.description.clone().into());
    ui.set_transaction_form_timestamp(record.datetime_unix_seconds.to_string().into());
    ui.set_transaction_form_recurring(record.is_recurring);
    let frequency = match record.recurring_frequency {
        Some(RecurringFrequency::Daily) => "daily",
        Some(RecurringFrequency::Weekly) => "weekly",
        Some(RecurringFrequency::Monthly) => "monthly",
        None => "monthly",
    };
    ui.set_transaction_form_frequency(frequency.into());
}

pub(super) fn current_unix_seconds() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|duration| duration.as_secs().min(i64::MAX as u64) as i64)
        .unwrap_or(0)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn valid_form() -> TransactionForm {
        parse_form(
            "Groceries".to_owned(),
            "1250.75".to_owned(),
            "Food".to_owned(),
            "Weekly shopping".to_owned(),
            "1800000000".to_owned(),
            false,
            "monthly".to_owned(),
        )
        .expect("valid test form")
    }

    #[test]
    fn parses_valid_form_without_changing_unix_timestamp_contract() {
        let form = valid_form();
        assert_eq!(form.name, "Groceries");
        assert_eq!(form.price, 1250.75);
        assert_eq!(form.datetime_unix_seconds, 1_800_000_000);
        assert_eq!(form.recurring_frequency, None);
    }

    #[test]
    fn rejects_non_finite_and_malformed_amounts() {
        for amount in ["NaN", "inf", "-inf", "nope", ""] {
            let result = parse_form(
                "Groceries".into(),
                amount.into(),
                "Food".into(),
                "".into(),
                "1800000000".into(),
                false,
                "monthly".into(),
            );
            assert!(result.is_err(), "amount should be rejected: {amount}");
        }
    }

    #[test]
    fn recurring_form_requires_supported_frequency() {
        for frequency in ["", "yearly", "sometimes"] {
            let result = parse_form(
                "Rent".into(),
                "1000".into(),
                "Housing".into(),
                "".into(),
                "1800000000".into(),
                true,
                frequency.into(),
            );
            assert!(result.is_err(), "frequency should be rejected: {frequency}");
        }
        let form = parse_form(
            "Rent".into(),
            "1000".into(),
            "Housing".into(),
            "".into(),
            "1800000000".into(),
            true,
            "monthly".into(),
        )
        .expect("monthly frequency is supported");
        assert_eq!(form.recurring_frequency, Some(RecurringFrequency::Monthly));
    }

    #[test]
    fn non_recurring_update_clears_frequency_explicitly() {
        let request = update_request(7, valid_form());
        assert_eq!(request.id, 7);
        assert_eq!(request.recurring_frequency, FieldChange::Clear);
        assert_eq!(request.is_recurring, FieldChange::Set(false));
    }
}
