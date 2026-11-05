//! Storage-independent transaction application service.
//!
//! Input validation, compatibility mapping, response bounds and typed errors
//! live here. SQL remains in the PostgreSQL/SQLite repository crates. The
//! kernel chooses exactly one repository from the authenticated session's
//! storage scope; this module never silently falls back between databases.

pub mod recurrence;

use crate::auth::AuthenticatedPrincipal;
use kinetib_db::DbContext;
use kinetib_db_local::LocalDbContext;
use thiserror::Error;

const MAX_PAGE: u32 = 10_000;
const MAX_PAGE_SIZE: u16 = 100;
const MAX_TRANSACTION_NAME_BYTES: usize = 128;
const MAX_TRANSACTION_DESCRIPTION_BYTES: usize = 512;
const MAX_TRANSACTION_CATEGORY_BYTES: usize = 64;
const MAX_JSON_ESCAPE_EXPANSION: usize = 6;
const MAX_JSON_ITEM_OVERHEAD_BYTES: usize = 512;
const MAX_JSON_PAGE_OVERHEAD_BYTES: usize = 256;
const MAX_TRANSACTION_RESPONSE_ESTIMATE_BYTES: usize = 60 * 1024;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum TransactionRecurringFrequency {
    Daily,
    Weekly,
    Monthly,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TransactionRecord {
    pub id: i64,
    pub name: String,
    pub price: f64,
    pub description: String,
    pub datetime_unix_seconds: i64,
    pub category: String,
    pub is_recurring: bool,
    pub recurring_frequency: Option<TransactionRecurringFrequency>,
    pub receipt_url: Option<String>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TransactionPage {
    pub page: u32,
    pub page_size: u16,
    pub has_more: bool,
    pub items: Vec<TransactionRecord>,
}

#[derive(Debug, Clone, PartialEq)]
pub struct NewTransaction {
    pub name: String,
    pub price: f64,
    pub description: String,
    pub datetime_unix_seconds: i64,
    pub category: String,
    pub is_recurring: bool,
    pub recurring_frequency: Option<TransactionRecurringFrequency>,
}

#[derive(Debug, Clone, PartialEq)]
pub enum FieldChange<T> {
    Unchanged,
    Set(T),
    Clear,
}

impl<T> Default for FieldChange<T> {
    fn default() -> Self {
        Self::Unchanged
    }
}

#[derive(Debug, Clone, PartialEq)]
pub struct TransactionUpdate {
    pub id: i64,
    pub name: FieldChange<String>,
    pub price: FieldChange<f64>,
    pub description: FieldChange<String>,
    pub datetime_unix_seconds: FieldChange<i64>,
    pub category: FieldChange<String>,
    pub is_recurring: FieldChange<bool>,
    pub recurring_frequency: FieldChange<TransactionRecurringFrequency>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TransactionDeleteResult {
    pub transaction_id: i64,
    pub deleted: bool,
}

#[derive(Debug, Clone, PartialEq)]
pub struct TransactionReceiptData {
    pub transaction_id: i64,
    pub name: String,
    pub price: f64,
    pub description: String,
    pub datetime_unix_seconds: i64,
    pub category: String,
}

#[derive(Debug, Error)]
pub enum TransactionError {
    #[error("invalid transaction input")]
    InvalidInput,
    #[error("invalid pagination")]
    InvalidPagination,
    #[error("transaction not found")]
    NotFound,
    /// SQLx details are intentionally not retained or exposed by this error.
    #[error("database query failed")]
    Database,
    #[error("stored transaction contains a non-finite price")]
    InvalidStoredPrice,
    #[error("stored transaction contains a field that exceeds its byte limit")]
    InvalidStoredFieldLength,
    #[error("stored transaction has inconsistent required fields")]
    InvalidStoredRecord,
    #[error("stored transaction contains an unsupported recurring frequency")]
    InvalidStoredRecurringFrequency,
    #[error("transaction page exceeds the bounded response budget; request a smaller page size")]
    ResponseTooLarge,
}

#[derive(Debug)]
struct RawTransaction {
    id: i64,
    name: Option<String>,
    name_within_limit: bool,
    price: f64,
    description: Option<String>,
    description_within_limit: bool,
    datetime_unix_seconds: i64,
    category: Option<String>,
    category_within_limit: bool,
    is_recurring: bool,
    recurring_frequency: Option<String>,
    receipt_url: Option<String>,
    receipt_url_within_limit: bool,
}

impl From<kinetib_db::transactions::TransactionRow> for RawTransaction {
    fn from(row: kinetib_db::transactions::TransactionRow) -> Self {
        Self {
            id: row.id,
            name: row.name,
            name_within_limit: row.name_within_limit,
            price: row.price,
            description: row.description,
            description_within_limit: row.description_within_limit,
            datetime_unix_seconds: row.datetime_unix_seconds,
            category: row.category,
            category_within_limit: row.category_within_limit,
            is_recurring: row.is_recurring,
            recurring_frequency: row.recurring_frequency,
            receipt_url: row.receipt_url,
            receipt_url_within_limit: row.receipt_url_within_limit,
        }
    }
}

impl From<kinetib_db_local::transactions::TransactionRow> for RawTransaction {
    fn from(row: kinetib_db_local::transactions::TransactionRow) -> Self {
        Self {
            id: row.id,
            name: row.name,
            name_within_limit: row.name_within_limit,
            price: row.price,
            description: row.description,
            description_within_limit: row.description_within_limit,
            datetime_unix_seconds: row.datetime_unix_seconds,
            category: row.category,
            category_within_limit: row.category_within_limit,
            is_recurring: row.is_recurring,
            recurring_frequency: row.recurring_frequency,
            receipt_url: row.receipt_url,
            receipt_url_within_limit: row.receipt_url_within_limit,
        }
    }
}

#[derive(Debug)]
struct RawReceipt {
    transaction_id: i64,
    name: Option<String>,
    name_within_limit: bool,
    price: f64,
    description: Option<String>,
    description_within_limit: bool,
    datetime_unix_seconds: i64,
    category: Option<String>,
    category_within_limit: bool,
}

impl From<kinetib_db::transactions::ReceiptRow> for RawReceipt {
    fn from(row: kinetib_db::transactions::ReceiptRow) -> Self {
        Self {
            transaction_id: row.transaction_id,
            name: row.name,
            name_within_limit: row.name_within_limit,
            price: row.price,
            description: row.description,
            description_within_limit: row.description_within_limit,
            datetime_unix_seconds: row.datetime_unix_seconds,
            category: row.category,
            category_within_limit: row.category_within_limit,
        }
    }
}

impl From<kinetib_db_local::transactions::ReceiptRow> for RawReceipt {
    fn from(row: kinetib_db_local::transactions::ReceiptRow) -> Self {
        Self {
            transaction_id: row.transaction_id,
            name: row.name,
            name_within_limit: row.name_within_limit,
            price: row.price,
            description: row.description,
            description_within_limit: row.description_within_limit,
            datetime_unix_seconds: row.datetime_unix_seconds,
            category: row.category,
            category_within_limit: row.category_within_limit,
        }
    }
}

/// Lists rows belonging exclusively to the authenticated CloudSync principal.
pub async fn list_transactions(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
    page: u32,
    page_size: u16,
) -> Result<TransactionPage, TransactionError> {
    let offset = pagination_offset(page, page_size)?;
    let rows = kinetib_db::transactions::list_transactions(
        &db.pool,
        principal.user_id(),
        i64::from(page_size) + 1,
        offset,
    )
    .await
    .map_err(|_| TransactionError::Database)?;
    build_page(page, page_size, rows.into_iter().map(RawTransaction::from))
}

/// Lists rows belonging exclusively to the authenticated LocalOnly principal.
pub async fn list_transactions_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
    page: u32,
    page_size: u16,
) -> Result<TransactionPage, TransactionError> {
    let offset = pagination_offset(page, page_size)?;
    let rows = kinetib_db_local::transactions::list_transactions(
        db,
        principal.user_id(),
        i64::from(page_size) + 1,
        offset,
    )
    .await
    .map_err(|_| TransactionError::Database)?;
    build_page(page, page_size, rows.into_iter().map(RawTransaction::from))
}

pub async fn create_transaction(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
    input: NewTransaction,
) -> Result<TransactionRecord, TransactionError> {
    validate_new_transaction(&input)?;
    let row = kinetib_db::transactions::create_transaction(
        &db.pool,
        principal.user_id(),
        kinetib_db::transactions::NewTransaction {
            name: input.name,
            price: input.price,
            description: input.description,
            datetime_unix_seconds: input.datetime_unix_seconds,
            category: input.category,
            is_recurring: input.is_recurring,
            recurring_frequency: input.recurring_frequency.map(to_pg_frequency),
        },
    )
    .await
    .map_err(|_| TransactionError::Database)?;
    map_record(row.into())
}

pub async fn create_transaction_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
    input: NewTransaction,
) -> Result<TransactionRecord, TransactionError> {
    validate_new_transaction(&input)?;
    let row = kinetib_db_local::transactions::create_transaction(
        db,
        principal.user_id(),
        kinetib_db_local::transactions::NewTransaction {
            name: input.name,
            price: input.price,
            description: input.description,
            datetime_unix_seconds: input.datetime_unix_seconds,
            category: input.category,
            is_recurring: input.is_recurring,
            recurring_frequency: input
                .recurring_frequency
                .map(frequency_as_str)
                .map(str::to_owned),
        },
    )
    .await
    .map_err(|_| TransactionError::Database)?;
    map_record(row.into())
}

pub async fn update_transaction(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
    input: TransactionUpdate,
) -> Result<TransactionRecord, TransactionError> {
    let input = normalize_and_validate_update(input)?;
    let changes = to_pg_changes(&input);
    let row = kinetib_db::transactions::update_transaction(
        &db.pool,
        principal.user_id(),
        input.id,
        changes,
    )
    .await
    .map_err(|_| TransactionError::Database)?
    .ok_or(TransactionError::NotFound)?;
    map_record(row.into())
}

pub async fn update_transaction_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
    input: TransactionUpdate,
) -> Result<TransactionRecord, TransactionError> {
    let input = normalize_and_validate_update(input)?;
    let changes = to_local_changes(&input);
    let row = kinetib_db_local::transactions::update_transaction(
        db,
        principal.user_id(),
        input.id,
        changes,
    )
    .await
    .map_err(|_| TransactionError::Database)?
    .ok_or(TransactionError::NotFound)?;
    map_record(row.into())
}

