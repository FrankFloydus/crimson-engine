////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "uv_peel_solver.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader::modeling::detail {
namespace {

constexpr std::size_t kNoColumn = std::numeric_limits<std::size_t>::max();
constexpr std::size_t kMaximumIterations = 4096U;
constexpr std::size_t kMaximumCoefficientVisits = 256000000U;
constexpr std::size_t kMaximumOverlapPairs = 8000000U;
constexpr double kMaximumCondition = 1.0e10;
constexpr double kNormalTolerance = 2.0e-10;
constexpr double kResidualTolerance = 2.0e-11;
constexpr double kRelativeAreaTolerance = 1.0e-12;
constexpr double kGuideStraightnessTolerance = 2.0e-5;

/// Describes a numerical failure without introducing document mutations.
[[nodiscard]] Error peel_error(std::string message) {
  return make_error(ErrorCode::ValidationFailed, std::move(message));
}

/// Checks the bounded private input before allocating numerical work buffers.
[[nodiscard]] std::optional<Error> check_size(const GuidedPeelChart &chart) {
  if (chart.positions.size() < 3U || chart.triangles.empty()) {
    return peel_error("UV Peel needs a nonempty surface chart.");
  }
  if (chart.positions.size() > kUvPeelMaxChartVertices ||
      chart.triangles.size() > kUvPeelMaxChartTriangles) {
    return peel_error(
        "UV Peel supports at most 16,384 chart vertices and 32,768 triangles "
        "per patch. Select a smaller patch.");
  }
  if (chart.quads.size() > chart.triangles.size() / 2U) {
    return peel_error("UV Peel authored quads do not match its canonical triangles.");
  }
  if (chart.guides.empty() || chart.guides.size() > chart.positions.size()) {
    return peel_error("UV Peel needs an opened flow guide for each chart.");
  }
  return std::nullopt;
}

/// Keeps connectivity checks linear in storage rather than building a matrix.
class DisjointSets {
public:
  explicit DisjointSets(std::size_t count) : parents_(count), ranks_(count, 0U) {
    std::iota(parents_.begin(), parents_.end(), 0U);
  }

  [[nodiscard]] std::size_t root(std::size_t value) {
    while (parents_[value] != value) {
      parents_[value] = parents_[parents_[value]];
      value = parents_[value];
    }
    return value;
  }

  void join(std::size_t left, std::size_t right) {
    left = root(left);
    right = root(right);
    if (left == right) {
      return;
    }
    if (ranks_[left] < ranks_[right]) {
      std::swap(left, right);
    }
    parents_[right] = left;
    if (ranks_[left] == ranks_[right]) {
      ++ranks_[left];
    }
  }

private:
  std::vector<std::size_t> parents_;
  std::vector<unsigned char> ranks_;
};

/// Identifies an edge only within the already bounded private chart.
[[nodiscard]] std::uint64_t edge_key(std::size_t first, std::size_t second) {
  const auto low = static_cast<std::uint64_t>(std::min(first, second));
  const auto high = static_cast<std::uint64_t>(std::max(first, second));
  return (low << 32U) | high;
}

/// Records canonical triangle adjacency without changing the owner's cuts.
struct TriangleEdge {
  std::uint64_t key = 0U;
  std::size_t triangle = 0U;
  bool forward = false;
};

/// Defensively rejects malformed or underconstrained numerical chart inputs.
[[nodiscard]] std::optional<Error> check_indices(const GuidedPeelChart &chart) {
  for (const auto &quad : chart.quads) {
    for (std::size_t corner = 0U; corner < quad.size(); ++corner) {
      if (quad[corner] >= chart.positions.size()) {
        return peel_error("UV Peel encountered an invalid authored quad.");
      }
      for (std::size_t previous = 0U; previous < corner; ++previous) {
        if (quad[corner] == quad[previous]) {
          return peel_error("UV Peel encountered a repeated authored quad corner.");
        }
      }
    }
  }
  std::vector<bool> used(chart.positions.size(), false);
  std::vector<TriangleEdge> edges;
  edges.reserve(chart.triangles.size() * 3U);
  for (std::size_t index = 0U; index < chart.triangles.size(); ++index) {
    const auto &vertices = chart.triangles[index].vertices;
    for (std::size_t corner = 0U; corner < 3U; ++corner) {
      const auto first = vertices[corner];
      const auto second = vertices[(corner + 1U) % 3U];
      if (first >= chart.positions.size() || second >= chart.positions.size() ||
          first == second) {
        return peel_error("UV Peel encountered an invalid chart triangle.");
      }
      used[first] = true;
      edges.push_back({edge_key(first, second), index, first < second});
    }
  }
  if (std::ranges::find(used, false) != used.end()) {
    return peel_error("UV Peel cannot solve unused or unconstrained chart vertices.");
  }
  std::ranges::sort(edges, [](const TriangleEdge &left, const TriangleEdge &right) {
    return left.key < right.key ||
           (left.key == right.key && left.triangle < right.triangle);
  });
  DisjointSets components(chart.triangles.size());
  for (std::size_t begin = 0U; begin < edges.size();) {
    std::size_t end = begin + 1U;
    while (end < edges.size() && edges[end].key == edges[begin].key) {
      ++end;
    }
    if (end - begin > 2U ||
        (end - begin == 2U && edges[begin].forward == edges[begin + 1U].forward)) {
      return peel_error("UV Peel needs a consistently oriented manifold chart.");
    }
    if (end - begin == 2U) {
      components.join(edges[begin].triangle, edges[begin + 1U].triangle);
    }
    begin = end;
  }
  for (std::size_t index = 1U; index < chart.triangles.size(); ++index) {
    if (components.root(index) != components.root(0U)) {
      return peel_error(
          "UV Peel cannot solve disconnected or vertex-only-connected charts together.");
    }
  }
  std::vector<bool> guide_vertices(chart.positions.size(), false);
  std::size_t references = 0U;
  for (const auto &guide : chart.guides) {
    references += guide.reference ? 1U : 0U;
    if (guide.vertices.size() < 2U || guide.vertices.size() > chart.positions.size()) {
      return peel_error("UV Peel needs flow guides containing at least two vertices.");
    }
    for (std::size_t corner = 0U; corner < guide.vertices.size(); ++corner) {
      const auto vertex = guide.vertices[corner];
      if (vertex >= chart.positions.size() || guide_vertices[vertex]) {
        return peel_error(
            "UV Peel flow guides must be opened, separate, nonbranching chains.");
      }
      guide_vertices[vertex] = true;
      if (corner == 0U) {
        continue;
      }
      const auto key = edge_key(guide.vertices[corner - 1U], vertex);
      const auto edge = std::lower_bound(
          edges.begin(), edges.end(), key,
          [](const TriangleEdge &item, std::uint64_t value) { return item.key < value; });
      if (edge == edges.end() || edge->key != key) {
        return peel_error("UV Peel flow guides must follow chart edges.");
      }
    }
  }
  if (references != 1U) {
    return peel_error("UV Peel needs exactly one reference flow guide per chart.");
  }
  return std::nullopt;
}

/// Associates an authored quad edge with its local winding position.
struct QuadEdge {
  std::uint64_t key = 0U;
  std::size_t quad = 0U;
  std::size_t corner = 0U;
};

/// Stores a verified product grid; rows increase in the positive V direction.
struct QuadStrip {
  std::vector<std::size_t> nodes;
  std::size_t stations = 0U;
  std::size_t rows = 0U;
  std::size_t reference_row = 0U;
};

/// Propagates longitudinal opposite-edge pairs from every selected guide.
[[nodiscard]] std::optional<std::vector<int>> classify_quad_edges(const GuidedPeelChart &chart) {
  std::vector<QuadEdge> edges;
  edges.reserve(chart.quads.size() * 4U);
  for (std::size_t quad = 0U; quad < chart.quads.size(); ++quad) {
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
      edges.push_back({edge_key(chart.quads[quad][corner],
                                chart.quads[quad][(corner + 1U) % 4U]), quad, corner});
    }
  }
  std::ranges::sort(edges, [](const QuadEdge &left, const QuadEdge &right) {
    return left.key < right.key ||
           (left.key == right.key && left.quad < right.quad);
  });
  std::vector<std::array<std::size_t, 4>> neighbors(chart.quads.size());
  for (auto &quad : neighbors) {
    quad.fill(kNoColumn);
  }
  for (std::size_t begin = 0U; begin < edges.size();) {
    std::size_t end = begin + 1U;
    while (end < edges.size() && edges[end].key == edges[begin].key) {
      ++end;
    }
    if (end - begin > 2U) {
      return std::nullopt;
    }
    if (end - begin == 2U) {
      const auto &first = edges[begin];
      const auto &second = edges[begin + 1U];
      neighbors[first.quad][first.corner] = second.quad * 4U + second.corner;
      neighbors[second.quad][second.corner] = first.quad * 4U + first.corner;
    }
    begin = end;
  }
  std::vector<int> parity(chart.quads.size(), -1);
  std::vector<std::size_t> pending;
  pending.reserve(chart.quads.size());
  const auto assign = [&](std::size_t quad, int value) {
    if (parity[quad] >= 0) {
      return parity[quad] == value;
    }
    parity[quad] = value;
    pending.push_back(quad);
    return true;
  };
  for (const auto &guide : chart.guides) {
    for (std::size_t index = 1U; index < guide.vertices.size(); ++index) {
      const auto key = edge_key(guide.vertices[index - 1U], guide.vertices[index]);
      auto edge = std::lower_bound(edges.begin(), edges.end(), key,
          [](const QuadEdge &candidate, std::uint64_t value) { return candidate.key < value; });
      if (edge == edges.end() || edge->key != key) {
        return std::nullopt;
      }
      while (edge != edges.end() && edge->key == key) {
        if (!assign(edge->quad, static_cast<int>(edge->corner % 2U))) {
          return std::nullopt;
        }
        ++edge;
      }
    }
  }
  for (std::size_t cursor = 0U; cursor < pending.size(); ++cursor) {
    const auto quad = pending[cursor];
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
      const auto neighbor = neighbors[quad][corner];
      if (neighbor == kNoColumn) {
        continue;
      }
      const bool longitudinal = static_cast<int>(corner % 2U) == parity[quad];
      const int neighbor_parity =
          static_cast<int>((neighbor % 4U + (longitudinal ? 0U : 1U)) % 2U);
      if (!assign(neighbor / 4U, neighbor_parity)) {
        return std::nullopt;
      }
    }
  }
  if (pending.size() != chart.quads.size()) {
    return std::nullopt;
  }
  return parity;
}

