////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/mesh/stairs.hpp>

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

/** A convex footprint with affine upper and lower surface heights. */
struct VolumePatch final {
  std::array<PlanPoint, 3> points;
  std::array<double, 3> upper;
  std::array<double, 3> lower;
  double min_x = 0.0;
  double max_x = 0.0;
  double min_z = 0.0;
  double max_z = 0.0;
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

/** Joins the authored top sheet, underside, and exposed perimeter only. */
struct SurfaceBuilder final {
  AuthoredTopology topology;
  std::map<std::array<float, 3>, VertexId> vertices;
  std::map<std::uint32_t, VertexId> lower_vertices;
  std::map<std::pair<std::uint32_t, std::uint32_t>,
           std::pair<VertexId, VertexId>> boundary;
  std::vector<VolumePatch> patches;

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

  [[nodiscard]] VertexId upper(WidePoint point, double bottom) {
    const VertexId id = vertex(point);
    const VertexId lower = vertex({point.x, bottom, point.z});
    const auto [iterator, inserted] = lower_vertices.emplace(id.index, lower);
    if (!inserted && iterator->second != lower) {
      reject("Nonadjacent stairs surfaces touch each other.");
    }
    return id;
  }

  void face(std::vector<VertexId> ids, bool top, bool allow_collapse = false) {
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    if (ids.size() > 1U && ids.front() == ids.back()) {
      ids.pop_back();
    }
    if (ids.size() < 3U) {
      if (allow_collapse) {
        return;
      }
      reject("The stairs dimensions collapse a generated face.");
    }
    if (topology.faces.size() >= kMaximumFaces) {
      reject("The stairs mesh exceeds the face budget.");
    }
    if (top) {
      for (std::size_t index = 0U; index < ids.size(); ++index) {
        const VertexId first = ids[index];
        const VertexId second = ids[(index + 1U) % ids.size()];
        const auto key = std::minmax(first.index, second.index);
        const auto found = boundary.find(key);
        if (found == boundary.end()) {
          boundary.emplace(key, std::pair{first, second});
        } else {
          if (found->second.first != second || found->second.second != first) {
            reject("The stairs sections have inconsistent boundary winding.");
          }
          boundary.erase(found);
        }
      }
    }
    topology.faces.push_back({.id = {topology.next_face_id++, 1U},
                              .vertices = std::move(ids),
                              .shading = AuthoredSurfaceShading::Flat});
  }

  void patch(std::array<PlanPoint, 3> points,
             std::array<double, 3> upper_heights,
             std::array<double, 3> lower_heights,
             std::size_t section, std::size_t station) {
    // Check the emitted float surfaces: a positive double clearance can vanish
    // when either surface is narrowed, even without coincident vertices.
    for (std::size_t index = 0U; index < points.size(); ++index) {
      points[index].x = static_cast<float>(points[index].x);
      points[index].z = static_cast<float>(points[index].z);
      upper_heights[index] = static_cast<float>(upper_heights[index]);
      lower_heights[index] = static_cast<float>(lower_heights[index]);
    }
    VolumePatch item{.points = points, .upper = upper_heights,
                     .lower = lower_heights, .section = section,
                     .station = station};
    item.min_x = item.max_x = points[0].x;
    item.min_z = item.max_z = points[0].z;
    for (const PlanPoint point : points) {
      item.min_x = std::min(item.min_x, point.x);
      item.max_x = std::max(item.max_x, point.x);
      item.min_z = std::min(item.min_z, point.z);
      item.max_z = std::max(item.max_z, point.z);
    }
    patches.push_back(item);
  }

  void strip(PlanPoint left_start, PlanPoint right_start,
             PlanPoint left_end, PlanPoint right_end,
             double start_top, double end_top,
             double start_bottom, double end_bottom,
             std::size_t section, std::size_t station) {
    const VertexId first = upper(elevated(left_start, start_top), start_bottom);
    const VertexId second = upper(elevated(left_end, end_top), end_bottom);
    const VertexId third = upper(elevated(right_end, end_top), end_bottom);
    const VertexId fourth = upper(elevated(right_start, start_top), start_bottom);
    // Matching diagonals give the ramp and its underside an explicit surface.
    face({first, second, third}, true);
    face({first, third, fourth}, true);
    const VertexId low_first = lower_vertices.at(first.index);
    const VertexId low_second = lower_vertices.at(second.index);
    const VertexId low_third = lower_vertices.at(third.index);
    const VertexId low_fourth = lower_vertices.at(fourth.index);
    face({low_third, low_second, low_first}, false);
    face({low_fourth, low_third, low_first}, false);
    patch({left_start, left_end, right_end}, {start_top, end_top, end_top},
          {start_bottom, end_bottom, end_bottom}, section, station);
    patch({left_start, right_end, right_start}, {start_top, end_top, start_top},
          {start_bottom, end_bottom, start_bottom}, section, station);
  }

