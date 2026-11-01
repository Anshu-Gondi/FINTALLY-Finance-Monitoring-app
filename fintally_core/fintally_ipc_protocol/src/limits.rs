//! Resource and field limits for the IPC wire protocol.
//!
//! These constants bound encoded frames and individual user-controlled fields.
//! The aggregate string-content ceiling for a maximum page is a theoretical
//! upper bound; it does not promise every maximum-sized page will fit one frame.

/// Fixed transport header reserved ahead of the encoded JSON payload.
pub const FRAME_HEADER_BYTES: usize = 16;

/// Hard upper bound for one complete wire frame, header included (64 KiB).
pub const MAX_FRAME_BYTES: usize = 65_536;

/// Maximum bytes available to the encoded payload after reserving the header.
pub const MAX_PAYLOAD_BYTES: usize = MAX_FRAME_BYTES - FRAME_HEADER_BYTES;

/// Default number of transaction records requested by the client.
pub const DEFAULT_PAGE_SIZE: u16 = 10;

/// Maximum transaction records requested in one page.
pub const MAX_PAGE_SIZE: u16 = 100;

/// Maximum supported one-based transaction page index.
pub const MAX_PAGE: u32 = 10_000;

/// Maximum number of budget records in one response (fits the 64 KiB frame).
pub const MAX_BUDGETS_PER_RESPONSE: usize = 64;

/// Maximum UTF-8 byte length of a budget category.
pub const MAX_BUDGET_CATEGORY_BYTES: usize = 64;

/// Maximum UTF-8 bytes accepted for a login email field.
pub const MAX_EMAIL_BYTES: usize = 320;

/// Maximum UTF-8 bytes accepted for a login password field.
pub const MAX_PASSWORD_BYTES: usize = 72;

/// Maximum UTF-8 bytes accepted for an opaque session token.
pub const MAX_SESSION_TOKEN_BYTES: usize = 4_096;

/// Maximum UTF-8 bytes in a transaction name.
pub const MAX_TRANSACTION_NAME_BYTES: usize = 128;

/// Maximum UTF-8 bytes in a transaction description.
pub const MAX_TRANSACTION_DESCRIPTION_BYTES: usize = 512;

/// Maximum UTF-8 bytes in a transaction category.
pub const MAX_TRANSACTION_CATEGORY_BYTES: usize = 64;

/// Maximum UTF-8 bytes in an optional receipt URL.
pub const MAX_RECEIPT_URL_BYTES: usize = 512;

/// Aggregate transaction string-content ceiling for one record.
pub const MAX_TRANSACTION_STRING_BYTES_PER_ITEM: usize = MAX_TRANSACTION_NAME_BYTES
    + MAX_TRANSACTION_DESCRIPTION_BYTES
    + MAX_TRANSACTION_CATEGORY_BYTES
    + MAX_RECEIPT_URL_BYTES;

/// Aggregate transaction string-content ceiling across a maximum-size page.
///
/// This is deliberately not compared with `MAX_PAYLOAD_BYTES`: the theoretical
/// raw-string ceiling can exceed one frame. `codec::encode_payload` enforces
/// the actual serialized size, including JSON syntax and escaping.
pub const MAX_TRANSACTION_STRING_BYTES_PER_PAGE: usize =
    MAX_TRANSACTION_STRING_BYTES_PER_ITEM * MAX_PAGE_SIZE as usize;

// Verify configuration invariants at compile time rather than with runtime
// assertions inside unit tests.
const _: () = {
    assert!(FRAME_HEADER_BYTES < MAX_FRAME_BYTES);
    assert!(MAX_PAYLOAD_BYTES > 0);
    assert!(FRAME_HEADER_BYTES + MAX_PAYLOAD_BYTES == MAX_FRAME_BYTES);
    assert!(MAX_TRANSACTION_STRING_BYTES_PER_ITEM < MAX_PAYLOAD_BYTES);
    assert!(DEFAULT_PAGE_SIZE > 0);
    assert!(DEFAULT_PAGE_SIZE <= MAX_PAGE_SIZE);
    assert!(MAX_PAGE_SIZE <= 100);
    assert!(MAX_PAGE > 0);
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn frame_limit_includes_header() {
        assert_eq!(FRAME_HEADER_BYTES + MAX_PAYLOAD_BYTES, MAX_FRAME_BYTES);
    }

    #[test]
    fn frame_limit_is_exactly_64_kib() {
        assert_eq!(MAX_FRAME_BYTES, 65_536);
    }

    #[test]
    fn transaction_string_budget_is_consistent() {
        assert_eq!(
            MAX_TRANSACTION_STRING_BYTES_PER_ITEM,
            MAX_TRANSACTION_NAME_BYTES
                + MAX_TRANSACTION_DESCRIPTION_BYTES
                + MAX_TRANSACTION_CATEGORY_BYTES
                + MAX_RECEIPT_URL_BYTES
        );

        assert_eq!(
            MAX_TRANSACTION_STRING_BYTES_PER_PAGE,
            MAX_TRANSACTION_STRING_BYTES_PER_ITEM * MAX_PAGE_SIZE as usize
        );
    }
}
