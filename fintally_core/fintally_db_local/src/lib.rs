//! Device-local identity storage.
//!
//! This crate intentionally has no dependency on `fintally_db` or PostgreSQL.
//! Local-only identity and transaction records share a separate SQLite database.
//! Other finance domains must be added here before LocalOnly sessions can use them.

use sqlx::sqlite::{SqliteConnectOptions, SqliteJournalMode, SqlitePoolOptions, SqliteSynchronous};
use sqlx::SqlitePool;
use std::path::Path;
use thiserror::Error;

pub mod budgets;
pub mod transactions;

#[derive(Debug, Error)]
pub enum LocalDbError {
    #[error("local database path is invalid")]
    InvalidPath,
    #[error("local database operation failed")]
    Database(#[source] sqlx::Error),
    #[error("local database directory could not be created")]
    Directory(#[source] std::io::Error),
    #[error("local database migration failed")]
    Migration(String),
}

#[derive(Debug, Clone)]
pub struct LocalUser {
    pub id: i64,
    pub password_hash: String,
}

#[derive(Clone)]
pub struct LocalDbContext {
    pub(crate) pool: SqlitePool,
}

impl LocalDbContext {
    /// Opens a device-local SQLite database and applies the identity and
    /// transaction migrations. The host supplies the path so mobile platforms
    /// can keep it inside their application-private sandbox directory.
    pub async fn connect(path: impl AsRef<Path>) -> Result<Self, LocalDbError> {
        let path = path.as_ref();
        if path.as_os_str().is_empty() {
            return Err(LocalDbError::InvalidPath);
        }
        if let Some(parent) = path
            .parent()
            .filter(|parent| !parent.as_os_str().is_empty())
        {
            tokio::fs::create_dir_all(parent)
                .await
                .map_err(LocalDbError::Directory)?;
        }

        #[cfg(unix)]
        {
            use std::os::unix::fs::{OpenOptionsExt, PermissionsExt};
            let file = std::fs::OpenOptions::new()
                .create(true)
                .write(true)
                .truncate(false)
                .mode(0o600)
                .open(path)
                .map_err(LocalDbError::Directory)?;
            drop(file);
            std::fs::set_permissions(path, std::fs::Permissions::from_mode(0o600))
                .map_err(LocalDbError::Directory)?;
        }

        let options = SqliteConnectOptions::new()
            .filename(path)
            .create_if_missing(true)
            .foreign_keys(true)
            .journal_mode(SqliteJournalMode::Wal)
            .synchronous(SqliteSynchronous::Normal);

        let pool = SqlitePoolOptions::new()
            .max_connections(2)
            .min_connections(1)
            .connect_with(options)
            .await
            .map_err(LocalDbError::Database)?;

        // Migrations create the schema before any query! operation can run.
        sqlx::migrate!("./migrations")
            .run(&pool)
            .await
            .map_err(|error| LocalDbError::Migration(error.to_string()))?;

        Ok(Self { pool })
    }

    /// Inserts a new local account. Uniqueness is enforced by SQLite, not by a
    /// racy preflight lookup. The caller must pass a precomputed password hash.
    pub async fn create_user(
        &self,
        email: &str,
        password_hash: &str,
        created_at_unix: i64,
    ) -> Result<i64, LocalDbError> {
        let row = sqlx::query!(
            "INSERT INTO local_users (email, password_hash, created_at_unix) VALUES (?1, ?2, ?3) RETURNING id AS \"id!\"",
            email,
            password_hash,
            created_at_unix
        )
        .fetch_one(&self.pool)
        .await
        .map_err(LocalDbError::Database)?;
        Ok(row.id)
    }

    pub async fn find_user(&self, email: &str) -> Result<Option<LocalUser>, LocalDbError> {
        let row = sqlx::query!(
            "SELECT id AS \"id!\", password_hash FROM local_users WHERE email = ?1",
            email
        )
        .fetch_optional(&self.pool)
        .await
        .map_err(LocalDbError::Database)?;

        Ok(row.map(|row| LocalUser {
            id: row.id,
            password_hash: row.password_hash,
        }))
    }
}

#[cfg(test)]
mod tests {
    use super::LocalDbContext;
    use std::time::{SystemTime, UNIX_EPOCH};

    fn temp_path() -> std::path::PathBuf {
        let nonce = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .expect("system clock after epoch")
            .as_nanos();
        std::env::temp_dir().join(format!("fintally-local-{nonce}.sqlite3"))
    }

    #[tokio::test]
    async fn creates_and_reads_local_identity_without_cloud_dependencies() {
        let path = temp_path();
        let db = LocalDbContext::connect(&path).await.expect("open local db");
        let id = db
            .create_user("private@example.test", "$2b$04$example", 1_800_000_000)
            .await
            .expect("insert local user");
        let found = db
            .find_user("PRIVATE@example.test")
            .await
            .expect("query local user")
            .expect("user exists");
        assert_eq!(found.id, id);
        assert_eq!(found.password_hash, "$2b$04$example");
        drop(db);
        let _ = std::fs::remove_file(&path);
        let _ = std::fs::remove_file(path.with_extension("sqlite3-wal"));
        let _ = std::fs::remove_file(path.with_extension("sqlite3-shm"));
    }
}
