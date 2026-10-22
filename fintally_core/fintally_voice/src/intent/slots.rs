//! Zero-copy slot extraction for deterministic intent classification.
//! All extracted values are bounded spans into the caller-owned source string.

use super::classify::{
    find_phrase, is_word_char, skip_whitespace, Currency, ExtractedSlots, IntentKind, MissingSlot,
    MissingSlots, TextSpan,
};
use crate::transcript::TranscriptLanguage;

pub(super) fn extract_slots(
    text: &str,
    language: TranscriptLanguage,
    kind: IntentKind,
) -> ExtractedSlots {
    let amount = if matches!(
        kind,
        IntentKind::RecordExpense
            | IntentKind::RecordIncome
            | IntentKind::TransferMoney
            | IntentKind::SetBudget
            | IntentKind::PayBill
            | IntentKind::CalculateEmi
    ) {
        find_amount(text, language, kind)
    } else {
        None
    };

    let (payee, category) = match kind {
        IntentKind::TransferMoney => (
            find_value_after_any(text, &["to", "à", "को"], language),
            None,
        ),
        IntentKind::PayBill => (
            find_value_after_any(text, &["to", "à", "को"], language)
                .or_else(|| find_known_bill(text)),
            None,
        ),
        IntentKind::RecordIncome => (
            find_value_after_any(text, &["from", "de", "से"], language),
            None,
        ),
        IntentKind::RecordExpense => (
            None,
            find_value_after_any(text, &["on", "for", "pour", "पर", "के लिए"], language),
        ),
        IntentKind::SetBudget => (
            None,
            find_value_after_any(text, &["for", "on", "pour", "के लिए"], language),
        ),
        _ => (None, None),
    };

    ExtractedSlots {
        amount,
        payee,
        source_account: if kind == IntentKind::TransferMoney {
            find_value_after_any(text, &["from", "de", "से"], language)
        } else {
            None
        },
        category,
        date: find_date(text, language),
        period: find_period(text, language),
        interest_rate: if kind == IntentKind::CalculateEmi {
            find_interest_rate(text)
        } else {
            None
        },
        tenure: if kind == IntentKind::CalculateEmi {
            find_tenure(text, language)
        } else {
            None
        },
        currency: amount.and_then(|span| detect_currency(text, span)),
    }
}

pub(super) fn missing_for(kind: IntentKind, slots: ExtractedSlots) -> MissingSlots {
    let mut missing = MissingSlots::default();
    match kind {
        IntentKind::RecordExpense => {
            if slots.amount.is_none() {
                missing.push(MissingSlot::Amount);
            }
            if slots.payee.is_none() && slots.category.is_none() {
                missing.push(MissingSlot::PayeeOrCategory);
            }
        }
        IntentKind::RecordIncome => {
            if slots.amount.is_none() {
                missing.push(MissingSlot::Amount);
            }
        }
        IntentKind::TransferMoney => {
            if slots.amount.is_none() {
                missing.push(MissingSlot::Amount);
            }
            if slots.payee.is_none() {
                missing.push(MissingSlot::Destination);
            }
        }
        IntentKind::SetBudget => {
            if slots.amount.is_none() {
                missing.push(MissingSlot::Amount);
            }
            if slots.period.is_none() {
                missing.push(MissingSlot::BudgetPeriod);
            }
        }
        IntentKind::PayBill => {
            if slots.amount.is_none() {
                missing.push(MissingSlot::Amount);
            }
            if slots.payee.is_none() {
                missing.push(MissingSlot::BillOrPayee);
            }
        }
        IntentKind::CalculateEmi => {
            if slots.amount.is_none() {
                missing.push(MissingSlot::Amount);
            }
            if slots.interest_rate.is_none() {
                missing.push(MissingSlot::InterestRate);
            }
            if slots.tenure.is_none() {
                missing.push(MissingSlot::EmiTenure);
            }
        }
        IntentKind::CheckBalance | IntentKind::ListTransactions | IntentKind::Unknown => {}
    }
    missing
}

