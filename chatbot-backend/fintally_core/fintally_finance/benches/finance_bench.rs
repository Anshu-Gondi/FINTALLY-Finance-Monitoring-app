use criterion::{black_box, criterion_group, criterion_main, Criterion};
use fintally_finance::*; // your crate

fn generate_data(n: usize) -> (Vec<i64>, Vec<f64>, Vec<i32>, Vec<i32>) {
    let principals = vec![10000; n];
    let rates = vec![10.0; n];
    let years = vec![5; n];
    let compounds = vec![12; n];

    (principals, rates, years, compounds)
}

fn bench_compound_interest(c: &mut Criterion) {
    let mut group = c.benchmark_group("compound_interest");

    for &size in &[100, 10_000, 100_000] {
        let (p, r, y, comp) = generate_data(size);

        group.bench_function(format!("n={}", size), |b| {
            b.iter(|| {
                compound_interest_core(
                    black_box(&p),
                    black_box(&r),
                    black_box(&y),
                    black_box(&comp),
                )
            })
        });
    }

    group.finish();
}

criterion_group!(benches, bench_compound_interest);
criterion_main!(benches);