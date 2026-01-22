// src/core/finance/loans.rs

use crate::core::finance::emi::{calculate_emi, is_emi_affordable};
use crate::core::types::*;
use crate::core::config::LoanPolicy;

pub fn assess_loan(
    monthly_income: f64,
    request: &LoanRequest,
    policy: &LoanPolicy,
) -> LoanAssessment {
    if monthly_income <= 0.0 {
        return LoanAssessment {
            emi: 0.0,
            approved: false,
            reason: Some("Invalid monthly income".into()),
        };
    }

    match request.purpose {
        LoanPurpose::Business if !policy.allow_business_loans => {
            return LoanAssessment {
                emi: 0.0,
                approved: false,
                reason: Some("Business loans not allowed for this profile".into()),
            };
        }
        LoanPurpose::Personal if !policy.allow_personal_loans => {
            return LoanAssessment {
                emi: 0.0,
                approved: false,
                reason: Some("Personal loans not allowed for this profile".into()),
            };
        }
        _ => {}
    }

    let emi = calculate_emi(
        request.principal,
        request.annual_rate,
        request.tenure_months,
    );

    if emi <= 0.0 {
        return LoanAssessment {
            emi,
            approved: false,
            reason: Some("Invalid loan parameters".into()),
        };
    }

    if !is_emi_affordable(emi, monthly_income, &policy.emi_policy) {
        return LoanAssessment {
            emi,
            approved: false,
            reason: Some("EMI not affordable under current policy".into()),
        };
    }

    LoanAssessment {
        emi,
        approved: true,
        reason: None,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::config::LoanPolicy;

    #[test]
    fn salaried_personal_loan_approved() {
        let policy = LoanPolicy::salaried();

        let request = LoanRequest {
            principal: 500_000.0,
            annual_rate: 10.0,
            tenure_months: 60,
            purpose: LoanPurpose::Personal,
        };

        let result = assess_loan(80_000.0, &request, &policy);
        assert!(result.approved);
    }

    #[test]
    fn salaried_business_loan_rejected() {
        let policy = LoanPolicy::salaried();

        let request = LoanRequest {
            principal: 300_000.0,
            annual_rate: 12.0,
            tenure_months: 36,
            purpose: LoanPurpose::Business,
        };

        let result = assess_loan(80_000.0, &request, &policy);
        assert!(!result.approved);
    }

    #[test]
    fn self_employed_business_allowed() {
        let policy = LoanPolicy::self_employed();

        let request = LoanRequest {
            principal: 400_000.0,
            annual_rate: 11.0,
            tenure_months: 48,
            purpose: LoanPurpose::Business,
        };

        let result = assess_loan(70_000.0, &request, &policy);
        assert!(result.approved);
    }

    #[test]
    fn low_income_rejected_due_to_affordability() {
        let policy = LoanPolicy::low_income();

        let request = LoanRequest {
            principal: 300_000.0,
            annual_rate: 12.0,
            tenure_months: 36,
            purpose: LoanPurpose::Personal,
        };

        let result = assess_loan(30_000.0, &request, &policy);
        assert!(!result.approved);
    }

    #[test]
    fn joint_borrower_higher_capacity() {
        let policy = LoanPolicy::joint_borrowers();

        let request = LoanRequest {
            principal: 1_200_000.0,
            annual_rate: 9.5,
            tenure_months: 240,
            purpose: LoanPurpose::Home,
        };

        let result = assess_loan(150_000.0, &request, &policy);
        assert!(result.approved);
    }
}
