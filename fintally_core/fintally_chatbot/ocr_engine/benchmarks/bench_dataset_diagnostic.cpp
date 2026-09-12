#include "ffi_bridge.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <limits>
#include <cmath>

namespace fs = std::filesystem;

// =============================================================================
// File helpers
// =============================================================================

static std::vector<uint8_t> read_file(
    const std::string& path
) {
    std::ifstream file(
        path,
        std::ios::binary | std::ios::ate
    );

    if (!file) {
        return {};
    }

    const std::streamsize size =
        file.tellg();

    if (size <= 0) {
        return {};
    }

    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> data(
        static_cast<std::size_t>(size)
    );

    file.read(
        reinterpret_cast<char*>(data.data()),
        size
    );

    return data;
}

static std::string read_text_file(
    const std::string& path
) {
    std::ifstream file(path);

    if (!file) {
        return {};
    }

    std::ostringstream ss;
    ss << file.rdbuf();

    return ss.str();
}

// =============================================================================
// Save extracted text
// =============================================================================

static void save_text(
    const std::string& name,
    const char* text
) {
    fs::create_directories(
        "diagnostic_output"
    );

    std::ofstream out(
        "diagnostic_output/" +
        name +
        ".txt"
    );

    if (!out) {
        return;
    }

    if (text) {
        out << text;
    }
}

// =============================================================================
// Save processed buffer
// =============================================================================

static void save_buffer(
    const std::string& name,
    const FinProcessedBuffer* buffer
) {
    if (!buffer ||
        !buffer->data) {
        return;
    }

    fs::create_directories(
        "diagnostic_output"
    );

    const std::string base =
        "diagnostic_output/" + name;

    if (buffer->channels == 1) {

        std::ofstream out(
            base + ".pgm",
            std::ios::binary
        );

        out
            << "P5\n"
            << buffer->width
            << ' '
            << buffer->height
            << "\n255\n";

        out.write(
            reinterpret_cast<
                const char*
            >(buffer->data),
            static_cast<
                std::streamsize
            >(buffer->data_len)
        );

    } else if (buffer->channels == 3) {

        std::ofstream out(
            base + ".ppm",
            std::ios::binary
        );

        out
            << "P6\n"
            << buffer->width
            << ' '
            << buffer->height
            << "\n255\n";

        out.write(
            reinterpret_cast<
                const char*
            >(buffer->data),
            static_cast<
                std::streamsize
            >(buffer->data_len)
        );
    }
}

// =============================================================================
// Print buffer statistics
// =============================================================================

static void print_buffer_stats(
    const FinProcessedBuffer* buffer
) {
    if (!buffer ||
        !buffer->data) {

        std::cout
            << "[BUFFER] NULL\n";

        return;
    }

    std::size_t zero = 0;
    std::size_t nonzero = 0;

    uint8_t min_value = 255;
    uint8_t max_value = 0;

    for (std::size_t i = 0;
         i < buffer->data_len;
         ++i) {

        const uint8_t v =
            buffer->data[i];

        min_value =
            std::min(
                min_value,
                v
            );

        max_value =
            std::max(
                max_value,
                v
            );

        if (v == 0) {
            ++zero;
        } else {
            ++nonzero;
        }
    }

    std::cout
        << "[BUFFER]\n"
        << "  dimensions   : "
        << buffer->width
        << "x"
        << buffer->height
        << "\n"
        << "  channels     : "
        << buffer->channels
        << "\n"
        << "  bytes        : "
        << buffer->data_len
        << "\n"
        << "  binarized    : "
        << buffer->is_binarized
        << "\n"
        << "  zero         : "
        << zero
        << "\n"
        << "  nonzero      : "
        << nonzero
        << "\n"
        << "  min          : "
        << static_cast<int>(min_value)
        << "\n"
        << "  max          : "
        << static_cast<int>(max_value)
        << "\n";
}

// =============================================================================
// OCR payload extraction
// =============================================================================

