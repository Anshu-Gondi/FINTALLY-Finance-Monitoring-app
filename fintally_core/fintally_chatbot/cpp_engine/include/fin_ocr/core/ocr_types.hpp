#pragma once

namespace fin_ocr {

struct BoundingBox {
    int min_x = 0;
    int min_y = 0;
    int max_x = -1;
    int max_y = -1;

    [[nodiscard]]
    int width() const noexcept {
        return max_x - min_x + 1;
    }

    [[nodiscard]]
    int height() const noexcept {
        return max_y - min_y + 1;
    }

    [[nodiscard]]
    int area() const noexcept {
        return width() * height();
    }

    [[nodiscard]]
    static constexpr bool is_digit(
        char c
    ) noexcept {
        return c >= '0' && c <= '9';
    }
};

struct Component {
    BoundingBox box;
};

} // namespace fin_ocr
