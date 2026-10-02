#include "fin_ocr/chart/label/y_axis/y_axis_value_recovery.hpp"

#include "fin_ocr/chart/label/text/text_utils.hpp"
#include "fin_ocr/chart/label/y_axis/y_axis_parser.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fin_ocr::chart::label::y_axis {

namespace {

// =============================================================================
// Y-AXIS VALUE RECOVERY CONFIGURATION
// =============================================================================
//
// Preserved from the legacy ChartLabelRecognizer implementation.
// =============================================================================

constexpr double RIGHT_Y_DECIMAL_RECOVERY_RELATIVE_TOLERANCE = 0.20;

constexpr double RIGHT_Y_DECIMAL_RECOVERY_ABSOLUTE_TOLERANCE = 0.75;

constexpr double RIGHT_Y_MAX_PERCENT_VALUE = 100.0;

constexpr std::size_t MIN_Y_AXIS_VALUE_RECOVERY_ANCHORS = 3u;

constexpr double Y_AXIS_VALUE_RECOVERY_MAX_TICK_ERROR_RATIO = 0.35;

constexpr double Y_AXIS_VALUE_RECOVERY_MIN_SPATIAL_SUPPORT = 0.50;

constexpr double Y_AXIS_VALUE_RECOVERY_MAX_ABSOLUTE_ERROR_RIGHT = 3.0;

constexpr double Y_AXIS_VALUE_RECOVERY_MAX_ABSOLUTE_ERROR_LEFT = 750.0;

constexpr double Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT = 100.0;

constexpr bool CHART_LABEL_DEBUG = true;

// =============================================================================
// MEDIAN
// =============================================================================
//
// Local statistical helper. The legacy implementation used nth_element-based
// median calculation; preserve that behavior without exposing median_value()
// through a public header.
// =============================================================================

[[nodiscard]]
double median_value(std::vector<double> values) noexcept {
  if (values.empty()) {
    return 0.0;
  }

  const std::size_t middle = values.size() / 2u;

  std::nth_element(values.begin(), values.begin() + middle, values.end());

  const double upper = values[middle];

  if (values.size() % 2u != 0u) {
    return upper;
  }

  std::nth_element(values.begin(), values.begin() + middle - 1u, values.end());

  const double lower = values[middle - 1u];

  return (lower + upper) * 0.5;
}

} // namespace

// =============================================================================
// RIGHT Y-AXIS DECIMAL RECOVERY
// =============================================================================

