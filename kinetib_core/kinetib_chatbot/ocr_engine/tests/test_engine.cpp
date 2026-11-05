#include <gtest/gtest.h>
#include "ffi_bridge.h"
#include "tensor_ops.hpp"
#include "thermal_sensor.hpp"
#include "fin_ocr/matrix_matcher.hpp"
#include "fin_ocr/pipeline/vision_pipeline.hpp"


#include <algorithm>
#include <cstdint>
#include <cstring>
#include <random>
#include <stdexcept>
#include <vector>

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

TEST(ChartProcessingTest, MultiColorHsvIsolationPreservesHueDifferences) {
    // 2 adjacent RGB pixels: Pixel A (Pure Red [255, 0, 0]), Pixel B (Pure Green [0, 255, 0])
    uint8_t rgb_chart[6] = {
        255, 0, 0,
        0, 255, 0
    };

    FinOcrEngineContext* engine = fin_engine_create();
    ASSERT_NE(engine, nullptr);

    FinProcessedBuffer* chart_buf = fin_process_document_bytes(
        engine, rgb_chart, sizeof(rgb_chart), FIN_INPUT_FIN_CHART, 2, 1, 3
    );

    ASSERT_NE(chart_buf, nullptr);
    ASSERT_EQ(chart_buf->channels, 3);

    // Channel index 2 holds hue delta in isolate_chart_color_channels_avx2
    const uint8_t red_hue_delta = chart_buf->data[2];
    const uint8_t green_hue_delta = chart_buf->data[5];

    EXPECT_EQ(red_hue_delta, 255);   // Max delta for pure red
    EXPECT_EQ(green_hue_delta, 255); // Max delta for pure green

    fin_free_processed_buffer(chart_buf);
    fin_engine_destroy(engine);
}

TEST(FfiBridgeTest, ReturnsNullOnInvalidDimensions) {
    FinOcrEngineContext* engine = fin_engine_create();
    ASSERT_NE(engine, nullptr);

    uint8_t dummy_input[100] = {0};

    // The C-ABI layer catches exceptions internally and safely returns nullptr
    FinProcessedBuffer* res = fin_process_document_bytes(
        engine, dummy_input, sizeof(dummy_input), FIN_INPUT_RAW_IMAGE, 0, 768, 3
    );
    EXPECT_EQ(res, nullptr);

    fin_engine_destroy(engine);
}

TEST(FfiBridgeTest, VerifiesBinarizedMetadataFlag) {
    FinOcrEngineContext* engine = fin_engine_create();
    ASSERT_NE(engine, nullptr);

    uint8_t dummy_input[1024];
    std::fill_n(dummy_input, 1024, 150);

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

TEST(VisionPipelineTest, ThrowsOnInvalidDimensions) {
    uint8_t dummy_input[100] = {0};

    EXPECT_THROW({
        static_cast<void>(
            fin_ocr::VisionPipeline::execute(
                dummy_input,
                sizeof(dummy_input),
                FIN_INPUT_RAW_IMAGE,
                0,
                768,
                3
            )
        );
    }, std::invalid_argument);
}

TEST(VisionPipelineTest, DirectPipelineExecutionHandlesRawFallback) {
    uint8_t raw_rgb[48];
    std::fill_n(raw_rgb, 48, 200);

    FinProcessedBuffer* res = execute_vision_pipeline(
        raw_rgb, sizeof(raw_rgb), FIN_INPUT_RAW_IMAGE, 4, 4, 3
    );

    ASSERT_NE(res, nullptr);
    EXPECT_EQ(res->width, 4);
    EXPECT_EQ(res->height, 4);
    EXPECT_EQ(res->channels, 3);
    EXPECT_EQ(res->data_len, 48);

    fin_free_processed_buffer(res);
}

TEST(ThermalMonitorTest, ReadsValidMetrics) {
    const FinThermalMetrics metrics = fin_get_thermal_metrics();
    EXPECT_GE(static_cast<int>(metrics.status), static_cast<int>(FIN_THERMAL_NORMAL));
    EXPECT_LE(static_cast<int>(metrics.status), static_cast<int>(FIN_THERMAL_CRITICAL_COLD));
}

TEST(ThermalMonitorTest, FallbackOnThermalCritical) {
    // Set thresholds below sub-zero to force CRITICAL_HOT status
    fin_set_thermal_thresholds(-100.0f, -50.0f);

    FinThermalMetrics metrics = fin_get_thermal_metrics();
    EXPECT_EQ(metrics.status, FIN_THERMAL_CRITICAL_HOT);

    FinOcrEngineContext* engine = fin_engine_create();
    ASSERT_NE(engine, nullptr);

    uint8_t dummy_input[1024];
    std::fill_n(dummy_input, 1024, 150);

    // Verify thermal guard path evaluates scalar thresholding under high heat
    FinProcessedBuffer* pdf_buf = fin_process_document_bytes(
        engine, dummy_input, sizeof(dummy_input), FIN_INPUT_PDF_PAGE, 32, 32, 1
    );
    ASSERT_NE(pdf_buf, nullptr);
    EXPECT_EQ(pdf_buf->is_binarized, 1);
    EXPECT_EQ(pdf_buf->data[0], 255); // (150 > 128) * 255 = 255

    fin_free_processed_buffer(pdf_buf);
    fin_engine_destroy(engine);

    // Restore standard operating limits
    fin_set_thermal_thresholds(75.0f, 87.0f);
}
