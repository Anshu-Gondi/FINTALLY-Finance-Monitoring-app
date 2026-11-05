//! IPC protocol version policy.
//!
//! Increment the protocol version for incompatible wire-format changes.
//! Keep compatibility decisions centralized in this module.
//!
//! Version policy:
//! - Version 1 is the initial authentication/list-only protocol.
//! - Version 2 adds typed transaction CRUD/receipt-metadata commands and the
//!   `has_more` page field.
//! - Version 3 adds budget CRUD commands and budget response DTOs.
//! - Version 4 adds authenticated feedback submission and receipt DTOs. Older
//!   clients cannot safely deserialize the new response variant.
//! - Unknown versions are rejected.
//! - Backward compatibility must be introduced explicitly and tested.

/// Current IPC protocol version.
pub const IPC_PROTOCOL_VERSION: u16 = 4;

/// Returns whether this implementation accepts the supplied protocol version.
///
/// This deliberately accepts only the current version. If compatible older
/// versions are introduced later, update this policy and compatibility tests.
#[inline]
pub const fn is_supported_protocol_version(version: u16) -> bool {
    version == IPC_PROTOCOL_VERSION
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn accepts_current_version() {
        assert!(is_supported_protocol_version(IPC_PROTOCOL_VERSION));
    }

    #[test]
    fn rejects_older_unsupported_version() {
        assert!(!is_supported_protocol_version(0));
    }

    #[test]
    fn rejects_future_version() {
        assert!(!is_supported_protocol_version(IPC_PROTOCOL_VERSION + 1));
    }

    #[test]
    fn rejects_maximum_version() {
        assert!(!is_supported_protocol_version(u16::MAX));
    }
}
