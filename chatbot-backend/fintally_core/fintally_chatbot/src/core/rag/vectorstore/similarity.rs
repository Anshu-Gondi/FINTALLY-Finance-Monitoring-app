pub struct Similarity;

impl Similarity {
    pub fn dot_product(a: &[f32], b: &[f32]) -> f32 {
        a.iter().zip(b.iter()).map(|(x, y)| x * y).sum()
    }

    pub fn magnitude(vec: &[f32]) -> f32 {
        vec.iter().map(|x| x * x).sum::<f32>().sqrt()
    }

    pub fn cosine_similarity(a: &[f32], b: &[f32]) -> f32 {
        let mag_a = Self::magnitude(a);
        let mag_b = Self::magnitude(b);
        
        if mag_a == 0.0 || mag_b == 0.0 {
            return 0.0;
        }
        
        Self::dot_product(a, b) / (mag_a * mag_b)
    }
}