mod common;
#[path = "common/transaction_helpers.rs"]
mod transaction_helpers;

use common::test_db;
use kinetib_app::transactions::list_transactions;
use transaction_helpers::{create_principal, insert_transaction};

#[tokio::test]
async fn one_principal_never_receives_another_users_transactions() {
    let Some(db) = test_db().await else { return };
    let alice = create_principal(&db, "alice@example.test", "alice-test-password").await;
    let bob = create_principal(&db, "bob@example.test", "bob-test-password").await;

    insert_transaction(
        &db,
        1,
        alice.user_id(),
        "Alice private purchase",
        10.0,
        "Alice description",
        100,
        "personal",
        false,
        None,
        None,
    )
    .await;
    insert_transaction(
        &db,
        2,
        bob.user_id(),
        "Bob private purchase",
        20.0,
        "Bob description",
        200,
        "personal",
        false,
        None,
        None,
    )
    .await;

    let alice_page = list_transactions(&db, &alice, 1, 10)
        .await
        .expect("list Alice transactions");
    assert_eq!(alice_page.items.len(), 1);
    assert_eq!(alice_page.items[0].id, 1);
    assert_eq!(alice_page.items[0].name, "Alice private purchase");

    let bob_page = list_transactions(&db, &bob, 1, 10)
        .await
        .expect("list Bob transactions");
    assert_eq!(bob_page.items.len(), 1);
    assert_eq!(bob_page.items[0].id, 2);
    assert_eq!(bob_page.items[0].name, "Bob private purchase");
}

#[tokio::test]
async fn empty_account_returns_an_empty_page() {
    let Some(db) = test_db().await else { return };
    let principal = create_principal(&db, "empty@example.test", "empty-test-password").await;

    let page = list_transactions(&db, &principal, 1, 10)
        .await
        .expect("empty transaction page should succeed");
    assert!(page.items.is_empty());
    assert_eq!(page.page, 1);
    assert_eq!(page.page_size, 10);
}
