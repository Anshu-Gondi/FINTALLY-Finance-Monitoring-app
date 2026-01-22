use crate::core::types::*;
use std::collections::HashMap;

/// Compute scores per category (0-100)
pub fn compute_stat_scores(profile: &StatProfile) -> HashMap<StatCategory, f64> {
    let mut category_scores: HashMap<StatCategory, f64> = HashMap::new();

    for metric in &profile.metrics {
        let score = if let Some(target) = metric.target {
            let diff = (metric.value - target).abs();
            let s = (1.0 - diff / target).max(0.0); // normalized 0-1
            s * metric.weight
        } else {
            metric.weight
        };

        let entry = category_scores.entry(metric.category.clone()).or_insert(0.0);
        *entry += score;
    }

    category_scores
        .into_iter()
        .map(|(cat, val)| (cat, (val.min(1.0) * 100.0)))
        .collect()
}

/// Generate alerts based on current value vs target & trends
pub fn generate_alerts(profile: &StatProfile) -> Vec<StatAlert> {
    let mut alerts = Vec::new();

    for metric in &profile.metrics {
        // 1️⃣ Target-based alerts
        if let Some(target) = metric.target {
            let diff_percent = ((metric.value - target) / target).abs() * 100.0;
            let level = if diff_percent > 20.0 {
                AlertLevel::Critical
            } else if diff_percent > 10.0 {
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
                        metric.name, diff_percent, target
                    ),
                    level,
                });
            }
        }

        // 2️⃣ Trend-based alerts (if we have history)
        if metric.history.len() >= 2 {
            let last = metric.history[metric.history.len() - 1];
            let prev = metric.history[metric.history.len() - 2];
            let change_percent = ((last - prev) / prev.abs()).abs() * 100.0;

            if change_percent > 15.0 {
                alerts.push(StatAlert {
                    metric_name: metric.name.clone(),
                    category: metric.category.clone(),
                    message: format!(
                        "{} changed by {:.1}% from last measurement",
                        metric.name, change_percent
                    ),
                    level: AlertLevel::Warning,
                });
            }
        }
    }

    alerts
}

#[cfg(test)]
mod tests {
    use crate::core::types::*;
    use std::collections::HashMap;

    #[test]
    fn test_compute_stat_scores_young_professional() {
        let mut profile = StatProfile::young_professional();

        // Simulate metric values
        profile.metrics.iter_mut().for_each(|m| {
            match m.name.as_str() {
                "BMI" => m.value = 23.0,
                "Resting Heart Rate" => m.value = 75.0,
                "Sleep Hours" => m.value = 7.5,
                "Net Worth" => m.value = 50_000.0,
                "Emergency Fund" => m.value = 20_000.0,
                "Focus Hours" => m.value = 5.5,
                _ => (),
            }
        });

        let scores = compute_stat_scores(&profile);

        assert!(scores[&StatCategory::Health] > 0.0);
        assert!(scores[&StatCategory::Finance] > 0.0);
        assert!(scores[&StatCategory::Productivity] > 0.0);
    }

    #[test]
    fn test_zero_values_no_crash() {
        let profile = StatProfile::young_professional();
        let scores = compute_stat_scores(&profile);
        assert!(scores.values().all(|&v| v >= 0.0));
    }

    #[test]
    fn test_alerts_generation() {
        let mut profile = StatProfile::young_professional();

        profile.metrics.iter_mut().for_each(|m| {
            match m.name.as_str() {
                "BMI" => { m.value = 30.0; m.history = vec![23.0, 25.0, 30.0]; },
                "Sleep Hours" => { m.value = 6.0; m.history = vec![8.0, 7.5, 6.0]; },
                "Net Worth" => { m.value = 40_000.0; m.history = vec![50_000.0, 45_000.0, 40_000.0]; },
                _ => (),
            }
        });

        let alerts = generate_alerts(&profile);

        assert!(alerts.iter().any(|a| a.metric_name == "BMI" && a.level == AlertLevel::Critical));
        assert!(alerts.iter().any(|a| a.metric_name == "Sleep Hours"));
        assert!(alerts.iter().any(|a| a.metric_name == "Net Worth"));
    }

    #[test]
    fn test_scores_with_history() {
        let mut profile = StatProfile::young_professional();
        profile.metrics.iter_mut().for_each(|m| m.value = 22.0);
        let scores = compute_stat_scores(&profile);
        assert!(scores[&StatCategory::Health] > 0.0);
    }
}