pub async fn delete_transaction(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
    transaction_id: i64,
) -> Result<TransactionDeleteResult, TransactionError> {
    if transaction_id <= 0 {
        return Err(TransactionError::InvalidInput);
    }
    // The nested Option distinguishes a missing row from a deleted row with no
    // receipt URL. The URL itself is deliberately never treated as a path here.
    let _former_receipt_url = kinetib_db::transactions::delete_transaction(
        &db.pool,
        principal.user_id(),
        transaction_id,
    )
    .await
    .map_err(|_| TransactionError::Database)?
    .ok_or(TransactionError::NotFound)?;
    Ok(TransactionDeleteResult {
        transaction_id,
        deleted: true,
    })
}

pub async fn delete_transaction_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
    transaction_id: i64,
) -> Result<TransactionDeleteResult, TransactionError> {
    if transaction_id <= 0 {
        return Err(TransactionError::InvalidInput);
    }
    let _former_receipt_url = kinetib_db_local::transactions::delete_transaction(
        db,
        principal.user_id(),
        transaction_id,
    )
    .await
    .map_err(|_| TransactionError::Database)?
    .ok_or(TransactionError::NotFound)?;
    Ok(TransactionDeleteResult {
        transaction_id,
        deleted: true,
    })
}

pub async fn get_transaction_receipt(
    db: &DbContext,
    principal: &AuthenticatedPrincipal,
    transaction_id: i64,
) -> Result<TransactionReceiptData, TransactionError> {
    if transaction_id <= 0 {
        return Err(TransactionError::InvalidInput);
    }
    let raw =
        kinetib_db::transactions::get_receipt_data(&db.pool, principal.user_id(), transaction_id)
            .await
            .map_err(|_| TransactionError::Database)?
            .map(RawReceipt::from)
            .ok_or(TransactionError::NotFound)?;
    map_receipt(raw)
}

