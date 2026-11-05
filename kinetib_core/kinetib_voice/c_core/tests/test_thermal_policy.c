/*
 * FinTally Voice thermal policy regression tests.
 *
 * Purpose
 * -------
 * Exercise the public thermal-policy API without depending on an actual
 * temperature sensor, OS thermal service, or timing source. The caller owns
 * telemetry, so deterministic input sequences can cover threshold boundaries,
 * hysteresis, confirmation counters, and workload decisions.
 *
 * The tests intentionally distinguish initial classification from later
 * escalation: the first valid temperature sample classifies the current state
 * immediately, while subsequent transitions into HOT require the configured
 * number of consecutive hot samples. CRITICAL is immediate by design.
 *
 * Cache alignment assertions verify the C object-layout contract only. They do
 * not claim that a cache-aligned object will remain resident in L1 or avoid
 * misses. Public ABI changes require rebuilding dependants and rechecking any
 * direct Rust mirror; opaque FFI handles are preferred.
 */
#include "thermal_policy.h"

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


static unsigned long g_checks;
static unsigned long g_failures;

#define CHECK(condition) do { \
    ++g_checks; \
    if (!(condition)) { \
        ++g_failures; \
        (void)fprintf(stderr, "FAIL %s:%d: %s\n", \
                      __FILE__, __LINE__, #condition); \
    } \
} while (0)

_Static_assert(
    _Alignof(FinVoiceThermalPolicy) >= FIN_VOICE_CACHE_LINE_BYTES,
    "FinVoiceThermalPolicy must retain the configured cache-line alignment"
);
_Static_assert(
    (offsetof(FinVoiceThermalPolicy, config) %
     FIN_VOICE_CACHE_LINE_BYTES) == 0u,
    "The aligned configuration member must begin on a cache-line boundary"
);
_Static_assert(
    (sizeof(FinVoiceThermalPolicy) % FIN_VOICE_CACHE_LINE_BYTES) == 0u,
    "Arrays of policies must preserve each object's required alignment"
);

static void telemetry_default(FinVoiceThermalTelemetry *telemetry)
{
    (void)memset(telemetry, 0, sizeof(*telemetry));
}

static bool update_temperature(
    FinVoiceThermalPolicy *policy,
    float temperature_c,
    uint64_t timestamp_ms,
    FinVoiceThermalDecision *decision
)
{
    FinVoiceThermalTelemetry telemetry;

    telemetry_default(&telemetry);
    telemetry.temperature_valid = true;
    telemetry.temperature_c = temperature_c;
    telemetry.monotonic_time_ms = timestamp_ms;
    {
        const bool ok = fin_voice_thermal_policy_update(
            policy, &telemetry, decision);
        if (ok) {
            /* The cached mode is part of the policy's public observable state. */
            CHECK(policy->workload_mode == decision->workload_mode);
        }
        return ok;
    }
}

static void check_decision_mode(
    const FinVoiceThermalDecision *decision,
    FinVoiceWorkloadMode mode
)
{
    CHECK(decision->workload_mode == mode);
    CHECK(decision->process_every_audio_frame);

    switch (mode) {
    case FIN_VOICE_WORKLOAD_NORMAL:
        CHECK(decision->optional_work_stride == 1u);
        CHECK(decision->max_secondary_candidates == 16u);
        CHECK(decision->recommended_work_budget_percent == 100u);
        CHECK(decision->run_secondary_rescoring);
        CHECK(decision->run_optional_refinement);
        CHECK(decision->allow_background_tasks);
        CHECK(decision->allow_online_adaptation);
        CHECK(decision->enable_diagnostics);
        break;
    case FIN_VOICE_WORKLOAD_CONSERVE:
        CHECK(decision->optional_work_stride == 2u);
        CHECK(decision->max_secondary_candidates == 8u);
        CHECK(decision->recommended_work_budget_percent == 80u);
        CHECK(decision->run_secondary_rescoring);
        CHECK(decision->run_optional_refinement);
        CHECK(!decision->allow_background_tasks);
        CHECK(!decision->allow_online_adaptation);
        CHECK(decision->enable_diagnostics);
        break;
    case FIN_VOICE_WORKLOAD_THROTTLED:
        CHECK(decision->optional_work_stride == 4u);
        CHECK(decision->max_secondary_candidates == 4u);
        CHECK(decision->recommended_work_budget_percent == 50u);
        CHECK(decision->run_secondary_rescoring);
        CHECK(!decision->run_optional_refinement);
        CHECK(!decision->allow_background_tasks);
        CHECK(!decision->allow_online_adaptation);
        CHECK(!decision->enable_diagnostics);
        break;
    case FIN_VOICE_WORKLOAD_MINIMAL:
    default:
        CHECK(decision->optional_work_stride == 8u);
        CHECK(decision->max_secondary_candidates == 0u);
        CHECK(decision->recommended_work_budget_percent == 25u);
        CHECK(!decision->run_secondary_rescoring);
        CHECK(!decision->run_optional_refinement);
        CHECK(!decision->allow_background_tasks);
        CHECK(!decision->allow_online_adaptation);
        CHECK(!decision->enable_diagnostics);
        break;
    }
}