/// Accepts only complete authored product strips anchored at every reference station.
[[nodiscard]] std::optional<QuadStrip> recognize_quad_strip(const GuidedPeelChart &chart) {
  if (chart.quads.empty() || chart.quads.size() * 2U != chart.triangles.size()) {
    return std::nullopt;
  }
  const auto reference = std::ranges::find(chart.guides, true, &PeelGuide::reference);
  const auto parity = classify_quad_edges(chart);
  if (reference == chart.guides.end() || !parity.has_value()) {
    return std::nullopt;
  }
  DisjointSets stations(chart.positions.size());
  DisjointSets rows(chart.positions.size());
  for (std::size_t quad = 0U; quad < chart.quads.size(); ++quad) {
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
      const auto first = chart.quads[quad][corner];
      const auto second = chart.quads[quad][(corner + 1U) % 4U];
      if (static_cast<int>(corner % 2U) == (*parity)[quad]) {
        rows.join(first, second);
      } else {
        stations.join(first, second);
      }
    }
  }
  std::vector<std::size_t> station_indices(chart.positions.size(), kNoColumn);
  for (std::size_t index = 0U; index < reference->vertices.size(); ++index) {
    auto &station = station_indices[stations.root(reference->vertices[index])];
    if (station != kNoColumn) {
      return std::nullopt;
    }
    station = index;
  }
  for (std::size_t vertex = 0U; vertex < chart.positions.size(); ++vertex) {
    if (station_indices[stations.root(vertex)] == kNoColumn) {
      return std::nullopt;
    }
  }
  std::vector<std::size_t> below(chart.positions.size(), kNoColumn);
  std::vector<std::size_t> above(chart.positions.size(), kNoColumn);
  const auto connect_rows = [&](std::size_t low, std::size_t high) {
    if (low == high || (above[low] != kNoColumn && above[low] != high) ||
        (below[high] != kNoColumn && below[high] != low)) {
      return false;
    }
    above[low] = high;
    below[high] = low;
    return true;
  };
  for (std::size_t quad = 0U; quad < chart.quads.size(); ++quad) {
    std::array<std::size_t, 4> indices{};
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
      indices[corner] = station_indices[stations.root(chart.quads[quad][corner])];
    }
    const auto [minimum, maximum] = std::ranges::minmax(indices);
    if (maximum != minimum + 1U) {
      return std::nullopt;
    }
    for (std::size_t corner = 0U; corner < 4U; ++corner) {
      const auto next = (corner + 1U) % 4U;
      const bool longitudinal = static_cast<int>(corner % 2U) == (*parity)[quad];
      if (longitudinal) {
        if (indices[corner] == indices[next]) {
          return std::nullopt;
        }
        continue;
      }
      if (indices[corner] != indices[next]) {
        return std::nullopt;
      }
      const auto first_row = rows.root(chart.quads[quad][corner]);
      const auto second_row = rows.root(chart.quads[quad][next]);
      // The authored winding traverses negative V at the lower U station.
      if (!(indices[corner] == minimum ? connect_rows(second_row, first_row)
                                     : connect_rows(first_row, second_row))) {
        return std::nullopt;
      }
    }
  }
  QuadStrip strip;
  strip.stations = reference->vertices.size();
  std::size_t first_row = kNoColumn;
  for (std::size_t vertex = 0U; vertex < chart.positions.size(); ++vertex) {
    if (rows.root(vertex) != vertex) {
      continue;
    }
    ++strip.rows;
    if (below[vertex] == kNoColumn) {
      if (first_row != kNoColumn) {
        return std::nullopt;
      }
      first_row = vertex;
    }
  }
  if (first_row == kNoColumn || strip.rows < 2U ||
      strip.rows > chart.positions.size() / strip.stations ||
      strip.rows * strip.stations != chart.positions.size()) {
    return std::nullopt;
  }
  std::vector<std::size_t> row_indices(chart.positions.size(), kNoColumn);
  auto row = first_row;
  for (std::size_t index = 0U; index < strip.rows; ++index) {
    if (row == kNoColumn || row_indices[row] != kNoColumn) {
      return std::nullopt;
    }
    row_indices[row] = index;
    row = above[row];
  }
  if (row != kNoColumn) {
    return std::nullopt;
  }
  strip.reference_row = row_indices[rows.root(reference->vertices.front())];
  strip.nodes.assign(chart.positions.size(), kNoColumn);
  for (std::size_t vertex = 0U; vertex < chart.positions.size(); ++vertex) {
    const auto row_index = row_indices[rows.root(vertex)];
    if (row_index == kNoColumn) {
      return std::nullopt;
    }
    const auto station = station_indices[stations.root(vertex)];
    auto &node = strip.nodes[row_index * strip.stations + station];
    if (node != kNoColumn) {
      return std::nullopt;
    }
    node = vertex;
  }
  const auto cell_count = (strip.rows - 1U) * (strip.stations - 1U);
  if (cell_count != chart.quads.size()) {
    return std::nullopt;
  }
  std::vector<bool> occupied(cell_count, false);
  for (const auto &quad : chart.quads) {
    std::size_t minimum_row = strip.rows;
    std::size_t maximum_row = 0U;
    std::size_t minimum_station = strip.stations;
    for (const auto vertex : quad) {
      const auto row_index = row_indices[rows.root(vertex)];
      minimum_row = std::min(minimum_row, row_index);
      maximum_row = std::max(maximum_row, row_index);
      minimum_station = std::min(minimum_station, station_indices[stations.root(vertex)]);
    }
    if (maximum_row != minimum_row + 1U) {
      return std::nullopt;
    }
    const auto cell = minimum_row * (strip.stations - 1U) + minimum_station;
    if (occupied[cell]) {
      return std::nullopt;
    }
    occupied[cell] = true;
  }
  return strip;
}

