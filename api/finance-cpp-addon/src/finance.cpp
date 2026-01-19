#include <napi.h>
#include <cmath>
#include <cstdint>

namespace finance
{

    /* ===============================
       HELPERS
       =============================== */

    inline bool ValidateSameLength(size_t a, size_t b, Napi::Env env)
    {
        if (a != b)
        {
            Napi::RangeError::New(env, "TypedArray length mismatch")
                .ThrowAsJavaScriptException();
            return false;
        }
        return true;
    }

    /* ===============================
       COMPOUND INTEREST (BATCH)
       =============================== */

    Napi::Value CalculateCompoundInterestBatch(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() != 4)
        {
            Napi::TypeError::New(env, "Expected 4 TypedArrays")
                .ThrowAsJavaScriptException();
            return env.Null();
        }

        auto principals = info[0].As<Napi::BigInt64Array>(); // paise
        auto rates = info[1].As<Napi::Float64Array>();       // %
        auto years = info[2].As<Napi::Int32Array>();
        auto compounds = info[3].As<Napi::Int32Array>();

        size_t n = principals.ElementLength();

        if (!ValidateSameLength(n, rates.ElementLength(), env) ||
            !ValidateSameLength(n, years.ElementLength(), env) ||
            !ValidateSameLength(n, compounds.ElementLength(), env))
        {
            return env.Null();
        }

        Napi::BigInt64Array result = Napi::BigInt64Array::New(env, n);

        auto *P = principals.Data();
        auto *R = rates.Data();
        auto *Y = years.Data();
        auto *C = compounds.Data();
        auto *out = result.Data();

        for (size_t i = 0; i < n; i++)
        {
            if (P[i] <= 0 || R[i] < 0 || Y[i] <= 0 || C[i] <= 0)
            {
                out[i] = 0;
                continue;
            }

            double rate = R[i] / 100.0;
            double amount =
                static_cast<double>(P[i]) *
                std::pow(1.0 + rate / C[i], C[i] * Y[i]);

            out[i] = static_cast<int64_t>(std::llround(amount));
        }