static void test_layout_and_names(void)
{
    FinVoiceThermalPolicy policy;
    int value;

    CHECK(((uintptr_t)&policy % FIN_VOICE_CACHE_LINE_BYTES) == 0u);
    CHECK(((uintptr_t)&policy.config % FIN_VOICE_CACHE_LINE_BYTES) == 0u);

    CHECK(strcmp(fin_voice_thermal_state_name(FIN_VOICE_THERMAL_UNKNOWN),
                 "unknown") == 0);
    CHECK(strcmp(fin_voice_thermal_state_name(FIN_VOICE_THERMAL_COOL),
                 "cool") == 0);
    CHECK(strcmp(fin_voice_thermal_state_name(FIN_VOICE_THERMAL_WARM),
                 "warm") == 0);
    CHECK(strcmp(fin_voice_thermal_state_name(FIN_VOICE_THERMAL_HOT),
                 "hot") == 0);
    CHECK(strcmp(fin_voice_thermal_state_name(FIN_VOICE_THERMAL_CRITICAL),
                 "critical") == 0);
    CHECK(strcmp(fin_voice_thermal_state_name((FinVoiceThermalState)99),
                 "unknown") == 0);

    CHECK(strcmp(fin_voice_workload_mode_name(FIN_VOICE_WORKLOAD_NORMAL),
                 "normal") == 0);
    CHECK(strcmp(fin_voice_workload_mode_name(FIN_VOICE_WORKLOAD_CONSERVE),
                 "conserve") == 0);
    CHECK(strcmp(fin_voice_workload_mode_name(FIN_VOICE_WORKLOAD_THROTTLED),
                 "throttled") == 0);
    CHECK(strcmp(fin_voice_workload_mode_name(FIN_VOICE_WORKLOAD_MINIMAL),
                 "minimal") == 0);
    CHECK(strcmp(fin_voice_workload_mode_name((FinVoiceWorkloadMode)99),
                 "normal") == 0);

    /* The names are non-null, stable diagnostics suitable for logs. */
    for (value = (int)FIN_VOICE_THERMAL_UNKNOWN;
         value <= (int)FIN_VOICE_THERMAL_CRITICAL; ++value) {
        CHECK(fin_voice_thermal_state_name((FinVoiceThermalState)value) != NULL);
    }
    for (value = (int)FIN_VOICE_WORKLOAD_NORMAL;
         value <= (int)FIN_VOICE_WORKLOAD_MINIMAL; ++value) {
        CHECK(fin_voice_workload_mode_name((FinVoiceWorkloadMode)value) != NULL);
    }
}

static void test_default_config_and_initialization(void)
{
    FinVoiceThermalPolicyConfig config;
    FinVoiceThermalPolicy policy;

    (void)memset(&config, 0xA5, sizeof(config));
    fin_voice_thermal_policy_default_config(&config);
    CHECK(config.warm_temperature_c == 65.0f);
    CHECK(config.hot_temperature_c == 78.0f);
    CHECK(config.critical_temperature_c == 88.0f);
    CHECK(config.hysteresis_c == 3.0f);
    CHECK(config.hot_samples_to_escalate == 2u);
    CHECK(config.cool_samples_to_recover == 3u);
    CHECK(config.cpu_utilization_conserve_percent == 85.0f);
    CHECK(config.cpu_utilization_throttle_percent == 97.0f);

    fin_voice_thermal_policy_default_config(NULL);
    CHECK(!fin_voice_thermal_policy_init(NULL, NULL));
    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(policy.initialized);
    CHECK(policy.state == FIN_VOICE_THERMAL_UNKNOWN);
    CHECK(policy.workload_mode == FIN_VOICE_WORKLOAD_NORMAL);
    CHECK(policy.hot_sample_count == 0u);
    CHECK(policy.recovery_sample_count == 0u);
    CHECK(!policy.has_temperature_sample);
    CHECK(policy.last_update_time_ms == 0u);
    CHECK(policy.config.warm_temperature_c == 65.0f);

    /* Inclusive physical guardrails are accepted by configuration validation. */
    config.warm_temperature_c = -40.0f;
    config.hot_temperature_c = 78.0f;
    config.critical_temperature_c = 150.0f;
    config.hysteresis_c = 0.0f;
    CHECK(fin_voice_thermal_policy_init(&policy, &config));
}

