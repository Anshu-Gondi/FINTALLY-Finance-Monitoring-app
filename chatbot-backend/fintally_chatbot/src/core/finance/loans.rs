// src/core/finance/loans.rs

use crate::core::finance::emi::is_emi_affordable;
use crate::core::types::*;

pub fn assess_loan(
    request: &LoanRequest,
    policy: &LoanPolicy,
) -> LoanAssessment {
    // Guard: income
    if request.monthly_income <= 0.0 {
        return LoanAssessment {
            approved: false,
            max_allowed_emi: 0.0,
            risk_score: 1.0,
            reason: "Invalid monthly income".to_string(),
        };
    }

    // Loan purpose rules
    match request.purpose {
        LoanPurpose::Business if !policy.allow_business_loans => {
            return LoanAssessment {
                approved: false,
                max_allowed_emi: 0.0,
                risk_score: 0.9,
                reason: "Business loans not allowed".to_string(),
            };
        }
        LoanPurpose::Personal if !policy.allow_personal_loans => {
            return LoanAssessment {
                approved: false,
                max_allowed_emi: 0.0,
                risk_score: 0.9,
                reason: "Personal loans not allowed".to_string(),
            };
        }
        _ => {}
    }

    // Max EMI allowed by policy
    let max_allowed_emi =
        request.monthly_income * policy.emi_policy.max_emi_percent / 100.0;

    // Affordability check
    if !is_emi_affordable(
        request.requested_emi,
        request.monthly_income,
        &policy.emi_policy,
    ) {
        return LoanAssessment {
            approved: false,
            max_allowed_emi,
            risk_score: 0.7,
            reason: "Requested EMI not affordable".to_string(),
        };
    }

    // Credit risk (simple baseline)
    let risk_score = match request.credit_score {
        750..=900 => 0.1,
        650..=749 => 0.3,
        _ => 0.6,
    };

    LoanAssessment {
        approved: true,
        max_allowed_emi,
        risk_score,
        reason: "Approved".to_string(),
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn salaried_personal_loan_approved() {
        let policy = LoanPolicy::salaried();

        let request = LoanRequest {
            monthly_income: 80_000.0,
            existing_emi: 10_000.0,
            requested_emi: 20_000.0,
            credit_score: 780,
            purpose: LoanPurpose::Personal,
            is_joint: false,
        };

        let result = assess_loan(&request, &policy);
        assert!(result.approved);
    }

    #[test]
    fn business_loan_rejected_for_salaried() {
        let policy = LoanPolicy::salaried();

        let request = LoanRequest {
            monthly_income: 80_000.0,
            existing_emi: 5_000.0,
            requested_emi: 15_000.0,
            credit_score: 720,
            purpose: LoanPurpose::Business,
            is_joint: false,
        };

        let result = assess_loan(&request, &policy);
        assert!(!result.approved);
    }

    #[test]
    fn low_income_rejected_due_to_affordability() {
        let policy = LoanPolicy::low_income();

        let request = LoanRequest {
            monthly_income: 30_000.0,
            existing_emi: 5_000.0,
            requested_emi: 15_000.0,
            credit_score: 680,
            purpose: LoanPurpose::Personal,
            is_joint: false,
        };

        let result = assess_loan(&request, &policy);
        assert!(!result.approved);
    }
}