/// Uses double differences before normalization so translations do not set precision.
[[nodiscard]] PeelPoint3 subtract(PeelPoint3 left, PeelPoint3 right) {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

/// Evaluates a finite geometric distance without squaring very large inputs.
[[nodiscard]] double length(PeelPoint3 value) {
  return std::hypot(value.x, value.y, value.z);
}

/// Evaluates the local triangle normal without selecting a global projection plane.
[[nodiscard]] PeelPoint3 cross(PeelPoint3 left, PeelPoint3 right) {
  return {left.y * right.z - left.z * right.y,
          left.z * right.x - left.x * right.z,
          left.x * right.y - left.y * right.x};
}

/// Stores the physical scale independently of translation-free normalized positions.
struct NormalizedGeometry {
  std::vector<PeelPoint3> positions;
  double scale = 1.0;
};

/// Places all numerical triangle computations in one bounded double coordinate frame.
[[nodiscard]] Result<NormalizedGeometry> normalize_geometry(const GuidedPeelChart &chart) {
  NormalizedGeometry result;
  result.positions.reserve(chart.positions.size());
  double scale = 0.0;
  const auto origin = chart.positions.front();
  for (const auto position : chart.positions) {
    const auto delta = subtract(position, origin);
    if (!std::isfinite(delta.x) || !std::isfinite(delta.y) || !std::isfinite(delta.z)) {
      return Result<NormalizedGeometry>::failure(
          peel_error("UV Peel positions are non-finite or exceed the numerical range."));
    }
    scale = std::max({scale, std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)});
    result.positions.push_back(delta);
  }
  if (!(scale > 0.0) || !std::isfinite(scale)) {
    return Result<NormalizedGeometry>::failure(peel_error("UV Peel chart has no extent."));
  }
  result.scale = scale;
  for (auto &position : result.positions) {
    position.x /= scale;
    position.y /= scale;
    position.z /= scale;
  }
  return Result<NormalizedGeometry>::success(std::move(result));
}

/// Holds a validated normalized triangle's local conformal frame.
struct TriangleFrame {
  double edge_length = 0.0;
  double x = 0.0;
  double y = 0.0;
  double weight = 0.0;
};

/// Retains the same geometric degeneracy checks for both numerical formulations.
[[nodiscard]] Result<TriangleFrame> triangle_frame(
    const NormalizedGeometry &geometry, const PeelTriangle &triangle) {
  const auto &vertices = triangle.vertices;
  const auto first = subtract(geometry.positions[vertices[1]], geometry.positions[vertices[0]]);
  const auto second = subtract(geometry.positions[vertices[2]], geometry.positions[vertices[0]]);
  const double first_length = length(first);
  const double largest_edge = std::max({first_length, length(second), length(subtract(second, first))});
  const double twice_area = length(cross(first, second));
  if (!(twice_area > kRelativeAreaTolerance * largest_edge * largest_edge) ||
      !std::isfinite(twice_area)) {
    return Result<TriangleFrame>::failure(
        peel_error("UV Peel cannot parameterize a degenerate or extremely thin triangle."));
  }
  return Result<TriangleFrame>::success({
      first_length,
      (first.x * second.x + first.y * second.y + first.z * second.z) / first_length,
      twice_area / first_length,
      std::sqrt(twice_area * 0.5)});
}

/// Represents an eliminated exact pin or a remaining least-squares variable.
struct CoordinateVariable {
  std::size_t column = kNoColumn;
  double pinned = 0.0;
};

/// Holds one of at most six nonzero coefficients in a numerical residual row.
struct RowEntry {
  std::size_t column = 0U;
  double coefficient = 0.0;
};

/// Retains sparse metric or Cauchy-Riemann rows instead of dense normal equations.
struct SparseRow {
  std::array<RowEntry, 6> entries{};
  std::size_t count = 0U;
  double right_hand_side = 0.0;

  void add(CoordinateVariable variable, double coefficient) {
    if (coefficient == 0.0) {
      return;
    }
    if (variable.column == kNoColumn) {
      right_hand_side -= variable.pinned * coefficient;
      return;
    }
    for (std::size_t index = 0U; index < count; ++index) {
      if (entries[index].column == variable.column) {
        entries[index].coefficient += coefficient;
        return;
      }
    }
    entries[count++] = {variable.column, coefficient};
  }
};

/// Shares guide V coordinates and removes all reference-arc pins exactly.
struct ConstrainedSystem {
  std::vector<CoordinateVariable> u;
  std::vector<CoordinateVariable> v;
  std::vector<SparseRow> rows;
  std::vector<double> column_scales;
  std::size_t columns = 0U;
  std::size_t nonzeros = 0U;
  std::size_t maximum_column_terms = 0U;
};

/// Applies the same finite-coefficient and column-conditioning rules to both systems.
[[nodiscard]] Result<ConstrainedSystem> finalize_system(ConstrainedSystem system) {
  if (system.columns == 0U || system.columns > system.rows.size()) {
    return Result<ConstrainedSystem>::failure(
        peel_error("UV Peel guide constraints do not define a two-dimensional chart."));
  }
  system.column_scales.assign(system.columns, 0.0);
  std::vector<std::size_t> column_terms(system.columns, 0U);
  for (const auto &row : system.rows) {
    if (!std::isfinite(row.right_hand_side)) {
      return Result<ConstrainedSystem>::failure(peel_error("UV Peel constraints exceed the numerical range."));
    }
    for (std::size_t index = 0U; index < row.count; ++index) {
      const auto &entry = row.entries[index];
      system.column_scales[entry.column] += entry.coefficient * entry.coefficient;
      ++column_terms[entry.column];
    }
  }
  system.maximum_column_terms = *std::ranges::max_element(column_terms);
  for (auto &scale : system.column_scales) {
    if (!(scale > 0.0) || !std::isfinite(scale)) {
      return Result<ConstrainedSystem>::failure(
          peel_error("UV Peel found an unconstrained or ill-conditioned chart coordinate."));
    }
    scale = 1.0 / std::sqrt(scale);
  }
  for (auto &row : system.rows) {
    for (std::size_t index = 0U; index < row.count; ++index) {
      auto &entry = row.entries[index];
      entry.coefficient *= system.column_scales[entry.column];
    }
    system.nonzeros += row.count;
  }
  return Result<ConstrainedSystem>::success(std::move(system));
}