static std::string extract_scored_ocr_text(
    const std::string& raw
) {
    constexpr const char* MODEL_FREE =
        "[EXTRACTED_MODEL_FREE_OCR]";

    constexpr const char* CHART_LABELS =
        "[EXTRACTED_CHART_LABELS]";

    const std::size_t model_pos =
        raw.find(MODEL_FREE);

    if (model_pos != std::string::npos) {

        std::size_t start =
            model_pos + std::string(MODEL_FREE).size();

        while (start < raw.size() &&
               (raw[start] == '\n' ||
                raw[start] == '\r' ||
                raw[start] == ' ')) {
            ++start;
        }

        const std::size_t next_section =
            raw.find(
                "[",
                start
            );

        if (next_section != std::string::npos) {
            return raw.substr(
                start,
                next_section - start
            );
        }

        return raw.substr(start);
    }

    const std::size_t chart_pos =
        raw.find(CHART_LABELS);

    if (chart_pos != std::string::npos) {

        std::size_t start =
            chart_pos + std::string(CHART_LABELS).size();

        while (start < raw.size() &&
               (raw[start] == '\n' ||
                raw[start] == '\r')) {
            ++start;
        }

        const std::size_t next_section =
            raw.find(
                "[",
                start
            );

        if (next_section != std::string::npos) {
            return raw.substr(
                start,
                next_section - start
            );
        }

        return raw.substr(start);
    }

    return raw;
}

// =============================================================================
// Normalize text for scoring
// =============================================================================

static std::string normalize_text(
    const std::string& input
) {
    std::string result;

    result.reserve(
        input.size()
    );

    bool previous_space = false;

    for (unsigned char ch :
         input) {

        if (std::isspace(ch)) {

            if (!previous_space) {
                result.push_back(' ');
            }

            previous_space = true;

        } else {

            result.push_back(
                static_cast<char>(
                    std::tolower(ch)
                )
            );

            previous_space = false;
        }
    }

    while (!result.empty() &&
           result.front() == ' ') {

        result.erase(
            result.begin()
        );
    }

    while (!result.empty() &&
           result.back() == ' ') {

        result.pop_back();
    }

    return result;
}

// =============================================================================
// Generic Levenshtein distance
// =============================================================================

static std::size_t levenshtein_distance(
    const std::string& a,
    const std::string& b
) {
    if (a.empty()) {
        return b.size();
    }

    if (b.empty()) {
        return a.size();
    }

    const std::string* shorter = &a;
    const std::string* longer = &b;

    if (a.size() > b.size()) {
        shorter = &b;
        longer = &a;
    }

    std::vector<std::size_t> previous(
        shorter->size() + 1
    );

    std::vector<std::size_t> current(
        shorter->size() + 1
    );

    for (std::size_t j = 0;
         j <= shorter->size();
         ++j) {

        previous[j] = j;
    }

    for (std::size_t i = 1;
         i <= longer->size();
         ++i) {

        current[0] = i;

        for (std::size_t j = 1;
             j <= shorter->size();
             ++j) {

            const std::size_t substitution =
                previous[j - 1] +
                (
                    (*longer)[i - 1] ==
                    (*shorter)[j - 1]
                        ? 0
                        : 1
                );

            const std::size_t insertion =
                current[j - 1] + 1;

            const std::size_t deletion =
                previous[j] + 1;

            current[j] =
                std::min({
                    substitution,
                    insertion,
                    deletion
                });
        }

        previous.swap(
            current
        );
    }

    return previous[
        shorter->size()
    ];
}

// =============================================================================
// Word tokenization
// =============================================================================

static std::vector<std::string> split_words(
    const std::string& text
) {
    std::vector<std::string> words;

    std::string current;

    for (unsigned char ch :
         text) {

        if (std::isspace(ch)) {

            if (!current.empty()) {

                words.push_back(
                    std::move(current)
                );

                current.clear();
            }

        } else {

            current.push_back(
                static_cast<char>(ch)
            );
        }
    }

    if (!current.empty()) {
        words.push_back(
            std::move(current)
        );
    }

    return words;
}

// =============================================================================
// Token Levenshtein distance
// =============================================================================

static std::size_t token_levenshtein_distance(
    const std::vector<std::string>& a,
    const std::vector<std::string>& b
) {
    if (a.empty()) {
        return b.size();
    }

    if (b.empty()) {
        return a.size();
    }

    const std::vector<std::string>* shorter = &a;
    const std::vector<std::string>* longer = &b;

    if (a.size() > b.size()) {
        shorter = &b;
        longer = &a;
    }

    std::vector<std::size_t> previous(
        shorter->size() + 1
    );

    std::vector<std::size_t> current(
        shorter->size() + 1
    );

    for (std::size_t j = 0;
         j <= shorter->size();
         ++j) {

        previous[j] = j;
    }

    for (std::size_t i = 1;
         i <= longer->size();
         ++i) {

        current[0] = i;

        for (std::size_t j = 1;
             j <= shorter->size();
             ++j) {

            const std::size_t substitution =
                previous[j - 1] +
                (
                    (*longer)[i - 1] ==
                    (*shorter)[j - 1]
                        ? 0
                        : 1
                );

            const std::size_t insertion =
                current[j - 1] + 1;

            const std::size_t deletion =
                previous[j] + 1;

            current[j] =
                std::min({
                    substitution,
                    insertion,
                    deletion
                });
        }

        previous.swap(
            current
        );
    }

    return previous[
        shorter->size()
    ];
}

