#include <gtest/gtest.h>

#include "ffi_bridge.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

// =============================================================================
// FILE HELPERS
// =============================================================================

std::vector<uint8_t> read_file_bytes(
    const std::string& filepath
) {
    std::ifstream file(
        filepath,
        std::ios::binary | std::ios::ate
    );

    if (!file.is_open()) {
        return {};
    }

    const std::streamsize size =
        file.tellg();

    if (size <= 0) {
        return {};
    }

    file.seekg(
        0,
        std::ios::beg
    );

    std::vector<uint8_t> buffer(
        static_cast<std::size_t>(size)
    );

    if (!file.read(
            reinterpret_cast<char*>(buffer.data()),
            size
        )) {
        return {};
    }

    return buffer;
}

void save_text_to_disk(
    const std::string& filename,
    const std::string& text
) {
    fs::create_directories(
        "debug_output"
    );

    std::ofstream out(
        "debug_output/" +
        filename +
        ".txt"
    );

    out << text;
}

void save_buffer_to_disk(
    const std::string& filename,
    const FinProcessedBuffer* buf
) {
    if (
        buf == nullptr ||
        buf->data == nullptr
    ) {
        return;
    }

    fs::create_directories(
        "debug_output"
    );

    const std::string path =
        "debug_output/" +
        filename;

    if (
        buf->channels == 1
    ) {
        std::ofstream out(
            path + ".pgm",
            std::ios::binary
        );

        out
            << "P5\n"
            << buf->width
            << " "
            << buf->height
            << "\n255\n";

        out.write(
            reinterpret_cast<const char*>(
                buf->data
            ),
            static_cast<std::streamsize>(
                buf->data_len
            )
        );

    } else if (
        buf->channels == 3
    ) {
        std::ofstream out(
            path + ".ppm",
            std::ios::binary
        );

        out
            << "P6\n"
            << buf->width
            << " "
            << buf->height
            << "\n255\n";

        out.write(
            reinterpret_cast<const char*>(
                buf->data
            ),
            static_cast<std::streamsize>(
                buf->data_len
            )
        );
    }
}

// =============================================================================
// BUFFER GEOMETRY / MEMORY VALIDATION
// =============================================================================

void verify_buffer_metadata(
    const FinProcessedBuffer* buf,
    uint32_t expected_w,
    uint32_t expected_h,
    uint32_t expected_channels,
    uint8_t expected_binarized
) {
    ASSERT_NE(
        buf,
        nullptr
    ) << "Buffer pointer is null.";

    ASSERT_NE(
        buf->data,
        nullptr
    ) << "Buffer data allocation pointer is null.";

    EXPECT_EQ(
        buf->width,
        expected_w
    ) << "Width mismatch.";

    EXPECT_EQ(
        buf->height,
        expected_h
    ) << "Height mismatch.";

    EXPECT_EQ(
        buf->channels,
        expected_channels
    ) << "Channel count mismatch.";

    EXPECT_EQ(
        buf->is_binarized,
        expected_binarized
    ) << "Binarization flag mismatch.";

    const std::size_t expected_len =
        static_cast<std::size_t>(
            expected_w
        ) *
        static_cast<std::size_t>(
            expected_h
        ) *
        static_cast<std::size_t>(
            expected_channels
        );

    EXPECT_EQ(
        buf->data_len,
        expected_len
    ) << "Data payload byte length mismatch.";

    const uintptr_t ptr_val =
        reinterpret_cast<uintptr_t>(
            buf->data
        );

    EXPECT_EQ(
        ptr_val % 64u,
        0u
    ) << "Memory address is not 64-byte aligned.";
}

// =============================================================================
// PIXEL STATISTICAL VALIDATION
// =============================================================================

