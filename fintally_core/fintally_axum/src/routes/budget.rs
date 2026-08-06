use axum::{
    extract::Path,
    http::StatusCode,
    response::IntoResponse,
    Extension, Json,
};
use fintally_db::DbContext;
use fintally_finance::budget_projection_batch;
use serde::Deserialize;
use time::OffsetDateTime;

use crate::auth::Claims;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BudgetCreate {
    pub amount: f64,
    pub category: String,
    pub start_date: Option<OffsetDateTime>,
    pub end_date: Option<OffsetDateTime>,
    pub is_recurring: bool,
}

pub enum BudgetApiError {
    DatabaseError(String),
    EngineError(String),
    NotFound,
}

impl IntoResponse for BudgetApiError {
    fn into_response(self) -> axum::response::Response {
        let (status, msg) = match self {
            BudgetApiError::DatabaseError(err) => (StatusCode::INTERNAL_SERVER_ERROR, err),
            BudgetApiError::EngineError(err) => (StatusCode::BAD_REQUEST, err),
            BudgetApiError::NotFound => (StatusCode::NOT_FOUND, "Budget allocation target not found.".to_string()),
        };
        (status, Json(serde_json::json!({ "success": false, "error": msg }))).into_response()
    }
}

// ──────────────────────────────────────────────────────────────
// POST /api/budget — Create or Update
// ──────────────────────────────────────────────────────────────
pub async fn create_or_update_budget(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
    Json(body): Json<BudgetCreate>,
) -> Result<impl IntoResponse, BudgetApiError> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|e| BudgetApiError::EngineError(format!("Invalid User ID: {}", e)))?;

    let start = body.start_date.unwrap_or_else(OffsetDateTime::now_utc).unix_timestamp();
    let end = body.end_date.map(|dt| dt.unix_timestamp());

    // Check for an overlapping existing budget setup
    let existing = sqlx::query!(
        r#"
        SELECT id FROM budgets
        WHERE user_id = $1
          AND category = $2
          AND start_date <= COALESCE($3, EXTRACT(EPOCH FROM NOW())::BIGINT)
          AND (end_date IS NULL OR end_date >= $4)
        LIMIT 1
        "#,
        user_id,
        body.category,
        end,
        start
    )
    .fetch_optional(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    if let Some(record) = existing {
        let updated = sqlx::query!(
            r#"
            UPDATE budgets
            SET amount = $1, start_date = $2, end_date = $3, is_recurring = $4
            WHERE id = $5
            RETURNING id, category, amount, start_date, end_date, is_recurring
            "#,
            body.amount,
            start,
            end,
            body.is_recurring,
            record.id
        )
        .fetch_one(&db_ctx.pool)
        .await
        .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

        let start_dt = updated.start_date.and_then(|ts| OffsetDateTime::from_unix_timestamp(ts).ok());
        let end_dt = updated.end_date.and_then(|ts| OffsetDateTime::from_unix_timestamp(ts).ok());

        return Ok((StatusCode::OK, Json(serde_json::json!({
            "success": true,
            "data": {
                "id": updated.id,
                "category": updated.category,
                "amount": updated.amount,
                "startDate": start_dt.map(|dt| dt.format(&time::format_description::well_known::Rfc3339).unwrap_or_default()),
                "endDate": end_dt.map(|dt| dt.format(&time::format_description::well_known::Rfc3339).unwrap_or_default()),
                "is_recurring": updated.is_recurring
            }
        }))));
    }

    let inserted = sqlx::query!(
        r#"
        INSERT INTO budgets (user_id, amount, category, start_date, end_date, is_recurring)
        VALUES ($1, $2, $3, $4, $5, $6)
        RETURNING id, category, amount, start_date, end_date, is_recurring
        "#,
        user_id,
        body.amount,
        body.category,
        start,
        end,
        body.is_recurring
    )
    .fetch_one(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    let start_dt = inserted.start_date.and_then(|ts| OffsetDateTime::from_unix_timestamp(ts).ok());
    let end_dt = inserted.end_date.and_then(|ts| OffsetDateTime::from_unix_timestamp(ts).ok());

    Ok((StatusCode::CREATED, Json(serde_json::json!({
        "success": true,
        "data": {
            "id": inserted.id,
            "category": inserted.category,
            "amount": inserted.amount,
            "startDate": start_dt.map(|dt| dt.format(&time::format_description::well_known::Rfc3339).unwrap_or_default()),
            "endDate": end_dt.map(|dt| dt.format(&time::format_description::well_known::Rfc3339).unwrap_or_default()),
            "is_recurring": inserted.is_recurring
        }
    }))))
}

// ──────────────────────────────────────────────────────────────
// GET /api/budget — List all budgets
// ──────────────────────────────────────────────────────────────
pub async fn get_budgets(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
) -> Result<impl IntoResponse, BudgetApiError> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|e| BudgetApiError::EngineError(format!("Invalid User ID: {}", e)))?;

    let rows = sqlx::query!(
        r#"SELECT id, category, amount, start_date, end_date, is_recurring
           FROM budgets WHERE user_id = $1 ORDER BY id DESC"#,
        user_id
    )
    .fetch_all(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    let response_data: Vec<_> = rows.into_iter().map(|r| {
        let start_dt = r.start_date.and_then(|ts| OffsetDateTime::from_unix_timestamp(ts).ok());
        let end_dt = r.end_date.and_then(|ts| OffsetDateTime::from_unix_timestamp(ts).ok());

        serde_json::json!({
            "id": r.id,
            "category": r.category,
            "amount": r.amount,
            "startDate": start_dt.map(|dt| dt.format(&time::format_description::well_known::Rfc3339).unwrap_or_default()),
            "endDate": end_dt.map(|dt| dt.format(&time::format_description::well_known::Rfc3339).unwrap_or_default()),
            "is_recurring": r.is_recurring
        })
    }).collect();

    Ok(Json(serde_json::json!({ "success": true, "data": response_data })))
}

