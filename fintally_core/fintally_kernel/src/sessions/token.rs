//! Cryptographically random, opaque bearer-token generation.

use sha2::{Digest, Sha256};
use std::fmt;
use thiserror::Error;
use zeroize::Zeroize;

const TOKEN_BYTES: usize = 32;
const TOKEN_HEX_BYTES: usize = TOKEN_BYTES * 2;

#[derive(Debug, Error)]
#[error("operating-system random source failed")]
pub struct TokenGenerationError;

#[derive(Clone, PartialEq, Eq, Hash)]
pub(crate) struct TokenFingerprint([u8; 32]);

impl fmt::Debug for TokenFingerprint {
    fn fmt(&self, formatter: &mut fmt::Formatter<'_>) -> fmt::Result {
        formatter.write_str("TokenFingerprint([REDACTED])")
    }
}

pub(crate) struct GeneratedToken {
    pub token: String,
    pub fingerprint: TokenFingerprint,
}

/// Generate 256 bits from the OS CSPRNG and return a 64-character hex token.
pub(crate) fn generate_token() -> Result<GeneratedToken, TokenGenerationError> {
    let mut random = [0_u8; TOKEN_BYTES];
    getrandom::getrandom(&mut random).map_err(|_| TokenGenerationError)?;

    const HEX: &[u8; 16] = b"0123456789abcdef";
    let mut token = String::with_capacity(TOKEN_HEX_BYTES);
    for byte in random.iter().copied() {
        token.push(HEX[(byte >> 4) as usize] as char);
        token.push(HEX[(byte & 0x0f) as usize] as char);
    }
    random.zeroize();

    let fingerprint = fingerprint_token(&token).ok_or(TokenGenerationError)?;
    Ok(GeneratedToken { token, fingerprint })
}

pub(crate) fn fingerprint_token(token: &str) -> Option<TokenFingerprint> {
    if token.len() != TOKEN_HEX_BYTES || !token.bytes().all(|byte| byte.is_ascii_hexdigit()) {
        return None;
    }

    let digest = Sha256::digest(token.as_bytes());
    let mut bytes = [0_u8; 32];
    bytes.copy_from_slice(&digest);
    Some(TokenFingerprint(bytes))
}

#[cfg(test)]
mod tests {
    use super::{fingerprint_token, generate_token};

    #[test]
    fn generated_token_has_expected_shape_and_unique_samples() {
        let first = generate_token().expect("OS randomness available");
        let second = generate_token().expect("OS randomness available");
        assert_eq!(first.token.len(), 64);
        assert!(first
            .token
            .bytes()
            .all(|byte| byte.is_ascii_hexdigit() && !byte.is_ascii_uppercase()));
        assert_ne!(first.token, second.token);
        assert!(fingerprint_token(&first.token).is_some());
    }

    #[test]
    fn rejects_wrong_length_or_non_hex_tokens_before_hashing() {
        assert!(fingerprint_token("short").is_none());
        assert!(fingerprint_token(&"z".repeat(64)).is_none());
    }

    #[test]
    fn fingerprint_is_deterministic_but_debug_redacted() {
        let one = fingerprint_token(&"a".repeat(64)).unwrap();
        let two = fingerprint_token(&"a".repeat(64)).unwrap();
        assert_eq!(one, two);
        assert!(!format!("{one:?}").contains(&"a".repeat(64)));
    }
}