fn find_amount(text: &str, language: TranscriptLanguage, kind: IntentKind) -> Option<TextSpan> {
    let mut from = 0usize;
    let mut best: Option<(TextSpan, u8)> = None;
    let mut tied = false;
    while let Some(span) = next_number(text, from) {
        from = span.end().max(from + 1);
        if follows_percent_or_tenure(text, span, language) {
            continue;
        }
        if !has_currency_marker(text, span) && looks_like_date_or_identifier(text, span, language) {
            continue;
        }
        let score = amount_context_score(text, span, kind);
        if score == 0 {
            continue;
        }
        match best {
            None => {
                best = Some((span, score));
                tied = false;
            }
            Some((_, best_score)) if score > best_score => {
                best = Some((span, score));
                tied = false;
            }
            Some((_, best_score)) if score == best_score => tied = true,
            _ => {}
        }
    }
    if tied {
        None
    } else {
        best.map(|(span, _)| span)
    }
}

fn amount_context_score(text: &str, number: TextSpan, kind: IntentKind) -> u8 {
    if has_currency_marker(text, number) {
        return 100;
    }
    if let Some(distance) = closest_preceding_cue(
        text,
        number.start(),
        &["amount", "principal", "loan amount", "total"],
    ) {
        return 95u8.saturating_sub((distance / 8).min(10) as u8);
    }
    let mut cues: &[&str] = &[];
    match kind {
        IntentKind::RecordExpense => {
            cues = &[
                "spent",
                "expense",
                "bought",
                "paid for",
                "for",
                "खर्च",
                "dépensé",
                "depense",
            ]
        }
        IntentKind::RecordIncome => {
            cues = &[
                "received",
                "salary",
                "earned",
                "income",
                "credited",
                "वेतन",
                "revenu",
                "salaire",
            ]
        }
        IntentKind::TransferMoney => {
            cues = &[
                "transfer",
                "send",
                "move money",
                "भेजो",
                "ट्रांसफर",
                "virement",
            ]
        }
        IntentKind::SetBudget => {
            cues = &[
                "set budget",
                "change budget",
                "budget",
                "बजट",
                "définir budget",
            ]
        }
        IntentKind::PayBill => {
            cues = &[
                "pay bill",
                "pay rent",
                "pay electricity",
                "pay",
                "bill payment",
                "payer",
                "बिल भुगतान",
            ]
        }
        IntentKind::CalculateEmi => cues = &["calculate emi", "emi", "principal", "loan amount"],
        _ => {}
    }
    if let Some(distance) = closest_preceding_cue(text, number.start(), cues) {
        return 82u8.saturating_sub((distance / 4).min(18) as u8);
    }
    0
}

fn closest_preceding_cue(text: &str, before: usize, cues: &[&str]) -> Option<usize> {
    let mut best: Option<usize> = None;
    for cue in cues {
        let mut offset = 0usize;
        while offset <= before && offset < text.len() {
            let Some(span) = find_phrase(&text[offset..], cue) else {
                break;
            };
            let start = offset + span.start();
            let end = offset + span.end();
            if end <= before {
                let distance = before - end;
                if distance <= 64 && best.map_or(true, |current| distance < current) {
                    best = Some(distance);
                }
            }
            if start + cue.len() >= text.len() {
                break;
            }
            offset = start + cue.len();
        }
    }
    best
}

fn next_number(text: &str, from: usize) -> Option<TextSpan> {
    let bytes = text.as_bytes();
    let mut i = from.min(bytes.len());
    while i < bytes.len() && !bytes[i].is_ascii_digit() {
        i += 1;
    }
    if i == bytes.len() {
        return None;
    }
    let start = i;
    let mut decimal_seen = false;
    while i < bytes.len() {
        match bytes[i] {
            b'0'..=b'9' => i += 1,
            b',' if i > start
                && i + 1 < bytes.len()
                && bytes[i - 1].is_ascii_digit()
                && bytes[i + 1].is_ascii_digit() =>
            {
                i += 1
            }
            b'.' if !decimal_seen
                && i > start
                && i + 1 < bytes.len()
                && bytes[i + 1].is_ascii_digit() =>
            {
                decimal_seen = true;
                i += 1;
            }
            _ => break,
        }
    }
    TextSpan::new(start, i)
}

