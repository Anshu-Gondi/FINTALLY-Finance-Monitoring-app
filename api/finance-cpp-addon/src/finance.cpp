#include <napi.h>
#include <cmath>
#include <cstdint>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

namespace finance {

constexpr int64_t PARALLEL_THRESHOLD = 10000;

/* =============================== */

inline bool ValidateSameLength(size_t a, size_t b, Napi::Env env) {
    if (a != b) {
        Napi::RangeError::New(env, "TypedArray length mismatch")
            .ThrowAsJavaScriptException();
        return false;
    }
    return true;
}

inline bool ShouldParallelize(size_t n) {
#ifdef _OPENMP
    return n >= PARALLEL_THRESHOLD;
#else
    return false;
#endif
}

/* ===============================
   SAFE MULTIPLY + ADD (cross-platform)
=============================== */

inline int64_t SafeMulAdd(int64_t a, int64_t b, int64_t c) {
#ifdef _WIN32
    // Windows: emulate 128-bit with double
    double temp = static_cast<double>(a) * static_cast<double>(b) + static_cast<double>(c);
    if (temp > static_cast<double>(std::numeric_limits<int64_t>::max()))
        return std::numeric_limits<int64_t>::max();
    return static_cast<int64_t>(temp);
#else
    // Linux / GCC / Clang: use __int128 for precision
    __int128 temp = static_cast<__int128>(a) * static_cast<__int128>(b) + static_cast<__int128>(c);
    if (temp > std::numeric_limits<int64_t>::max())
        return std::numeric_limits<int64_t>::max();
    return static_cast<int64_t>(temp);
#endif
}

/* ===============================
   COMPOUND INTEREST (Optimized)
=============================== */

Napi::Value CalculateCompoundInterestBatch(const Napi::CallbackInfo& info) {

    Napi::Env env = info.Env();
    if (info.Length() != 4)
        return Napi::TypeError::New(env, "Expected 4 TypedArrays").Value();

    auto principals = info[0].As<Napi::BigInt64Array>();
    auto rates      = info[1].As<Napi::Float64Array>();
    auto years      = info[2].As<Napi::Int32Array>();
    auto compounds  = info[3].As<Napi::Int32Array>();

    size_t n = principals.ElementLength();

    if (!ValidateSameLength(n, rates.ElementLength(), env) ||
        !ValidateSameLength(n, years.ElementLength(), env) ||
        !ValidateSameLength(n, compounds.ElementLength(), env))
        return env.Null();

    Napi::BigInt64Array result = Napi::BigInt64Array::New(env, n);

    auto* __restrict P = principals.Data();
    auto* __restrict R = rates.Data();
    auto* __restrict Y = years.Data();
    auto* __restrict C = compounds.Data();
    auto* __restrict out = result.Data();

    if (ShouldParallelize(n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int64_t i = 0; i < (int64_t)n; i++) {

            bool valid = (P[i] > 0) & (R[i] >= 0) &
                         (Y[i] > 0) & (C[i] > 0);

            if (!valid) {
                out[i] = 0;
                continue;
            }

            double rate = R[i] * 0.01;
            double x = rate / C[i];

            double expArg = (double)(C[i] * Y[i]) * std::log1p(x);
            double amount = P[i] * std::exp(expArg);

            out[i] = (int64_t)std::llround(amount);
        }
    } else {
        for (int64_t i = 0; i < (int64_t)n; i++) {

            if (P[i] <= 0 || R[i] < 0 || Y[i] <= 0 || C[i] <= 0) {
                out[i] = 0;
                continue;
            }

            double rate = R[i] * 0.01;
            double x = rate / C[i];
            double expArg = (double)(C[i] * Y[i]) * std::log1p(x);
            double amount = P[i] * std::exp(expArg);

            out[i] = (int64_t)std::llround(amount);
        }
    }

    return result;
}

/* ===============================
   EMI (Optimized)
=============================== */

Napi::Value CalculateEMIBatch(const Napi::CallbackInfo& info) {

    Napi::Env env = info.Env();
    if (info.Length() != 3)
        return Napi::TypeError::New(env, "Expected 3 TypedArrays").Value();

    auto principals = info[0].As<Napi::BigInt64Array>();
    auto rates      = info[1].As<Napi::Float64Array>();
    auto months     = info[2].As<Napi::Int32Array>();

    size_t n = principals.ElementLength();

    if (!ValidateSameLength(n, rates.ElementLength(), env) ||
        !ValidateSameLength(n, months.ElementLength(), env))
        return env.Null();

    Napi::BigInt64Array result = Napi::BigInt64Array::New(env, n);

    auto* __restrict P = principals.Data();
    auto* __restrict R = rates.Data();
    auto* __restrict M = months.Data();
    auto* __restrict out = result.Data();

    if (ShouldParallelize(n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int64_t i = 0; i < (int64_t)n; i++) {

            if (P[i] <= 0 || M[i] <= 0) {
                out[i] = 0;
                continue;
            }

            double r = (R[i] * 0.01) / 12.0;

            if (r == 0.0) {
                out[i] = P[i] / M[i];
                continue;
            }

            double expArg = (double)M[i] * std::log1p(r);
            double p = std::exp(expArg);

            double emi = (P[i] * r * p) / (p - 1.0);
            out[i] = (int64_t)std::llround(emi);
        }
    } else {
        for (int64_t i = 0; i < (int64_t)n; i++) {

            if (P[i] <= 0 || M[i] <= 0) {
                out[i] = 0;
                continue;
            }

            double r = (R[i] * 0.01) / 12.0;

            if (r == 0.0) {
                out[i] = P[i] / M[i];
                continue;
            }

            double expArg = (double)M[i] * std::log1p(r);
            double p = std::exp(expArg);

            double emi = (P[i] * r * p) / (p - 1.0);
            out[i] = (int64_t)std::llround(emi);
        }
    }

    return result;
}

/* ===============================
   SIP (Optimized)
=============================== */

Napi::Value CalculateSIPBatch(const Napi::CallbackInfo& info) {

    Napi::Env env = info.Env();
    if (info.Length() != 4)
        return Napi::TypeError::New(env, "Expected 4 TypedArrays").Value();

    auto monthly   = info[0].As<Napi::BigInt64Array>();
    auto rates     = info[1].As<Napi::Float64Array>();
    auto years     = info[2].As<Napi::Int32Array>();
    auto compounds = info[3].As<Napi::Int32Array>();

    size_t n = monthly.ElementLength();

    if (!ValidateSameLength(n, rates.ElementLength(), env) ||
        !ValidateSameLength(n, years.ElementLength(), env) ||
        !ValidateSameLength(n, compounds.ElementLength(), env))
        return env.Null();

    Napi::BigInt64Array result = Napi::BigInt64Array::New(env, n);

    auto* __restrict M = monthly.Data();
    auto* __restrict R = rates.Data();
    auto* __restrict Y = years.Data();
    auto* __restrict C = compounds.Data();
    auto* __restrict out = result.Data();

    if (ShouldParallelize(n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int64_t i = 0; i < (int64_t)n; i++) {

            if (M[i] <= 0 || Y[i] <= 0 || C[i] <= 0) {
                out[i] = 0;
                continue;
            }

            int totalMonths = Y[i] * 12;
            double rate = (R[i] * 0.01) / C[i];

            if (rate == 0.0) {
                out[i] = M[i] * totalMonths;
                continue;
            }

            double expArg = (double)totalMonths * std::log1p(rate);
            double growth = std::exp(expArg);

            double fv = M[i] *
                        (growth - 1.0) / rate *
                        (1.0 + rate);

            out[i] = (int64_t)std::llround(fv);
        }
    } else {
        for (int64_t i = 0; i < (int64_t)n; i++) {

            if (M[i] <= 0 || Y[i] <= 0 || C[i] <= 0) {
                out[i] = 0;
                continue;
            }

            int totalMonths = Y[i] * 12;
            double rate = (R[i] * 0.01) / C[i];

            if (rate == 0.0) {
                out[i] = M[i] * totalMonths;
                continue;
            }

            double expArg = (double)totalMonths * std::log1p(rate);
            double growth = std::exp(expArg);

            double fv = M[i] *
                        (growth - 1.0) / rate *
                        (1.0 + rate);

            out[i] = (int64_t)std::llround(fv);
        }
    }

    return result;
}

/* ===============================
   BUDGET PROJECTION (Optimized)
=============================== */

Napi::Value CalculateBudgetProjectionBatch(const Napi::CallbackInfo& info) {

    Napi::Env env = info.Env();
    if (info.Length() != 4)
        return Napi::TypeError::New(env, "Expected 4 TypedArrays").Value();

    auto principals = info[0].As<Napi::BigInt64Array>();
    auto budgets    = info[1].As<Napi::BigInt64Array>();
    auto spent      = info[2].As<Napi::BigInt64Array>();
    auto months     = info[3].As<Napi::Int32Array>();

    size_t n = principals.ElementLength();

    if (!ValidateSameLength(n, budgets.ElementLength(), env) ||
        !ValidateSameLength(n, spent.ElementLength(), env) ||
        !ValidateSameLength(n, months.ElementLength(), env))
        return env.Null();

    auto* __restrict P = principals.Data();
    auto* __restrict B = budgets.Data();
    auto* __restrict S = spent.Data();
    auto* __restrict M = months.Data();

    Napi::BigInt64Array projected = Napi::BigInt64Array::New(env, n);
    Napi::Float64Array usage = Napi::Float64Array::New(env, n);
    Napi::Uint8Array flag = Napi::Uint8Array::New(env, n);

    auto* __restrict ps = projected.Data();
    auto* __restrict up = usage.Data();
    auto* __restrict wf = flag.Data();

    if (ShouldParallelize(n)) {
#ifdef _OPENMP
#pragma omp parallel for schedule(static)
#endif
        for (int64_t i = 0; i < (int64_t)n; i++) {

            ps[i] = SafeMulAdd(P[i], M[i], S[i]);

            double usageLocal =
                (B[i] > 0)
                    ? (double)ps[i] / (double)B[i] * 100.0
                    : 100.0;

            up[i] = usageLocal;

            wf[i] = (usageLocal >= 100.0) * 2 +
                    (usageLocal >= 80.0 && usageLocal < 100.0);
        }
    } else {
        for (int64_t i = 0; i < (int64_t)n; i++) {

            ps[i] = SafeMulAdd(P[i], M[i], S[i]);

            double usageLocal =
                (B[i] > 0)
                    ? (double)ps[i] / (double)B[i] * 100.0
                    : 100.0;

            up[i] = usageLocal;

            wf[i] = (usageLocal >= 100.0) * 2 +
                    (usageLocal >= 80.0 && usageLocal < 100.0);
        }
    }

    Napi::Object result = Napi::Object::New(env);
    result.Set("projectedSpent", projected);
    result.Set("usagePercent", usage);
    result.Set("warningFlag", flag);

    return result;
}

/* ===============================
   MODULE EXPORT
=============================== */

Napi::Object Init(Napi::Env env, Napi::Object exports) {

#ifdef _OPENMP
    omp_set_num_threads(2);   // match your 2-core CPU
#endif

    exports.Set("compoundInterestBatch",
                Napi::Function::New(env, CalculateCompoundInterestBatch));
    exports.Set("emiBatch",
                Napi::Function::New(env, CalculateEMIBatch));
    exports.Set("sipBatch",
                Napi::Function::New(env, CalculateSIPBatch));
    exports.Set("budgetProjectionBatch",
                Napi::Function::New(env, CalculateBudgetProjectionBatch));

    return exports;
}

NODE_API_MODULE(finance, Init)

} // namespace finance
