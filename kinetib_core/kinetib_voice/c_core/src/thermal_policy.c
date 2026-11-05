#include "thermal_policy.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------------- */
/* Configuration and validation                                              */
/* ------------------------------------------------------------------------- */

static FinVoiceWorkloadMode fin_voice_mode_for_state(FinVoiceThermalState state)
{
    switch (state) {
    case FIN_VOICE_THERMAL_WARM:
        return FIN_VOICE_WORKLOAD_CONSERVE;
    case FIN_VOICE_THERMAL_HOT:
        return FIN_VOICE_WORKLOAD_THROTTLED;
    case FIN_VOICE_THERMAL_CRITICAL:
        return FIN_VOICE_WORKLOAD_MINIMAL;
    case FIN_VOICE_THERMAL_COOL:
    case FIN_VOICE_THERMAL_UNKNOWN:
    default:
        return FIN_VOICE_WORKLOAD_NORMAL;
    }
}

static FinVoiceThermalState fin_voice_initial_state(
    const FinVoiceThermalPolicyConfig *config,
    float temperature_c
)
{
    if (temperature_c >= config->critical_temperature_c) {
        return FIN_VOICE_THERMAL_CRITICAL;
    }
    if (temperature_c >= config->hot_temperature_c) {
        return FIN_VOICE_THERMAL_HOT;
    }
    if (temperature_c >= config->warm_temperature_c) {
        return FIN_VOICE_THERMAL_WARM;
    }
    return FIN_VOICE_THERMAL_COOL;
}

static bool fin_voice_config_valid(const FinVoiceThermalPolicyConfig *config)
{
    if (config == NULL) {
        return false;
    }

    if (!isfinite(config->warm_temperature_c) ||
        !isfinite(config->hot_temperature_c) ||
        !isfinite(config->critical_temperature_c) ||
        !isfinite(config->hysteresis_c) ||
        !isfinite(config->cpu_utilization_conserve_percent) ||
        !isfinite(config->cpu_utilization_throttle_percent)) {
        return false;
    }

    if (config->warm_temperature_c < -40.0f ||
        config->critical_temperature_c > 150.0f ||
        config->warm_temperature_c >= config->hot_temperature_c ||
        config->hot_temperature_c >= config->critical_temperature_c ||
        config->hysteresis_c < 0.0f ||
        config->hysteresis_c >=
            (config->critical_temperature_c - config->warm_temperature_c)) {
        return false;
    }

    if (config->hot_samples_to_escalate == 0U ||
        config->cool_samples_to_recover == 0U) {
        return false;
    }

    if (config->cpu_utilization_conserve_percent < 0.0f ||
        config->cpu_utilization_throttle_percent > 100.0f ||
        config->cpu_utilization_conserve_percent >=
            config->cpu_utilization_throttle_percent) {
        return false;
    }

    return true;
}

void fin_voice_thermal_policy_default_config(
    FinVoiceThermalPolicyConfig *config
)
{
    if (config == NULL) {
        return;
    }

    config->warm_temperature_c = 65.0f;
    config->hot_temperature_c = 78.0f;
    config->critical_temperature_c = 88.0f;
    config->hysteresis_c = 3.0f;
    config->hot_samples_to_escalate = 2U;
    config->cool_samples_to_recover = 3U;
    config->cpu_utilization_conserve_percent = 85.0f;
    config->cpu_utilization_throttle_percent = 97.0f;
}

bool fin_voice_thermal_policy_init(
    FinVoiceThermalPolicy *policy,
    const FinVoiceThermalPolicyConfig *config
)
{
    FinVoiceThermalPolicyConfig defaults;

    if (policy == NULL) {
        return false;
    }

    if (config == NULL) {
        fin_voice_thermal_policy_default_config(&defaults);
        config = &defaults;
    }

    if (!fin_voice_config_valid(config)) {
        (void)memset(policy, 0, sizeof(*policy));
        return false;
    }

    (void)memset(policy, 0, sizeof(*policy));
    policy->config = *config;
    policy->state = FIN_VOICE_THERMAL_UNKNOWN;
    policy->workload_mode = FIN_VOICE_WORKLOAD_NORMAL;
    policy->initialized = true;

    return true;
}

/* ------------------------------------------------------------------------- */
/* Hysteresis state machine                                                  */
/* ------------------------------------------------------------------------- */