/// Builds the area-weighted equations from the original LSCM formulation.
[[nodiscard]] Result<ConstrainedSystem> build_conformal_system(
    const GuidedPeelChart &chart, const NormalizedGeometry &geometry) {
  ConstrainedSystem system;
  const auto count = chart.positions.size();
  system.u.resize(count);
  system.v.resize(count);
  std::vector<bool> pinned(count, false);
  std::vector<std::size_t> v_representatives(count);
  std::iota(v_representatives.begin(), v_representatives.end(), 0U);
  for (const auto &guide : chart.guides) {
    double distance = 0.0;
    for (std::size_t index = 0U; index < guide.vertices.size(); ++index) {
      const auto vertex = guide.vertices[index];
      v_representatives[vertex] = guide.vertices.front();
      if (index > 0U) {
        const double step = length(subtract(
            geometry.positions[vertex], geometry.positions[guide.vertices[index - 1U]]));
        if (!(step > 0.0) || !std::isfinite(step)) {
          return Result<ConstrainedSystem>::failure(
              peel_error("UV Peel encountered a collapsed flow-guide edge."));
        }
        distance += step;
      }
      if (guide.reference) {
        pinned[vertex] = true;
        system.u[vertex].pinned = distance;
      }
    }
  }
  for (std::size_t vertex = 0U; vertex < count; ++vertex) {
    if (!pinned[vertex]) {
      system.u[vertex].column = system.columns++;
    }
  }
  std::vector<std::size_t> v_columns(count, kNoColumn);
  for (std::size_t vertex = 0U; vertex < count; ++vertex) {
    if (pinned[vertex]) {
      continue;
    }
    auto &column = v_columns[v_representatives[vertex]];
    if (column == kNoColumn) {
      column = system.columns++;
    }
    system.v[vertex].column = column;
  }
  system.rows.reserve(chart.triangles.size() * 2U);
  for (const auto &triangle : chart.triangles) {
    const auto &vertices = triangle.vertices;
    const auto frame = triangle_frame(geometry, triangle);
    if (!frame.ok()) {
      return Result<ConstrainedSystem>::failure(frame.error());
    }
    const auto [first_length, local_x, local_y, weight] = frame.value();
    const std::array<double, 3> gradient_x{
        -weight / first_length, weight / first_length, 0.0};
    const std::array<double, 3> gradient_y{
        weight * (local_x / first_length - 1.0) / local_y,
        -weight * (local_x / first_length) / local_y, weight / local_y};
    SparseRow real;
    SparseRow imaginary;
    for (std::size_t corner = 0U; corner < 3U; ++corner) {
      const auto vertex = vertices[corner];
      real.add(system.u[vertex], gradient_x[corner]);
      real.add(system.v[vertex], -gradient_y[corner]);
      imaginary.add(system.u[vertex], gradient_y[corner]);
      imaginary.add(system.v[vertex], gradient_x[corner]);
    }
    system.rows.push_back(real);
    system.rows.push_back(imaginary);
  }
  return finalize_system(std::move(system));
}

/// Fixes strip stations exactly and fits only physical transverse widths.
[[nodiscard]] Result<ConstrainedSystem> build_strip_system(
    const GuidedPeelChart &chart, const NormalizedGeometry &geometry, const QuadStrip &strip) {
  for (const auto &triangle : chart.triangles) {
    const auto frame = triangle_frame(geometry, triangle);
    if (!frame.ok()) {
      return Result<ConstrainedSystem>::failure(frame.error());
    }
  }
  ConstrainedSystem system;
  const auto count = chart.positions.size();
  system.u.resize(count);
  system.v.resize(count);
  std::vector<double> distances(strip.stations, 0.0);
  for (std::size_t station = 1U; station < strip.stations; ++station) {
    const auto first = strip.nodes[strip.reference_row * strip.stations + station - 1U];
    const auto second = strip.nodes[strip.reference_row * strip.stations + station];
    const double step = length(subtract(geometry.positions[second], geometry.positions[first]));
    if (!(step > 0.0) || !std::isfinite(step)) {
      return Result<ConstrainedSystem>::failure(
          peel_error("UV Peel encountered a collapsed flow-guide edge."));
    }
    distances[station] = distances[station - 1U] + step;
  }
  std::vector<bool> pinned(count, false);
  for (std::size_t row = 0U; row < strip.rows; ++row) {
    for (std::size_t station = 0U; station < strip.stations; ++station) {
      const auto node = strip.nodes[row * strip.stations + station];
      system.u[node].pinned = distances[station];
      pinned[node] = row == strip.reference_row;
    }
  }
  std::vector<std::size_t> representatives(count);
  std::iota(representatives.begin(), representatives.end(), 0U);
  for (const auto &guide : chart.guides) {
    for (const auto vertex : guide.vertices) {
      representatives[vertex] = guide.vertices.front();
    }
  }
  std::vector<std::size_t> columns(count, kNoColumn);
  for (std::size_t node = 0U; node < count; ++node) {
    if (pinned[node]) {
      continue;
    }
    auto &column = columns[representatives[node]];
    if (column == kNoColumn) {
      column = system.columns++;
    }
    system.v[node].column = column;
  }
  system.rows.reserve((strip.rows - 1U) * strip.stations);
  for (std::size_t row = 0U; row + 1U < strip.rows; ++row) {
    for (std::size_t station = 0U; station < strip.stations; ++station) {
      const auto first = strip.nodes[row * strip.stations + station];
      const auto second = strip.nodes[(row + 1U) * strip.stations + station];
      const double width = length(subtract(geometry.positions[second], geometry.positions[first]));
      if (!(width > 0.0) || !std::isfinite(width)) {
        return Result<ConstrainedSystem>::failure(
            peel_error("UV Peel encountered a collapsed transverse strip edge."));
      }
      SparseRow metric;
      metric.add(system.v[first], -1.0);
      metric.add(system.v[second], 1.0);
      metric.right_hand_side += width;
      system.rows.push_back(metric);
    }
  }
  return finalize_system(std::move(system));
}

/// Uses authored strip structure when proven, retaining LSCM for other charts.
[[nodiscard]] Result<ConstrainedSystem> build_system(
    const GuidedPeelChart &chart, const NormalizedGeometry &geometry) {
  if (const auto strip = recognize_quad_strip(chart)) {
    return build_strip_system(chart, geometry, *strip);
  }
  return build_conformal_system(chart, geometry);
}