// =============================================================================
// Numeric token extraction
// =============================================================================

static bool is_numeric_character(
    char c
) {
    return
        std::isdigit(
            static_cast<unsigned char>(c)
        ) ||
        c == ',' ||
        c == '.' ||
        c == '%' ||
        c == '-' ||
        c == '$';
}

static bool contains_digit(
    const std::string& token
) {
    for (unsigned char c :
         token) {

        if (std::isdigit(c)) {
            return true;
        }
    }

    return false;
}

static std::string normalize_numeric_token(
    const std::string& token
) {
    std::string result;

    result.reserve(
        token.size()
    );

    for (char c : token) {

        if (std::isdigit(
                static_cast<unsigned char>(c)
            )) {

            result.push_back(c);

        } else if (c == '.' ||
                   c == '-' ||
                   c == '%') {

            result.push_back(c);
        }
    }

    return result;
}

static std::vector<std::string> extract_numeric_tokens(
    const std::string& text
) {
    std::vector<std::string> tokens;

    std::string current;

    for (std::size_t i = 0;
         i <= text.size();
         ++i) {

        const char c =
            i < text.size()
                ? text[i]
                : ' ';

        if (is_numeric_character(c)) {

            current.push_back(c);

        } else {

            if (!current.empty() &&
                contains_digit(current)) {

                const std::string normalized =
                    normalize_numeric_token(
                        current
                    );

                if (!normalized.empty()) {

                    tokens.push_back(
                        normalized
                    );
                }
            }

            current.clear();
        }
    }

    return tokens;
}

// =============================================================================
// Accuracy report
// =============================================================================

struct AccuracyReport {
    bool available = false;

    std::size_t expected_chars = 0;
    std::size_t predicted_chars = 0;
    std::size_t char_errors = 0;

    std::size_t expected_words = 0;
    std::size_t predicted_words = 0;
    std::size_t word_errors = 0;

    std::size_t expected_numeric = 0;
    std::size_t predicted_numeric = 0;
    std::size_t numeric_errors = 0;

    std::size_t exact_numeric_matches = 0;

    double cer = 0.0;
    double wer = 0.0;
    double numeric_accuracy = 0.0;
};

static AccuracyReport calculate_accuracy(
    const std::string& expected_raw,
    const std::string& predicted_raw
) {
    AccuracyReport report;

    if (expected_raw.empty()) {
        return report;
    }

    report.available = true;

    const std::string expected =
        normalize_text(
            expected_raw
        );

    const std::string predicted =
        normalize_text(
            predicted_raw
        );

    // -------------------------------------------------------------------------
    // Character-level
    // -------------------------------------------------------------------------

    report.expected_chars =
        expected.size();

    report.predicted_chars =
        predicted.size();

    report.char_errors =
        levenshtein_distance(
            expected,
            predicted
        );

    if (report.expected_chars > 0) {

        report.cer =
            static_cast<double>(
                report.char_errors
            ) /
            static_cast<double>(
                report.expected_chars
            );
    }

    // -------------------------------------------------------------------------
    // Word-level
    // -------------------------------------------------------------------------

    const auto expected_words =
        split_words(
            expected
        );

    const auto predicted_words =
        split_words(
            predicted
        );

    report.expected_words =
        expected_words.size();

    report.predicted_words =
        predicted_words.size();

    report.word_errors =
        token_levenshtein_distance(
            expected_words,
            predicted_words
        );

    if (report.expected_words > 0) {

        report.wer =
            static_cast<double>(
                report.word_errors
            ) /
            static_cast<double>(
                report.expected_words
            );
    }

    // -------------------------------------------------------------------------
    // Numeric accuracy
    // -------------------------------------------------------------------------

    const auto expected_numeric =
        extract_numeric_tokens(
            expected
        );

    const auto predicted_numeric =
        extract_numeric_tokens(
            predicted
        );

    report.expected_numeric =
        expected_numeric.size();

    report.predicted_numeric =
        predicted_numeric.size();

    const std::size_t common =
        std::min(
            expected_numeric.size(),
            predicted_numeric.size()
        );

    for (std::size_t i = 0;
         i < common;
         ++i) {

        if (expected_numeric[i] ==
            predicted_numeric[i]) {

            ++report.exact_numeric_matches;
        }
    }

    report.numeric_errors =
        token_levenshtein_distance(
            expected_numeric,
            predicted_numeric
        );

    if (report.expected_numeric > 0) {

        report.numeric_accuracy =
            static_cast<double>(
                report.exact_numeric_matches
            ) /
            static_cast<double>(
                report.expected_numeric
            );
    }

    return report;
}

