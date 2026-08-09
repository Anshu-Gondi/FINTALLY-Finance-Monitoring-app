#include <gtest/gtest.h>
#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include <vector>
#include <random>
#include <cstdint>

class TensorOpsTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::mt19937 rng(42);
        std::uniform_int_distribution<int> dist(0, 255);
        test_data.resize(1024 * 1024);
        for (auto& byte : test_data) {
            byte = static_cast<uint8_t>(dist(rng));
        }
    }
    std::vector<uint8_t> test_data;
};

TEST_F(TensorOpsTest, BinarizeSimdMatchesScalar) {
    std::vector<uint8_t> simd_buf = test_data;
    std::vector<uint8_t> scalar_buf = test_data;

    fin::ops::binarize_simd(simd_buf.data(), simd_buf.size());

    for (size_t i = 0; i < scalar_buf.size(); ++i) {
        scalar_buf[i] = (scalar_buf[i] > 128) ? 255 : 0;
    }

    EXPECT_EQ(simd_buf, scalar_buf);
}

TEST_F(TensorOpsTest, ContrastBoostSimdWorks) {
    std::vector<uint8_t> buf(1024, 100);
    fin::ops::contrast_boost_simd(buf.data(), buf.size());
    EXPECT_NE(buf[0], 100);
}

TEST(FfiBridgeTest, RejectsInvalidDimensions) {
    FinOcrEngineContext* engine = fin_engine_create();
    ASSERT_NE(engine, nullptr);

    uint8_t dummy_input[100] = {0};

    FinProcessedBuffer* res = fin_process_document_bytes(
        engine, dummy_input, sizeof(dummy_input), FIN_INPUT_RAW_IMAGE, 0, 768, 3
    );
    EXPECT_EQ(res, nullptr);

    fin_engine_destroy(engine);
}

TEST(FfiBridgeTest, VerifiesBinarizedMetadataFlag) {
    FinOcrEngineContext* engine = fin_engine_create();
    uint8_t dummy_input[1024] = {150};

    FinProcessedBuffer* pdf_buf = fin_process_document_bytes(
        engine, dummy_input, sizeof(dummy_input), FIN_INPUT_PDF_PAGE, 32, 32, 1
    );
    ASSERT_NE(pdf_buf, nullptr);
    EXPECT_EQ(pdf_buf->is_binarized, 1);
    EXPECT_EQ(pdf_buf->data_len, 32 * 32 * 1);
    fin_free_processed_buffer(pdf_buf);

    FinProcessedBuffer* chart_buf = fin_process_document_bytes(
        engine, dummy_input, sizeof(dummy_input), FIN_INPUT_FIN_CHART, 32, 32, 1
    );
    ASSERT_NE(chart_buf, nullptr);
    EXPECT_EQ(chart_buf->is_binarized, 0);
    fin_free_processed_buffer(chart_buf);

    fin_engine_destroy(engine);
}

TEST(ThermalMonitorTest, ReadsValidMetrics) {
    FinThermalMetrics metrics = fin_get_thermal_metrics();
    EXPECT_GE(static_cast<int>(metrics.status), 0);
    EXPECT_LE(static_cast<int>(metrics.status), 3);
}

TEST(ThermalMonitorTest, FallbackOnThermalCritical) {
    // Force critical threshold to an artificially low value (-50C)
    // so ambient CPU temperature triggers FIN_THERMAL_CRITICAL_HOT
    fin_set_thermal_thresholds(-100.0f, -50.0f);

    FinThermalMetrics metrics = fin_get_thermal_metrics();
    EXPECT_EQ(metrics.status, FIN_THERMAL_CRITICAL_HOT);

    FinOcrEngineContext* engine = fin_engine_create();
    uint8_t dummy_input[1024] = {150};

    // Execute vision pipeline under forced thermal fallback mode
    FinProcessedBuffer* pdf_buf = fin_process_document_bytes(
        engine, dummy_input, sizeof(dummy_input), FIN_INPUT_PDF_PAGE, 32, 32, 1
    );
    ASSERT_NE(pdf_buf, nullptr);
    EXPECT_EQ(pdf_buf->is_binarized, 1);
    EXPECT_EQ(pdf_buf->data[0], 255); // 150 > 128 binarizes to 255

    fin_free_processed_buffer(pdf_buf);
    fin_engine_destroy(engine);

    // Restore standard thermal limits
    fin_set_thermal_thresholds(75.0f, 87.0f);
}