fn follows_percent_or_tenure(text: &str, number: TextSpan, language: TranscriptLanguage) -> bool {
    let after = skip_whitespace(text, number.end());
    if text
        .get(after..)
        .is_some_and(|tail| tail.as_bytes().first() == Some(&b'%'))
    {
        return true;
    }
    let tail = &text[after..];
    let units: &[&str] = match language {
        TranscriptLanguage::French => &["mois", "an", "ans", "année", "années"],
        TranscriptLanguage::Hindi => &["महीना", "महीने", "माह", "साल", "वर्ष"],
        TranscriptLanguage::English | TranscriptLanguage::Unknown => {
            &["month", "months", "year", "years", "yr", "yrs"]
        }
    };
    units
        .iter()
        .any(|unit| find_phrase(tail, unit).is_some_and(|span| span.start() == 0))
}

fn has_currency_marker(text: &str, number: TextSpan) -> bool {
    let before = &text[..number.start()];
    let after = &text[number.end()..];
    before.trim_end().ends_with('₹')
        || before.trim_end().ends_with('$')
        || before.trim_end().ends_with('€')
        || before.trim_end().ends_with('£')
        || ends_with_code(before, &["inr", "usd", "eur", "gbp", "rs", "rs."])
        || starts_with_currency(after)
}

fn detect_currency(text: &str, number: TextSpan) -> Option<Currency> {
    let before = &text[..number.start()];
    let after = &text[number.end()..];
    if before.trim_end().ends_with('₹')
        || after.trim_start().starts_with("₹")
        || ends_with_code(before, &["inr", "rs", "rs."])
        || starts_with_code(after, &["inr", "rupee", "rupees"])
    {
        Some(Currency::Inr)
    } else if before.trim_end().ends_with('$')
        || after.trim_start().starts_with("$")
        || ends_with_code(before, &["usd"])
        || starts_with_code(after, &["usd", "dollar", "dollars"])
    {
        Some(Currency::Usd)
    } else if before.trim_end().ends_with('€')
        || after.trim_start().starts_with("€")
        || ends_with_code(before, &["eur"])
        || starts_with_code(after, &["eur", "euro", "euros"])
    {
        Some(Currency::Eur)
    } else if before.trim_end().ends_with('£')
        || after.trim_start().starts_with("£")
        || ends_with_code(before, &["gbp"])
        || starts_with_code(after, &["gbp", "pound", "pounds"])
    {
        Some(Currency::Gbp)
    } else {
        None
    }
}

fn starts_with_currency(text: &str) -> bool {
    let tail = text.trim_start();
    tail.starts_with("₹")
        || tail.starts_with("$")
        || tail.starts_with("€")
        || tail.starts_with("£")
        || starts_with_code(
            tail,
            &[
                "inr", "usd", "eur", "gbp", "rupee", "rupees", "dollar", "dollars", "euro",
                "euros", "pound", "pounds",
            ],
        )
}

fn starts_with_code(text: &str, words: &[&str]) -> bool {
    let trimmed = text.trim_start();
    words.iter().any(|word| starts_with_word_ci(trimmed, word))
}

fn ends_with_code(text: &str, words: &[&str]) -> bool {
    let trimmed = text.trim_end_matches(|c: char| c.is_whitespace() || c == '.');
    words.iter().any(|word| {
        if trimmed.len() < word.len() {
            return false;
        }
        let start = trimmed.len() - word.len();
        let (Some(suffix), Some(prefix)) = (trimmed.get(start..), trimmed.get(..start)) else {
            return false;
        };
        suffix.eq_ignore_ascii_case(word)
            && prefix
                .chars()
                .next_back()
                .map_or(true, |ch| !is_word_char(ch))
    })
}

