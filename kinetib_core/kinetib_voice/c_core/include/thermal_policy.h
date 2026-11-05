#ifndef FIN_VOICE_THERMAL_POLICY_H
#define FIN_VOICE_THERMAL_POLICY_H

#include <stdbool.h>
#include <stdint.h>

#include "macros/cache_alignment.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Cross-platform thermal/workload policy.
 *
 * This module never reads hardware sensors itself. The platform integration
 * supplies telemetry from an OS API, device SDK, or board-specific sensor.
 * Missing temperature telemetry is not interpreted as a cool device.
 *
 * The policy throttles optional work only. It explicitly recommends keeping
 * primary audio capture and per-frame processing active on every input frame.
 */

typedef enum FinVoiceThermalState {
    FIN_VOICE_THERMAL_UNKNOWN = 0,
    FIN_VOICE_THERMAL_COOL,
    FIN_VOICE_THERMAL_WARM,
    FIN_VOICE_THERMAL_HOT,
    FIN_VOICE_THERMAL_CRITICAL
} FinVoiceThermalState;

typedef enum FinVoiceWorkloadMode {
    FIN_VOICE_WORKLOAD_NORMAL = 0,
    FIN_VOICE_WORKLOAD_CONSERVE,
    FIN_VOICE_WORKLOAD_THROTTLED,
    FIN_VOICE_WORKLOAD_MINIMAL
} FinVoiceWorkloadMode;

typedef struct FinVoiceThermalPolicyConfig {
    float warm_temperature_c;
    float hot_temperature_c;
    float critical_temperature_c;
    float hysteresis_c;
    uint8_t hot_samples_to_escalate;
    uint8_t cool_samples_to_recover;
    float cpu_utilization_conserve_percent;
    float cpu_utilization_throttle_percent;
} FinVoiceThermalPolicyConfig;

typedef struct FinVoiceThermalTelemetry {
    bool temperature_valid;
    float temperature_c;

    bool cpu_utilization_valid;
    float cpu_utilization_percent;

    /* OS/SDK says the device has already thermally throttled. */
    bool platform_throttling;

    /* Caller reports memory pressure or a constrained-memory condition. */
    bool memory_pressure;

    /* Optional monotonic timestamp, in milliseconds; zero means unavailable. */
    uint64_t monotonic_time_ms;
} FinVoiceThermalTelemetry;

typedef struct FinVoiceThermalDecision {
    FinVoiceThermalState thermal_state;
    FinVoiceWorkloadMode workload_mode;

    /* Cadence for optional work, not audio capture or primary inference. */
    uint8_t optional_work_stride;
    uint8_t max_secondary_candidates;
    uint8_t recommended_work_budget_percent;

    bool process_every_audio_frame;
    bool run_secondary_rescoring;
    bool run_optional_refinement;
    bool allow_background_tasks;
    bool allow_online_adaptation;
    bool enable_diagnostics;

    bool temperature_available;
    bool platform_reports_throttling;
} FinVoiceThermalDecision;

typedef struct FinVoiceThermalPolicy {
    FIN_VOICE_CACHE_ALIGN FinVoiceThermalPolicyConfig config;
    FinVoiceThermalState state;
    FinVoiceWorkloadMode workload_mode;

    uint8_t hot_sample_count;
    uint8_t recovery_sample_count;
    bool has_temperature_sample;
    bool initialized;
    uint64_t last_update_time_ms;
} FinVoiceThermalPolicy;

void fin_voice_thermal_policy_default_config(
    FinVoiceThermalPolicyConfig *config
);

/*
 * Validate and initialize a policy. Pass NULL config to use defaults.
 * Returns false for invalid threshold ordering/ranges or a NULL policy.
 */
bool fin_voice_thermal_policy_init(
    FinVoiceThermalPolicy *policy,
    const FinVoiceThermalPolicyConfig *config
);

/*
 * Update policy from caller-supplied telemetry.
 * Returns false for invalid pointers, non-finite valid readings, or out-of-range
 * CPU utilization. 'decision' is written only after successful evaluation.
 */
bool fin_voice_thermal_policy_update(
    FinVoiceThermalPolicy *policy,
    const FinVoiceThermalTelemetry *telemetry,
    FinVoiceThermalDecision *decision
);

const char *fin_voice_thermal_state_name(FinVoiceThermalState state);
const char *fin_voice_workload_mode_name(FinVoiceWorkloadMode mode);

#ifdef __cplusplus
}
#endif

#endif /* FIN_VOICE_THERMAL_POLICY_H */
