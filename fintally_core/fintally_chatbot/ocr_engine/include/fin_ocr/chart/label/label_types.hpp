#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "fin_ocr/chart/association/association_types.hpp"

namespace fin_ocr::chart::label {

inline constexpr std::size_t MAX_CANDIDATE_COMPONENTS = 4096u;
inline constexpr double MIN_LABEL_DENSITY =
    0.010;

// =============================================================================
// INTERNAL DETECTED LABEL
// =============================================================================

struct DetectedLabel {

  ::fin_ocr::chart::association::ChartLabel label;

  float density = 0.0f;

  std::size_t glyph_count = 0;
};

// =============================================================================
// OCR REJECTION REASON
// =============================================================================

enum class CandidateRejectReason : std::uint8_t {

  NONE = 0,

  GEOMETRY,
  ZONE,
  ZONE_SCORE,
  SEQUENCE,

  EMPTY_CROP,
  OCR_EMPTY,
  REPEATED_NOISE,
  TEXT_QUALITY,
  GEOMETRY_TEXT,
  DENSITY,
  CONFIDENCE,

  COMPOSITION,
  SINGLE_GLYPH,
  TWO_GLYPH,
  THREE_GLYPH,
  PUNCTUATION,
  OTHER_CHARS,
  NUMERIC_ONLY,
  LONG_GEOMETRY,
  OCCUPANCY
};

// =============================================================================
// TEXT CANDIDATE
// =============================================================================

struct TextCandidate {

  int min_x = 0;
  int min_y = 0;

  int max_x = 0;
  int max_y = 0;

  std::size_t active_pixels = 0;
};

// =============================================================================
// OCR BUFFER
// =============================================================================
//
// One-channel OCR representation.
//
// The dimensions always describe the supplied data buffer.
//
// =============================================================================

struct OcrBuffer {

  std::vector<std::uint8_t> data;

  int width = 0;

  int height = 0;
};

// =============================================================================
// LABEL ZONE
// =============================================================================

enum class LabelZone : std::uint8_t {

  UNKNOWN = 0,

  LEFT_Y_AXIS,
  RIGHT_Y_AXIS,

  CATEGORY_AXIS,

  TITLE,

  PLOT_INTERIOR,

  OUTSIDE
};

} // namespace fin_ocr::chart::label
