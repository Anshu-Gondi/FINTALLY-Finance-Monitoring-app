//! Application-level entry points for recurring generation.
//!
//! This layer coordinates storage-scoped repository operations. It deliberately
//! does not own a clock, scheduler, database pool, or platform runtime.

use kinetib_db::DbContext;
use kinetib_db_local::LocalDbContext;
pub use kinetib_recurrence::BatchStats;

/// Generate one bounded batch for an authenticated CloudSync store.
pub async fn process_cloud_batch(
    db: &DbContext,
    now_unix_seconds: i64,
    template_limit: i64,
    catch_up_per_template: u8,
    after_template_id: i64,
) -> Result<BatchStats, kinetib_db::transactions::recurrence::RecurrenceRepositoryError> {
    kinetib_db::transactions::recurrence::process_due_batch(
        &db.pool,
        now_unix_seconds,
        template_limit,
        catch_up_per_template,
        after_template_id,
    )
    .await
}

/// Generate one bounded batch for the device-local LocalOnly store.
pub async fn process_local_batch(
    db: &LocalDbContext,
    now_unix_seconds: i64,
    template_limit: i64,
    catch_up_per_template: u8,
    after_template_id: i64,
) -> Result<BatchStats, kinetib_db_local::transactions::recurrence::RecurrenceRepositoryError> {
    kinetib_db_local::transactions::recurrence::process_due_batch(
        db,
        now_unix_seconds,
        template_limit,
        catch_up_per_template,
        after_template_id,
    )
    .await
}
