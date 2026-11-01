use crate::common::insert_user;
use fintally_app::auth::{authenticate_password, AuthenticatedPrincipal};
use fintally_db::DbContext;

/// Creates a low-cost test hash, inserts the user and returns an authenticated
/// principal. This helper is compiled only by transaction integration tests.
pub async fn create_principal(
    db: &DbContext,
    email: &str,
    password: &str,
) -> AuthenticatedPrincipal {
    let password_hash = bcrypt::hash(password, 4).expect("hash test password");
    insert_user(db, email, &password_hash).await;
    authenticate_password(db, email, password)
        .await
        .expect("authenticate test user")
}

#[allow(clippy::too_many_arguments)]
pub async fn insert_transaction(
    db: &DbContext,
    id: i64,
    user_id: i64,
    name: &str,
    price: f64,
    description: &str,
    datetime: i64,
    category: &str,
    is_recurring: bool,
    recurring_frequency: Option<&str>,
    receipt_url: Option<&str>,
) {
    sqlx::query!(
        r#"
        INSERT INTO transactions
            (id, user_id, name, price, description, datetime, category,
             is_recurring, recurring_frequency, receipt_url)
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8,
                ($9::text)::public.recurring_frequency, $10)
        "#,
        id,
        user_id,
        name,
        price,
        description,
        datetime,
        category,
        is_recurring,
        recurring_frequency,
        receipt_url
    )
    .execute(&db.pool)
    .await
    .expect("insert temporary test transaction");
}
