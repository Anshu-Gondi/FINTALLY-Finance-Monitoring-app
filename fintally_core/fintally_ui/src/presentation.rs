//! Bounded, presentation-only conversion of protocol DTOs to UI text.
//!
//! This module does not mutate financial data and does not access a database.

use fintally_ipc_protocol::{RecurringFrequency, TransactionPage, TransactionRecord};
use std::fmt::Write as _;

/// Limit UI-owned text separately from the 64 KiB wire frame limit.
pub const MAX_UI_SUMMARY_BYTES: usize = 16 * 1024;
const MAX_NAME_DISPLAY_BYTES: usize = 128;
const MAX_DESCRIPTION_DISPLAY_BYTES: usize = 512;
const MAX_CATEGORY_DISPLAY_BYTES: usize = 64;

pub fn format_transaction_page(page: &TransactionPage) -> String {
    let mut output = String::with_capacity(1024);
    if page.items.is_empty() {
        return "No transactions found for this page.".to_owned();
    }

    let mut displayed = 0usize;
    for item in &page.items {
        let block = format_record(item);
        if output.len().saturating_add(block.len()) > MAX_UI_SUMMARY_BYTES {
            break;
        }
        output.push_str(&block);
        displayed += 1;
    }

    if displayed < page.items.len() {
        let note = format!(
            "\nShowing {displayed} of {} transactions to keep the UI display bounded. Refresh with a smaller page size for more detail.",
            page.items.len()
        );
        append_bounded(&mut output, &note);
    }

    output
}

fn format_record(item: &TransactionRecord) -> String {
    let name = sanitize_field(&item.name, MAX_NAME_DISPLAY_BYTES);
    let category = sanitize_field(&item.category, MAX_CATEGORY_DISPLAY_BYTES);
    let description = sanitize_field(&item.description, MAX_DESCRIPTION_DISPLAY_BYTES);
    let price = if item.price.is_finite() {
        format!("{:.2}", item.price)
    } else {
        "unavailable".to_owned()
    };
    let frequency = match item.recurring_frequency {
        Some(RecurringFrequency::Daily) => "daily",
        Some(RecurringFrequency::Weekly) => "weekly",
        Some(RecurringFrequency::Monthly) => "monthly",
        None => "none",
    };

    let mut block = String::with_capacity(256 + name.len() + description.len());
    let _ = writeln!(&mut block, "#{}  {}", item.id, name);
    let _ = writeln!(&mut block, "  Amount: {price}");
    let _ = writeln!(&mut block, "  Category: {category}");
    let _ = writeln!(
        &mut block,
        "  Date (Unix seconds): {}",
        item.datetime_unix_seconds
    );
    let _ = writeln!(
        &mut block,
        "  Recurring: {} ({frequency})",
        if item.is_recurring { "yes" } else { "no" }
    );
    let _ = writeln!(&mut block, "  Description: {description}\n");
    block
}

fn sanitize_field(value: &str, max_bytes: usize) -> String {
    let mut output = String::with_capacity(value.len().min(max_bytes));
    for character in value.chars() {
        let normalized = if character.is_control() {
            ' '
        } else {
            character
        };
        if output.len().saturating_add(normalized.len_utf8()) > max_bytes {
            break;
        }
        output.push(normalized);
    }
    output
}

fn append_bounded(output: &mut String, suffix: &str) {
    if output.len().saturating_add(suffix.len()) <= MAX_UI_SUMMARY_BYTES {
        output.push_str(suffix);
        return;
    }

    // The suffix is an ASCII status string, so truncation is boundary-safe.
    let available = MAX_UI_SUMMARY_BYTES.saturating_sub(output.len());
    let mut end = available.min(suffix.len());
    while !suffix.is_char_boundary(end) {
        end = end.saturating_sub(1);
    }
    output.push_str(&suffix[..end]);
}

#[cfg(test)]
mod tests {
    use super::*;

    fn record(name: &str, description: &str) -> TransactionRecord {
        TransactionRecord {
            id: 1,
            name: name.to_owned(),
            price: 125.5,
            description: description.to_owned(),
            datetime_unix_seconds: 1_800_000_000,
            category: "General".to_owned(),
            is_recurring: false,
            recurring_frequency: None,
            receipt_url: None,
        }
    }

    #[test]
    fn control_characters_in_stored_text_are_sanitized() {
        let page = TransactionPage {
            page: 1,
            page_size: 1,
            has_more: false,
            items: vec![record("Rent\u{0007} statement", "first\u{001b}second")],
        };
        let rendered = format_transaction_page(&page);
        assert!(rendered.contains("Rent  statement"));
        assert!(rendered.contains("first second"));
        assert!(!rendered.contains('\u{0007}'));
        assert!(!rendered.contains('\u{001b}'));
    }

    #[test]
    fn presentation_never_exceeds_the_ui_text_budget() {
        let page = TransactionPage {
            page: 1,
            page_size: 100,
            has_more: false,
            items: (0..100)
                .map(|id| {
                    let mut item = record("transaction", &"x".repeat(512));
                    item.id = id;
                    item
                })
                .collect(),
        };
        let rendered = format_transaction_page(&page);
        assert!(rendered.len() <= MAX_UI_SUMMARY_BYTES);
        assert!(rendered.contains("Showing "));
    }

    #[test]
    fn empty_page_has_an_explicit_message() {
        let page = TransactionPage {
            page: 1,
            page_size: 10,
            has_more: false,
            items: Vec::new(),
        };
        assert_eq!(
            format_transaction_page(&page),
            "No transactions found for this page."
        );
    }
}