static void fin_voice_thermal_update_state(
    FinVoiceThermalPolicy *policy,
    float temperature_c
)
{
    const FinVoiceThermalPolicyConfig *const config = &policy->config;
    const float warm_recovery =
        config->warm_temperature_c - config->hysteresis_c;
    const float hot_recovery =
        config->hot_temperature_c - config->hysteresis_c;
    const float critical_recovery =
        config->critical_temperature_c - config->hysteresis_c;

    if (!policy->has_temperature_sample) {
        policy->state = fin_voice_initial_state(config, temperature_c);
        policy->has_temperature_sample = true;
        policy->hot_sample_count = 0U;
        policy->recovery_sample_count = 0U;
        return;
    }

    /* Critical readings are acted upon immediately; no confirmation delay. */
    if (temperature_c >= config->critical_temperature_c) {
        policy->state = FIN_VOICE_THERMAL_CRITICAL;
        policy->hot_sample_count = 0U;
        policy->recovery_sample_count = 0U;
        return;
    }

    /*
     * Recovery from CRITICAL must be checked before the ordinary HOT
     * escalation branch. Otherwise a temperature that has cooled below the
     * critical recovery point but remains above the hot threshold could never
     * leave CRITICAL.
     */
    if (policy->state == FIN_VOICE_THERMAL_CRITICAL) {
        policy->hot_sample_count = 0U;
        if (temperature_c < critical_recovery) {
            if (policy->recovery_sample_count < UINT8_MAX) {
                ++policy->recovery_sample_count;
            }
            if (policy->recovery_sample_count >=
                config->cool_samples_to_recover) {
                policy->state = FIN_VOICE_THERMAL_HOT;
                policy->recovery_sample_count = 0U;
            }
        } else {
            policy->recovery_sample_count = 0U;
        }
        return;
    }

    /* Escalate from non-critical states to HOT only after repeated samples. */
    if (temperature_c >= config->hot_temperature_c) {
        policy->recovery_sample_count = 0U;
        if (policy->hot_sample_count < UINT8_MAX) {
            ++policy->hot_sample_count;
        }
        if (policy->hot_sample_count >= config->hot_samples_to_escalate) {
            policy->state = FIN_VOICE_THERMAL_HOT;
        }
        return;
    }

    policy->hot_sample_count = 0U;

    /* Recovery from HOT steps down to WARM first, never directly to COOL. */
    if (policy->state == FIN_VOICE_THERMAL_HOT) {
        if (temperature_c < hot_recovery) {
            if (policy->recovery_sample_count < UINT8_MAX) {
                ++policy->recovery_sample_count;
            }
            if (policy->recovery_sample_count >=
                config->cool_samples_to_recover) {
                policy->state = FIN_VOICE_THERMAL_WARM;
                policy->recovery_sample_count = 0U;
            }
        } else {
            policy->recovery_sample_count = 0U;
        }
        return;
    }

    if (policy->state == FIN_VOICE_THERMAL_WARM) {
        if (temperature_c < warm_recovery) {
            if (policy->recovery_sample_count < UINT8_MAX) {
                ++policy->recovery_sample_count;
            }
            if (policy->recovery_sample_count >=
                config->cool_samples_to_recover) {
                policy->state = FIN_VOICE_THERMAL_COOL;
                policy->recovery_sample_count = 0U;
            }
        } else {
            policy->recovery_sample_count = 0U;
        }
        return;
    }

    policy->recovery_sample_count = 0U;
    if (temperature_c >= config->warm_temperature_c) {
        policy->state = FIN_VOICE_THERMAL_WARM;
    } else if (policy->state == FIN_VOICE_THERMAL_UNKNOWN) {
        policy->state = FIN_VOICE_THERMAL_COOL;
    }
}

/* ------------------------------------------------------------------------- */
/* Workload decision generation                                              */
/* ------------------------------------------------------------------------- */

static FinVoiceWorkloadMode fin_voice_mode_max(
    FinVoiceWorkloadMode lhs,
    FinVoiceWorkloadMode rhs
)
{
    return lhs > rhs ? lhs : rhs;
}

