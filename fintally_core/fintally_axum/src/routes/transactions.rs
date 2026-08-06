use axum::{
    extract::{Multipart, Path, Query},
    http::{header, HeaderMap, StatusCode},
    response::IntoResponse,
    Extension, Json,
};
use time::{format_description::well_known::Rfc3339, OffsetDateTime};
use fintally_db::DbContext;
use fintally_db::models::RecurringFrequency;
use serde::Deserialize;
use std::path::Path as StdPath;
use tokio::fs;

// Import the Claims struct from your auth module
use crate::auth::Claims;

const UPLOAD_DIR: &str = "./uploads";

#[derive(Deserialize)]
pub struct PaginationQuery {
    pub page: Option<i64>,
}

struct UploadedTransaction {
    name: String,
    price: f64,
    description: String,
    datetime: OffsetDateTime,
    category: String,
    is_recurring: bool,
    recurring_frequency: Option<RecurringFrequency>,
    receipt_bytes: Option<Vec<u8>>,
    receipt_name: Option<String>,
}

pub async fn test_route() -> Json<serde_json::Value> {
    Json(serde_json::json!({ "body": "test ok" }))
}

// POST /api/transaction
pub async fn create_transaction(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
    mut multipart: Multipart,
) -> Result<impl IntoResponse, (StatusCode, String)> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|_| (StatusCode::BAD_REQUEST, "Malformed user authenticating token integer format".to_string()))?;

    let tx = extract_multipart(&mut multipart).await?;

    let mut receipt_url: Option<String> = None;
    if let (Some(bytes), Some(name)) = (tx.receipt_bytes, tx.receipt_name) {
        fs::create_dir_all(UPLOAD_DIR).await.map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;
        let timestamp_millis = (OffsetDateTime::now_utc().unix_timestamp_nanos() / 1_000_000) as i64;
        let filename = format!("{}_{}", timestamp_millis, name);
        let path = StdPath::new(UPLOAD_DIR).join(&filename);
        fs::write(path, bytes).await.map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;
        receipt_url = Some(format!("/uploads/{}", filename));
    }

    // Convert OffsetDateTime to an i64 Unix timestamp
    let datetime_i64 = tx.datetime.unix_timestamp();

    let rec = sqlx::query!(
        r#"INSERT INTO transactions (user_id, name, price, description, datetime, category, is_recurring, recurring_frequency, receipt_url)
          VALUES ($1, $2, $3, $4, $5, $6, $7, $8::recurring_frequency, $9) RETURNING id"#,
        user_id, tx.name, tx.price, tx.description, datetime_i64, tx.category, tx.is_recurring, tx.recurring_frequency as Option<RecurringFrequency>, receipt_url
    )
    .fetch_one(&db_ctx.pool)
    .await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;

    Ok((StatusCode::CREATED, Json(serde_json::json!({ "success": true, "id": rec.id }))))
}

// GET /api/transaction
pub async fn get_transactions(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
    Query(pagination): Query<PaginationQuery>,
) -> Result<impl IntoResponse, (StatusCode, String)> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|_| (StatusCode::BAD_REQUEST, "Malformed user authenticating token integer format".to_string()))?;

    let limit = 10;
    let page = pagination.page.unwrap_or(1).max(1);
    let offset = (page - 1) * limit;

    let rows = sqlx::query!(
        r#"SELECT id, user_id, name, price, description, datetime, category, is_recurring, recurring_frequency as "recurring_frequency: RecurringFrequency", receipt_url FROM transactions WHERE user_id = $1 ORDER BY datetime DESC LIMIT $2 OFFSET $3"#,
        user_id, limit, offset
    )
    .fetch_all(&db_ctx.pool)
    .await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;

    let list: Vec<serde_json::Value> = rows.into_iter().map(|r| {
        serde_json::json!({
            "id": r.id,
            "name": r.name,
            "price": r.price,
            "description": r.description,
            "datetime": r.datetime,
            "category": r.category,
            "isRecurring": r.is_recurring,
            "recurringFrequency": r.recurring_frequency,
            "receiptUrl": r.receipt_url
        })
    }).collect();

    Ok(Json(serde_json::json!({ "success": true, "data": list })))
}