/// Multiplies the sparse, column-scaled system by an unknown vector.
void multiply(const ConstrainedSystem &system, std::span<const double> values,
              std::span<double> product) {
  for (std::size_t index = 0U; index < system.rows.size(); ++index) {
    const auto &row = system.rows[index];
    double value = 0.0;
    for (std::size_t entry = 0U; entry < row.count; ++entry) {
      value += row.entries[entry].coefficient * values[row.entries[entry].column];
    }
    product[index] = value;
  }
}

/// Multiplies by the transpose without forming or storing normal equations.
void multiply_transpose(const ConstrainedSystem &system,
                        std::span<const double> values, std::span<double> product) {
  std::ranges::fill(product, 0.0);
  for (std::size_t index = 0U; index < system.rows.size(); ++index) {
    const auto &row = system.rows[index];
    for (std::size_t entry = 0U; entry < row.count; ++entry) {
      product[row.entries[entry].column] += row.entries[entry].coefficient * values[index];
    }
  }
}

/// Computes vector magnitudes in the bounded, column-scaled numerical system.
[[nodiscard]] double norm(std::span<const double> values) {
  double squared = 0.0;
  for (const double value : values) {
    squared += value * value;
  }
  return std::sqrt(squared);
}

/// Applies the normalization used by Golub-Kahan bidiagonalization.
void divide(std::span<double> values, double divisor) {
  for (auto &value : values) {
    value /= divisor;
  }
}

/// Bounds accumulated double roundoff for the stated number of arithmetic operations.
[[nodiscard]] double roundoff_gamma(std::size_t operations) {
  const double accumulated =
      static_cast<double>(operations) * std::numeric_limits<double>::epsilon();
  return accumulated / (1.0 - accumulated);
}

/// Tests stationarity without demanding residual-gradient precision below its evaluation error.
[[nodiscard]] bool converged(const ConstrainedSystem &system,
                             std::span<const double> solution,
                             double right_hand_side_norm,
                             std::span<double> residual, std::span<double> gradient) {
  multiply(system, solution, residual);
  for (std::size_t index = 0U; index < residual.size(); ++index) {
    residual[index] -= system.rows[index].right_hand_side;
  }
  multiply_transpose(system, residual, gradient);
  const double residual_norm = norm(residual);
  const double gradient_norm = norm(gradient);
  const double solution_norm = norm(solution);
  const double matrix_norm = std::sqrt(static_cast<double>(system.columns));
  if (!std::isfinite(residual_norm) || !std::isfinite(gradient_norm) ||
      !std::isfinite(solution_norm)) {
    return false;
  }
  const double requested_gradient = kNormalTolerance * matrix_norm * residual_norm;
  if (residual_norm <= kResidualTolerance *
                           (matrix_norm * solution_norm + right_hand_side_norm) ||
      gradient_norm <= requested_gradient) {
    return true;
  }
  // Near-compatible constraints may leave a real residual of order float epsilon.
  // Subtracting Ax-b still incurs double roundoff at the scale of Ax and b, so
  // a relative gradient test alone can request unattainable sub-roundoff accuracy.
  double residual_error_squared = 0.0;
  for (const auto &row : system.rows) {
    double magnitude = std::abs(row.right_hand_side);
    for (std::size_t index = 0U; index < row.count; ++index) {
      const auto &entry = row.entries[index];
      magnitude += std::abs(entry.coefficient * solution[entry.column]);
    }
    const double row_error = roundoff_gamma(row.count * 2U + 1U) * magnitude;
    residual_error_squared += row_error * row_error;
  }
  // Columns have unit norm. The Frobenius bound covers propagation of row errors
  // through A^T, plus the separately counted dot-product error in each column.
  const double gradient_error = matrix_norm *
      (std::sqrt(residual_error_squared) +
       roundoff_gamma(system.maximum_column_terms * 2U) * residual_norm);
  return std::isfinite(gradient_error) && gradient_norm <= requested_gradient + gradient_error;
}

/// Implements column-scaled LSQR from its bidiagonalization, not imported source.
[[nodiscard]] Result<std::vector<double>> solve_system(const ConstrainedSystem &system) {
  std::vector<double> solution(system.columns, 0.0);
  std::vector<double> left(system.rows.size());
  for (std::size_t index = 0U; index < left.size(); ++index) {
    left[index] = system.rows[index].right_hand_side;
  }
  double beta = norm(left);
  const double right_hand_side_norm = beta;
  if (!std::isfinite(beta)) {
    return Result<std::vector<double>>::failure(peel_error("UV Peel constraints exceed the numerical range."));
  }
  if (beta == 0.0) {
    return Result<std::vector<double>>::success(std::move(solution));
  }
  divide(left, beta);
  std::vector<double> right(system.columns);
  multiply_transpose(system, left, right);
  double alpha = norm(right);
  if (!std::isfinite(alpha)) {
    return Result<std::vector<double>>::failure(peel_error("UV Peel system is ill-conditioned."));
  }
  std::vector<double> residual(system.rows.size());
  std::vector<double> gradient(system.columns);
  if (alpha == 0.0) {
    if (converged(system, solution, right_hand_side_norm, residual, gradient)) {
      return Result<std::vector<double>>::success(std::move(solution));
    }
    return Result<std::vector<double>>::failure(peel_error("UV Peel has underdetermined guide constraints."));
  }
  divide(right, alpha);
  std::vector<double> direction = right;
  std::vector<double> next_left(system.rows.size());
  std::vector<double> next_right(system.columns);
  double rho_bar = alpha;
  double phi_bar = beta;
  double matrix_norm = 0.0;
  double inverse_norm_squared = 0.0;
  std::size_t coefficient_visits = system.nonzeros;
  for (std::size_t iteration = 0U; iteration < kMaximumIterations; ++iteration) {
    if (coefficient_visits > kMaximumCoefficientVisits - system.nonzeros * 5U) {
      return Result<std::vector<double>>::failure(peel_error(
          "UV Peel exceeded its numerical work budget. Select a smaller or simpler patch."));
    }
    coefficient_visits += system.nonzeros * 2U;
    multiply(system, right, next_left);
    for (std::size_t index = 0U; index < left.size(); ++index) {
      next_left[index] -= alpha * left[index];
    }
    beta = norm(next_left);
    if (!std::isfinite(beta)) {
      break;
    }
    if (beta > 0.0) {
      divide(next_left, beta);
    }
    matrix_norm = std::hypot(matrix_norm, std::hypot(alpha, beta));
    multiply_transpose(system, next_left, next_right);
    for (std::size_t index = 0U; index < right.size(); ++index) {
      next_right[index] -= beta * right[index];
    }
    alpha = norm(next_right);
    if (!std::isfinite(alpha)) {
      break;
    }
    if (alpha > 0.0) {
      divide(next_right, alpha);
    }
    left.swap(next_left);
    right.swap(next_right);
    const double rho = std::hypot(rho_bar, beta);
    if (!(rho > 0.0) || !std::isfinite(rho)) {
      break;
    }
    const double cosine = rho_bar / rho;
    const double sine = beta / rho;
    const double theta = sine * alpha;
    rho_bar = -cosine * alpha;
    const double phi = cosine * phi_bar;
    phi_bar *= sine;
    const double solution_step = phi / rho;
    const double direction_step = theta / rho;
    for (std::size_t index = 0U; index < solution.size(); ++index) {
      const double inverse_component = direction[index] / rho;
      inverse_norm_squared += inverse_component * inverse_component;
      solution[index] += solution_step * direction[index];
      direction[index] = right[index] - direction_step * direction[index];
    }
    const double condition = matrix_norm * std::sqrt(inverse_norm_squared);
    if (!std::isfinite(condition) || condition > kMaximumCondition) {
      return Result<std::vector<double>>::failure(peel_error(
          "UV Peel system is too ill-conditioned. Simplify very thin faces or choose another guide."));
    }
    const double estimated_gradient = alpha * std::abs(sine * phi);
    const double estimated_roundoff =
        roundoff_gamma(system.rows.front().entries.size() * 2U + 1U) *
        matrix_norm * right_hand_side_norm;
    if ((iteration + 1U) % 16U == 0U || alpha == 0.0 || beta == 0.0 ||
        estimated_gradient <= kNormalTolerance * matrix_norm * std::abs(phi_bar) + estimated_roundoff ||
        iteration + 1U == kMaximumIterations) {
      coefficient_visits += system.nonzeros * 3U;
      if (converged(system, solution, right_hand_side_norm, residual, gradient)) {
        return Result<std::vector<double>>::success(std::move(solution));
      }
    }
  }
  // Happy breakdown can occur between checks; accept only a verified stationary
  // solution, never the zero bidiagonal coefficient on its own.
  if (coefficient_visits <= kMaximumCoefficientVisits - system.nonzeros * 3U &&
      converged(system, solution, right_hand_side_norm, residual, gradient)) {
    return Result<std::vector<double>>::success(std::move(solution));
  }
  return Result<std::vector<double>>::failure(peel_error(
      "UV Peel did not converge to a stable chart. Select a smaller patch or adjust the flow guides."));
}

