use crate::DbContext;
use serde::{Deserialize, Serialize};
use sqlx::{Row, FromRow};
use chrono::{DateTime, Utc};

pub struct ChatHistoryService {
    ctx: DbContext,
}

#[derive(Debug, Serialize, Deserialize)]
pub struct SlimMessage {
    pub role: String,
    pub content: String,
}

#[derive(Debug, Serialize, Deserialize, FromRow)]
pub struct FullMessageRow {
    pub id: i64,
    pub user_id: String,
    pub session_id: String,
    pub role: String,
    pub content: String,
    pub timestamp: DateTime<Utc>,
    pub metadata: sqlx::types::Json<serde_json::Value>,
}

impl ChatHistoryService {
    pub fn new(ctx: DbContext) -> Self {
        Self { ctx }
    }

    /// Stripped role+content only — what the LLM bridge needs
    pub async fn get_history(&self, user_id: &str, session_id: Option<&str>, limit: i64) -> Result<Vec<SlimMessage>, sqlx::Error> {
        let sid = session_id.unwrap_or("default");

        let rows = sqlx::query(
            r#"
            SELECT role, content FROM (
                SELECT id, role, content FROM chat_messages 
                WHERE user_id = $1 AND session_id = $2
                ORDER BY id DESC LIMIT $3
            ) sub ORDER BY id ASC
            "#
        )
        .bind(user_id)
        .bind(sid)
        .bind(limit)
        .fetch_all(&self.ctx.pool)
        .await?;

        let messages = rows.into_iter().map(|r| SlimMessage {
            role: r.get::<String, _>("role"),
            content: r.get::<String, _>("content"),
        }).collect();

        Ok(messages)
    }

    /// Full objects with timestamps + metadata — for the /history endpoint
    pub async fn get_full_history(&self, user_id: &str, session_id: Option<&str>, limit: i64) -> Result<Vec<FullMessageRow>, sqlx::Error> {
        let sid = session_id.unwrap_or("default");

        sqlx::query_as::<_, FullMessageRow>(
            r#"
            SELECT id, user_id, session_id, role, content, timestamp, metadata FROM (
                SELECT id, user_id, session_id, role, content, timestamp, metadata 
                FROM chat_messages 
                WHERE user_id = $1 AND session_id = $2
                ORDER BY id DESC LIMIT $3
            ) sub ORDER BY id ASC
            "#
        )
        .bind(user_id)
        .bind(sid)
        .bind(limit)
        .fetch_all(&self.ctx.pool)
        .await
    }

    /// Fetches all active sessions matching a user ID
    pub async fn get_all_sessions(&self, user_id: &str) -> Result<Vec<String>, sqlx::Error> {
        let rows = sqlx::query(
            "SELECT session_id FROM chat_sessions WHERE user_id = $1 ORDER BY updated_at DESC LIMIT 50"
        )
        .bind(user_id)
        .fetch_all(&self.ctx.pool)
        .await?;

        Ok(rows.into_iter().map(|r| r.get::<String, _>("session_id")).collect())
    }

    /// Appends message log and trims history to maximum bounds natively in-place
    pub async fn append_message(
        &self,
        user_id: &str,
        role: &str,
        content: &str,
        session_id: Option<&str>,
        metadata: Option<serde_json::Value>,
    ) -> Result<(), sqlx::Error> {
        let sid = session_id.unwrap_or("default");
        let now = Utc::now();
        let meta = metadata.unwrap_or_else(|| serde_json::json!({}));

        // 1. Maintain parent sessions index row
        sqlx::query(
            r#"
            INSERT INTO chat_sessions (user_id, session_id, created_at, updated_at)
            VALUES ($1, $2, $3, $3)
            ON CONFLICT (user_id, session_id) 
            DO UPDATE SET updated_at = EXCLUDED.updated_at
            "#
        )
        .bind(user_id)
        .bind(sid)
        .bind(now)
        .execute(&self.ctx.pool)
        .await?;

        // 2. Append history record
        sqlx::query(
            r#"
            INSERT INTO chat_messages (user_id, session_id, role, content, timestamp, metadata)
            VALUES ($1, $2, $3, $4, $5, $6)
            "#
        )
        .bind(user_id)
        .bind(sid)
        .bind(role)
        .bind(content)
        .bind(now)
        .bind(sqlx::types::Json(meta))
        .execute(&self.ctx.pool)
        .await?;

        // 3. Keep performance tight: Clean history boundaries beyond MAX_HISTORY_LENGTH (100)
        sqlx::query(
            r#"
            DELETE FROM chat_messages 
            WHERE id IN (
                SELECT id FROM chat_messages 
                WHERE user_id = $1 AND session_id = $2 
                ORDER BY id DESC 
                OFFSET 100
            )
            "#
        )
        .bind(user_id)
        .bind(sid)
        .execute(&self.ctx.pool)
        .await?;

        Ok(())
    }

    pub async fn clear_history(&self, user_id: &str, session_id: Option<&str>) -> Result<(), sqlx::Error> {
        let sid = session_id.unwrap_or("default");
        sqlx::query("DELETE FROM chat_messages WHERE user_id = $1 AND session_id = $2")
            .bind(user_id)
            .bind(sid)
            .execute(&self.ctx.pool)
            .await?;
        Ok(())
    }

    pub async fn delete_session(&self, user_id: &str, session_id: Option<&str>) -> Result<(), sqlx::Error> {
        let sid = session_id.unwrap_or("default");
        sqlx::query("DELETE FROM chat_sessions WHERE user_id = $1 AND session_id = $2")
            .bind(user_id)
            .bind(sid)
            .execute(&self.ctx.pool)
            .await?;
        Ok(())
    }
}