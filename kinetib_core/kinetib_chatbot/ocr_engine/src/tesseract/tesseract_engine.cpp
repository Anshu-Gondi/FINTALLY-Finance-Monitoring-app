#include "fin_ocr/tesseract/tesseract_engine.hpp"
#include "fin_ocr/tesseract/tesseract_preprocess.hpp"

#include "fin_ocr/core/ocr_config.hpp"

#include <tesseract/baseapi.h>

#include <utility>

namespace fin_ocr {

// =============================================================================
// PRIVATE TESSERACT IMPLEMENTATION
// =============================================================================
//
// TessBaseAPI is stateful and must not be exposed through the public header.
//
// The public TesseractEngine uses PImpl so that Tesseract headers/types remain
// private to this translation unit.
//
// =============================================================================

struct TesseractEngine::Impl {

    tesseract::TessBaseAPI api;

    bool initialized = false;

    Impl()
    {
        const int rc =
            api.Init(
                nullptr,
                "eng",
                tesseract::OEM_LSTM_ONLY
            );

        if (rc != 0) {
            return;
        }

        initialized = true;

        api.SetVariable(
            "preserve_interword_spaces",
            "1"
        );

        api.SetVariable(
            "classify_enable_learning",
            "0"
        );

        api.SetVariable(
            "user_defined_dpi",
            "300"
        );
    }

    ~Impl()
    {
        if (initialized) {
            api.End();
        }
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

// =============================================================================
// CONSTRUCTOR
// =============================================================================

TesseractEngine::TesseractEngine()
    : impl_(
          std::make_unique<Impl>()
      )
{
}

// =============================================================================
// DESTRUCTOR
// =============================================================================

TesseractEngine::~TesseractEngine() = default;

// =============================================================================
// MOVE CONSTRUCTOR
// =============================================================================

TesseractEngine::TesseractEngine(
    TesseractEngine&& other
) noexcept = default;

// =============================================================================
// MOVE ASSIGNMENT
// =============================================================================

TesseractEngine& TesseractEngine::operator=(
    TesseractEngine&& other
) noexcept = default;

// =============================================================================
// INITIALIZATION STATUS
// =============================================================================

bool TesseractEngine::initialized() const noexcept
{
    return
        impl_ != nullptr &&
        impl_->initialized;
}

// =============================================================================
// SINGLE TESSERACT RECOGNITION PASS
// =============================================================================

bool TesseractEngine::recognize(
    const std::vector<uint8_t>& gray,
    int width,
    int height,
    int psm,
    bool numeric_mode,
    std::string& output,
    float& confidence
) {

    output.clear();
    confidence = 0.0f;

    if (
        !initialized() ||
        gray.empty() ||
        width <= 0 ||
        height <= 0
    ) {
        return false;
    }

    impl_->api.Clear();

    // =========================================================================
    // IMAGE FIRST
    // =========================================================================

    impl_->api.SetImage(
        gray.data(),
        width,
        height,
        1,
        width
    );

    // =========================================================================
    // RESOLUTION AFTER IMAGE
    // =========================================================================

    impl_->api.SetSourceResolution(
        config::TESSERACT_SOURCE_DPI
    );

    impl_->api.SetPageSegMode(
        static_cast<
            tesseract::PageSegMode
        >(psm)
    );

    impl_->api.SetVariable(
        "preserve_interword_spaces",
        "1"
    );

    impl_->api.SetVariable(
        "classify_enable_learning",
        "0"
    );

    // =========================================================================
    // NUMERIC WHITELIST
    // =========================================================================

    if (numeric_mode) {

        impl_->api.SetVariable(
            "tessedit_char_whitelist",
            "0123456789.,-+$%()/₹"
        );

    } else {

        impl_->api.SetVariable(
            "tessedit_char_whitelist",
            ""
        );
    }

    // =========================================================================
    // RECOGNIZE
    // =========================================================================

    if (
        impl_->api.Recognize(
            nullptr
        ) != 0
    ) {

        impl_->api.Clear();

        return false;
    }

    confidence =
        static_cast<float>(
            impl_->api.MeanTextConf()
        );

    char* raw =
        impl_->api.GetUTF8Text();

    if (raw == nullptr) {

        impl_->api.Clear();

        return false;
    }

    // We intentionally do the text cleanup in the recognizer/preprocess layer,
    // so the engine itself only owns Tesseract interaction.
    //
    // For now, return the raw UTF-8 result through a minimal normalization
    // here. The higher-level recognizer can still apply its own cleanup.
    output = raw;

    delete[] raw;

    if (output.empty()) {

        impl_->api.Clear();

        return false;
    }

    return true;
}

} // namespace fin_ocr
