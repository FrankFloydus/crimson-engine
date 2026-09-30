////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/poly_operation.hpp>
#include <quader/modeling/neutral/polygon/poly_operation_descriptor.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_constants.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <set>
#include <span>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader_poly {

using namespace document_internal;

namespace {

/**
 * Implements the Merge Vertices By Distance Operation modeling operation for the polygon document and mesh editing core.
 */
class MergeVerticesByDistanceOperation final : public PolyOperation {
public:
    explicit MergeVerticesByDistanceOperation(float tolerance);

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::MergeSelectedVerticesByDistance).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::MergeSelectedVerticesByDistance).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    float tolerance_ = 0.0F;
};

/**
 * Represents a Distance Merge Vertex value used by the polygon document and mesh editing core.
 */
struct DistanceMergeVertex {
  ElementId id = kInvalidElementId;
  quader::QVec3 position;
  std::size_t input_index = 0;
};

/**
 * Represents a Distance Merge Cluster value used by the polygon document and mesh editing core.
 */
struct DistanceMergeCluster {
    std::vector<DistanceMergeVertex> vertices;
    ElementId survivor_id = kInvalidElementId;
    quader::QVec3 centroid;
};

float distance_axis_coordinate(quader::QVec3 position, std::uint8_t axis)
{
    return axis == 0U ? position.x : (axis == 1U ? position.y : position.z);
}

struct DistanceRadiusNode {
    std::size_t vertex_index = 0;
    std::uint8_t axis = 0;
};

/// Indexes the immutable source positions. Query order is deliberately separate
/// from merge order: callers restore input order before checking assignment.
class DistanceRadiusIndex {
public:
    explicit DistanceRadiusIndex(std::span<const DistanceMergeVertex> vertices)
        : vertices_(vertices)
    {
        nodes_.reserve(vertices.size());
        for (std::size_t index = 0; index < vertices.size(); ++index) {
            nodes_.push_back({index, 0});
        }
        build_range(0, nodes_.size());
    }

    void find_within_radius(quader::QVec3 position, float distance_limit_squared,
                            std::vector<std::size_t>& candidates) const
    {
        candidates.clear();
        query_range(0, nodes_.size(), position, distance_limit_squared, candidates);
    }

private:
    void build_range(std::size_t begin, std::size_t end)
    {
        if (begin == end) {
            return;
        }

        quader::QVec3 minimum = vertices_[nodes_[begin].vertex_index].position;
        quader::QVec3 maximum = minimum;
        for (std::size_t index = begin + 1U; index < end; ++index) {
            const quader::QVec3 position = vertices_[nodes_[index].vertex_index].position;
            minimum.x = std::min(minimum.x, position.x);
            minimum.y = std::min(minimum.y, position.y);
            minimum.z = std::min(minimum.z, position.z);
            maximum.x = std::max(maximum.x, position.x);
            maximum.y = std::max(maximum.y, position.y);
            maximum.z = std::max(maximum.z, position.z);
        }

        std::uint8_t axis = 0;
        double widest_extent = static_cast<double>(maximum.x) - minimum.x;
        for (std::uint8_t candidate_axis = 1; candidate_axis < 3; ++candidate_axis) {
            const double extent =
                static_cast<double>(distance_axis_coordinate(maximum, candidate_axis)) -
                distance_axis_coordinate(minimum, candidate_axis);
            if (extent > widest_extent) {
                axis = candidate_axis;
                widest_extent = extent;
            }
        }

        const std::size_t middle = begin + (end - begin) / 2U;
        std::nth_element(
            nodes_.begin() + static_cast<std::ptrdiff_t>(begin),
            nodes_.begin() + static_cast<std::ptrdiff_t>(middle),
            nodes_.begin() + static_cast<std::ptrdiff_t>(end),
            [&](const DistanceRadiusNode& left, const DistanceRadiusNode& right) {
                const float left_coordinate =
                    distance_axis_coordinate(vertices_[left.vertex_index].position, axis);
                const float right_coordinate =
                    distance_axis_coordinate(vertices_[right.vertex_index].position, axis);
                return left_coordinate < right_coordinate ||
                    (left_coordinate == right_coordinate && left.vertex_index < right.vertex_index);
            });
        nodes_[middle].axis = axis;
        build_range(begin, middle);
        build_range(middle + 1U, end);
    }