fn looks_like_date_or_identifier(
    text: &str,
    number: TextSpan,
    language: TranscriptLanguage,
) -> bool {
    let before = &text[..number.start()];
    let after = &text[number.end()..];
    if before
        .chars()
        .next_back()
        .is_some_and(|ch| matches!(ch, '/' | '-' | ':'))
        || after
            .chars()
            .next()
            .is_some_and(|ch| matches!(ch, '/' | '-' | ':'))
    {
        return true;
    }
    if ends_with_code(
        before,
        &[
            "account",
            "account no",
            "id",
            "reference",
            "invoice",
            "card",
            "phone",
            "transaction number",
        ],
    ) {
        return true;
    }
    // A four-digit year is not an amount unless explicitly marked with a currency.
    if let Some(value) = number.resolve(text).and_then(|raw| raw.parse::<u16>().ok()) {
        if (1900..=2200).contains(&value) {
            return true;
        }
    }
    let months: &[&str] = match language {
        TranscriptLanguage::French => &[
            "janvier",
            "février",
            "mars",
            "avril",
            "mai",
            "juin",
            "juillet",
            "août",
            "septembre",
            "octobre",
            "novembre",
            "décembre",
        ],
        TranscriptLanguage::Hindi => &[
            "जनवरी",
            "फरवरी",
            "मार्च",
            "अप्रैल",
            "मई",
            "जून",
            "जुलाई",
            "अगस्त",
            "सितंबर",
            "अक्टूबर",
            "नवंबर",
            "दिसंबर",
        ],
        TranscriptLanguage::English | TranscriptLanguage::Unknown => &[
            "january",
            "february",
            "march",
            "april",
            "may",
            "june",
            "july",
            "august",
            "september",
            "october",
            "november",
            "december",
        ],
    };
    let numeric_width = number.end() - number.start();
    months
        .iter()
        .filter_map(|month| find_phrase(text, month))
        .any(|month| {
            // Month + one/two-digit number commonly represents dates (e.g. October 12).
            let month_before_day = month.end() <= number.start()
                && number.start() - month.end() <= 4
                && numeric_width <= 2;
            // Day + month only looks date-like with an explicit date preposition ("on 12 March").
            let date_preposition = ends_with_code(
                before,
                &["on", "on the", "dated", "date", "le", "du", "दिनांक"],
            );
            let day_before_month = date_preposition
                && number.end() <= month.start()
                && month.start() - number.end() <= 16
                && numeric_width <= 2;
            month_before_day || day_before_month
        })
}

fn find_interest_rate(text: &str) -> Option<TextSpan> {
    let mut from = 0usize;
    while let Some(number) = next_number(text, from) {
        from = number.end().max(from + 1);
        let after = skip_whitespace(text, number.end());
        if text
            .get(after..)
            .is_some_and(|tail| tail.as_bytes().first() == Some(&b'%'))
        {
            return TextSpan::new(number.start(), after + 1);
        }
    }
    None
}

fn find_tenure(text: &str, language: TranscriptLanguage) -> Option<TextSpan> {
    let units: &[&str] = match language {
        TranscriptLanguage::French => &["mois", "an", "ans", "année", "années"],
        TranscriptLanguage::Hindi => &["महीना", "महीने", "माह", "साल", "वर्ष"],
        TranscriptLanguage::English | TranscriptLanguage::Unknown => {
            &["month", "months", "year", "years", "yr", "yrs"]
        }
    };
    let mut from = 0usize;
    while let Some(number) = next_number(text, from) {
        from = number.end().max(from + 1);
        let after = skip_whitespace(text, number.end());
        for unit in units {
            if let Some(unit_span) = find_phrase(&text[after..], unit) {
                if unit_span.start() == 0 {
                    return TextSpan::new(number.start(), after + unit_span.end());
                }
            }
        }
    }
    None
}

fn find_date(text: &str, language: TranscriptLanguage) -> Option<TextSpan> {
    let words: &[&str] = match language {
        TranscriptLanguage::French => &["aujourd'hui", "demain", "hier"],
        TranscriptLanguage::Hindi => &["आज", "कल", "परसों"],
        TranscriptLanguage::English | TranscriptLanguage::Unknown => {
            &["today", "tomorrow", "yesterday"]
        }
    };
    words
        .iter()
        .filter_map(|word| find_phrase(text, word))
        .chain(find_iso_date(text))
        .min_by_key(|span| span.start())
}

fn find_iso_date(text: &str) -> Option<TextSpan> {
    let bytes = text.as_bytes();
    if bytes.len() < 10 {
        return None;
    }
    for start in 0..=bytes.len() - 10 {
        if start > 0 && bytes[start - 1].is_ascii_digit() {
            continue;
        }
        let candidate = &bytes[start..start + 10];
        let valid = candidate[0..4].iter().all(u8::is_ascii_digit)
            && candidate[4] == b'-'
            && candidate[5..7].iter().all(u8::is_ascii_digit)
            && candidate[7] == b'-'
            && candidate[8..10].iter().all(u8::is_ascii_digit);
        if !valid || (start + 10 < bytes.len() && bytes[start + 10].is_ascii_digit()) {
            continue;
        }
        if text.is_char_boundary(start) && text.is_char_boundary(start + 10) {
            return TextSpan::new(start, start + 10);
        }
    }
    None
}

