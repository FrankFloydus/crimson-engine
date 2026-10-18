////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/mesh/stairs.hpp>

#include "polygon_document_native.hpp"

#include <quader/modeling/neutral/geometry/geometry.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <new>
#include <numbers>
#include <numeric>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace quader::modeling {
namespace {

constexpr std::size_t kMaximumVertices = 100'000U;
constexpr std::size_t kMaximumFaces = 200'000U;
constexpr double kFullTurn = 2.0 * std::numbers::pi;

struct PlanPoint final {
  double x = 0.0;
  double z = 0.0;
};

struct WidePoint final {
  double x = 0.0;
  double y = 0.0;
  double z = 0.0;
};

struct Flight final {
  PlanPoint start;
  PlanPoint end;
  double width = 0.0;
  double length = 0.0;
  double start_height = 0.0;
  double end_height = 0.0;
  std::uint32_t steps = 0U;
};

struct Landing final {
  double min_x = 0.0;
  double max_x = 0.0;
  double min_z = 0.0;
  double max_z = 0.0;
};

struct Route final {
  std::vector<Flight> flights;
  std::vector<Landing> landings;
  double width = 0.0;
  double depth = 0.0;
  double sweep = 0.0;
};

/** One canonical rendered triangle projected onto the footprint plane. */
struct SurfaceTriangle final {
  std::array<PlanPoint, 3> points;
  std::array<double, 3> heights;
};

/** Bounds one route panel while retaining every actual exterior face. */
struct VolumeCell final {
  std::vector<PlanPoint> footprint;
  std::vector<FaceId> faces;
  std::vector<SurfaceTriangle> upper;
  std::vector<SurfaceTriangle> lower;
  double min_x = 0.0;
  double max_x = 0.0;
  double min_z = 0.0;
  double max_z = 0.0;
  double minimum_lower = 0.0;
  double maximum_upper = 0.0;
  std::size_t section = 0U;
  std::size_t station = 0U;
};

[[noreturn]] void reject(const char *message) {
  throw std::invalid_argument(message);
}

[[nodiscard]] double cross(PlanPoint a, PlanPoint b, PlanPoint c) {
  return (b.x - a.x) * (c.z - a.z) -
         (b.z - a.z) * (c.x - a.x);
}

[[nodiscard]] double distance(PlanPoint a, PlanPoint b) {
  return std::hypot(a.x - b.x, a.z - b.z);
}

[[nodiscard]] PlanPoint interpolate(PlanPoint a, PlanPoint b, double fraction) {
  if (fraction == 0.0) {
    return a;
  }
  if (fraction == 1.0) {
    return b;
  }
  return {a.x + (b.x - a.x) * fraction,
          a.z + (b.z - a.z) * fraction};
}

[[nodiscard]] WidePoint elevated(PlanPoint point, double height) {
  return {point.x, height, point.z};
}

void require_dimension(double value, const char *message) {
  constexpr double kMaximumDimension =
      static_cast<double>(std::numeric_limits<float>::max()) / 64.0;
  if (!std::isfinite(value) || value < kStairsMinimumDimension ||
      value > kMaximumDimension) {
    reject(message);
  }
}

[[nodiscard]] bool circular(const StairsSpec &spec) {
  return spec.layout == StairsLayout::Arc ||
         spec.layout == StairsLayout::Spiral;
}

void validate_spec(const StairsSpec &spec) {
  switch (spec.layout) {
  case StairsLayout::Straight:
  case StairsLayout::L:
  case StairsLayout::U:
  case StairsLayout::Switchback:
  case StairsLayout::Arc:
  case StairsLayout::Spiral:
    break;
  default:
    reject("Unknown stairs layout.");
  }
  if (spec.surface != StairsSurface::Steps &&
      spec.surface != StairsSurface::Ramp) {
    reject("Unknown stairs surface.");
  }
  if (spec.construction != StairsConstruction::Solid &&
      spec.construction != StairsConstruction::Slab) {
    reject("Unknown stairs construction.");
  }
  if (spec.switchback_arrangement != StairsSwitchbackArrangement::Spread &&
      spec.switchback_arrangement != StairsSwitchbackArrangement::Stacked) {
    reject("Unknown switchback arrangement.");
  }
  require_dimension(spec.height, "Height must be finite and positive.");
  if (spec.construction == StairsConstruction::Slab) {
    require_dimension(spec.slab_thickness,
                      "Slab thickness must be finite and positive.");
  }
  if (spec.surface == StairsSurface::Steps) {
    if (spec.automatic_step_count) {
      require_dimension(spec.preferred_riser,
                        "Preferred riser must be finite and positive.");
    } else if (spec.step_count < kStairsMinimumStepCount ||
               spec.step_count > kStairsMaximumStepCount) {
      reject("Step count must be between 1 and 1024.");
    }
  }
  if (spec.layout != StairsLayout::Straight) {
    require_dimension(spec.flight_width,
                      "Flight width must be finite and positive.");
  }
  if (circular(spec)) {
    require_dimension(spec.inner_radius,
                      "Inner radius must be finite and positive.");
    if (spec.curved_segments < kStairsMinimumCurvedSegments ||
        spec.curved_segments > kStairsMaximumCurvedSegments) {
      reject("Curved segments must be between 3 and 256 per turn.");
    }
    if (spec.layout == StairsLayout::Arc &&
        (!std::isfinite(spec.sweep_degrees) || spec.sweep_degrees <= 0.0 ||
         spec.sweep_degrees > 360.0)) {
      reject("Arc sweep must be greater than zero and at most 360 degrees.");
    }
    if (spec.layout == StairsLayout::Spiral &&
        (!std::isfinite(spec.turns) || spec.turns <= 0.0 ||
         spec.turns > kStairsMaximumTurns)) {
      reject("Spiral turns must be greater than zero and at most eight.");
    }
  } else {
    require_dimension(spec.footprint_depth,
                      "Footprint depth must be finite and positive.");
    if (spec.layout == StairsLayout::Straight || spec.layout == StairsLayout::L) {
      require_dimension(spec.footprint_width,
                        "Footprint width must be finite and positive.");
    }
  }
  if (spec.layout == StairsLayout::L || spec.layout == StairsLayout::U ||
      spec.layout == StairsLayout::Switchback) {
    require_dimension(spec.landing_depth,
                      "Landing depth must be finite and positive.");
  }
  if (spec.layout == StairsLayout::U ||
      spec.layout == StairsLayout::Switchback) {
    require_dimension(spec.gap, "The gap between flights must be positive.");
  }
  if (spec.layout == StairsLayout::Switchback &&
      (spec.flight_count < kStairsMinimumFlightCount ||
       spec.flight_count > kStairsMaximumFlightCount)) {
    reject("Switchbacks require between two and sixteen flights.");
  }
}

[[nodiscard]] std::pair<PlanPoint, PlanPoint>
flight_section(const Flight &flight, double fraction) {
  const PlanPoint center = interpolate(flight.start, flight.end, fraction);
  const double half_width = flight.width * 0.5;
  const double left_x = -(flight.end.z - flight.start.z) / flight.length;
  const double left_z = (flight.end.x - flight.start.x) / flight.length;
  return {{center.x + left_x * half_width, center.z + left_z * half_width},
          {center.x - left_x * half_width, center.z - left_z * half_width}};
}

[[nodiscard]] Route make_route(const StairsSpec &spec) {
  Route route;
  route.width = spec.footprint_width;
  route.depth = spec.footprint_depth;
  const auto add_flight = [&](PlanPoint start, PlanPoint end, double width) {
    const double length = distance(start, end);
    require_dimension(length,
                      "The footprint must leave positive length for every flight.");
    route.flights.push_back({.start = start, .end = end,
                             .width = width, .length = length});
  };
  if (circular(spec)) {
    route.width = 2.0 * (spec.inner_radius + spec.flight_width);
    route.depth = route.width;
    route.sweep = spec.layout == StairsLayout::Arc
                      ? spec.sweep_degrees * std::numbers::pi / 180.0
                      : spec.turns * kFullTurn;
    if (spec.construction == StairsConstruction::Solid &&
        route.sweep >= kFullTurn) {
      reject("Solid circular stairs cannot complete a turn; use Slab.");
    }
    const double length =
        (spec.inner_radius + spec.flight_width * 0.5) * route.sweep;
    require_dimension(length, "The circular walking line is too short.");
    route.flights.push_back({.width = spec.flight_width, .length = length});
    return route;
  }
  if (spec.layout == StairsLayout::Straight) {
    add_flight({0.0, -route.depth * 0.5}, {0.0, route.depth * 0.5},
               route.width);
    return route;
  }
  const double lane = spec.flight_width;
  const double landing = spec.landing_depth;
  if (spec.layout == StairsLayout::L) {
    if (landing < lane || route.width <= landing || route.depth <= landing) {
      reject("An L layout needs a landing at least as wide as its flight and room for both runs.");
    }
    const double left = -route.width * 0.5;
    const double back = route.depth * 0.5;
    add_flight({left + lane * 0.5, -route.depth * 0.5},
               {left + lane * 0.5, back - landing}, lane);
    add_flight({left + landing, back - lane * 0.5},
               {route.width * 0.5, back - lane * 0.5}, lane);
    route.landings.push_back({left, left + landing, back - landing, back});
  } else {
    const std::size_t count = spec.layout == StairsLayout::U
                                  ? 2U : spec.flight_count;
    const bool stacked = spec.layout == StairsLayout::U ||
                         spec.switchback_arrangement ==
                             StairsSwitchbackArrangement::Stacked;
    const std::size_t lanes = stacked ? 2U : count;
    route.width = static_cast<double>(lanes) * lane +
                  static_cast<double>(lanes - 1U) * spec.gap;
    if (route.depth <= (count > 2U ? 2.0 : 1.0) * landing) {
      reject("The footprint is too short for the flights and landings.");
    }
    if (stacked && count > 2U &&
        spec.construction == StairsConstruction::Solid) {
      reject("Stacked switchbacks require Slab construction.");
    }
    const double front = -route.depth * 0.5;
    const double back = route.depth * 0.5;
    for (std::size_t index = 0U; index < count; ++index) {
      const std::size_t lane_index = stacked ? index % 2U : index;
      const double center = -route.width * 0.5 + lane * 0.5 +
                            static_cast<double>(lane_index) * (lane + spec.gap);
      const bool forward = index % 2U == 0U;
      const double start = index == 0U ? front
                           : forward ? front + landing : back - landing;
      const double end = index + 1U == count ? (forward ? back : front)
                         : forward ? back - landing : front + landing;
      add_flight({center, start}, {center, end}, lane);
      if (index + 1U != count) {
        const std::size_t next_lane = stacked ? (index + 1U) % 2U : index + 1U;
        const double next_center = -route.width * 0.5 + lane * 0.5 +
            static_cast<double>(next_lane) * (lane + spec.gap);
        route.landings.push_back({
            std::min(center, next_center) - lane * 0.5,
            std::max(center, next_center) + lane * 0.5,
            forward ? back - landing : front,
            forward ? back : front + landing});
      }
    }
  }
  if (!spec.clockwise) {
    for (Flight &flight : route.flights) {
      flight.start.x = -flight.start.x;
      flight.end.x = -flight.end.x;
    }
    for (Landing &landing_box : route.landings) {
      const double previous_minimum = landing_box.min_x;
      landing_box.min_x = -landing_box.max_x;
      landing_box.max_x = -previous_minimum;
    }
  }
  return route;
}

void solve_elevations(const StairsSpec &spec, Route &route, StairsMesh &result) {
  const std::size_t count = route.flights.size();
  const double total_length = std::accumulate(
      route.flights.begin(), route.flights.end(), 0.0,
      [](double total, const Flight &flight) { return total + flight.length; });
  if (spec.surface == StairsSurface::Steps) {
    const double ratio = spec.automatic_step_count
        ? spec.height / spec.preferred_riser : static_cast<double>(spec.step_count);
    const double nearest = std::round(ratio);
    const double rounded_count = std::abs(ratio - nearest) <=
        std::numeric_limits<double>::epsilon() * std::max(1.0, ratio) * 8.0
            ? nearest : std::ceil(ratio);
    const double requested = spec.automatic_step_count
        ? std::max(static_cast<double>(count),
                   rounded_count)
        : static_cast<double>(spec.step_count);
    if (requested < static_cast<double>(count) ||
        requested > static_cast<double>(kStairsMaximumStepCount)) {
      reject("Step count must cover every flight and cannot exceed 1024.");
    }
    result.effective_step_count = static_cast<std::uint32_t>(requested);
    result.riser_height = spec.height / requested;
    auto remaining = result.effective_step_count;
    double remaining_length = total_length;
    std::vector<bool> fixed(count, false);
    bool constrained = true;
    while (constrained) {
      constrained = false;
      for (std::size_t index = 0U; index < count; ++index) {
        if (!fixed[index] && static_cast<double>(remaining) *
                route.flights[index].length / remaining_length < 1.0) {
          fixed[index] = true;
          route.flights[index].steps = 1U;
          --remaining;
          remaining_length -= route.flights[index].length;
          constrained = true;
        }
      }
    }
    std::vector<std::pair<double, std::size_t>> remainders;
    std::uint32_t assigned = 0U;
    for (std::size_t index = 0U; index < count; ++index) {
      if (fixed[index]) {
        continue;
      }
      const double share = static_cast<double>(remaining) *
                           route.flights[index].length / remaining_length;
      const auto whole = static_cast<std::uint32_t>(std::floor(share));
      route.flights[index].steps = whole;
      assigned += whole;
      remainders.emplace_back(share - whole, index);
    }
    std::stable_sort(remainders.begin(), remainders.end(),
                     [](const auto &left, const auto &right) {
                       return left.first > right.first;
                     });
    for (std::uint32_t index = assigned; index < remaining; ++index) {
      ++route.flights[remainders[index - assigned].second].steps;
    }
  }
  std::uint32_t preceding_steps = 0U;
  double preceding_length = 0.0;
  for (std::size_t index = 0U; index < count; ++index) {
    Flight &flight = route.flights[index];
    flight.start_height = spec.surface == StairsSurface::Steps
        ? spec.height * preceding_steps / result.effective_step_count
        : spec.height * preceding_length / total_length;
    preceding_steps += flight.steps;
    preceding_length += flight.length;
    flight.end_height = index + 1U == count ? spec.height
        : spec.surface == StairsSurface::Steps
            ? spec.height * preceding_steps / result.effective_step_count
            : spec.height * preceding_length / total_length;
    const double tread = flight.steps == 0U ? 0.0
                         : flight.length / static_cast<double>(flight.steps);
    result.flight_step_counts.push_back(flight.steps);
    result.flight_tread_depths.push_back(tread);
    result.tread_depth = index == 0U ? tread
                                   : std::min(result.tread_depth, tread);
    result.slope_degrees = std::max(result.slope_degrees,
        std::atan2(flight.end_height - flight.start_height, flight.length) *
            180.0 / std::numbers::pi);
  }
}

/** One authored cross-section, with a center guide on both horizontal sheets. */
struct FlightRing final {
  std::array<VertexId, 3> upper;
  std::array<VertexId, 3> lower;
};

/** Authors walking and underside rails with ordinary closed wall panels. */
struct SurfaceBuilder final {
  AuthoredTopology topology;
  std::map<std::array<float, 3>, VertexId> vertices;
  std::vector<VolumeCell> cells;

