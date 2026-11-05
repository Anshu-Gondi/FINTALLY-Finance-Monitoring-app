//! Storage-independent budget and notification domain contracts.
//!
//! This module owns validation and deterministic threshold-crossing semantics.
//! Database repositories, the kernel scheduler, and platform delivery adapters
//! must remain separate. A notification event never mutates its budget.

use thiserror::Error;

pub const MAX_BUDGET_CATEGORY_BYTES: usize = 64;
pub const MAX_TIMEZONE_NAME_BYTES: usize = 64;
pub const MAX_IDEMPOTENCY_KEY_BYTES: usize = 192;
pub const DEFAULT_THRESHOLD_PERCENTAGES: [u16; 2] = [80, 100];

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NewBudget {
    pub category: String,
    pub amount_minor_units: i64,
    pub period_start_unix: i64,
    pub period_end_unix: Option<i64>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BudgetRecord {
    pub id: i64,
    pub category: String,
    pub amount_minor_units: i64,
    pub period_start_unix: i64,
    pub period_end_unix: Option<i64>,
    pub is_active: bool,
    pub revision: i64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntervalUnit {
    Minutes,
    Hours,
    Days,
    Weeks,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub enum CustomRecurrenceRule {
    /// Repeat every N units. Day/week calculation must preserve the local
    /// wall-clock time in the selected timezone across daylight-saving changes.
    Interval { every: u32, unit: IntervalUnit },
    /// Monday is bit 0 and Sunday is bit 6. At least one bit must be set.
    Weekly { weekday_mask: u8 },
    /// Months without the requested day are skipped, not clamped to month-end.
    Monthly { day_of_month: u8 },
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct NewReminderSchedule {
    pub rule: CustomRecurrenceRule,
    pub local_time_minute: u16,
    pub timezone_name: String,
    pub starts_at_unix: i64,
    pub ends_at_unix: Option<i64>,
    pub first_run_at_unix: i64,
    pub enabled: bool,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct ThresholdRule {
    pub threshold_percent: u16,
    pub enabled: bool,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum BudgetNotificationType {
    ScheduledReminder,
    ThresholdCrossed,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ThresholdState {
    pub is_above: bool,
    pub crossing_generation: u64,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct ThresholdTransition {
    pub state: ThresholdState,
    pub crossed_upward: bool,
    pub crossed_downward: bool,
}

#[derive(Debug, Error, Clone, PartialEq, Eq)]
pub enum BudgetValidationError {
    #[error("budget category must contain 1 to 64 UTF-8 bytes")]
    InvalidCategory,
    #[error("budget amount must be a positive number of minor currency units")]
    InvalidAmount,
    #[error("budget period end cannot precede its start")]
    InvalidPeriod,
    #[error("reminder timezone must contain 1 to 64 bytes and no control characters")]
    InvalidTimezone,
    #[error("reminder local time must be between 00:00 and 23:59")]
    InvalidLocalTime,
    #[error("custom recurrence interval must be between 1 and 100000")]
    InvalidInterval,
    #[error("weekly recurrence must select at least one valid weekday")]
    InvalidWeekdayMask,
    #[error("monthly recurrence day must be between 1 and 31")]
    InvalidMonthDay,
    #[error("reminder end cannot precede its start")]
    InvalidReminderPeriod,
    #[error("threshold percentage must be between 1 and 1000")]
    InvalidThreshold,
    #[error("threshold crossing generation overflow")]
    GenerationOverflow,
}

impl NewBudget {
    /// Returns the category value repositories should persist. Trimming happens
    /// before overlap checks so whitespace cannot create parallel categories.
    pub fn normalized_category(&self) -> Result<String, BudgetValidationError> {
        let category = self.category.trim();
        if category.is_empty()
            || category.len() > MAX_BUDGET_CATEGORY_BYTES
            || category.chars().any(char::is_control)
        {
            return Err(BudgetValidationError::InvalidCategory);
        }
        Ok(category.to_owned())
    }

    pub fn validate(&self) -> Result<(), BudgetValidationError> {
        self.normalized_category()?;
        if self.amount_minor_units <= 0 {
            return Err(BudgetValidationError::InvalidAmount);
        }
        if self
            .period_end_unix
            .is_some_and(|end| end < self.period_start_unix)
        {
            return Err(BudgetValidationError::InvalidPeriod);
        }
        Ok(())
    }
}

impl NewReminderSchedule {
    pub fn validate(&self) -> Result<(), BudgetValidationError> {
        if self.timezone_name.is_empty()
            || self.timezone_name.len() > MAX_TIMEZONE_NAME_BYTES
            || self.timezone_name.chars().any(char::is_control)
        {
            return Err(BudgetValidationError::InvalidTimezone);
        }
        if self.local_time_minute > 1439 {
            return Err(BudgetValidationError::InvalidLocalTime);
        }
        if self
            .ends_at_unix
            .is_some_and(|end| end < self.starts_at_unix)
            || self.first_run_at_unix < self.starts_at_unix
            || self
                .ends_at_unix
                .is_some_and(|end| self.first_run_at_unix > end)
        {
            return Err(BudgetValidationError::InvalidReminderPeriod);
        }
        match &self.rule {
            CustomRecurrenceRule::Interval { every, .. } if !(1..=100_000).contains(every) => {
                Err(BudgetValidationError::InvalidInterval)
            }
            CustomRecurrenceRule::Weekly { weekday_mask }
                if !(1..=0b0111_1111).contains(weekday_mask) =>
            {
                Err(BudgetValidationError::InvalidWeekdayMask)
            }
            CustomRecurrenceRule::Monthly { day_of_month } if !(1..=31).contains(day_of_month) => {
                Err(BudgetValidationError::InvalidMonthDay)
            }
            _ => Ok(()),
        }
    }
}

impl ThresholdRule {
    pub fn validate(&self) -> Result<(), BudgetValidationError> {
        if !(1..=1000).contains(&self.threshold_percent) {
            return Err(BudgetValidationError::InvalidThreshold);
        }
        Ok(())
    }
}

/// Initial per-budget defaults. Repositories must insert these in the same
/// database transaction as the allocation so a partially configured budget
/// cannot become visible to another request.
pub fn default_threshold_rules() -> [ThresholdRule; 2] {
    [
        ThresholdRule {
            threshold_percent: DEFAULT_THRESHOLD_PERCENTAGES[0],
            enabled: true,
        },
        ThresholdRule {
            threshold_percent: DEFAULT_THRESHOLD_PERCENTAGES[1],
            enabled: true,
        },
    ]
}

/// Converts a legacy major-unit float into integer minor units at the import
/// boundary only. New budget storage and IPC contracts should use integer
/// minor units. This is not an accounting precision model for arbitrary FX.
pub fn major_to_minor_units(amount_major: f64) -> Result<i64, BudgetValidationError> {
    if !amount_major.is_finite() || amount_major <= 0.0 {
        return Err(BudgetValidationError::InvalidAmount);
    }
    let scaled = amount_major * 100.0;
    // i64::MAX rounds up to 2^63 when represented as f64, so use the exclusive
    // 2^63 boundary rather than `i64::MAX as f64` in this conversion guard.
    const I64_EXCLUSIVE_UPPER_BOUND: f64 = 9_223_372_036_854_775_808.0;
    if !scaled.is_finite() || scaled >= I64_EXCLUSIVE_UPPER_BOUND {
        return Err(BudgetValidationError::InvalidAmount);
    }
    let rounded = scaled.round();
    if rounded < 1.0 || rounded >= I64_EXCLUSIVE_UPPER_BOUND {
        return Err(BudgetValidationError::InvalidAmount);
    }
    Ok(rounded as i64)
}

/// Computes a threshold transition without floating-point arithmetic.
/// `spent_minor_units` is the positive aggregate of eligible expense amounts.
pub fn evaluate_threshold(
    budget_minor_units: i64,
    spent_minor_units: i64,
    threshold_percent: u16,
    previous: ThresholdState,
) -> Result<ThresholdTransition, BudgetValidationError> {
    if budget_minor_units <= 0 || spent_minor_units < 0 {
        return Err(BudgetValidationError::InvalidAmount);
    }
    if !(1..=1000).contains(&threshold_percent) {
        return Err(BudgetValidationError::InvalidThreshold);
    }

    let is_above = i128::from(spent_minor_units) * 100
        >= i128::from(budget_minor_units) * i128::from(threshold_percent);
    let crossed_upward = !previous.is_above && is_above;
    let crossed_downward = previous.is_above && !is_above;
    let crossing_generation = if crossed_upward {
        previous
            .crossing_generation
            .checked_add(1)
            .ok_or(BudgetValidationError::GenerationOverflow)?
    } else {
        previous.crossing_generation
    };

    Ok(ThresholdTransition {
        state: ThresholdState {
            is_above,
            crossing_generation,
        },
        crossed_upward,
        crossed_downward,
    })
}

pub fn scheduled_reminder_idempotency_key(schedule_id: i64, occurrence_unix: i64) -> String {
    format!("budget-reminder:{schedule_id}:{occurrence_unix}")
}

pub fn threshold_idempotency_key(
    budget_id: i64,
    period_start_unix: i64,
    threshold_percent: u16,
    crossing_generation: u64,
) -> String {
    format!(
        "budget-threshold:{budget_id}:{period_start_unix}:{threshold_percent}:{crossing_generation}"
    )
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn amount_import_rejects_non_finite_and_rounds_to_minor_units() {
        assert_eq!(major_to_minor_units(12.345).unwrap(), 1235);
        assert!(major_to_minor_units(f64::NAN).is_err());
        assert!(major_to_minor_units(f64::INFINITY).is_err());
        assert!(major_to_minor_units(0.0).is_err());
    }

    #[test]
    fn new_budgets_start_with_eighty_and_one_hundred_percent_thresholds() {
        let defaults = default_threshold_rules();
        assert_eq!(
            defaults.clone().map(|rule| rule.threshold_percent),
            DEFAULT_THRESHOLD_PERCENTAGES
        );
        assert!(defaults.iter().all(|rule| rule.enabled));
    }

    #[test]
    fn threshold_crossing_is_emitted_once_while_above() {
        let initial = ThresholdState {
            is_above: false,
            crossing_generation: 0,
        };
        let crossed = evaluate_threshold(10_000, 8_000, 80, initial).unwrap();
        assert!(crossed.crossed_upward);
        assert_eq!(crossed.state.crossing_generation, 1);

        let remains_above = evaluate_threshold(10_000, 8_500, 80, crossed.state).unwrap();
        assert!(!remains_above.crossed_upward);
        assert_eq!(remains_above.state.crossing_generation, 1);
    }

    #[test]
    fn falling_below_rearms_the_next_upward_crossing() {
        let above = ThresholdState {
            is_above: true,
            crossing_generation: 3,
        };
        let below = evaluate_threshold(10_000, 7_999, 80, above).unwrap();
        assert!(below.crossed_downward);
        assert!(!below.state.is_above);
        assert_eq!(below.state.crossing_generation, 3);

        let above_again = evaluate_threshold(10_000, 8_000, 80, below.state).unwrap();
        assert!(above_again.crossed_upward);
        assert_eq!(above_again.state.crossing_generation, 4);
    }

    #[test]
    fn exact_threshold_boundary_counts_as_crossed() {
        let transition = evaluate_threshold(
            10_000,
            10_000,
            100,
            ThresholdState {
                is_above: false,
                crossing_generation: 0,
            },
        )
        .unwrap();
        assert!(transition.crossed_upward);
    }

    #[test]
    fn category_rejects_control_characters() {
        let budget = NewBudget {
            category: "Food\nOther".into(),
            amount_minor_units: 10_000,
            period_start_unix: 1_800_000_000,
            period_end_unix: None,
        };
        assert_eq!(
            budget.validate(),
            Err(BudgetValidationError::InvalidCategory)
        );
    }

    #[test]
    fn custom_schedule_validation_checks_the_rule_shape() {
        let schedule = NewReminderSchedule {
            rule: CustomRecurrenceRule::Weekly {
                weekday_mask: 0b0001_0101,
            },
            local_time_minute: 9 * 60 + 30,
            timezone_name: "Asia/Kolkata".into(),
            starts_at_unix: 1_800_000_000,
            ends_at_unix: None,
            first_run_at_unix: 1_800_000_000,
            enabled: true,
        };
        assert!(schedule.validate().is_ok());

        let invalid = NewReminderSchedule {
            rule: CustomRecurrenceRule::Weekly { weekday_mask: 0 },
            ..schedule
        };
        assert_eq!(
            invalid.validate(),
            Err(BudgetValidationError::InvalidWeekdayMask)
        );
    }

    #[test]
    fn notification_idempotency_namespaces_are_separate() {
        let reminder = scheduled_reminder_idempotency_key(9, 1_800_000_000);
        let threshold = threshold_idempotency_key(9, 1_800_000_000, 80, 1);
        assert_ne!(reminder, threshold);
        assert!(reminder.len() <= MAX_IDEMPOTENCY_KEY_BYTES);
        assert!(threshold.len() <= MAX_IDEMPOTENCY_KEY_BYTES);
    }
}