[[nodiscard]]
bool recover_right_y_axis_decimal(
    std::string &text, const std::vector<DetectedLabel> &accepted_labels,
    int candidate_min_y, int candidate_max_y, double &original_value,
    double &recovered_value) noexcept {

  original_value = 0.0;

  recovered_value = 0.0;

  if (text.empty() || accepted_labels.size() < 2u) {

    return false;
  }

  double current_value = 0.0;

  if (!parse_y_axis_numeric_value(text, YAxisKind::RIGHT, current_value)) {

    return false;
  }

  if (!std::isfinite(current_value) || current_value < 0.0 ||
      current_value > RIGHT_Y_MAX_PERCENT_VALUE) {

    return false;
  }

  std::vector<std::pair<double, double>> points;
  points.reserve(accepted_labels.size());

  for (const DetectedLabel &accepted : accepted_labels) {

    double value = 0.0;

    if (!parse_y_axis_numeric_value(accepted.label.text, YAxisKind::RIGHT,
                                    value)) {

      continue;
    }

    if (!std::isfinite(value) || value < 0.0 ||
        value > RIGHT_Y_MAX_PERCENT_VALUE) {

      continue;
    }

    const double center_y = (static_cast<double>(accepted.label.min_y) +
                             static_cast<double>(accepted.label.max_y)) *
                            0.5;

    points.push_back({center_y, value});
  }

  if (points.size() < 2u) {

    return false;
  }

  std::sort(
      points.begin(), points.end(),
      [](const auto &a, const auto &b) noexcept { return a.first < b.first; });

  const double current_center_y = (static_cast<double>(candidate_min_y) +
                                   static_cast<double>(candidate_max_y)) *
                                  0.5;

  double best_pair_distance = std::numeric_limits<double>::max();

  double best_slope = 0.0;

  double best_intercept = 0.0;

  for (std::size_t i = 0u; i < points.size(); ++i) {

    for (std::size_t j = i + 1u; j < points.size(); ++j) {

      const double y0 = points[i].first;

      const double y1 = points[j].first;

      const double dy = y1 - y0;

      if (std::abs(dy) < 1.0e-9) {

        continue;
      }

      const double pair_center = (y0 + y1) * 0.5;

      const double pair_distance = std::abs(current_center_y - pair_center);

      const double slope = (points[j].second - points[i].second) / dy;

      if (!std::isfinite(slope)) {

        continue;
      }

      const double intercept = points[i].second - slope * points[i].first;

      if (!std::isfinite(intercept)) {

        continue;
      }

      if (pair_distance < best_pair_distance) {

        best_pair_distance = pair_distance;

        best_slope = slope;

        best_intercept = intercept;
      }
    }
  }

  if (!std::isfinite(best_pair_distance)) {

    return false;
  }

  const double predicted = best_slope * current_center_y + best_intercept;

  if (!std::isfinite(predicted) || predicted < 0.0 ||
      predicted > RIGHT_Y_MAX_PERCENT_VALUE) {

    return false;
  }

  constexpr double scales[] = {0.1, 0.01, 10.0};

  double best_scale = 1.0;

  double best_error = std::numeric_limits<double>::max();

  for (const double scale : scales) {

    const double candidate = current_value * scale;

    if (!std::isfinite(candidate) || candidate < 0.0 ||
        candidate > RIGHT_Y_MAX_PERCENT_VALUE) {

      continue;
    }

    const double absolute_error = std::abs(candidate - predicted);

    const double relative_error =
        absolute_error / std::max(1.0, std::abs(predicted));

    const bool within_tolerance =
        absolute_error <= RIGHT_Y_DECIMAL_RECOVERY_ABSOLUTE_TOLERANCE ||
        relative_error <= RIGHT_Y_DECIMAL_RECOVERY_RELATIVE_TOLERANCE;

    if (!within_tolerance) {

      continue;
    }

    if (absolute_error < best_error) {

      best_error = absolute_error;

      best_scale = scale;
    }
  }

  if (best_scale == 1.0) {

    return false;
  }

  const double canonical_value = current_value * best_scale;

  if (!std::isfinite(canonical_value) || canonical_value < 0.0 ||
      canonical_value > RIGHT_Y_MAX_PERCENT_VALUE) {

    return false;
  }

  if (std::abs(canonical_value - current_value) < 0.05) {

    return false;
  }

  char formatted[64] = {};

  const int written =
      std::snprintf(formatted, sizeof(formatted), "%.1f%%", canonical_value);

  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(formatted)) {

    return false;
  }

  original_value = current_value;

  recovered_value = canonical_value;

  text.assign(formatted, static_cast<std::size_t>(written));

  return true;
}

// =============================================================================
// ROBUST Y-AXIS SEQUENCE OUTLIER FILTER
// =============================================================================
//
// A valid linear axis has approximately linear numeric values as a function
// of image Y. OCR hallucinations such as "55554%" violate that relationship
// by orders of magnitude.
//
// The fit uses the median of all pairwise slopes (Theil-Sen style), which is
// robust to a minority of bad OCR values and does not assume a particular
// axis range.
//
// =============================================================================