  [[nodiscard]] VertexId vertex(WidePoint point) {
    const std::array<float, 3> key = {static_cast<float>(point.x),
        static_cast<float>(point.y), static_cast<float>(point.z)};
    if (!std::isfinite(key[0]) || !std::isfinite(key[1]) ||
        !std::isfinite(key[2])) {
      reject("The stairs dimensions cannot be represented by the mesh.");
    }
    const auto found = vertices.find(key);
    if (found != vertices.end()) {
      return found->second;
    }
    if (vertices.size() >= kMaximumVertices) {
      reject("The stairs mesh exceeds the vertex budget.");
    }
    const VertexId id{topology.next_vertex_id++, 1U};
    topology.vertices.push_back({id, {key[0], key[1], key[2]}});
    vertices.emplace(key, id);
    return id;
  }

  void face(std::vector<VertexId> ids, std::size_t cell,
            bool terminal_triangle = false, std::vector<Vec2> uvs = {}) {
    const std::size_t requested_size = ids.size();
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.size() > 1U && ids.front() == ids.back()) {
      ids.pop_back();
    }
    std::vector<VertexId> unique = ids;
    std::sort(unique.begin(), unique.end(), [](VertexId a, VertexId b) {
      return a.index < b.index;
    });
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    if (ids.size() < 3U || unique.size() != ids.size() ||
        (ids.size() != requested_size && !terminal_triangle)) {
      reject("The stairs dimensions collapse a generated face.");
    }
    if (topology.faces.size() >= kMaximumFaces) {
      reject("The stairs mesh exceeds the face budget.");
    }
    const FaceId id{topology.next_face_id++, 1U};
    topology.faces.push_back({.id = id, .vertices = std::move(ids), .uvs = std::move(uvs),
                              .shading = AuthoredSurfaceShading::Flat});
    cells.at(cell).faces.push_back(id);
  }