fn find_period(text: &str, language: TranscriptLanguage) -> Option<TextSpan> {
    let words: &[&str] = match language {
        TranscriptLanguage::French => &[
            "ce mois",
            "ce mois-ci",
            "mois prochain",
            "chaque mois",
            "annuel",
            "mensuel",
        ],
        TranscriptLanguage::Hindi => &["इस महीने", "अगले महीने", "हर महीने", "मासिक", "सालाना"],
        TranscriptLanguage::English | TranscriptLanguage::Unknown => &[
            "this month",
            "last month",
            "next month",
            "monthly",
            "weekly",
            "yearly",
            "annually",
            "this week",
            "this year",
        ],
    };
    words
        .iter()
        .filter_map(|word| find_phrase(text, word))
        .min_by_key(|span| span.start())
}

fn find_known_bill(text: &str) -> Option<TextSpan> {
    const BILLS: &[&str] = &[
        "electricity bill",
        "water bill",
        "internet bill",
        "phone bill",
        "gas bill",
        "credit card",
        "rent",
        "electricity",
        "water bill",
        "internet",
        "rent bill",
        "बिजली बिल",
        "किराया",
        "facture",
        "loyer",
    ];
    BILLS
        .iter()
        .filter_map(|word| find_phrase(text, word))
        .min_by_key(|span| span.start())
}

fn find_value_after_any(
    text: &str,
    keywords: &[&str],
    language: TranscriptLanguage,
) -> Option<TextSpan> {
    let mut connector: Option<TextSpan> = None;
    for keyword in keywords {
        if let Some(span) = find_phrase(text, keyword) {
            if connector.map_or(true, |current| span.start() < current.start()) {
                connector = Some(span);
            }
        }
    }
    let connector = connector?;
    let mut start = skip_whitespace(text, connector.end());
    while text
        .as_bytes()
        .get(start)
        .is_some_and(|byte| *byte == b':' || *byte == b'=')
    {
        start = skip_whitespace(text, start + 1);
    }
    if start >= text.len() {
        return None;
    }

    let tail = &text[start..];
    let stop_words: &[&str] = match language {
        TranscriptLanguage::French => &[
            " demain",
            " aujourd'hui",
            " hier",
            " depuis",
            " avec",
            " sur",
            " pour",
            " à",
        ],
        TranscriptLanguage::Hindi => &[" आज", " कल", " से", " पर", " के लिए", " को"],
        TranscriptLanguage::English | TranscriptLanguage::Unknown => &[
            " tomorrow",
            " today",
            " yesterday",
            " from",
            " using",
            " with",
            " on",
            " for",
            " to",
            " and",
            " but",
            " because",
            " then",
        ],
    };
    let mut end = text.len();
    for (relative, ch) in tail.char_indices() {
        if matches!(ch, ',' | ';' | '!' | '?' | '\n' | '\r') {
            end = end.min(start + relative);
            break;
        }
        if ch == '.' {
            end = end.min(start + relative);
            break;
        }
    }
    for stop in stop_words {
        if let Some(span) = find_phrase(tail, stop.trim_start()) {
            // A stop phrase only shortens this value when it follows a separator space.
            if span.start() > 0
                && tail[..span.start()]
                    .chars()
                    .next_back()
                    .is_some_and(|ch| ch.is_whitespace())
            {
                end = end.min(start + span.start());
            }
        }
    }
    while end > start
        && text[..end]
            .chars()
            .next_back()
            .is_some_and(char::is_whitespace)
    {
        end -= text[..end].chars().next_back().map_or(0, char::len_utf8);
    }
    while start < end
        && text[start..]
            .chars()
            .next()
            .is_some_and(char::is_whitespace)
    {
        start += text[start..].chars().next().map_or(0, char::len_utf8);
    }
    if start >= end {
        None
    } else {
        TextSpan::new(start, end)
    }
}

fn starts_with_word_ci(text: &str, word: &str) -> bool {
    text.get(..word.len())
        .is_some_and(|prefix| prefix.eq_ignore_ascii_case(word))
        && text
            .get(word.len()..)
            .and_then(|tail| tail.chars().next())
            .map_or(true, |ch| !is_word_char(ch))
}