    void query_range(std::size_t begin, std::size_t end,
                     quader::QVec3 position, float distance_limit_squared,
                     std::vector<std::size_t>& candidates) const
    {
        if (begin == end) {
            return;
        }
        const std::size_t middle = begin + (end - begin) / 2U;
        const DistanceRadiusNode& node = nodes_[middle];
        const quader::QVec3 candidate_position = vertices_[node.vertex_index].position;
        if (length_squared(candidate_position - position) <= distance_limit_squared) {
            candidates.push_back(node.vertex_index);
        }

        const float axis_delta = distance_axis_coordinate(position, node.axis) -
            distance_axis_coordinate(candidate_position, node.axis);
        if (axis_delta <= 0.0F) {
            query_range(begin, middle, position, distance_limit_squared, candidates);
            if (axis_delta * axis_delta <= distance_limit_squared) {
                query_range(middle + 1U, end, position, distance_limit_squared, candidates);
            }
        } else {
            query_range(middle + 1U, end, position, distance_limit_squared, candidates);
            if (axis_delta * axis_delta <= distance_limit_squared) {
                query_range(begin, middle, position, distance_limit_squared, candidates);
            }
        }
    }

    std::span<const DistanceMergeVertex> vertices_;
    std::vector<DistanceRadiusNode> nodes_;
};

std::vector<DistanceMergeVertex> selected_distance_vertices(
    const Document& document, const Selection& selection)
{
    std::unordered_map<ElementId, const Vertex*> available_vertices;
    available_vertices.reserve(document.vertices.size());
    for (const Vertex& vertex : document.vertices) {
        available_vertices.emplace(vertex.id, &vertex);
    }

    std::vector<DistanceMergeVertex> vertices;
    vertices.reserve(std::min(selection.vertices.size(), document.vertices.size()));
    for (const ElementId vertex_id : selection.vertices) {
        const auto found = available_vertices.find(vertex_id);
        if (vertex_id == kInvalidElementId || found == available_vertices.end()) {
            continue;
        }
        vertices.push_back({vertex_id, found->second->position, vertices.size()});
        available_vertices.erase(found);
    }
    return vertices;
}

quader::QVec3 centroid_for_distance_cluster(std::span<const DistanceMergeVertex> vertices)
{
    quader::QVec3 centroid;
    for (const DistanceMergeVertex& vertex : vertices) {
        centroid += vertex.position;
    }
    return vertices.empty() ? centroid : centroid / static_cast<float>(vertices.size());
}

ElementId survivor_for_distance_cluster(std::span<const DistanceMergeVertex> vertices, quader::QVec3 centroid)
{
    if (vertices.empty()) {
      return kInvalidElementId;
    }
    if (vertices.size() == 2U) {
        return vertices[0].id;
    }

    const DistanceMergeVertex* best = &vertices[0];
    float best_distance = length_squared(vertices[0].position - centroid);
    for (const DistanceMergeVertex& vertex : vertices) {
        const float distance = length_squared(vertex.position - centroid);
        if (distance + kEpsilon < best_distance ||
            (std::abs(distance - best_distance) <= kEpsilon &&
             vertex.input_index < best->input_index)) {
          best = &vertex;
          best_distance = distance;
        }
    }
    return best->id;
}

std::vector<DistanceMergeCluster> distance_merge_clusters(
    std::span<const DistanceMergeVertex> vertices,
    float tolerance)
{
    const DistanceRadiusIndex radius_index(vertices);
    std::vector<bool> assigned(vertices.size(), false);
    std::vector<DistanceMergeCluster> clusters;
    std::vector<std::size_t> candidates;
    candidates.reserve(vertices.size());
    std::vector<DistanceMergeVertex> cluster;
    const float tolerance_squared = tolerance * tolerance;
    const float boundary_epsilon =
        std::max(kEpsilon, tolerance_squared * 0.00001F);
    const float distance_limit_squared = tolerance_squared + boundary_epsilon;
    for (std::size_t seed_index = 0; seed_index < vertices.size(); ++seed_index) {
        if (assigned[seed_index]) {
            continue;
        }

        radius_index.find_within_radius(vertices[seed_index].position,
                                       distance_limit_squared, candidates);
        std::sort(candidates.begin(), candidates.end());
        cluster.clear();
        cluster.push_back(vertices[seed_index]);
        for (const std::size_t candidate_index : candidates) {
            if (candidate_index <= seed_index || assigned[candidate_index]) {
                continue;
            }
            cluster.push_back(vertices[candidate_index]);
        }

        assigned[seed_index] = true;
        if (cluster.size() < 2U) {
            continue;
        }
        for (const DistanceMergeVertex& vertex : cluster) {
            if (vertex.input_index < assigned.size()) {
                assigned[vertex.input_index] = true;
            }
        }

        DistanceMergeCluster merge_cluster;
        merge_cluster.vertices = std::move(cluster);
        merge_cluster.centroid = centroid_for_distance_cluster(merge_cluster.vertices);
        merge_cluster.survivor_id = survivor_for_distance_cluster(merge_cluster.vertices, merge_cluster.centroid);
        if (merge_cluster.survivor_id != kInvalidElementId) {
          clusters.push_back(std::move(merge_cluster));
        }
    }
    return clusters;
}

