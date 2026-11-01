mod common;

use common::{insert_user, test_db};
use fintally_app::auth::{authenticate_password, AuthError};

#[tokio::test]
async fn wrong_password_missing_account_and_malformed_hash_share_public_auth_error() {
    let Some(db) = test_db().await else { return };

    let valid_hash = bcrypt::hash("correct-test-password", 4).expect("hash test password");
    insert_user(&db, "correct@example.test", &valid_hash).await;
    insert_user(&db, "malformed@example.test", "not-a-bcrypt-hash").await;

    let wrong_password = authenticate_password(&db, "correct@example.test", "wrong-password")
        .await
        .expect_err("wrong password must fail");
    let missing_account = authenticate_password(&db, "missing@example.test", "wrong-password")
        .await
        .expect_err("missing account must fail");
    let malformed_hash = authenticate_password(&db, "malformed@example.test", "some-password")
        .await
        .expect_err("malformed hash must fail closed");

    assert_eq!(wrong_password, AuthError::InvalidCredentials);
    assert_eq!(missing_account, AuthError::InvalidCredentials);
    assert_eq!(malformed_hash, AuthError::InvalidCredentials);
    assert_eq!(wrong_password.to_string(), missing_account.to_string());
    assert_eq!(missing_account.to_string(), malformed_hash.to_string());
}

#[tokio::test]
async fn valid_local_credentials_return_the_principal() {
    let Some(db) = test_db().await else { return };

    let hash = bcrypt::hash("correct-test-password", 4).expect("hash test password");
    let expected_id = insert_user(&db, "valid@example.test", &hash).await;

    let principal = authenticate_password(&db, "valid@example.test", "correct-test-password")
        .await
        .expect("valid credentials should authenticate");

    assert_eq!(principal.user_id(), expected_id);
}

#[tokio::test]
async fn closed_database_pool_returns_a_generic_database_error() {
    let Some(db) = test_db().await else { return };
    db.pool.close().await;

    let result = authenticate_password(&db, "valid@example.test", "a-valid-password").await;
    assert!(matches!(result, Err(AuthError::Database)));
    assert_eq!(AuthError::Database.to_string(), "database operation failed");
}