/// Computes a signed twice-area in a translation-free UV coordinate frame.
[[nodiscard]] double orientation(PeelPoint2 first, PeelPoint2 second, PeelPoint2 point) {
  return (second.u - first.u) * (point.v - first.v) -
         (second.v - first.v) * (point.u - first.u);
}

/// Bounds determinant roundoff from its actual products, not UV offset magnitude.
[[nodiscard]] double orientation_error(PeelPoint2 first, PeelPoint2 second, PeelPoint2 point) {
  constexpr double kRoundoff = 64.0 * std::numeric_limits<double>::epsilon();
  return kRoundoff *
         (std::abs((second.u - first.u) * (point.v - first.v)) +
          std::abs((second.v - first.v) * (point.u - first.u)));
}

/// Allows shared-edge and cut-boundary contact while rejecting intersecting interiors.
[[nodiscard]] bool triangle_interiors_overlap(
    const PeelTriangleCoordinates &first, const PeelTriangleCoordinates &second,
    double sign) {
  const auto separates = [&](const PeelTriangleCoordinates &boundary,
                             const PeelTriangleCoordinates &other) {
    for (std::size_t edge = 0U; edge < 3U; ++edge) {
      const auto start = boundary[edge];
      const auto end = boundary[(edge + 1U) % 3U];
      bool outside = true;
      for (const auto point : other) {
        if (orientation(start, end, point) * sign > orientation_error(start, end, point)) {
          outside = false;
          break;
        }
      }
      if (outside) {
        return true;
      }
    }
    return false;
  };
  return !separates(first, second) && !separates(second, first);
}

/// Limits actual intersection to the contact extent implied by displaced corner rays.
[[nodiscard]] bool intersection_near_corner(
    const PeelTriangleCoordinates &first, const PeelTriangleCoordinates &second,
    double sign, PeelPoint2 corner, double radius, double roundoff) {
  std::array<PeelPoint2, 8> polygon{};
  std::array<PeelPoint2, 8> clipped{};
  std::copy(first.begin(), first.end(), polygon.begin());
  std::size_t count = first.size();
  for (std::size_t edge = 0U; edge < 3U; ++edge) {
    const auto start = second[edge];
    const auto end = second[(edge + 1U) % 3U];
    std::size_t clipped_count = 0U;
    const auto append = [&](PeelPoint2 point) {
      if (clipped_count == clipped.size()) {
        return false;
      }
      clipped[clipped_count++] = point;
      return true;
    };
    auto previous = polygon[count - 1U];
    double previous_side = orientation(start, end, previous) * sign;
    for (std::size_t index = 0U; index < count; ++index) {
      const auto current = polygon[index];
      const double current_side = orientation(start, end, current) * sign;
      if ((previous_side >= 0.0) != (current_side >= 0.0)) {
        const double fraction = previous_side / (previous_side - current_side);
        if (!std::isfinite(fraction) ||
            !append({previous.u + (current.u - previous.u) * fraction,
                     previous.v + (current.v - previous.v) * fraction})) {
          return false;
        }
      }
      if (current_side >= 0.0 && !append(current)) {
        return false;
      }
      previous = current;
      previous_side = current_side;
    }
    if (clipped_count == 0U) {
      return false;
    }
    polygon = clipped;
    count = clipped_count;
  }
  for (std::size_t index = 0U; index < count; ++index) {
    const double u = polygon[index].u - corner.u;
    const double v = polygon[index].v - corner.v;
    if (std::hypot(u, v) > radius + roundoff) {
      return false;
    }
  }
  return true;
}

/// Admits only displacement-induced contact at one shared chart corner.
[[nodiscard]] bool shared_corner_float_contact(
    const PeelTriangleCoordinates &first, const PeelTriangleCoordinates &second,
    std::size_t first_corner, std::size_t second_corner, double sign) {
  const auto first_point = first[first_corner];
  const auto second_point = second[second_corner];
  const double displacement =
      0.5 * std::hypot(second_point.u - first_point.u, second_point.v - first_point.v);
  if (!(displacement > 0.0)) {
    return false;
  }
  const PeelPoint2 midpoint{first_point.u + (second_point.u - first_point.u) * 0.5,
                            first_point.v + (second_point.v - first_point.v) * 0.5};
  auto stitched_first = first;
  auto stitched_second = second;
  stitched_first[first_corner] = midpoint;
  stitched_second[second_corner] = midpoint;
  if (orientation(stitched_first[0], stitched_first[1], stitched_first[2]) * sign <=
          orientation_error(stitched_first[0], stitched_first[1], stitched_first[2]) ||
      orientation(stitched_second[0], stitched_second[1], stitched_second[2]) * sign <=
          orientation_error(stitched_second[0], stitched_second[1], stitched_second[2]) ||
      triangle_interiors_overlap(stitched_first, stitched_second, sign)) {
    return false;
  }
  double maximum_cosine = -1.0;
  double longest_ray = 0.0;
  double shortest_ray = std::numeric_limits<double>::max();
  for (std::size_t first_index = 0U; first_index < 3U; ++first_index) {
    if (first_index == first_corner) {
      continue;
    }
    const PeelPoint2 first_ray{first[first_index].u - midpoint.u,
                               first[first_index].v - midpoint.v};
    const double first_length = std::hypot(first_ray.u, first_ray.v);
    for (std::size_t second_index = 0U; second_index < 3U; ++second_index) {
      if (second_index == second_corner) {
        continue;
      }
      const PeelPoint2 second_ray{second[second_index].u - midpoint.u,
                                  second[second_index].v - midpoint.v};
      const double second_length = std::hypot(second_ray.u, second_ray.v);
      if (!(first_length > 0.0) || !(second_length > 0.0)) {
        return false;
      }
      const double cosine = (first_ray.u * second_ray.u + first_ray.v * second_ray.v) /
                            (first_length * second_length);
      maximum_cosine = std::max(maximum_cosine, std::clamp(cosine, -1.0, 1.0));
      longest_ray = std::max({longest_ray, first_length, second_length});
      shortest_ray = std::min({shortest_ray, first_length, second_length});
    }
  }
  // Stitching moves one corner in each triangle by d. The triangles lie within
  // d of their disjoint stitched wedges, so a gap alpha permits intersection
  // only within d / sin(alpha / 2). The input float budget bounds d, not this
  // spatial extent: narrow wedges amplify displacement geometrically.
  const double half_gap_sine = std::sqrt(std::max(0.0, (1.0 - maximum_cosine) * 0.5));
  if (!(half_gap_sine > 0.0)) {
    return false;
  }
  const double radius = displacement / half_gap_sine;
  const double roundoff = 64.0 * std::numeric_limits<double>::epsilon() * longest_ray;
  if (!(shortest_ray > radius + roundoff)) {
    return false;
  }
  return intersection_near_corner(first, second, sign, midpoint, radius, roundoff);
}