// PUT /api/transaction/:id
pub async fn update_transaction(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
    Path(transaction_id): Path<i64>,
    mut multipart: Multipart,
) -> Result<impl IntoResponse, (StatusCode, String)> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|_| (StatusCode::BAD_REQUEST, "Malformed user authenticating token integer format".to_string()))?;

    let existing = sqlx::query!(
        "SELECT receipt_url FROM transactions WHERE id = $1 AND user_id = $2",
        transaction_id,
        user_id
    )
    .fetch_optional(&db_ctx.pool)
    .await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?
    .ok_or_else(|| (StatusCode::NOT_FOUND, "Transaction not found".to_string()))?;

    // ... (Your existing multipart extraction logic stays the same) ...
    let mut name: Option<String> = None;
    let mut price: Option<f64> = None;
    let mut description: Option<String> = None;
    let mut datetime: Option<OffsetDateTime> = None;
    let mut category: Option<String> = None;
    let mut is_recurring: Option<bool> = None;
    let mut recurring_frequency: Option<RecurringFrequency> = None;
    let mut receipt_bytes: Option<Vec<u8>> = None;
    let mut receipt_name: Option<String> = None;

    while let Some(field) = multipart.next_field().await.map_err(|e| (StatusCode::BAD_REQUEST, e.to_string()))? {
        let field_name = field.name().unwrap_or("").to_string();
        if field_name == "receipt" {
            receipt_name = field.file_name().map(String::from);
            receipt_bytes = Some(field.bytes().await.map_err(|e| (StatusCode::BAD_REQUEST, e.to_string()))?.to_vec());
        } else {
            let value = field.text().await.map_err(|e| (StatusCode::BAD_REQUEST, e.to_string()))?;
            if value.is_empty() { continue; }
            match field_name.as_str() {
                "name" => name = Some(value),
                "price" => price = Some(value.parse::<f64>().unwrap_or(0.0)),
                "description" => description = Some(value),
                "datetime" => datetime = Some(OffsetDateTime::parse(&value, &Rfc3339).unwrap_or_else(|_| OffsetDateTime::now_utc())),
                "category" => category = Some(value),
                "isRecurring" => is_recurring = Some(value.parse::<bool>().unwrap_or(false)),
                "recurringFrequency" => {
                    recurring_frequency = match value.as_str() {
                        "Daily" => Some(RecurringFrequency::Daily),
                        "Weekly" => Some(RecurringFrequency::Weekly),
                        "Monthly" => Some(RecurringFrequency::Monthly),
                        _ => None,
                    }
                }
                _ => {}
            }
        }
    }

    let mut new_receipt_url = existing.receipt_url;
    if let (Some(bytes), Some(file_name)) = (receipt_bytes, receipt_name) {
        if let Some(old_url) = &new_receipt_url {
            let _ = fs::remove_file(format!(".{}", old_url)).await;
        }
        let timestamp_millis = (OffsetDateTime::now_utc().unix_timestamp_nanos() / 1_000_000) as i64;
        let filename = format!("{}_{}", timestamp_millis, file_name);
        fs::write(StdPath::new(UPLOAD_DIR).join(&filename), bytes).await.map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;
        new_receipt_url = Some(format!("/uploads/{}", filename));
    }

    // Convert Option<OffsetDateTime> to Option<i64>
    let datetime_i64 = datetime.map(|dt| dt.unix_timestamp());

    sqlx::query!(
        r#"UPDATE transactions
          SET name = COALESCE($1, name),
              price = COALESCE($2, price),
              description = COALESCE($3, description),
              datetime = COALESCE($4, datetime),
              category = COALESCE($5, category),
              is_recurring = COALESCE($6, is_recurring),
              recurring_frequency = COALESCE($7::recurring_frequency, recurring_frequency),
              receipt_url = COALESCE($8, receipt_url)
          WHERE id = $9 AND user_id = $10"#,
        name, price, description, datetime_i64, category, is_recurring, recurring_frequency as Option<RecurringFrequency>, new_receipt_url, transaction_id, user_id
    )
    .execute(&db_ctx.pool)
    .await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;

    Ok(Json(serde_json::json!({ "success": true, "message": "Transaction updated successfully" })))
}

// DELETE /api/transaction/:id
pub async fn delete_transaction(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
    Path(transaction_id): Path<i64>,
) -> Result<impl IntoResponse, (StatusCode, String)> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|_| (StatusCode::BAD_REQUEST, "Malformed user authenticating token integer format".to_string()))?;

    let record = sqlx::query!(
        "SELECT receipt_url FROM transactions WHERE id = $1 AND user_id = $2",
        transaction_id,
        user_id
    )
    .fetch_optional(&db_ctx.pool)
    .await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?
    .ok_or_else(|| (StatusCode::NOT_FOUND, "Transaction not found".to_string()))?;

    if let Some(url) = record.receipt_url {
        let _ = fs::remove_file(format!(".{}", url)).await;
    }

    sqlx::query!("DELETE FROM transactions WHERE id = $1 AND user_id = $2", transaction_id, user_id)
        .execute(&db_ctx.pool)
        .await
        .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;

    Ok(Json(serde_json::json!({ "success": true, "message": "Transaction deleted successfully" })))
}

