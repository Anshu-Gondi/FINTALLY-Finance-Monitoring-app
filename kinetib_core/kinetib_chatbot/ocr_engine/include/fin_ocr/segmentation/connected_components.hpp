#pragma once

#include <cstdint>
#include <vector>

#include "fin_ocr/core/ocr_types.hpp"

namespace fin_ocr {

class ConnectedComponents {
public:
    [[nodiscard]]
    static std::vector<BoundingBox> extract(
        const uint8_t* image,
        int width,
        int min_y,
        int max_y,
        int channels
    );
};

} // namespace fin_ocr
