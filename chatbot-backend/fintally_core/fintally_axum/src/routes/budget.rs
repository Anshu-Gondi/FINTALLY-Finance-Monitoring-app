use axum::{
    extract::Path,
    http::StatusCode,
    response::IntoResponse,
    Extension, Json,
};
use chrono::{DateTime, Utc};
use fintally_db::DbContext;
use fintally_finance::budget_projection_batch;
use serde::Deserialize;
use uuid::Uuid;
use crate::auth::Claims;

#[derive(Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct BudgetCreate {
    pub amount: f64, 
    pub category: String,
    pub start_date: Option<DateTime<Utc>>,
    pub end_date: Option<DateTime<Utc>>,
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
    let start = body.start_date.unwrap_or_else(Utc::now);
    let end = body.end_date;

    // Pass &claims.user_id directly as &str
    let existing = sqlx::query!(
        r#"
        SELECT id FROM budgets 
        WHERE user_id = $1 
          AND category = $2 
          AND start_date <= COALESCE($3, NOW())
          AND (end_date IS NULL OR end_date >= $4)
        LIMIT 1
        "#,
        &claims.user_id, 
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
            body.amount, start, end, body.is_recurring, record.id
        )
        .fetch_one(&db_ctx.pool)
        .await
        .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

        return Ok((StatusCode::OK, Json(serde_json::json!({ 
            "success": true, 
            "data": {
                "id": updated.id,
                "category": updated.category,
                "amount": updated.amount,
                "startDate": updated.start_date,
                "endDate": updated.end_date,
                "is_recurring": updated.is_recurring
            }
        }))));
    }

    // Pass &claims.user_id directly as &str
    let inserted = sqlx::query!(
        r#"
        INSERT INTO budgets (user_id, amount, category, start_date, end_date, is_recurring)
        VALUES ($1, $2, $3, $4, $5, $6)
        RETURNING id, category, amount, start_date, end_date, is_recurring
        "#,
        &claims.user_id, body.amount, body.category, start, end, body.is_recurring
    )
    .fetch_one(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    Ok((StatusCode::CREATED, Json(serde_json::json!({ 
        "success": true, 
        "data": {
            "id": inserted.id,
            "category": inserted.category,
            "amount": inserted.amount,
            "startDate": inserted.start_date,
            "endDate": inserted.end_date,
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
    let rows = sqlx::query!(
        r#"SELECT id, category, amount, start_date, end_date, is_recurring 
           FROM budgets WHERE user_id = $1 ORDER BY id DESC"#,
        &claims.user_id
    )
    .fetch_all(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    let response_data: Vec<_> = rows.into_iter().map(|r| {
        serde_json::json!({
            "id": r.id,
            "category": r.category,
            "amount": r.amount,
            "startDate": r.start_date,
            "endDate": r.end_date,
            "is_recurring": r.is_recurring
        })
    }).collect();

    Ok(Json(serde_json::json!({ "success": true, "data": response_data })))
}

// ──────────────────────────────────────────────────────────────
// GET /api/budget/summary — Realtime calculations using Rust Engine
// ──────────────────────────────────────────────────────────────
pub async fn budget_summary(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
) -> Result<impl IntoResponse, BudgetApiError> {
    // 1. Fetch user budgets (expects &str/String)
    let user_budgets = sqlx::query!(
        r#"SELECT category, amount, start_date, end_date FROM budgets WHERE user_id = $1"#,
        &claims.user_id
    )
    .fetch_all(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    if user_budgets.is_empty() {
        return Ok(Json(serde_json::json!({ "success": true, "data": [] })));
    }

    // 2. Parse string into actual Uuid for the transactions table lookup
    let user_id_uuid = Uuid::parse_str(&claims.user_id)
        .map_err(|e| BudgetApiError::EngineError(format!("Invalid User UUID format: {}", e)))?;

    // 3. Compute dynamic aggregate expenditures from transactions (expects Uuid)
    let expense_rows = sqlx::query!(
        r#"SELECT category as "category!", COALESCE(SUM(ABS(price)), 0.0) as "total!" 
           FROM transactions WHERE user_id = $1 AND price < 0 GROUP BY category"#,
        user_id_uuid // <-- Use parsed Uuid here
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

    // Convert values down into standard integer subunits (Paise) for your mathematical engine
    let budget_amounts_paise: Vec<i64> = user_budgets.iter().map(|b| (b.amount * 100.0).round() as i64).collect();
    let spent_paise: Vec<i64> = spent_per_budget.iter().map(|s| (s * 100.0).round() as i64).collect();
    let zero_rates = vec![0_i64; user_budgets.len()];
    let one_month = vec![1_i32; user_budgets.len()];

    let rust_result = budget_projection_batch(&zero_rates, &budget_amounts_paise, &spent_paise, &one_month)
        .map_err(|e| BudgetApiError::EngineError(e))?;

    let warning_labels = ["healthy", "warning", "over_budget"];
    let mut summaries = Vec::with_capacity(user_budgets.len());

    for (i, b) in user_budgets.into_iter().enumerate() {
        let start_str = b.start_date.map(|dt| dt.format("%Y-%m-%d").to_string()).unwrap_or_else(|| "N/A".to_string());
        let end_str = b.end_date.map(|dt| dt.format("%Y-%m-%d").to_string()).unwrap_or_else(|| "Ongoing".to_string());
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
    let result = sqlx::query!(
        "DELETE FROM budgets WHERE id = $1 AND user_id = $2",
        budget_id,
        &claims.user_id
    )
    .execute(&db_ctx.pool)
    .await
    .map_err(|e| BudgetApiError::DatabaseError(e.to_string()))?;

    if result.rows_affected() == 0 {
        return Err(BudgetApiError::NotFound);
    }

    Ok(Json(serde_json::json!({ "success": true, "message": "Budget allocation cleared down successfully" })))
}