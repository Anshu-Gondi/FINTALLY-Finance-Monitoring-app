mod common;
#[path = "common/transaction_helpers.rs"]
mod transaction_helpers;

use common::test_db;
use kinetib_app::transactions::list_transactions;
use transaction_helpers::{create_principal, insert_transaction};

#[tokio::test]
async fn pages_are_sorted_by_datetime_descending_without_overlap() {
    let Some(db) = test_db().await else { return };
    let principal = create_principal(&db, "pages@example.test", "paging-test-password").await;

    // Insert out of chronological order to verify the SQL ordering, not insert order.
    for (id, name, datetime) in [
        (1, "oldest", 100_i64),
        (2, "newest", 300_i64),
        (3, "middle", 200_i64),
    ] {
        insert_transaction(
            &db,
            id,
            principal.user_id(),
            name,
            1.0,
            "description",
            datetime,
            "category",
            false,
            None,
            None,
        )
        .await;
    }

    let first_page = list_transactions(&db, &principal, 1, 2)
        .await
        .expect("first page");
    let second_page = list_transactions(&db, &principal, 2, 2)
        .await
        .expect("second page");

    assert_eq!(
        first_page
            .items
            .iter()
            .map(|item| item.id)
            .collect::<Vec<_>>(),
        vec![2, 3]
    );
    assert_eq!(
        second_page
            .items
            .iter()
            .map(|item| item.id)
            .collect::<Vec<_>>(),
        vec![1]
    );
    assert_eq!(first_page.page, 1);
    assert_eq!(second_page.page, 2);
}

#[tokio::test]
async fn maximum_page_and_size_are_accepted_by_validation_even_when_empty() {
    let Some(db) = test_db().await else { return };
    let principal = create_principal(&db, "max-page@example.test", "paging-test-password").await;

    let page = list_transactions(&db, &principal, 10_000, 100)
        .await
        .expect("maximum pagination values are valid");

    assert_eq!(page.page, 10_000);
    assert_eq!(page.page_size, 100);
    assert!(page.items.is_empty());
}
