////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>

#include <algorithm>
#include <bit>
#include <compare>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <ranges>
#include <span>
#include <utility>
#include <vector>

namespace quader_poly {
namespace {

/// Stores the canonical exact-bit identity of a vertex position.
struct PositionKey {
  std::uint32_t x = 0;
  std::uint32_t y = 0;
  std::uint32_t z = 0;

  friend auto operator<=>(const PositionKey&, const PositionKey&) = default;
};

using FacePositionKey = std::vector<PositionKey>;

/// Stores position and local incidence facts used to partition vertices.
struct VertexKey {
  PositionKey position;
  std::vector<PositionKey> neighboring_positions;
  std::vector<FacePositionKey> incident_faces;

  friend auto operator<=>(const VertexKey&, const VertexKey&) = default;
};

/// Stores order-independent coarse geometry facts before exact matching.
struct GeometryKey {
  std::vector<VertexKey> vertices;
  std::vector<FacePositionKey> faces;

  friend bool operator==(const GeometryKey&, const GeometryKey&) = default;
};

/// Stores indexed topology and canonical facts for bijection matching.
struct ParsedGeometry {
  GeometryKey key;
  std::vector<VertexKey> vertex_keys;
  std::vector<std::vector<std::size_t>> faces;
  std::vector<std::vector<std::size_t>> incident_faces;
};

/// Describes how a vertex pair participates in a shared polygon loop.
struct PairFaceRelation {
  std::size_t face_size = 0;
  std::size_t cyclic_distance = 0;

