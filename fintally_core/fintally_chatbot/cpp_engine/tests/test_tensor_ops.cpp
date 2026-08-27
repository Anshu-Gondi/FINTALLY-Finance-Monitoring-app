#include "tensor_ops.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>

namespace {

void reference_binarize(std::vector<std::uint8_t>& data) {
    for (auto& x : data) {
        x = (x > 128u) ? 255u : 0u;
    }
}

void reference_contrast(std::vector<std::uint8_t>& data) {
    for (auto& x : data) {
        const std::uint32_t value =
            static_cast<std::uint32_t>(x) * 294u;

        const std::uint32_t result = value >> 8u;

        x = static_cast<std::uint8_t>(
            result > 255u ? 255u : result
        );
    }
}

std::vector<std::uint8_t> make_random_data(std::size_t size) {
    std::vector<std::uint8_t> data(size);

    std::mt19937 rng(0xF1A0C0DEu);
    std::uniform_int_distribution<unsigned> dist(0, 255);

    for (auto& x : data) {
        x = static_cast<std::uint8_t>(dist(rng));
    }

    return data;
}

TEST(TensorOps, NullAndEmptyAreSafe) {
    EXPECT_NO_THROW(fin::ops::binarize_simd(nullptr, 0));
    EXPECT_NO_THROW(fin::ops::contrast_boost_simd(nullptr, 0));

    std::uint8_t value = 123;

    EXPECT_NO_THROW(fin::ops::binarize_simd(&value, 0));
    EXPECT_NO_THROW(fin::ops::contrast_boost_simd(&value, 0));

    EXPECT_EQ(value, 123);
}

TEST(TensorOps, AlignmentHelper) {
    constexpr std::size_t alignment = 64;

    void* ptr = fin::ops::aligned_alloc(alignment, 4096);

    ASSERT_NE(ptr, nullptr);
    EXPECT_TRUE(fin::ops::is_aligned(ptr, alignment));

    fin::ops::aligned_free(ptr);
}

TEST(TensorOps, BinarizationBoundaryValues) {
    std::vector<std::uint8_t> data{
        0, 1, 127, 128, 129, 254, 255
    };

    const std::vector<std::uint8_t> expected{
        0, 0, 0, 0, 255, 255, 255
    };

    fin::ops::binarize_simd(data.data(), data.size());

    EXPECT_EQ(data, expected);
}

TEST(TensorOps, ContrastBoundaryValues) {
    std::vector<std::uint8_t> data{
        0, 1, 127, 128, 200, 220, 254, 255
    };

    auto expected = data;
    reference_contrast(expected);

    fin::ops::contrast_boost_simd(data.data(), data.size());

    EXPECT_EQ(data, expected);
}

TEST(TensorOps, BinarizationMatchesScalarReference) {
    constexpr std::size_t sizes[] = {
        1, 2, 7, 15, 16, 17,
        31, 32, 33,
        63, 64, 65,
        127, 128, 129,
        255, 256, 257,
        1023, 1024, 1025,
        4095, 4096, 4097,
        10000
    };

    for (const auto size : sizes) {
        auto actual = make_random_data(size);
        auto expected = actual;

        reference_binarize(expected);

        fin::ops::binarize_simd(actual.data(), actual.size());

        ASSERT_EQ(actual, expected)
            << "Mismatch at size=" << size;
    }
}

TEST(TensorOps, ContrastMatchesScalarReference) {
    constexpr std::size_t sizes[] = {
        1, 2, 7, 15, 16, 17,
        31, 32, 33,
        63, 64, 65,
        127, 128, 129,
        255, 256, 257,
        1023, 1024, 1025,
        4095, 4096, 4097,
        10000
    };

    for (const auto size : sizes) {
        auto actual = make_random_data(size);
        auto expected = actual;

        reference_contrast(expected);

        fin::ops::contrast_boost_simd(actual.data(), actual.size());

        ASSERT_EQ(actual, expected)
            << "Mismatch at size=" << size;
    }
}

TEST(TensorOps, BinarizationWorksWithUnalignedPointer) {
    constexpr std::size_t size = 4097;

    auto storage = make_random_data(size + 65);

    auto* data = storage.data() + 1;

    std::vector<std::uint8_t> expected(data, data + size);

    reference_binarize(expected);

    fin::ops::binarize_simd(data, size);

    ASSERT_TRUE(std::equal(
        data,
        data + size,
        expected.begin()
    ));
}

TEST(TensorOps, ContrastWorksWithUnalignedPointer) {
    constexpr std::size_t size = 4097;

    auto storage = make_random_data(size + 65);

    auto* data = storage.data() + 1;

    std::vector<std::uint8_t> expected(data, data + size);

    reference_contrast(expected);

    fin::ops::contrast_boost_simd(data, size);

    ASSERT_TRUE(std::equal(
        data,
        data + size,
        expected.begin()
    ));
}

TEST(TensorOps, BinarizationWorksWithLargeBuffer) {
    constexpr std::size_t size = 8 * 1024 * 1024;

    auto actual = make_random_data(size);
    auto expected = actual;

    reference_binarize(expected);

    fin::ops::binarize_simd(actual.data(), actual.size());

    EXPECT_EQ(actual, expected);
}

TEST(TensorOps, ContrastWorksWithLargeBuffer) {
    constexpr std::size_t size = 8 * 1024 * 1024;

    auto actual = make_random_data(size);
    auto expected = actual;

    reference_contrast(expected);

    fin::ops::contrast_boost_simd(actual.data(), actual.size());

    EXPECT_EQ(actual, expected);
}

} // namespace
