////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_constants.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <set>
#include <span>
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
    const Document& document,
    const std::vector<ElementId>& selected_vertices,
    float tolerance)
{
    std::vector<DistanceMergeVertex> vertices;
    vertices.reserve(selected_vertices.size());
    for (std::size_t index = 0; index < selected_vertices.size(); ++index) {
        const Vertex* vertex = find_vertex(document, selected_vertices[index]);
        if (vertex == nullptr) {
            continue;
        }
        vertices.push_back({ vertex->id, vertex->position, index });
    }

    std::vector<bool> assigned(vertices.size(), false);
    std::vector<DistanceMergeCluster> clusters;
    const float tolerance_squared = tolerance * tolerance;
    const float boundary_epsilon =
        std::max(kEpsilon, tolerance_squared * 0.00001F);
    for (std::size_t seed_index = 0; seed_index < vertices.size(); ++seed_index) {
        if (assigned[seed_index]) {
            continue;
        }

        std::vector<DistanceMergeVertex> cluster;
        cluster.push_back(vertices[seed_index]);
        for (std::size_t candidate_index = seed_index + 1U; candidate_index < vertices.size(); ++candidate_index) {
            if (assigned[candidate_index]) {
                continue;
            }
            const float distance_squared = length_squared(vertices[candidate_index].position - vertices[seed_index].position);
            if (distance_squared <= tolerance_squared + boundary_epsilon) {
                cluster.push_back(vertices[candidate_index]);
            }
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
    return {false, "Merge by Distance needs vertex selection mode."};
  }
    if (!std::isfinite(tolerance_) || tolerance_ < 0.0F) {
        return { false, "Merge by Distance needs a finite non-negative tolerance." };
    }

    const std::vector<ElementId> selected_vertices = selected_valid_vertices(document, selection);
    if (selected_vertices.size() < 2U) {
        return { false, "Select at least two vertices to merge by distance." };
    }

    const std::vector<DistanceMergeCluster> clusters = distance_merge_clusters(document, selected_vertices, tolerance_);
    if (clusters.empty()) {
        return { false, "No selected vertices are within the merge distance." };
    }

    Document candidate = document;
    std::vector<ElementId> selected_after_merge = selected_vertices;
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
            build_vertex_merge_candidate(candidate, next_candidate, merge_vertex_ids, cluster.survivor_id, cluster.centroid);
        if (!merge_result.changed) {
            return merge_result;
        }
        if (find_vertex(next_candidate, cluster.survivor_id) == nullptr) {
            return { false, "Merge by Distance would remove a survivor vertex." };
        }

        candidate = std::move(next_candidate);
        for (const ElementId removed_vertex_id : merge_vertex_ids) {
            std::erase(selected_after_merge, removed_vertex_id);
        }
        add_unique_id(survivor_ids, cluster.survivor_id);
    }

    prune_unused_vertices(candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Merge by Distance would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Vertex;
    for (const ElementId vertex_id : selected_after_merge) {
        if (find_vertex(document, vertex_id) != nullptr) {
            add_unique_id(selection.vertices, vertex_id);
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