  friend auto operator<=>(const PairFaceRelation&,
                          const PairFaceRelation&) = default;
};

std::uint32_t exact_float_key(float value)
{
    // Positive and negative zero are the same geometric coordinate.
    return value == 0.0F ? 0U : std::bit_cast<std::uint32_t>(value);
}

PositionKey position_key(quader::QVec3 position)
{
    return {
        exact_float_key(position.x),
        exact_float_key(position.y),
        exact_float_key(position.z),
    };
}

template <typename Value>
std::vector<Value> canonical_face_loop(std::span<const Value> values)
{
    if (values.empty()) {
        return {};
    }

    std::vector<Value> best;
    for (const bool reverse : { false, true }) {
        for (std::size_t start = 0U; start < values.size(); ++start) {
            std::vector<Value> candidate;
            candidate.reserve(values.size());
            for (std::size_t offset = 0U; offset < values.size(); ++offset) {
                const std::size_t index = reverse
                    ? (start + values.size() - offset) % values.size()
                    : (start + offset) % values.size();
                candidate.push_back(values[index]);
            }
            if (best.empty() || std::ranges::lexicographical_compare(
                                    candidate, best)) {
                best = std::move(candidate);
            }
        }
    }
    return best;
}

std::optional<ParsedGeometry> parse_geometry(const Document& document)
{
    ParsedGeometry result;
    result.vertex_keys.resize(document.vertices.size());
    result.incident_faces.resize(document.vertices.size());

    std::map<ElementId, std::size_t> vertex_index_by_id;
    for (std::size_t index = 0U; index < document.vertices.size(); ++index) {
        const Vertex& vertex = document.vertices[index];
        if (vertex.id == kInvalidElementId ||
            !vertex_index_by_id.emplace(vertex.id, index).second) {
            return std::nullopt;
        }
        result.vertex_keys[index].position = position_key(vertex.position);
    }

    std::map<ElementId, bool> face_ids;
    result.faces.reserve(document.faces.size());
    result.key.faces.reserve(document.faces.size());
    for (const Face& face : document.faces) {
        if (face.id == kInvalidElementId || face.vertices.size() < 3U ||
            !face_ids.emplace(face.id, true).second) {
            return std::nullopt;
        }

        std::vector<std::size_t> face_vertices;
        std::vector<PositionKey> face_positions;
        face_vertices.reserve(face.vertices.size());
        face_positions.reserve(face.vertices.size());
        for (const ElementId vertex_id : face.vertices) {
            const auto found = vertex_index_by_id.find(vertex_id);
            if (found == vertex_index_by_id.end() ||
                std::find(face_vertices.begin(), face_vertices.end(),
                          found->second) != face_vertices.end()) {
                return std::nullopt;
            }
            face_vertices.push_back(found->second);
            face_positions.push_back(
                result.vertex_keys[found->second].position);
        }

        const std::size_t face_index = result.faces.size();
        const FacePositionKey face_key = canonical_face_loop<PositionKey>(
            face_positions);
        result.key.faces.push_back(face_key);
        result.faces.push_back(face_vertices);
        for (std::size_t index = 0U; index < face_vertices.size(); ++index) {
            const std::size_t vertex_index = face_vertices[index];
            VertexKey& vertex_key = result.vertex_keys[vertex_index];
            result.incident_faces[vertex_index].push_back(face_index);
            vertex_key.incident_faces.push_back(face_key);
            vertex_key.neighboring_positions.push_back(
                face_positions[(index + face_positions.size() - 1U) %
                               face_positions.size()]);
            vertex_key.neighboring_positions.push_back(
                face_positions[(index + 1U) % face_positions.size()]);
        }
    }

    std::ranges::sort(result.key.faces);
    result.key.vertices = result.vertex_keys;
    for (VertexKey& vertex : result.key.vertices) {
        std::ranges::sort(vertex.neighboring_positions);
        std::ranges::sort(vertex.incident_faces);
    }
    for (VertexKey& vertex : result.vertex_keys) {
        std::ranges::sort(vertex.neighboring_positions);
        std::ranges::sort(vertex.incident_faces);
    }
    std::ranges::sort(result.key.vertices);
    return result;
}

std::vector<PairFaceRelation> pair_face_relations(
    const ParsedGeometry& geometry, std::size_t first, std::size_t second)
{
    std::vector<PairFaceRelation> result;
    for (const std::size_t face_index : geometry.incident_faces[first]) {
        const std::vector<std::size_t>& face = geometry.faces[face_index];
        const auto first_position = std::find(face.begin(), face.end(), first);
        const auto second_position =
            std::find(face.begin(), face.end(), second);
        if (second_position == face.end()) {
            continue;
        }
        const std::size_t first_offset = static_cast<std::size_t>(
            std::distance(face.begin(), first_position));
        const std::size_t second_offset = static_cast<std::size_t>(
            std::distance(face.begin(), second_position));
        const std::size_t forward =
            (second_offset + face.size() - first_offset) % face.size();
        result.push_back({
            .face_size = face.size(),
            .cyclic_distance = std::min(forward, face.size() - forward),
        });
    }
    std::ranges::sort(result);
    return result;
}

bool face_mapping_is_compatible(
    const ParsedGeometry& left, std::size_t left_face_index,
    const ParsedGeometry& right, std::size_t right_face_index,
    std::span<const std::size_t> left_to_right)
{
    const std::vector<std::size_t>& left_face = left.faces[left_face_index];
    const std::vector<std::size_t>& right_face = right.faces[right_face_index];
    if (left_face.size() != right_face.size()) {
        return false;
    }

    for (const bool reverse : { false, true }) {
        for (std::size_t start = 0U; start < right_face.size(); ++start) {
            bool compatible = true;
            for (std::size_t offset = 0U; offset < left_face.size(); ++offset) {
                const std::size_t right_offset = reverse
                    ? (start + right_face.size() - offset) % right_face.size()
                    : (start + offset) % right_face.size();
                const std::size_t left_vertex = left_face[offset];
                const std::size_t right_vertex = right_face[right_offset];
                if (left.vertex_keys[left_vertex].position !=
                        right.vertex_keys[right_vertex].position ||
                    (left_to_right[left_vertex] !=
                         std::numeric_limits<std::size_t>::max() &&
                     left_to_right[left_vertex] != right_vertex)) {
                    compatible = false;
                    break;
                }
            }
            if (compatible) {
                return true;
            }
        }
    }
    return false;
}

bool incident_faces_have_compatible_targets(
    const ParsedGeometry& left, std::size_t left_vertex,
    const ParsedGeometry& right, std::span<const std::size_t> left_to_right)
{
    for (const std::size_t left_face : left.incident_faces[left_vertex]) {
        bool found = false;
        for (std::size_t right_face = 0U; right_face < right.faces.size();
             ++right_face) {
            if (face_mapping_is_compatible(
                    left, left_face, right, right_face, left_to_right)) {
                found = true;
                break;
            }
        }
        if (!found) {
            return false;
        }
    }
    return true;
}

bool complete_face_topology_matches(
    const ParsedGeometry& left, const ParsedGeometry& right,
    std::span<const std::size_t> left_to_right)
{
    std::vector<std::vector<std::size_t>> mapped_left_faces;
    mapped_left_faces.reserve(left.faces.size());
    for (const std::vector<std::size_t>& face : left.faces) {
        std::vector<std::size_t> mapped;
        mapped.reserve(face.size());
        for (const std::size_t vertex : face) {
            mapped.push_back(left_to_right[vertex]);
        }
        mapped_left_faces.push_back(
            canonical_face_loop<std::size_t>(mapped));
    }

    std::vector<std::vector<std::size_t>> right_faces;
    right_faces.reserve(right.faces.size());
    for (const std::vector<std::size_t>& face : right.faces) {
        right_faces.push_back(canonical_face_loop<std::size_t>(face));
    }
    std::ranges::sort(mapped_left_faces);
    std::ranges::sort(right_faces);
    return mapped_left_faces == right_faces;
}

bool has_exact_topology_bijection(const ParsedGeometry& left,
                                  const ParsedGeometry& right)
{
    const std::size_t vertex_count = left.vertex_keys.size();
    constexpr std::size_t kUnmapped =
        std::numeric_limits<std::size_t>::max();
    std::vector<std::vector<std::size_t>> candidates(vertex_count);
    for (std::size_t left_vertex = 0U; left_vertex < vertex_count;
         ++left_vertex) {
        for (std::size_t right_vertex = 0U; right_vertex < vertex_count;
             ++right_vertex) {
            if (left.vertex_keys[left_vertex] ==
                right.vertex_keys[right_vertex]) {
                candidates[left_vertex].push_back(right_vertex);
            }
        }
        if (candidates[left_vertex].empty()) {
            return false;
        }
    }

    std::vector<std::size_t> left_to_right(vertex_count, kUnmapped);
    std::vector<bool> right_used(vertex_count, false);
    std::vector<std::size_t> order;
    order.reserve(vertex_count);
    for (std::size_t left_vertex = 0U; left_vertex < vertex_count;
         ++left_vertex) {
        if (candidates[left_vertex].size() != 1U) {
            order.push_back(left_vertex);
            continue;
        }
        const std::size_t right_vertex = candidates[left_vertex].front();
        if (right_used[right_vertex]) {
            return false;
        }
        left_to_right[left_vertex] = right_vertex;
        right_used[right_vertex] = true;
    }
    if (order.empty()) {
        return complete_face_topology_matches(left, right, left_to_right);
    }

    std::ranges::sort(order, [&](std::size_t first, std::size_t second) {
        if (candidates[first].size() != candidates[second].size()) {
            return candidates[first].size() < candidates[second].size();
        }
        return left.incident_faces[first].size() >
               left.incident_faces[second].size();
    });

    const auto search = [&](const auto& self, std::size_t depth) -> bool {
        if (depth == order.size()) {
            return complete_face_topology_matches(left, right, left_to_right);
        }

        const std::size_t left_vertex = order[depth];
        for (const std::size_t right_vertex : candidates[left_vertex]) {
            if (right_used[right_vertex]) {
                continue;
            }

            bool relations_match = true;
            for (std::size_t mapped_left = 0U; mapped_left < vertex_count;
                 ++mapped_left) {
                if (mapped_left == left_vertex ||
                    left_to_right[mapped_left] == kUnmapped) {
                    continue;
                }
                if (pair_face_relations(left, left_vertex, mapped_left) !=
                    pair_face_relations(right, right_vertex,
                                        left_to_right[mapped_left])) {
                    relations_match = false;
                    break;
                }
            }
            if (!relations_match) {
                continue;
            }

            left_to_right[left_vertex] = right_vertex;
            right_used[right_vertex] = true;
            const bool compatible = incident_faces_have_compatible_targets(
                left, left_vertex, right, left_to_right);
            if (compatible && self(self, depth + 1U)) {
                return true;
            }
            right_used[right_vertex] = false;
            left_to_right[left_vertex] = kUnmapped;
        }
        return false;
    };
    return search(search, 0U);
}

} // namespace

bool has_same_geometry(const Document& left, const Document& right)
{
    if (left.vertices.size() != right.vertices.size() ||
        left.faces.size() != right.faces.size()) {
        return false;
    }
    const std::optional<ParsedGeometry> left_geometry = parse_geometry(left);
    const std::optional<ParsedGeometry> right_geometry = parse_geometry(right);
    return left_geometry.has_value() && right_geometry.has_value() &&
           left_geometry->key == right_geometry->key &&
           has_exact_topology_bijection(*left_geometry, *right_geometry);
}

} // namespace quader_poly
