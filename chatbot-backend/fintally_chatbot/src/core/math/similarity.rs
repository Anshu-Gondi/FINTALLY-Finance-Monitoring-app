use crate::core::types::*;
use std::f64;

/// Euclidean Distance (lower = more similar)
pub fn euclidean_distance(a: &UserProfileVector, b: &UserProfileVector) -> f64 {
    let len = a.metrics.len().min(b.metrics.len());
    let sum_sq: f64 = a
        .metrics
        .iter()
        .zip(b.metrics.iter())
        .take(len)
        .map(|(x, y)| (x - y).powi(2))
        .sum();
    sum_sq.sqrt()
}

/// Cosine Similarity (1.0 = identical)
pub fn cosine_similarity(a: &UserProfileVector, b: &UserProfileVector) -> f64 {
    let len = a.metrics.len().min(b.metrics.len());
    let dot: f64 = a
        .metrics
        .iter()
        .zip(b.metrics.iter())
        .take(len)
        .map(|(x, y)| x * y)
        .sum();
    let norm_a = a
        .metrics
        .iter()
        .take(len)
        .map(|x| x.powi(2))
        .sum::<f64>()
        .sqrt();
    let norm_b = b
        .metrics
        .iter()
        .take(len)
        .map(|x| x.powi(2))
        .sum::<f64>()
        .sqrt();
    if norm_a == 0.0 || norm_b == 0.0 {
        0.0
    } else {
        dot / (norm_a * norm_b)
    }
}

/// Pearson Correlation Coefficient (-1 to 1)
pub fn pearson_correlation(a: &UserProfileVector, b: &UserProfileVector) -> f64 {
    let len = a.metrics.len().min(b.metrics.len());
    if len == 0 {
        return 0.0;
    }

    let mean_a: f64 = a.metrics.iter().take(len).sum::<f64>() / len as f64;
    let mean_b: f64 = b.metrics.iter().take(len).sum::<f64>() / len as f64;

    let mut numerator = 0.0;
    let mut denom_a = 0.0;
    let mut denom_b = 0.0;

    for i in 0..len {
        let da = a.metrics[i] - mean_a;
        let db = b.metrics[i] - mean_b;
        numerator += da * db;
        denom_a += da.powi(2);
        denom_b += db.powi(2);
    }

    if denom_a == 0.0 || denom_b == 0.0 {
        0.0
    } else {
        numerator / (denom_a.sqrt() * denom_b.sqrt())
    }
}

/// Compute similarity using selected metric
/// IMPORTANT: vectors must be aligned & normalized before calling
pub fn similarity(a: &UserProfileVector, b: &UserProfileVector, metric: SimilarityMetric) -> f64 {
    match metric {
        SimilarityMetric::Euclidean => euclidean_distance(a, b),
        SimilarityMetric::Cosine => cosine_similarity(a, b),
        SimilarityMetric::Pearson => pearson_correlation(a, b),
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::core::types::UserProfileVector;

    #[test]
    fn test_euclidean_distance() {
        let a = UserProfileVector {
            user_id: "A".to_string(),
            metrics: vec![1.0, 2.0, 3.0],
        };
        let b = UserProfileVector {
            user_id: "B".to_string(),
            metrics: vec![1.0, 2.0, 6.0],
        };
        let dist = euclidean_distance(&a, &b);
        assert!((dist - 3.0).abs() < 1e-6);
    }

    #[test]
    fn test_cosine_similarity() {
        let a = UserProfileVector {
            user_id: "A".to_string(),
            metrics: vec![1.0, 0.0, 0.0],
        };
        let b = UserProfileVector {
            user_id: "B".to_string(),
            metrics: vec![0.0, 1.0, 0.0],
        };
        let sim = cosine_similarity(&a, &b);
        assert!((sim - 0.0).abs() < 1e-6);
    }

    #[test]
    fn test_pearson_correlation() {
        let a = UserProfileVector {
            user_id: "A".to_string(),
            metrics: vec![1.0, 2.0, 3.0],
        };
        let b = UserProfileVector {
            user_id: "B".to_string(),
            metrics: vec![1.0, 2.0, 3.0],
        };
        let corr = pearson_correlation(&a, &b);
        assert!((corr - 1.0).abs() < 1e-6);
    }

    #[test]
    fn test_similarity_dispatcher() {
        let a = UserProfileVector {
            user_id: "A".into(),
            metrics: vec![1.0, 2.0, 3.0],
        };
        let b = UserProfileVector {
            user_id: "B".into(),
            metrics: vec![1.0, 2.0, 6.0],
        };

        let s = similarity(&a, &b, SimilarityMetric::Euclidean);
        assert!((s - 3.0).abs() < 1e-6);
    }
}
