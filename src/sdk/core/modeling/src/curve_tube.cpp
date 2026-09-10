////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/curves/curve_tube.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <new>
#include <numbers>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace quader::modeling {
namespace {

constexpr double kMinimumRadius = 0.001;
constexpr double kMaximumRadius = 100000.0;
constexpr std::uint32_t kMinimumRadialResolution = 3U;
constexpr std::uint32_t kMaximumRadialResolution = 128U;
// A derived preview mesh must remain comfortably bounded even when the Curve
// sampler itself is configured near its million-sample safety limit.
constexpr std::uint64_t kMaximumOutputVertices = 4'000'000U;
constexpr std::uint64_t kMaximumOutputIndices = 24'000'000U;
constexpr double kDirectionEpsilon = 1.0e-24;

struct WideVec3 final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct TubeStation final {
  WideVec3 position;
  WideVec3 sampled_tangent;
  WideVec3 tangent;
  WideVec3 normal;
  WideVec3 binormal;
  double distance = 0.0;
};

struct BuiltTubeData final {
  MeshPayload payload;
  AuthoredTopology topology;
};

[[nodiscard]] Error tube_error(ErrorCode code, std::string message) {
  return Error{
      .code = code,
      .message = message,
      .diagnostics = {{.code = "curve.tube",
                       .severity = DiagnosticSeverity::Error,
                       .path = "curve",
                       .message = std::move(message)}},
  };
}

[[nodiscard]] WideVec3 wide(Vec3 value) noexcept {
  return {value.x, value.y, value.z};
}

[[nodiscard]] WideVec3 operator+(WideVec3 left, WideVec3 right) noexcept {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] WideVec3 operator-(WideVec3 left, WideVec3 right) noexcept {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] WideVec3 operator*(WideVec3 value, double scalar) noexcept {
  return {value.x * scalar, value.y * scalar, value.z * scalar};
}

[[nodiscard]] double dot(WideVec3 left, WideVec3 right) noexcept {
  return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] WideVec3 cross(WideVec3 left, WideVec3 right) noexcept {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

[[nodiscard]] double length(WideVec3 value) noexcept {
  return std::sqrt(dot(value, value));
}

[[nodiscard]] std::optional<WideVec3>
normalized(WideVec3 value) noexcept {
  const double magnitude = length(value);
  if (!std::isfinite(magnitude) || magnitude <= kDirectionEpsilon) {
    return std::nullopt;
  }
  return value * (1.0 / magnitude);
}

[[nodiscard]] bool same_position(WideVec3 left, WideVec3 right) noexcept {
  return left.x == right.x && left.y == right.y && left.z == right.z;
}

[[nodiscard]] bool finite(WideVec3 value) noexcept {
  return std::isfinite(value.x) && std::isfinite(value.y) &&
         std::isfinite(value.z);
}

[[nodiscard]] std::optional<Vec3> narrow(WideVec3 value) noexcept {
  constexpr double kFloatMaximum =
      static_cast<double>(std::numeric_limits<float>::max());
  if (!finite(value) || std::abs(value.x) > kFloatMaximum ||
      std::abs(value.y) > kFloatMaximum ||
      std::abs(value.z) > kFloatMaximum) {
    return std::nullopt;
  }
  return Vec3{static_cast<float>(value.x), static_cast<float>(value.y),
              static_cast<float>(value.z)};
}

[[nodiscard]] WideVec3 rotate_around_axis(WideVec3 value, WideVec3 axis,
                                          double radians) noexcept {
  const double cosine = std::cos(radians);
  const double sine = std::sin(radians);
  return value * cosine + cross(axis, value) * sine +
         axis * (dot(axis, value) * (1.0 - cosine));
}

[[nodiscard]] WideVec3 initial_normal(WideVec3 tangent) noexcept {
  const double x_alignment = std::abs(tangent.x);
  const double y_alignment = std::abs(tangent.y);
  const double z_alignment = std::abs(tangent.z);
  WideVec3 reference{1.0, 0.0, 0.0};
  if (y_alignment < x_alignment && y_alignment <= z_alignment) {
    reference = {0.0, 1.0, 0.0};
  } else if (z_alignment < x_alignment && z_alignment < y_alignment) {
    reference = {0.0, 0.0, 1.0};
  }
  return *normalized(reference - tangent * dot(reference, tangent));
}

[[nodiscard]] WideVec3 transport_normal(WideVec3 normal,
                                        WideVec3 from_tangent,
                                        WideVec3 to_tangent) noexcept {
  const double cosine =
      std::clamp(dot(from_tangent, to_tangent), -1.0, 1.0);
  const WideVec3 axis = cross(from_tangent, to_tangent);
  const double sine = length(axis);
  WideVec3 transported = normal;
  if (sine > kDirectionEpsilon) {
    transported = rotate_around_axis(normal, axis * (1.0 / sine),
                                     std::atan2(sine, cosine));
  } else if (cosine < 0.0) {
    // Antiparallel tangents have no unique minimum rotation. Keeping the
    // current normal as the half-turn axis is deterministic and continuous
    // with the already transported frame.
    transported = normal;
  }
  const std::optional<WideVec3> projected = normalized(
      transported - to_tangent * dot(transported, to_tangent));
  return projected.value_or(initial_normal(to_tangent));
}

[[nodiscard]] WideVec3 fallback_tangent(
    const std::vector<TubeStation> &stations, std::size_t index,
    bool closed) noexcept {
  const std::size_t count = stations.size();
  const bool has_previous = closed || index > 0U;
  const bool has_next = closed || index + 1U < count;
  const std::size_t previous = index == 0U ? count - 1U : index - 1U;
  const std::size_t next = index + 1U == count ? 0U : index + 1U;
  const std::optional<WideVec3> incoming =
      has_previous
          ? normalized(stations[index].position - stations[previous].position)
          : std::nullopt;
  const std::optional<WideVec3> outgoing =
      has_next
          ? normalized(stations[next].position - stations[index].position)
          : std::nullopt;
  if (incoming.has_value() && outgoing.has_value()) {
    if (const auto bisector = normalized(*incoming + *outgoing)) {
      return *bisector;
    }
    return *outgoing;
  }
  if (outgoing.has_value()) {
    return *outgoing;
  }
  return incoming.value_or(WideVec3{});
}

[[nodiscard]] bool checked_multiply(std::uint64_t left, std::uint64_t right,
                                    std::uint64_t &result) noexcept {
  if (right != 0U && left > std::numeric_limits<std::uint64_t>::max() / right) {
    return false;
  }
  result = left * right;
  return true;
}

[[nodiscard]] Result<BuiltTubeData>
build_tube(const CurveDocument &document, CurveTubeOptions options,
           bool build_editable_document) {
  Result<std::vector<CurveSample>> sampled = document.sample(options.sampling);
  if (!sampled.ok()) {
    return Result<BuiltTubeData>::failure(std::move(sampled).error());
  }

  std::vector<TubeStation> stations;
  stations.reserve(sampled.value().size());
  for (const CurveSample &sample : sampled.value()) {
    const WideVec3 position = wide(sample.position);
    const WideVec3 tangent = wide(sample.tangent);
    if (!finite(position) || !finite(tangent)) {
      return Result<BuiltTubeData>::failure(tube_error(
          ErrorCode::ValidationFailed,
          "Curve tube samples contain non-finite geometry."));
    }
    if (!stations.empty() &&
        same_position(stations.back().position, position)) {
      // Adjacent segments repeat their shared anchor. Retain one ring and the
      // latter segment's outgoing derivative at authored corners.
      stations.back().sampled_tangent = tangent;
      continue;
    }
    stations.push_back({.position = position, .sampled_tangent = tangent});
  }

  if (document.closed() && stations.size() > 1U &&
      same_position(stations.front().position, stations.back().position)) {
    stations.pop_back();
  }
  const std::size_t minimum_stations = document.closed() ? 3U : 2U;
  if (stations.size() < minimum_stations) {
    return Result<BuiltTubeData>::failure(tube_error(
        ErrorCode::ValidationFailed,
        "Curve tube requires a nondegenerate path with enough unique samples."));
  }

  double total_length = 0.0;
  for (std::size_t index = 1U; index < stations.size(); ++index) {
    total_length +=
        length(stations[index].position - stations[index - 1U].position);
    stations[index].distance = total_length;
  }
  if (document.closed()) {
    total_length += length(stations.front().position -
                           stations.back().position);
  }
  if (!std::isfinite(total_length) || total_length <= 0.0) {
    return Result<BuiltTubeData>::failure(tube_error(
        ErrorCode::ValidationFailed,
        "Curve tube requires a path with finite nonzero length."));
  }

  for (std::size_t index = 0U; index < stations.size(); ++index) {
    const std::optional<WideVec3> sampled_direction =
        normalized(stations[index].sampled_tangent);
    stations[index].tangent = sampled_direction.value_or(
        fallback_tangent(stations, index, document.closed()));
    if (!normalized(stations[index].tangent).has_value()) {
      return Result<BuiltTubeData>::failure(tube_error(
          ErrorCode::ValidationFailed,
          "Curve tube could not resolve a path direction."));
    }
    stations[index].tangent = *normalized(stations[index].tangent);
  }

  stations.front().normal = initial_normal(stations.front().tangent);
  stations.front().binormal =
      *normalized(cross(stations.front().tangent, stations.front().normal));
  for (std::size_t index = 1U; index < stations.size(); ++index) {
    stations[index].normal = transport_normal(
        stations[index - 1U].normal, stations[index - 1U].tangent,
        stations[index].tangent);
    stations[index].binormal =
        *normalized(cross(stations[index].tangent, stations[index].normal));
  }

  if (document.closed()) {
    const WideVec3 seam_normal = transport_normal(
        stations.back().normal, stations.back().tangent,
        stations.front().tangent);
    const double residual_twist = std::atan2(
        dot(stations.front().tangent,
            cross(seam_normal, stations.front().normal)),
        std::clamp(dot(seam_normal, stations.front().normal), -1.0, 1.0));
    for (TubeStation &station : stations) {
      const double fraction = station.distance / total_length;
      station.normal = rotate_around_axis(
          station.normal, station.tangent, residual_twist * fraction);
      station.normal = *normalized(
          station.normal - station.tangent *
                               dot(station.normal, station.tangent));
      station.binormal =
          *normalized(cross(station.tangent, station.normal));
    }
  }

  const std::uint64_t ring_stride =
      static_cast<std::uint64_t>(options.radial_resolution) + 1U;
  std::uint64_t vertex_count = 0U;
  const std::uint64_t published_ring_count =
      static_cast<std::uint64_t>(stations.size()) +
      (document.closed() ? 1U : 0U);
  const std::uint64_t longitudinal_segments =
      document.closed() ? stations.size() : stations.size() - 1U;
  std::uint64_t index_count = 0U;
  std::uint64_t topology_vertex_count = 0U;
  std::uint64_t topology_face_count = 0U;
  if (!checked_multiply(published_ring_count, ring_stride, vertex_count) ||
      !checked_multiply(longitudinal_segments, options.radial_resolution,
                        topology_face_count) ||
      !checked_multiply(static_cast<std::uint64_t>(stations.size()),
                        options.radial_resolution,
                        topology_vertex_count) ||
      !checked_multiply(topology_face_count, 6U, index_count) ||
      vertex_count > std::numeric_limits<std::uint32_t>::max() ||
      index_count > std::numeric_limits<std::uint32_t>::max() ||
      topology_vertex_count >= std::numeric_limits<std::uint32_t>::max() ||
      topology_face_count >= std::numeric_limits<std::uint32_t>::max()) {
    return Result<BuiltTubeData>::failure(tube_error(
        ErrorCode::ValidationFailed,
        "Curve tube exceeds the supported vertex or index range."));
  }
  if (vertex_count > kMaximumOutputVertices ||
      index_count > kMaximumOutputIndices) {
    return Result<BuiltTubeData>::failure(tube_error(
        ErrorCode::ValidationFailed,
        "Curve tube exceeds the practical derived-mesh budget."));
  }

  BuiltTubeData built;
  MeshPayload &payload = built.payload;
  if (vertex_count > payload.vertices.max_size() ||
      index_count > payload.indices.max_size() ||
      (build_editable_document &&
       (topology_vertex_count > built.topology.vertices.max_size() ||
        topology_face_count > built.topology.faces.max_size()))) {
    return Result<BuiltTubeData>::failure(tube_error(
        ErrorCode::ValidationFailed,
        "Curve tube exceeds the platform's mesh storage range."));
  }
  payload.content_revision = document.content_revision();
  payload.vertices.reserve(static_cast<std::size_t>(vertex_count));
  payload.indices.reserve(static_cast<std::size_t>(index_count));

  constexpr double kTau = 2.0 * std::numbers::pi_v<double>;
  for (const TubeStation &station : stations) {
    for (std::uint32_t radial = 0U;
         radial <= options.radial_resolution; ++radial) {
      const double fraction = static_cast<double>(radial) /
                              options.radial_resolution;
      const double angle = radial == options.radial_resolution
                               ? 0.0
                               : kTau * fraction;
      const WideVec3 radial_normal =
          station.normal * std::cos(angle) +
          station.binormal * std::sin(angle);
      const std::optional<Vec3> normal = narrow(radial_normal);
      const std::optional<Vec3> position = narrow(
          station.position + radial_normal * options.radius);
      if (!normal.has_value() || !position.has_value()) {
        return Result<BuiltTubeData>::failure(tube_error(
            ErrorCode::ValidationFailed,
            "Curve tube vertices exceed the supported numeric range."));
      }
      payload.vertices.push_back({
          .position = *position,
          .normal = *normal,
          .uv0 = {static_cast<float>(station.distance / total_length),
                  static_cast<float>(fraction)},
          .uv_geometric_normal = *normal,
      });
    }
  }
  if (document.closed()) {
    // Keep the authored start as the geometric/frame seam, but publish it a
    // second time at longitudinal U=1. The closing strip can then interpolate
    // toward 1 instead of wrapping backward to the start ring's U=0.
    for (std::uint32_t radial = 0U;
         radial <= options.radial_resolution; ++radial) {
      MeshVertexPayload seam_vertex = payload.vertices[radial];
      seam_vertex.uv0.x = 1.0F;
      payload.vertices.push_back(seam_vertex);
    }
  }

  const std::uint32_t stride =
      static_cast<std::uint32_t>(ring_stride);
  for (std::size_t ring = 0U; ring < longitudinal_segments; ++ring) {
    const std::size_t next_ring = ring + 1U;
    for (std::uint32_t radial = 0U;
         radial < options.radial_resolution; ++radial) {
      const auto first = static_cast<std::uint32_t>(ring * stride + radial);
      const std::uint32_t first_radial = first + 1U;
      const auto next =
          static_cast<std::uint32_t>(next_ring * stride + radial);
      const std::uint32_t next_radial = next + 1U;
      payload.indices.insert(payload.indices.end(),
                             {first, first_radial, next, first_radial,
                              next_radial, next});
    }
  }
  payload.primitives.push_back({
      .index_offset = 0U,
      .index_count = static_cast<std::uint32_t>(payload.indices.size()),
  });

  if (!build_editable_document) {
    return Result<BuiltTubeData>::success(std::move(built));
  }

  AuthoredTopology &topology = built.topology;
  topology.vertices.reserve(static_cast<std::size_t>(topology_vertex_count));
  topology.faces.reserve(static_cast<std::size_t>(topology_face_count));
  const std::uint32_t authored_stride = options.radial_resolution;
  const auto authored_vertex_id = [authored_stride](
                                      std::size_t ring,
                                      std::uint32_t radial) noexcept {
    return make_id<VertexTag>(
        static_cast<std::uint32_t>(ring * authored_stride + radial + 1U));
  };
  for (std::size_t ring = 0U; ring < stations.size(); ++ring) {
    for (std::uint32_t radial = 0U;
         radial < options.radial_resolution; ++radial) {
      topology.vertices.push_back({
          .id = authored_vertex_id(ring, radial),
          .position = payload.vertices[ring * stride + radial].position,
      });
    }
  }

  std::uint32_t next_face_id = 1U;
  for (std::size_t ring = 0U; ring < longitudinal_segments; ++ring) {
    const std::size_t next_ring =
        ring + 1U == stations.size() ? 0U : ring + 1U;
    const float first_u =
        static_cast<float>(stations[ring].distance / total_length);
    const float next_u =
        document.closed() && next_ring == 0U
            ? 1.0F
            : static_cast<float>(stations[next_ring].distance / total_length);
    for (std::uint32_t radial = 0U;
         radial < options.radial_resolution; ++radial) {
      const std::uint32_t next_radial =
          radial + 1U == options.radial_resolution ? 0U : radial + 1U;
      const float first_v = static_cast<float>(radial) /
                            static_cast<float>(options.radial_resolution);
      const float next_v = static_cast<float>(radial + 1U) /
                           static_cast<float>(options.radial_resolution);
      topology.faces.push_back({
          .id = make_id<FaceTag>(next_face_id++),
          .vertices = {authored_vertex_id(ring, radial),
                       authored_vertex_id(ring, next_radial),
                       authored_vertex_id(next_ring, next_radial),
                       authored_vertex_id(next_ring, radial)},
          .uvs = {{first_u, first_v}, {first_u, next_v},
                  {next_u, next_v}, {next_u, first_v}},
          .material_slot = 0U,
      });
    }
  }
  topology.next_vertex_id =
      static_cast<std::uint32_t>(topology_vertex_count + 1U);
  topology.next_face_id =
      static_cast<std::uint32_t>(topology_face_count + 1U);
  return Result<BuiltTubeData>::success(std::move(built));
}

[[nodiscard]] std::optional<Error>
validate_tube_options(CurveTubeOptions options) {
  if (!std::isfinite(options.radius) || options.radius < kMinimumRadius ||
      options.radius > kMaximumRadius) {
    return tube_error(
        ErrorCode::InvalidArgument,
        "Curve tube radius must be finite and between 0.001 and 100000.");
  }
  if (options.radial_resolution < kMinimumRadialResolution ||
      options.radial_resolution > kMaximumRadialResolution) {
    return tube_error(
        ErrorCode::InvalidArgument,
        "Curve tube radial resolution must be between 3 and 128.");
  }
  return std::nullopt;
}

} // namespace

Result<CurveTubeMesh>
build_curve_tube_mesh(const CurveDocument &document,
                      CurveTubeOptions options) {
  if (std::optional<Error> error = validate_tube_options(options)) {
    return Result<CurveTubeMesh>::failure(std::move(*error));
  }
  try {
    Result<BuiltTubeData> built = build_tube(document, options, true);
    if (!built.ok()) {
      return Result<CurveTubeMesh>::failure(std::move(built).error());
    }
    Result<PolygonDocument> polygon =
        PolygonDocument::from_authored_topology(built.value().topology);
    if (!polygon.ok()) {
      return Result<CurveTubeMesh>::failure(std::move(polygon).error());
    }
    CurveTubeMesh result{
        .payload = std::move(built).value().payload,
        .document = std::move(polygon).value(),
    };
    return Result<CurveTubeMesh>::success(std::move(result));
  } catch (const std::bad_alloc &) {
    return Result<CurveTubeMesh>::failure(tube_error(
        ErrorCode::InternalError,
        "Not enough memory to build the Curve tube."));
  } catch (const std::length_error &) {
    return Result<CurveTubeMesh>::failure(tube_error(
        ErrorCode::InternalError,
        "Curve tube allocation exceeds the platform storage range."));
  }
}

Result<MeshPayload> tessellate_curve_tube(const CurveDocument &document,
                                          CurveTubeOptions options) {
  if (std::optional<Error> error = validate_tube_options(options)) {
    return Result<MeshPayload>::failure(std::move(*error));
  }
  try {
    Result<BuiltTubeData> built = build_tube(document, options, false);
    if (!built.ok()) {
      return Result<MeshPayload>::failure(std::move(built).error());
    }
    return Result<MeshPayload>::success(std::move(built).value().payload);
  } catch (const std::bad_alloc &) {
    return Result<MeshPayload>::failure(tube_error(
        ErrorCode::InternalError,
        "Not enough memory to tessellate the Curve tube."));
  } catch (const std::length_error &) {
    return Result<MeshPayload>::failure(tube_error(
        ErrorCode::InternalError,
        "Curve tube allocation exceeds the platform storage range."));
  }
}

} // namespace quader::modeling
