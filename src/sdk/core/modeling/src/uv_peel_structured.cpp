////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "uv_peel_structured.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace quader::modeling::detail {

/** Owned request-local topology, exposed to consumers only through const handles. */
struct StructuredPeelTopology {
  /** One authored, directed face edge, independent of vertex storage order. */
  struct FaceEdgeOccurrence {
    std::size_t face_index{};
    std::size_t edge_corner{};

    bool operator==(const FaceEdgeOccurrence &) const = default;
  };

  /** Keep two usable incidences and a saturated count for local ambiguity checks. */
  struct EdgeIncidences {
    std::array<FaceEdgeOccurrence, 2> occurrences{};
    std::size_t count{};
  };

  using VertexPair = std::pair<std::size_t, std::size_t>;
  using EdgeIndex = std::map<VertexPair, EdgeIncidences>;

  std::vector<StructuredPeelFace> faces;
  std::size_t vertex_count{};
  EdgeIndex index;
};

namespace {

using FaceEdgeOccurrence = StructuredPeelTopology::FaceEdgeOccurrence;
using VertexPair = StructuredPeelTopology::VertexPair;
using EdgeIndex = StructuredPeelTopology::EdgeIndex;

/** Signed incident faces of one segment in the selected guide orientation. */
struct GuideSegment {
  std::size_t first_vertex{};
  std::size_t second_vertex{};
  std::array<std::optional<FaceEdgeOccurrence>, 2> sides{};
};

/** A corner event keeps its authored identity even at a shared geometric point. */
struct CornerEvent {
  std::size_t face_index{};
  std::size_t corner{};
  double distance{};
};

/** Independently integrated endpoint polylines on one signed side of the guide. */
struct IntegratedSide {
  std::array<std::vector<CornerEvent>, 2> endpoints;
  std::array<double, 2> lengths{};
};

/** The geometry needed to evaluate both endpoint occurrences of one segment. */
struct SegmentProfile {
  std::array<IntegratedSide, 2> sides;
  std::array<double, 2> along_distances{};
  std::array<double, 2> widths{};
};

/** Resolve overlapping assignments by the specified occurrence priority alone. */
struct CornerAssignment {
  std::array<std::size_t, 4> priority{};
  StructuredPeelUv uv;
};

/** Canonicalize lookup only; endpoint transport never depends on this ordering. */
[[nodiscard]] VertexPair edge_key(std::size_t first, std::size_t second) {
  return first < second ? VertexPair{first, second} : VertexPair{second, first};
}

/** Construct a failure without exposing partially computed coordinates. */
[[nodiscard]] StructuredPeelResult failure(std::string message) {
  StructuredPeelResult result;
  result.error_message = std::move(message);
  return result;
}

/** Measure local differences through the world basis, without a translation. */
[[nodiscard]] double physical_distance(const StructuredPeelInput &input,
                                       std::size_t first,
                                       std::size_t second) {
  const auto &first_position = input.positions[first];
  const auto &second_position = input.positions[second];
  const double dx = second_position.x - first_position.x;
  const double dy = second_position.y - first_position.y;
  const double dz = second_position.z - first_position.z;
  const double x = input.world_basis[0].x * dx +
                   input.world_basis[1].x * dy + input.world_basis[2].x * dz;
  const double y = input.world_basis[0].y * dx +
                   input.world_basis[1].y * dy + input.world_basis[2].y * dz;
  const double z = input.world_basis[0].z * dx +
                   input.world_basis[1].z * dy + input.world_basis[2].z * dz;
  return std::hypot(x, y, z);
}

/** Index all authored uses without rejecting unrelated nonmanifold edges. */
[[nodiscard]] bool index_edges(std::span<const StructuredPeelFace> faces,
                               std::size_t vertex_count, EdgeIndex &index,
                               std::string &error) {
  for (std::size_t face_index = 0U; face_index < faces.size(); ++face_index) {
    const auto &vertices = faces[face_index].vertices;
    if (vertices.size() < 3U) {
      error = "UV Peel requires valid authored face loops.";
      return false;
    }
    for (std::size_t corner = 0U; corner < vertices.size(); ++corner) {
      const std::size_t first = vertices[corner];
      const std::size_t second = vertices[(corner + 1U) % vertices.size()];
      if (first >= vertex_count || second >= vertex_count || first == second) {
        error = "UV Peel encountered invalid authored vertex connectivity.";
        return false;
      }
      auto &incidences = index[edge_key(first, second)];
      if (incidences.count < incidences.occurrences.size()) {
        incidences.occurrences[incidences.count] = {face_index, corner};
      }
      if (incidences.count < 3U) {
        ++incidences.count;
      }
    }
  }
  return true;
}

/** Find each signed incidence and validate the winding of an encountered edge. */
[[nodiscard]] bool incident_sides(
    const StructuredPeelTopology &topology, std::size_t first, std::size_t second,
    std::array<std::optional<FaceEdgeOccurrence>, 2> &sides,
    std::string &error) {
  sides = {};
  const auto found = topology.index.find(edge_key(first, second));
  if (found == topology.index.end()) {
    error = "UV Peel encountered an edge without an authored face.";
    return false;
  }
  const auto &incidences = found->second;
  if (incidences.count > 2U) {
    error = "UV Peel requires manifold authored edges.";
    return false;
  }
  for (std::size_t use = 0U; use < incidences.count; ++use) {
    const auto occurrence = incidences.occurrences[use];
    const auto &vertices = topology.faces[occurrence.face_index].vertices;
    const std::size_t side = vertices[occurrence.edge_corner] == first ? 0U : 1U;
    if (sides[side].has_value()) {
      error = "UV Peel requires consistently oriented shared face edges.";
      return false;
    }
    sides[side] = occurrence;
  }
  return true;
}

/** Follow opposite quad edges while integrating both transported endpoints. */
[[nodiscard]] bool integrate_side(
    const StructuredPeelInput &input, const StructuredPeelTopology &topology,
    FaceEdgeOccurrence initial, std::size_t initial_first_vertex,
    std::size_t initial_second_vertex, std::size_t &remaining_budget,
    IntegratedSide &profile, std::string &error) {
  FaceEdgeOccurrence current = initial;
  std::array<std::size_t, 2> endpoint_vertices{
      initial_first_vertex, initial_second_vertex};
  std::set<std::pair<std::size_t, std::size_t>> visited;
  while (topology.faces[current.face_index].vertices.size() == 4U) {
    const auto occurrence_key =
        std::pair{current.face_index, current.edge_corner};
    if (!visited.insert(occurrence_key).second) {
      if (current == initial && endpoint_vertices[0] == initial_first_vertex &&
          endpoint_vertices[1] == initial_second_vertex) {
        break;
      }
      error = "UV Peel found a transverse walk that repeats before returning to its seed.";
      return false;
    }
    if (remaining_budget < 4U) {
      error = "UV Peel exceeds the supported corner-write budget.";
      return false;
    }
    remaining_budget -= 4U;

    const auto &vertices = topology.faces[current.face_index].vertices;
    const std::size_t edge_corner = current.edge_corner;
    const bool forward = vertices[edge_corner] == endpoint_vertices[0];
    const std::array<std::size_t, 2> near_corners =
        forward ? std::array{edge_corner, (edge_corner + 1U) % 4U}
                : std::array{(edge_corner + 1U) % 4U, edge_corner};
    const std::array<std::size_t, 2> far_corners =
        forward ? std::array{(edge_corner + 3U) % 4U, (edge_corner + 2U) % 4U}
                : std::array{(edge_corner + 2U) % 4U, (edge_corner + 3U) % 4U};
    for (std::size_t endpoint = 0U; endpoint < 2U; ++endpoint) {
      if (vertices[near_corners[endpoint]] != endpoint_vertices[endpoint]) {
        error = "UV Peel could not transport a guide endpoint across a quad.";
        return false;
      }
      const double step = physical_distance(
          input, vertices[near_corners[endpoint]], vertices[far_corners[endpoint]]);
      const double distance = profile.lengths[endpoint] + step;
      if (!std::isfinite(step) || !std::isfinite(distance)) {
        error = "UV Peel found a non-finite transverse distance.";
        return false;
      }
      profile.endpoints[endpoint].push_back(
          {current.face_index, near_corners[endpoint], profile.lengths[endpoint]});
      profile.endpoints[endpoint].push_back(
          {current.face_index, far_corners[endpoint], distance});
      profile.lengths[endpoint] = distance;
      endpoint_vertices[endpoint] = vertices[far_corners[endpoint]];
    }

    const std::size_t opposite_corner = (edge_corner + 2U) % 4U;
    std::array<std::optional<FaceEdgeOccurrence>, 2> exit_sides;
    if (!incident_sides(topology, vertices[opposite_corner],
                        vertices[(opposite_corner + 1U) % 4U], exit_sides, error)) {
      return false;
    }
    if (!exit_sides[1].has_value()) {
      break;
    }
    current = *exit_sides[1];
  }
  return true;
}

/** Closed profiles meet at authored corners, not merely at coincident vertices. */
[[nodiscard]] bool same_corner(const CornerEvent &first,
                               const CornerEvent &second) {
  return first.face_index == second.face_index && first.corner == second.corner;
}

/** Check options used in double-precision chart evaluation. */
[[nodiscard]] bool valid_options(const StructuredPeelOptions &options) {
  return std::isfinite(options.u_repeat) && options.u_repeat > 0.0 &&
         std::isfinite(options.v_repeat) && options.v_repeat > 0.0 &&
         std::isfinite(options.u_offset) && std::isfinite(options.v_offset);
}

} // namespace