static void check_invalid_config(FinVoiceThermalPolicyConfig config)
{
    FinVoiceThermalPolicy policy;

    (void)memset(&policy, 0xA5, sizeof(policy));
    CHECK(!fin_voice_thermal_policy_init(&policy, &config));
    /* Failure clears the object so it cannot accidentally be used as active. */
    CHECK(!policy.initialized);
}

static void test_invalid_config_rejection(void)
{
    FinVoiceThermalPolicyConfig c;

    fin_voice_thermal_policy_default_config(&c);
    c.warm_temperature_c = NAN; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.hot_temperature_c = INFINITY; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.critical_temperature_c = NAN; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.hysteresis_c = INFINITY; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.cpu_utilization_conserve_percent = NAN; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.cpu_utilization_throttle_percent = INFINITY; check_invalid_config(c);

    fin_voice_thermal_policy_default_config(&c);
    c.warm_temperature_c = -40.01f; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.critical_temperature_c = 150.01f; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.warm_temperature_c = c.hot_temperature_c; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.hot_temperature_c = c.critical_temperature_c; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.warm_temperature_c = 80.0f; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.hysteresis_c = -0.01f; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.hysteresis_c = c.critical_temperature_c - c.warm_temperature_c;
    check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.hot_samples_to_escalate = 0u; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.cool_samples_to_recover = 0u; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.cpu_utilization_conserve_percent = -0.01f; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.cpu_utilization_throttle_percent = 100.01f; check_invalid_config(c);
    fin_voice_thermal_policy_default_config(&c);
    c.cpu_utilization_conserve_percent = 97.0f;
    c.cpu_utilization_throttle_percent = 97.0f;
    check_invalid_config(c);
}

static void test_custom_confirmation_counts(void)
{
    FinVoiceThermalPolicyConfig config;
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;

    fin_voice_thermal_policy_default_config(&config);
    config.hot_samples_to_escalate = 1u;
    config.cool_samples_to_recover = 1u;
    CHECK(fin_voice_thermal_policy_init(&policy, &config));

    CHECK(update_temperature(&policy, 70.0f, 1u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(update_temperature(&policy, 78.0f, 2u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);

    /* A single qualifying recovery sample moves down one state, not two. */
    CHECK(update_temperature(&policy, 74.9f, 3u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(update_temperature(&policy, 61.9f, 4u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_COOL);
}

static void test_counter_saturation(void)
{
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;
    unsigned int i;

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 70.0f, 1u, &decision));
    for (i = 0u; i < 300u; ++i) {
        CHECK(update_temperature(&policy, 78.0f, (uint64_t)i + 2u,
                                 &decision));
    }
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);
    CHECK(policy.hot_sample_count == UINT8_MAX);
}

static void test_unknown_and_mode_decisions(void)
{
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;
    FinVoiceThermalTelemetry telemetry;

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    telemetry_default(&telemetry);
    (void)memset(&decision, 0, sizeof(decision));
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_UNKNOWN);
    CHECK(decision.thermal_state == FIN_VOICE_THERMAL_UNKNOWN);
    CHECK(!decision.temperature_available);
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_NORMAL);

    CHECK(update_temperature(&policy, 20.0f, 10u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_COOL);
    CHECK(decision.temperature_available);
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_NORMAL);

    CHECK(update_temperature(&policy, 70.0f, 11u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_CONSERVE);

    CHECK(update_temperature(&policy, 88.0f, 12u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_CRITICAL);
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_MINIMAL);
}

static void test_initial_threshold_classification(void)
{
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, -40.0f, 1u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_COOL);

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 65.0f, 1u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);

    /* First sample classifies immediately; confirmation applies after startup. */
    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 78.0f, 1u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 88.0f, 1u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_CRITICAL);

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 150.0f, 1u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_CRITICAL);
}