  [[nodiscard]] std::size_t cell(std::size_t section, std::size_t station) {
    cells.push_back({.section = section, .station = station});
    return cells.size() - 1U;
  }

  [[nodiscard]] FlightRing ring(const std::array<PlanPoint, 3> &upper_points,
                                 double top,
                                 const std::array<PlanPoint, 3> &lower_points,
                                 double bottom) {
    FlightRing result;
    for (std::size_t rail = 0U; rail < 3U; ++rail) {
      result.upper[rail] = vertex(elevated(upper_points[rail], top));
      result.lower[rail] = vertex(elevated(lower_points[rail], bottom));
    }
    return result;
  }

  void connect(const FlightRing &first, const FlightRing &second,
               std::size_t cell_index, bool terminal_tip = false) {
    for (std::size_t lane = 0U; lane < 2U; ++lane) {
      face({first.upper[lane], second.upper[lane], second.upper[lane + 1U],
            first.upper[lane + 1U]}, cell_index);
      if (!terminal_tip) {
        face({first.lower[lane], first.lower[lane + 1U], second.lower[lane + 1U],
              second.lower[lane]}, cell_index);
      }
    }
    if (terminal_tip) {
      // At a solid ramp tip the top and bottom centers coincide. Removing the
      // first underside rail prevents its loop from continuing onto the top.
      face({first.lower[0], first.lower[1], first.lower[2], second.lower[2],
            second.lower[1], second.lower[0]}, cell_index);
    }
  }