static FinVoiceThermalDecision fin_voice_make_decision(
    const FinVoiceThermalPolicy *policy,
    const FinVoiceThermalTelemetry *telemetry
)
{
    FinVoiceThermalDecision decision;
    FinVoiceWorkloadMode mode = fin_voice_mode_for_state(policy->state);

    (void)memset(&decision, 0, sizeof(decision));

    /* CPU pressure can lower optional work without pretending it is heat. */
    if (telemetry->cpu_utilization_valid) {
        if (telemetry->cpu_utilization_percent >=
            policy->config.cpu_utilization_throttle_percent) {
            mode = fin_voice_mode_max(mode, FIN_VOICE_WORKLOAD_THROTTLED);
        } else if (telemetry->cpu_utilization_percent >=
                   policy->config.cpu_utilization_conserve_percent) {
            mode = fin_voice_mode_max(mode, FIN_VOICE_WORKLOAD_CONSERVE);
        }
    }

    if (telemetry->memory_pressure) {
        mode = fin_voice_mode_max(mode, FIN_VOICE_WORKLOAD_CONSERVE);
    }
    if (telemetry->platform_throttling) {
        mode = fin_voice_mode_max(mode, FIN_VOICE_WORKLOAD_THROTTLED);
    }

    decision.thermal_state = policy->state;
    decision.workload_mode = mode;
    decision.process_every_audio_frame = true;
    decision.temperature_available = telemetry->temperature_valid;
    decision.platform_reports_throttling = telemetry->platform_throttling;

    switch (mode) {
    case FIN_VOICE_WORKLOAD_NORMAL:
        decision.optional_work_stride = 1U;
        decision.max_secondary_candidates = 16U;
        decision.recommended_work_budget_percent = 100U;
        decision.run_secondary_rescoring = true;
        decision.run_optional_refinement = true;
        decision.allow_background_tasks = true;
        decision.allow_online_adaptation = true;
        decision.enable_diagnostics = true;
        break;

    case FIN_VOICE_WORKLOAD_CONSERVE:
        decision.optional_work_stride = 2U;
        decision.max_secondary_candidates = 8U;
        decision.recommended_work_budget_percent = 80U;
        decision.run_secondary_rescoring = true;
        decision.run_optional_refinement = true;
        decision.allow_background_tasks = false;
        decision.allow_online_adaptation = false;
        decision.enable_diagnostics = true;
        break;

    case FIN_VOICE_WORKLOAD_THROTTLED:
        decision.optional_work_stride = 4U;
        decision.max_secondary_candidates = 4U;
        decision.recommended_work_budget_percent = 50U;
        decision.run_secondary_rescoring = true;
        decision.run_optional_refinement = false;
        decision.allow_background_tasks = false;
        decision.allow_online_adaptation = false;
        decision.enable_diagnostics = false;
        break;

    case FIN_VOICE_WORKLOAD_MINIMAL:
    default:
        decision.optional_work_stride = 8U;
        decision.max_secondary_candidates = 0U;
        decision.recommended_work_budget_percent = 25U;
        decision.run_secondary_rescoring = false;
        decision.run_optional_refinement = false;
        decision.allow_background_tasks = false;
        decision.allow_online_adaptation = false;
        decision.enable_diagnostics = false;
        break;
    }

    return decision;
}

bool fin_voice_thermal_policy_update(
    FinVoiceThermalPolicy *policy,
    const FinVoiceThermalTelemetry *telemetry,
    FinVoiceThermalDecision *decision
)
{
    if (policy == NULL || telemetry == NULL || decision == NULL ||
        !policy->initialized) {
        return false;
    }

    if (telemetry->temperature_valid &&
        (!isfinite(telemetry->temperature_c) ||
         telemetry->temperature_c < -40.0f ||
         telemetry->temperature_c > 150.0f)) {
        return false;
    }

    if (telemetry->cpu_utilization_valid &&
        (!isfinite(telemetry->cpu_utilization_percent) ||
         telemetry->cpu_utilization_percent < 0.0f ||
         telemetry->cpu_utilization_percent > 100.0f)) {
        return false;
    }

    if (telemetry->temperature_valid) {
        fin_voice_thermal_update_state(policy, telemetry->temperature_c);
    } else {
        /* Missing sensor data is never treated as evidence of cooling. */
        policy->hot_sample_count = 0U;
        policy->recovery_sample_count = 0U;
    }

    if (telemetry->platform_throttling &&
        policy->state != FIN_VOICE_THERMAL_CRITICAL) {
        policy->state = FIN_VOICE_THERMAL_HOT;
    }

    if (telemetry->monotonic_time_ms != 0U) {
        policy->last_update_time_ms = telemetry->monotonic_time_ms;
    }

    *decision = fin_voice_make_decision(policy, telemetry);
    policy->workload_mode = decision->workload_mode;
    return true;
}

const char *fin_voice_thermal_state_name(FinVoiceThermalState state)
{
    switch (state) {
    case FIN_VOICE_THERMAL_COOL: return "cool";
    case FIN_VOICE_THERMAL_WARM: return "warm";
    case FIN_VOICE_THERMAL_HOT: return "hot";
    case FIN_VOICE_THERMAL_CRITICAL: return "critical";
    case FIN_VOICE_THERMAL_UNKNOWN:
    default: return "unknown";
    }
}

const char *fin_voice_workload_mode_name(FinVoiceWorkloadMode mode)
{
    switch (mode) {
    case FIN_VOICE_WORKLOAD_CONSERVE: return "conserve";
    case FIN_VOICE_WORKLOAD_THROTTLED: return "throttled";
    case FIN_VOICE_WORKLOAD_MINIMAL: return "minimal";
    case FIN_VOICE_WORKLOAD_NORMAL:
    default: return "normal";
    }
}