MergeVerticesByDistanceOperation::MergeVerticesByDistanceOperation(float tolerance)
    : tolerance_(tolerance)
{
}

OperationResult MergeVerticesByDistanceOperation::apply(Document& document, Selection& selection) const
{
  if (selection.mode != SelectionMode::Vertex) {
    return OperationResult::failure("Merge by Distance needs vertex selection mode.");
  }
    if (!std::isfinite(tolerance_) || tolerance_ < 0.0F) {
        return OperationResult::failure("Merge by Distance needs a finite non-negative tolerance.");
    }
    const OperationResult source_validation =
        validate_authored_operation_candidate(document, document, true, true);
    if (!source_validation.success) {
        return source_validation;
    }

    const std::vector<DistanceMergeVertex> selected_vertices =
        selected_distance_vertices(document, selection);
    if (selected_vertices.size() < 2U) {
        return OperationResult::failure("Select at least two vertices to merge by distance.");
    }

    const std::vector<DistanceMergeCluster> clusters = distance_merge_clusters(selected_vertices, tolerance_);
    if (clusters.empty()) {
        return { false, "No selected vertices are within the merge distance." };
    }

    Document candidate = document;
    std::vector<ElementId> survivor_ids;
    survivor_ids.reserve(clusters.size());
    for (const DistanceMergeCluster& cluster : clusters) {
        std::set<ElementId> merge_vertex_ids;
        for (const DistanceMergeVertex& vertex : cluster.vertices) {
            if (vertex.id != cluster.survivor_id) {
                merge_vertex_ids.insert(vertex.id);
            }
        }
        if (merge_vertex_ids.empty()) {
            continue;
        }

        Document next_candidate;
        const OperationResult merge_result =
            build_topology_vertex_merge_candidate(
                candidate, next_candidate, merge_vertex_ids,
                cluster.survivor_id, cluster.centroid, true);
        if (!merge_result.changed) {
            return merge_result;
        }
        if (find_vertex(next_candidate, cluster.survivor_id) == nullptr) {
            return OperationResult::failure("Merge by Distance would remove a survivor vertex.");
        }

        candidate = std::move(next_candidate);
        survivor_ids.push_back(cluster.survivor_id);
    }

    const OperationResult validation =
        validate_authored_operation_candidate(document, candidate, true, true);
    if (!validation.success) {
        return validation;
    }

    document = std::move(candidate);
    std::unordered_set<ElementId> live_vertex_ids;
    live_vertex_ids.reserve(document.vertices.size());
    for (const Vertex& vertex : document.vertices) {
        live_vertex_ids.insert(vertex.id);
    }
    selection.clear();
    selection.mode = SelectionMode::Vertex;
    for (const DistanceMergeVertex& vertex : selected_vertices) {
        if (live_vertex_ids.contains(vertex.id)) {
            selection.vertices.push_back(vertex.id);
        }
    }
    const ElementId active_id =
        !survivor_ids.empty()
            ? survivor_ids.front()
            : (selection.vertices.empty() ? kInvalidElementId
                                          : selection.vertices.front());
    if (active_id != kInvalidElementId) {
      activate_vertex_selection(selection, active_id);
    }

    return { true, {} };
}

} // namespace

OperationResult merge_selected_vertices_by_distance(Document& document, Selection& selection, float tolerance)
{
    return MergeVerticesByDistanceOperation(tolerance).apply(document, selection);
}

} // namespace quader_poly