/// Keeps both opposite corners outside the measured band between shared edge copies.
[[nodiscard]] bool separated_across_shared_edge(
    PeelPoint2 start, PeelPoint2 end, PeelPoint2 own_corner,
    PeelPoint2 other_start, PeelPoint2 other_end, PeelPoint2 other_corner, double sign) {
  const double band = std::max(
      std::abs(orientation(start, end, other_start)) + orientation_error(start, end, other_start),
      std::abs(orientation(start, end, other_end)) + orientation_error(start, end, other_end));
  return orientation(start, end, own_corner) * sign >
             band + orientation_error(start, end, own_corner) &&
         orientation(start, end, other_corner) * sign <
             -band - orientation_error(start, end, other_corner);
}

/// Uses chart-node identity and actual endpoint error; unrelated triangles stay strict.
[[nodiscard]] bool shared_float_contact(
    const PeelTriangle &first_topology, const PeelTriangle &second_topology,
    const PeelTriangleCoordinates &first, const PeelTriangleCoordinates &second,
    std::span<const PeelPoint2> coordinates, double sign, double budget) {
  if (!(budget > 0.0)) {
    return false;
  }
  std::size_t shared_count = 0U;
  std::size_t first_corner = 0U;
  std::size_t second_corner = 0U;
  for (std::size_t first_index = 0U; first_index < 3U; ++first_index) {
    for (std::size_t second_index = 0U; second_index < 3U; ++second_index) {
      if (first_topology.vertices[first_index] != second_topology.vertices[second_index]) {
        continue;
      }
      const auto reference = coordinates[first_topology.vertices[first_index]];
      for (const auto point : {first[first_index], second[second_index]}) {
        if (std::abs(point.u - reference.u) > budget || std::abs(point.v - reference.v) > budget) {
          return false;
        }
      }
      ++shared_count;
      first_corner = first_index;
      second_corner = second_index;
    }
  }
  if (shared_count == 1U) {
    return shared_corner_float_contact(first, second, first_corner, second_corner, sign);
  }
  if (shared_count != 2U) {
    return false;
  }
  for (std::size_t first_edge = 0U; first_edge < 3U; ++first_edge) {
    for (std::size_t second_edge = 0U; second_edge < 3U; ++second_edge) {
      if (first_topology.vertices[first_edge] != second_topology.vertices[(second_edge + 1U) % 3U] ||
          first_topology.vertices[(first_edge + 1U) % 3U] != second_topology.vertices[second_edge]) {
        continue;
      }
      return separated_across_shared_edge(
                 first[first_edge], first[(first_edge + 1U) % 3U], first[(first_edge + 2U) % 3U],
                 second[second_edge], second[(second_edge + 1U) % 3U], second[(second_edge + 2U) % 3U], sign) &&
             separated_across_shared_edge(
                 second[second_edge], second[(second_edge + 1U) % 3U], second[(second_edge + 2U) % 3U],
                 first[first_edge], first[(first_edge + 1U) % 3U], first[(first_edge + 2U) % 3U], sign);
    }
  }
  return false;
}

/// Stores a triangle's AABB for bounded deterministic sweep-and-prune validation.
struct TriangleBounds {
  double min_u = 0.0;
  double max_u = 0.0;
  double min_v = 0.0;
  double max_v = 0.0;
  std::size_t triangle = 0U;
};

/// Finds interior overlap with linear storage and a bounded broadphase pair count.
[[nodiscard]] std::optional<Error> validate_overlap(
    std::span<const PeelTriangle> topology, std::span<const PeelPoint2> coordinates,
    std::span<const PeelTriangleCoordinates> triangles, double sign, double stored_tolerance) {
  std::vector<TriangleBounds> bounds;
  bounds.reserve(triangles.size());
  double widths_u = 0.0;
  double widths_v = 0.0;
  double min_u = triangles.front().front().u;
  double max_u = min_u;
  double min_v = triangles.front().front().v;
  double max_v = min_v;
  for (std::size_t index = 0U; index < triangles.size(); ++index) {
    const auto &triangle = triangles[index];
    const auto first = triangle.front();
    TriangleBounds bound{first.u, first.u, first.v, first.v, index};
    for (const auto point : triangle) {
      bound.min_u = std::min(bound.min_u, point.u);
      bound.max_u = std::max(bound.max_u, point.u);
      bound.min_v = std::min(bound.min_v, point.v);
      bound.max_v = std::max(bound.max_v, point.v);
    }
    widths_u += bound.max_u - bound.min_u;
    widths_v += bound.max_v - bound.min_v;
    min_u = std::min(min_u, bound.min_u);
    max_u = std::max(max_u, bound.max_u);
    min_v = std::min(min_v, bound.min_v);
    max_v = std::max(max_v, bound.max_v);
    bounds.push_back(bound);
  }
  const bool sweep_u = widths_u * (max_v - min_v) <= widths_v * (max_u - min_u);
  const auto lower = [&](const TriangleBounds &bound) { return sweep_u ? bound.min_u : bound.min_v; };
  const auto upper = [&](const TriangleBounds &bound) { return sweep_u ? bound.max_u : bound.max_v; };
  std::ranges::sort(bounds, [&](const TriangleBounds &left, const TriangleBounds &right) {
    return lower(left) < lower(right) ||
           (lower(left) == lower(right) && left.triangle < right.triangle);
  });
  std::vector<std::size_t> active;
  active.reserve(bounds.size());
  std::size_t pairs = 0U;
  for (std::size_t index = 0U; index < bounds.size(); ++index) {
    const auto &current = bounds[index];
    std::erase_if(active, [&](std::size_t previous) { return upper(bounds[previous]) <= lower(current); });
    for (const auto previous : active) {
      if (++pairs > kMaximumOverlapPairs) {
        return peel_error(
            "UV Peel overlap validation exceeded its work budget. Select a smaller patch.");
      }
      const auto &other = bounds[previous];
      if (current.max_u <= other.min_u || other.max_u <= current.min_u ||
          current.max_v <= other.min_v || other.max_v <= current.min_v) {
        continue;
      }
      if (triangle_interiors_overlap(triangles[current.triangle],
                                     triangles[other.triangle], sign) &&
          !shared_float_contact(topology[current.triangle], topology[other.triangle],
                                triangles[current.triangle], triangles[other.triangle],
                                coordinates, sign, stored_tolerance)) {
        return peel_error(
            "UV Peel would overlap texture-chart interiors. Adjust the flow guides or select a smaller patch.");
      }
    }
    active.push_back(index);
  }
  return std::nullopt;
}

} // namespace

