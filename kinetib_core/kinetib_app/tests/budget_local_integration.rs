//! LocalOnly end-to-end budget CRUD test.
//!
//! The test opens a disposable SQLite database, lets the database crate apply
//! its migrations, and exercises the same application service used by kernel
//! dispatch. It never connects to PostgreSQL or Supabase.

use kinetib_app::auth::register_password_local;
use kinetib_app::budgets::{
    create_budget_local, delete_budget_local, list_budgets_local, update_budget_local, BudgetError,
    NewBudget,
};
use kinetib_app::transactions::{create_transaction_local, NewTransaction};
use kinetib_db_local::LocalDbContext;
use std::time::{SystemTime, UNIX_EPOCH};

fn temporary_database_path() -> std::path::PathBuf {
    let nonce = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("system clock after Unix epoch")
        .as_nanos();
    std::env::temp_dir().join(format!("kinetib-budget-{nonce}.sqlite3"))
}

fn new_budget(category: &str, amount_minor_units: i64, start: i64, end: Option<i64>) -> NewBudget {
    NewBudget {
        category: category.to_owned(),
        amount_minor_units,
        period_start_unix: start,
        period_end_unix: end,
    }
}

#[tokio::test]
async fn local_budget_crud_is_owner_scoped_and_aggregates_only_matching_period_expenses() {
    let path = temporary_database_path();
    let db = LocalDbContext::connect(&path)
        .await
        .expect("open migrated local database");

    let alice = register_password_local(&db, "alice@example.test", "alice-budget-password")
        .await
        .expect("register local owner");
    let bob = register_password_local(&db, "bob@example.test", "bob-budget-password")
        .await
        .expect("register second local owner");

    let now = 1_800_000_000_i64;
    let start = now - 100;
    let end = Some(now + 100);
    let created = create_budget_local(&db, &alice, new_budget("Food", 50_000, start, end), now)
        .await
        .expect("create Alice budget");
    assert_eq!(created.revision, 1);
    assert_eq!(created.spent_minor_units, 0);

    create_transaction_local(
        &db,
        &alice,
        NewTransaction {
            name: "In-period food expense".into(),
            price: -12.345,
            description: "Should be included".into(),
            datetime_unix_seconds: now,
            category: "Food".into(),
            is_recurring: false,
            recurring_frequency: None,
        },
    )
    .await
    .expect("insert in-period expense");

    create_transaction_local(
        &db,
        &alice,
        NewTransaction {
            name: "Out-of-period food expense".into(),
            price: -7.0,
            description: "Must not be included".into(),
            datetime_unix_seconds: now + 101,
            category: "Food".into(),
            is_recurring: false,
            recurring_frequency: None,
        },
    )
    .await
    .expect("insert out-of-period expense");

    let listed = list_budgets_local(&db, &alice)
        .await
        .expect("list Alice budgets");
    assert_eq!(listed.len(), 1);
    assert_eq!(listed[0].spent_minor_units, 1_235);
    assert_eq!(listed[0].remaining_minor_units, 48_765);
    assert_eq!(listed[0].utilization_percent_x100, 247);

    let overlap =
        create_budget_local(&db, &alice, new_budget("Food", 20_000, start, end), now).await;
    assert!(matches!(overlap, Err(BudgetError::PeriodConflict)));

    // A second owner can allocate the same category and period independently.
    let bob_budget = create_budget_local(&db, &bob, new_budget("Food", 30_000, start, end), now)
        .await
        .expect("same category is independent across owners");
    assert_eq!(list_budgets_local(&db, &bob).await.unwrap().len(), 1);

    let updated = update_budget_local(
        &db,
        &alice,
        created.id,
        created.revision,
        new_budget("Food", 60_000, start, end),
        now + 1,
    )
    .await
    .expect("update with current revision");
    assert_eq!(updated.revision, 2);
    assert_eq!(updated.amount_minor_units, 60_000);
    assert_eq!(updated.spent_minor_units, 1_235);

    let stale_update = update_budget_local(
        &db,
        &alice,
        created.id,
        created.revision,
        new_budget("Food", 70_000, start, end),
        now + 2,
    )
    .await;
    assert!(matches!(stale_update, Err(BudgetError::NotFoundOrStale)));

    let deleted = delete_budget_local(&db, &alice, created.id, updated.revision)
        .await
        .expect("delete current Alice budget");
    assert!(deleted.deleted);
    assert!(list_budgets_local(&db, &alice).await.unwrap().is_empty());
    assert_eq!(
        list_budgets_local(&db, &bob).await.unwrap()[0].id,
        bob_budget.id
    );

    drop(db);
    let _ = std::fs::remove_file(&path);
    let _ = std::fs::remove_file(path.with_extension("sqlite3-wal"));
    let _ = std::fs::remove_file(path.with_extension("sqlite3-shm"));
}
