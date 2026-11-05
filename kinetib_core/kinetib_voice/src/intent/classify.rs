//! Deterministic, bounded intent classification for FinTally Voice.
//!
//! This is a conservative rule engine, not an execution engine. It never performs
//! transactions, writes budgets, or calls a financial API. A caller must honor
//! `IntentDecision`; mutating intents are confirmation-gated, and uncertain or
//! negated wording is routed to clarification.
//!
//! Resource contract:
//! - Input is rejected above `MAX_INTENT_TEXT_BYTES`.
//! - Classification uses fixed-size score/slot arrays and does not allocate.
//! - Extracted values are byte spans into the original UTF-8 text. No amount,
//!   payee, date, currency, or negation text is normalized or rewritten.
//! - This is a service/control-path function, not an audio callback. The 100 KiB
//!   DSP working-set budget is owned by the engine/runtime, not by this module.

use super::rules::RULES;
use super::slots::{extract_slots, missing_for};
use crate::transcript::TranscriptLanguage;
use thiserror::Error;

/// Maximum accepted UTF-8 input, including spaces and punctuation.
pub const MAX_INTENT_TEXT_BYTES: usize = 2 * 1024;

/// Below this score, the command is never eligible for a non-clarification path.
pub const MIN_CONFIDENCE: f32 = 0.80;

/// A winner must exceed the next distinct intent by at least this many points.
const MIN_SCORE_MARGIN: u8 = 12;
const INTENT_COUNT: usize = 8;
const MAX_MISSING_SLOTS: usize = 4;

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntentKind {
    RecordExpense,
    RecordIncome,
    TransferMoney,
    SetBudget,
    PayBill,
    CheckBalance,
    ListTransactions,
    CalculateEmi,
    Unknown,
}

impl IntentKind {
    const fn index(self) -> Option<usize> {
        match self {
            Self::RecordExpense => Some(0),
            Self::RecordIncome => Some(1),
            Self::TransferMoney => Some(2),
            Self::SetBudget => Some(3),
            Self::PayBill => Some(4),
            Self::CheckBalance => Some(5),
            Self::ListTransactions => Some(6),
            Self::CalculateEmi => Some(7),
            Self::Unknown => None,
        }
    }

    const fn is_mutating(self) -> bool {
        matches!(
            self,
            Self::RecordExpense
                | Self::RecordIncome
                | Self::TransferMoney
                | Self::SetBudget
                | Self::PayBill
        )
    }
}

/// Caller-facing gate. `NeedsConfirmation` is a request to show a review step,
/// not permission to execute a transaction without the user's confirmation.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum IntentDecision {
    /// A sufficiently clear read-only query or EMI calculation.
    ReadOnly,
    /// A complete, high-confidence mutation that must be reviewed and confirmed.
    NeedsConfirmation,
    /// Ambiguous, negated, incomplete, or low-confidence wording.
    NeedsClarification,
    /// No supported intent matched.
    NoMatch,
}

/// The explicit currency marker seen near the extracted amount, if any.
/// No exchange-rate lookup or currency conversion is performed.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Currency {
    Inr,
    Usd,
    Eur,
    Gbp,
}

/// A UTF-8 byte range in the original input. This type does not own or copy text.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TextSpan {
    start: u16,
    end: u16,
}

impl TextSpan {
    pub(super) fn new(start: usize, end: usize) -> Option<Self> {
        if start <= end && end <= MAX_INTENT_TEXT_BYTES {
            Some(Self {
                start: start as u16,
                end: end as u16,
            })
        } else {
            None
        }
    }

    pub const fn start(self) -> usize {
        self.start as usize
    }

    pub const fn end(self) -> usize {
        self.end as usize
    }

    /// Resolve this span against the same input passed to `classify_intent`.
    /// Returns `None` if the supplied text is too short or an offset is not a UTF-8 boundary;
    /// callers must resolve spans against the exact input used for classification.
    pub fn resolve<'a>(self, original: &'a str) -> Option<&'a str> {
        let start = self.start();
        let end = self.end();
        if start <= end
            && end <= original.len()
            && original.is_char_boundary(start)
            && original.is_char_boundary(end)
        {
            original.get(start..end)
        } else {
            None
        }
    }
}