  void riser(PlanPoint left, PlanPoint right, double before, double after,
             double bottom) {
    face({upper(elevated(left, before), bottom),
          upper(elevated(left, after), bottom),
          upper(elevated(right, after), bottom),
          upper(elevated(right, before), bottom)}, true);
  }

  void close_boundary() {
    std::map<std::pair<float, float>, std::vector<VertexId>> vertical_stations;
    const auto position = [&](VertexId id) {
      return topology.vertices.at(id.index - 1U).position;
    };
    for (const auto &[key, edge] : boundary) {
      (void)key;
      for (const VertexId id : {edge.first, edge.second,
                                lower_vertices.at(edge.first.index),
                                lower_vertices.at(edge.second.index)}) {
        const Vec3 point = position(id);
        vertical_stations[{point.x, point.z}].push_back(id);
      }
    }
    for (auto &[key, stations] : vertical_stations) {
      (void)key;
      std::sort(stations.begin(), stations.end(), [&](VertexId a, VertexId b) {
        return position(a).y < position(b).y;
      });
      stations.erase(std::unique(stations.begin(), stations.end()), stations.end());
    }
    for (const auto &[key, edge] : boundary) {
      (void)key;
      const Vec3 first = position(edge.first);
      const Vec3 second = position(edge.second);
      if (first.x == second.x && first.z == second.z) {
        continue;
      }
      std::vector<VertexId> sides{edge.second, edge.first};
      const auto append_vertical = [&](VertexId from, VertexId to) {
        const Vec3 start = position(from);
        const Vec3 end = position(to);
        std::vector<VertexId> interior;
        for (const VertexId id : vertical_stations.at({start.x, start.z})) {
          const double height = position(id).y;
          if (height > std::min(start.y, end.y) &&
              height < std::max(start.y, end.y)) {
            interior.push_back(id);
          }
        }
        if (start.y > end.y) {
          std::reverse(interior.begin(), interior.end());
        }
        sides.insert(sides.end(), interior.begin(), interior.end());
        sides.push_back(to);
      };
      append_vertical(edge.first, lower_vertices.at(edge.first.index));
      sides.push_back(lower_vertices.at(edge.second.index));
      append_vertical(lower_vertices.at(edge.second.index), edge.second);
      face(std::move(sides), false, true);
    }
  }
};

[[nodiscard]] double lower_height(const StairsSpec &spec, double grade) {
  return spec.construction == StairsConstruction::Solid
             ? 0.0 : grade - spec.slab_thickness;
}

void build_flight(const StairsSpec &spec, const Route &route,
                  std::size_t index, SurfaceBuilder &builder) {
  const Flight &flight = route.flights[index];
  const bool round = circular(spec);
  const std::uint32_t panels = round
      ? std::max(1U, static_cast<std::uint32_t>(
          std::ceil(route.sweep / kFullTurn * spec.curved_segments))) : 1U;
  std::vector<double> fractions = {0.0, 1.0};
  for (std::uint32_t panel = 1U; panel < panels; ++panel) {
    fractions.push_back(static_cast<double>(panel) / panels);
  }
  for (std::uint32_t step = 1U; step < flight.steps; ++step) {
    fractions.push_back(static_cast<double>(step) / flight.steps);
  }
  std::sort(fractions.begin(), fractions.end());
  fractions.erase(std::unique(fractions.begin(), fractions.end(),
      [](double first, double second) {
        return std::abs(first - second) < 1.0e-12;
      }), fractions.end());
  const auto section = [&](double fraction) {
    if (!round) {
      return flight_section(flight, fraction);
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
      // Cardinal points are authored exactly; unrelated small coordinates and
      // every riser height retain their supplied precision.
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
    return spec.clockwise ? std::pair{inner, outer} : std::pair{outer, inner};
  };
  const auto grade = [&](double fraction) {
    return fraction == 1.0 ? flight.end_height
        : flight.start_height +
          (flight.end_height - flight.start_height) * fraction;
  };
  double previous_top = flight.start_height;
  for (std::size_t station = 0U; station + 1U < fractions.size(); ++station) {
    const double start = fractions[station];
    const double end = fractions[station + 1U];
    const auto [left_start, right_start] = section(start);
    const auto [left_end, right_end] = section(end);
    double start_top = grade(start);
    double end_top = grade(end);
    if (flight.steps != 0U) {
      const auto step = static_cast<std::uint32_t>(
          std::floor((start + end) * 0.5 * flight.steps)) + 1U;
      start_top = end_top = step == flight.steps ? flight.end_height
          : flight.start_height +
            (flight.end_height - flight.start_height) * step / flight.steps;
      if (start_top > previous_top) {
        builder.riser(left_start, right_start, previous_top, start_top,
                      lower_height(spec, grade(start)));
      }
    }
    builder.strip(left_start, right_start, left_end, right_end,
                   start_top, end_top, lower_height(spec, grade(start)),
                   lower_height(spec, grade(end)), index * 2U, station);
    previous_top = end_top;
  }
}

void build_landing(const StairsSpec &spec, const Route &route,
                   std::size_t index, SurfaceBuilder &builder) {
  const Landing &box = route.landings[index];
  const double width = box.max_x - box.min_x;
  const double depth = box.max_z - box.min_z;
  const auto incoming = flight_section(route.flights[index], 1.0);
  const auto outgoing = flight_section(route.flights[index + 1U], 0.0);
  std::vector<PlanPoint> perimeter = {
      {box.min_x, box.min_z}, {box.min_x, box.max_z},
      {box.max_x, box.max_z}, {box.max_x, box.min_z},
      incoming.first, incoming.second, outgoing.first, outgoing.second};
  const double epsilon = std::max(width, depth) * 1.0e-9;
  const auto position = [&](PlanPoint point) {
    if (std::abs(point.x - box.min_x) <= epsilon) {
      return point.z - box.min_z;
    }
    if (std::abs(point.z - box.max_z) <= epsilon) {
      return depth + point.x - box.min_x;
    }
    if (std::abs(point.x - box.max_x) <= epsilon) {
      return depth + width + box.max_z - point.z;
    }
    return 2.0 * depth + width + box.max_x - point.x;
  };
  std::sort(perimeter.begin(), perimeter.end(), [&](PlanPoint a, PlanPoint b) {
    return position(a) < position(b);
  });
  perimeter.erase(std::unique(perimeter.begin(), perimeter.end(),
      [&](PlanPoint a, PlanPoint b) { return distance(a, b) <= epsilon; }),
      perimeter.end());
  const PlanPoint center{(box.min_x + box.max_x) * 0.5,
                         (box.min_z + box.max_z) * 0.5};
  const double height = route.flights[index].end_height;
  const double bottom = lower_height(spec, height);
  const VertexId center_id = builder.upper(elevated(center, height), bottom);
  for (std::size_t corner = 0U; corner < perimeter.size(); ++corner) {
    const PlanPoint a = perimeter[corner];
    const PlanPoint b = perimeter[(corner + 1U) % perimeter.size()];
    const VertexId first = builder.upper(elevated(a, height), bottom);
    const VertexId second = builder.upper(elevated(b, height), bottom);
    builder.face({center_id, first, second}, true);
    builder.face({builder.lower_vertices.at(second.index),
                  builder.lower_vertices.at(first.index),
                  builder.lower_vertices.at(center_id.index)}, false);
    builder.patch({center, a, b}, {height, height, height},
                   {bottom, bottom, bottom}, index * 2U + 1U, 0U);
  }
}

[[nodiscard]] std::vector<PlanPoint>
intersection(const VolumePatch &a, const VolumePatch &b, double epsilon) {
  std::vector<PlanPoint> polygon(a.points.begin(), a.points.end());
  const double sign = cross(b.points[0], b.points[1], b.points[2]) < 0.0
                          ? -1.0 : 1.0;
  for (std::size_t edge = 0U; edge < 3U && !polygon.empty(); ++edge) {
    const PlanPoint start = b.points[edge];
    const PlanPoint end = b.points[(edge + 1U) % 3U];
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

[[nodiscard]] double height_at(const VolumePatch &patch,
                               const std::array<double, 3> &heights,
                               PlanPoint point) {
  const double area = cross(patch.points[0], patch.points[1], patch.points[2]);
  const double first = cross(point, patch.points[1], patch.points[2]) / area;
  const double second = cross(patch.points[0], point, patch.points[2]) / area;
  return first * heights[0] + second * heights[1] +
         (1.0 - first - second) * heights[2];
}

void reject_collisions(const SurfaceBuilder &builder, double width,
                        double depth, double height) {
  // Normalize axes independently for intersection predicates only. A very
  // wide flight must not erase a small rise or widen its longitudinal contacts.
  std::vector<VolumePatch> patches = builder.patches;
  for (VolumePatch &patch : patches) {
    for (PlanPoint &point : patch.points) {
      point.x /= width;
      point.z /= depth;
    }
    patch.min_x /= width;
    patch.max_x /= width;
    patch.min_z /= depth;
    patch.max_z /= depth;
  }
  constexpr double kPlanEpsilon = 1.0e-10;
  const double epsilon = kPlanEpsilon;
  const double height_epsilon = std::max(1.0, height) *
      std::numeric_limits<double>::epsilon() * 16.0;
  double min_x = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double min_z = min_x;
  double max_z = max_x;
  double widths = 0.0;
  double depths = 0.0;
  for (const VolumePatch &patch : patches) {
    min_x = std::min(min_x, patch.min_x);
    max_x = std::max(max_x, patch.max_x);
    min_z = std::min(min_z, patch.min_z);
    max_z = std::max(max_z, patch.max_z);
    widths += patch.max_x - patch.min_x;
    depths += patch.max_z - patch.min_z;
  }
  // Sweep along the axis that separates more patches. Straight flights must
  // not compare every tread merely because their cross-flight bounds agree.
  const bool sweep_x = (max_x - min_x) / std::max(widths, epsilon) >
                       (max_z - min_z) / std::max(depths, epsilon);
  const auto minimum = [&](const VolumePatch &patch) {
    return sweep_x ? patch.min_x : patch.min_z;
  };
  const auto maximum = [&](const VolumePatch &patch) {
    return sweep_x ? patch.max_x : patch.max_z;
  };
  std::vector<std::size_t> order(patches.size());
  std::iota(order.begin(), order.end(), 0U);
  std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
    return minimum(patches[a]) < minimum(patches[b]);
  });
  for (std::size_t first = 0U; first < order.size(); ++first) {
    const VolumePatch &a = patches[order[first]];
    for (std::size_t second = first + 1U; second < order.size(); ++second) {
      const VolumePatch &b = patches[order[second]];
      if (minimum(b) > maximum(a) + epsilon) {
        break;
      }
      if (b.min_z > a.max_z + epsilon || a.min_z > b.max_z + epsilon ||
          b.min_x > a.max_x + epsilon || a.min_x > b.max_x + epsilon) {
        continue;
      }
      const std::vector<PlanPoint> overlap = intersection(a, b, epsilon);
      if (overlap.empty()) {
        continue;
      }
      double area = 0.0;
      for (std::size_t index = 1U; index + 1U < overlap.size(); ++index) {
        area += std::abs(cross(overlap[0], overlap[index], overlap[index + 1U]));
      }
      const auto section_distance = a.section > b.section
          ? a.section - b.section : b.section - a.section;
      const auto station_distance = a.station > b.station
          ? a.station - b.station : b.station - a.station;
      const bool adjacent = section_distance == 1U || section_distance == 2U ||
          (section_distance == 0U && station_distance <= 1U);
      const double contact_area = epsilon *
          std::max({a.max_x - a.min_x, a.max_z - a.min_z,
                    b.max_x - b.min_x, b.max_z - b.min_z, epsilon}) * 8.0;
      if (area <= contact_area && adjacent) {
        continue;
      }
      bool a_above = true;
      bool b_above = true;
      for (const PlanPoint point : overlap) {
        a_above = a_above &&
            height_at(a, a.lower, point) >
                height_at(b, b.upper, point) + height_epsilon;
        b_above = b_above &&
            height_at(b, b.lower, point) >
                height_at(a, a.upper, point) + height_epsilon;
      }
      if (!a_above && !b_above) {
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
    reject_collisions(builder, route.width, route.depth, spec.height);
    builder.close_boundary();
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
