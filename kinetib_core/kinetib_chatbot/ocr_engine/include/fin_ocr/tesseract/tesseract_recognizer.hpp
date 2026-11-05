#pragma once

#include <cstdint>
#include <string>

namespace fin_ocr {

struct TesseractCandidate {
    std::string text;

    float confidence = -1.0f;

    int psm = 7;

    bool numeric_mode = false;

    double score = -1.0;
};

class TesseractRecognizer {
public:
    [[nodiscard]]
    bool recognize_line(
        const uint8_t* image,
        int width,
        int y0,
        int y1,
        int channels,
        std::string& output,
        int& confidence
    ) const;
};

} // namespace fin_ocr
