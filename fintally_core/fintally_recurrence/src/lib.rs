//! Deterministic recurrence calculations shared by storage repositories.
//!
//! All persisted schedule values are Unix seconds and recurrence is evaluated
//! in UTC. The module has no database, clock, runtime, or UI dependency.

use thiserror::Error;
use time::{Date, Duration, Month, OffsetDateTime, PrimitiveDateTime};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Frequency {
    Daily,
    Weekly,
    Monthly,
}

#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct BatchStats {
    pub templates_scanned: u32,
    pub occurrences_created: u32,
    pub schedules_completed: u32,
    pub last_template_id: Option<i64>,
}

impl BatchStats {
    pub fn merge(&mut self, other: Self) {
        self.templates_scanned = self
            .templates_scanned
            .saturating_add(other.templates_scanned);
        self.occurrences_created = self
            .occurrences_created
            .saturating_add(other.occurrences_created);
        self.schedules_completed = self
            .schedules_completed
            .saturating_add(other.schedules_completed);
        if other.last_template_id.is_some() {
            self.last_template_id = other.last_template_id;
        }
    }
}

#[derive(Debug, Error, Clone, Copy, PartialEq, Eq)]
pub enum ScheduleError {
    #[error("recurrence timestamp is outside the supported calendar range")]
    TimestampOutOfRange,
    #[error("recurrence month calculation overflowed")]
    MonthOverflow,
}

/// Returns the first scheduled occurrence strictly after `after_unix_seconds`.
///
/// For monthly schedules the original anchor day is preserved. If that day does
/// not exist in a target month, it is clamped to that month's final day. This
/// avoids Jan-31 -> Feb-28 -> Mar-28 drift; Jan-31 -> Feb-28 -> Mar-31 instead.
pub fn next_occurrence_at(
    anchor_unix_seconds: i64,
    after_unix_seconds: i64,
    frequency: Frequency,
) -> Result<i64, ScheduleError> {
    let anchor = OffsetDateTime::from_unix_timestamp(anchor_unix_seconds)
        .map_err(|_| ScheduleError::TimestampOutOfRange)?;
    let after = OffsetDateTime::from_unix_timestamp(after_unix_seconds)
        .map_err(|_| ScheduleError::TimestampOutOfRange)?;
    let cursor = if after < anchor { anchor } else { after };

    match frequency {
        Frequency::Daily => cursor
            .checked_add(Duration::days(1))
            .map(OffsetDateTime::unix_timestamp)
            .ok_or(ScheduleError::TimestampOutOfRange),
        Frequency::Weekly => cursor
            .checked_add(Duration::weeks(1))
            .map(OffsetDateTime::unix_timestamp)
            .ok_or(ScheduleError::TimestampOutOfRange),
        Frequency::Monthly => {
            let anchor_month = month_index(anchor.year(), anchor.month());
            let cursor_month = month_index(cursor.year(), cursor.month());
            let elapsed_months = cursor_month
                .checked_sub(anchor_month)
                .ok_or(ScheduleError::MonthOverflow)?
                .max(0);
            let mut offset = elapsed_months;
            let mut candidate = add_months(anchor, offset)?;
            if candidate <= cursor {
                offset = offset.checked_add(1).ok_or(ScheduleError::MonthOverflow)?;
                candidate = add_months(anchor, offset)?;
            }
            Ok(candidate.unix_timestamp())
        }
    }
}

/// Adds calendar months to an anchor using month-end clamping.
///
/// This is used to convert the legacy `tenure_months` value to an actual end
/// timestamp. A non-positive duration is intentionally rejected by callers.
pub fn add_months_from_anchor(anchor_unix_seconds: i64, months: i64) -> Result<i64, ScheduleError> {
    let anchor = OffsetDateTime::from_unix_timestamp(anchor_unix_seconds)
        .map_err(|_| ScheduleError::TimestampOutOfRange)?;
    let shifted = add_months(anchor, months)?;
    Ok(shifted.unix_timestamp())
}

