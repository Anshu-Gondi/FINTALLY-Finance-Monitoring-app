use crate::core::types::*;
use crate::core::utils::errors::AppError;
use std::collections::HashMap;

/// Compute scores per category (0-100)
pub fn compute_stat_scores(profile: &StatProfile) -> Result<HashMap<StatCategory, f64>, AppError> {
    if profile.metrics.is_empty() {
        return Err(AppError::InvalidInput("StatProfile has no metrics".into()));
    }

    let mut category_scores: HashMap<StatCategory, f64> = HashMap::new();

    for metric in &profile.metrics {
        if metric.weight < 0.0 {
            return Err(
                AppError::InvalidInput(format!("Metric weight cannot be negative: {}", metric.name))
            );
        }

        let score = if let Some(target) = metric.target {
            if target <= 0.0 {
                return Err(
                    AppError::InvalidInput(
                        format!("Metric target must be positive: {}", metric.name)
                    )
                );
            }
            let diff = (metric.value - target).abs();
            let s = (1.0 - diff / target).max(0.0); // normalized 0-1
            s * metric.weight
        } else {
            metric.weight
        };

        let entry = category_scores.entry(metric.category.clone()).or_insert(0.0);
        *entry += score;
    }

    Ok(
        category_scores
            .into_iter()
            .map(|(cat, val)| (cat, val.min(1.0) * 100.0))
            .collect()
    )
}

/// Generate alerts based on current value vs target & trends
pub fn generate_alerts(profile: &StatProfile) -> Result<Vec<StatAlert>, AppError> {
    if profile.metrics.is_empty() {
        return Err(AppError::InvalidInput("StatProfile has no metrics".into()));
    }

    let policy = &profile.alert_policy;
    let mut alerts = Vec::new();

    for metric in &profile.metrics {
        // 1️⃣ Target-based alerts
        if let Some(target) = metric.target {
            if target > 0.0 {
                let diff_percent = ((metric.value - target) / target).abs() * 100.0;

                let level = if diff_percent >= policy.target_critical_percent {
                    AlertLevel::Critical
                } else if diff_percent >= policy.target_warning_percent {
                    AlertLevel::Warning
                } else {
                    AlertLevel::Info
                };

                if level != AlertLevel::Info {
                    alerts.push(StatAlert {
                        metric_name: metric.name.clone(),
                        category: metric.category.clone(),
                        message: format!(
                            "{} is {:.1}% away from target {:.1}",
                            metric.name,
                            diff_percent,
                            target
                        ),
                        level,
                    });
                }
            }
        }

        // 2️⃣ Trend-based alerts
        if metric.history.len() >= 2 {
            let last = metric.history[metric.history.len() - 1];
            let prev = metric.history[metric.history.len() - 2];

            let change_percent = if prev.abs() > 0.0 {
                ((last - prev) / prev.abs()).abs() * 100.0
            } else {
                100.0
            };

            if change_percent >= policy.trend_warning_percent {
                alerts.push(StatAlert {
                    metric_name: metric.name.clone(),
                    category: metric.category.clone(),
                    message: format!(
                        "{} changed by {:.1}% from last measurement",
                        metric.name,
                        change_percent
                    ),
                    level: AlertLevel::Warning,
                });
            }
        }
    }

    Ok(alerts)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compute_stat_scores_young_professional() {
        let mut profile = StatProfile::single_parent_profile();

        // Simulate metric values
        profile.metrics.iter_mut().for_each(|m| {
            match m.name.as_str() {
                "BMI" => {
                    m.value = 23.0;
                }
                "Resting Heart Rate" => {
                    m.value = 75.0;
                }
                "Sleep Hours" => {
                    m.value = 7.5;
                }
                "Net Worth" => {
                    m.value = 50_000.0;
                }
                "Emergency Fund" => {
                    m.value = 20_000.0;
                }
                "Focus Hours" => {
                    m.value = 5.5;
                }
                _ => (),
            }
        });

        let scores = compute_stat_scores(&profile).unwrap();

        assert!(scores[&StatCategory::Health] > 0.0);
        assert!(scores[&StatCategory::Finance] > 0.0);
        assert!(scores[&StatCategory::Productivity] > 0.0);
    }

    #[test]
    fn test_zero_values_no_crash() {
        let profile = StatProfile::single_parent_profile();
        let scores = compute_stat_scores(&profile).unwrap();
        assert!(scores.values().all(|&v| v >= 0.0));
    }

    #[test]
    fn test_alerts_generation() {
        let mut profile = StatProfile::single_parent_profile();

        profile.metrics.iter_mut().for_each(|m| {
            match m.name.as_str() {
                "BMI" => {
                    m.value = 30.0;
                    m.history = vec![23.0, 25.0, 30.0];
                }
                "Focus Hours" => {
                    m.value = 3.0;
                    m.history = vec![6.0, 5.0, 3.0];
                }
                "Emergency Fund" => {
                    m.value = 10_000.0;
                    m.history = vec![30_000.0, 20_000.0, 10_000.0];
                }
                _ => {}
            }
        });

        profile.metrics.push(StatMetric {
            name: "Custom Metric".into(),
            category: StatCategory::Finance,
            value: 600.0,
            target: None,
            measurement: MeasurementType::Float,
            weight: 0.1,
            history: vec![400.0, 450.0, 600.0],
        });

        let alerts = generate_alerts(&profile).unwrap();

        assert!(alerts.iter().any(|a| a.metric_name == "BMI"));
        assert!(alerts.iter().any(|a| a.metric_name == "Focus Hours"));
        assert!(alerts.iter().any(|a| a.metric_name == "Emergency Fund"));

        let custom_alerts: Vec<_> = alerts
            .iter()
            .filter(|a| a.metric_name == "Custom Metric")
            .collect();

        assert!(!custom_alerts.is_empty());
        assert!(custom_alerts.iter().any(|a| a.level == AlertLevel::Warning));
    }

    #[test]
    fn test_scores_with_history() {
        let mut profile = StatProfile::single_parent_profile();
        profile.metrics.iter_mut().for_each(|m| {
            m.value = 22.0;
        });
        let scores = compute_stat_scores(&profile).unwrap();
        assert!(scores[&StatCategory::Health] > 0.0);
    }

    #[test]
    fn test_empty_metrics_errors() {
        let profile = StatProfile {
            metrics: vec![],
            alert_policy: AlertPolicy::standard(),
        };
        assert!(compute_stat_scores(&profile).is_err());
        assert!(generate_alerts(&profile).is_err());
    }
}