        return result;
    }

    /* ===============================
       EMI (BATCH)
       =============================== */

    Napi::Value CalculateEMIBatch(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() != 3)
        {
            Napi::TypeError::New(env, "Expected 3 TypedArrays")
                .ThrowAsJavaScriptException();
            return env.Null();
        }

        auto principals = info[0].As<Napi::BigInt64Array>();
        auto rates = info[1].As<Napi::Float64Array>();
        auto months = info[2].As<Napi::Int32Array>();

        size_t n = principals.ElementLength();

        if (!ValidateSameLength(n, rates.ElementLength(), env) ||
            !ValidateSameLength(n, months.ElementLength(), env))
        {
            return env.Null();
        }

        Napi::BigInt64Array result = Napi::BigInt64Array::New(env, n);

        auto *P = principals.Data();
        auto *R = rates.Data();
        auto *M = months.Data();
        auto *out = result.Data();

        for (size_t i = 0; i < n; i++)
        {
            if (P[i] <= 0 || M[i] <= 0)
            {
                out[i] = 0;
                continue;
            }

            double r = (R[i] / 100.0) / 12.0;

            if (r == 0.0)
            {
                out[i] = P[i] / M[i];
                continue;
            }

            double p = std::pow(1.0 + r, M[i]);
            double emi = (P[i] * r * p) / (p - 1.0);

            out[i] = static_cast<int64_t>(std::llround(emi));
        }

        return result;
    }

    /* ===============================
       SIP (BATCH)
       =============================== */

    Napi::Value CalculateSIPBatch(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() != 4)
        {
            Napi::TypeError::New(env, "Expected 4 TypedArrays")
                .ThrowAsJavaScriptException();
            return env.Null();
        }

        auto monthly = info[0].As<Napi::BigInt64Array>();
        auto rates = info[1].As<Napi::Float64Array>();
        auto years = info[2].As<Napi::Int32Array>();
        auto compounds = info[3].As<Napi::Int32Array>();

        size_t n = monthly.ElementLength();

        if (!ValidateSameLength(n, rates.ElementLength(), env) ||
            !ValidateSameLength(n, years.ElementLength(), env) ||
            !ValidateSameLength(n, compounds.ElementLength(), env))
        {
            return env.Null();
        }

        Napi::BigInt64Array result = Napi::BigInt64Array::New(env, n);

        auto *M = monthly.Data();
        auto *R = rates.Data();
        auto *Y = years.Data();
        auto *C = compounds.Data();
        auto *out = result.Data();

        for (size_t i = 0; i < n; i++)
        {
            if (M[i] <= 0 || Y[i] <= 0 || C[i] <= 0)
            {
                out[i] = 0;
                continue;
            }

            int totalMonths = Y[i] * 12;
            double rate = (R[i] / 100.0) / C[i];

            if (rate == 0.0)
            {
                out[i] = M[i] * totalMonths;
                continue;
            }

            double fv =
                M[i] *
                (std::pow(1.0 + rate, totalMonths) - 1.0) /
                rate *
                (1.0 + rate);

            out[i] = static_cast<int64_t>(std::llround(fv));
        }

        return result;
    }

    // Inputs (all TypedArrays of same length):
    // principals: BigInt64Array (EMI amount in paise)
    // budgets: BigInt64Array (budget limit in paise)
    // spentSoFar: BigInt64Array (spent already in paise)
    // months: Int32Array (EMI months)

    // Outputs:
    // projectedSpent: BigInt64Array
    // usagePercent: Float64Array
    // warningFlag: Uint8Array (0=SAFE,1=NEAR_LIMIT,2=EXCEEDED)
    Napi::Value CalculateBudgetProjectionBatch(const Napi::CallbackInfo &info)
    {
        Napi::Env env = info.Env();

        if (info.Length() != 4)
        {
            Napi::TypeError::New(env, "Expected 4 TypedArrays")
                .ThrowAsJavaScriptException();
            return env.Null();
        }

        auto principals = info[0].As<Napi::BigInt64Array>();
        auto budgets = info[1].As<Napi::BigInt64Array>();
        auto spentSoFar = info[2].As<Napi::BigInt64Array>();
        auto months = info[3].As<Napi::Int32Array>();

        size_t n = principals.ElementLength();
        if (!ValidateSameLength(n, budgets.ElementLength(), env) ||
            !ValidateSameLength(n, spentSoFar.ElementLength(), env) ||
            !ValidateSameLength(n, months.ElementLength(), env))
        {
            return env.Null();
        }

        auto *P = principals.Data();
        auto *B = budgets.Data();
        auto *S = spentSoFar.Data();
        auto *M = months.Data();

        Napi::BigInt64Array projectedSpent = Napi::BigInt64Array::New(env, n);
        Napi::Float64Array usagePercent = Napi::Float64Array::New(env, n);
        Napi::Uint8Array warningFlag = Napi::Uint8Array::New(env, n);

        auto *ps = projectedSpent.Data();
        auto *up = usagePercent.Data();
        auto *wf = warningFlag.Data();

        for (size_t i = 0; i < n; i++)
        {
            ps[i] = P[i] * M[i] + S[i]; // simple projected spent
            up[i] = (B[i] > 0) ? (double(ps[i]) / double(B[i]) * 100.0) : 100.0;
            wf[i] = (up[i] >= 100.0) ? 2 : (up[i] >= 80.0 ? 1 : 0);
        }

        Napi::Object result = Napi::Object::New(env);
        result.Set("projectedSpent", projectedSpent);
        result.Set("usagePercent", usagePercent);
        result.Set("warningFlag", warningFlag);

        return result;
    }

    /* ===============================
       MODULE EXPORT
       =============================== */

    Napi::Object Init(Napi::Env env, Napi::Object exports)
    {
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
