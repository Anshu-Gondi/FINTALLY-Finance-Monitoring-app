#include "matrix_matcher.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <random>
#include <string>
#include <vector>
#include <utility>

using namespace fin_ocr;

using Clock = std::chrono::steady_clock;

static volatile char g_sink = '?';

struct BenchmarkResult {
    double total_ms;
    double avg_us;
    double throughput;
};

static BenchmarkResult benchmark_glyph(
    MatrixMatcher& matcher,
    const std::vector<uint8_t>& patch,
    int w,
    int h,
    std::size_t iterations
) {
    for (int i = 0; i < 1000; ++i) {
        g_sink = matcher.match_glyph(patch, w, h);
    }

    const auto start = Clock::now();

    for (std::size_t i = 0; i < iterations; ++i) {
        g_sink = matcher.match_glyph(patch, w, h);
    }

    const auto end = Clock::now();

    const double total_ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    const double avg_us =
        (total_ms * 1000.0) /
        static_cast<double>(iterations);

    const double throughput =
        static_cast<double>(iterations) /
        (total_ms / 1000.0);

    return {total_ms, avg_us, throughput};
}

static std::vector<uint8_t> make_test_patch(
    int w,
    int h,
    std::uint32_t seed,
    int noise = 0
) {
    std::vector<uint8_t> image(
        static_cast<std::size_t>(w) *
        static_cast<std::size_t>(h),
        0
    );

    std::mt19937 rng(seed);

    if (noise > 0) {
        std::uniform_int_distribution<int> dist(0, noise);
        for (auto& p : image) {
            p = static_cast<uint8_t>(dist(rng));
        }
    }

    return image;
}

// -----------------------------------------------------------------------------
// Synthetic glyphs used by the correctness and line-recognition benchmark.
// These are actual glyph shapes, not hollow rectangles, so recognize_line()
// measures the OCR path rather than a benchmark artifact.
// -----------------------------------------------------------------------------

static std::vector<uint8_t> make_digit_0(
    int w = 16,
    int h = 16
) {
    std::vector<uint8_t> img(
        static_cast<std::size_t>(w) *
        static_cast<std::size_t>(h),
        0
    );

    for (int y = 1; y < h - 1; ++y) {
        for (int x = 1; x < w - 1; ++x) {
            const bool left = x <= 2;
            const bool right = x >= w - 3;
            const bool top = y <= 2;
            const bool bottom = y >= h - 3;

            if (left || right || top || bottom) {
                img[
                    static_cast<std::size_t>(y) *
                    static_cast<std::size_t>(w) +
                    static_cast<std::size_t>(x)
                ] = 255;
            }
        }
    }

    return img;
}

static std::vector<uint8_t> make_digit_1(
    int w = 16,
    int h = 16
) {
    std::vector<uint8_t> img(
        static_cast<std::size_t>(w) *
        static_cast<std::size_t>(h),
        0
    );

    // Small top serif.
    for (int x = 5; x <= 10; ++x) {
        img[
            static_cast<std::size_t>(1) *
            static_cast<std::size_t>(w) +
            static_cast<std::size_t>(x)
        ] = 255;
    }

    // Diagonal shoulder.
    for (int y = 2; y < 5 && y < h; ++y) {
        const int x = 10 - (y - 2);
        if (x >= 0 && x < w) {
            img[
                static_cast<std::size_t>(y) *
                static_cast<std::size_t>(w) +
                static_cast<std::size_t>(x)
            ] = 255;
        }
    }

    // Stem.
    const int stem_x = w / 2;
    for (int y = 2; y < h - 2; ++y) {
        img[
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(w) +
            static_cast<std::size_t>(stem_x)
        ] = 255;
    }

    // Baseline.
    for (int x = 3; x < w - 3; ++x) {
        img[
            static_cast<std::size_t>(h - 2) *
            static_cast<std::size_t>(w) +
            static_cast<std::size_t>(x)
        ] = 255;
    }

    return img;
}

static void draw_glyph(
    std::vector<uint8_t>& image,
    int image_w,
    int image_h,
    int x0,
    int y0,
    const std::vector<uint8_t>& glyph,
    int glyph_w,
    int glyph_h
) {
    for (int y = 0; y < glyph_h; ++y) {
        const int dst_y = y0 + y;

        if (dst_y < 0 || dst_y >= image_h) {
            continue;
        }

        const std::size_t src_row =
            static_cast<std::size_t>(y) *
            static_cast<std::size_t>(glyph_w);

        const std::size_t dst_row =
            static_cast<std::size_t>(dst_y) *
            static_cast<std::size_t>(image_w);

        for (int x = 0; x < glyph_w; ++x) {
            const int dst_x = x0 + x;

            if (dst_x < 0 || dst_x >= image_w) {
                continue;
            }

            if (glyph[
                    src_row +
                    static_cast<std::size_t>(x)
                ] > 100) {

                image[
                    dst_row +
                    static_cast<std::size_t>(dst_x)
                ] = 255;
            }
        }
    }
}

static void print_result(
    const char* name,
    const BenchmarkResult& r
) {
    std::cout
        << std::left
        << std::setw(28)
        << name
        << " total="
        << std::setw(12)
        << std::fixed
        << std::setprecision(3)
        << r.total_ms
        << " ms"
        << " avg="
        << std::setw(10)
        << r.avg_us
        << " us"
        << " throughput="
        << std::fixed
        << std::setprecision(0)
        << r.throughput
        << " glyph/s\n";
}