[[nodiscard]]
std::vector<bool>
y_axis_sequence_inlier_mask(const std::vector<DetectedLabel> &labels,
                            YAxisKind axis) noexcept {

  std::vector<bool> keep(labels.size(), true);

  if (labels.size() < 4u) {

    return keep;
  }

  struct Point {

    double y = 0.0;
    double value = 0.0;
  };

  std::vector<Point> points;
  points.reserve(labels.size());

  std::vector<std::size_t> point_indices;
  point_indices.reserve(labels.size());

  for (std::size_t i = 0u; i < labels.size(); ++i) {

    double value = 0.0;

    if (!parse_y_axis_numeric_value(labels[i].label.text, axis, value)) {

      keep[i] = false;

      continue;
    }

    const double center_y = (static_cast<double>(labels[i].label.min_y) +
                             static_cast<double>(labels[i].label.max_y)) *
                            0.5;

    points.push_back({center_y, value});

    point_indices.push_back(i);
  }

  if (points.size() < 4u) {

    return keep;
  }

  std::vector<double> slopes;

  slopes.reserve(points.size() * (points.size() - 1u) / 2u);

  for (std::size_t i = 0u; i < points.size(); ++i) {

    for (std::size_t j = i + 1u; j < points.size(); ++j) {

      const double dy = points[j].y - points[i].y;

      if (std::abs(dy) < 1.0e-9) {

        continue;
      }

      const double slope = (points[j].value - points[i].value) / dy;

      if (std::isfinite(slope)) {

        slopes.push_back(slope);
      }
    }
  }

  if (slopes.empty()) {

    return keep;
  }

  const double slope = median_value(std::move(slopes));

  if (!std::isfinite(slope)) {

    return keep;
  }

  std::vector<double> intercepts;
  intercepts.reserve(points.size());

  for (const Point &point : points) {

    intercepts.push_back(point.value - slope * point.y);
  }

  const double intercept = median_value(std::move(intercepts));

  if (!std::isfinite(intercept)) {

    return keep;
  }

  std::vector<double> residuals;
  residuals.reserve(points.size());

  double min_value = std::numeric_limits<double>::max();

  double max_value = std::numeric_limits<double>::lowest();

  for (const Point &point : points) {

    min_value = std::min(min_value, point.value);

    max_value = std::max(max_value, point.value);

    const double predicted = slope * point.y + intercept;

    residuals.push_back(std::abs(point.value - predicted));
  }

  const double median_residual = median_value(residuals);

  const double value_range = max_value - min_value;

  if (!std::isfinite(median_residual) || !std::isfinite(value_range)) {

    return keep;
  }

  const double residual_threshold =
      std::max(1.0, std::max(median_residual * 8.0, value_range * 0.02));

  for (std::size_t i = 0u; i < points.size(); ++i) {

    const double predicted = slope * points[i].y + intercept;

    const double residual = std::abs(points[i].value - predicted);

    if (residual > residual_threshold) {

      keep[point_indices[i]] = false;
    }
  }

  return keep;
}

