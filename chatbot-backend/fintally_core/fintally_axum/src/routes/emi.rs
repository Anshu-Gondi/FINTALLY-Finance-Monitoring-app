use axum::{ extract::{ Extension, Json, Path }, http::StatusCode, response::IntoResponse };
use chrono::Utc;
use serde::Serialize;
use serde_json::json;
use sqlx::Row; // ◄─ CRITICAL: Required to use row.get() on anonymous rows

// Bring in your existing shared structural models from your workspace
use fintally_db::{
    DbContext,
    models::{ EmiCalculateRequest, EmiCheckRequest, EmiCreateRequest, RecurringFrequency },
};
use crate::auth::Claims; // Your extractor structure
use fintally_finance::*; // Core finance calculations module

// ─── Shared Response Payloads ────────────────────────────────────────────────

#[derive(Serialize)]
pub struct ApiResponse<T> {
    pub success: bool,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub message: Option<String>,
    #[serde(skip_serializing_if = "Option::is_none")]
    pub data: Option<T>,
}

#[derive(Serialize)]
pub struct EmiMathData {
    pub emi: f64,
    #[serde(rename = "totalPayable")]
    pub total_payable: f64,
    #[serde(rename = "totalInterest")]
    pub total_interest: f64,
}

#[derive(Serialize)]
pub struct BudgetImpactData {
    pub affordable: bool,
    pub emi: f64,
    #[serde(rename = "budgetLimit")]
    pub budget_limit: Option<f64>,
    #[serde(rename = "currentSpent")]
    pub current_spent: f64,
    #[serde(rename = "remainingBudget")]
    pub remaining_budget: Option<f64>,
    pub message: String,
}

#[derive(Serialize)]
pub struct EmiCheckResponse {
    pub emi: f64,
    #[serde(rename = "totalPayable")]
    pub total_payable: f64,
    #[serde(rename = "totalInterest")]
    pub total_interest: f64,
    #[serde(rename = "budgetImpact")]
    pub budget_impact: BudgetImpactData,
}

#[derive(Serialize)]
pub struct EmiCreateResponse {
    pub emi: f64,
    #[serde(rename = "transactionId")]
    pub transaction_id: String,
    #[serde(rename = "budgetImpact")]
    pub budget_impact: BudgetImpactData,
}

// ─── Scalar Finance Calculation ──────────────────────────────────────────────

fn calculate_single_emi(principal: f64, annual_rate: f64, months: i32) -> f64 {
    let principal_paise = (principal * 100.0).round() as i64;

    if let Ok(res_vector) = fintally_finance::emi_batch(
        &[principal_paise],
        &[annual_rate],
        &[months]
    ) {
        if let Some(&paise_output) = res_vector.first() {
            return (((paise_output as f64) / 100.0) * 100.0).round() / 100.0;
        }
    }

    if annual_rate == 0.0 || months <= 0 {
        return ((principal / (months as f64)) * 100.0).round() / 100.0;
    }
    let r = (annual_rate * 0.01) / 12.0;
    let factor = (1.0 + r).powi(months);
    let emi = (principal * r * factor) / (factor - 1.0);
    (emi * 100.0).round() / 100.0
}

pub async fn emi_calculate(
    _claims: Claims, 
    Json(body): Json<EmiCalculateRequest>
) -> impl IntoResponse {
    let emi = calculate_single_emi(body.principal, body.annual_rate, body.months);
    let months_f = body.months as f64;

    let data = EmiMathData {
        emi,
        total_payable: (emi * months_f * 100.0).round() / 100.0,
        total_interest: ((emi * months_f - body.principal) * 100.0).round() / 100.0,
    };

    (
        StatusCode::OK,
        Json(ApiResponse {
            success: true,
            message: None,
            data: Some(data),
        }),
    )
}