pub async fn get_transaction_receipt_local(
    db: &LocalDbContext,
    principal: &AuthenticatedPrincipal,
    transaction_id: i64,
) -> Result<TransactionReceiptData, TransactionError> {
    if transaction_id <= 0 {
        return Err(TransactionError::InvalidInput);
    }
    let raw =
        kinetib_db_local::transactions::get_receipt_data(db, principal.user_id(), transaction_id)
            .await
            .map_err(|_| TransactionError::Database)?
            .map(RawReceipt::from)
            .ok_or(TransactionError::NotFound)?;
    map_receipt(raw)
}

fn pagination_offset(page: u32, page_size: u16) -> Result<i64, TransactionError> {
    if page == 0 || page > MAX_PAGE || page_size == 0 || page_size > MAX_PAGE_SIZE {
        return Err(TransactionError::InvalidPagination);
    }
    let offset = u64::from(page - 1)
        .checked_mul(u64::from(page_size))
        .ok_or(TransactionError::InvalidPagination)?;
    i64::try_from(offset).map_err(|_| TransactionError::InvalidPagination)
}

fn build_page<I>(page: u32, page_size: u16, rows: I) -> Result<TransactionPage, TransactionError>
where
    I: Iterator<Item = RawTransaction>,
{
    // Repositories fetch one extra row. Keep it out of the response and use it
    // only to indicate whether another page is available.
    let mut rows = rows.peekable();
    let mut items = Vec::with_capacity(usize::from(page_size));
    let mut raw_string_bytes = 0_usize;
    while items.len() < usize::from(page_size) {
        let Some(row) = rows.next() else {
            break;
        };
        let record = map_record(row)?;
        let next_raw_string_bytes = add_string_bytes(
            raw_string_bytes,
            &record.name,
            &record.description,
            &record.category,
            record.receipt_url.as_deref(),
        )?;
        if !response_estimate_fits(items.len() + 1, next_raw_string_bytes) {
            return Err(TransactionError::ResponseTooLarge);
        }
        raw_string_bytes = next_raw_string_bytes;
        items.push(record);
    }
    let has_more = page < MAX_PAGE && rows.peek().is_some();
    Ok(TransactionPage {
        page,
        page_size,
        has_more,
        items,
    })
}