  [[nodiscard]] const Vec3 &position(VertexId id) const {
    return topology.vertices.at(id.index - 1U).position;
  }

  /** Retain each wall outline, splitting only when canonical validation requires it. */
  [[nodiscard]] std::vector<std::vector<VertexId>> wall_parts(
      std::vector<VertexId> ids) const {
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.size() > 1U && ids.front() == ids.back()) {
      ids.pop_back();
    }
    if (ids.size() < 3U) {
      return {};
    }
    quader_poly::Document probe;
    quader_poly::Face face;
    face.id = 1U;
    for (std::size_t index = 0U; index < ids.size(); ++index) {
      const auto id = static_cast<quader_poly::ElementId>(index + 1U);
      const Vec3 &point = position(ids[index]);
      probe.vertices.push_back({id, {point.x, point.y, point.z}});
      face.vertices.push_back(id);
    }
    probe.next_vertex_id = static_cast<quader_poly::ElementId>(ids.size() + 1U);
    probe.next_face_id = 2U;
    probe.faces.push_back(face);
    const quader_poly::document_internal::VertexLookupIndex lookup(probe);
    const auto normal = quader_poly::document_internal::face_normal_from_vertex_lookup(
        face, lookup);
    const auto canonical = quader_poly::document_internal::triangulate_face_local_indices(
        face, lookup, normal);
    const bool valid = canonical.size() == ids.size() - 2U &&
        quader_poly::validate_polygon_document(probe).ok();
    if (valid) {
      return {std::move(ids)};
    }
    for (std::size_t anchor = 0U; anchor < ids.size(); ++anchor) {
      std::vector<std::vector<VertexId>> parts;
      probe.faces.clear();
      bool oriented = true;
      for (std::size_t step = 1U; step + 1U < ids.size(); ++step) {
        const std::array corners = {anchor, (anchor + step) % ids.size(),
                                   (anchor + step + 1U) % ids.size()};
        const auto point = [&](std::size_t corner) -> quader_geometry::QVec3d {
          const Vec3 &value = position(ids[corner]);
          return {value.x, value.y, value.z};
        };
        const auto area = quader_geometry::triangle_area_vector(
            quader_geometry::QTriangle3<double>{point(corners[0]), point(corners[1]),
                                               point(corners[2])});
        if (!(quader_geometry::dot(area,
                  quader_geometry::QVec3d{normal.x, normal.y, normal.z}) > 0.0)) {
          oriented = false;
          break;
        }
        parts.push_back({ids[corners[0]], ids[corners[1]], ids[corners[2]]});
        quader_poly::Face triangle;
        triangle.id = static_cast<quader_poly::ElementId>(parts.size());
        for (const std::size_t corner : corners) {
          triangle.vertices.push_back(static_cast<quader_poly::ElementId>(corner + 1U));
        }
        probe.faces.push_back(std::move(triangle));
      }
      probe.next_face_id = static_cast<quader_poly::ElementId>(probe.faces.size() + 1U);
      if (oriented && quader_poly::validate_polygon_document(probe).ok()) {
        return parts;
      }
    }
    return {};
  }

  /** Affine wall UVs do not depend on the canonical diagonal of a planar patch. */
  void wall_face(std::vector<VertexId> ids, std::size_t cell_index,
                 PlanPoint start, PlanPoint end, double run_start) {
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.size() > 1U && ids.front() == ids.back()) {
      ids.pop_back();
    }
    const double length = distance(start, end);
    std::vector<Vec2> uvs;
    uvs.reserve(ids.size());
    for (const VertexId id : ids) {
      const Vec3 &point = position(id);
      double run = ((static_cast<double>(point.x) - start.x) * (end.x - start.x) +
                    (static_cast<double>(point.z) - start.z) * (end.z - start.z)) / length;
      if (point.x == start.x && point.z == start.z) {
        run = 0.0;
      } else if (point.x == end.x && point.z == end.z) {
        run = length;
      }
      uvs.push_back({static_cast<float>((run_start + run) * 0.5), point.y * 0.5F});
    }
    face(std::move(ids), cell_index, false, std::move(uvs));
  }

  /** Use the existing sheet boundary once per chord, without interior wall vertices. */
  void wall(const FlightRing &first, const FlightRing &risen,
            const FlightRing &last, bool has_riser, std::size_t cell_index,
            std::array<double, 2> &runs) {
    for (std::size_t side = 0U; side < 2U; ++side) {
      const std::size_t rail = side * 2U;
      const Vec3 &first_top = position(risen.upper[rail]);
      const Vec3 &last_top = position(last.upper[rail]);
      const PlanPoint start = {first_top.x, first_top.z};
      const PlanPoint end = {last_top.x, last_top.z};
      const double length = distance(start, end);
      if (!(length > 0.0)) {
        reject("The stairs footprint collapses a generated surface.");
      }
      const double run_start = runs[side];
      runs[side] += length;
      std::vector<VertexId> corners = {first.lower[rail]};
      if (has_riser) {
        corners.push_back(risen.lower[rail]);
      }
      corners.insert(corners.end(), {last.lower[rail], last.upper[rail],
                                     risen.upper[rail], first.upper[rail]});
      if (side != 0U) {
        std::reverse(corners.begin(), corners.end());
      }
      auto parts = wall_parts(std::move(corners));
      if (parts.empty()) {
        reject("The stairs wall cannot retain its canonical surface.");
      }
      for (auto &part : parts) {
        wall_face(std::move(part), cell_index, start, end, run_start);
      }
    }
  }

  void cap(const FlightRing &ring, bool end, std::size_t cell_index) {
    std::vector<VertexId> perimeter = {ring.upper[0], ring.upper[1], ring.upper[2],
                                       ring.lower[2], ring.lower[1], ring.lower[0]};
    if (end) {
      std::reverse(perimeter.begin(), perimeter.end());
    }
    face(std::move(perimeter), cell_index);
  }
};