StructuredPeelTopologyResult make_structured_peel_topology(
    std::vector<StructuredPeelFace> faces, std::size_t vertex_count) {
  auto topology = std::make_shared<StructuredPeelTopology>();
  topology->faces = std::move(faces);
  topology->vertex_count = vertex_count;
  StructuredPeelTopologyResult result;
  if (!index_edges(topology->faces, vertex_count, topology->index,
                   result.error_message)) {
    return result;
  }
  result.success = true;
  result.topology = std::move(topology);
  return result;
}

StructuredPeelResult compute_structured_uv_peel(
    const StructuredPeelInput &input, std::size_t &remaining_corner_budget) {
  if (!input.topology || input.positions.size() != input.topology->vertex_count) {
    return failure("UV Peel requires topology matching the input position count.");
  }
  const auto &topology = *input.topology;
  if (input.guide.size() < 2U ||
      (input.closed && input.guide.front() != input.guide.back())) {
    return failure("UV Peel requires an ordered guide with valid endpoints.");
  }
  const std::size_t segment_count = input.guide.size() - 1U;
  if (segment_count > input.corner_budget_cap / 4U) {
    return failure("UV Peel exceeds the supported guide-sample budget.");
  }
  std::string error;

  std::vector<GuideSegment> segments;
  segments.reserve(segment_count);
  std::array<std::set<std::size_t>, 2> incident_faces;
  bool has_quad = false;
  for (std::size_t segment_index = 0U; segment_index < segment_count;
       ++segment_index) {
    GuideSegment segment{input.guide[segment_index], input.guide[segment_index + 1U]};
    if (segment.first_vertex >= input.positions.size() ||
        segment.second_vertex >= input.positions.size() ||
        !incident_sides(topology, segment.first_vertex, segment.second_vertex,
                        segment.sides, error)) {
      return failure(error.empty() ? "UV Peel encountered an invalid guide vertex."
                                   : std::move(error));
    }
    for (std::size_t side = 0U; side < 2U; ++side) {
      if (segment.sides[side].has_value()) {
        const std::size_t face_index = segment.sides[side]->face_index;
        if (input.closed) {
          incident_faces[side].insert(face_index);
        }
        has_quad = has_quad || topology.faces[face_index].vertices.size() == 4U;
      }
    }
    segments.push_back(std::move(segment));
  }
  if (!has_quad) {
    StructuredPeelResult result;
    result.success = true;
    result.no_quad_incident_sides = true;
    return result;
  }
  if (!valid_options(input.options)) {
    return failure("UV Peel requires U or V flow, finite positive repetition factors, and finite offsets.");
  }
  if (input.closed && incident_faces[1].size() < incident_faces[0].size()) {
    std::reverse(segments.begin(), segments.end());
    for (auto &segment : segments) {
      std::swap(segment.first_vertex, segment.second_vertex);
      std::swap(segment.sides[0], segment.sides[1]);
    }
  }

  std::size_t available_budget = remaining_corner_budget;
  double total_guide_length = 0.0;
  double maximum_width = 0.0;
  std::vector<SegmentProfile> profiles;
  profiles.reserve(segment_count);
  for (const auto &segment : segments) {
    SegmentProfile profile;
    const double segment_length =
        physical_distance(input, segment.first_vertex, segment.second_vertex);
    if (!std::isfinite(segment_length) || !(segment_length > 0.0)) {
      return failure("UV Peel requires guide edges with finite positive length.");
    }
    profile.along_distances[0] = total_guide_length;
    total_guide_length += segment_length;
    profile.along_distances[1] = total_guide_length;
    if (!std::isfinite(total_guide_length)) {
      return failure("UV Peel requires a nonempty finite guide.");
    }
    for (std::size_t side = 0U; side < 2U; ++side) {
      if (segment.sides[side].has_value() &&
          !integrate_side(input, topology, *segment.sides[side], segment.first_vertex,
                          segment.second_vertex, available_budget,
                          profile.sides[side], error)) {
        return failure(std::move(error));
      }
    }
    for (std::size_t endpoint = 0U; endpoint < 2U; ++endpoint) {
      const auto &positive = profile.sides[0].endpoints[endpoint];
      const auto &negative = profile.sides[1].endpoints[endpoint];
      if (positive.empty() && negative.empty()) {
        continue;
      }
      const bool shared_closed_profile = !positive.empty() && !negative.empty() &&
                                         same_corner(negative.back(), positive.front());
      const double width = profile.sides[0].lengths[endpoint] +
          (shared_closed_profile ? 0.0 : profile.sides[1].lengths[endpoint]);
      if (!std::isfinite(width) || !(width > 0.0)) {
        return failure("UV Peel requires a finite positive transverse span.");
      }
      profile.widths[endpoint] = width;
      maximum_width = std::max(maximum_width, width);
    }
    profiles.push_back(std::move(profile));
  }

  const auto &options = input.options;
  const double along_repeat = options.axis_u ? options.u_repeat : options.v_repeat;
  const double along_offset = options.axis_u ? options.u_offset : options.v_offset;
  const double across_repeat = options.axis_u ? options.v_repeat : options.u_repeat;
  const double across_offset = options.axis_u ? options.v_offset : options.u_offset;
  const double across_scale = options.world_space ? 0.5 * maximum_width : 1.0;
  std::map<std::size_t, std::array<std::optional<CornerAssignment>, 4>> assignments;
  for (std::size_t segment_index = 0U; segment_index < profiles.size();
       ++segment_index) {
    const auto &profile = profiles[segment_index];
    for (std::size_t endpoint = 0U; endpoint < 2U; ++endpoint) {
      const double along_distance = profile.along_distances[endpoint];
      const double along = along_offset + along_repeat *
          (options.world_space ? 0.5 * along_distance
                               : along_distance / total_guide_length);
      for (std::size_t side = 0U; side < 2U; ++side) {
        const auto &events = profile.sides[side].endpoints[endpoint];
        const double sign = side == 0U ? 1.0 : -1.0;
        for (std::size_t occurrence = 0U; occurrence < events.size(); ++occurrence) {
          const auto &event = events[occurrence];
          const double across_distance =
              across_scale * (event.distance / profile.widths[endpoint]);
          const double across = across_offset + sign * across_repeat * across_distance;
          if (!std::isfinite(along) || !std::isfinite(across)) {
            return failure("UV Peel coordinates exceed the supported stored UV range.");
          }
          CornerAssignment candidate;
          candidate.priority = {segment_index, endpoint, side, occurrence};
          candidate.uv = options.axis_u ? StructuredPeelUv{along, across}
                                        : StructuredPeelUv{across, along};
          auto &assigned = assignments[event.face_index][event.corner];
          if (!assigned.has_value() || assigned->priority < candidate.priority) {
            assigned = candidate;
          }
        }
      }
    }
  }

  StructuredPeelResult result;
  result.faces.reserve(assignments.size());
  for (const auto &[face_index, corners] : assignments) {
    StructuredPeelFaceCoordinates face;
    face.face_index = face_index;
    for (std::size_t corner = 0U; corner < corners.size(); ++corner) {
      if (!corners[corner].has_value()) {
        return failure("UV Peel coordinates exceed the supported stored UV range.");
      }
      face.corners[corner] = corners[corner]->uv;
    }
    result.faces.push_back(std::move(face));
  }
  result.success = true;
  remaining_corner_budget = available_budget;
  return result;
}

} // namespace quader::modeling::detail