// =============================================================================
// Print accuracy
// =============================================================================

static void print_accuracy_report(
    const std::string& name,
    const std::string& ground_truth_path,
    const std::string& predicted_raw
) {
    std::cout
        << "\n============================================================\n"
        << "OCR ACCURACY: "
        << name
        << "\n"
        << "============================================================\n";

    const std::string ground_truth =
        read_text_file(
            ground_truth_path
        );

    if (ground_truth.empty()) {

        std::cout
            << "[ACCURACY] UNAVAILABLE\n"
            << "  ground truth file missing/empty:\n"
            << "  "
            << ground_truth_path
            << "\n"
            << "\n"
            << "Create that file with the exact expected transcription.\n";

        return;
    }

    const std::string predicted =
        extract_scored_ocr_text(
            predicted_raw
        );

    const AccuracyReport report =
        calculate_accuracy(
            ground_truth,
            predicted
        );

    if (!report.available) {
        return;
    }

    std::cout
        << std::fixed
        << std::setprecision(2);

    std::cout
        << "[GROUND TRUTH]\n"
        << "  file             : "
        << ground_truth_path
        << "\n"
        << "  characters       : "
        << report.expected_chars
        << "\n"
        << "  words            : "
        << report.expected_words
        << "\n"
        << "  numeric tokens   : "
        << report.expected_numeric
        << "\n";

    std::cout
        << "[PREDICTION]\n"
        << "  characters       : "
        << report.predicted_chars
        << "\n"
        << "  words            : "
        << report.predicted_words
        << "\n"
        << "  numeric tokens   : "
        << report.predicted_numeric
        << "\n";

    std::cout
        << "[CER]\n"
        << "  errors           : "
        << report.char_errors
        << "\n"
        << "  CER              : "
        << (report.cer * 100.0)
        << "%\n"
        << "  Character Acc.   : "
        << std::max(
            0.0,
            100.0 -
                report.cer * 100.0
        )
        << "%\n";

    std::cout
        << "[WER]\n"
        << "  errors           : "
        << report.word_errors
        << "\n"
        << "  WER              : "
        << (report.wer * 100.0)
        << "%\n"
        << "  Word Acc.        : "
        << std::max(
            0.0,
            100.0 -
                report.wer * 100.0
        )
        << "%\n";

    std::cout
        << "[NUMERIC ACCURACY]\n"
        << "  exact matches    : "
        << report.exact_numeric_matches
        << " / "
        << report.expected_numeric
        << "\n"
        << "  accuracy         : "
        << (report.numeric_accuracy * 100.0)
        << "%\n"
        << "  edit errors      : "
        << report.numeric_errors
        << "\n";

    // -------------------------------------------------------------------------
    // Preview
    // -------------------------------------------------------------------------

    const std::string normalized_expected =
        normalize_text(
            ground_truth
        );

    const std::string normalized_predicted =
        normalize_text(
            predicted
        );

    std::cout
        << "\n[EXPECTED PREVIEW]\n"
        << normalized_expected.substr(
               0,
               std::min<std::size_t>(
                   500,
                   normalized_expected.size()
               )
           )
        << "\n";

    std::cout
        << "\n[PREDICTED PREVIEW]\n"
        << normalized_predicted.substr(
               0,
               std::min<std::size_t>(
                   500,
                   normalized_predicted.size()
               )
           )
        << "\n";

    // Save normalized comparison material for later diagnosis.

    fs::create_directories(
        "diagnostic_output"
    );

    {
        std::ofstream out(
            "diagnostic_output/" +
            name +
            "_accuracy_expected.txt"
        );

        if (out) {
            out << normalized_expected;
        }
    }

    {
        std::ofstream out(
            "diagnostic_output/" +
            name +
            "_accuracy_predicted.txt"
        );

        if (out) {
            out << normalized_predicted;
        }
    }
}

// =============================================================================
// Run one dataset
// =============================================================================