// GET /api/transaction/receipt/:id
pub async fn get_receipt(
    Extension(db_ctx): Extension<DbContext>,
    claims: Claims,
    Path(transaction_id): Path<i64>,
) -> Result<impl IntoResponse, (StatusCode, String)> {
    let user_id = claims.user_id.parse::<i64>()
        .map_err(|_| (StatusCode::BAD_REQUEST, "Malformed user authenticating token integer format".to_string()))?;

    let r = sqlx::query!(
        "SELECT id, name, price, description, category FROM transactions WHERE id = $1 AND user_id = $2",
        transaction_id,
        user_id
    )
    .fetch_optional(&db_ctx.pool)
    .await
    .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?
    .ok_or_else(|| (StatusCode::NOT_FOUND, "Transaction not found".to_string()))?;

    let font_family = genpdf::fonts::from_files("./fonts", "LiberationSans", None)
        .map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, format!("Font error: {}", e)))?;

    let mut doc = genpdf::Document::new(font_family);
    doc.set_title("FinTally Transaction Receipt");

    let mut decorator = genpdf::SimplePageDecorator::new();
    decorator.set_margins(15);
    doc.set_page_decorator(decorator);

    doc.push(genpdf::elements::Text::new("FinTally"));
    doc.push(genpdf::elements::Text::new("Transaction Receipt"));

    let mut table = genpdf::elements::TableLayout::new(vec![1, 3]);
    let _ = table.row()
        .element(genpdf::elements::Paragraph::new("Transaction ID"))
        .element(genpdf::elements::Paragraph::new(r.id.to_string()))
        .push();
    let _ = table.row()
        .element(genpdf::elements::Paragraph::new("Name"))
        .element(genpdf::elements::Paragraph::new(r.name))
        .push();
    let _ = table.row()
        .element(genpdf::elements::Paragraph::new("Amount"))
        .element(genpdf::elements::Paragraph::new(format!("Rs.{}", r.price)))
        .push();
    let _ = table.row()
        .element(genpdf::elements::Paragraph::new("Category"))
        .element(genpdf::elements::Paragraph::new(r.category))
        .push();

    doc.push(table);

    let mut buffer = Vec::new();
    doc.render(&mut buffer).map_err(|e| (StatusCode::INTERNAL_SERVER_ERROR, e.to_string()))?;

    let mut headers = HeaderMap::new();
    headers.insert(header::CONTENT_TYPE, "application/pdf".parse().unwrap());
    headers.insert(header::CONTENT_DISPOSITION, format!("attachment; filename=\"receipt_{}.pdf\"", transaction_id).parse().unwrap());

    Ok((headers, buffer))
}

async fn extract_multipart(multipart: &mut Multipart) -> Result<UploadedTransaction, (StatusCode, String)> {
    let mut name = String::new();
    let mut price = 0.0;
    let mut description = String::new();
    let mut datetime = OffsetDateTime::now_utc();
    let mut category = "General".to_string();
    let mut is_recurring = false;
    let mut recurring_frequency = None;
    let mut receipt_bytes = None;
    let mut receipt_name = None;

    while let Some(field) = multipart.next_field().await.map_err(|e| (StatusCode::BAD_REQUEST, e.to_string()))? {
        let field_name = field.name().unwrap_or("").to_string();
        if field_name == "receipt" {
            receipt_name = field.file_name().map(String::from);
            receipt_bytes = Some(field.bytes().await.map_err(|e| (StatusCode::BAD_REQUEST, e.to_string()))?.to_vec());
        } else {
            let value = field.text().await.map_err(|e| (StatusCode::BAD_REQUEST, e.to_string()))?;
            match field_name.as_str() {
                "name" => name = value,
                "price" => price = value.parse::<f64>().unwrap_or(0.0),
                "description" => description = value,
                "datetime" => datetime = OffsetDateTime::parse(&value, &Rfc3339).unwrap_or_else(|_| OffsetDateTime::now_utc()),
                "category" => category = value,
                "isRecurring" => is_recurring = value.parse::<bool>().unwrap_or(false),
                "recurringFrequency" => {
                    recurring_frequency = match value.as_str() {
                        "Daily" => Some(RecurringFrequency::Daily),
                        "Weekly" => Some(RecurringFrequency::Weekly),
                        "Monthly" => Some(RecurringFrequency::Monthly),
                        _ => None,
                    }
                }
                _ => {}
            }
        }
    }
    Ok(UploadedTransaction { name, price, description, datetime, category, is_recurring, recurring_frequency, receipt_bytes, receipt_name })
}
