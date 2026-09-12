#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fin_ocr {

class TesseractEngine {
public:
    TesseractEngine();
    ~TesseractEngine();

    TesseractEngine(
        const TesseractEngine&
    ) = delete;

    TesseractEngine& operator=(
        const TesseractEngine&
    ) = delete;

    TesseractEngine(
        TesseractEngine&&
    ) noexcept;

    TesseractEngine& operator=(
        TesseractEngine&&
    ) noexcept;

    [[nodiscard]]
    bool initialized() const noexcept;

    [[nodiscard]]
    bool recognize(
        const std::vector<uint8_t>& gray,
        int width,
        int height,
        int psm,
        bool numeric_mode,
        std::string& output,
        float& confidence
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace fin_ocr
