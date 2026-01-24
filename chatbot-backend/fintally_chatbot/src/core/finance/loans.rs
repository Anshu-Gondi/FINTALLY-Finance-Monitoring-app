// src/core/finance/loans.rs

use crate::core::finance::emi::is_emi_affordable;
use crate::core::types::*;

use crate::core::utils::errors::AppError;
use crate::core::utils::domain_error::DomainError;

pub fn assess_loan_checked(
    request: &LoanRequest,
    policy: &LoanPolicy,
) -> Result<LoanAssessment, AppError> {
    if request.monthly_income <= 0.0 {
        return Err(AppError::InvalidInput(
            "Monthly income must be positive".to_string(),
        ));
    }

    match request.purpose {
        LoanPurpose::Business if !policy.allow_business_loans => {
            return Err(AppError::Domain(
                DomainError::ProfileInvariantViolated {
                    reason: "Business loans not allowed".to_string(),
                },
            ));
        }
        LoanPurpose::Personal if !policy.allow_personal_loans => {
            return Err(AppError::Domain(
                DomainError::ProfileInvariantViolated {
                    reason: "Personal loans not allowed".to_string(),
                },
            ));
        }
        _ => {}
    }

    let available_income = request.monthly_income - request.existing_emi;
    let max_allowed_emi =
        available_income * policy.emi_policy.max_emi_percent / 100.0;

    is_emi_affordable(
        request.requested_emi,
        request.monthly_income,
        &policy.emi_policy,
    )
    .map_err(|e| AppError::from(DomainError::from(e)))?;

    let risk_score = match request.credit_score {
        750..=900 => 0.1,
        650..=749 => 0.3,
        _ => 0.6,
    };

    Ok(LoanAssessment {
        approved: true,
        max_allowed_emi,
        risk_score,
        reason: "Approved".to_string(),
    })
}

pub fn assess_loan(
    request: &LoanRequest,
    policy: &LoanPolicy,
) -> LoanAssessment {
    match assess_loan_checked(request, policy) {
        Ok(assessment) => assessment,
        Err(err) => LoanAssessment {
            approved: false,
            max_allowed_emi: 0.0,
            risk_score: 0.7,
            reason: err.to_string(),
        },
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

    #[test]
    fn custom_emi_policy_allows_edge() {
        let custom_emi = EmiPolicy::custom(60.0, 10.0, IncomeType::Variable, false);
        let custom_policy = LoanPolicy::custom(custom_emi, true, true);

        // This EMI is under 60%, so it should be approved
        let request = LoanRequest {
            monthly_income: 50_000.0,
            existing_emi: 0.0,
            requested_emi: 28_000.0, // 56% of income → allowed
            credit_score: 700,
            purpose: LoanPurpose::Personal,
            is_joint: false,
        };

        let result = assess_loan(&request, &custom_policy);
        assert!(result.approved); // ✅ now matches reality

        let request2 = LoanRequest {
            monthly_income: 50_000.0,
            existing_emi: 0.0,
            requested_emi: 31_000.0, // 62% → exceeds 60%
            credit_score: 700,
            purpose: LoanPurpose::Personal,
            is_joint: false,
        };

        let result2 = assess_loan(&request2, &custom_policy);
        assert!(!result2.approved); // ❌ exceeds max_emi_percent
    }

    #[test]
    fn custom_loan_policy_disallows_business() {
        let emi_policy = EmiPolicy::custom(40.0, 20.0, IncomeType::Salaried, false);
        let loan_policy = LoanPolicy::custom(emi_policy, false, true);

        let request = LoanRequest {
            monthly_income: 80_000.0,
            existing_emi: 0.0,
            requested_emi: 20_000.0,
            credit_score: 750,
            purpose: LoanPurpose::Business,
            is_joint: false,
        };

        let result = assess_loan(&request, &loan_policy);
        assert!(!result.approved); // Business loans disallowed
    }

    #[test]
    fn custom_loan_policy_allows_personal_with_joint() {
        let emi_policy = EmiPolicy::custom(45.0, 25.0, IncomeType::Salaried, true);
        let loan_policy = LoanPolicy::custom(emi_policy, true, true);

        let request = LoanRequest {
            monthly_income: 100_000.0,
            existing_emi: 10_000.0,
            requested_emi: 40_000.0,
            credit_score: 720,
            purpose: LoanPurpose::Personal,
            is_joint: true,
        };

        let result = assess_loan(&request, &loan_policy);
        assert!(result.approved); // Custom policy allows this
    }
}
