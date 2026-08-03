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

typedef struct {
    uint8_t* data;
    size_t width;
    size_t height;
    size_t channels;
    size_t data_len;
} FinProcessedBuffer;

typedef struct FinOcrEngineContext FinOcrEngineContext;

FinOcrEngineContext* fin_engine_create(void);
void fin_engine_destroy(FinOcrEngineContext* engine);

FinProcessedBuffer* fin_process_document_bytes(
    FinOcrEngineContext* engine,
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type
);

void fin_free_processed_buffer(FinProcessedBuffer* buffer);

#ifdef __cplusplus
}
#endif

#endif // FFI_BRIDGE_H
