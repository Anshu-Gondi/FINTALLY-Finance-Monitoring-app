#include <benchmark/benchmark.h>
#include "ffi_bridge.h"
#include <fstream>
#include <vector>
#include <string>

namespace {

std::vector<uint8_t> load_dataset_file(const std::string& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) return {};

    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    file.read(reinterpret_cast<char*>(buffer.data()), size);
    return buffer;
}

} // anonymous namespace

static void BM_Dataset_PDF_BalanceSheet(benchmark::State& state) {
    auto bytes = load_dataset_file("data/financial_balance_sheet_image.pdf");
    if (bytes.empty()) {
        state.SkipWithError("Could not read PDF test dataset file.");
        return;
    }

    FinOcrEngineContext* engine = fin_engine_create();

    for (auto _ : state) {
        FinProcessedBuffer* buf = fin_process_document_bytes(
            engine, bytes.data(), bytes.size(), FIN_INPUT_PDF_PAGE, 1920, 1080, 1
        );
        benchmark::DoNotOptimize(buf);
        fin_free_processed_buffer(buf);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * bytes.size()));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    fin_engine_destroy(engine);
}
BENCHMARK(BM_Dataset_PDF_BalanceSheet);

static void BM_Dataset_Webp_FinancialChart(benchmark::State& state) {
    auto bytes = load_dataset_file("data/financial_chart_test_image.webp");
    if (bytes.empty()) {
        state.SkipWithError("Could not read chart dataset file.");
        return;
    }

    FinOcrEngineContext* engine = fin_engine_create();

    for (auto _ : state) {
        FinProcessedBuffer* buf = fin_process_document_bytes(
            engine, bytes.data(), bytes.size(), FIN_INPUT_FIN_CHART, 1280, 720, 3
        );
        benchmark::DoNotOptimize(buf);
        fin_free_processed_buffer(buf);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * bytes.size()));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    fin_engine_destroy(engine);
}
BENCHMARK(BM_Dataset_Webp_FinancialChart);

static void BM_Dataset_Jpg_Receipt(benchmark::State& state) {
    auto bytes = load_dataset_file("data/financial_receipt_image.jpg");
    if (bytes.empty()) {
        state.SkipWithError("Could not read receipt dataset file.");
        return;
    }

    FinOcrEngineContext* engine = fin_engine_create();

    for (auto _ : state) {
        FinProcessedBuffer* buf = fin_process_document_bytes(
            engine, bytes.data(), bytes.size(), FIN_INPUT_RAW_IMAGE, 1024, 1024, 3
        );
        benchmark::DoNotOptimize(buf);
        fin_free_processed_buffer(buf);
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations() * bytes.size()));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations()));
    fin_engine_destroy(engine);
}
BENCHMARK(BM_Dataset_Jpg_Receipt);

BENCHMARK_MAIN();