[[nodiscard]] double lower_height(const StairsSpec &spec, double grade) {
  return spec.construction == StairsConstruction::Solid
             ? 0.0 : grade - spec.slab_thickness;
}

[[nodiscard]] std::array<PlanPoint, 3>
section_rails(std::pair<PlanPoint, PlanPoint> section) {
  return {section.first, interpolate(section.first, section.second, 0.5),
          section.second};
}

void build_flight(const StairsSpec &spec, const Route &route,
                  std::size_t index, SurfaceBuilder &builder) {
  const Flight &flight = route.flights[index];
  const bool round = circular(spec);
  const std::uint32_t target_panels = round
      ? std::max(1U, static_cast<std::uint32_t>(
          std::ceil(route.sweep / kFullTurn * spec.curved_segments))) : 1U;
  const std::uint32_t panels_per_tread = flight.steps == 0U ? 1U
      : std::max(1U, (target_panels + flight.steps - 1U) / flight.steps);
  const std::uint32_t panels = flight.steps == 0U
      ? target_panels : flight.steps * panels_per_tread;
  const auto section = [&](double fraction) {
    if (!round) {
      return section_rails(flight_section(flight, fraction));
    }
    const double angle = -std::numbers::pi * 0.5 +
        (spec.clockwise ? 1.0 : -1.0) * route.sweep * fraction;
    double cosine = std::cos(angle);
    double sine = std::sin(angle);
    const double quadrant = angle / (std::numbers::pi * 0.5);
    const double nearest_quadrant = std::round(quadrant);
    if (std::abs(quadrant - nearest_quadrant) <=
        std::numeric_limits<double>::epsilon() *
            std::max(1.0, std::abs(quadrant)) * 8.0) {
      constexpr std::array<double, 4> kCosines = {1.0, 0.0, -1.0, 0.0};
      constexpr std::array<double, 4> kSines = {0.0, 1.0, 0.0, -1.0};
      const auto cardinal = static_cast<std::size_t>(
          (static_cast<int>(nearest_quadrant) % 4 + 4) % 4);
      cosine = kCosines[cardinal];
      sine = kSines[cardinal];
    }
    const auto at_radius = [&](double radius) -> PlanPoint {
      return {radius * cosine, radius * sine};
    };
    const PlanPoint inner = at_radius(spec.inner_radius);
    const PlanPoint outer = at_radius(spec.inner_radius + spec.flight_width);
    return section_rails(spec.clockwise ? std::pair{inner, outer}
                                        : std::pair{outer, inner});
  };
  const auto grade = [&](double fraction) {
    return fraction == 1.0 ? flight.end_height
        : flight.start_height +
          (flight.end_height - flight.start_height) * fraction;
  };
  const auto tread_height = [&](std::uint32_t step) {
    return step == flight.steps ? flight.end_height
        : flight.start_height +
          (flight.end_height - flight.start_height) * step / flight.steps;
  };
  const bool tip = index == 0U && flight.steps == 0U &&
                    spec.construction == StairsConstruction::Solid;
  const auto initial = section(0.0);
  FlightRing previous = builder.ring(initial,
      index == 0U && flight.steps != 0U ? tread_height(1U) : flight.start_height,
      initial, lower_height(spec, flight.start_height));
  std::array<double, 2> wall_runs{};
  for (std::uint32_t station = 0U; station < panels; ++station) {
    const double start = static_cast<double>(station) / panels;
    const double end = static_cast<double>(station + 1U) / panels;
    const auto start_points = section(start);
    const auto end_points = section(end);
    const std::size_t cell = builder.cell(index * 2U, station);
    if (index == 0U && station == 0U && !tip) {
      builder.cap(previous, false, cell);
    }
    const double top = flight.steps == 0U ? grade(end)
        : tread_height(station / panels_per_tread + 1U);
    const FlightRing first = previous;
    FlightRing risen = previous;
    const bool has_riser = flight.steps != 0U && station % panels_per_tread == 0U &&
        (station != 0U || index != 0U);
    if (has_riser) {
      std::array<PlanPoint, 3> middle;
      for (std::size_t rail = 0U; rail < 3U; ++rail) {
        middle[rail] = interpolate(start_points[rail], end_points[rail], 0.5);
      }
      // Retain the underside's existing chord subdivision and rendered surface;
      // the side wall below is authored once from this panel's complete outline.
      risen = builder.ring(start_points, top, middle,
          lower_height(spec, grade((start + end) * 0.5)));
      builder.connect(previous, risen, cell);
      previous = risen;
    }
    const FlightRing next = builder.ring(end_points, top, end_points,
        lower_height(spec, grade(end)));
    builder.connect(previous, next, cell, tip && station == 0U);
    builder.wall(first, risen, next, has_riser, cell, wall_runs);
    previous = next;
  }
  if (index + 1U == route.flights.size()) {
    builder.cap(previous, true, builder.cells.size() - 1U);
  }
}

[[nodiscard]] PlanPoint stored_point(PlanPoint point) {
  return {static_cast<float>(point.x), static_cast<float>(point.z)};
}

[[nodiscard]] bool same_point(PlanPoint a, PlanPoint b) {
  return a.x == b.x && a.z == b.z;
}