static void test_escalation_hysteresis_and_recovery(void)
{
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 70.0f, 1u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);

    CHECK(update_temperature(&policy, 78.0f, 2u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(policy.hot_sample_count == 1u);
    CHECK(update_temperature(&policy, 77.9f, 3u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(policy.hot_sample_count == 0u);
    CHECK(update_temperature(&policy, 78.0f, 4u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(policy.hot_sample_count == 1u);
    CHECK(update_temperature(&policy, 78.0f, 5u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);
    CHECK(policy.hot_sample_count == 2u);

    /* Critical entry is immediate even if the confirmation count is nonzero. */
    CHECK(update_temperature(&policy, 88.0f, 6u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_CRITICAL);
    CHECK(policy.hot_sample_count == 0u);
    CHECK(policy.recovery_sample_count == 0u);

    /* At the exact recovery boundary, recovery is not counted (strict <). */
    CHECK(update_temperature(&policy, 85.0f, 7u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_CRITICAL);
    CHECK(policy.recovery_sample_count == 0u);
    CHECK(update_temperature(&policy, 84.9f, 8u, &decision));
    CHECK(policy.recovery_sample_count == 1u);
    CHECK(update_temperature(&policy, 85.0f, 9u, &decision));
    CHECK(policy.recovery_sample_count == 0u);
    CHECK(policy.state == FIN_VOICE_THERMAL_CRITICAL);
    CHECK(update_temperature(&policy, 84.9f, 10u, &decision));
    CHECK(update_temperature(&policy, 84.9f, 11u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_CRITICAL);
    CHECK(update_temperature(&policy, 84.9f, 12u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);
    CHECK(policy.recovery_sample_count == 0u);

    /* HOT -> WARM requires consecutive samples strictly below 78 - 3. */
    CHECK(update_temperature(&policy, 75.0f, 13u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);
    CHECK(policy.recovery_sample_count == 0u);
    CHECK(update_temperature(&policy, 74.9f, 14u, &decision));
    CHECK(update_temperature(&policy, 74.9f, 15u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);
    CHECK(update_temperature(&policy, 74.9f, 16u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);

    /* WARM -> COOL also has strict hysteresis and a multi-sample recovery. */
    CHECK(update_temperature(&policy, 62.0f, 17u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(policy.recovery_sample_count == 0u);
    CHECK(update_temperature(&policy, 61.9f, 18u, &decision));
    CHECK(update_temperature(&policy, 61.9f, 19u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(update_temperature(&policy, 61.9f, 20u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_COOL);
    CHECK(policy.recovery_sample_count == 0u);
}

static void test_missing_sensor_resets_counters_without_cooling(void)
{
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;
    FinVoiceThermalTelemetry telemetry;

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 70.0f, 1u, &decision));
    CHECK(update_temperature(&policy, 78.0f, 2u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(policy.hot_sample_count == 1u);

    telemetry_default(&telemetry);
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(policy.hot_sample_count == 0u);
    CHECK(policy.recovery_sample_count == 0u);
    CHECK(!decision.temperature_available);

    CHECK(update_temperature(&policy, 78.0f, 3u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(update_temperature(&policy, 78.0f, 4u, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);

    CHECK(update_temperature(&policy, 74.9f, 5u, &decision));
    CHECK(policy.recovery_sample_count == 1u);
    telemetry_default(&telemetry);
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);
    CHECK(policy.recovery_sample_count == 0u);
}

static void test_cpu_memory_and_platform_pressure(void)
{
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;
    FinVoiceThermalTelemetry telemetry;

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 25.0f, 1u, &decision));

    telemetry_default(&telemetry);
    telemetry.cpu_utilization_valid = true;
    telemetry.cpu_utilization_percent = 84.99f;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_NORMAL);

    telemetry.cpu_utilization_percent = 85.0f;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_CONSERVE);

    telemetry.cpu_utilization_percent = 96.99f;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_CONSERVE);

    telemetry.cpu_utilization_percent = 97.0f;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_THROTTLED);

    telemetry_default(&telemetry);
    telemetry.memory_pressure = true;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_CONSERVE);

    telemetry_default(&telemetry);
    telemetry.platform_throttling = true;
    telemetry.monotonic_time_ms = 100u;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_HOT);
    CHECK(decision.platform_reports_throttling);
    CHECK(policy.last_update_time_ms == 100u);
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_THROTTLED);

    /* Timestamp zero means unavailable and must not erase the last known one. */
    telemetry_default(&telemetry);
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    CHECK(policy.last_update_time_ms == 100u);

    /* Platform throttle never lowers a previously critical thermal state. */
    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 88.0f, 1u, &decision));
    telemetry_default(&telemetry);
    telemetry.platform_throttling = true;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_CRITICAL);
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_MINIMAL);
}

static void snapshot_unchanged_on_failed_update(
    FinVoiceThermalPolicy *policy,
    const FinVoiceThermalTelemetry *telemetry
)
{
    FinVoiceThermalDecision decision;
    unsigned char policy_before[sizeof(*policy)];
    unsigned char decision_before[sizeof(decision)];

    (void)memset(&decision, 0xA5, sizeof(decision));
    (void)memcpy(policy_before, policy, sizeof(*policy));
    (void)memcpy(decision_before, &decision, sizeof(decision));

    CHECK(!fin_voice_thermal_policy_update(policy, telemetry, &decision));
    CHECK(memcmp(policy_before, policy, sizeof(*policy)) == 0);
    CHECK(memcmp(decision_before, &decision, sizeof(decision)) == 0);
}

static void test_invalid_telemetry_and_transactional_failure(void)
{
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;
    FinVoiceThermalTelemetry telemetry;

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 70.0f, 77u, &decision));

    telemetry_default(&telemetry);
    telemetry.temperature_valid = true;
    telemetry.temperature_c = NAN;
    snapshot_unchanged_on_failed_update(&policy, &telemetry);
    telemetry.temperature_c = INFINITY;
    snapshot_unchanged_on_failed_update(&policy, &telemetry);
    telemetry.temperature_c = -40.01f;
    snapshot_unchanged_on_failed_update(&policy, &telemetry);
    telemetry.temperature_c = 150.01f;
    snapshot_unchanged_on_failed_update(&policy, &telemetry);

    telemetry_default(&telemetry);
    telemetry.cpu_utilization_valid = true;
    telemetry.cpu_utilization_percent = NAN;
    snapshot_unchanged_on_failed_update(&policy, &telemetry);
    telemetry.cpu_utilization_percent = INFINITY;
    snapshot_unchanged_on_failed_update(&policy, &telemetry);
    telemetry.cpu_utilization_percent = -0.01f;
    snapshot_unchanged_on_failed_update(&policy, &telemetry);
    telemetry.cpu_utilization_percent = 100.01f;
    snapshot_unchanged_on_failed_update(&policy, &telemetry);

    CHECK(!fin_voice_thermal_policy_update(NULL, &telemetry, &decision));
    CHECK(!fin_voice_thermal_policy_update(&policy, NULL, &decision));
    CHECK(!fin_voice_thermal_policy_update(&policy, &telemetry, NULL));

    /* The validity flags gate their payload fields: missing readings are ignored. */
    telemetry_default(&telemetry);
    telemetry.temperature_c = NAN;
    telemetry.cpu_utilization_percent = INFINITY;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    CHECK(policy.state == FIN_VOICE_THERMAL_WARM);
    CHECK(!decision.temperature_available);
}

static void test_cpu_boundaries_and_flag_gating(void)
{
    FinVoiceThermalPolicy policy;
    FinVoiceThermalDecision decision;
    FinVoiceThermalTelemetry telemetry;

    CHECK(fin_voice_thermal_policy_init(&policy, NULL));
    CHECK(update_temperature(&policy, 20.0f, 1u, &decision));

    telemetry_default(&telemetry);
    telemetry.cpu_utilization_valid = true;
    telemetry.cpu_utilization_percent = 0.0f;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_NORMAL);
    telemetry.cpu_utilization_percent = 100.0f;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_THROTTLED);

    telemetry.cpu_utilization_valid = false;
    telemetry.cpu_utilization_percent = NAN;
    CHECK(fin_voice_thermal_policy_update(&policy, &telemetry, &decision));
    check_decision_mode(&decision, FIN_VOICE_WORKLOAD_NORMAL);
}

int main(void)
{
    test_layout_and_names();
    test_default_config_and_initialization();
    test_invalid_config_rejection();
    test_custom_confirmation_counts();
    test_counter_saturation();
    test_unknown_and_mode_decisions();
    test_initial_threshold_classification();
    test_escalation_hysteresis_and_recovery();
    test_missing_sensor_resets_counters_without_cooling();
    test_cpu_memory_and_platform_pressure();
    test_invalid_telemetry_and_transactional_failure();
    test_cpu_boundaries_and_flag_gating();

    if (g_failures != 0u) {
        (void)fprintf(stderr, "Thermal policy tests: %lu/%lu checks failed\n",
                      g_failures, g_checks);
        return EXIT_FAILURE;
    }

    (void)printf("Thermal policy tests: %lu checks passed\n", g_checks);
    return EXIT_SUCCESS;
}