fn map_record(row: RawTransaction) -> Result<TransactionRecord, TransactionError> {
    if !row.name_within_limit
        || !row.description_within_limit
        || !row.category_within_limit
        || !row.receipt_url_within_limit
    {
        return Err(TransactionError::InvalidStoredFieldLength);
    }
    if !row.price.is_finite() {
        return Err(TransactionError::InvalidStoredPrice);
    }
    let name = row.name.ok_or(TransactionError::InvalidStoredRecord)?;
    let description = row
        .description
        .ok_or(TransactionError::InvalidStoredRecord)?;
    let category = row.category.ok_or(TransactionError::InvalidStoredRecord)?;
    let recurring_frequency = parse_recurring_frequency(row.recurring_frequency)?;
    if row.is_recurring != recurring_frequency.is_some() {
        return Err(TransactionError::InvalidStoredRecord);
    }
    Ok(TransactionRecord {
        id: row.id,
        name,
        price: row.price,
        description,
        datetime_unix_seconds: row.datetime_unix_seconds,
        category,
        is_recurring: row.is_recurring,
        recurring_frequency,
        receipt_url: row.receipt_url,
    })
}

fn map_receipt(row: RawReceipt) -> Result<TransactionReceiptData, TransactionError> {
    if !row.name_within_limit || !row.description_within_limit || !row.category_within_limit {
        return Err(TransactionError::InvalidStoredFieldLength);
    }
    if !row.price.is_finite() {
        return Err(TransactionError::InvalidStoredPrice);
    }
    Ok(TransactionReceiptData {
        transaction_id: row.transaction_id,
        name: row.name.ok_or(TransactionError::InvalidStoredRecord)?,
        price: row.price,
        description: row
            .description
            .ok_or(TransactionError::InvalidStoredRecord)?,
        datetime_unix_seconds: row.datetime_unix_seconds,
        category: row.category.ok_or(TransactionError::InvalidStoredRecord)?,
    })
}

