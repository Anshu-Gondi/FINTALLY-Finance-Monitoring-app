import time
import random
import fintally_finance


# ----------------------------
# PURE PYTHON IMPLEMENTATION
# ----------------------------

def compound_interest_py(principals, rates, years, compounds):
    result = []
    for p, r, y, c in zip(principals, rates, years, compounds):
        if p <= 0 or r < 0 or y <= 0 or c <= 0:
            result.append(0)
            continue

        rate = r * 0.01
        x = rate / c
        exp_arg = (c * y) * (1 + x)**0 - 1  # naive version

        amount = p * ((1 + x) ** (c * y))
        result.append(round(amount))

    return result


# ----------------------------
# DATA GENERATION
# ----------------------------

def generate(n):
    principals = [random.randint(1000, 100000) for _ in range(n)]
    rates = [random.uniform(1, 15) for _ in range(n)]
    years = [random.randint(1, 10) for _ in range(n)]
    compounds = [12] * n
    return principals, rates, years, compounds


# ----------------------------
# BENCHMARK
# ----------------------------

def benchmark(n):
    print(f"\n=== Testing with n = {n} ===")

    p, r, y, c = generate(n)

    # Python
    start = time.perf_counter()
    compound_interest_py(p, r, y, c)
    py_time = time.perf_counter() - start

    # Rust
    start = time.perf_counter()
    fintally_finance.compound_interest_batch(p, r, y, c)
    rust_time = time.perf_counter() - start

    print(f"Python: {py_time:.4f}s")
    print(f"Rust:   {rust_time:.4f}s")
    print(f"Speedup: {py_time / rust_time:.2f}x")


# ----------------------------
# RUN TESTS
# ----------------------------

if __name__ == "__main__":
    for n in [100, 10_000, 100_000]:
        benchmark(n)