/// Only spans and compact metadata are retained; text remains owned by the caller.
#[derive(Debug, Clone, Copy, Default, PartialEq, Eq)]
pub struct ExtractedSlots {
    /// Numeric principal/transaction amount. The exact source spelling is preserved.
    pub amount: Option<TextSpan>,
    /// Destination, payee, income source, or bill target, depending on the intent.
    pub payee: Option<TextSpan>,
    /// Explicit source account on a transfer; the caller must otherwise resolve the
    /// selected/default account and show it on the confirmation screen.
    pub source_account: Option<TextSpan>,
    /// Expense/budget category, when expressed explicitly.
    pub category: Option<TextSpan>,
    /// Explicit relative date or ISO-8601 calendar date, preserved verbatim.
    pub date: Option<TextSpan>,
    /// Explicit budget/query period such as `this month` or `monthly`.
    pub period: Option<TextSpan>,
    /// Explicit interest rate, including its percent marker where present.
    pub interest_rate: Option<TextSpan>,
    /// EMI tenure phrase such as `24 months` or `2 years`.
    pub tenure: Option<TextSpan>,
    /// Currency code/symbol when explicitly stated beside the amount.
    pub currency: Option<Currency>,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum MissingSlot {
    Amount,
    PayeeOrCategory,
    Destination,
    BillOrPayee,
    BudgetPeriod,
    InterestRate,
    EmiTenure,
}

/// Fixed-capacity list: no heap allocation is needed to report missing fields.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct MissingSlots {
    entries: [Option<MissingSlot>; MAX_MISSING_SLOTS],
    len: u8,
}

impl Default for MissingSlots {
    fn default() -> Self {
        Self {
            entries: [None; MAX_MISSING_SLOTS],
            len: 0,
        }
    }
}

impl MissingSlots {
    pub(super) fn push(&mut self, slot: MissingSlot) {
        let len = usize::from(self.len);
        if len < self.entries.len() {
            self.entries[len] = Some(slot);
            self.len += 1;
        }
    }

    pub fn is_empty(self) -> bool {
        self.len == 0
    }

    pub fn len(self) -> usize {
        usize::from(self.len)
    }

    pub fn contains(self, requested: MissingSlot) -> bool {
        self.iter().any(|slot| slot == requested)
    }

    pub fn iter(&self) -> impl Iterator<Item = MissingSlot> + '_ {
        self.entries[..usize::from(self.len)]
            .iter()
            .filter_map(|slot| *slot)
    }
}

/// A stable classification result. The value contains no heap-owned strings.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct IntentResult {
    pub kind: IntentKind,
    /// Deterministic rule score from 0.0 to 1.0, not a calibrated ML probability.
    pub confidence: f32,
    pub decision: IntentDecision,
    pub slots: ExtractedSlots,
    pub missing_slots: MissingSlots,
    /// True when multiple different intents have scores too close to choose safely.
    pub ambiguous: bool,
    /// True when a conservative negation/cancellation cue was detected.
    pub negated: bool,
    /// The matched rule phrase, useful for explanations/telemetry without copying text.
    pub evidence: Option<TextSpan>,
}

#[derive(Debug, Error, Clone, PartialEq, Eq)]
pub enum ClassificationError {
    #[error("intent input is {actual} bytes; maximum is {max} bytes")]
    InputTooLong { actual: usize, max: usize },
}

/// Classify text without normalizing or mutating the source transcript.
///
/// `TranscriptLanguage::Unknown` enables only English rules at deliberately low
/// confidence; it can never authorize a financial mutation or read-only response.
pub fn classify_intent(
    text: &str,
    language: TranscriptLanguage,
) -> Result<IntentResult, ClassificationError> {
    if text.len() > MAX_INTENT_TEXT_BYTES {
        return Err(ClassificationError::InputTooLong {
            actual: text.len(),
            max: MAX_INTENT_TEXT_BYTES,
        });
    }

    let mut scores = [0u8; INTENT_COUNT];
    let mut evidence = [None; INTENT_COUNT];
    let rules_language = if language == TranscriptLanguage::Unknown {
        TranscriptLanguage::English
    } else {
        language
    };

    for rule in RULES.iter().filter(|rule| rule.language == rules_language) {
        if let Some(span) = find_phrase(text, rule.phrase) {
            if let Some(index) = rule.kind.index() {
                if rule.score > scores[index] {
                    scores[index] = rule.score;
                    evidence[index] = Some(span);
                }
            }
        }
    }

    let mut best_index: Option<usize> = None;
    let mut best_score = 0u8;
    let mut second_score = 0u8;
    for (index, &score) in scores.iter().enumerate() {
        if score > best_score {
            second_score = best_score;
            best_score = score;
            best_index = Some(index);
        } else if score > second_score {
            second_score = score;
        }
    }

    let Some(index) = best_index else {
        return Ok(IntentResult {
            kind: IntentKind::Unknown,
            confidence: 0.0,
            decision: IntentDecision::NoMatch,
            slots: ExtractedSlots::default(),
            missing_slots: MissingSlots::default(),
            ambiguous: false,
            negated: contains_negation(text, language),
            evidence: None,
        });
    };

    let kind = kind_from_index(index);
    let ambiguous = second_score > 0 && best_score.saturating_sub(second_score) < MIN_SCORE_MARGIN;
    let mut confidence = f32::from(best_score) / 100.0;
    if ambiguous {
        confidence = confidence.min(0.59);
    }
    if language == TranscriptLanguage::Unknown {
        confidence = confidence.min(0.69);
    }

    let slots = extract_slots(text, language, kind);
    let missing_slots = missing_for(kind, slots);
    let negated = contains_negation(text, language);
    let decision = decide(kind, confidence, ambiguous, negated, missing_slots);

    Ok(IntentResult {
        kind,
        confidence,
        decision,
        slots,
        missing_slots,
        ambiguous,
        negated,
        evidence: evidence[index],
    })
}