fn validate_new_transaction(input: &NewTransaction) -> Result<(), TransactionError> {
    if input.name.trim().is_empty()
        || input.name.len() > MAX_TRANSACTION_NAME_BYTES
        || input.description.len() > MAX_TRANSACTION_DESCRIPTION_BYTES
        || input.category.trim().is_empty()
        || input.category.len() > MAX_TRANSACTION_CATEGORY_BYTES
        || !input.price.is_finite()
        || input.is_recurring != input.recurring_frequency.is_some()
    {
        return Err(TransactionError::InvalidInput);
    }
    if let Some(frequency) = input.recurring_frequency {
        validate_schedule_anchor(input.datetime_unix_seconds, frequency)?;
    }
    Ok(())
}

/// Validate with the exact calendar engine used by the recurring worker. This
/// rejects timestamps that fit in i64 but cannot produce the next occurrence,
/// preventing a bad template from repeatedly aborting a scheduler batch.
fn validate_schedule_anchor(
    anchor_unix_seconds: i64,
    frequency: TransactionRecurringFrequency,
) -> Result<(), TransactionError> {
    let frequency = match frequency {
        TransactionRecurringFrequency::Daily => kinetib_recurrence::Frequency::Daily,
        TransactionRecurringFrequency::Weekly => kinetib_recurrence::Frequency::Weekly,
        TransactionRecurringFrequency::Monthly => kinetib_recurrence::Frequency::Monthly,
    };
    kinetib_recurrence::next_occurrence_at(anchor_unix_seconds, anchor_unix_seconds, frequency)
        .map(|_| ())
        .map_err(|_| TransactionError::InvalidInput)
}

/// A changed timestamp may belong to an already-recurring row even when the
/// request leaves its recurrence fields unchanged. Require a timestamp that is
/// valid for every supported cadence in that case.
fn validate_timestamp_for_any_schedule(anchor_unix_seconds: i64) -> Result<(), TransactionError> {
    for frequency in [
        TransactionRecurringFrequency::Daily,
        TransactionRecurringFrequency::Weekly,
        TransactionRecurringFrequency::Monthly,
    ] {
        validate_schedule_anchor(anchor_unix_seconds, frequency)?;
    }
    Ok(())
}

