mod common;
#[path = "common/transaction_helpers.rs"]
mod transaction_helpers;

use common::test_db;
use fintally_app::transactions::{
    list_transactions, TransactionError, TransactionRecurringFrequency,
};
use transaction_helpers::{create_principal, insert_transaction};

const TEST_PASSWORD: &str = "bounded-test-password";

#[tokio::test]
async fn maps_supported_recurring_frequency_and_receipt_url() {
    let Some(db) = test_db().await else { return };
    let principal = create_principal(&db, "frequency@example.test", TEST_PASSWORD).await;

    insert_transaction(
        &db,
        1,
        principal.user_id(),
        "Weekly groceries",
        42.50,
        "Grocery purchase",
        100,
        "food",
        true,
        Some("weekly"),
        Some("https://receipts.example.test/1"),
    )
    .await;

    let page = list_transactions(&db, &principal, 1, 10)
        .await
        .expect("valid transaction row should map");
    assert_eq!(page.items.len(), 1);
    assert_eq!(
        page.items[0].recurring_frequency,
        Some(TransactionRecurringFrequency::Weekly)
    );
    assert_eq!(
        page.items[0].receipt_url.as_deref(),
        Some("https://receipts.example.test/1")
    );
}

#[tokio::test]
async fn rejects_name_over_utf8_byte_limit() {
    let Some(db) = test_db().await else { return };
    let principal = create_principal(&db, "large-name@example.test", TEST_PASSWORD).await;
    let oversized_name = "n".repeat(129);

    insert_transaction(
        &db,
        1,
        principal.user_id(),
        &oversized_name,
        1.0,
        "description",
        1,
        "category",
        false,
        None,
        None,
    )
    .await;

    assert!(matches!(
        list_transactions(&db, &principal, 1, 10).await,
        Err(TransactionError::InvalidStoredFieldLength)
    ));
}

#[tokio::test]
async fn rejects_oversized_description_category_and_receipt_url() {
    // Test each bound independently so one invalid row cannot mask another.
    for (case, description, category, receipt_url) in [
        ("description", "d".repeat(513), "category".to_owned(), None),
        ("category", "description".to_owned(), "c".repeat(65), None),
        (
            "receipt",
            "description".to_owned(),
            "category".to_owned(),
            Some("r".repeat(513)),
        ),
    ] {
        let Some(db) = test_db().await else { return };
        let email = format!("{case}@example.test");
        let principal = create_principal(&db, &email, TEST_PASSWORD).await;
        let receipt_ref = receipt_url.as_deref();

        insert_transaction(
            &db,
            1,
            principal.user_id(),
            "name",
            1.0,
            &description,
            1,
            &category,
            false,
            None,
            receipt_ref,
        )
        .await;

        assert!(matches!(
            list_transactions(&db, &principal, 1, 10).await,
            Err(TransactionError::InvalidStoredFieldLength)
        ));
    }
}

#[tokio::test]
async fn rejects_non_finite_stored_price() {
    let Some(db) = test_db().await else { return };
    let principal = create_principal(&db, "non-finite@example.test", TEST_PASSWORD).await;

    insert_transaction(
        &db,
        1,
        principal.user_id(),
        "invalid price",
        f64::NAN,
        "description",
        1,
        "category",
        false,
        None,
        None,
    )
    .await;

    assert!(matches!(
        list_transactions(&db, &principal, 1, 10).await,
        Err(TransactionError::InvalidStoredPrice)
    ));
}

#[tokio::test]
async fn rejects_page_that_exceeds_response_estimate_without_truncating() {
    let Some(db) = test_db().await else { return };
    let principal = create_principal(&db, "large-page@example.test", TEST_PASSWORD).await;
    let name = "n".repeat(128);
    let description = "d".repeat(512);
    let category = "c".repeat(64);
    let receipt = "r".repeat(512);

    for id in 1..=8 {
        insert_transaction(
            &db,
            id,
            principal.user_id(),
            &name,
            1.0,
            &description,
            id,
            &category,
            false,
            None,
            Some(&receipt),
        )
        .await;
    }

    assert!(matches!(
        list_transactions(&db, &principal, 1, 10).await,
        Err(TransactionError::ResponseTooLarge)
    ));
}

#[tokio::test]
async fn rejects_invalid_pagination_before_database_query() {
    let Some(db) = test_db().await else { return };
    let principal = create_principal(&db, "pagination-boundary@example.test", TEST_PASSWORD).await;

    for (page, page_size) in [(0, 10), (10_001, 10), (1, 0), (1, 101)] {
        assert!(matches!(
            list_transactions(&db, &principal, page, page_size).await,
            Err(TransactionError::InvalidPagination)
        ));
    }
}
