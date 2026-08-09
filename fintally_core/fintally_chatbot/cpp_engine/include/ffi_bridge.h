#ifndef FFI_BRIDGE_H
#define FFI_BRIDGE_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    FIN_INPUT_RAW_IMAGE = 0,
    FIN_INPUT_PDF_PAGE  = 1,
    FIN_INPUT_FIN_CHART = 2
} FinInputType;

typedef enum {
    FIN_THERMAL_NORMAL        = 0,
    FIN_THERMAL_WARM          = 1,
    FIN_THERMAL_CRITICAL_HOT  = 2,
    FIN_THERMAL_CRITICAL_COLD = 3
} FinThermalStatus;

typedef struct {
    float max_temp_celsius;
    float avg_temp_celsius;
    FinThermalStatus status;
} FinThermalMetrics;

typedef struct {
    uint8_t* data;
    size_t width;
    size_t height;
    size_t channels;
    size_t data_len;
    int is_binarized; // Metadata flag: 1 = binarized image, 0 = standard image
} FinProcessedBuffer;

typedef struct FinOcrEngineContext FinOcrEngineContext;

FinOcrEngineContext* fin_engine_create(void);
void fin_engine_destroy(FinOcrEngineContext* engine);

// Dynamic vision processing pipeline
FinProcessedBuffer* fin_process_document_bytes(
    FinOcrEngineContext* engine,
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type,
    size_t target_width,
    size_t target_height,
    size_t target_channels
);

void fin_free_processed_buffer(FinProcessedBuffer* buffer);

// Thermal Monitoring C-FFI API
FinThermalMetrics fin_get_thermal_metrics(void);
void fin_set_thermal_thresholds(float warm_limit_c, float critical_limit_c);

#ifdef __cplusplus
}
#endif

#endif // FFI_BRIDGE_H
