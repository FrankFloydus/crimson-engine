////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/curves/curve_arc_length_path.hpp>
#include <quader/modeling/curves/curve_document.hpp>
#include <quader/modeling/curves/curve_shapes.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader::modeling {

/// Pins one immutable set of authored controls independently of allocation cursors.
struct CurveDocumentData {
  std::vector<CurveControlPoint> points;
  std::vector<CurveSegmentId> segments;
  bool closed = false;
  CurveContentIdentity identity{};
  std::uint64_t revision = 0;
};

/// Pins transformed path geometry and its nondecreasing distance stations.
struct CurveArcLengthPathData {
  CurveDocument world_document;
  std::vector<CurveArcLengthStation> stations;
  double total_length = 0.0;
  bool closed = false;
};

namespace {

constexpr std::uint64_t kExhaustedIndex =
    std::uint64_t{std::numeric_limits<std::uint32_t>::max()} + 1U;
constexpr std::size_t kMaxPoints = 1'000'000;
constexpr std::size_t kCodecHeaderSize = 40;
constexpr std::size_t kCodecPointSize = 44;
constexpr std::size_t kCodecSegmentSize = 8;
constexpr double kSplitRelativeError =
    64.0 * std::numeric_limits<float>::epsilon();
constexpr std::array<std::byte, 4> kCurveMagic{
    std::byte{'Q'}, std::byte{'C'}, std::byte{'R'}, std::byte{'V'}};

[[nodiscard]] CurveContentIdentity next_content_identity() noexcept {
  static std::atomic<std::uint64_t> next{1};
  return {next.fetch_add(1, std::memory_order_relaxed)};
}

[[nodiscard]] std::uint64_t next_revision(std::uint64_t revision) noexcept {
  return revision == std::numeric_limits<std::uint64_t>::max()
             ? revision
             : revision + 1U;
}

[[nodiscard]] Error curve_error(ErrorCode code, std::string message,
                                std::string path = {}) {
  Error error = make_error(code, std::move(message));
  error.diagnostics.push_back({.code = "curve.document",
                               .severity = DiagnosticSeverity::Error,
                               .path = std::move(path),
                               .message = error.message});
  return error;
}

[[nodiscard]] Error curve_path_error(ErrorCode code, std::string message,
                                     std::string path = {}) {
  Error error = make_error(code, std::move(message));
  error.diagnostics.push_back({.code = "curve.arc_length_path",
                               .severity = DiagnosticSeverity::Error,
                               .path = std::move(path),
                               .message = error.message});
  return error;
}

[[nodiscard]] Error curve_shape_error(std::string message,
                                      std::string path) {
  Error error = make_error(ErrorCode::InvalidArgument, std::move(message));
  error.diagnostics.push_back({.code = "curve.shape",
                               .severity = DiagnosticSeverity::Error,
                               .path = std::move(path),
                               .message = error.message});
  return error;
}

[[nodiscard]] Result<OperationResult> unchanged() {
  return Result<OperationResult>::success({});
}

[[nodiscard]] Result<OperationResult> changed(bool topology) {
  OperationResult result;
  result.changed = true;
  result.dirty.topology = topology;
  result.dirty.geometry = true;
  result.dirty.overlays = true;
  return Result<OperationResult>::success(std::move(result));
}

[[nodiscard]] bool same(Vec3 a, Vec3 b) noexcept {
  return a.x == b.x && a.y == b.y && a.z == b.z;
}

[[nodiscard]] bool finite(Vec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

/// Keeps curve evaluation, subdivision and bounds arithmetic above storage precision.
struct Double3 {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

[[nodiscard]] Double3 wide(Vec3 value) noexcept {
  return {value.x, value.y, value.z};
}

[[nodiscard]] Double3 operator+(Double3 a, Double3 b) noexcept {
  return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] Double3 operator-(Double3 a, Double3 b) noexcept {
  return {a.x - b.x, a.y - b.y, a.z - b.z};
}

[[nodiscard]] Double3 operator*(Double3 value, double scale) noexcept {
  return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] double dot(Double3 a, Double3 b) noexcept {
  return a.x * b.x + a.y * b.y + a.z * b.z;
}

[[nodiscard]] Double3 interpolate(Double3 a, Double3 b, double t) noexcept {
  return a * (1.0 - t) + b * t;
}

[[nodiscard]] bool representable(Double3 value) noexcept {
  constexpr double kLimit = std::numeric_limits<float>::max();
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z) && std::abs(value.x) <= kLimit &&
         std::abs(value.y) <= kLimit && std::abs(value.z) <= kLimit;
}

[[nodiscard]] Vec3 narrow(Double3 value) noexcept {
  return {static_cast<float>(value.x), static_cast<float>(value.y),
          static_cast<float>(value.z)};
}

[[nodiscard]] bool valid_point(const CurvePointSpec &point) noexcept {
  return finite(point.position) && finite(point.incoming_handle) &&
         finite(point.outgoing_handle) &&
         representable(wide(point.position) + wide(point.incoming_handle)) &&
         representable(wide(point.position) + wide(point.outgoing_handle));
}

[[nodiscard]] CurvePointSpec point_spec(const CurveControlPoint &point) noexcept {
  return {point.position, point.incoming_handle, point.outgoing_handle};
}

[[nodiscard]] bool same_point(const CurveControlPoint &point,
                               const CurvePointSpec &spec) noexcept {
  return same(point.position, spec.position) &&
         same(point.incoming_handle, spec.incoming_handle) &&
         same(point.outgoing_handle, spec.outgoing_handle);
}

[[nodiscard]] std::optional<Error>
validate_authored(std::span<const CurveControlPoint> points,
                  std::span<const CurveSegmentId> segments, bool closed,
                  CurveIdentityHighWater high_water) {
  if (points.size() > kMaxPoints) {
    return curve_error(ErrorCode::ValidationFailed,
                       "Curve exceeds the supported point count.", "points");
  }
  if (closed && points.size() < 2U) {
    return curve_error(ErrorCode::ValidationFailed,
                       "A closed curve needs at least two points.", "closed");
  }
  const std::size_t expected_segments =
      closed ? points.size() : (points.empty() ? 0U : points.size() - 1U);
  if (segments.size() != expected_segments) {
    return curve_error(ErrorCode::ValidationFailed,
                       "Curve segments must match the ordered path.", "segments");
  }
  if (high_water.next_point_index == 0 ||
      high_water.next_point_index > kExhaustedIndex ||
      high_water.next_segment_index == 0 ||
      high_water.next_segment_index > kExhaustedIndex) {
    return curve_error(ErrorCode::ValidationFailed,
                       "Curve allocation cursors are out of range.",
                       "identity_high_water");
  }
  std::unordered_set<std::uint32_t> point_ids;
  point_ids.reserve(points.size());
  for (std::size_t i = 0; i < points.size(); ++i) {
    const auto &point = points[i];
    if (!point.id.valid() || point.id.generation != 1 ||
        point.id.index >= high_water.next_point_index ||
        !point_ids.insert(point.id.index).second) {
      return curve_error(ErrorCode::ValidationFailed,
                         "Curve point IDs must be unique and below their allocation cursor.",
                         "points[" + std::to_string(i) + "].id");
    }
    if (!valid_point(point_spec(point))) {
      return curve_error(ErrorCode::ValidationFailed,
                         "Curve controls must have finite representable positions.",
                         "points[" + std::to_string(i) + "]");
    }
  }
  std::unordered_set<std::uint32_t> segment_ids;
  segment_ids.reserve(segments.size());
  for (const auto id : segments) {
    if (!id.valid() || id.generation != 1 ||
        id.index >= high_water.next_segment_index ||
        !segment_ids.insert(id.index).second) {
      return curve_error(ErrorCode::ValidationFailed,
                         "Curve segment IDs must be unique and below their allocation cursor.",
                         "segments");
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Error>
validate_authored(const AuthoredCurve &authored) {
  return validate_authored(authored.points, authored.segments, authored.closed,
                           authored.identity_high_water);
}

template <typename IdType>
[[nodiscard]] Error missing_id(IdType id, std::uint64_t next_index,
                                const char *label) {
  const auto code = id.index != 0 &&
                            (id.generation != 1 || id.index < next_index)
                        ? ErrorCode::StaleId
                        : ErrorCode::InvalidId;
  return curve_error(code, std::string("Curve ") + label + " ID is not live.");
}

[[nodiscard]] bool can_allocate(std::uint64_t next_index) noexcept {
  return next_index > 0 && next_index < kExhaustedIndex;
}

[[nodiscard]] Error allocation_exhausted() {
  return curve_error(ErrorCode::ValidationFailed,
                     "Curve component ID capacity is exhausted.");
}

[[nodiscard]] std::array<Double3, 4>
segment_controls(std::span<const CurveControlPoint> points,
                  std::size_t index) noexcept {
  const auto &start = points[index];
  const auto &end = points[(index + 1U) % points.size()];
  return {wide(start.position),
          wide(start.position) + wide(start.outgoing_handle),
          wide(end.position) + wide(end.incoming_handle), wide(end.position)};
}

[[nodiscard]] std::array<Double3, 4>
relative_segment_controls(std::span<const CurveControlPoint> points,
                           std::size_t index, Double3 origin) noexcept {
  const auto &start = points[index];
  const auto &end = points[(index + 1U) % points.size()];
  const auto start_position = wide(start.position) - origin;
  const auto end_position = wide(end.position) - origin;
  return {start_position, start_position + wide(start.outgoing_handle),
          end_position + wide(end.incoming_handle), end_position};
}

[[nodiscard]] double control_hull_diameter_squared(
    const std::array<Double3, 4> &controls) noexcept {
  double diameter_squared = 0.0;
  for (std::size_t i = 0; i < controls.size(); ++i) {
    for (std::size_t j = i + 1U; j < controls.size(); ++j) {
      const auto difference = controls[i] - controls[j];
      diameter_squared = std::max(diameter_squared, dot(difference, difference));
    }
  }
  return diameter_squared;
}

[[nodiscard]] bool controls_within_error(
    const std::array<Double3, 4> &stored,
    const std::array<Double3, 4> &ideal, double error_squared) noexcept {
  for (std::size_t i = 0; i < stored.size(); ++i) {
    const auto difference = stored[i] - ideal[i];
    if (dot(difference, difference) > error_squared) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] Double3 evaluate_position(const std::array<Double3, 4> &controls,
                                         double t) noexcept {
  const auto a = interpolate(controls[0], controls[1], t);
  const auto b = interpolate(controls[1], controls[2], t);
  const auto c = interpolate(controls[2], controls[3], t);
  return interpolate(interpolate(a, b, t), interpolate(b, c, t), t);
}

[[nodiscard]] Double3 evaluate_tangent(const std::array<Double3, 4> &controls,
                                        double t) noexcept {
  const auto a = controls[1] - controls[0];
  const auto b = controls[2] - controls[1];
  const auto c = controls[3] - controls[2];
  return interpolate(interpolate(a, b, t), interpolate(b, c, t), t) * 3.0;
}

[[nodiscard]] Double3 transform_vector(Transform3 transform,
                                        Vec3 value) noexcept {
  return wide(transform.x_axis) * value.x +
         wide(transform.y_axis) * value.y +
         wide(transform.z_axis) * value.z;
}

[[nodiscard]] double distance_to_segment_squared(Double3 point, Double3 start,
                                                  Double3 end) noexcept {
  const auto delta = end - start;
  const double length_squared = dot(delta, delta);
  const double t = length_squared > 0.0
                       ? std::clamp(dot(point - start, delta) / length_squared,
                                    0.0, 1.0)
                       : 0.0;
  const auto distance = point - interpolate(start, end, t);
  return dot(distance, distance);
}

/// Accumulates conservative double-precision bounds before outward float rounding.
struct BoundsAccumulator {
  Double3 min{};
  Double3 max{};
  bool valid = false;

  void include(Double3 point) noexcept {
    if (!valid) {
      min = point;
      max = point;
      valid = true;
      return;
    }
    min = {std::min(min.x, point.x), std::min(min.y, point.y),
           std::min(min.z, point.z)};
    max = {std::max(max.x, point.x), std::max(max.y, point.y),
           std::max(max.z, point.z)};
  }
};

void include_axis_extrema(const std::array<Double3, 4> &controls,
                          std::array<double, 4> axis,
                          BoundsAccumulator &bounds) noexcept {
  const double a = -axis[0] + 3.0 * axis[1] - 3.0 * axis[2] + axis[3];
  const double b = 2.0 * (axis[0] - 2.0 * axis[1] + axis[2]);
  const double c = axis[1] - axis[0];
  const auto include_root = [&](double t) {
    if (t > 0.0 && t < 1.0) {
      bounds.include(evaluate_position(controls, t));
    }
  };
  if (a == 0.0) {
    if (b != 0.0) {
      include_root(-c / b);
    }
    return;
  }
  const double discriminant = b * b - 4.0 * a * c;
  if (discriminant < 0.0) {
    return;
  }
  const double q = -0.5 * (b + std::copysign(std::sqrt(discriminant), b));
  if (q == 0.0) {
    include_root(-b / (2.0 * a));
    return;
  }
  include_root(q / a);
  include_root(c / q);
}

[[nodiscard]] float lower_float(double value) noexcept {
  const float result = static_cast<float>(value);
  return result > value
             ? std::nextafter(result, -std::numeric_limits<float>::infinity())
             : result;
}

[[nodiscard]] float upper_float(double value) noexcept {
  const float result = static_cast<float>(value);
  return result < value
             ? std::nextafter(result, std::numeric_limits<float>::infinity())
             : result;
}

/// Owns bounded recursive tessellation state for one output path.
struct CurveSampler {
  CurveSamplingOptions options;
  std::vector<CurveSample> samples;
  std::optional<Error> error;

  bool append(const std::array<Double3, 4> &original, CurveSegmentId segment,
               double parameter) {
    if (samples.size() >= options.max_samples) {
      error = curve_error(ErrorCode::ValidationFailed,
                          "Curve sampling exceeded its sample budget.");
      return false;
    }
    const auto position = evaluate_position(original, parameter);
    const auto tangent = evaluate_tangent(original, parameter);
    if (!representable(position) || !representable(tangent)) {
      error = curve_error(ErrorCode::ValidationFailed,
                          "Evaluated curve data exceeds the supported numeric range.");
      return false;
    }
    samples.push_back({narrow(position), narrow(tangent), segment, parameter});
    return true;
  }

  bool subdivide(const std::array<Double3, 4> &controls,
                  const std::array<Double3, 4> &original,
                  CurveSegmentId segment, double start, double end,
                  std::uint32_t depth) {
    const double tolerance_squared = options.tolerance * options.tolerance;
    // The renderer receives float endpoints. Testing against that actual chord
    // also bounds narrowing error at large coordinates; testing only the
    // double-precision control polygon could silently erase a small bend.
    const auto chord_start = wide(narrow(evaluate_position(original, start)));
    const auto chord_end = wide(narrow(evaluate_position(original, end)));
    double flatness = 0.0;
    for (const auto control : controls) {
      flatness = std::max(flatness,
                          distance_to_segment_squared(control, chord_start, chord_end));
    }
    if (flatness <= tolerance_squared) {
      return append(original, segment, end);
    }
    if (depth >= options.max_depth) {
      error = curve_error(ErrorCode::ValidationFailed,
                          "Curve sampling could not meet tolerance within its depth or storage precision limit.");
      return false;
    }
    const auto a = interpolate(controls[0], controls[1], 0.5);
    const auto b = interpolate(controls[1], controls[2], 0.5);
    const auto c = interpolate(controls[2], controls[3], 0.5);
    const auto d = interpolate(a, b, 0.5);
    const auto e = interpolate(b, c, 0.5);
    const auto point = interpolate(d, e, 0.5);
    const double middle = (start + end) * 0.5;
    return subdivide({controls[0], a, d, point}, original, segment,
                     start, middle, depth + 1U) &&
           subdivide({point, e, c, controls[3]}, original, segment,
                     middle, end, depth + 1U);
  }
};

/**
 * Uses a stronger criterion than display tessellation: every stored control
 * must approach its linearly parameterized float chord. This also subdivides a
 * geometrically straight cubic whose speed varies with parameter, which is
 * required before a distance fraction can be mapped back to parameter.
 */
struct CurveArcLengthBuilder {
  CurveSamplingOptions options;
  std::vector<CurveSample> stations;
  std::optional<Error> error;

  bool append(const std::array<Double3, 4> &original,
              CurveSegmentId segment, double parameter) {
    if (stations.size() >= options.max_samples) {
      error = curve_path_error(
          ErrorCode::ValidationFailed,
          "Curve arc-length sampling exceeded its sample budget.");
      return false;
    }
    const auto position = evaluate_position(original, parameter);
    const auto tangent = evaluate_tangent(original, parameter);
    if (!representable(position) || !representable(tangent)) {
      error = curve_path_error(
          ErrorCode::ValidationFailed,
          "Evaluated Curve path data exceeds the supported numeric range.");
      return false;
    }
    stations.push_back(
        {narrow(position), narrow(tangent), segment, parameter});
    return true;
  }

  bool subdivide(const std::array<Double3, 4> &controls,
                 const std::array<Double3, 4> &original,
                 CurveSegmentId segment, double start, double end,
                 std::uint32_t depth) {
    const double tolerance_squared = options.tolerance * options.tolerance;
    const auto chord_start = wide(narrow(evaluate_position(original, start)));
    const auto chord_end = wide(narrow(evaluate_position(original, end)));
    const std::array linearly_parameterized_chord{
        chord_start, interpolate(chord_start, chord_end, 1.0 / 3.0),
        interpolate(chord_start, chord_end, 2.0 / 3.0), chord_end};
    double deviation = 0.0;
    for (std::size_t index = 0; index < controls.size(); ++index) {
      const auto difference = controls[index] - linearly_parameterized_chord[index];
      deviation = std::max(deviation, dot(difference, difference));
    }
    if (deviation <= tolerance_squared) {
      return append(original, segment, end);
    }
    if (depth >= options.max_depth) {
      error = curve_path_error(
          ErrorCode::ValidationFailed,
          "Curve arc-length sampling could not meet tolerance within its depth or storage precision limit.");
      return false;
    }
    const auto a = interpolate(controls[0], controls[1], 0.5);
    const auto b = interpolate(controls[1], controls[2], 0.5);
    const auto c = interpolate(controls[2], controls[3], 0.5);
    const auto d = interpolate(a, b, 0.5);
    const auto e = interpolate(b, c, 0.5);
    const auto point = interpolate(d, e, 0.5);
    const double middle = (start + end) * 0.5;
    return subdivide({controls[0], a, d, point}, original, segment,
                     start, middle, depth + 1U) &&
           subdivide({point, e, c, controls[3]}, original, segment,
                     middle, end, depth + 1U);
  }
};

[[nodiscard]] double curve_path_distance(Vec3 left, Vec3 right) noexcept {
  return std::hypot(static_cast<double>(right.x) - left.x,
                    static_cast<double>(right.y) - left.y,
                    static_cast<double>(right.z) - left.z);
}

[[nodiscard]] Vec3 normalized_curve_path_direction(Vec3 value) noexcept {
  const double length = std::hypot(static_cast<double>(value.x),
                                   static_cast<double>(value.y),
                                   static_cast<double>(value.z));
  if (!std::isfinite(length) || length <= 0.0) {
    return {};
  }
  return {static_cast<float>(value.x / length),
          static_cast<float>(value.y / length),
          static_cast<float>(value.z / length)};
}

[[nodiscard]] bool nonzero_curve_path_direction(Vec3 value) noexcept {
  return value.x != 0.0F || value.y != 0.0F || value.z != 0.0F;
}

[[nodiscard]] Vec3 curve_path_direction(
    std::span<const CurveArcLengthStation> stations, Vec3 position,
    Vec3 derivative,
    std::size_t first_at_distance, std::size_t after_distance) noexcept {
  Vec3 direction = normalized_curve_path_direction(derivative);
  if (nonzero_curve_path_direction(direction)) {
    return direction;
  }
  if (after_distance < stations.size()) {
    const Vec3 next = stations[after_distance].evaluation.position;
    direction = normalized_curve_path_direction(
        {next.x - position.x, next.y - position.y, next.z - position.z});
    if (nonzero_curve_path_direction(direction)) {
      return direction;
    }
  }
  if (first_at_distance > 0U) {
    const Vec3 previous =
        stations[first_at_distance - 1U].evaluation.position;
    direction = normalized_curve_path_direction(
        {position.x - previous.x, position.y - previous.y,
         position.z - previous.z});
  }
  return direction;
}

/// Writes fixed-width curve values without native layout or endianness assumptions.
class CurveByteWriter final {
public:
  explicit CurveByteWriter(std::size_t capacity) { bytes_.reserve(capacity); }

  void integer(std::uint64_t value, unsigned int count) {
    for (unsigned int i = 0; i < count; ++i) {
      bytes_.push_back(static_cast<std::byte>((value >> (i * 8U)) & 0xffU));
    }
  }
  void vector(Vec3 value) {
    integer(std::bit_cast<std::uint32_t>(value.x), 4);
    integer(std::bit_cast<std::uint32_t>(value.y), 4);
    integer(std::bit_cast<std::uint32_t>(value.z), 4);
  }
  [[nodiscard]] std::vector<std::byte> finish() && { return std::move(bytes_); }

private:
  std::vector<std::byte> bytes_;
};

/// Reads a size-validated curve record; callers validate the whole payload first.
class CurveByteReader final {
public:
  explicit CurveByteReader(std::span<const std::byte> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::uint64_t integer(unsigned int count) noexcept {
    std::uint64_t value = 0;
    for (unsigned int i = 0; i < count; ++i) {
      value |= std::uint64_t{std::to_integer<unsigned int>(bytes_[offset_++])}
               << (i * 8U);
    }
    return value;
  }
  [[nodiscard]] Vec3 vector() noexcept {
    const float x = std::bit_cast<float>(static_cast<std::uint32_t>(integer(4)));
    const float y = std::bit_cast<float>(static_cast<std::uint32_t>(integer(4)));
    const float z = std::bit_cast<float>(static_cast<std::uint32_t>(integer(4)));
    return {x, y, z};
  }

private:
  std::span<const std::byte> bytes_;
  std::size_t offset_ = 0;
};

} // namespace

CurveDocument::CurveDocument() : data_(std::make_shared<CurveDocumentData>()) {}

Result<CurveDocument>
CurveDocument::make_bezier(std::span<const CurvePointSpec> points, bool closed) {
  if (points.size() > kMaxPoints) {
    return Result<CurveDocument>::failure(
        curve_error(ErrorCode::InvalidArgument, "Curve exceeds the supported point count."));
  }
  AuthoredCurve authored;
  authored.closed = closed;
  authored.points.reserve(points.size());
  for (const auto &point : points) {
    authored.points.push_back(
        {.id = {static_cast<std::uint32_t>(authored.identity_high_water.next_point_index++), 1},
         .position = point.position,
         .incoming_handle = point.incoming_handle,
         .outgoing_handle = point.outgoing_handle});
  }
  const std::size_t count = closed ? points.size()
                                    : (points.empty() ? 0U : points.size() - 1U);
  authored.segments.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    authored.segments.push_back(
        {static_cast<std::uint32_t>(authored.identity_high_water.next_segment_index++), 1});
  }
  return from_authored_curve(authored);
}

Result<CurveDocument> make_xy_circle_curve(double radius) {
  constexpr double kQuarterArcHandleRatio =
      4.0 * (1.41421356237309504880168872420969807857 - 1.0) / 3.0;
  if (!std::isfinite(radius) || radius <= 0.0) {
    return Result<CurveDocument>::failure(curve_shape_error(
        "Curve circle radius must be finite and positive.", "radius"));
  }
  if (radius > static_cast<double>(std::numeric_limits<float>::max())) {
    return Result<CurveDocument>::failure(curve_shape_error(
        "Curve circle radius exceeds the supported numeric range.", "radius"));
  }
  const float stored_radius = static_cast<float>(radius);
  const float handle = static_cast<float>(radius * kQuarterArcHandleRatio);
  if (!std::isfinite(stored_radius) || stored_radius <= 0.0F ||
      !std::isfinite(handle) || handle <= 0.0F) {
    return Result<CurveDocument>::failure(curve_shape_error(
        "Curve circle radius cannot be represented by Curve storage.",
        "radius"));
  }

  const std::array points{
      CurvePointSpec{.position = {stored_radius, 0.0F, 0.0F},
                     .incoming_handle = {0.0F, -handle, 0.0F},
                     .outgoing_handle = {0.0F, handle, 0.0F}},
      CurvePointSpec{.position = {0.0F, stored_radius, 0.0F},
                     .incoming_handle = {handle, 0.0F, 0.0F},
                     .outgoing_handle = {-handle, 0.0F, 0.0F}},
      CurvePointSpec{.position = {-stored_radius, 0.0F, 0.0F},
                     .incoming_handle = {0.0F, handle, 0.0F},
                     .outgoing_handle = {0.0F, -handle, 0.0F}},
      CurvePointSpec{.position = {0.0F, -stored_radius, 0.0F},
                     .incoming_handle = {-handle, 0.0F, 0.0F},
                     .outgoing_handle = {handle, 0.0F, 0.0F}},
  };
  return CurveDocument::make_bezier(points, true);
}

Result<CurveDocument> make_xy_stadium_curve(double radius,
                                            double total_length) {
  if (!std::isfinite(radius) || radius <= 0.0) {
    return Result<CurveDocument>::failure(curve_shape_error(
        "Curve stadium radius must be finite and positive.", "radius"));
  }
  if (!std::isfinite(total_length)) {
    return Result<CurveDocument>::failure(curve_shape_error(
        "Curve stadium total length must be finite.", "total_length"));
  }
  const double diameter = radius * 2.0;
  if (!std::isfinite(diameter) || total_length < diameter) {
    return Result<CurveDocument>::failure(curve_shape_error(
        "Curve stadium total length must be at least twice its radius.",
        "total_length"));
  }
  if (total_length == diameter) {
    return make_xy_circle_curve(radius);
  }

  constexpr double kQuarterArcHandleRatio =
      4.0 * (1.41421356237309504880168872420969807857 - 1.0) / 3.0;
  const double half_length = total_length * 0.5;
  const double cap_center_offset = half_length - radius;
  const double straight_handle_length = (total_length - diameter) / 3.0;
  if (half_length > static_cast<double>(std::numeric_limits<float>::max()) ||
      radius > static_cast<double>(std::numeric_limits<float>::max())) {
    return Result<CurveDocument>::failure(curve_shape_error(
        "Curve stadium dimensions exceed the supported numeric range.",
        "total_length"));
  }

  const float stored_half_length = static_cast<float>(half_length);
  const float stored_radius = static_cast<float>(radius);
  const float stored_offset = static_cast<float>(cap_center_offset);
  const float arc_handle =
      static_cast<float>(radius * kQuarterArcHandleRatio);
  const float straight_handle = static_cast<float>(straight_handle_length);
  if (!std::isfinite(stored_half_length) || !std::isfinite(stored_radius) ||
      !std::isfinite(stored_offset) || !std::isfinite(arc_handle) ||
      !std::isfinite(straight_handle) || stored_radius <= 0.0F ||
      arc_handle <= 0.0F || straight_handle <= 0.0F ||
      stored_half_length <= stored_radius || stored_offset <= 0.0F) {
    return Result<CurveDocument>::failure(curve_shape_error(
        "Curve stadium dimensions cannot be represented by Curve storage.",
        "total_length"));
  }

  const std::array points{
      CurvePointSpec{.position = {stored_half_length, 0.0F, 0.0F},
                     .incoming_handle = {0.0F, -arc_handle, 0.0F},
                     .outgoing_handle = {0.0F, arc_handle, 0.0F}},
      CurvePointSpec{.position = {stored_offset, stored_radius, 0.0F},
                     .incoming_handle = {arc_handle, 0.0F, 0.0F},
                     .outgoing_handle = {-straight_handle, 0.0F, 0.0F}},
      CurvePointSpec{.position = {-stored_offset, stored_radius, 0.0F},
                     .incoming_handle = {straight_handle, 0.0F, 0.0F},
                     .outgoing_handle = {-arc_handle, 0.0F, 0.0F}},
      CurvePointSpec{.position = {-stored_half_length, 0.0F, 0.0F},
                     .incoming_handle = {0.0F, arc_handle, 0.0F},
                     .outgoing_handle = {0.0F, -arc_handle, 0.0F}},
      CurvePointSpec{.position = {-stored_offset, -stored_radius, 0.0F},
                     .incoming_handle = {-arc_handle, 0.0F, 0.0F},
                     .outgoing_handle = {straight_handle, 0.0F, 0.0F}},
      CurvePointSpec{.position = {stored_offset, -stored_radius, 0.0F},
                     .incoming_handle = {-straight_handle, 0.0F, 0.0F},
                     .outgoing_handle = {arc_handle, 0.0F, 0.0F}},
  };
  return CurveDocument::make_bezier(points, true);
}

Result<CurveDocument>
CurveDocument::from_authored_curve(const AuthoredCurve &authored) {
  if (auto error = validate_authored(authored)) {
    return Result<CurveDocument>::failure(std::move(*error));
  }
  CurveDocument result;
  auto data = std::make_shared<CurveDocumentData>();
  data->points = authored.points;
  data->segments = authored.segments;
  data->closed = authored.closed;
  data->identity = next_content_identity();
  result.data_ = std::move(data);
  result.identity_high_water_ = authored.identity_high_water;
  return Result<CurveDocument>::success(std::move(result));
}

std::size_t CurveDocument::point_count() const noexcept { return points().size(); }

std::size_t CurveDocument::segment_count() const noexcept { return segments().size(); }

bool CurveDocument::closed() const noexcept { return data_ && data_->closed; }

std::span<const CurveControlPoint> CurveDocument::points() const noexcept {
  return data_ ? std::span<const CurveControlPoint>(data_->points)
               : std::span<const CurveControlPoint>{};
}

std::span<const CurveSegmentId> CurveDocument::segments() const noexcept {
  return data_ ? std::span<const CurveSegmentId>(data_->segments)
               : std::span<const CurveSegmentId>{};
}

CurveContentIdentity CurveDocument::content_identity() const noexcept {
  return data_ ? data_->identity : CurveContentIdentity{};
}

std::uint64_t CurveDocument::content_revision() const noexcept {
  return data_ ? data_->revision : 0;
}

CurveIdentityHighWater CurveDocument::identity_high_water() const noexcept {
  return identity_high_water_;
}

void CurveDocument::preserve_identity_high_water(
    CurveIdentityHighWater observed) noexcept {
  identity_high_water_.next_point_index =
      std::max(identity_high_water_.next_point_index,
               std::min(observed.next_point_index, kExhaustedIndex));
  identity_high_water_.next_segment_index =
      std::max(identity_high_water_.next_segment_index,
               std::min(observed.next_segment_index, kExhaustedIndex));
}

AuthoredCurve CurveDocument::authored_curve() const {
  AuthoredCurve result;
  result.points.assign(points().begin(), points().end());
  result.segments.assign(segments().begin(), segments().end());
  result.closed = closed();
  result.identity_high_water = identity_high_water_;
  return result;
}

bool CurveDocument::has_same_geometry(const CurveDocument &other) const noexcept {
  if (data_ == other.data_) {
    return true;
  }
  if (closed() != other.closed() || point_count() != other.point_count() ||
      !std::equal(segments().begin(), segments().end(),
                   other.segments().begin(), other.segments().end())) {
    return false;
  }
  for (std::size_t i = 0; i < point_count(); ++i) {
    if (points()[i].id != other.points()[i].id ||
        !same_point(points()[i], point_spec(other.points()[i]))) {
      return false;
    }
  }
  return true;
}

Result<OperationResult> CurveDocument::validate() const {
  if (auto error = validate_authored(points(), segments(), closed(),
                                     identity_high_water_)) {
    return Result<OperationResult>::failure(std::move(*error));
  }
  return unchanged();
}

void CurveDocument::publish(AuthoredCurve authored) {
  auto data = std::make_shared<CurveDocumentData>();
  data->points = std::move(authored.points);
  data->segments = std::move(authored.segments);
  data->closed = authored.closed;
  data->identity = next_content_identity();
  data->revision = next_revision(content_revision());
  preserve_identity_high_water(authored.identity_high_water);
  data_ = std::move(data);
}

Result<OperationResult> CurveDocument::append_point(const CurvePointSpec &point) {
  if (!valid_point(point)) {
    return Result<OperationResult>::failure(curve_error(
        ErrorCode::InvalidArgument, "Curve controls must have finite representable positions."));
  }
  if (point_count() >= kMaxPoints ||
      !can_allocate(identity_high_water_.next_point_index) ||
      (point_count() != 0 && !can_allocate(identity_high_water_.next_segment_index))) {
    return Result<OperationResult>::failure(allocation_exhausted());
  }
  auto authored = authored_curve();
  const CurvePointId id{
      static_cast<std::uint32_t>(authored.identity_high_water.next_point_index++), 1};
  authored.points.push_back({id, point.position, point.incoming_handle, point.outgoing_handle});
  if (authored.points.size() > 1U) {
    authored.segments.push_back(
        {static_cast<std::uint32_t>(authored.identity_high_water.next_segment_index++), 1});
  }
  publish(std::move(authored));
  return changed(true);
}

Result<OperationResult> CurveDocument::set_point(CurvePointId id,
                                                const CurvePointSpec &point) {
  const CurvePointEdit edit{.id = id, .point = point};
  return set_points(std::span<const CurvePointEdit>(&edit, 1U));
}

Result<OperationResult>
CurveDocument::set_points(std::span<const CurvePointEdit> edits) {
  if (edits.empty()) {
    return unchanged();
  }

  std::unordered_map<std::uint32_t, std::size_t> point_indices;
  point_indices.reserve(point_count());
  for (std::size_t index = 0; index < point_count(); ++index) {
    point_indices.emplace(points()[index].id.index, index);
  }

  std::unordered_set<std::uint32_t> edited_ids;
  edited_ids.reserve(edits.size());
  std::vector<std::size_t> edit_indices;
  edit_indices.reserve(edits.size());
  bool has_change = false;
  for (const CurvePointEdit &edit : edits) {
    const auto found = edit.id.generation == 1U
                           ? point_indices.find(edit.id.index)
                           : point_indices.end();
    if (found == point_indices.end()) {
      return Result<OperationResult>::failure(missing_id(
          edit.id, identity_high_water_.next_point_index, "point"));
    }
    if (!edited_ids.insert(edit.id.index).second) {
      return Result<OperationResult>::failure(curve_error(
          ErrorCode::InvalidArgument,
          "Curve point edits contain a duplicate point ID."));
    }
    if (!valid_point(edit.point)) {
      return Result<OperationResult>::failure(curve_error(
          ErrorCode::InvalidArgument,
          "Curve controls must have finite representable positions."));
    }
    edit_indices.push_back(found->second);
    has_change = has_change || !same_point(points()[found->second], edit.point);
  }

  if (!has_change) {
    return unchanged();
  }

  auto authored = authored_curve();
  for (std::size_t edit_index = 0; edit_index < edits.size(); ++edit_index) {
    const CurvePointEdit &edit = edits[edit_index];
    authored.points[edit_indices[edit_index]] = {
        edit.id, edit.point.position, edit.point.incoming_handle,
        edit.point.outgoing_handle};
  }
  publish(std::move(authored));
  return changed(false);
}

Result<OperationResult> CurveDocument::toggle_point_smoothness(CurvePointId id) {
  const auto controls = points();
  const auto found = std::find_if(controls.begin(), controls.end(),
                                [id](const auto &entry) { return entry.id == id; });
  if (found == controls.end()) {
    return Result<OperationResult>::failure(
        missing_id(id, identity_high_water_.next_point_index, "point"));
  }
  CurvePointSpec replacement{.position = found->position};
  if (!same(found->incoming_handle, {}) || !same(found->outgoing_handle, {})) {
    return set_point(id, replacement);
  }

  const auto index = static_cast<std::size_t>(found - controls.begin());
  const bool has_previous = index != 0U || closed();
  const bool has_next = index + 1U != controls.size() || closed();
  const Double3 incoming = has_previous
      ? wide(found->position) -
            wide(controls[index == 0U ? controls.size() - 1U : index - 1U].position)
      : Double3{};
  const Double3 outgoing = has_next
      ? wide(controls[(index + 1U) % controls.size()].position) - wide(found->position)
      : Double3{};
  const double incoming_length = std::sqrt(dot(incoming, incoming));
  const double outgoing_length = std::sqrt(dot(outgoing, outgoing));
  if (incoming_length == 0.0 && outgoing_length == 0.0) {
    return unchanged();
  }
  const Double3 incoming_direction = incoming_length == 0.0
      ? Double3{} : incoming * (1.0 / incoming_length);
  const Double3 outgoing_direction = outgoing_length == 0.0
      ? Double3{} : outgoing * (1.0 / outgoing_length);
  Double3 tangent = incoming_direction + outgoing_direction;
  const double tangent_length = std::sqrt(dot(tangent, tangent));
  // An exact reversal (including a two-anchor closed path) has no bisector.
  // Keep a deterministic tangent along the outgoing chord in that case.
  tangent = tangent_length == 0.0
      ? outgoing_direction : tangent * (1.0 / tangent_length);
  const Double3 incoming_handle = tangent *
      (-(has_previous ? incoming_length : outgoing_length) / 3.0);
  const Double3 outgoing_handle = tangent *
      ((has_next ? outgoing_length : incoming_length) / 3.0);
  if (!representable(incoming_handle) || !representable(outgoing_handle)) {
    return Result<OperationResult>::failure(curve_error(
        ErrorCode::InvalidArgument, "Smooth curve handles are not representable."));
  }
  replacement.incoming_handle = narrow(incoming_handle);
  replacement.outgoing_handle = narrow(outgoing_handle);
  return set_point(id, replacement);
}

Result<OperationResult> CurveDocument::remove_point(CurvePointId id) {
  const auto found = std::find_if(points().begin(), points().end(),
                                  [id](const auto &entry) { return entry.id == id; });
  if (found == points().end()) {
    return Result<OperationResult>::failure(
        missing_id(id, identity_high_water_.next_point_index, "point"));
  }
  const auto index = static_cast<std::size_t>(found - points().begin());
  auto authored = authored_curve();
  authored.points.erase(authored.points.begin() + static_cast<std::ptrdiff_t>(index));
  if (authored.points.size() < 2U) {
    authored.closed = false;
    authored.segments.clear();
  } else {
    const auto segment_index = std::min(index, authored.segments.size() - 1U);
    authored.segments.erase(authored.segments.begin() +
                             static_cast<std::ptrdiff_t>(segment_index));
  }
  publish(std::move(authored));
  return changed(true);
}

Result<OperationResult> CurveDocument::set_closed(bool value) {
  if (value == closed()) {
    return unchanged();
  }
  if (value && point_count() < 2U) {
    return Result<OperationResult>::failure(curve_error(
        ErrorCode::InvalidArgument, "A closed curve needs at least two points."));
  }
  if (value && !can_allocate(identity_high_water_.next_segment_index)) {
    return Result<OperationResult>::failure(allocation_exhausted());
  }
  auto authored = authored_curve();
  authored.closed = value;
  if (value) {
    authored.segments.push_back(
        {static_cast<std::uint32_t>(authored.identity_high_water.next_segment_index++), 1});
  } else {
    authored.segments.pop_back();
  }
  publish(std::move(authored));
  return changed(true);
}

Result<OperationResult> CurveDocument::split_segment(CurveSegmentId id,
                                                    double parameter) {
  const auto found = std::find(segments().begin(), segments().end(), id);
  if (found == segments().end()) {
    return Result<OperationResult>::failure(
        missing_id(id, identity_high_water_.next_segment_index, "segment"));
  }
  if (!std::isfinite(parameter) || parameter <= 0.0 || parameter >= 1.0) {
    return Result<OperationResult>::failure(curve_error(
        ErrorCode::InvalidArgument, "Curve split parameter must be strictly between zero and one."));
  }
  if (point_count() >= kMaxPoints ||
      !can_allocate(identity_high_water_.next_point_index) ||
      !can_allocate(identity_high_water_.next_segment_index)) {
    return Result<OperationResult>::failure(allocation_exhausted());
  }
  const auto index = static_cast<std::size_t>(found - segments().begin());
  // Work relative to the first anchor so the precision budget measures the
  // authored feature, never its distance from the world/document origin.
  const auto origin = wide(points()[index].position);
  const auto controls = relative_segment_controls(points(), index, origin);
  const auto a = interpolate(controls[0], controls[1], parameter);
  const auto b = interpolate(controls[1], controls[2], parameter);
  const auto c = interpolate(controls[2], controls[3], parameter);
  const auto d = interpolate(a, b, parameter);
  const auto e = interpolate(b, c, parameter);
  const auto position = interpolate(d, e, parameter);
  const auto absolute_position = origin + position;
  if (!representable(absolute_position)) {
    return Result<OperationResult>::failure(curve_error(
        ErrorCode::ValidationFailed, "Split curve controls exceed the supported numeric range."));
  }
  const auto stored_position = narrow(absolute_position);
  const auto stored_relative_position = wide(stored_position) - origin;
  const auto incoming = d - stored_relative_position;
  const auto outgoing = e - stored_relative_position;
  const auto first_outgoing = a - controls[0];
  const auto last_incoming = c - controls[3];
  if (!representable(incoming) || !representable(outgoing) ||
      !representable(first_outgoing) || !representable(last_incoming)) {
    return Result<OperationResult>::failure(curve_error(
        ErrorCode::ValidationFailed, "Split curve controls exceed the supported numeric range."));
  }
  auto authored = authored_curve();
  const CurvePointId point_id{
      static_cast<std::uint32_t>(authored.identity_high_water.next_point_index++), 1};
  const CurveSegmentId segment_id{
      static_cast<std::uint32_t>(authored.identity_high_water.next_segment_index++), 1};
  authored.points[index].outgoing_handle = narrow(first_outgoing);
  authored.points[(index + 1U) % point_count()].incoming_handle = narrow(last_incoming);
  authored.points.insert(authored.points.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                          {point_id, stored_position, narrow(incoming), narrow(outgoing)});
  authored.segments.insert(authored.segments.begin() + static_cast<std::ptrdiff_t>(index + 1U),
                            segment_id);
  // Cubic Bernstein weights are nonnegative and sum to one: bounding every
  // control displacement also bounds the pointwise error of each split half.
  const double error_squared = control_hull_diameter_squared(controls) *
                               kSplitRelativeError * kSplitRelativeError;
  if (!controls_within_error(relative_segment_controls(authored.points, index, origin),
                              {controls[0], a, d, position}, error_squared) ||
      !controls_within_error(relative_segment_controls(authored.points, index + 1U, origin),
                              {position, e, c, controls[3]}, error_squared)) {
    return Result<OperationResult>::failure(curve_error(
        ErrorCode::ValidationFailed,
        "Split curve controls cannot preserve the shape within local storage precision."));
  }
  if (auto error = validate_authored(authored)) {
    return Result<OperationResult>::failure(std::move(*error));
  }
  publish(std::move(authored));
  return changed(true);
}

Result<OperationResult> CurveDocument::transform(Transform3 transform) {
  if (!finite(transform.x_axis) || !finite(transform.y_axis) ||
      !finite(transform.z_axis) || !finite(transform.origin)) {
    return Result<OperationResult>::failure(curve_error(
        ErrorCode::InvalidArgument, "Curve transform must be finite."));
  }
  auto authored = authored_curve();
  bool any_changed = false;
  for (auto &point : authored.points) {
    const auto position = transform_vector(transform, point.position) + wide(transform.origin);
    const auto incoming = transform_vector(transform, point.incoming_handle);
    const auto outgoing = transform_vector(transform, point.outgoing_handle);
    if (!representable(position) || !representable(incoming) ||
        !representable(outgoing)) {
      return Result<OperationResult>::failure(curve_error(
          ErrorCode::InvalidArgument, "Transformed curve controls exceed the supported numeric range."));
    }
    const CurvePointSpec value{narrow(position), narrow(incoming), narrow(outgoing)};
    if (!valid_point(value)) {
      return Result<OperationResult>::failure(curve_error(
          ErrorCode::InvalidArgument, "Transformed curve controls exceed the supported numeric range."));
    }
    any_changed = any_changed || !same_point(point, value);
    point = {point.id, value.position, value.incoming_handle, value.outgoing_handle};
  }
  if (!any_changed) {
    return unchanged();
  }
  publish(std::move(authored));
  return changed(false);
}

CurveBounds CurveDocument::bounds() const noexcept {
  BoundsAccumulator bounds;
  for (const auto &point : points()) {
    bounds.include(wide(point.position));
  }
  for (std::size_t i = 0; i < segment_count(); ++i) {
    const auto controls = segment_controls(points(), i);
    include_axis_extrema(controls,
                         {controls[0].x, controls[1].x, controls[2].x, controls[3].x}, bounds);
    include_axis_extrema(controls,
                         {controls[0].y, controls[1].y, controls[2].y, controls[3].y}, bounds);
    include_axis_extrema(controls,
                         {controls[0].z, controls[1].z, controls[2].z, controls[3].z}, bounds);
  }
  if (!bounds.valid) {
    return {};
  }
  return {{lower_float(bounds.min.x), lower_float(bounds.min.y), lower_float(bounds.min.z)},
          {upper_float(bounds.max.x), upper_float(bounds.max.y), upper_float(bounds.max.z)}, true};
}

Result<CurveEvaluation> CurveDocument::evaluate(CurveSegmentId id,
                                               double parameter) const {
  const auto found = std::find(segments().begin(), segments().end(), id);
  if (found == segments().end()) {
    return Result<CurveEvaluation>::failure(
        missing_id(id, identity_high_water_.next_segment_index, "segment"));
  }
  if (!std::isfinite(parameter) || parameter < 0.0 || parameter > 1.0) {
    return Result<CurveEvaluation>::failure(curve_error(
        ErrorCode::InvalidArgument, "Curve parameter must be between zero and one."));
  }
  const auto controls = segment_controls(
      points(), static_cast<std::size_t>(found - segments().begin()));
  const auto position = evaluate_position(controls, parameter);
  const auto tangent = evaluate_tangent(controls, parameter);
  if (!representable(position) || !representable(tangent)) {
    return Result<CurveEvaluation>::failure(curve_error(
        ErrorCode::ValidationFailed, "Evaluated curve data exceeds the supported numeric range."));
  }
  return Result<CurveEvaluation>::success({narrow(position), narrow(tangent)});
}

Result<std::vector<CurveSample>>
CurveDocument::sample(CurveSamplingOptions options) const {
  if (!std::isfinite(options.tolerance) || options.tolerance <= 0.0 ||
      options.max_depth > 24 || options.max_samples == 0 ||
      options.max_samples > kMaxPoints) {
    return Result<std::vector<CurveSample>>::failure(curve_error(
        ErrorCode::InvalidArgument, "Curve sampling options are out of range."));
  }
  CurveSampler sampler{.options = options};
  sampler.samples.reserve(std::min<std::size_t>(options.max_samples,
                                               segment_count() * 4U + 1U));
  if (point_count() == 1U) {
    sampler.samples.push_back({.position = points().front().position});
  }
  for (std::size_t i = 0; i < segment_count(); ++i) {
    const auto controls = segment_controls(points(), i);
    if (!sampler.append(controls, segments()[i], 0.0) ||
        !sampler.subdivide(controls, controls, segments()[i], 0.0, 1.0, 0)) {
      return Result<std::vector<CurveSample>>::failure(std::move(*sampler.error));
    }
  }
  return Result<std::vector<CurveSample>>::success(std::move(sampler.samples));
}

CurveArcLengthPath::CurveArcLengthPath(
    std::shared_ptr<const CurveArcLengthPathData> data) noexcept
    : data_(std::move(data)) {}

Result<CurveArcLengthPath>
CurveArcLengthPath::build(const CurveDocument &document,
                          Transform3 world_from_curve,
                          CurveSamplingOptions options) {
  if (!std::isfinite(options.tolerance) || options.tolerance <= 0.0 ||
      options.max_depth > 24 || options.max_samples == 0 ||
      options.max_samples > kMaxPoints) {
    return Result<CurveArcLengthPath>::failure(curve_path_error(
        ErrorCode::InvalidArgument,
        "Curve arc-length sampling options are out of range."));
  }
  try {
    CurveDocument world_document = document;
    auto transformed = world_document.transform(world_from_curve);
    if (!transformed.ok()) {
      return Result<CurveArcLengthPath>::failure(
          std::move(transformed).error());
    }

    CurveArcLengthBuilder builder{.options = options};
    builder.stations.reserve(std::min<std::size_t>(
        options.max_samples, world_document.segment_count() * 4U + 1U));
    if (world_document.point_count() == 1U) {
      builder.stations.push_back(
          {.position = world_document.points().front().position});
    }
    for (std::size_t index = 0; index < world_document.segment_count();
         ++index) {
      const auto controls = segment_controls(world_document.points(), index);
      if (!builder.append(controls, world_document.segments()[index], 0.0) ||
          !builder.subdivide(controls, controls,
                             world_document.segments()[index], 0.0, 1.0,
                             0U)) {
        return Result<CurveArcLengthPath>::failure(
            std::move(*builder.error));
      }
    }

    auto data = std::make_shared<CurveArcLengthPathData>();
    data->world_document = std::move(world_document);
    data->stations.resize(builder.stations.size());
    for (std::size_t index = 0; index < builder.stations.size(); ++index) {
      if (index > 0U) {
        data->total_length += curve_path_distance(
            builder.stations[index - 1U].position,
            builder.stations[index].position);
      }
      if (!std::isfinite(data->total_length)) {
        return Result<CurveArcLengthPath>::failure(curve_path_error(
            ErrorCode::ValidationFailed,
            "Curve arc length exceeds the supported numeric range."));
      }
      const CurveSample &sample = builder.stations[index];
      data->stations[index] = {
          .distance = data->total_length,
          .evaluation = {
              .position = sample.position,
              .tangent = sample.tangent,
              .segment = sample.segment,
              .parameter = sample.parameter,
          },
      };
    }
    std::size_t group_start = 0U;
    while (group_start < data->stations.size()) {
      std::size_t group_end = group_start + 1U;
      while (group_end < data->stations.size() &&
             data->stations[group_end].distance ==
                 data->stations[group_start].distance) {
        ++group_end;
      }
      for (std::size_t index = group_start; index < group_end; ++index) {
        CurvePathEvaluation &evaluation = data->stations[index].evaluation;
        evaluation.tangent = curve_path_direction(
            data->stations, evaluation.position, evaluation.tangent,
            group_start, group_end);
      }
      group_start = group_end;
    }
    data->closed = data->world_document.closed();
    return Result<CurveArcLengthPath>::success(
        CurveArcLengthPath{std::move(data)});
  } catch (const std::bad_alloc &) {
    return Result<CurveArcLengthPath>::failure(curve_path_error(
        ErrorCode::InternalError,
        "Not enough memory to build the Curve arc-length path."));
  } catch (const std::length_error &) {
    return Result<CurveArcLengthPath>::failure(curve_path_error(
        ErrorCode::InternalError,
        "Curve arc-length path allocation is too large."));
  }
}

double CurveArcLengthPath::total_length() const noexcept {
  return data_->total_length;
}

bool CurveArcLengthPath::closed() const noexcept { return data_->closed; }

std::span<const CurveArcLengthStation>
CurveArcLengthPath::stations() const noexcept {
  return data_->stations;
}

Result<CurvePathEvaluation>
CurveArcLengthPath::evaluate_at_distance(double distance) const {
  if (!std::isfinite(distance) || distance < 0.0 ||
      distance > data_->total_length) {
    return Result<CurvePathEvaluation>::failure(curve_path_error(
        ErrorCode::InvalidArgument,
        "Curve path distance must be finite and within the path length."));
  }
  if (data_->stations.empty()) {
    return Result<CurvePathEvaluation>::failure(curve_path_error(
        ErrorCode::ValidationFailed,
        "An empty Curve has no position to evaluate."));
  }

  if (distance == 0.0) {
    return Result<CurvePathEvaluation>::success(
        data_->stations.front().evaluation);
  }
  if (distance == data_->total_length) {
    return Result<CurvePathEvaluation>::success(
        data_->stations.back().evaluation);
  }

  const auto right = std::lower_bound(
      data_->stations.begin(), data_->stations.end(), distance,
      [](const CurveArcLengthStation &station, double target) {
        return station.distance < target;
      });
  const std::size_t right_index =
      static_cast<std::size_t>(right - data_->stations.begin());
  if (right != data_->stations.end() && right->distance == distance) {
    return Result<CurvePathEvaluation>::success(
        right->evaluation);
  }
  if (right_index == 0U || right_index >= data_->stations.size()) {
    return Result<CurvePathEvaluation>::failure(curve_path_error(
        ErrorCode::InternalError,
        "Curve arc-length lookup could not resolve a distance bracket."));
  }

  const std::size_t left_index = right_index - 1U;
  const CurveArcLengthStation &left = data_->stations[left_index];
  const CurveArcLengthStation &right_station = data_->stations[right_index];
  if (!left.evaluation.segment.valid() ||
      left.evaluation.segment != right_station.evaluation.segment ||
      right_station.distance <= left.distance) {
    return Result<CurvePathEvaluation>::failure(curve_path_error(
        ErrorCode::InternalError,
        "Curve arc-length lookup encountered invalid segment provenance."));
  }
  const double fraction =
      (distance - left.distance) /
      (right_station.distance - left.distance);
  const double parameter =
      left.evaluation.parameter +
      (right_station.evaluation.parameter - left.evaluation.parameter) *
          fraction;
  auto evaluated = data_->world_document.evaluate(left.evaluation.segment,
                                                   parameter);
  if (!evaluated.ok()) {
    return Result<CurvePathEvaluation>::failure(
        std::move(evaluated).error());
  }
  const CurveEvaluation &value = evaluated.value();
  return Result<CurvePathEvaluation>::success({
      .position = value.position,
      .tangent = curve_path_direction(data_->stations, value.position,
                                      value.tangent, right_index,
                                      right_index),
      .segment = left.evaluation.segment,
      .parameter = parameter,
  });
}

Result<std::vector<std::byte>> serialize_curve_document(const CurveDocument &document) {
  try {
    const auto authored = document.authored_curve();
    if (auto error = validate_authored(authored)) {
      return Result<std::vector<std::byte>>::failure(std::move(*error));
    }
    CurveByteWriter writer(kCodecHeaderSize + authored.points.size() * kCodecPointSize +
                            authored.segments.size() * kCodecSegmentSize);
    for (const auto byte : kCurveMagic) {
      writer.integer(std::to_integer<unsigned int>(byte), 1);
    }
    writer.integer(1, 2);
    writer.integer(0, 2);
    writer.integer(authored.closed ? 1U : 0U, 4);
    writer.integer(authored.points.size(), 4);
    writer.integer(authored.segments.size(), 4);
    writer.integer(0, 4);
    writer.integer(authored.identity_high_water.next_point_index, 8);
    writer.integer(authored.identity_high_water.next_segment_index, 8);
    for (const auto &point : authored.points) {
      writer.integer(point.id.index, 4);
      writer.integer(point.id.generation, 4);
      writer.vector(point.position);
      writer.vector(point.incoming_handle);
      writer.vector(point.outgoing_handle);
    }
    for (const auto segment : authored.segments) {
      writer.integer(segment.index, 4);
      writer.integer(segment.generation, 4);
    }
    return Result<std::vector<std::byte>>::success(std::move(writer).finish());
  } catch (const std::bad_alloc &) {
    return Result<std::vector<std::byte>>::failure(curve_error(
        ErrorCode::IoError, "Not enough memory to encode the curve document."));
  }
}

Result<CurveDocument> deserialize_curve_document(std::span<const std::byte> bytes) {
  if (bytes.size() < kCodecHeaderSize ||
      !std::equal(kCurveMagic.begin(), kCurveMagic.end(), bytes.begin())) {
    return Result<CurveDocument>::failure(curve_error(
        ErrorCode::IoError, "Invalid or truncated curve document header."));
  }
  CurveByteReader reader(bytes.subspan(4));
  const auto major = reader.integer(2);
  const auto minor = reader.integer(2);
  if (major != 1 || minor != 0) {
    return Result<CurveDocument>::failure(curve_error(
        ErrorCode::UnsupportedOperation, "Unsupported curve document version."));
  }
  const auto flags = reader.integer(4);
  const auto point_count = reader.integer(4);
  const auto segment_count = reader.integer(4);
  const auto reserved = reader.integer(4);
  const CurveIdentityHighWater high_water{reader.integer(8), reader.integer(8)};
  if (flags > 1 || reserved != 0 || point_count > kMaxPoints ||
      segment_count > kMaxPoints ||
      bytes.size() != kCodecHeaderSize + point_count * kCodecPointSize +
                        segment_count * kCodecSegmentSize) {
    return Result<CurveDocument>::failure(curve_error(
        ErrorCode::IoError, "Curve document record counts, flags or payload length are invalid."));
  }
  try {
    AuthoredCurve authored;
    authored.closed = flags != 0;
    authored.identity_high_water = high_water;
    authored.points.reserve(static_cast<std::size_t>(point_count));
    authored.segments.reserve(static_cast<std::size_t>(segment_count));
    for (std::uint64_t i = 0; i < point_count; ++i) {
      CurveControlPoint point;
      point.id = {static_cast<std::uint32_t>(reader.integer(4)),
                  static_cast<std::uint32_t>(reader.integer(4))};
      point.position = reader.vector();
      point.incoming_handle = reader.vector();
      point.outgoing_handle = reader.vector();
      authored.points.push_back(point);
    }
    for (std::uint64_t i = 0; i < segment_count; ++i) {
      authored.segments.push_back({static_cast<std::uint32_t>(reader.integer(4)),
                                   static_cast<std::uint32_t>(reader.integer(4))});
    }
    return CurveDocument::from_authored_curve(authored);
  } catch (const std::bad_alloc &) {
    return Result<CurveDocument>::failure(curve_error(
        ErrorCode::IoError, "Not enough memory to decode the curve document."));
  } catch (const std::length_error &) {
    return Result<CurveDocument>::failure(curve_error(
        ErrorCode::IoError, "Curve document allocation is too large."));
  }
}

} // namespace quader::modeling