[[nodiscard]]
YAxisValueFit
estimate_y_axis_value_fit(const std::vector<TextCandidate> &axis_candidates,
                          const std::vector<DetectedLabel> &axis_results,
                          YAxisKind axis) noexcept {

  YAxisValueFit fit{};

  if (axis_candidates.size() < 2u ||
      axis_results.size() < MIN_Y_AXIS_VALUE_RECOVERY_ANCHORS) {

    return fit;
  }

  std::vector<double> centers;
  centers.reserve(axis_candidates.size());

  for (const TextCandidate &candidate : axis_candidates) {

    centers.push_back((static_cast<double>(candidate.min_y) +
                       static_cast<double>(candidate.max_y)) *
                      0.5);
  }

  std::sort(centers.begin(), centers.end());

  std::vector<double> spacings;
  spacings.reserve(centers.size() - 1u);

  for (std::size_t i = 1u; i < centers.size(); ++i) {

    const double spacing = centers[i] - centers[i - 1u];

    if (spacing > 1.0) {

      spacings.push_back(spacing);
    }
  }

  if (spacings.empty()) {

    return fit;
  }

  fit.tick_spacing = median_value(std::move(spacings));

  if (!std::isfinite(fit.tick_spacing) || fit.tick_spacing <= 1.0) {

    return fit;
  }

  struct Point {

    double y = 0.0;

    double value = 0.0;
  };

  std::vector<Point> points;
  points.reserve(axis_results.size());

  for (const DetectedLabel &result : axis_results) {

    double value = 0.0;

    if (!parse_y_axis_numeric_value(result.label.text, axis, value)) {

      continue;
    }

    points.push_back({(static_cast<double>(result.label.min_y) +
                       static_cast<double>(result.label.max_y)) *
                          0.5,
                      value});
  }

  if (points.size() < MIN_Y_AXIS_VALUE_RECOVERY_ANCHORS) {

    return fit;
  }

  std::sort(points.begin(), points.end(),
            [](const Point &a, const Point &b) noexcept { return a.y < b.y; });

  std::vector<double> tick_deltas;

  tick_deltas.reserve(points.size() > 1u ? points.size() - 1u : 0u);

  // Use spatially adjacent OCR anchors. A single bad OCR result should only
  // contaminate one local interval rather than half of all pairwise slopes.
  //
  // Example:
  //
  //     55%   <- bad
  //     16.5% <- good
  //     11.0% <- good
  //      5.5% <- good
  //
  // Adjacent deltas are:
  //
  //     -38.5, -5.5, -5.5
  //
  // and their median remains -5.5.
  for (std::size_t i = 1u; i < points.size(); ++i) {

    const double spatial_distance = points[i].y - points[i - 1u].y;

    if (spatial_distance < fit.tick_spacing * 0.50) {

      continue;
    }

    const double spatial_ticks = spatial_distance / fit.tick_spacing;

    const long long rounded_ticks = std::llround(spatial_ticks);

    if (rounded_ticks <= 0) {

      continue;
    }

    if (std::abs(spatial_ticks - static_cast<double>(rounded_ticks)) >
        Y_AXIS_VALUE_RECOVERY_MAX_TICK_ERROR_RATIO) {

      continue;
    }

    const double delta = (points[i].value - points[i - 1u].value) /
                         static_cast<double>(rounded_ticks);

    if (std::isfinite(delta)) {

      tick_deltas.push_back(delta);
    }
  }

  if (tick_deltas.empty()) {

    return fit;
  }

  fit.tick_delta = median_value(std::move(tick_deltas));

  if (!std::isfinite(fit.tick_delta) || std::abs(fit.tick_delta) < 1.0e-9) {

    return fit;
  }

  fit.slope = fit.tick_delta / fit.tick_spacing;

  std::vector<double> intercepts;
  intercepts.reserve(points.size());

  for (const Point &point : points) {

    intercepts.push_back(point.value - fit.slope * point.y);
  }

  fit.intercept = median_value(std::move(intercepts));

  if (!std::isfinite(fit.slope) || !std::isfinite(fit.intercept)) {

    return fit;
  }

  if (axis == YAxisKind::RIGHT) {

    const double first = fit.slope * centers.front() + fit.intercept;

    const double last = fit.slope * centers.back() + fit.intercept;

    if (first < -5.0 || first > Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT + 5.0 ||
        last < -5.0 || last > Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT + 5.0) {

      return fit;
    }
  }

  fit.anchors = points.size();

  fit.valid = true;

  return fit;
}

// =============================================================================
// FORMAT RECOVERED Y-AXIS VALUE
// =============================================================================

