use std::fmt;
use std::error::Error;
use crate::core::types::*;


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
