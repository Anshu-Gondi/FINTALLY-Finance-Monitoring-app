#pragma once

#include <cstdint>
#include <string>

namespace fin_ocr {

class LineRecognizer;

class ChartLabelRecognizer {
public:
    explicit ChartLabelRecognizer(
        const LineRecognizer& line_recognizer
    ) noexcept;

    [[nodiscard]]
    std::string recognize(
        const uint8_t* chart_buffer,
        int width,
        int height
    ) const;

private:
    const LineRecognizer& line_recognizer_;
};

} // namespace fin_ocr