[[nodiscard]]
std::string format_recovered_y_axis_value(
    double value, YAxisKind axis,
    const std::vector<DetectedLabel> &existing_results) {

  if (!std::isfinite(value)) {

    return {};
  }

  if (std::abs(value) < 0.05) {

    value = 0.0;
  }

  char formatted[64] = {};

  if (axis == YAxisKind::RIGHT) {

    if (std::abs(value) < 0.05) {

      return "0%";
    }

    const int written =
        std::snprintf(formatted, sizeof(formatted), "%.1f%%", value);

    if (written <= 0 ||
        static_cast<std::size_t>(written) >= sizeof(formatted)) {

      return {};
    }

    return std::string(formatted, static_cast<std::size_t>(written));
  }

  if (std::abs(value) < 0.5) {

    return "$0";
  }

  bool use_k = false;

  for (const DetectedLabel &result : existing_results) {

    if (result.label.text.find('k') != std::string::npos) {

      use_k = true;

      break;
    }
  }

  if (use_k || std::abs(value) >= 1000.0) {

    const double thousands = value / 1000.0;

    const int written =
        std::snprintf(formatted, sizeof(formatted), "$%.2fk", thousands);

    if (written <= 0 ||
        static_cast<std::size_t>(written) >= sizeof(formatted)) {

      return {};
    }

    return std::string(formatted, static_cast<std::size_t>(written));
  }

  const int written =
      std::snprintf(formatted, sizeof(formatted), "$%.0f", value);

  if (written <= 0 || static_cast<std::size_t>(written) >= sizeof(formatted)) {

    return {};
  }

  return std::string(formatted, static_cast<std::size_t>(written));
}

// =============================================================================
// RECONCILE Y-AXIS VALUES AGAINST SPATIAL GRID
// =============================================================================
//
// Repairs OCR values that contradict the established spatial tick sequence
// and fills candidate ticks where OCR returned nothing.

