#include <napi.h>
#include <memory>
#include <vector>
#include <cmath>
#include <stdexcept>

namespace Finance {

// RAII wrapper for temporary buffers (auto cleanup)
class ComputationBuffer {
public:
    explicit ComputationBuffer(size_t size) : data_(std::make_unique<double[]>(size)) {}
    double* data() { return data_.get(); }
private:
    std::unique_ptr<double[]> data_;
};

// Compound Interest: A = P(1 + r/n)^(nt)
Napi::Value CalculateCompoundInterest(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4 || !info[0].IsNumber() || !info[1].IsNumber() || !info[2].IsNumber() || !info[3].IsNumber()) {
        Napi::TypeError::New(env, "Expected 4 numbers: principal, rate, years, compounding_per_year").ThrowAsJavaScriptException();
        return env.Null();
    }

    double principal = info[0].As<Napi::Number>().DoubleValue();
    double rate = info[1].As<Napi::Number>().DoubleValue() / 100.0;  // % to decimal
    int years = info[2].As<Napi::Number>().Int32Value();
    int n = info[3].As<Napi::Number>().Int32Value();

    if (principal <= 0 || rate < 0 || years <= 0 || n <= 0) {
        Napi::RangeError::New(env, "Invalid financial inputs").ThrowAsJavaScriptException();
        return env.Null();
    }

    double amount = principal * std::pow(1 + rate/n, n * years);
    return Napi::Number::New(env, amount);
}

// EMI Calculation
Napi::Value CalculateEMI(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 3) {
        Napi::TypeError::New(env, "Expected 3 numbers: principal, annual_rate, months").ThrowAsJavaScriptException();
        return env.Null();
    }

    double P = info[0].As<Napi::Number>().DoubleValue();
    double annual_r = info[1].As<Napi::Number>().DoubleValue() / 100.0 / 12.0;  // monthly rate
    int months = info[2].As<Napi::Number>().Int32Value();

    if (months == 0) return Napi::Number::New(env, 0.0);

    double emi = P * annual_r * std::pow(1 + annual_r, months) /
                 (std::pow(1 + annual_r, months) - 1);
    return Napi::Number::New(env, emi);
}

// Future SIP Value
Napi::Value CalculateSIP(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (info.Length() < 4) {
        Napi::TypeError::New(env, "Expected 4 numbers: monthly, rate, years, compounding_per_year").ThrowAsJavaScriptException();
        return env.Null();
    }

    double M = info[0].As<Napi::Number>().DoubleValue();
    double r = info[1].As<Napi::Number>().DoubleValue() / 100.0;
    int years = info[2].As<Napi::Number>().Int32Value();
    int n = info[3].As<Napi::Number>().Int32Value();

    int total_months = years * 12;
    double monthly_rate = r / n;
    double future_value = M * (std::pow(1 + monthly_rate, total_months) - 1) / monthly_rate * (1 + monthly_rate);
    return Napi::Number::New(env, future_value);
}

// Add more: tax calculation, ROI, etc.

Napi::Object Init(Napi::Env env, Napi::Object exports) {
    exports.Set("calculateCompoundInterest", Napi::Function::New(env, CalculateCompoundInterest));
    exports.Set("calculateEMI", Napi::Function::New(env, CalculateEMI));
    exports.Set("calculateSIP", Napi::Function::New(env, CalculateSIP));
    return exports;
}

NODE_API_MODULE(finance, Init)

}  // namespace Finance