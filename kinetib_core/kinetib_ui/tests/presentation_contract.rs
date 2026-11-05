use kinetib_ipc_protocol::{RecurringFrequency, TransactionPage, TransactionRecord};
use kinetib_ui::presentation::{format_transaction_page, MAX_UI_SUMMARY_BYTES};

#[test]
fn records_render_without_changing_transaction_values() {
    let page = TransactionPage {
        page: 2,
        page_size: 1,
        has_more: false,
        items: vec![TransactionRecord {
            id: 42,
            name: "Groceries".to_owned(),
            price: 849.25,
            description: "Weekly purchase".to_owned(),
            datetime_unix_seconds: 1_800_000_000,
            category: "Food".to_owned(),
            is_recurring: true,
            recurring_frequency: Some(RecurringFrequency::Weekly),
            receipt_url: Some("https://example.invalid/receipt".to_owned()),
        }],
    };

    let output = format_transaction_page(&page);
    assert!(output.contains("Groceries"));
    assert!(output.contains("849.25"));
    assert!(output.contains("weekly"));
    assert!(!output.contains("example.invalid"));
    assert!(output.len() <= MAX_UI_SUMMARY_BYTES);
}

#[test]
fn large_page_display_is_bounded_and_explicit() {
    let items = (0..100)
        .map(|id| TransactionRecord {
            id,
            name: format!("Transaction {id}"),
            price: 1.0,
            description: "d".repeat(512),
            datetime_unix_seconds: id,
            category: "category".to_owned(),
            is_recurring: false,
            recurring_frequency: None,
            receipt_url: None,
        })
        .collect();
    let page = TransactionPage {
        page: 1,
        page_size: 100,
        has_more: false,
        items,
    };

    let output = format_transaction_page(&page);
    assert!(output.len() <= MAX_UI_SUMMARY_BYTES);
    assert!(output.contains("Showing "));
}
