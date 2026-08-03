#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include <new>
#include <cstdlib>
#include <cstring>

// Full definition of the opaque context structure here so the compiler knows its size
struct FinOcrEngineContext {
    size_t max_buffer_limit;
};

// Forward declaration of internal pipeline function from ocr_pipeline.cpp
FinProcessedBuffer* execute_vision_pipeline(
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type
);

extern "C" {

FinOcrEngineContext* fin_engine_create(void) {
    auto* ctx = new (std::nothrow) FinOcrEngineContext();
    if (ctx) {
        ctx->max_buffer_limit = 1024 * 1024 * 16; // 16MB max scratchpad per thread
    }
    return ctx;
}

void fin_engine_destroy(FinOcrEngineContext* engine) {
    if (engine) {
        delete engine;
    }
}

FinProcessedBuffer* fin_process_document_bytes(
    FinOcrEngineContext* engine,
    const uint8_t* input_bytes,
    size_t input_len,
    FinInputType input_type
) {
    if (!engine || !input_bytes || input_len == 0) return nullptr;

    // Delegate to C++ implementation in ocr_pipeline.cpp
    return execute_vision_pipeline(input_bytes, input_len, input_type);
}

void fin_free_processed_buffer(FinProcessedBuffer* buffer) {
    if (buffer) {
        if (buffer->data) {
            fin::ops::aligned_free(buffer->data);
        }
        delete buffer;
    }
}

}
