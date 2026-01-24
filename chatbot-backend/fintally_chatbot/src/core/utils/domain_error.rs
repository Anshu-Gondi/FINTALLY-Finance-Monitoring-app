use std::error::Error;
use std::fmt;

#[derive(Debug, Clone, PartialEq)]
pub enum DomainError {
    Emi(EmiError),
    InvalidIncome { value: f64 },
    InvalidPercentage { value: f64 },
    AllocationOverflow { attempted: f64, available: f64 },
    ProfileInvariantViolated { reason: String },
}

#[derive(Debug, Clone, PartialEq)]
pub enum EmiError {
    InvalidPrincipal(f64),
    InvalidRate(f64),
    InvalidTenure(u32),
    IncomeTooLow(f64),
    EmiTooHigh { emi_percent: f64, max_allowed: f64 },
    InsufficientSurplus { surplus_percent: f64, required: f64 },
}

impl fmt::Display for EmiError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            EmiError::InvalidPrincipal(v) => write!(f, "principal must be positive, got {}", v),

            EmiError::InvalidRate(v) => write!(f, "annual rate must be positive, got {}", v),

            EmiError::InvalidTenure(v) => write!(f, "tenure must be > 0 months, got {}", v),

            EmiError::IncomeTooLow(v) => write!(f, "monthly income too low: {}", v),

            EmiError::EmiTooHigh {
                emi_percent,
                max_allowed,
            } => write!(
                f,
                "EMI {:.2}% exceeds allowed {:.2}%",
                emi_percent, max_allowed
            ),

            EmiError::InsufficientSurplus {
                surplus_percent,
                required,
            } => write!(
                f,
                "surplus {:.2}% is below required {:.2}%",
                surplus_percent, required
            ),
        }
    }
}

impl fmt::Display for DomainError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            DomainError::Emi(e) => write!(f, "EMI rule violation: {}", e),
            DomainError::InvalidIncome { value } => write!(f, "Invalid income: {}", value),

            DomainError::InvalidPercentage { value } => write!(f, "Invalid percentage: {}", value),

            DomainError::AllocationOverflow {
                attempted,
                available,
            } => write!(
                f,
                "Allocation overflow: attempted {}, available {}",
                attempted, available
            ),

            DomainError::ProfileInvariantViolated { reason } => {
                write!(f, "Profile invariant violated: {}", reason)
            }
        }
    }
}

impl Error for DomainError {}