fn normalize_and_validate_update(
    mut input: TransactionUpdate,
) -> Result<TransactionUpdate, TransactionError> {
    use FieldChange::{Clear, Set, Unchanged};

    if input.id <= 0 {
        return Err(TransactionError::InvalidInput);
    }
    if matches!(&input.is_recurring, Set(false)) && matches!(&input.recurring_frequency, Unchanged)
    {
        input.recurring_frequency = Clear;
    }
    let any_change = !matches!(&input.name, Unchanged)
        || !matches!(&input.price, Unchanged)
        || !matches!(&input.description, Unchanged)
        || !matches!(&input.datetime_unix_seconds, Unchanged)
        || !matches!(&input.category, Unchanged)
        || !matches!(&input.is_recurring, Unchanged)
        || !matches!(&input.recurring_frequency, Unchanged);
    if !any_change {
        return Err(TransactionError::InvalidInput);
    }

    match &input.name {
        Set(value) if !value.trim().is_empty() && value.len() <= MAX_TRANSACTION_NAME_BYTES => {}
        Unchanged => {}
        _ => return Err(TransactionError::InvalidInput),
    }
    match &input.price {
        Set(value) if value.is_finite() => {}
        Unchanged => {}
        _ => return Err(TransactionError::InvalidInput),
    }
    match &input.datetime_unix_seconds {
        Set(value) => validate_timestamp_for_any_schedule(*value)?,
        Unchanged => {}
        Clear => return Err(TransactionError::InvalidInput),
    }
    match &input.description {
        Set(value) if value.len() <= MAX_TRANSACTION_DESCRIPTION_BYTES => {}
        Unchanged => {}
        _ => return Err(TransactionError::InvalidInput),
    }
    match &input.category {
        Set(value) if !value.trim().is_empty() && value.len() <= MAX_TRANSACTION_CATEGORY_BYTES => {
        }
        Unchanged => {}
        _ => return Err(TransactionError::InvalidInput),
    }
    match (&input.is_recurring, &input.recurring_frequency) {
        (Set(true), Set(frequency)) => {
            let anchor = match &input.datetime_unix_seconds {
                Set(value) => *value,
                // A client enabling recurrence must state the anchor explicitly.
                Unchanged | Clear => return Err(TransactionError::InvalidInput),
            };
            validate_schedule_anchor(anchor, *frequency)?;
        }
        (Set(false), Clear) | (Unchanged, Unchanged) => {}
        (Clear, _) => return Err(TransactionError::InvalidInput),
        (Unchanged, Set(_))
        | (Unchanged, Clear)
        | (Set(true), Unchanged)
        | (Set(true), Clear)
        | (Set(false), Unchanged)
        | (Set(false), Set(_)) => {
            return Err(TransactionError::InvalidInput);
        }
    }
    Ok(input)
}

fn to_pg_changes(input: &TransactionUpdate) -> kinetib_db::transactions::TransactionChanges {
    use FieldChange::{Clear, Set, Unchanged};

    let (recurring_frequency_changed, recurring_frequency) = match &input.recurring_frequency {
        Unchanged => (false, None),
        Clear => (true, None),
        Set(value) => (true, Some(to_pg_frequency(*value))),
    };

    kinetib_db::transactions::TransactionChanges {
        name_changed: !matches!(&input.name, Unchanged),
        name: match &input.name {
            Set(value) => Some(value.clone()),
            _ => None,
        },
        price_changed: !matches!(&input.price, Unchanged),
        price: match &input.price {
            Set(value) => Some(*value),
            _ => None,
        },
        description_changed: !matches!(&input.description, Unchanged),
        description: match &input.description {
            Set(value) => Some(value.clone()),
            _ => None,
        },
        datetime_changed: !matches!(&input.datetime_unix_seconds, Unchanged),
        datetime_unix_seconds: match &input.datetime_unix_seconds {
            Set(value) => Some(*value),
            _ => None,
        },
        category_changed: !matches!(&input.category, Unchanged),
        category: match &input.category {
            Set(value) => Some(value.clone()),
            _ => None,
        },
        is_recurring_changed: !matches!(&input.is_recurring, Unchanged),
        is_recurring: match &input.is_recurring {
            Set(value) => Some(*value),
            _ => None,
        },
        recurring_frequency_changed,
        recurring_frequency,
    }
}

fn to_local_changes(
    input: &TransactionUpdate,
) -> kinetib_db_local::transactions::TransactionChanges {
    use FieldChange::{Clear, Set, Unchanged};

    let (recurring_frequency_changed, recurring_frequency) = match &input.recurring_frequency {
        Unchanged => (false, None),
        Clear => (true, None),
        Set(value) => (true, Some(frequency_as_str(*value).to_owned())),
    };

    kinetib_db_local::transactions::TransactionChanges {
        name_changed: !matches!(&input.name, Unchanged),
        name: match &input.name {
            Set(value) => Some(value.clone()),
            _ => None,
        },
        price_changed: !matches!(&input.price, Unchanged),
        price: match &input.price {
            Set(value) => Some(*value),
            _ => None,
        },
        description_changed: !matches!(&input.description, Unchanged),
        description: match &input.description {
            Set(value) => Some(value.clone()),
            _ => None,
        },
        datetime_changed: !matches!(&input.datetime_unix_seconds, Unchanged),
        datetime_unix_seconds: match &input.datetime_unix_seconds {
            Set(value) => Some(*value),
            _ => None,
        },
        category_changed: !matches!(&input.category, Unchanged),
        category: match &input.category {
            Set(value) => Some(value.clone()),
            _ => None,
        },
        is_recurring_changed: !matches!(&input.is_recurring, Unchanged),
        is_recurring: match &input.is_recurring {
            Set(value) => Some(*value),
            _ => None,
        },
        recurring_frequency_changed,
        recurring_frequency,
    }
}