void build_landing(const StairsSpec &spec, const Route &route,
                   std::size_t index, SurfaceBuilder &builder) {
  const Landing &box = route.landings[index];
  const PlanPoint minimum = stored_point({box.min_x, box.min_z});
  const PlanPoint maximum = stored_point({box.max_x, box.max_z});
  const double width = maximum.x - minimum.x;
  const double depth = maximum.z - minimum.z;
  if (!(width > 0.0 && depth > 0.0)) {
    reject("The landing dimensions cannot be represented by the mesh.");
  }
  const std::array<PlanPoint, 4> corners = {{
      minimum, {minimum.x, maximum.z}, maximum, {maximum.x, minimum.z}}};
  const auto incoming = section_rails(flight_section(route.flights[index], 1.0));
  const auto outgoing = section_rails(flight_section(route.flights[index + 1U], 0.0));
  std::array<std::array<PlanPoint, 3>, 2> ports = {incoming, outgoing};
  std::vector<PlanPoint> base(corners.begin(), corners.end());
  for (auto &port : ports) {
    for (PlanPoint &point : port) {
      point = stored_point(point);
      base.push_back(point);
    }
  }
  const PlanPoint guide_start = spec.layout == StairsLayout::L
      ? (spec.clockwise ? corners[0] : corners[3])
      : stored_point({box.min_x, (box.min_z + box.max_z) * 0.5});
  const PlanPoint guide_end = spec.layout == StairsLayout::L
      ? (spec.clockwise ? corners[2] : corners[1])
      : stored_point({box.max_x, (box.min_z + box.max_z) * 0.5});
  base.push_back(guide_start);
  base.push_back(guide_end);
  const auto position = [&](PlanPoint point) {
    if (point.x == minimum.x) {
      return point.z - minimum.z;
    }
    if (point.z == maximum.z) {
      return depth + point.x - minimum.x;
    }
    if (point.x == maximum.x) {
      return depth + width + maximum.z - point.z;
    }
    if (point.z == minimum.z) {
      return 2.0 * depth + width + maximum.x - point.x;
    }
    reject("The flight and landing boundaries cannot share exact mesh vertices.");
  };
  std::sort(base.begin(), base.end(), [&](PlanPoint a, PlanPoint b) {
    return position(a) < position(b);
  });
  base.erase(std::unique(base.begin(), base.end(), same_point), base.end());
  const auto on_port = [&](PlanPoint a, PlanPoint b) {
    for (const auto &port : ports) {
      const auto within = [&](PlanPoint point) {
        return point.x >= std::min(port[0].x, port[2].x) &&
               point.x <= std::max(port[0].x, port[2].x) &&
               point.z >= std::min(port[0].z, port[2].z) &&
               point.z <= std::max(port[0].z, port[2].z);
      };
      if (within(a) && within(b)) {
        return true;
      }
    }
    return false;
  };
  std::vector<PlanPoint> perimeter;
  std::vector<std::array<PlanPoint, 3>> walls;
  for (std::size_t corner = 0U; corner < base.size(); ++corner) {
    const PlanPoint a = base[corner];
    const PlanPoint b = base[(corner + 1U) % base.size()];
    perimeter.push_back(a);
    if (!on_port(a, b)) {
      const PlanPoint middle = stored_point(interpolate(a, b, 0.5));
      if (same_point(a, middle) || same_point(b, middle)) {
        reject("The landing wall is too small to retain its guide boundary.");
      }
      perimeter.push_back(middle);
      walls.push_back({a, middle, b});
    }
  }
  const auto guide_index = [&](PlanPoint point) {
    const auto found = std::find_if(perimeter.begin(), perimeter.end(),
        [&](PlanPoint candidate) { return same_point(candidate, point); });
    return static_cast<std::size_t>(found - perimeter.begin());
  };
  const std::size_t first = guide_index(guide_start);
  const std::size_t second = guide_index(guide_end);
  const double height = route.flights[index].end_height;
  const double bottom = lower_height(spec, height);
  const std::size_t cell = builder.cell(index * 2U + 1U, 0U);
  const auto sheet = [&](std::size_t start, std::size_t end) {
    std::vector<VertexId> top;
    std::vector<VertexId> lower;
    for (std::size_t corner = start;; corner = (corner + 1U) % perimeter.size()) {
      top.push_back(builder.vertex(elevated(perimeter[corner], height)));
      lower.push_back(builder.vertex(elevated(perimeter[corner], bottom)));
      if (corner == end) {
        break;
      }
    }
    if (top.size() <= 4U) {
      reject("The landing cannot retain its independent planar guide.");
    }
    builder.face(std::move(top), cell);
    std::reverse(lower.begin(), lower.end());
    builder.face(std::move(lower), cell);
  };
  sheet(first, second);
  sheet(second, first);
  for (const auto &wall : walls) {
    builder.face({builder.vertex(elevated(wall[2], height)),
                  builder.vertex(elevated(wall[1], height)),
                  builder.vertex(elevated(wall[0], height)),
                  builder.vertex(elevated(wall[0], bottom)),
                  builder.vertex(elevated(wall[1], bottom)),
                  builder.vertex(elevated(wall[2], bottom))}, cell);
  }
}

[[nodiscard]] std::vector<PlanPoint>
intersection(std::span<const PlanPoint> a, std::span<const PlanPoint> b,
             double epsilon) {
  std::vector<PlanPoint> polygon(a.begin(), a.end());
  const double sign = cross(b[0], b[1], b[2]) < 0.0 ? -1.0 : 1.0;
  for (std::size_t edge = 0U; edge < b.size() && !polygon.empty(); ++edge) {
    const PlanPoint start = b[edge];
    const PlanPoint end = b[(edge + 1U) % b.size()];
    const double tolerance = epsilon * distance(start, end);
    std::vector<PlanPoint> clipped;
    PlanPoint previous = polygon.back();
    double before = sign * cross(start, end, previous);
    for (const PlanPoint point : polygon) {
      const double after = sign * cross(start, end, point);
      const bool previous_inside = before >= -tolerance;
      const bool inside = after >= -tolerance;
      if (inside != previous_inside && before != after) {
        clipped.push_back(interpolate(previous, point,
            std::clamp(before / (before - after), 0.0, 1.0)));
      }
      if (inside) {
        clipped.push_back(point);
      }
      previous = point;
      before = after;
    }
    polygon = std::move(clipped);
  }
  return polygon;
}

[[nodiscard]] double height_at(const SurfaceTriangle &triangle, PlanPoint point) {
  const double area = cross(triangle.points[0], triangle.points[1], triangle.points[2]);
  const std::array weights = {
      std::clamp(cross(point, triangle.points[1], triangle.points[2]) / area, 0.0, 1.0),
      std::clamp(cross(triangle.points[0], point, triangle.points[2]) / area, 0.0, 1.0),
      std::clamp(cross(triangle.points[0], triangle.points[1], point) / area, 0.0, 1.0)};
  const double total = weights[0] + weights[1] + weights[2];
  if (!(total > 0.0) || !std::isfinite(total)) {
    reject("The stairs dimensions cannot retain a finite clearance evaluation.");
  }
  return (weights[0] * triangle.heights[0] + weights[1] * triangle.heights[1] +
          weights[2] * triangle.heights[2]) / total;
}