void reconcile_y_axis_values(const std::vector<TextCandidate> &axis_candidates,
                             YAxisKind axis,
                             std::vector<DetectedLabel> &axis_results) {

  const YAxisValueFit fit =
      estimate_y_axis_value_fit(axis_candidates, axis_results, axis);

  if (!fit.valid || fit.anchors < MIN_Y_AXIS_VALUE_RECOVERY_ANCHORS) {

    return;
  }

  // -------------------------------------------------------------------------
  // Repair accepted OCR outliers.
  // -------------------------------------------------------------------------

  for (DetectedLabel &result : axis_results) {

    double current_value = 0.0;

    if (!parse_y_axis_numeric_value(result.label.text, axis, current_value)) {

      continue;
    }

    const double center_y = (static_cast<double>(result.label.min_y) +
                             static_cast<double>(result.label.max_y)) *
                            0.5;

    double predicted = fit.slope * center_y + fit.intercept;

    if (axis == YAxisKind::RIGHT) {

      predicted =
          std::clamp(predicted, 0.0, Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT);
    }

    const double error = std::abs(current_value - predicted);

    const double max_error =
        axis == YAxisKind::RIGHT
            ? std::max(Y_AXIS_VALUE_RECOVERY_MAX_ABSOLUTE_ERROR_RIGHT,
                       std::abs(fit.tick_delta) * 0.55)
            : std::max(Y_AXIS_VALUE_RECOVERY_MAX_ABSOLUTE_ERROR_LEFT,
                       std::abs(fit.tick_delta) * 0.55);

    if (error <= max_error) {

      continue;
    }

    const std::string canonical =
        format_recovered_y_axis_value(predicted, axis, axis_results);

    if (canonical.empty()) {

      continue;
    }

    const std::string original = result.label.text;

    result.label.text = canonical;

    if constexpr (CHART_LABEL_DEBUG) {

      std::fprintf(stderr,
                   "[ChartLabelRecognizer] "
                   "y_axis_spatial_value_recovered: "
                   "axis=%s "
                   "\"%s\" -> \"%s\" "
                   "predicted=%.4f "
                   "tick_delta=%.4f\n",
                   axis == YAxisKind::LEFT ? "left" : "right", original.c_str(),
                   canonical.c_str(), predicted, fit.tick_delta);
    }
  }

  // -------------------------------------------------------------------------
  // Recover missing candidate values.
  // -------------------------------------------------------------------------

  std::vector<DetectedLabel> recovered;
  recovered.reserve(axis_candidates.size());

  for (const TextCandidate &candidate : axis_candidates) {

    const double candidate_center_y = (static_cast<double>(candidate.min_y) +
                                       static_cast<double>(candidate.max_y)) *
                                      0.5;

    bool already_present = false;

    for (const DetectedLabel &result : axis_results) {

      const double result_center_y = (static_cast<double>(result.label.min_y) +
                                      static_cast<double>(result.label.max_y)) *
                                     0.5;

      if (std::abs(result_center_y - candidate_center_y) <=
          fit.tick_spacing * 0.35) {

        already_present = true;

        break;
      }
    }

    if (already_present) {

      continue;
    }

    double predicted = fit.slope * candidate_center_y + fit.intercept;

    if (axis == YAxisKind::RIGHT) {

      if (predicted < -1.0 ||
          predicted > Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT + 1.0) {

        continue;
      }

      predicted =
          std::clamp(predicted, 0.0, Y_AXIS_VALUE_RECOVERY_MAX_VALUE_RIGHT);
    }

    const double tick_units = std::abs(fit.tick_delta) > 1.0e-9
                                  ? predicted / std::abs(fit.tick_delta)
                                  : 0.0;

    const double nearest_tick =
        std::round(tick_units) * std::abs(fit.tick_delta);

    if (std::abs(predicted - nearest_tick) >
        std::max(0.75, std::abs(fit.tick_delta) *
                           Y_AXIS_VALUE_RECOVERY_MAX_TICK_ERROR_RATIO)) {

      continue;
    }

    const std::string recovered_text =
        format_recovered_y_axis_value(predicted, axis, axis_results);

    if (recovered_text.empty()) {

      continue;
    }

    DetectedLabel recovered_label{};

    recovered_label.label.text = recovered_text;

    recovered_label.label.min_x = candidate.min_x;

    recovered_label.label.min_y = candidate.min_y;

    recovered_label.label.max_x = candidate.max_x;

    recovered_label.label.max_y = candidate.max_y;

    recovered_label.label.kind =
        ::fin_ocr::chart::association::ChartLabelKind::UNKNOWN;

    recovered_label.label.series_index = -1;

    recovered_label.label.category_index = -1;

    recovered_label.label.confidence = static_cast<float>(
        std::clamp(Y_AXIS_VALUE_RECOVERY_MIN_SPATIAL_SUPPORT, 0.0, 1.0));

    const int candidate_width = candidate.max_x - candidate.min_x + 1;

    const int candidate_height = candidate.max_y - candidate.min_y + 1;

    const std::size_t area =
        candidate_width > 0 && candidate_height > 0
            ? static_cast<std::size_t>(candidate_width) *
                  static_cast<std::size_t>(candidate_height)
            : 0u;

    recovered_label.density =
        area > 0u
            ? static_cast<float>(static_cast<double>(candidate.active_pixels) /
                                 static_cast<double>(area))
            : 0.0f;

    recovered_label.glyph_count =
        ::fin_ocr::chart::label::text::count_glyphs(recovered_text);

    recovered.push_back(std::move(recovered_label));

    if constexpr (CHART_LABEL_DEBUG) {

      std::fprintf(stderr,
                   "[ChartLabelRecognizer] "
                   "y_axis_missing_value_recovered: "
                   "axis=%s "
                   "x=%d..%d y=%d..%d "
                   "text=\"%s\" "
                   "predicted=%.4f\n",
                   axis == YAxisKind::LEFT ? "left" : "right", candidate.min_x,
                   candidate.max_x, candidate.min_y, candidate.max_y,
                   recovered_text.c_str(), predicted);
    }
  }

  for (DetectedLabel &item : recovered) {

    axis_results.push_back(std::move(item));
  }

  std::sort(axis_results.begin(), axis_results.end(),
            [](const DetectedLabel &a, const DetectedLabel &b) noexcept {
              if (a.label.min_y != b.label.min_y) {

                return a.label.min_y < b.label.min_y;
              }

              return a.label.min_x < b.label.min_x;
            });
}

} // namespace fin_ocr::chart::label::y_axis