// ──────────────────────────────────────────────────────────────
// GET /api/budget/summary
// ──────────────────────────────────────────────────────────────
pub async fn budget_summary(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
) -> Result<impl IntoResponse, BudgetApiError> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|e| BudgetApiError::EngineError(format!("Invalid User ID: {}", e)))?;

    let user_budgets = sqlx::query!(
        r#"SELECT category, amount, start_date, end_date FROM budgets WHERE user_id = $1"#,
        user_id
    )
    .fetch_all(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    if user_budgets.is_empty() {
        return Ok(Json(serde_json::json!({ "success": true, "data": [] })));
    }

    let expense_rows = sqlx::query!(
        r#"SELECT category as "category!", COALESCE(SUM(ABS(price)), 0.0) as "total!"
           FROM transactions WHERE user_id = $1 AND price < 0 GROUP BY category"#,
        user_id
    )
    .fetch_all(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    let total_spent: f64 = expense_rows.iter().map(|r| r.total).sum();

    let mut spent_per_budget: Vec<f64> = Vec::with_capacity(user_budgets.len());
    for b in &user_budgets {
        if b.category == "Overall" {
            spent_per_budget.push(total_spent);
        } else {
            let matched_val = expense_rows.iter().find(|r| r.category == b.category).map(|r| r.total).unwrap_or(0.0);
            spent_per_budget.push(matched_val);
        }
    }

    let budget_amounts_paise: Vec<i64> = user_budgets.iter().map(|b| (b.amount * 100.0).round() as i64).collect();
    let spent_paise: Vec<i64> = spent_per_budget.iter().map(|s| (s * 100.0).round() as i64).collect();
    let zero_rates = vec![0_i64; user_budgets.len()];
    let one_month = vec![1_i32; user_budgets.len()];

    let rust_result = budget_projection_batch(&zero_rates, &budget_amounts_paise, &spent_paise, &one_month)
        .map_err(|e| BudgetApiError::EngineError(e))?;

    let warning_labels = ["healthy", "warning", "over_budget"];
    let mut summaries = Vec::with_capacity(user_budgets.len());

    for (i, b) in user_budgets.into_iter().enumerate() {
        let start_dt = b.start_date.and_then(|ts| OffsetDateTime::from_unix_timestamp(ts).ok());
        let end_dt = b.end_date.and_then(|ts| OffsetDateTime::from_unix_timestamp(ts).ok());

        let start_str = start_dt
            .map(|dt| format!("{}-{:02}-{:02}", dt.year(), u8::from(dt.month()), dt.day()))
            .unwrap_or_else(|| "N/A".to_string());
        let end_str = end_dt
            .map(|dt| format!("{}-{:02}-{:02}", dt.year(), u8::from(dt.month()), dt.day()))
            .unwrap_or_else(|| "Ongoing".to_string());

        let remaining = (b.amount - spent_per_budget[i]).max(0.0);
        let flag_index = rust_result.warning_flag[i] as usize;

        summaries.push(serde_json::json!({
            "category": b.category,
            "budget": b.amount,
            "spent": spent_per_budget[i],
            "remaining": remaining,
            "usagePercent": rust_result.usage_percent[i].round(),
            "status": warning_labels.get(flag_index).unwrap_or(&"healthy"),
            "period": format!("{} - {}", start_str, end_str),
        }));
    }

    Ok(Json(serde_json::json!({ "success": true, "data": summaries })))
}

// ──────────────────────────────────────────────────────────────
// DELETE /api/budget/:id
// ──────────────────────────────────────────────────────────────
pub async fn delete_budget(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
    Path(budget_id): Path<i64>,
) -> Result<impl IntoResponse, BudgetApiError> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|e| BudgetApiError::EngineError(format!("Invalid User ID: {}", e)))?;

    let result = sqlx::query!(
        "DELETE FROM budgets WHERE id = $1 AND user_id = $2",
        budget_id,
        user_id
    )
    .execute(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    if result.rows_affected() == 0 {
        return Err(BudgetApiError::NotFound);
    }

    Ok(Json(serde_json::json!({ "success": true, "message": "Budget allocation cleared down successfully" })))
}
