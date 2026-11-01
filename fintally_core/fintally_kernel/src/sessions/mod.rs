//! Opaque, expiring, revocable sessions.

pub mod expiry;
pub mod store;
pub mod token;

pub use store::{
    SessionIdentity, SessionLookupError, SessionStore, SessionStoreError, HARD_MAX_ACTIVE_SESSIONS,
};