fn month_index(year: i32, month: Month) -> i64 {
    i64::from(year) * 12 + i64::from(month as u8) - 1
}

fn add_months(anchor: OffsetDateTime, months: i64) -> Result<OffsetDateTime, ScheduleError> {
    let target_index = month_index(anchor.year(), anchor.month())
        .checked_add(months)
        .ok_or(ScheduleError::MonthOverflow)?;
    let target_year = i32::try_from(target_index.div_euclid(12))
        .map_err(|_| ScheduleError::TimestampOutOfRange)?;
    let target_month_number =
        u8::try_from(target_index.rem_euclid(12) + 1).map_err(|_| ScheduleError::MonthOverflow)?;
    let target_month =
        Month::try_from(target_month_number).map_err(|_| ScheduleError::MonthOverflow)?;

    let mut day = anchor.day();
    let target_date = loop {
        if let Ok(date) = Date::from_calendar_date(target_year, target_month, day) {
            break date;
        }
        day = day
            .checked_sub(1)
            .ok_or(ScheduleError::TimestampOutOfRange)?;
    };

    Ok(PrimitiveDateTime::new(target_date, anchor.time()).assume_utc())
}

#[cfg(test)]
mod tests {
    use super::{add_months_from_anchor, next_occurrence_at, Frequency};
    use time::{Date, Month, PrimitiveDateTime, Time};

    fn unix(year: i32, month: Month, day: u8, hour: u8) -> i64 {
        PrimitiveDateTime::new(
            Date::from_calendar_date(year, month, day).unwrap(),
            Time::from_hms(hour, 0, 0).unwrap(),
        )
        .assume_utc()
        .unix_timestamp()
    }

    #[test]
    fn daily_and_weekly_use_fixed_utc_intervals() {
        let anchor = unix(2026, Month::January, 1, 12);
        assert_eq!(
            next_occurrence_at(anchor, anchor, Frequency::Daily).unwrap(),
            unix(2026, Month::January, 2, 12)
        );
        assert_eq!(
            next_occurrence_at(anchor, anchor, Frequency::Weekly).unwrap(),
            unix(2026, Month::January, 8, 12)
        );
    }

    #[test]
    fn monthly_schedule_preserves_anchor_day_across_short_months() {
        let anchor = unix(2024, Month::January, 31, 9);
        let february = next_occurrence_at(anchor, anchor, Frequency::Monthly).unwrap();
        assert_eq!(february, unix(2024, Month::February, 29, 9));
        assert_eq!(
            next_occurrence_at(anchor, february, Frequency::Monthly).unwrap(),
            unix(2024, Month::March, 31, 9)
        );
    }

    #[test]
    fn monthly_schedule_does_not_skip_anchor_day_when_cursor_is_earlier_in_month() {
        let anchor = unix(2024, Month::January, 31, 9);
        let cursor = unix(2024, Month::March, 30, 9);
        assert_eq!(
            next_occurrence_at(anchor, cursor, Frequency::Monthly).unwrap(),
            unix(2024, Month::March, 31, 9)
        );
    }

    #[test]
    fn monthly_schedule_handles_leap_day_anchor() {
        let anchor = unix(2024, Month::February, 29, 8);
        let next = next_occurrence_at(anchor, anchor, Frequency::Monthly).unwrap();
        assert_eq!(next, unix(2024, Month::March, 29, 8));
    }

    #[test]
    fn legacy_tenure_converts_to_calendar_month_end() {
        let anchor = unix(2024, Month::January, 31, 9);
        assert_eq!(
            add_months_from_anchor(anchor, 1).unwrap(),
            unix(2024, Month::February, 29, 9)
        );
        assert_eq!(
            add_months_from_anchor(anchor, 12).unwrap(),
            unix(2025, Month::January, 31, 9)
        );
    }
}