fn decide(
    kind: IntentKind,
    confidence: f32,
    ambiguous: bool,
    negated: bool,
    missing: MissingSlots,
) -> IntentDecision {
    if kind == IntentKind::Unknown {
        return IntentDecision::NoMatch;
    }
    if ambiguous || negated || confidence < MIN_CONFIDENCE || !missing.is_empty() {
        return IntentDecision::NeedsClarification;
    }
    if kind.is_mutating() {
        return IntentDecision::NeedsConfirmation;
    }
    IntentDecision::ReadOnly
}

fn kind_from_index(index: usize) -> IntentKind {
    match index {
        0 => IntentKind::RecordExpense,
        1 => IntentKind::RecordIncome,
        2 => IntentKind::TransferMoney,
        3 => IntentKind::SetBudget,
        4 => IntentKind::PayBill,
        5 => IntentKind::CheckBalance,
        6 => IntentKind::ListTransactions,
        7 => IntentKind::CalculateEmi,
        _ => IntentKind::Unknown,
    }
}

fn contains_negation(text: &str, language: TranscriptLanguage) -> bool {
    const EN: &[&str] = &[
        "do not",
        "don't",
        "dont",
        "never",
        "not",
        "cancel",
        "cancelled",
        "canceled",
    ];
    const FR: &[&str] = &["ne", "pas", "annule", "annuler", "n'"];
    const HI: &[&str] = &["नहीं", "मत", "ना", "रद्द"];
    let words = match language {
        TranscriptLanguage::English | TranscriptLanguage::Unknown => EN,
        TranscriptLanguage::French => FR,
        TranscriptLanguage::Hindi => HI,
    };
    words.iter().any(|word| find_phrase(text, word).is_some())
}

/// Finds the first standalone phrase without allocating or changing text.
pub(super) fn find_phrase(text: &str, phrase: &str) -> Option<TextSpan> {
    if phrase.is_empty() || phrase.len() > text.len() {
        return None;
    }
    for (start, _) in text.char_indices() {
        let end = start.checked_add(phrase.len())?;
        if end > text.len() || !text.is_char_boundary(end) {
            continue;
        }
        if !phrase_eq_case_insensitive(text.get(start..end)?, phrase) {
            continue;
        }
        let left_ok = text[..start]
            .chars()
            .next_back()
            .map_or(true, |ch| !is_word_char(ch));
        let right_ok = text[end..]
            .chars()
            .next()
            .map_or(true, |ch| !is_word_char(ch));
        if left_ok && right_ok {
            return TextSpan::new(start, end);
        }
    }
    None
}

fn phrase_eq_case_insensitive(candidate: &str, phrase: &str) -> bool {
    candidate == phrase
        || candidate.eq_ignore_ascii_case(phrase)
        || candidate
            .chars()
            .flat_map(char::to_lowercase)
            .eq(phrase.chars().flat_map(char::to_lowercase))
}

pub(super) fn is_word_char(ch: char) -> bool {
    ch.is_alphanumeric() || ch == '_'
}

pub(super) fn skip_whitespace(text: &str, mut index: usize) -> usize {
    while index < text.len() {
        let Some(ch) = text[index..].chars().next() else {
            break;
        };
        if !ch.is_whitespace() {
            break;
        }
        index += ch.len_utf8();
    }
    index
}

#[cfg(test)]
mod tests {
    use super::*;

    fn classify(text: &str) -> IntentResult {
        classify_intent(text, TranscriptLanguage::English).expect("bounded input")
    }

    #[test]
    fn transfer_extracts_original_amount_and_destination_but_requires_confirmation() {
        let input = "Transfer ₹1,250.50 to ACME Ltd";
        let result = classify(input);
        assert_eq!(result.kind, IntentKind::TransferMoney);
        assert_eq!(result.decision, IntentDecision::NeedsConfirmation);
        assert_eq!(
            result.slots.amount.and_then(|span| span.resolve(input)),
            Some("1,250.50")
        );
        assert_eq!(
            result.slots.payee.and_then(|span| span.resolve(input)),
            Some("ACME Ltd")
        );
        assert_eq!(result.slots.currency, Some(Currency::Inr));
    }