pub async fn emi_check(
    Extension(ctx): Extension<DbContext>,
    claims: Claims,
    Json(body): Json<EmiCheckRequest>
) -> impl IntoResponse {
    let emi = calculate_single_emi(body.principal, body.annual_rate, body.months);
    let months_f = body.months as f64;

    // Fixed: Used .bind() chains and row.get("amount") extraction
    let budget_row = sqlx::query(
        r#"
        SELECT amount FROM budgets 
        WHERE user_id = $1 AND (category = $2 OR category = 'Overall')
        ORDER BY (category = $2) DESC 
        LIMIT 1
        "#,
    )
    .bind(&claims.user_id)
    .bind(&body.category)
    .fetch_optional(&ctx.pool)
    .await;

    let budget_limit: Option<f64> = match budget_row {
        Ok(Some(row)) => Some(row.get("amount")),
        _ => None,
    };

    // Fixed: Used .bind() chains and row.get("total") extraction
    let spent_row = sqlx::query(
        r#"
        SELECT COALESCE(SUM(ABS(price)), 0.0) as total 
        FROM transactions
        WHERE user_id = $1 AND price < 0 
          AND ($2 = 'Overall' OR category = $2)
        "#,
    )
    .bind(&claims.user_id)
    .bind(&body.category)
    .fetch_one(&ctx.pool)
    .await;

    let current_spent: f64 = spent_row
        .map(|r| r.get::<f64, &str>("total"))
        .unwrap_or(0.0);

    let budget_impact = match budget_limit {
        None => BudgetImpactData {
            affordable: true,
            emi,
            budget_limit: None,
            current_spent,
            remaining_budget: None,
            message: "No budget set — EMI checked without constraints.".to_string(),
        },
        Some(limit) => {
            let remaining = limit - current_spent;
            let affordable = emi <= remaining;
            let message = if affordable {
                "EMI fits within your budget.".to_string()
            } else {
                format!("EMI of ₹{:.2} exceeds remaining budget of ₹{:.2}.", emi, remaining)
            };

            BudgetImpactData {
                affordable,
                emi,
                budget_limit: Some(limit),
                current_spent,
                remaining_budget: Some(remaining),
                message,
            }
        }
    };

    let response_data = EmiCheckResponse {
        emi,
        total_payable: (emi * months_f * 100.0).round() / 100.0,
        total_interest: ((emi * months_f - body.principal) * 100.0).round() / 100.0,
        budget_impact,
    };

    (
        StatusCode::OK,
        Json(ApiResponse {
            success: true,
            message: None,
            data: Some(response_data),
        }),
    )
}