fn to_pg_frequency(
    value: TransactionRecurringFrequency,
) -> kinetib_db::models::RecurringFrequency {
    match value {
        TransactionRecurringFrequency::Daily => kinetib_db::models::RecurringFrequency::Daily,
        TransactionRecurringFrequency::Weekly => kinetib_db::models::RecurringFrequency::Weekly,
        TransactionRecurringFrequency::Monthly => kinetib_db::models::RecurringFrequency::Monthly,
    }
}

fn frequency_as_str(value: TransactionRecurringFrequency) -> &'static str {
    match value {
        TransactionRecurringFrequency::Daily => "daily",
        TransactionRecurringFrequency::Weekly => "weekly",
        TransactionRecurringFrequency::Monthly => "monthly",
    }
}

fn parse_recurring_frequency(
    value: Option<String>,
) -> Result<Option<TransactionRecurringFrequency>, TransactionError> {
    match value.as_deref() {
        None => Ok(None),
        Some(value) if value.eq_ignore_ascii_case("daily") => {
            Ok(Some(TransactionRecurringFrequency::Daily))
        }
        Some(value) if value.eq_ignore_ascii_case("weekly") => {
            Ok(Some(TransactionRecurringFrequency::Weekly))
        }
        Some(value) if value.eq_ignore_ascii_case("monthly") => {
            Ok(Some(TransactionRecurringFrequency::Monthly))
        }
        Some(_) => Err(TransactionError::InvalidStoredRecurringFrequency),
    }
}

fn response_estimate_fits(item_count: usize, raw_string_bytes: usize) -> bool {
    let Some(item_overhead) = item_count.checked_mul(MAX_JSON_ITEM_OVERHEAD_BYTES) else {
        return false;
    };
    let Some(escaped_strings) = raw_string_bytes.checked_mul(MAX_JSON_ESCAPE_EXPANSION) else {
        return false;
    };
    item_overhead
        .checked_add(escaped_strings)
        .and_then(|value| value.checked_add(MAX_JSON_PAGE_OVERHEAD_BYTES))
        .is_some_and(|estimate| estimate <= MAX_TRANSACTION_RESPONSE_ESTIMATE_BYTES)
}