[[nodiscard]] std::vector<PlanPoint> projected_hull(std::vector<PlanPoint> points) {
  std::sort(points.begin(), points.end(), [](PlanPoint a, PlanPoint b) {
    return a.x < b.x || (a.x == b.x && a.z < b.z);
  });
  points.erase(std::unique(points.begin(), points.end(), same_point), points.end());
  if (points.size() < 3U) {
    reject("The stairs footprint collapses a generated surface.");
  }
  std::vector<PlanPoint> hull;
  hull.reserve(points.size() * 2U);
  for (const PlanPoint point : points) {
    // No area epsilon: even a narrow float-authored rail sliver must be enclosed.
    while (hull.size() >= 2U &&
           cross(hull[hull.size() - 2U], hull.back(), point) <= 0.0) {
      hull.pop_back();
    }
    hull.push_back(point);
  }
  const std::size_t lower_size = hull.size();
  for (std::size_t index = points.size() - 1U; index-- > 0U;) {
    const PlanPoint point = points[index];
    while (hull.size() > lower_size &&
           cross(hull[hull.size() - 2U], hull.back(), point) <= 0.0) {
      hull.pop_back();
    }
    hull.push_back(point);
  }
  hull.pop_back();
  if (hull.size() < 3U) {
    reject("The stairs footprint collapses a generated surface.");
  }
  return hull;
}

[[nodiscard]] double projected_area(std::span<const PlanPoint> polygon) {
  double area = 0.0;
  for (std::size_t index = 1U; index + 1U < polygon.size(); ++index) {
    area += std::abs(cross(polygon[0], polygon[index], polygon[index + 1U]));
  }
  return area;
}

/** Uses the same float vertices and canonical diagonals as rendering and Peel. */
void collect_cell_surfaces(const PolygonDocument &document,
                           std::vector<VolumeCell> &cells,
                           double width, double depth) {
  const auto &native = PolygonDocumentNativeAccess::document(document);
  const quader_poly::document_internal::VertexLookupIndex lookup(native);
  std::map<quader_poly::ElementId, const quader_poly::Face *> faces;
  for (const auto &face : native.faces) {
    faces.emplace(face.id, &face);
  }
  const auto triangles = [&](VolumeCell &cell) {
    for (const FaceId id : cell.faces) {
      const auto &face = *faces.at(id.index);
      const auto normal = quader_poly::document_internal::face_normal_from_vertex_lookup(
          face, lookup);
      const auto indices = quader_poly::document_internal::triangulate_face_local_indices(
          face, lookup, normal);
      if (indices.size() != face.vertices.size() - 2U) {
        reject("A stairs surface cannot retain its canonical triangulation.");
      }
      for (const auto &triangle : indices) {
        SurfaceTriangle surface;
        std::array<quader_geometry::QVec3d, 3> positions;
        const std::array corners = {triangle.a, triangle.b, triangle.c};
        for (std::size_t corner = 0U; corner < corners.size(); ++corner) {
          const auto &position = lookup.find(face.vertices[corners[corner]])->position;
          positions[corner] = {position.x, position.y, position.z};
          surface.points[corner] = {static_cast<double>(position.x) / width,
                                    static_cast<double>(position.z) / depth};
          surface.heights[corner] = position.y;
        }
        const auto area_vector = quader_geometry::triangle_area_vector(
            quader_geometry::QTriangle3<double>{positions[0], positions[1], positions[2]});
        // The renderer corrects canonical ngon triangles to the face's outward
        // normal; clearance must classify that same final triangle winding.
        if (quader_geometry::dot(area_vector,
                quader_geometry::QVec3d{normal.x, normal.y, normal.z}) < 0.0) {
          std::swap(surface.points[1], surface.points[2]);
          std::swap(surface.heights[1], surface.heights[2]);
        }
        const double area = cross(surface.points[0], surface.points[1], surface.points[2]);
        if (area == 0.0) {
          continue;
        }
        // Float-rounded side and cap triangles can form narrow envelope slivers.
        // Use their actual winding, not the face's nominal authored role.
        (area < 0.0 ? cell.upper : cell.lower).push_back(surface);
      }
    }
  };
  for (VolumeCell &cell : cells) {
    triangles(cell);
    if (cell.upper.empty() || cell.lower.empty()) {
      reject("The stairs footprint collapses a generated surface.");
    }
    std::vector<PlanPoint> projected_points;
    for (const auto &surfaces : {std::span<const SurfaceTriangle>(cell.upper),
                                  std::span<const SurfaceTriangle>(cell.lower)}) {
      for (const SurfaceTriangle &triangle : surfaces) {
        projected_points.insert(projected_points.end(), triangle.points.begin(),
                                 triangle.points.end());
      }
    }
    cell.footprint = projected_hull(std::move(projected_points));
    cell.min_x = cell.max_x = cell.footprint[0].x;
    cell.min_z = cell.max_z = cell.footprint[0].z;
    for (const PlanPoint point : cell.footprint) {
      cell.min_x = std::min(cell.min_x, point.x);
      cell.max_x = std::max(cell.max_x, point.x);
      cell.min_z = std::min(cell.min_z, point.z);
      cell.max_z = std::max(cell.max_z, point.z);
    }
    cell.minimum_lower = std::numeric_limits<double>::max();
    cell.maximum_upper = std::numeric_limits<double>::lowest();
    for (const SurfaceTriangle &triangle : cell.lower) {
      for (const double height : triangle.heights) {
        cell.minimum_lower = std::min(cell.minimum_lower, height);
      }
    }
    for (const SurfaceTriangle &triangle : cell.upper) {
      for (const double height : triangle.heights) {
        cell.maximum_upper = std::max(cell.maximum_upper, height);
      }
    }
  }
}