static void diagnose_dataset(
    const std::string& name,
    const std::string& path,
    const std::string& ground_truth_path,
    FinInputType input_type,
    std::size_t width,
    std::size_t height,
    std::size_t channels
) {
    std::cout
        << "\n============================================================\n"
        << "DATASET: "
        << name
        << "\n"
        << "FILE: "
        << path
        << "\n"
        << "============================================================\n";

    const auto bytes =
        read_file(path);

    if (bytes.empty()) {

        std::cout
            << "[ERROR] Could not read file.\n";

        return;
    }

    std::cout
        << "[INPUT]\n"
        << "  bytes        : "
        << bytes.size()
        << "\n"
        << "  target       : "
        << width
        << "x"
        << height
        << "x"
        << channels
        << "\n"
        << "  ground truth : "
        << ground_truth_path
        << "\n";

    FinOcrEngineContext* engine =
        fin_engine_create();

    if (!engine) {

        std::cout
            << "[ERROR] fin_engine_create() failed.\n";

        return;
    }

    FinProcessedBuffer* buffer =
        fin_process_document_bytes(
            engine,
            bytes.data(),
            bytes.size(),
            input_type,
            width,
            height,
            channels
        );

    if (!buffer) {

        std::cout
            << "[ERROR] fin_process_document_bytes() returned NULL.\n";

        fin_engine_destroy(
            engine
        );

        return;
    }

    print_buffer_stats(
        buffer
    );

    const std::string buffer_name =
        name + "_processed";

    save_buffer(
        buffer_name,
        buffer
    );

    std::cout
        << "[SAVED]\n"
        << "  diagnostic_output/"
        << buffer_name
        << "\n";

    char* text =
        fin_engine_recognize_text(
            engine,
            buffer,
            bytes.data(),
            bytes.size(),
            input_type
        );

    std::cout
        << "\n============================================================\n"
        << "OCR OUTPUT: "
        << name
        << "\n"
        << "============================================================\n";

    if (!text) {

        std::cout
            << "[OCR] NULL\n";

        print_accuracy_report(
            name,
            ground_truth_path,
            {}
        );

    } else {

        std::cout
            << text
            << "\n";

        save_text(
            name + "_ocr",
            text
        );

        std::cout
            << "[SAVED] diagnostic_output/"
            << name
            << "_ocr.txt\n";

        // ---------------------------------------------------------------------
        // Actual OCR accuracy measurement
        // ---------------------------------------------------------------------

        print_accuracy_report(
            name,
            ground_truth_path,
            std::string(text)
        );
    }

    std::cout
        << "============================================================\n";

    fin_free_string(
        text
    );

    fin_free_processed_buffer(
        buffer
    );

    fin_engine_destroy(
        engine
    );
}

// =============================================================================
// Main
// =============================================================================

int main() {

    std::cout
        << "=============================================\n"
        << " FINTALLY Dataset OCR Diagnostic\n"
        << "=============================================\n";

    // =========================================================================
    // PDF
    // =========================================================================

    diagnose_dataset(
        "pdf_balance_sheet",
        "data/financial_balance_sheet_image.pdf",
        "data/ground_truth/pdf_balance_sheet.txt",
        FIN_INPUT_PDF_PAGE,
        1920,
        1080,
        1
    );

    // =========================================================================
    // Financial chart (Chart Pipeline)
    // =========================================================================

    diagnose_dataset(
        "financial_chart",
        "data/financial_chart_test_image.webp",
        "data/ground_truth/financial_chart.txt",
        FIN_INPUT_FIN_CHART,
        1280,
        720,
        3
    );

    // =========================================================================
    // Financial chart (Raw Image Pipeline)
    // =========================================================================

    diagnose_dataset(
        "financial_chart_as_raw",
        "data/financial_chart_test_image.webp",
        "data/ground_truth/financial_chart.txt",
        FIN_INPUT_RAW_IMAGE,
        1280,
        720,
        3
    );

    // =========================================================================
    // Receipt
    // =========================================================================

    diagnose_dataset(
        "receipt",
        "data/financial_receipt_image.jpg",
        "data/ground_truth/receipt.txt",
        FIN_INPUT_RAW_IMAGE,
        1024,
        1024,
        3
    );

    // =========================================================================
    // Multi Receipt
    // =========================================================================

    diagnose_dataset(
        "multi_receipt",
        "data/financial_mulit_receipt_image.webp",
        "data/ground_truth/multi_receipt.txt",
        FIN_INPUT_RAW_IMAGE,
        1024,
        1024,
        3
    );

    std::cout
        << "\n=============================================\n"
        << "Diagnostic complete\n"
        << "Outputs: ./diagnostic_output/\n"
        << "=============================================\n";

    return 0;
}