void verify_buffer_integrity(
    const FinProcessedBuffer* buf,
    bool expect_binary
) {
    ASSERT_NE(
        buf,
        nullptr
    );

    ASSERT_NE(
        buf->data,
        nullptr
    );

    ASSERT_GT(
        buf->data_len,
        0u
    );

    std::size_t non_zero_count = 0;

    uint64_t sum = 0;

    uint8_t min_val = 255;

    uint8_t max_val = 0;

    for (
        std::size_t i = 0;
        i < buf->data_len;
        ++i
    ) {
        const uint8_t val =
            buf->data[i];

        if (
            val > 0
        ) {
            ++non_zero_count;
        }

        sum += val;

        min_val =
            std::min(
                min_val,
                val
            );

        max_val =
            std::max(
                max_val,
                val
            );

        if (
            expect_binary
        ) {
            EXPECT_TRUE(
                val == 0 ||
                val == 255
            )
                << "Non-binary pixel value at index "
                << i
                << ": "
                << static_cast<int>(val);
        }
    }

    const double mean =
        static_cast<double>(sum) /
        static_cast<double>(buf->data_len);

    double variance = 0.0;

    for (
        std::size_t i = 0;
        i < buf->data_len;
        ++i
    ) {
        const double diff =
            static_cast<double>(
                buf->data[i]
            ) -
            mean;

        variance +=
            diff * diff;
    }

    variance /=
        static_cast<double>(
            buf->data_len
        );

    std::cout
        << "    [Stats] Non-zero pixels: "
        << non_zero_count
        << " / "
        << buf->data_len
        << " ("
        << (
            100.0 *
            static_cast<double>(
                non_zero_count
            ) /
            static_cast<double>(
                buf->data_len
            )
        )
        << "%)\n"
        << "    [Stats] Min: "
        << static_cast<int>(min_val)
        << ", Max: "
        << static_cast<int>(max_val)
        << ", Mean: "
        << mean
        << ", StdDev: "
        << std::sqrt(variance)
        << '\n';

    EXPECT_GT(
        non_zero_count,
        0u
    ) << "Output buffer is completely black.";

    EXPECT_LT(
        non_zero_count,
        buf->data_len
    ) << "Output buffer is completely white.";

    EXPECT_GT(
        variance,
        1.0
    ) << "Zero or near-zero variance detected.";
}

// =============================================================================
// BUFFER-LEVEL OCR VALIDATION
// =============================================================================
//
// This is deliberately separate from fin_engine_recognize_text().
//
// For charts, the VisionPipeline should already have populated:
//
//     buffer->extracted_text
//
// before the C ABI wrapper is called.
//
// =============================================================================

void verify_attached_ocr_text(
    const FinProcessedBuffer* buf,
    const std::string& test_name,
    const std::vector<std::string>& expected_substrings,
    std::size_t min_required_length = 20
) {
    ASSERT_NE(
        buf,
        nullptr
    );

    ASSERT_NE(
        buf->extracted_text,
        nullptr
    )
        << "Vision pipeline did not attach extracted OCR text.";

    const std::string text(
        buf->extracted_text
    );

    ASSERT_FALSE(
        text.empty()
    )
        << "Vision pipeline attached an empty OCR string.";

    EXPECT_GE(
        text.length(),
        min_required_length
    )
        << "Attached OCR text is too short. "
        << "Length: "
        << text.length()
        << ", expected at least "
        << min_required_length;

    for (
        const auto& expected :
        expected_substrings
    ) {
        EXPECT_NE(
            text.find(expected),
            std::string::npos
        )
            << "Missing expected chart OCR content: '"
            << expected
            << "'.\nAttached OCR:\n"
            << text;
    }

    std::cout
        << "\n=== [Attached OCR Diagnostic - "
        << test_name
        << "] ===\n"
        << "Length: "
        << text.length()
        << " chars\n"
        << text
        << "\n===========================================\n";

    save_text_to_disk(
        test_name +
            "_attached_ocr",
        text
    );
}

// =============================================================================
// FINAL C-ABI PAYLOAD VALIDATION
// =============================================================================

void verify_ocr_text_payload(
    const char* raw_text_ptr,
    const std::string& test_name,
    const std::vector<std::string>& expected_substrings,
    std::size_t min_required_length = 20
) {
    ASSERT_NE(
        raw_text_ptr,
        nullptr
    )
        << "OCR engine returned a null text pointer.";

    const std::string ocr_text(
        raw_text_ptr
    );

    ASSERT_FALSE(
        ocr_text.empty()
    )
        << "OCR engine produced an empty payload.";

    EXPECT_GE(
        ocr_text.length(),
        min_required_length
    )
        << "OCR payload is too short. "
        << "Length: "
        << ocr_text.length()
        << ", expected at least "
        << min_required_length;

    for (
        const auto& expected :
        expected_substrings
    ) {
        EXPECT_NE(
            ocr_text.find(expected),
            std::string::npos
        )
            << "Missing required structural marker/content: '"
            << expected
            << "'.\nFinal payload:\n"
            << ocr_text;
    }

    std::cout
        << "\n=== [Final OCR Payload Diagnostic - "
        << test_name
        << "] ===\n"
        << "Payload Length: "
        << ocr_text.length()
        << " chars\n"
        << "Payload:\n"
        << ocr_text
        << "\n===================================================\n";

    save_text_to_disk(
        test_name,
        ocr_text
    );
}

} // anonymous namespace