int main() {
    MatrixMatcher matcher;

    constexpr std::size_t ITERATIONS = 100'000;

    std::cout << "=============================================\n";
    std::cout << " MatrixMatcher Benchmark\n";
    std::cout << "=============================================\n";
    std::cout << "Iterations: " << ITERATIONS << "\n\n";

    // -------------------------------------------------------------------------
    // 1. Basic glyph benchmark
    // -------------------------------------------------------------------------

    const auto glyph0 = make_digit_0();
    const auto glyph1 = make_digit_1();

    const auto r0 = benchmark_glyph(
        matcher, glyph0, 16, 16, ITERATIONS
    );

    const auto r1 = benchmark_glyph(
        matcher, glyph1, 16, 16, ITERATIONS
    );

    print_result("match_glyph 16x16 '0'", r0);
    print_result("match_glyph 16x16 '1'", r1);

    // -------------------------------------------------------------------------
    // 2. Different patch sizes
    // -------------------------------------------------------------------------

    std::cout << "\nPatch-size scaling:\n";

    for (auto [w, h] : {
        std::pair{8, 8},
        std::pair{12, 12},
        std::pair{16, 16},
        std::pair{24, 24},
        std::pair{32, 32},
        std::pair{48, 48},
        std::pair{64, 32}
    }) {
        auto patch = make_test_patch(
            w,
            h,
            static_cast<std::uint32_t>(w * 100 + h)
        );

        const auto result = benchmark_glyph(
            matcher,
            patch,
            w,
            h,
            50'000
        );

        std::string name =
            "match_glyph " +
            std::to_string(w) +
            "x" +
            std::to_string(h);

        print_result(name.c_str(), result);
    }

    // -------------------------------------------------------------------------
    // 3. recognize_line benchmark
    //
    // IMPORTANT:
    // The previous benchmark generated 12x32 hollow rectangles. Those are
    // connected components, but they are not digits, so '?' was the expected
    // result. This version draws real 16x16 synthetic glyphs.
    // -------------------------------------------------------------------------

    constexpr int WIDTH = 512;
    constexpr int HEIGHT = 64;
    constexpr int GLYPH_COUNT = 20;
    constexpr int GLYPH_SPACING = 24;
    constexpr int GLYPH_Y = 24;

    std::vector<uint8_t> line(
        static_cast<std::size_t>(WIDTH) *
        static_cast<std::size_t>(HEIGHT),
        0
    );

    for (int i = 0; i < GLYPH_COUNT; ++i) {
        const int base_x = 5 + i * GLYPH_SPACING;

        const auto& glyph =
            (i % 2 == 0)
                ? glyph0
                : glyph1;

        draw_glyph(
            line,
            WIDTH,
            HEIGHT,
            base_x,
            GLYPH_Y,
            glyph,
            16,
            16
        );
    }

    constexpr std::size_t LINE_ITERATIONS = 5'000;

    for (int i = 0; i < 100; ++i) {
        const auto text = matcher.recognize_line(
            line.data(),
            WIDTH,
            0,
            HEIGHT,
            1
        );

        g_sink = text.empty() ? '?' : text[0];
    }

    const auto line_start = Clock::now();

    std::size_t total_chars = 0;

    for (std::size_t i = 0;
         i < LINE_ITERATIONS;
         ++i) {

        const auto text = matcher.recognize_line(
            line.data(),
            WIDTH,
            0,
            HEIGHT,
            1
        );

        total_chars += text.size();

        if (!text.empty()) {
            g_sink = text[0];
        }
    }

    const auto line_end = Clock::now();

    const double line_ms =
        std::chrono::duration<double, std::milli>(
            line_end - line_start
        ).count();

    const double line_avg_us =
        line_ms * 1000.0 /
        static_cast<double>(LINE_ITERATIONS);

    const double lines_per_sec =
        static_cast<double>(LINE_ITERATIONS) /
        (line_ms / 1000.0);

    const double chars_per_sec =
        static_cast<double>(total_chars) /
        (line_ms / 1000.0);

    std::cout << "\nrecognize_line:\n";
    std::cout << "  total          : " << line_ms << " ms\n";
    std::cout << "  average        : " << line_avg_us << " us/line\n";
    std::cout << "  throughput     : " << lines_per_sec << " lines/sec\n";
    std::cout << "  avg characters : "
              << static_cast<double>(total_chars) /
                 static_cast<double>(LINE_ITERATIONS)
              << "\n";
    std::cout << "  char throughput: "
              << chars_per_sec
              << " chars/sec\n";

    // -------------------------------------------------------------------------
    // 4. recognize_line correctness
    // -------------------------------------------------------------------------

    const std::string expected_line =
        "0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1 0 1";

    const std::string actual_line =
        matcher.recognize_line(
            line.data(),
            WIDTH,
            0,
            HEIGHT,
            1
        );

    std::cout << "\nrecognize_line correctness:\n";
    std::cout << "  expected       : \"" << expected_line << "\"\n";
    std::cout << "  result         : \"" << actual_line << "\"\n";
    std::cout << "  characters     : " << actual_line.size() << "\n";
    std::cout << "  exact match    : "
              << (actual_line == expected_line ? "PASS" : "FAIL")
              << "\n";

    // -------------------------------------------------------------------------
    // 5. Correctness smoke test
    // -------------------------------------------------------------------------

    std::cout << "\nCorrectness smoke test:\n";

    const char result0 = matcher.match_glyph(
        glyph0,
        16,
        16
    );

    const char result1 = matcher.match_glyph(
        glyph1,
        16,
        16
    );

    std::cout << "  synthetic 0 -> " << result0 << "\n";
    std::cout << "  synthetic 1 -> " << result1 << "\n";

    std::cout << "\n=============================================\n";
    std::cout << "Benchmark complete\n";
    std::cout << "=============================================\n";

    return 0;
}