fn add_string_bytes(
    current: usize,
    name: &str,
    description: &str,
    category: &str,
    receipt_url: Option<&str>,
) -> Result<usize, TransactionError> {
    current
        .checked_add(name.len())
        .and_then(|value| value.checked_add(description.len()))
        .and_then(|value| value.checked_add(category.len()))
        .and_then(|value| value.checked_add(receipt_url.map_or(0, str::len)))
        .ok_or(TransactionError::ResponseTooLarge)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn raw_transaction(id: i64) -> RawTransaction {
        RawTransaction {
            id,
            name: Some(format!("Transaction {id}")),
            name_within_limit: true,
            price: 12.5,
            description: Some(String::new()),
            description_within_limit: true,
            datetime_unix_seconds: 1_800_000_000,
            category: Some("general".into()),
            category_within_limit: true,
            is_recurring: false,
            recurring_frequency: None,
            receipt_url: None,
            receipt_url_within_limit: true,
        }
    }

    #[test]
    fn pagination_is_one_based_bounded_and_overflow_checked() {
        assert_eq!(pagination_offset(1, 10).unwrap(), 0);
        assert_eq!(pagination_offset(2, 10).unwrap(), 10);
        assert!(pagination_offset(0, 10).is_err());
        assert!(pagination_offset(MAX_PAGE + 1, 10).is_err());
        assert!(pagination_offset(1, 0).is_err());
        assert!(pagination_offset(1, MAX_PAGE_SIZE + 1).is_err());
    }

    #[test]
    fn invalid_amount_and_missing_recurrence_are_rejected() {
        let mut value = NewTransaction {
            name: "Coffee".into(),
            price: f64::NAN,
            description: String::new(),
            datetime_unix_seconds: 1_800_000_000,
            category: "food".into(),
            is_recurring: false,
            recurring_frequency: None,
        };
        assert!(matches!(
            validate_new_transaction(&value),
            Err(TransactionError::InvalidInput)
        ));

        value.price = 4.5;
        value.is_recurring = true;
        assert!(matches!(
            validate_new_transaction(&value),
            Err(TransactionError::InvalidInput)
        ));
    }

    #[test]
    fn recurring_create_rejects_timestamps_the_scheduler_cannot_advance() {
        let input = NewTransaction {
            name: "Extreme date schedule".into(),
            price: 5.0,
            description: String::new(),
            datetime_unix_seconds: i64::MAX,
            category: "general".into(),
            is_recurring: true,
            recurring_frequency: Some(TransactionRecurringFrequency::Daily),
        };
        assert!(matches!(
            validate_new_transaction(&input),
            Err(TransactionError::InvalidInput)
        ));
    }

    #[test]
    fn enabling_recurrence_requires_and_validates_the_anchor_timestamp() {
        let mut input = TransactionUpdate {
            id: 3,
            name: FieldChange::Unchanged,
            price: FieldChange::Unchanged,
            description: FieldChange::Unchanged,
            datetime_unix_seconds: FieldChange::Unchanged,
            category: FieldChange::Unchanged,
            is_recurring: FieldChange::Set(true),
            recurring_frequency: FieldChange::Set(TransactionRecurringFrequency::Weekly),
        };
        assert!(matches!(
            normalize_and_validate_update(input.clone()),
            Err(TransactionError::InvalidInput)
        ));

        input.datetime_unix_seconds = FieldChange::Set(i64::MAX);
        assert!(matches!(
            normalize_and_validate_update(input),
            Err(TransactionError::InvalidInput)
        ));
    }

    #[test]
    fn update_turning_recurrence_off_clears_the_old_frequency() {
        let input = TransactionUpdate {
            id: 3,
            name: FieldChange::Unchanged,
            price: FieldChange::Set(5.0),
            description: FieldChange::Unchanged,
            datetime_unix_seconds: FieldChange::Unchanged,
            category: FieldChange::Unchanged,
            is_recurring: FieldChange::Set(false),
            recurring_frequency: FieldChange::Unchanged,
        };
        let normalized = normalize_and_validate_update(input).unwrap();
        assert!(matches!(normalized.recurring_frequency, FieldChange::Clear));
    }

    #[test]
    fn update_rejects_clear_for_non_nullable_fields() {
        let input = TransactionUpdate {
            id: 3,
            name: FieldChange::Clear,
            price: FieldChange::Unchanged,
            description: FieldChange::Unchanged,
            datetime_unix_seconds: FieldChange::Unchanged,
            category: FieldChange::Unchanged,
            is_recurring: FieldChange::Unchanged,
            recurring_frequency: FieldChange::Unchanged,
        };
        assert!(matches!(
            normalize_and_validate_update(input),
            Err(TransactionError::InvalidInput)
        ));
    }

    #[test]
    fn page_uses_extra_row_for_has_more_without_returning_it() {
        let page = build_page(
            1,
            1,
            vec![raw_transaction(2), raw_transaction(1)].into_iter(),
        )
        .unwrap();
        assert_eq!(page.items.len(), 1);
        assert_eq!(page.items[0].id, 2);
        assert!(page.has_more);

        let last_page = build_page(2, 1, vec![raw_transaction(1)].into_iter()).unwrap();
        assert_eq!(last_page.items.len(), 1);
        assert!(!last_page.has_more);
    }

    #[test]
    fn response_budget_fails_closed_on_overflow_or_large_strings() {
        assert!(response_estimate_fits(1, 10));
        assert!(!response_estimate_fits(usize::MAX, usize::MAX));
    }
}