// =============================================================================
// TEST FIXTURE
// =============================================================================

class DatasetProcessingTest
    : public ::testing::Test {

protected:

    FinOcrEngineContext* engine =
        nullptr;

    void SetUp() override {
        engine =
            fin_engine_create();

        ASSERT_NE(
            engine,
            nullptr
        )
            << "Failed to allocate OCR engine context.";
    }

    void TearDown() override {
        if (
            engine
        ) {
            fin_engine_destroy(
                engine
            );

            engine =
                nullptr;
        }
    }
};

// =============================================================================
// PDF
// =============================================================================

TEST_F(
    DatasetProcessingTest,
    ProcessesPdfBalanceSheet
) {
    const auto bytes =
        read_file_bytes(
            "data/financial_balance_sheet_image.pdf"
        );

    ASSERT_FALSE(
        bytes.empty()
    )
        << "Failed to locate test fixture: "
        << "data/financial_balance_sheet_image.pdf";

    constexpr uint32_t req_w = 1920;
    constexpr uint32_t req_h = 1080;
    constexpr uint32_t req_ch = 1;

    FinProcessedBuffer* buf =
        fin_process_document_bytes(
            engine,
            bytes.data(),
            bytes.size(),
            FIN_INPUT_PDF_PAGE,
            req_w,
            req_h,
            req_ch
        );

    verify_buffer_metadata(
        buf,
        req_w,
        req_h,
        req_ch,
        /*expected_binarized=*/1
    );

    verify_buffer_integrity(
        buf,
        /*expect_binary=*/true
    );

    save_buffer_to_disk(
        "pdf_balance_sheet_out",
        buf
    );

    char* text =
        fin_engine_recognize_text(
            engine,
            buf,
            bytes.data(),
            bytes.size(),
            FIN_INPUT_PDF_PAGE
        );

    verify_ocr_text_payload(
        text,
        "pdf_balance_sheet_out",
        {
            "[FIN_ENGINE_OCR_OUTPUT]",
            "Dimensions: 1920x1080",
            "Binarized: YES",
            "Payload Status: VALID_PREPROCESSED_BUFFER"
        },
        100
    );

    fin_free_string(
        text
    );

    fin_free_processed_buffer(
        buf
    );
}

// =============================================================================
// WEBP FINANCIAL CHART
// =============================================================================
//
// IMPORTANT:
//
// This test deliberately checks:
//
//     A. VisionPipeline produced extracted_text.
//     B. fin_engine_recognize_text() wrapped that text.
//
// This isolates chart-recognition failures from payload-formatting failures.
//
// =============================================================================

TEST_F(
    DatasetProcessingTest,
    ProcessesWebpFinancialCharts
) {
    const auto chart_bytes =
        read_file_bytes(
            "data/financial-graphs-and-charts-in-excel.jpg"
        );

    ASSERT_FALSE(
        chart_bytes.empty()
    )
        << "Failed to locate test fixture: "
        << "data/financial-graphs-and-charts-in-excel.jpg";

    constexpr uint32_t req_w = 1280;
    constexpr uint32_t req_h = 720;
    constexpr uint32_t req_ch = 3;

    FinProcessedBuffer* buf =
        fin_process_document_bytes(
            engine,
            chart_bytes.data(),
            chart_bytes.size(),
            FIN_INPUT_FIN_CHART,
            req_w,
            req_h,
            req_ch
        );

    verify_buffer_metadata(
        buf,
        req_w,
        req_h,
        req_ch,
        /*expected_binarized=*/0
    );

    verify_buffer_integrity(
        buf,
        /*expect_binary=*/false
    );

    save_buffer_to_disk(
        "chart1_rgb_out",
        buf
    );

    // -------------------------------------------------------------------------
    // STAGE A: VERIFY VISION PIPELINE OCR RESULT
    // -------------------------------------------------------------------------

    verify_attached_ocr_text(
        buf,
        "chart1",
        {
            "[CHART_TEXT_DATA]",
            "Line 1"
        },
        40
    );

    // -------------------------------------------------------------------------
    // STAGE B: VERIFY HIGH-LEVEL C ABI PAYLOAD
    // -------------------------------------------------------------------------

    char* text =
        fin_engine_recognize_text(
            engine,
            buf,
            chart_bytes.data(),
            chart_bytes.size(),
            FIN_INPUT_FIN_CHART
        );

    verify_ocr_text_payload(
        text,
        "chart1_text_out",
        {
            "[EXTRACTED_CHART_LABELS]",
            "[CHART_TEXT_DATA]",
            "Line 1"
        },
        120
    );

    fin_free_string(
        text
    );

    fin_free_processed_buffer(
        buf
    );
}

