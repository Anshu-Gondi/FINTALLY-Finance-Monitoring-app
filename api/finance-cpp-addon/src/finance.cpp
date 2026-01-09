#include <napi.h>
#include <cmath>
#include <cstdint>

namespace finance {

/* ===============================
   COMPOUND INTEREST (BATCH)
   =============================== */

Napi::Value CalculateCompoundInterestBatch(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    if (info.Length() != 4)
        throw Napi::TypeError::New(env, "Expected 4 TypedArrays");

    auto principals = info[0].As<Napi::BigInt64Array>();   // paise
    auto rates = info[1].As<Napi::Float64Array>();        // %
    auto years = info[2].As<Napi::Int32Array>();
    auto compounds = info[3].As<Napi::Int32Array>();

    size_t n = principals.ElementLength();

    Napi::BigInt64Array result = Napi::BigInt64Array::New(env, n);

    int64_t* P = principals.Data();
    double* R = rates.Data();
    int32_t* Y = years.Data();
    int32_t* C = compounds.Data();
    int64_t* out = result.Data();

    for (size_t i = 0; i < n; i++) {
        if (P[i] <= 0 || R[i] < 0 || Y[i] <= 0 || C[i] <= 0) {
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

Napi::Value CalculateEMIBatch(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    auto principals = info[0].As<Napi::BigInt64Array>();
    auto rates = info[1].As<Napi::Float64Array>();
    auto months = info[2].As<Napi::Int32Array>();

    size_t n = principals.ElementLength();
    Napi::BigInt64Array outArr = Napi::BigInt64Array::New(env, n);

    int64_t* P = principals.Data();
    double* R = rates.Data();
    int32_t* M = months.Data();
    int64_t* out = outArr.Data();

    for (size_t i = 0; i < n; i++) {
        if (P[i] <= 0 || M[i] <= 0) {
            out[i] = 0;
            continue;
        }

        double r = (R[i] / 100.0) / 12.0;

        if (r == 0.0) {
            out[i] = P[i] / M[i];
            continue;
        }

        double p = std::pow(1.0 + r, M[i]);
        double emi = (P[i] * r * p) / (p - 1.0);

        out[i] = static_cast<int64_t>(std::llround(emi));
    }

    return outArr;
}

/* ===============================
   SIP (BATCH)
   =============================== */

Napi::Value CalculateSIPBatch(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();

    auto monthly = info[0].As<Napi::BigInt64Array>();   // paise
    auto rates = info[1].As<Napi::Float64Array>();     // %
    auto years = info[2].As<Napi::Int32Array>();
    auto compounds = info[3].As<Napi::Int32Array>();

    size_t n = monthly.ElementLength();
    Napi::BigInt64Array result = Napi::BigInt64Array::New(env, n);

    int64_t* M = monthly.Data();
    double* R = rates.Data();
    int32_t* Y = years.Data();
    int32_t* C = compounds.Data();
    int64_t* out = result.Data();

    for (size_t i = 0; i < n; i++) {
        int totalMonths = Y[i] * 12;
        double rate = (R[i] / 100.0) / C[i];

        if (rate == 0.0) {
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

/* ===============================
   MODULE EXPORT
   =============================== */

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("compoundInterestBatch", Napi::Function::New(env, CalculateCompoundInterestBatch));
    exports.Set("emiBatch", Napi::Function::New(env, CalculateEMIBatch));
    exports.Set("sipBatch", Napi::Function::New(env, CalculateSIPBatch));
    return exports;
}

NODE_API_MODULE(finance, Init)

} // namespace finance 