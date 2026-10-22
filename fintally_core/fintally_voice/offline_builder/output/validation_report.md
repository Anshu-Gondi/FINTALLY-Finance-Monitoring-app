# FinTally Voice — Codebook Validation

- Schema version: 1
- Feature dimension: 32
- K-means seed: 252758545
- Feature and centroid storage: IEEE-754 FP32
- Validation distance arithmetic: FP64 accumulation over FP32 values
- Languages evaluated: 3

## Results

| Language | Backend | Train frames | Validation frames | Clusters | Occupied (validation) | Empty (validation) | Converged | Iterations | Training inertia | Validation MSE | Validation RMSE/dimension | Max validation squared distance |
|---|---|---:|---:|---:|---:|---:|:---:|---:|---:|---:|---:|---:|
| en | cuda-nearest-assignment+rayon-reduction | 4628995 | 900152 | 128 | 128 | 0 | true | 272 | 4.44885625e7 | 9.57178114e0 | 5.46916960e-1 | 4.65655057e3 |
| fr | cuda-nearest-assignment+rayon-reduction | 740395 | 90550 | 128 | 128 | 0 | true | 201 | 6.87444744e6 | 8.51290983e0 | 5.15779442e-1 | 4.17436740e2 |
| hi | cuda-nearest-assignment+rayon-reduction | 2112345 | 900694 | 128 | 127 | 1 | true | 195 | 1.52354705e7 | 8.79362402e0 | 5.24214413e-1 | 1.11692422e3 |

Note: training inertia is measured on the training split. Validation MSE is the mean nearest-centroid squared distance per validation frame. RMSE/dimension is the square root of MSE divided by feature dimensionality.
Note: an empty validation cluster means no held-out validation frame selected that centroid; it does not, by itself, prove the centroid is dead on the training distribution.