// =============================================================================
// RECEIPT
// =============================================================================

TEST_F(
    DatasetProcessingTest,
    ProcessesReceiptImages
) {
    const auto jpg_bytes =
        read_file_bytes(
            "data/financial_receipt_image.jpg"
        );

    ASSERT_FALSE(
        jpg_bytes.empty()
    )
        << "Failed to locate test fixture: "
        << "data/financial_receipt_image.jpg";

    constexpr uint32_t req_w = 1024;
    constexpr uint32_t req_h = 1024;
    constexpr uint32_t req_ch = 3;

    FinProcessedBuffer* buf =
        fin_process_document_bytes(
            engine,
            jpg_bytes.data(),
            jpg_bytes.size(),
            FIN_INPUT_RAW_IMAGE,
            req_w,
            req_h,
            req_ch
        );

    verify_buffer_metadata(
        buf,
        req_w,
        req_h,
        req_ch,
        /*expected_binarized=*/0
    );

    verify_buffer_integrity(
        buf,
        /*expect_binary=*/false
    );

    save_buffer_to_disk(
        "receipt_out",
        buf
    );

    char* text =
        fin_engine_recognize_text(
            engine,
            buf,
            jpg_bytes.data(),
            jpg_bytes.size(),
            FIN_INPUT_RAW_IMAGE
        );

    verify_ocr_text_payload(
        text,
        "receipt_text_out",
        {
            "[FIN_ENGINE_OCR_OUTPUT]",
            "Dimensions: 1024x1024",
            "Binarized: NO",
            "Payload Status: VALID_PREPROCESSED_BUFFER"
        },
        80
    );

    fin_free_string(
        text
    );

    fin_free_processed_buffer(
        buf
    );
}

// =============================================================================
// CORRUPTED INPUT
// =============================================================================

TEST_F(
    DatasetProcessingTest,
    HandlesCorruptedInputBytesGracefully
) {
    const std::vector<uint8_t> corrupt_bytes{
        0x00,
        0xFF,
        0xDE,
        0xAD,
        0xBE,
        0xEF,
        0x12,
        0x34
    };

    FinProcessedBuffer* buf =
        fin_process_document_bytes(
            engine,
            corrupt_bytes.data(),
            corrupt_bytes.size(),
            FIN_INPUT_RAW_IMAGE,
            64,
            64,
            3
        );

    ASSERT_NE(
        buf,
        nullptr
    );

    EXPECT_EQ(
        buf->width,
        64u
    );

    EXPECT_EQ(
        buf->height,
        64u
    );

    EXPECT_EQ(
        buf->data_len,
        64u * 64u * 3u
    );

    fin_free_processed_buffer(
        buf
    );
}

// =============================================================================
// NULL BUFFER
// =============================================================================

TEST_F(
    DatasetProcessingTest,
    HandlesNullBufferInRecognizeText
) {
    const std::vector<uint8_t> dummy_bytes{
        0x01,
        0x02,
        0x03
    };

    char* text =
        fin_engine_recognize_text(
            engine,
            nullptr,
            dummy_bytes.data(),
            dummy_bytes.size(),
            FIN_INPUT_RAW_IMAGE
        );

    EXPECT_EQ(
        text,
        nullptr
    );

    if (
        text
    ) {
        fin_free_string(
            text
        );
    }
}
