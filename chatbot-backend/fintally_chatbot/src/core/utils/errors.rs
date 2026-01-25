use std::fmt;
use std::error::Error;
use crate::core::utils::domain_error::{DomainError, EmiError};

/// Core error type for the application
#[derive(Debug)]
pub enum AppError {
    InvalidInput(String),
    Domain(DomainError),
    CalculationError(String),
    ProfileNotFound(String),
    AllocationError(String),
    ExternalServiceError(String), // placeholder if you ever integrate APIs
    Other(String),
}

impl fmt::Display for AppError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            AppError::InvalidInput(msg) => write!(f, "Invalid input: {}", msg),
            AppError::Domain(domain_error) => write!(f, "Domain error: {}", domain_error),
            AppError::CalculationError(msg) => write!(f, "Calculation error: {}", msg),
            AppError::ProfileNotFound(msg) => write!(f, "Profile not found: {}", msg),
            AppError::AllocationError(msg) => write!(f, "Allocation error: {}", msg),
            AppError::ExternalServiceError(msg) => write!(f, "External service error: {}", msg),
            AppError::Other(msg) => write!(f, "Other error: {}", msg),
        }
    }
}

impl From<DomainError> for AppError {
    fn from(err: DomainError) -> Self {
        AppError::Domain(err)
    }
}
impl From<crate::core::utils::domain_error::EmiError> for AppError {
    fn from(err: crate::core::utils::domain_error::EmiError) -> Self {
        AppError::Domain(DomainError::Emi(err))
    }
}


impl Error for AppError {}

/// Helper macros for quickly creating errors
#[macro_export]
macro_rules! invalid_input {
    ($msg:expr) => {
        $crate::core::utils::errors::AppError::InvalidInput($msg.to_string())
    };
}

#[macro_export]
macro_rules! calc_error {
    ($msg:expr) => {
        $crate::core::utils::errors::AppError::CalculationError($msg.to_string())
    };
}

#[macro_export]
macro_rules! allocation_error {
    ($msg:expr) => {
        $crate::core::utils::errors::AppError::AllocationError($msg.to_string())
    };
}

#[macro_export]
macro_rules! emi_error {
    ($err:expr) => {
        $crate::core::utils::errors::AppError::Domain(DomainError::Emi($err))
    };
}