void reject_collisions(const PolygonDocument &document, SurfaceBuilder &builder,
                        double width, double depth, double height) {
  collect_cell_surfaces(document, builder.cells, width, depth);
  const auto &cells = builder.cells;
  constexpr double kPlanEpsilon = 1.0e-10;
  const double height_epsilon = std::max(1.0, height) *
      std::numeric_limits<double>::epsilon() * 16.0;
  double min_x = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double min_z = min_x;
  double max_z = max_x;
  double widths = 0.0;
  double depths = 0.0;
  for (const VolumeCell &cell : cells) {
    min_x = std::min(min_x, cell.min_x);
    max_x = std::max(max_x, cell.max_x);
    min_z = std::min(min_z, cell.min_z);
    max_z = std::max(max_z, cell.max_z);
    widths += cell.max_x - cell.min_x;
    depths += cell.max_z - cell.min_z;
  }
  // Keep the broad phase on route panels, not all detailed mesh triangles.
  const bool sweep_x = (max_x - min_x) / std::max(widths, kPlanEpsilon) >
                       (max_z - min_z) / std::max(depths, kPlanEpsilon);
  const auto minimum = [&](const VolumeCell &cell) {
    return sweep_x ? cell.min_x : cell.min_z;
  };
  const auto maximum = [&](const VolumeCell &cell) {
    return sweep_x ? cell.max_x : cell.max_z;
  };
  const auto above = [&](const VolumeCell &upper, const VolumeCell &lower,
                         bool adjacent, double contact_area) {
    for (const SurfaceTriangle &bottom : upper.lower) {
      for (const SurfaceTriangle &top : lower.upper) {
        // A padded clipping predicate can extrapolate far beyond a near-vertical
        // side triangle. Only the actual projected triangles define clearance.
        const auto overlap = intersection(bottom.points, top.points, 0.0);
        if (adjacent && projected_area(overlap) <= contact_area) {
          continue;
        }
        for (const PlanPoint point : overlap) {
          if (!(height_at(bottom, point) > height_at(top, point) + height_epsilon)) {
            return false;
          }
        }
      }
    }
    return true;
  };
  std::vector<std::size_t> order(cells.size());
  std::iota(order.begin(), order.end(), 0U);
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return minimum(cells[a]) < minimum(cells[b]);
  });
  for (std::size_t first = 0U; first < order.size(); ++first) {
    const VolumeCell &a = cells[order[first]];
    for (std::size_t second = first + 1U; second < order.size(); ++second) {
      const VolumeCell &b = cells[order[second]];
      if (minimum(b) > maximum(a) + kPlanEpsilon) {
        break;
      }
      if (b.min_z > a.max_z + kPlanEpsilon || a.min_z > b.max_z + kPlanEpsilon ||
          b.min_x > a.max_x + kPlanEpsilon || a.min_x > b.max_x + kPlanEpsilon ||
          a.minimum_lower > b.maximum_upper + height_epsilon ||
          b.minimum_lower > a.maximum_upper + height_epsilon) {
        continue;
      }
      const auto overlap = intersection(a.footprint, b.footprint, kPlanEpsilon);
      if (overlap.empty()) {
        continue;
      }
      const auto section_distance = a.section > b.section
          ? a.section - b.section : b.section - a.section;
      const auto station_distance = a.station > b.station
          ? a.station - b.station : b.station - a.station;
      const bool adjacent = section_distance == 1U || section_distance == 2U ||
          (section_distance == 0U && station_distance <= 1U);
      const double contact_area = kPlanEpsilon *
          std::max({a.max_x - a.min_x, a.max_z - a.min_z,
                    b.max_x - b.min_x, b.max_z - b.min_z, kPlanEpsilon}) * 8.0;
      if (projected_area(overlap) <= contact_area && adjacent) {
        continue;
      }
      if (!above(a, b, adjacent, contact_area) &&
          !above(b, a, adjacent, contact_area)) {
        reject("The stairs body overlaps or touches a nonadjacent section; increase clearance or change the layout.");
      }
    }
  }
}

void verify_closed_boundary(const AuthoredTopology &topology) {
  std::map<std::pair<std::uint32_t, std::uint32_t>, std::pair<int, int>> edges;
  for (const AuthoredTopologyFace &face : topology.faces) {
    for (std::size_t index = 0U; index < face.vertices.size(); ++index) {
      const auto first = face.vertices[index].index;
      const auto second = face.vertices[(index + 1U) % face.vertices.size()].index;
      auto &entry = edges[std::minmax(first, second)];
      ++entry.first;
      entry.second += first < second ? 1 : -1;
    }
  }
  for (const auto &[key, entry] : edges) {
    (void)key;
    if (entry.first != 2 || entry.second != 0) {
      reject("The stairs dimensions do not produce a closed manifold boundary.");
    }
  }
}

} // namespace

Result<StairsMesh> build_stairs_mesh(const StairsSpec &spec) {
  try {
    validate_spec(spec);
    Route route = make_route(spec);
    StairsMesh result;
    result.footprint_width = route.width;
    result.footprint_depth = route.depth;
    solve_elevations(spec, route, result);
    SurfaceBuilder builder;
    for (std::size_t index = 0U; index < route.flights.size(); ++index) {
      build_flight(spec, route, index, builder);
      if (index < route.landings.size()) {
        build_landing(spec, route, index, builder);
      }
    }
    verify_closed_boundary(builder.topology);
    auto document = PolygonDocument::from_authored_topology(builder.topology);
    if (!document.ok()) {
      return Result<StairsMesh>::failure(document.error());
    }
    const auto validation = document.value().validate();
    if (!validation.ok()) {
      return Result<StairsMesh>::failure(validation.error());
    }
    if (!validation.value().success) {
      return Result<StairsMesh>::failure(make_error(
          ErrorCode::ValidationFailed, "The generated stairs geometry is invalid."));
    }
    reject_collisions(document.value(), builder, route.width, route.depth, spec.height);
    result.document = std::move(document).value();
    return Result<StairsMesh>::success(std::move(result));
  } catch (const std::invalid_argument &error) {
    return Result<StairsMesh>::failure(make_error(
        ErrorCode::InvalidArgument, error.what()));
  } catch (const std::bad_alloc &) {
    return Result<StairsMesh>::failure(make_error(
        ErrorCode::ValidationFailed, "The stairs mesh exceeds available memory."));
  } catch (const std::length_error &) {
    return Result<StairsMesh>::failure(make_error(
        ErrorCode::ValidationFailed, "The stairs mesh exceeds storage limits."));
  }
}

} // namespace quader::modeling