pub async fn emi_create(
    Extension(ctx): Extension<DbContext>,
    claims: Claims,
    Json(body): Json<EmiCreateRequest>
) -> impl IntoResponse {
    let emi = calculate_single_emi(body.principal, body.annual_rate, body.months);

    let mut tx = match ctx.pool.begin().await {
        Ok(t) => t,
        Err(_) => {
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(json!({"success": false, "error": "Database error"})),
            ).into_response();
        }
    };

    // Fixed: Applied .bind() chain and row.get("amount")
    let budget_row = sqlx::query(
        r#"SELECT amount FROM budgets WHERE user_id = $1 AND (category = $2 OR category = 'Overall') ORDER BY (category = $2) DESC LIMIT 1"#,
    )
    .bind(&claims.user_id)
    .bind(&body.category)
    .fetch_optional(&mut *tx)
    .await;

    let budget_limit: Option<f64> = budget_row
        .ok()
        .flatten()
        .map(|r| r.get("amount"));

    // Fixed: Applied .bind() chain and row.get("total")
    let spent_row = sqlx::query(
        r#"SELECT COALESCE(SUM(ABS(price)), 0.0) as total FROM transactions WHERE user_id = $1 AND price < 0 AND ($2 = 'Overall' OR category = $2)"#,
    )
    .bind(&claims.user_id)
    .bind(&body.category)
    .fetch_one(&mut *tx)
    .await;

    let current_spent: f64 = spent_row
        .map(|r| r.get::<f64, &str>("total"))
        .unwrap_or(0.0);

    let mut budget_impact = BudgetImpactData {
        affordable: true,
        emi,
        budget_limit,
        current_spent,
        remaining_budget: None,
        message: "EMI fits within budget rules.".to_string(),
    };

    if let Some(limit) = budget_limit {
        let remaining = limit - current_spent;
        budget_impact.remaining_budget = Some(remaining);
        budget_impact.affordable = emi <= remaining;
        if !budget_impact.affordable {
            return (
                StatusCode::BAD_REQUEST,
                Json(json!({
                    "success": false,
                    "detail": { "message": "EMI exceeds your budget", "budgetImpact": budget_impact }
                })),
            ).into_response();
        }
    }

    let description = format!("EMI for loan ({} months @ {}%)", body.months, body.annual_rate);

    // Fixed: Removed missing emiMeta column entirely based on your visual schema inspection!
    let inserted_row = sqlx::query(
        r#"
        INSERT INTO transactions (
            name, price, description, datetime, category, user_id, 
            is_recurring, recurring_frequency
        )
        VALUES ($1, $2, $3, $4, $5, $6, $7, $8::recurring_frequency)
        RETURNING id
        "#,
    )
    .bind(&body.name)
    .bind(-emi)
    .bind(&description)
    .bind(Utc::now())
    .bind(&body.category)
    .bind(&claims.user_id)
    .bind(true)
    .bind(RecurringFrequency::Monthly)
    .fetch_one(&mut *tx)
    .await;

    // Fixed: Extracted row.get("id") safely with types
    let row_id: i64 = match inserted_row {
        Ok(r) => r.get("id"),
        Err(e) => {
            let _ = tx.rollback().await;
            return (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(json!({"success": false, "error": format!("Insertion failure: {}", e)})),
            ).into_response();
        }
    };

    if tx.commit().await.is_err() {
        return (
            StatusCode::INTERNAL_SERVER_ERROR,
            Json(json!({"success": false, "error": "Commit failure"})),
        ).into_response();
    }

    let out = EmiCreateResponse {
        emi,
        transaction_id: row_id.to_string(),
        budget_impact,
    };

    (
        StatusCode::OK,
        Json(ApiResponse {
            success: true,
            message: Some("EMI added as recurring transaction".to_string()),
            data: Some(out),
        }),
    ).into_response()
}

// ─── DELETE /api/emi/:id ─────────────────────────────────────────────────────
pub async fn emi_delete(
    Extension(ctx): Extension<DbContext>,
    claims: Claims,
    Path(id): Path<i32> 
) -> impl IntoResponse {
    // Fixed: Configured runtime binds matching column maps
    let result = sqlx::query(
        r#"
        DELETE FROM transactions
        WHERE id = $1 
          AND user_id = $2 
          AND is_recurring = true
        "#,
    )
    .bind(id as i64) // Matches your int8 primary key type shown in Screenshot 2026-07-03 163729.png
    .bind(&claims.user_id)
    .execute(&ctx.pool)
    .await;

    match result {
        Ok(postgres_result) => {
            if postgres_result.rows_affected() == 0 {
                return (
                    StatusCode::NOT_FOUND,
                    Json(ApiResponse::<()> {
                        success: false,
                        message: Some("EMI record not found or you are not authorized to delete it.".to_string()),
                        data: None,
                    }),
                );
            }

            (
                StatusCode::OK,
                Json(ApiResponse::<()> {
                    success: true,
                    message: Some("EMI recurring transaction successfully deleted.".to_string()),
                    data: None,
                }),
            )
        }
        Err(err) => {
            (
                StatusCode::INTERNAL_SERVER_ERROR,
                Json(ApiResponse::<()> {
                    success: false,
                    message: Some(format!("Database error occurred: {}", err)),
                    data: None,
                }),
            )
        }
    }
}