std::optional<Error> validate_uv_peel_chart(
    const GuidedPeelChart &chart, std::span<const PeelPoint2> coordinates,
    bool guide_direction_is_u,
    std::span<const PeelTriangleCoordinates> triangle_coordinates,
    double stored_coordinate_tolerance) {
  if (auto error = check_size(chart)) {
    return error;
  }
  if (auto error = check_indices(chart)) {
    return error;
  }
  if (coordinates.size() != chart.positions.size()) {
    return peel_error("UV Peel coordinate count does not match its chart.");
  }
  if (!triangle_coordinates.empty() &&
      triangle_coordinates.size() != chart.triangles.size()) {
    return peel_error("UV Peel stored triangle samples do not match its chart.");
  }
  if (!std::isfinite(stored_coordinate_tolerance) || stored_coordinate_tolerance < 0.0) {
    return peel_error("UV Peel stored-coordinate tolerance is invalid.");
  }
  std::vector<PeelPoint2> normalized;
  normalized.reserve(coordinates.size());
  double scale = 0.0;
  const auto origin = coordinates.front();
  for (const auto coordinate : coordinates) {
    const PeelPoint2 point{coordinate.u - origin.u, coordinate.v - origin.v};
    if (!std::isfinite(point.u) || !std::isfinite(point.v)) {
      return peel_error("UV Peel produced non-finite or unrepresentable coordinates.");
    }
    scale = std::max({scale, std::abs(point.u), std::abs(point.v)});
    normalized.push_back(point);
  }
  std::vector<PeelTriangleCoordinates> normalized_triangles;
  normalized_triangles.reserve(chart.triangles.size());
  for (std::size_t index = 0U; index < chart.triangles.size(); ++index) {
    PeelTriangleCoordinates triangle;
    for (std::size_t corner = 0U; corner < 3U; ++corner) {
      auto point = normalized[chart.triangles[index].vertices[corner]];
      if (!triangle_coordinates.empty()) {
        const auto coordinate = triangle_coordinates[index][corner];
        point = {coordinate.u - origin.u, coordinate.v - origin.v};
      }
      if (!std::isfinite(point.u) || !std::isfinite(point.v)) {
        return peel_error("UV Peel stored triangle coordinates are non-finite or unrepresentable.");
      }
      scale = std::max({scale, std::abs(point.u), std::abs(point.v)});
      triangle[corner] = point;
    }
    normalized_triangles.push_back(triangle);
  }
  if (!(scale > 0.0)) {
    return peel_error("UV Peel produced a collapsed chart.");
  }
  for (auto &point : normalized) {
    point.u /= scale;
    point.v /= scale;
  }
  for (auto &triangle : normalized_triangles) {
    for (auto &point : triangle) {
      point.u /= scale;
      point.v /= scale;
    }
  }
  double sign = 0.0;
  for (const auto &triangle : normalized_triangles) {
    const auto first = triangle[0];
    const auto second = triangle[1];
    const auto third = triangle[2];
    const double area = orientation(first, second, third);
    const double longest_edge = std::max({
        std::hypot(second.u - first.u, second.v - first.v),
        std::hypot(third.u - first.u, third.v - first.v),
        std::hypot(third.u - second.u, third.v - second.v)});
    if (!(std::abs(area) > kRelativeAreaTolerance * longest_edge * longest_edge)) {
      return peel_error("UV Peel produced a collapsed or excessively thin texture triangle.");
    }
    if (sign == 0.0) {
      sign = area > 0.0 ? 1.0 : -1.0;
    } else if (area * sign <= 0.0) {
      return peel_error("UV Peel would invert a texture triangle. Adjust the flow guides.");
    }
  }
  const auto flow = [&](PeelPoint2 point) { return guide_direction_is_u ? point.u : point.v; };
  const auto across = [&](PeelPoint2 point) { return guide_direction_is_u ? point.v : point.u; };
  for (const auto &guide : chart.guides) {
    const auto first = normalized[guide.vertices.front()];
    const double delta = flow(normalized[guide.vertices.back()]) - flow(first);
    if (delta == 0.0 || (guide.reference && delta < 0.0)) {
      return peel_error("UV Peel produced a collapsed or reversed reference flow guide.");
    }
    const double direction = delta > 0.0 ? 1.0 : -1.0;
    const double minimum_step = std::abs(delta) * 1.0e-12;
    for (std::size_t index = 0U; index < guide.vertices.size(); ++index) {
      const auto point = normalized[guide.vertices[index]];
      if (std::abs(across(point) - across(first)) > kGuideStraightnessTolerance) {
        return peel_error("UV Peel could not retain a straight flow guide in stored texture coordinates.");
      }
      if (index > 0U &&
          (flow(point) - flow(normalized[guide.vertices[index - 1U]])) * direction <= minimum_step) {
        return peel_error("UV Peel would fold a flow guide back onto itself.");
      }
    }
  }
  const double stored_tolerance =
      triangle_coordinates.empty() ? 0.0 : stored_coordinate_tolerance / scale;
  if (!std::isfinite(stored_tolerance)) {
    return peel_error("UV Peel stored-coordinate tolerance cannot be normalized.");
  }
  return validate_overlap(chart.triangles, normalized, normalized_triangles, sign, stored_tolerance);
}

Result<std::vector<PeelPoint2>> solve_guided_uv_peel(const GuidedPeelChart &chart) {
  if (auto error = check_size(chart)) {
    return Result<std::vector<PeelPoint2>>::failure(std::move(*error));
  }
  if (auto error = check_indices(chart)) {
    return Result<std::vector<PeelPoint2>>::failure(std::move(*error));
  }
  auto geometry = normalize_geometry(chart);
  if (!geometry.ok()) {
    return Result<std::vector<PeelPoint2>>::failure(geometry.error());
  }
  auto system = build_system(chart, geometry.value());
  if (!system.ok()) {
    return Result<std::vector<PeelPoint2>>::failure(system.error());
  }
  auto solution = solve_system(system.value());
  if (!solution.ok()) {
    return Result<std::vector<PeelPoint2>>::failure(solution.error());
  }
  const auto coordinate = [&](CoordinateVariable variable) {
    return variable.column == kNoColumn
        ? variable.pinned
        : solution.value()[variable.column] * system.value().column_scales[variable.column];
  };
  std::vector<PeelPoint2> coordinates;
  coordinates.reserve(chart.positions.size());
  for (std::size_t index = 0U; index < chart.positions.size(); ++index) {
    coordinates.push_back({coordinate(system.value().u[index]) * geometry.value().scale,
                           coordinate(system.value().v[index]) * geometry.value().scale});
  }
  if (auto error = validate_uv_peel_chart(chart, coordinates)) {
    return Result<std::vector<PeelPoint2>>::failure(std::move(*error));
  }
  return Result<std::vector<PeelPoint2>>::success(std::move(coordinates));
}

} // namespace quader::modeling::detail