    #[test]
    fn negated_money_movement_never_reaches_confirmation() {
        let input = "do not transfer 1250 to ACME Ltd";
        let result = classify(input);
        assert_eq!(result.kind, IntentKind::TransferMoney);
        assert!(result.negated);
        assert_eq!(result.decision, IntentDecision::NeedsClarification);
    }

    #[test]
    fn incomplete_transfer_reports_amount_and_destination() {
        let result = classify("transfer money");
        assert_eq!(result.decision, IntentDecision::NeedsClarification);
        assert!(result.missing_slots.contains(MissingSlot::Amount));
        assert!(result.missing_slots.contains(MissingSlot::Destination));
    }

    #[test]
    fn transfer_preserves_explicit_source_and_destination_accounts() {
        let input = "transfer ₹500 from Checking to Savings";
        let result = classify(input);
        assert_eq!(
            result
                .slots
                .source_account
                .and_then(|span| span.resolve(input)),
            Some("Checking")
        );
        assert_eq!(
            result.slots.payee.and_then(|span| span.resolve(input)),
            Some("Savings")
        );
    }

    #[test]
    fn expense_requires_amount_and_category_or_payee() {
        let input = "I spent ₹450 on groceries";
        let result = classify(input);
        assert_eq!(result.kind, IntentKind::RecordExpense);
        assert_eq!(result.decision, IntentDecision::NeedsConfirmation);
        assert_eq!(
            result.slots.amount.and_then(|span| span.resolve(input)),
            Some("450")
        );
        assert_eq!(
            result.slots.category.and_then(|span| span.resolve(input)),
            Some("groceries")
        );
    }

    #[test]
    fn competing_financial_intents_are_never_authorized() {
        let result = classify("I spent salary 500");
        assert!(result.ambiguous);
        assert_eq!(result.decision, IntentDecision::NeedsClarification);
    }

    #[test]
    fn inquiry_is_read_only_and_does_not_require_financial_confirmation() {
        let result = classify("check balance");
        assert_eq!(result.kind, IntentKind::CheckBalance);
        assert_eq!(result.decision, IntentDecision::ReadOnly);
    }

    #[test]
    fn unknown_language_cannot_authorize_a_mutation() {
        let result = classify_intent("transfer 500 to Bob", TranscriptLanguage::Unknown).unwrap();
        assert_eq!(result.kind, IntentKind::TransferMoney);
        assert!(result.confidence < MIN_CONFIDENCE);
        assert_eq!(result.decision, IntentDecision::NeedsClarification);
    }

    #[test]
    fn account_identifiers_do_not_become_amounts() {
        let result = classify("transfer funds from account 1234");
        assert_eq!(result.kind, IntentKind::TransferMoney);
        assert_eq!(result.slots.amount, None);
        assert_eq!(result.decision, IntentDecision::NeedsClarification);
    }

    #[test]
    fn emi_calculation_extracts_principal_rate_and_tenure_without_mutation() {
        let input = "calculate EMI for principal 100000 at 12% for 24 months";
        let result = classify(input);
        assert_eq!(result.kind, IntentKind::CalculateEmi);
        assert_eq!(
            result.slots.amount.and_then(|span| span.resolve(input)),
            Some("100000")
        );
        assert_eq!(
            result
                .slots
                .interest_rate
                .and_then(|span| span.resolve(input)),
            Some("12%")
        );
        assert_eq!(
            result.slots.tenure.and_then(|span| span.resolve(input)),
            Some("24 months")
        );
        assert_eq!(result.decision, IntentDecision::ReadOnly);
    }

    #[test]
    fn date_components_are_not_extracted_as_financial_amounts() {
        let result = classify("set budget for 2026-10-21 monthly");
        assert_eq!(result.kind, IntentKind::SetBudget);
        assert_eq!(result.slots.amount, None);
        assert_eq!(result.decision, IntentDecision::NeedsClarification);
    }

    #[test]
    fn a_month_mentioned_after_an_expense_does_not_hide_its_amount() {
        let input = "I spent 100 in March";
        let result = classify(input);
        assert_eq!(
            result.slots.amount.and_then(|span| span.resolve(input)),
            Some("100")
        );
    }

    #[test]
    fn iso_date_is_extracted_without_reformatting() {
        let input = "show transactions on 2026-10-21";
        let result = classify(input);
        assert_eq!(
            result.slots.date.and_then(|span| span.resolve(input)),
            Some("2026-10-21")
        );
    }

    #[test]
    fn oversized_input_is_rejected_not_truncated() {
        let input = "x".repeat(MAX_INTENT_TEXT_BYTES + 1);
        assert_eq!(
            classify_intent(&input, TranscriptLanguage::English),
            Err(ClassificationError::InputTooLong {
                actual: MAX_INTENT_TEXT_BYTES + 1,
                max: MAX_INTENT_TEXT_BYTES,
            })
        );
    }
}
