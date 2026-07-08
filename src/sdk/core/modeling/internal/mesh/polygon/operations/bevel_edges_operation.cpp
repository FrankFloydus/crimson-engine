////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <diagnostics/profile.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_backend.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <array>
#include <cmath>

#include <string_view>

namespace quader_poly {

using namespace document_internal;

namespace {

OperationResult bevel_selected_edges_impl(Document& document, Selection& selection, const EdgeBevelSettings& requested_settings);

/**
 * Implements the Bevel Edges Operation modeling operation for the polygon document and mesh editing core.
 */
class BevelEdgesOperation final : public PolyOperation {
public:
    explicit BevelEdgesOperation(EdgeBevelSettings settings = {});

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::BevelSelectedEdges).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::BevelSelectedEdges).label; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    EdgeBevelSettings settings_;
};

/**
 * Represents a Bevel Face Edge Use value used by the polygon document and mesh editing core.
 */
struct BevelFaceEdgeUse {
    std::size_t face_index = 0;
    ElementId from = kInvalidElementId;
    ElementId to = kInvalidElementId;
};

void orient_generated_faces_from_source_adjacency(const Document& source, Document& candidate)
{
    std::map<std::pair<ElementId, ElementId>, std::vector<BevelFaceEdgeUse>> uses_by_edge;
    for (std::size_t face_index = 0; face_index < candidate.faces.size(); ++face_index) {
        const Face& face = candidate.faces[face_index];
        if (face.vertices.size() < 2U) {
            continue;
        }
        for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
            const ElementId from = face.vertices[vertex_index];
            const ElementId to = face.vertices[(vertex_index + 1U) % face.vertices.size()];
            uses_by_edge[edge_key(make_edge(from, to))].push_back({ face_index, from, to });
        }
    }

    std::vector<bool> oriented(candidate.faces.size(), false);
    std::vector<bool> flip(candidate.faces.size(), false);
    std::vector<std::size_t> stack;
    stack.reserve(candidate.faces.size());
    for (std::size_t face_index = 0; face_index < candidate.faces.size(); ++face_index) {
        if (find_face(source, candidate.faces[face_index].id) == nullptr) {
            continue;
        }
        oriented[face_index] = true;
        stack.push_back(face_index);
    }
    if (stack.empty() && !candidate.faces.empty()) {
        oriented[0] = true;
        stack.push_back(0);
    }

    auto oriented_from = [&flip](const BevelFaceEdgeUse& use) {
        return flip[use.face_index] ? use.to : use.from;
    };
    auto oriented_to = [&flip](const BevelFaceEdgeUse& use) {
        return flip[use.face_index] ? use.from : use.to;
    };

    std::size_t stack_index = 0;
    const auto propagate_orientation = [&]() {
        while (stack_index < stack.size()) {
            const std::size_t face_index = stack[stack_index++];
            const Face& face = candidate.faces[face_index];
            for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
                const BevelFaceEdgeUse current {
                    face_index,
                    face.vertices[vertex_index],
                    face.vertices[(vertex_index + 1U) % face.vertices.size()],
                };
                const auto uses = uses_by_edge.find(edge_key(make_edge(current.from, current.to)));
                if (uses == uses_by_edge.end()) {
                    continue;
                }

                const ElementId current_from = oriented_from(current);
                const ElementId current_to = oriented_to(current);
                for (const BevelFaceEdgeUse& neighbor : uses->second) {
                    if (neighbor.face_index == face_index) {
                        continue;
                    }

                    const bool neighbor_should_flip =
                        !(neighbor.from == current_to && neighbor.to == current_from);
                    if (oriented[neighbor.face_index]) {
                        continue;
                    }
                    flip[neighbor.face_index] = neighbor_should_flip;
                    oriented[neighbor.face_index] = true;
                    stack.push_back(neighbor.face_index);
                }
            }
        }
    };
    propagate_orientation();
    for (std::size_t face_index = 0; face_index < candidate.faces.size(); ++face_index) {
        if (oriented[face_index]) {
            continue;
        }
        oriented[face_index] = true;
        stack.push_back(face_index);
        propagate_orientation();
    }

    for (std::size_t face_index = 0; face_index < candidate.faces.size(); ++face_index) {
        if (!flip[face_index]) {
            continue;
        }
        reverse_face_winding(candidate.faces[face_index]);
    }
}


OperationResult bevel_selected_edges_impl(Document& document, Selection& selection, const EdgeBevelSettings& requested_settings)
{
    QDR_PROFILE_SCOPE("qdr_document.bevel_selected_edges_impl");
    if (selection.mode != SelectionMode::Edge || selection.edges.empty()) {
      return {false, {}};
    }

    const std::vector<Edge> selected_edges = selected_valid_edges(document, selection);
    if (selected_edges.empty()) {
        return { false, {} };
    }

    const EdgeBevelSettings settings = sanitized_edge_bevel_settings(requested_settings);
    const std::map<std::pair<ElementId, ElementId>, int> incidence_counts = edge_incidence_counts(document);
    std::set<std::pair<ElementId, ElementId>> selected_edge_keys;
    std::map<ElementId, int> selected_edge_counts;
    std::map<ElementId, std::vector<ElementId>> selected_neighbors_by_vertex;
    for (const Edge& edge : selected_edges) {
        const auto count = incidence_counts.find(edge_key(edge));
        if (count == incidence_counts.end() || count->second != 2) {
            continue;
        }
        selected_edge_keys.insert(edge_key(edge));
        ++selected_edge_counts[edge.a];
        ++selected_edge_counts[edge.b];
        selected_neighbors_by_vertex[edge.a].push_back(edge.b);
        selected_neighbors_by_vertex[edge.b].push_back(edge.a);
    }
    if (selected_edge_keys.empty()) {
        return { false, {} };
    }

    const std::vector<Edge> all_document_edges = document_edges(document);
    std::map<ElementId, bool> fully_selected_vertices;
    for (const Vertex& vertex : document.vertices) {
        bool has_incident_edge = false;
        bool all_incident_edges_selected = true;
        for (const Edge& edge : all_document_edges) {
            if (edge.a != vertex.id && edge.b != vertex.id) {
                continue;
            }
            has_incident_edge = true;
            if (!selected_edge_keys.contains(edge_key(edge))) {
                all_incident_edges_selected = false;
                break;
            }
        }
        fully_selected_vertices[vertex.id] = has_incident_edge && all_incident_edges_selected;
    }

    Document candidate = document;
    std::vector<EdgeBevelBuild> builds;
    builds.reserve(selected_edges.size());
    std::map<std::tuple<ElementId, ElementId, ElementId>, std::pair<std::size_t, int>> side_by_face_edge;
    std::map<ElementId, std::vector<EdgeBevelFaceVertexOffset>> offsets_by_vertex;
    std::map<std::pair<ElementId, ElementId>, std::pair<ElementId, quader::QVec3>> miter_vertices;
    std::map<ElementId, std::vector<std::pair<ElementId, quader::QVec3>>> profile_vertices_by_source;
    const auto profile_points_are_collinear = [](quader::QVec3 start, quader::QVec3 middle, quader::QVec3 end) {
        const quader::QVec3 first = middle - start;
        const quader::QVec3 second = middle - end;
        const float scale = length_squared(first) + length_squared(second);
        if (scale <= kEpsilon) {
          return true;
        }
        return length_squared(cross(first, second)) <= (scale * scale * 0.000001F);
    };

    const auto selected_neighbor_profile_line_for_side =
        [&](const EdgeBevelBuild& build, int side_index, ElementId endpoint_id) -> std::optional<EdgeBevelOffsetLine> {
        if (side_index < 0 || side_index > 1) {
            return std::nullopt;
        }
        const EdgeBevelSide& side = build.sides[static_cast<std::size_t>(side_index)];
        const Face* face = find_face(document, side.face_id);
        if (face == nullptr || face->vertices.size() < 3U) {
            return std::nullopt;
        }

        const auto vertex = std::ranges::find(face->vertices, endpoint_id);
        if (vertex == face->vertices.end()) {
            return std::nullopt;
        }

        const std::size_t vertex_index = static_cast<std::size_t>(std::distance(face->vertices.begin(), vertex));
        const std::size_t previous_index = (vertex_index + face->vertices.size() - 1U) % face->vertices.size();
        const std::array<std::size_t, 2> candidate_indices { previous_index, vertex_index };
        const std::pair current_key = edge_key(build.edge);
        for (const std::size_t edge_index : candidate_indices) {
            const Edge candidate_edge = make_edge(
                face->vertices[edge_index],
                face->vertices[(edge_index + 1U) % face->vertices.size()]);
            const std::pair candidate_key = edge_key(candidate_edge);
            if (candidate_key == current_key || !selected_edge_keys.contains(candidate_key)) {
                continue;
            }
            const Vertex* from = find_vertex(document, candidate_edge.a);
            const Vertex* to = find_vertex(document, candidate_edge.b);
            if (from == nullptr || to == nullptr) {
                continue;
            }
            const quader::QVec3 direction = normalize_or_zero(to->position - from->position);
            if (length_squared(direction) <= kEpsilon) {
              continue;
            }
            return EdgeBevelOffsetLine {
                edge_bevel_side_endpoint_position(side, endpoint_id),
                direction,
            };
        }
        return std::nullopt;
    };

    const auto profile_middle_for_endpoint = [&](const EdgeBevelBuild& build, ElementId endpoint_id) {
        const Vertex* source_a = find_vertex(document, build.edge.a);
        const Vertex* source_b = find_vertex(document, build.edge.b);
        if (source_a == nullptr || source_b == nullptr) {
            return quader::QVec3 {};
        }

        const bool endpoint_is_a = endpoint_id == build.edge.a;
        const Vertex* source_endpoint = endpoint_is_a ? source_a : source_b;
        const quader::QVec3 side0 = edge_bevel_side_endpoint_position(build.sides[0], endpoint_id);
        const quader::QVec3 side1 = edge_bevel_side_endpoint_position(build.sides[1], endpoint_id);
        const int selected_count = selected_edge_counts.contains(endpoint_id) ? selected_edge_counts.at(endpoint_id) : 0;
        const bool fully_selected = fully_selected_vertices.contains(endpoint_id) && fully_selected_vertices.at(endpoint_id);
        if (selected_count == 2 && !fully_selected) {
            return source_endpoint->position;
        }
        quader::QVec3 middle = endpoint_is_a ?
            edge_bevel_profile_middle_on_edge(source_a->position, source_b->position, side0, side1) :
            edge_bevel_profile_middle_on_edge(source_b->position, source_a->position, side0, side1);

        if (selected_count >= 3 && profile_points_are_collinear(side0, middle, side1)) {
            const std::optional<EdgeBevelOffsetLine> first_line =
                selected_neighbor_profile_line_for_side(build, 0, endpoint_id);
            const std::optional<EdgeBevelOffsetLine> second_line =
                selected_neighbor_profile_line_for_side(build, 1, endpoint_id);
            if (first_line.has_value() && second_line.has_value()) {
                if (const std::optional<quader::QVec3> meet_position =
                        edge_bevel_intersect_offset_lines(*first_line, *second_line)) {
                    middle = *meet_position;
                }
            }
        }
        return middle;
    };

    const auto rebuild_edge_bevel_boundary_state = [&]() {
        side_by_face_edge.clear();
        offsets_by_vertex.clear();
        profile_vertices_by_source.clear();

        for (std::size_t build_index = 0; build_index < builds.size(); ++build_index) {
            EdgeBevelBuild& build = builds[build_index];
            for (int side_index = 0; side_index < 2; ++side_index) {
                EdgeBevelSide& side = build.sides[static_cast<std::size_t>(side_index)];
                for (const ElementId endpoint_id : { build.edge.a, build.edge.b }) {
                    ElementId offset_vertex_id = edge_bevel_side_endpoint_vertex(side, endpoint_id);
                    const quader::QVec3 offset_position = edge_bevel_side_endpoint_position(side, endpoint_id);
                    auto& vertex_offsets = offsets_by_vertex[endpoint_id];
                    const auto reusable = std::ranges::find_if(vertex_offsets, [offset_position](const EdgeBevelFaceVertexOffset& existing) {
                        return length_squared(existing.position - offset_position) <= 0.000001F;
                    });
                    if (reusable != vertex_offsets.end()) {
                        offset_vertex_id = reusable->offset_vertex_id;
                        side.endpoint_vertices[endpoint_id] = offset_vertex_id;
                    } else {
                        vertex_offsets.push_back({
                            endpoint_id,
                            offset_vertex_id,
                            side.face_id,
                            offset_position,
                        });
                    }
                }
            }

            const Vertex* source_a = find_vertex(document, build.edge.a);
            const Vertex* source_b = find_vertex(document, build.edge.b);
            if (source_a == nullptr || source_b == nullptr) {
                continue;
            }

            build.rows.resize(static_cast<std::size_t>(settings.segments + 1));
            for (int segment_index = 0; segment_index <= settings.segments; ++segment_index) {
                if (segment_index == 0) {
                    build.rows[static_cast<std::size_t>(segment_index)] = {
                        edge_bevel_side_endpoint_vertex(build.sides[0], build.edge.a),
                        edge_bevel_side_endpoint_vertex(build.sides[0], build.edge.b),
                    };
                    continue;
                }
                if (segment_index == settings.segments) {
                    build.rows[static_cast<std::size_t>(segment_index)] = {
                        edge_bevel_side_endpoint_vertex(build.sides[1], build.edge.a),
                        edge_bevel_side_endpoint_vertex(build.sides[1], build.edge.b),
                    };
                    continue;
                }

                const quader::QVec3 side0_a = edge_bevel_side_endpoint_position(build.sides[0], build.edge.a);
                const quader::QVec3 side0_b = edge_bevel_side_endpoint_position(build.sides[0], build.edge.b);
                const quader::QVec3 side1_a = edge_bevel_side_endpoint_position(build.sides[1], build.edge.a);
                const quader::QVec3 side1_b = edge_bevel_side_endpoint_position(build.sides[1], build.edge.b);
                const quader::QVec3 middle_a = profile_middle_for_endpoint(build, build.edge.a);
                const quader::QVec3 middle_b = profile_middle_for_endpoint(build, build.edge.b);
                const quader::QVec3 profiled_a = edge_bevel_profile_point(
                    middle_a,
                    side0_a,
                    side1_a,
                    segment_index,
                    settings.segments,
                    settings.profile,
                    settings.profile_type);
                const quader::QVec3 profiled_b = edge_bevel_profile_point(
                    middle_b,
                    side0_b,
                    side1_b,
                    segment_index,
                    settings.segments,
                    settings.profile,
                    settings.profile_type);
                build.rows[static_cast<std::size_t>(segment_index)] = {
                    edge_bevel_profile_vertex_for_source(candidate, build.edge.a, profiled_a, profile_vertices_by_source),
                    edge_bevel_profile_vertex_for_source(candidate, build.edge.b, profiled_b, profile_vertices_by_source),
                };
            }

            for (int side_index = 0; side_index < 2; ++side_index) {
                side_by_face_edge.emplace(
                    edge_bevel_side_key(build.sides[static_cast<std::size_t>(side_index)].face_id, build.edge),
                    std::pair { build_index, side_index });
            }
        }
    };

    for (const Edge& selected_edge : selected_edges) {
        if (!selected_edge_keys.contains(edge_key(selected_edge))) {
            continue;
        }
        const std::vector<std::size_t> adjacent_faces = adjacent_face_indices_for_edge(document, selected_edge);
        if (adjacent_faces.size() != 2U) {
            continue;
        }

        EdgeBevelBuild build;
        build.edge = make_edge(selected_edge.a, selected_edge.b);
        build.concave = edge_bevel_edge_is_concave(document, build.edge, adjacent_faces);
        bool side_failed = false;
        for (std::size_t side_index = 0; side_index < adjacent_faces.size(); ++side_index) {
            std::optional<EdgeBevelSide> side =
                edge_bevel_side_for_face(candidate, document, build.edge, adjacent_faces[side_index], settings.width);
            if (!side.has_value()) {
                side_failed = true;
                break;
            }
            EdgeBevelSide bevel_side = std::move(*side);
            const Face& source_face = document.faces[adjacent_faces[side_index]];
            for (const ElementId endpoint_id : { build.edge.a, build.edge.b }) {
                if (std::optional<std::pair<ElementId, quader::QVec3>> miter =
                        edge_bevel_face_vertex_miter(
                            candidate,
                            document,
                            source_face,
                            endpoint_id,
                            settings.width,
                            selected_edge_keys,
                            miter_vertices)) {
                    bevel_side.endpoint_vertices[endpoint_id] = miter->first;
                    bevel_side.endpoint_positions[endpoint_id] = miter->second;
                }

                ElementId offset_vertex_id = edge_bevel_side_endpoint_vertex(bevel_side, endpoint_id);
                const quader::QVec3 offset_position = edge_bevel_side_endpoint_position(bevel_side, endpoint_id);
                auto& vertex_offsets = offsets_by_vertex[endpoint_id];
                const auto reusable = std::ranges::find_if(vertex_offsets, [offset_position](const EdgeBevelFaceVertexOffset& existing) {
                    return length_squared(existing.position - offset_position) <= 0.000001F;
                });
                if (reusable != vertex_offsets.end()) {
                    offset_vertex_id = reusable->offset_vertex_id;
                    bevel_side.endpoint_vertices[endpoint_id] = offset_vertex_id;
                } else {
                    vertex_offsets.push_back({
                        endpoint_id,
                        offset_vertex_id,
                        bevel_side.face_id,
                        offset_position,
                    });
                }
            }
            build.sides[side_index] = std::move(bevel_side);
        }
        if (side_failed) {
            continue;
        }

        const Vertex* source_a = find_vertex(document, build.edge.a);
        const Vertex* source_b = find_vertex(document, build.edge.b);
        if (source_a == nullptr || source_b == nullptr) {
            continue;
        }

        build.rows.resize(static_cast<std::size_t>(settings.segments + 1));
        for (int segment_index = 0; segment_index <= settings.segments; ++segment_index) {
            if (segment_index == 0) {
                build.rows[static_cast<std::size_t>(segment_index)] = {
                    edge_bevel_side_endpoint_vertex(build.sides[0], build.edge.a),
                    edge_bevel_side_endpoint_vertex(build.sides[0], build.edge.b),
                };
                continue;
            }
            if (segment_index == settings.segments) {
                build.rows[static_cast<std::size_t>(segment_index)] = {
                    edge_bevel_side_endpoint_vertex(build.sides[1], build.edge.a),
                    edge_bevel_side_endpoint_vertex(build.sides[1], build.edge.b),
                };
                continue;
            }

            const quader::QVec3 side0_a = edge_bevel_side_endpoint_position(build.sides[0], build.edge.a);
            const quader::QVec3 side0_b = edge_bevel_side_endpoint_position(build.sides[0], build.edge.b);
            const quader::QVec3 side1_a = edge_bevel_side_endpoint_position(build.sides[1], build.edge.a);
            const quader::QVec3 side1_b = edge_bevel_side_endpoint_position(build.sides[1], build.edge.b);
            const quader::QVec3 middle_a = profile_middle_for_endpoint(build, build.edge.a);
            const quader::QVec3 middle_b = profile_middle_for_endpoint(build, build.edge.b);
            const quader::QVec3 profiled_a = edge_bevel_profile_point(
                middle_a,
                side0_a,
                side1_a,
                segment_index,
                settings.segments,
                settings.profile,
                settings.profile_type);
            const quader::QVec3 profiled_b = edge_bevel_profile_point(
                middle_b,
                side0_b,
                side1_b,
                segment_index,
                settings.segments,
                settings.profile,
                settings.profile_type);
            build.rows[static_cast<std::size_t>(segment_index)] = {
                edge_bevel_profile_vertex_for_source(candidate, build.edge.a, profiled_a, profile_vertices_by_source),
                edge_bevel_profile_vertex_for_source(candidate, build.edge.b, profiled_b, profile_vertices_by_source),
            };
        }

        if (std::ranges::any_of(
                build.rows, [](const std::array<ElementId, 2> &row) {
                  return row[0] == kInvalidElementId ||
                         row[1] == kInvalidElementId || row[0] == row[1];
                })) {
          continue;
        }

        const std::size_t build_index = builds.size();
        for (int side_index = 0; side_index < 2; ++side_index) {
            side_by_face_edge.emplace(edge_bevel_side_key(build.sides[side_index].face_id, build.edge), std::pair { build_index, side_index });
        }
        builds.push_back(std::move(build));
    }

    /**
     * Represents an Edge Bevel Endpoint Side Ref value used by the polygon document and mesh editing core.
     */
    struct EdgeBevelEndpointSideRef {
        std::size_t build_index = 0;
        int side_index = 0;
        ElementId endpoint_id = kInvalidElementId;
        ElementId vertex_id = kInvalidElementId;
        quader::QVec3 position;
    };

    /**
     * Represents an Edge Bevel Split Miter Ref value used by the polygon document and mesh editing core.
     */
    struct EdgeBevelSplitMiterRef {
        std::size_t build_index = 0;
        int side_index = 0;
        ElementId endpoint_id = kInvalidElementId;
        ElementId vertex_id = kInvalidElementId;
        quader::QVec3 position;
        quader::QVec3 normal;
    };

    const auto split_miter_edge_line = [&document, &builds](const EdgeBevelSplitMiterRef& ref) -> std::optional<EdgeBevelOffsetLine> {
        if (ref.build_index >= builds.size()) {
            return std::nullopt;
        }

        const EdgeBevelBuild& build = builds[ref.build_index];
        const ElementId other_endpoint_id = build.edge.a == ref.endpoint_id ? build.edge.b : build.edge.a;
        const Vertex* endpoint = find_vertex(document, ref.endpoint_id);
        const Vertex* other = find_vertex(document, other_endpoint_id);
        if (endpoint == nullptr || other == nullptr) {
            return std::nullopt;
        }

        const quader::QVec3 direction = normalize_or_zero(other->position - endpoint->position);
        if (length_squared(direction) <= kEpsilon) {
          return std::nullopt;
        }
        return EdgeBevelOffsetLine { ref.position, direction };
    };

    // Coplanar source-face splits can separate two selected perimeter edges with
    // an unselected spoke. Share one miter vertex so the spoke stays single.
    for (const auto& [vertex_id, selected_count] : selected_edge_counts) {
        const bool fully_selected = fully_selected_vertices.contains(vertex_id) && fully_selected_vertices.at(vertex_id);
        if (selected_count != 2 || fully_selected) {
            continue;
        }

        std::vector<std::vector<EdgeBevelSplitMiterRef>> groups;
        for (std::size_t build_index = 0; build_index < builds.size(); ++build_index) {
            EdgeBevelBuild& build = builds[build_index];
            if (build.edge.a != vertex_id && build.edge.b != vertex_id) {
                continue;
            }
            for (int side_index = 0; side_index < 2; ++side_index) {
                EdgeBevelSide& side = build.sides[static_cast<std::size_t>(side_index)];
                const ElementId endpoint_vertex_id = edge_bevel_side_endpoint_vertex(side, vertex_id);
                const quader::QVec3 endpoint_position = edge_bevel_side_endpoint_position(side, vertex_id);
                if (endpoint_vertex_id == kInvalidElementId ||
                    length_squared(side.normal) <= kEpsilon) {
                  continue;
                }

                EdgeBevelSplitMiterRef ref {
                    build_index,
                    side_index,
                    vertex_id,
                    endpoint_vertex_id,
                    endpoint_position,
                    normalize_or_zero(side.normal),
                };
                const auto existing_group = std::ranges::find_if(groups, [&ref](const std::vector<EdgeBevelSplitMiterRef>& group) {
                    return !group.empty() && dot(group.front().normal, ref.normal) >= 0.999F;
                });
                if (existing_group == groups.end()) {
                    groups.push_back({ ref });
                } else {
                    existing_group->push_back(ref);
                }
            }
        }

        for (const std::vector<EdgeBevelSplitMiterRef>& group : groups) {
            if (group.size() != 2U || length_squared(group[0].position - group[1].position) <= 0.000001F) {
                continue;
            }

            const std::optional<EdgeBevelOffsetLine> first_line = split_miter_edge_line(group[0]);
            const std::optional<EdgeBevelOffsetLine> second_line = split_miter_edge_line(group[1]);
            if (!first_line.has_value() || !second_line.has_value()) {
                continue;
            }

            const std::optional<quader::QVec3> miter_position = edge_bevel_intersect_offset_lines(*first_line, *second_line);
            if (!miter_position.has_value()) {
                continue;
            }

            const ElementId miter_vertex_id = add_vertex(candidate, *miter_position);
            for (const EdgeBevelSplitMiterRef& ref : group) {
                EdgeBevelBuild& build = builds[ref.build_index];
                EdgeBevelSide& side = build.sides[static_cast<std::size_t>(ref.side_index)];
                side.endpoint_vertices[vertex_id] = miter_vertex_id;
                side.endpoint_positions[vertex_id] = *miter_position;
            }
        }
    }

    std::map<ElementId, std::vector<EdgeBevelCornerArc>> patch_miter_arcs_by_vertex;
    std::map<std::pair<ElementId, ElementId>, std::vector<EdgeBevelCornerArc>> patch_miter_face_arcs_by_vertex;
    std::map<ElementId, ElementId> patch_miter_middle_by_vertex;
    const auto make_patch_miter_arc = [&candidate, &settings](
        ElementId first_vertex_id,
        quader::QVec3 first_position,
        ElementId second_vertex_id,
        quader::QVec3 second_position) {
        EdgeBevelCornerArc arc;
        arc.use_global_profile = false;
        arc.profile = 0.25F;
        arc.profile_type = BevelProfileType::Offset;
        arc.profile_middle = (first_position + second_position) * 0.5F;
        arc.has_profile_middle = true;
        arc.vertices.reserve(static_cast<std::size_t>(settings.segments + 1));
        for (int segment_index = 0; segment_index <= settings.segments; ++segment_index) {
            const float t = static_cast<float>(segment_index) / static_cast<float>(settings.segments);
            ElementId vertex_id = kInvalidElementId;
            quader::QVec3 position = first_position + ((second_position - first_position) * t);
            if (segment_index == 0) {
                vertex_id = first_vertex_id;
                position = first_position;
            } else if (segment_index == settings.segments) {
                vertex_id = second_vertex_id;
                position = second_position;
            } else {
                vertex_id = add_vertex(candidate, position);
            }
            arc.vertices.push_back(vertex_id);
        }
        return arc;
    };

    for (const auto& [vertex_id, selected_count] : selected_edge_counts) {
        if (selected_count < 3) {
            continue;
        }
        if (fully_selected_vertices.contains(vertex_id) && fully_selected_vertices.at(vertex_id)) {
            continue;
        }
        const Vertex* source_vertex = find_vertex(document, vertex_id);
        if (source_vertex == nullptr) {
            continue;
        }

        std::vector<std::vector<EdgeBevelEndpointSideRef>> groups;
        for (std::size_t build_index = 0; build_index < builds.size(); ++build_index) {
            EdgeBevelBuild& build = builds[build_index];
            if (build.edge.a != vertex_id && build.edge.b != vertex_id) {
                continue;
            }
            for (int side_index = 0; side_index < 2; ++side_index) {
                EdgeBevelSide& side = build.sides[static_cast<std::size_t>(side_index)];
                const ElementId endpoint_vertex_id = edge_bevel_side_endpoint_vertex(side, vertex_id);
                const quader::QVec3 endpoint_position = edge_bevel_side_endpoint_position(side, vertex_id);
                if (endpoint_vertex_id == kInvalidElementId) {
                  continue;
                }

                EdgeBevelEndpointSideRef ref {
                    build_index,
                    side_index,
                    vertex_id,
                    endpoint_vertex_id,
                    endpoint_position,
                };
                const auto existing_group = std::ranges::find_if(groups, [ref](const std::vector<EdgeBevelEndpointSideRef>& group) {
                    return std::ranges::any_of(group, [ref](const EdgeBevelEndpointSideRef& existing) {
                        return existing.vertex_id == ref.vertex_id ||
                            length_squared(existing.position - ref.position) <= 0.000001F;
                    });
                });
                if (existing_group == groups.end()) {
                    groups.push_back({ ref });
                } else {
                    existing_group->push_back(ref);
                }
            }
        }

        std::vector<EdgeBevelEndpointSideRef> singleton_refs;
        singleton_refs.reserve(2);
        for (const std::vector<EdgeBevelEndpointSideRef>& group : groups) {
            if (group.size() == 1U) {
                singleton_refs.push_back(group.front());
            }
        }
        if (singleton_refs.size() != 2U) {
            continue;
        }

        const quader::QVec3 first_delta = singleton_refs[0].position - source_vertex->position;
        const quader::QVec3 second_delta = singleton_refs[1].position - source_vertex->position;
        const quader::QVec3 merged_position = source_vertex->position + first_delta + second_delta;
        const ElementId merged_vertex_id = add_vertex(candidate, merged_position);
        const auto adjusted_patch_miter_ref =
            [&candidate, &builds, &document, source_vertex, merged_position](EdgeBevelEndpointSideRef ref) {
                if (ref.build_index >= builds.size() || ref.side_index < 0 || ref.side_index > 1) {
                    return ref;
                }

                EdgeBevelBuild& build = builds[ref.build_index];
                const ElementId other_endpoint_id = build.edge.a == ref.endpoint_id ? build.edge.b : build.edge.a;
                const Vertex* other_endpoint = find_vertex(document, other_endpoint_id);
                if (other_endpoint == nullptr) {
                    return ref;
                }

                const quader::QVec3 edge_direction = normalize_or_zero(source_vertex->position - other_endpoint->position);
                const EdgeBevelSide& opposite_side = build.sides[static_cast<std::size_t>(1 - ref.side_index)];
                const quader::QVec3 opposite_position = edge_bevel_side_endpoint_position(opposite_side, ref.endpoint_id);
                if (length_squared(edge_direction) <= kEpsilon ||
                    length_squared(opposite_position - merged_position) <=
                        kEpsilon) {
                  return ref;
                }

                const quader::QVec3 adjusted_position =
                    merged_position + (edge_direction * dot(opposite_position - merged_position, edge_direction));
                if (length_squared(adjusted_position - merged_position) <=
                    kEpsilon) {
                  return ref;
                }

                EdgeBevelSide& side = build.sides[static_cast<std::size_t>(ref.side_index)];
                if (Vertex* endpoint_vertex = find_vertex(candidate, ref.vertex_id)) {
                    endpoint_vertex->position = adjusted_position;
                } else {
                    ref.vertex_id = add_vertex(candidate, adjusted_position);
                    side.endpoint_vertices[ref.endpoint_id] = ref.vertex_id;
                }
                side.endpoint_positions[ref.endpoint_id] = adjusted_position;
                ref.position = adjusted_position;
                return ref;
            };

        EdgeBevelEndpointSideRef first_patch_ref =
            adjusted_patch_miter_ref(singleton_refs[0]);
        EdgeBevelEndpointSideRef second_patch_ref =
            adjusted_patch_miter_ref(singleton_refs[1]);

        patch_miter_middle_by_vertex[vertex_id] = merged_vertex_id;
        auto& miter_arcs = patch_miter_arcs_by_vertex[vertex_id];
        EdgeBevelCornerArc first_arc = make_patch_miter_arc(
            first_patch_ref.vertex_id,
            first_patch_ref.position,
            merged_vertex_id,
            merged_position);
        EdgeBevelCornerArc second_arc = make_patch_miter_arc(
            merged_vertex_id,
            merged_position,
            second_patch_ref.vertex_id,
            second_patch_ref.position);
        const ElementId first_face_id =
            builds[first_patch_ref.build_index].sides[static_cast<std::size_t>(first_patch_ref.side_index)].face_id;
        const ElementId second_face_id =
            builds[second_patch_ref.build_index].sides[static_cast<std::size_t>(second_patch_ref.side_index)].face_id;
        patch_miter_face_arcs_by_vertex[{ vertex_id, first_face_id }].push_back(first_arc);
        patch_miter_face_arcs_by_vertex[{ vertex_id, second_face_id }].push_back(second_arc);
        miter_arcs.push_back(std::move(first_arc));
        miter_arcs.push_back(std::move(second_arc));
    }

    rebuild_edge_bevel_boundary_state();

    std::map<ElementId, std::vector<ElementId>> terminal_profiles_by_vertex;
    for (const EdgeBevelBuild& build : builds) {
        const bool edge_a_terminal = (selected_edge_counts.contains(build.edge.a) ? selected_edge_counts.at(build.edge.a) : 0) <= 1;
        const bool edge_b_terminal = (selected_edge_counts.contains(build.edge.b) ? selected_edge_counts.at(build.edge.b) : 0) <= 1;
        const bool edge_a_fully_selected = fully_selected_vertices.contains(build.edge.a) && fully_selected_vertices.at(build.edge.a);
        const bool edge_b_fully_selected = fully_selected_vertices.contains(build.edge.b) && fully_selected_vertices.at(build.edge.b);
        if (!edge_a_fully_selected && edge_a_terminal) {
            terminal_profiles_by_vertex[build.edge.a] = edge_bevel_endpoint_profile_vertices(build, build.edge.a);
        }
        if (!edge_b_fully_selected && edge_b_terminal) {
            terminal_profiles_by_vertex[build.edge.b] = edge_bevel_endpoint_profile_vertices(build, build.edge.b);
        }
    }
    std::map<std::pair<ElementId, ElementId>, ElementId> terminal_boundary_vertices_by_edge;
    const auto terminal_boundary_vertex =
        [&](ElementId source_vertex_id,
            ElementId neighbor_vertex_id) -> ElementId {
      if (source_vertex_id == kInvalidElementId ||
          neighbor_vertex_id == kInvalidElementId) {
        return kInvalidElementId;
      }
      const std::pair key{source_vertex_id, neighbor_vertex_id};
      if (const auto existing = terminal_boundary_vertices_by_edge.find(key);
          existing != terminal_boundary_vertices_by_edge.end()) {
        return existing->second;
      }

      const Vertex *source_vertex = find_vertex(document, source_vertex_id);
      const Vertex *neighbor_vertex = find_vertex(document, neighbor_vertex_id);
      if (source_vertex == nullptr || neighbor_vertex == nullptr) {
        return kInvalidElementId;
      }

      const quader::QVec3 edge_delta =
          neighbor_vertex->position - source_vertex->position;
      const float edge_length = length(edge_delta);
      if (edge_length <= kEpsilon) {
        return kInvalidElementId;
      }

      const float boundary_distance =
          std::min(settings.width, edge_length * 0.999F);
      const quader::QVec3 boundary_position =
          source_vertex->position +
          (edge_delta * (boundary_distance / edge_length));
      if (const auto reusable = std::ranges::find_if(
              candidate.vertices,
              [boundary_position](const Vertex &vertex) {
                return length_squared(vertex.position - boundary_position) <= 0.000001F;
              });
          reusable != candidate.vertices.end()) {
        terminal_boundary_vertices_by_edge.emplace(key, reusable->id);
        return reusable->id;
      }

      const ElementId boundary_vertex_id =
          add_vertex(candidate, boundary_position);
      terminal_boundary_vertices_by_edge.emplace(key, boundary_vertex_id);
      return boundary_vertex_id;
    };

    const auto append_terminal_boundary_profile_vertices = [&](
        ElementId source_vertex_id,
        ElementId previous_id,
        ElementId next_id,
        std::span<const ElementId> profile_vertices,
        std::vector<ElementId>& rebuilt_loop) {
        const ElementId previous_boundary_id = terminal_boundary_vertex(source_vertex_id, previous_id);
        const ElementId next_boundary_id = terminal_boundary_vertex(source_vertex_id, next_id);
        if (previous_boundary_id == kInvalidElementId ||
            next_boundary_id == kInvalidElementId) {
          return false;
        }

        std::vector<ElementId> ordered_profile_vertices;
        ordered_profile_vertices.reserve(profile_vertices.size());
        append_edge_bevel_terminal_profile_vertices(
            candidate,
            source_vertex_id,
            previous_id,
            next_id,
            profile_vertices,
            ordered_profile_vertices);
        if (ordered_profile_vertices.empty() ||
            contains_id(ordered_profile_vertices, source_vertex_id)) {
            return false;
        }

        rebuilt_loop.push_back(previous_boundary_id);
        for (const ElementId profile_vertex_id : ordered_profile_vertices) {
          if (profile_vertex_id == kInvalidElementId) {
            continue;
          }
            if (!rebuilt_loop.empty() && rebuilt_loop.back() == profile_vertex_id) {
                continue;
            }
            rebuilt_loop.push_back(profile_vertex_id);
        }
        if (rebuilt_loop.empty() || rebuilt_loop.back() != next_boundary_id) {
            rebuilt_loop.push_back(next_boundary_id);
        }
        return true;
    };

    std::vector<Face> rebuilt_faces;
    rebuilt_faces.reserve(document.faces.size() + (selected_edges.size() * static_cast<std::size_t>(settings.segments * 3)));
    std::vector<ElementId> generated_face_ids;
    generated_face_ids.reserve(selected_edges.size() * static_cast<std::size_t>(settings.segments * 3));
    std::set<ElementId> trimmed_terminal_vertices;
    /**
     * Represents an Edge Bevel Terminal Corner Patch value used by the polygon document and mesh editing core.
     */
    struct EdgeBevelTerminalCornerPatch {
      ElementId source_vertex_id = kInvalidElementId;
      ElementId previous_id = kInvalidElementId;
      ElementId next_id = kInvalidElementId;
      std::vector<ElementId> profile_vertices;
      std::uint32_t material_slot = 0;
      quader::QVec3 expected_normal;
    };
    std::vector<EdgeBevelTerminalCornerPatch> terminal_corner_patches;
    const auto append_patch_miter_face_arc = [&patch_miter_face_arcs_by_vertex](
        ElementId source_vertex_id,
        ElementId face_id,
        ElementId endpoint_id,
        bool endpoint_first,
        std::vector<ElementId>& rebuilt_loop) {
        const auto arcs = patch_miter_face_arcs_by_vertex.find({ source_vertex_id, face_id });
        if (arcs == patch_miter_face_arcs_by_vertex.end()) {
            return false;
        }
        const auto arc = std::ranges::find_if(arcs->second, [endpoint_id](const EdgeBevelCornerArc& candidate_arc) {
            return !candidate_arc.vertices.empty() &&
                (candidate_arc.vertices.front() == endpoint_id || candidate_arc.vertices.back() == endpoint_id);
        });
        if (arc == arcs->second.end()) {
            return false;
        }

        std::vector<ElementId> vertices = arc->vertices;
        if (endpoint_first) {
            if (vertices.front() != endpoint_id) {
                std::ranges::reverse(vertices);
            }
        } else if (vertices.back() != endpoint_id) {
            std::ranges::reverse(vertices);
        }

        for (const ElementId patch_vertex_id : vertices) {
          if (patch_vertex_id == kInvalidElementId) {
            continue;
          }
            if (!rebuilt_loop.empty() && rebuilt_loop.back() == patch_vertex_id) {
                continue;
            }
            rebuilt_loop.push_back(patch_vertex_id);
        }
        return true;
    };

    for (const Face& face : document.faces) {
        bool face_changed = false;
        std::vector<ElementId> rebuilt_loop;
        rebuilt_loop.reserve(face.vertices.size() * 4U);
        for (std::size_t vertex_index = 0; vertex_index < face.vertices.size(); ++vertex_index) {
            const ElementId vertex_id = face.vertices[vertex_index];
            const ElementId previous_id = face.vertices[(vertex_index + face.vertices.size() - 1U) % face.vertices.size()];
            const ElementId next_id = face.vertices[(vertex_index + 1U) % face.vertices.size()];
            const auto previous_side = side_by_face_edge.find(edge_bevel_side_key(face.id, make_edge(previous_id, vertex_id)));
            const auto next_side = side_by_face_edge.find(edge_bevel_side_key(face.id, make_edge(vertex_id, next_id)));
            if (previous_side == side_by_face_edge.end() && next_side == side_by_face_edge.end()) {
                const auto offsets = offsets_by_vertex.find(vertex_id);
                if (offsets == offsets_by_vertex.end()) {
                    rebuilt_loop.push_back(vertex_id);
                } else {
                    const auto terminal_profile = terminal_profiles_by_vertex.find(vertex_id);
                    const bool terminal_vertex = terminal_profile != terminal_profiles_by_vertex.end();
                    const int selected_count = selected_edge_counts.contains(vertex_id)
                        ? selected_edge_counts.at(vertex_id)
                        : 0;
                    const bool selected_edge_leaves_face_plane = [&]() {
                        if (terminal_vertex || selected_count < 2) {
                            return false;
                        }
                        const Vertex* source_vertex = find_vertex(document, vertex_id);
                        const auto neighbors = selected_neighbors_by_vertex.find(vertex_id);
                        const quader::QVec3 normal = normalize_or_zero(face_normal(document, face));
                        if (source_vertex == nullptr || neighbors == selected_neighbors_by_vertex.end() ||
                            length_squared(normal) <= kEpsilon) {
                            return false;
                        }
                        return std::ranges::any_of(neighbors->second, [&](ElementId neighbor_id) {
                            const Vertex* neighbor_vertex = find_vertex(document, neighbor_id);
                            if (neighbor_vertex == nullptr) {
                                return false;
                            }
                            const quader::QVec3 direction =
                                normalize_or_zero(neighbor_vertex->position - source_vertex->position);
                            return std::abs(dot(direction, normal)) > 0.0001F;
                        });
                    }();
                    const bool selected_open_corner_vertex = selected_edge_leaves_face_plane;
                    if (terminal_vertex) {
                        if (edge_bevel_endpoint_profile_lies_in_face_corner(
                                candidate,
                                face,
                                vertex_id,
                                previous_id,
                                next_id,
                                terminal_profile->second)) {
                            if (!append_terminal_boundary_profile_vertices(
                                    vertex_id,
                                    previous_id,
                                    next_id,
                                    terminal_profile->second,
                                    rebuilt_loop)) {
                                append_edge_bevel_terminal_profile_vertices(
                                    candidate,
                                    vertex_id,
                                    previous_id,
                                    next_id,
                                    terminal_profile->second,
                                    rebuilt_loop);
                            }
                            face_changed = true;
                            continue;
                        }
                    }

                    const auto patch_middle = patch_miter_middle_by_vertex.find(vertex_id);
                    const Vertex* middle_vertex = patch_middle != patch_miter_middle_by_vertex.end() ?
                        find_vertex(candidate, patch_middle->second) :
                        nullptr;
                    if (middle_vertex != nullptr &&
                        edge_bevel_point_lies_on_face_plane(document, face, middle_vertex->position) &&
                        edge_bevel_point_lies_in_face_corner(document, face, vertex_id, previous_id, next_id, middle_vertex->position)) {
                        rebuilt_loop.push_back(patch_middle->second);
                    } else {
                        const std::size_t before_offsets = rebuilt_loop.size();
                        append_edge_bevel_unselected_vertex_offsets(
                            document,
                            face,
                            vertex_id,
                            previous_id,
                            next_id,
                            offsets->second,
                            rebuilt_loop);
                        if (terminal_vertex && rebuilt_loop.size() == before_offsets + 1U &&
                            rebuilt_loop.back() == vertex_id) {
                            rebuilt_loop.pop_back();
                            const ElementId previous_boundary_id = terminal_boundary_vertex(vertex_id, previous_id);
                            const ElementId next_boundary_id = terminal_boundary_vertex(vertex_id, next_id);
                            if (previous_boundary_id != kInvalidElementId &&
                                next_boundary_id != kInvalidElementId) {
                              rebuilt_loop.push_back(previous_boundary_id);
                              if (rebuilt_loop.back() != next_boundary_id) {
                                rebuilt_loop.push_back(next_boundary_id);
                              }
                            }
                            const bool profile_lies_on_face = std::ranges::all_of(
                                terminal_profile->second,
                                [&candidate, &face](ElementId profile_vertex_id) {
                                    const Vertex* profile_vertex = find_vertex(candidate, profile_vertex_id);
                                    return profile_vertex != nullptr &&
                                        edge_bevel_point_lies_on_face_plane(candidate, face, profile_vertex->position);
                                });
                            if (profile_lies_on_face) {
                              terminal_corner_patches.push_back({
                                  vertex_id,
                                  previous_boundary_id != kInvalidElementId
                                      ? previous_boundary_id
                                      : previous_id,
                                  next_boundary_id != kInvalidElementId
                                      ? next_boundary_id
                                      : next_id,
                                  terminal_profile->second,
                                  face.material_slot,
                                  face_normal(document, face),
                              });
                            }
                        } else if ((terminal_vertex || selected_open_corner_vertex) &&
                            rebuilt_loop.size() > before_offsets) {
                            const ElementId previous_boundary_id = terminal_boundary_vertex(vertex_id, previous_id);
                            if (previous_boundary_id != kInvalidElementId &&
                                (rebuilt_loop.size() == before_offsets ||
                                 rebuilt_loop[before_offsets] !=
                                     previous_boundary_id)) {
                              rebuilt_loop.insert(
                                  rebuilt_loop.begin() +
                                      static_cast<std::ptrdiff_t>(
                                          before_offsets),
                                  previous_boundary_id);
                            }
                            const ElementId next_boundary_id = terminal_boundary_vertex(vertex_id, next_id);
                            if (next_boundary_id != kInvalidElementId &&
                                (rebuilt_loop.empty() ||
                                 rebuilt_loop.back() != next_boundary_id)) {
                              rebuilt_loop.push_back(next_boundary_id);
                            }
                        }
                    }
                    face_changed = true;
                }
                continue;
            }

            if (previous_side != side_by_face_edge.end()) {
                const EdgeBevelSide& bevel_side = builds[previous_side->second.first].sides[previous_side->second.second];
                const ElementId offset = edge_bevel_side_endpoint_vertex(bevel_side, vertex_id);
                if (offset == kInvalidElementId) {
                  continue;
                }
                if (!append_patch_miter_face_arc(vertex_id, face.id, offset, true, rebuilt_loop)) {
                    rebuilt_loop.push_back(offset);
                }
                if (next_side == side_by_face_edge.end() && terminal_profiles_by_vertex.contains(vertex_id)) {
                    const ElementId next_boundary_id = terminal_boundary_vertex(vertex_id, next_id);
                    if (next_boundary_id != kInvalidElementId &&
                        (rebuilt_loop.empty() ||
                         rebuilt_loop.back() != next_boundary_id)) {
                      rebuilt_loop.push_back(next_boundary_id);
                    }
                }
            }
            if (next_side != side_by_face_edge.end()) {
                const EdgeBevelSide& bevel_side = builds[next_side->second.first].sides[next_side->second.second];
                const ElementId offset = edge_bevel_side_endpoint_vertex(bevel_side, vertex_id);
                if (offset == kInvalidElementId) {
                  continue;
                }
                if (previous_side == side_by_face_edge.end() && terminal_profiles_by_vertex.contains(vertex_id)) {
                    const ElementId previous_boundary_id = terminal_boundary_vertex(vertex_id, previous_id);
                    if (previous_boundary_id != kInvalidElementId &&
                        (rebuilt_loop.empty() ||
                         rebuilt_loop.back() != previous_boundary_id)) {
                      rebuilt_loop.push_back(previous_boundary_id);
                    }
                }
                if (append_patch_miter_face_arc(vertex_id, face.id, offset, false, rebuilt_loop)) {
                    continue;
                }
                if (rebuilt_loop.empty() || rebuilt_loop.back() != offset) {
                    rebuilt_loop.push_back(offset);
                }
            }
            face_changed = true;
        }

        if (!face_changed) {
            rebuilt_faces.push_back(face);
            continue;
        }

        rebuilt_loop = compact_edge_bevel_face_loop(std::move(rebuilt_loop));
        if (rebuilt_loop.size() < 3) {
            continue;
        }

        Face rebuilt_face = face;
        rebuilt_face.vertices = std::move(rebuilt_loop);
        rebuilt_face.uvs.clear();
        rebuilt_faces.push_back(std::move(rebuilt_face));
    }

    for (const auto& [vertex_id, profile_vertices] : terminal_profiles_by_vertex) {
        (void)profile_vertices;
        const bool source_vertex_still_used = std::ranges::any_of(rebuilt_faces, [vertex_id](const Face& face) {
            return contains_id(face.vertices, vertex_id);
        });
        if (!source_vertex_still_used) {
            trimmed_terminal_vertices.insert(vertex_id);
        }
    }

    std::vector<Face> bevel_faces;
    bevel_faces.reserve(selected_edges.size() * static_cast<std::size_t>(settings.segments * 4));
    const auto terminal_profile_continuation_support =
        [&candidate, &rebuilt_faces](std::span<const ElementId> profile_vertices) -> std::optional<ElementId> {
        if (profile_vertices.size() < 2U) {
            return std::nullopt;
        }

        const std::set<ElementId> profile_vertex_set(profile_vertices.begin(), profile_vertices.end());
        const auto non_profile_neighbors = [&rebuilt_faces, &profile_vertex_set](ElementId profile_vertex_id) {
            std::set<ElementId> neighbors;
            for (const Face& face : rebuilt_faces) {
                for (std::size_t index = 0; index < face.vertices.size(); ++index) {
                    if (face.vertices[index] != profile_vertex_id) {
                        continue;
                    }
                    const ElementId previous_id =
                        face.vertices[(index + face.vertices.size() - 1U) % face.vertices.size()];
                    const ElementId next_id = face.vertices[(index + 1U) % face.vertices.size()];
                    if (!profile_vertex_set.contains(previous_id)) {
                        neighbors.insert(previous_id);
                    }
                    if (!profile_vertex_set.contains(next_id)) {
                        neighbors.insert(next_id);
                    }
                }
            }
            return neighbors;
        };

        const std::set<ElementId> first_neighbors = non_profile_neighbors(profile_vertices.front());
        const std::set<ElementId> last_neighbors = non_profile_neighbors(profile_vertices.back());
        for (const ElementId support_id : first_neighbors) {
          if (support_id != kInvalidElementId &&
              last_neighbors.contains(support_id) &&
              find_vertex(candidate, support_id) != nullptr) {
            return support_id;
          }
        }
        return std::nullopt;
    };

    const auto insert_terminal_cap_in_source_faces =
        [&rebuilt_faces](ElementId support_vertex_id, ElementId cap_vertex_id, std::span<const ElementId> profile_vertices) {
        if (profile_vertices.empty()) {
            return;
        }
        const std::set<ElementId> terminal_endpoints {
            profile_vertices.front(),
            profile_vertices.back(),
        };
        for (Face& face : rebuilt_faces) {
            std::vector<ElementId> loop;
            loop.reserve(face.vertices.size() + 1U);
            bool inserted = false;
            for (std::size_t index = 0; index < face.vertices.size(); ++index) {
                const ElementId current = face.vertices[index];
                const ElementId next = face.vertices[(index + 1U) % face.vertices.size()];
                loop.push_back(current);
                if (!inserted &&
                    ((current == support_vertex_id && terminal_endpoints.contains(next)) ||
                        (terminal_endpoints.contains(current) && next == support_vertex_id))) {
                    loop.push_back(cap_vertex_id);
                    inserted = true;
                }
            }
            if (inserted) {
                face.vertices = compact_edge_bevel_face_loop(std::move(loop));
                face.uvs.clear();
            }
        }
    };

    const auto terminal_support_vertex_for_build = [&](const EdgeBevelBuild& build, ElementId endpoint_id) -> std::optional<ElementId> {
        const ElementId other_endpoint_id = build.edge.a == endpoint_id ? build.edge.b : build.edge.a;
        ElementId support_vertex_id = kInvalidElementId;
        for (const EdgeBevelSide& side : build.sides) {
            const Face* face = find_face(document, side.face_id);
            if (face == nullptr || face->vertices.size() < 3U) {
                return std::nullopt;
            }
            const auto endpoint = std::ranges::find(face->vertices, endpoint_id);
            if (endpoint == face->vertices.end()) {
                return std::nullopt;
            }

            const std::size_t vertex_index = static_cast<std::size_t>(std::distance(face->vertices.begin(), endpoint));
            const std::array<ElementId, 2> neighbors {
                face->vertices[(vertex_index + face->vertices.size() - 1U) % face->vertices.size()],
                face->vertices[(vertex_index + 1U) % face->vertices.size()],
            };

            ElementId side_support = kInvalidElementId;
            for (const ElementId neighbor_id : neighbors) {
                if (neighbor_id == other_endpoint_id) {
                    continue;
                }
                side_support = neighbor_id;
                break;
            }
            if (side_support == kInvalidElementId) {
              return std::nullopt;
            }
            if (support_vertex_id == kInvalidElementId) {
              support_vertex_id = side_support;
            } else if (support_vertex_id != side_support) {
              return std::nullopt;
            }
        }
        return support_vertex_id == kInvalidElementId
                   ? std::nullopt
                   : std::optional<ElementId>(support_vertex_id);
    };

    for (const EdgeBevelBuild& build : builds) {
        const Vertex* edge_a = find_vertex(document, build.edge.a);
        const Vertex* edge_b = find_vertex(document, build.edge.b);
        if (edge_a == nullptr || edge_b == nullptr) {
            continue;
        }
        const quader::QVec3 edge_direction = normalize_or_zero(edge_b->position - edge_a->position);
        if (length_squared(edge_direction) <= kEpsilon) {
          continue;
        }

        for (const ElementId endpoint_id : { build.edge.a, build.edge.b }) {
            const bool terminal = (selected_edge_counts.contains(endpoint_id) ? selected_edge_counts.at(endpoint_id) : 0) <= 1;
            if (!terminal || !trimmed_terminal_vertices.contains(endpoint_id)) {
                continue;
            }
            const auto profile = terminal_profiles_by_vertex.find(endpoint_id);
            if (profile == terminal_profiles_by_vertex.end() || profile->second.size() < 2U) {
                continue;
            }
            if (const std::optional<ElementId> support_vertex_id =
                    terminal_profile_continuation_support(profile->second)) {
                const Vertex* endpoint = find_vertex(document, endpoint_id);
                const Vertex* support = find_vertex(candidate, *support_vertex_id);
                if (endpoint != nullptr && support != nullptr) {
                    const quader::QVec3 support_delta = support->position - endpoint->position;
                    const float support_length = length(support_delta);
                    if (support_length > kEpsilon) {
                      const float cap_distance =
                          std::min(settings.width, support_length * 0.999F);
                      const ElementId cap_vertex_id = add_vertex(
                          candidate, endpoint->position +
                                         (support_delta *
                                          (cap_distance / support_length)));
                      insert_terminal_cap_in_source_faces(
                          *support_vertex_id, cap_vertex_id, profile->second);
                      const quader::QVec3 expected_normal =
                          endpoint_id == build.edge.a ? edge_direction * -1.0F
                                                      : edge_direction;
                      for (std::size_t profile_index = 0;
                           profile_index + 1U < profile->second.size();
                           ++profile_index) {
                        append_edge_bevel_face(
                            candidate, bevel_faces, generated_face_ids,
                            {cap_vertex_id, profile->second[profile_index],
                             profile->second[profile_index + 1U]},
                            build.sides[0].material_slot, expected_normal);
                      }
                      continue;
                    }
                }
            }
            const std::optional<ElementId> support_vertex_id =
                terminal_support_vertex_for_build(build, endpoint_id);
            if (!support_vertex_id.has_value()) {
                continue;
            }

            const quader::QVec3 expected_normal = endpoint_id == build.edge.a ? edge_direction * -1.0F : edge_direction;
            for (std::size_t profile_index = 0; profile_index + 1U < profile->second.size(); ++profile_index) {
                append_edge_bevel_face(
                    candidate,
                    bevel_faces,
                    generated_face_ids,
                    { *support_vertex_id, profile->second[profile_index], profile->second[profile_index + 1U] },
                    build.sides[0].material_slot,
                    expected_normal);
            }
        }
    }

    auto terminal_patch_edge_counts = [&]() {
        std::map<std::pair<ElementId, ElementId>, int> counts;
        const auto add_faces = [&counts](const std::vector<Face>& faces) {
            for (const Face& face : faces) {
                for (std::size_t index = 0; index < face.vertices.size(); ++index) {
                    const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
                    ++counts[edge_key(edge)];
                }
            }
        };
        add_faces(rebuilt_faces);
        add_faces(bevel_faces);
        return counts;
    }();
    const auto terminal_patch_edge_count = [&terminal_patch_edge_counts](Edge edge) {
        const auto count = terminal_patch_edge_counts.find(edge_key(edge));
        return count == terminal_patch_edge_counts.end() ? 0 : count->second;
    };

    std::set<std::tuple<ElementId, ElementId, ElementId>> terminal_corner_patch_keys;
    for (const EdgeBevelTerminalCornerPatch& patch : terminal_corner_patches) {
      if (patch.profile_vertices.size() < 2U ||
          patch.previous_id == kInvalidElementId ||
          patch.next_id == kInvalidElementId) {
        continue;
      }
        if (!terminal_corner_patch_keys.emplace(patch.source_vertex_id, patch.previous_id, patch.next_id).second) {
            continue;
        }

        std::vector<ElementId> profile_vertices = patch.profile_vertices;
        const bool forward_matches_boundary =
            terminal_patch_edge_count(make_edge(patch.next_id, profile_vertices.front())) == 1 &&
            terminal_patch_edge_count(make_edge(patch.previous_id, profile_vertices.back())) == 1;
        const bool reverse_matches_boundary =
            terminal_patch_edge_count(make_edge(patch.next_id, profile_vertices.back())) == 1 &&
            terminal_patch_edge_count(make_edge(patch.previous_id, profile_vertices.front())) == 1;
        if (reverse_matches_boundary && !forward_matches_boundary) {
            std::ranges::reverse(profile_vertices);
        }

        std::vector<ElementId> patch_loop;
        patch_loop.reserve(profile_vertices.size() + 2U);
        patch_loop.push_back(patch.next_id);
        for (const ElementId profile_vertex_id : profile_vertices) {
          if (profile_vertex_id != kInvalidElementId) {
            patch_loop.push_back(profile_vertex_id);
          }
        }
        patch_loop.push_back(patch.previous_id);
        patch_loop = compact_edge_bevel_face_loop(std::move(patch_loop));
        if (patch_loop.size() < 3U) {
            continue;
        }

        append_edge_bevel_face(
            candidate,
            bevel_faces,
            generated_face_ids,
            patch_loop,
            patch.material_slot,
            patch.expected_normal);
    }

    for (const EdgeBevelBuild& build : builds) {
        const Vertex* edge_a = find_vertex(document, build.edge.a);
        const Vertex* edge_b = find_vertex(document, build.edge.b);
        if (edge_a == nullptr || edge_b == nullptr) {
            continue;
        }

        quader::QVec3 strip_normal = build.sides[0].normal + build.sides[1].normal;
        if (length_squared(strip_normal) <= kEpsilon) {
          const quader::QVec3 edge_direction =
              normalize_or_zero(edge_b->position - edge_a->position);
          const quader::QVec3 side_delta =
              edge_bevel_side_endpoint_position(build.sides[1], build.edge.a) -
              edge_bevel_side_endpoint_position(build.sides[0], build.edge.a);
          strip_normal = normalize_or_zero(cross(edge_direction, side_delta));
        }

        for (int segment_index = 0; segment_index < settings.segments; ++segment_index) {
            const std::array<ElementId, 2>& first = build.rows[static_cast<std::size_t>(segment_index)];
            const std::array<ElementId, 2>& second = build.rows[static_cast<std::size_t>(segment_index + 1)];
            if (!append_edge_bevel_face(
                    candidate,
                    bevel_faces,
                    generated_face_ids,
                    { first[0], first[1], second[1], second[0] },
                    build.sides[0].material_slot,
                    strip_normal)) {
                continue;
            }
        }

        const quader::QVec3 edge_direction = normalize_or_zero(edge_b->position - edge_a->position);
        const bool edge_a_fully_selected = fully_selected_vertices.contains(build.edge.a) && fully_selected_vertices.at(build.edge.a);
        const bool edge_b_fully_selected = fully_selected_vertices.contains(build.edge.b) && fully_selected_vertices.at(build.edge.b);
        const bool edge_a_terminal = (selected_edge_counts.contains(build.edge.a) ? selected_edge_counts.at(build.edge.a) : 0) <= 1;
        const bool edge_b_terminal = (selected_edge_counts.contains(build.edge.b) ? selected_edge_counts.at(build.edge.b) : 0) <= 1;
        const bool edge_a_trimmed_terminal = trimmed_terminal_vertices.contains(build.edge.a);
        const bool edge_b_trimmed_terminal = trimmed_terminal_vertices.contains(build.edge.b);
        if ((!edge_a_fully_selected && edge_a_terminal) || (!edge_b_fully_selected && edge_b_terminal)) {
            for (int segment_index = 0; segment_index < settings.segments; ++segment_index) {
                const std::array<ElementId, 2>& first = build.rows[static_cast<std::size_t>(segment_index)];
                const std::array<ElementId, 2>& second = build.rows[static_cast<std::size_t>(segment_index + 1)];
                if (!edge_a_fully_selected && edge_a_terminal &&
                    !edge_a_trimmed_terminal &&
                    !append_edge_bevel_face(
                        candidate,
                        bevel_faces,
                        generated_face_ids,
                        { build.edge.a, first[0], second[0] },
                        build.sides[0].material_slot,
                        edge_direction * -1.0F)) {
                    continue;
                }
                if (!edge_b_fully_selected && edge_b_terminal &&
                    !edge_b_trimmed_terminal &&
                    !append_edge_bevel_face(
                        candidate,
                        bevel_faces,
                        generated_face_ids,
                        { build.edge.b, second[1], first[1] },
                        build.sides[0].material_slot,
                        edge_direction)) {
                    continue;
                }
            }
        }
    }

    // Doorway/cutout terminals can keep the source vertex on one face while a neighboring
    // rebuilt face steps to a bevel offset; close that support seam with a small face.
    auto combined_face_edge_counts = [&]() {
        std::map<std::pair<ElementId, ElementId>, int> counts;
        const auto add_faces = [&counts](const std::vector<Face>& faces) {
            for (const Face& face : faces) {
                for (std::size_t index = 0; index < face.vertices.size(); ++index) {
                    const Edge edge = make_edge(face.vertices[index], face.vertices[(index + 1U) % face.vertices.size()]);
                    ++counts[edge_key(edge)];
                }
            }
        };
        add_faces(rebuilt_faces);
        add_faces(bevel_faces);
        return counts;
    };

    std::map<std::pair<ElementId, ElementId>, int> current_edge_counts = combined_face_edge_counts();
    const auto current_edge_count = [&current_edge_counts](Edge edge) {
        const auto count = current_edge_counts.find(edge_key(edge));
        return count == current_edge_counts.end() ? 0 : count->second;
    };
    const auto record_support_face_edges = [&current_edge_counts](const std::vector<ElementId>& vertices) {
        for (std::size_t index = 0; index < vertices.size(); ++index) {
            const Edge edge = make_edge(vertices[index], vertices[(index + 1U) % vertices.size()]);
            ++current_edge_counts[edge_key(edge)];
        }
    };

    std::set<std::array<ElementId, 3>> terminal_support_faces;
    for (const auto& [vertex_id, profile_vertices] : terminal_profiles_by_vertex) {
        (void)profile_vertices;
        if (trimmed_terminal_vertices.contains(vertex_id)) {
            continue;
        }
        const auto offsets = offsets_by_vertex.find(vertex_id);
        if (offsets == offsets_by_vertex.end()) {
            continue;
        }
        const bool source_vertex_still_used = std::ranges::any_of(rebuilt_faces, [vertex_id](const Face& face) {
            return contains_id(face.vertices, vertex_id);
        });
        if (!source_vertex_still_used) {
            continue;
        }

        for (const Face& face : document.faces) {
            const auto vertex = std::ranges::find(face.vertices, vertex_id);
            if (vertex == face.vertices.end() || face.vertices.size() < 3U) {
                continue;
            }
            const std::size_t vertex_index = static_cast<std::size_t>(std::distance(face.vertices.begin(), vertex));
            const std::array<ElementId, 2> neighbors {
                face.vertices[(vertex_index + face.vertices.size() - 1U) % face.vertices.size()],
                face.vertices[(vertex_index + 1U) % face.vertices.size()],
            };

            for (const EdgeBevelFaceVertexOffset& offset : offsets->second) {
              if (offset.source_vertex_id != vertex_id ||
                  offset.offset_vertex_id == kInvalidElementId ||
                  !edge_bevel_point_lies_on_face_plane(candidate, face,
                                                       offset.position) ||
                  !edge_bevel_point_lies_in_face_corner(
                      candidate, face, vertex_id, neighbors[0], neighbors[1],
                      offset.position)) {
                continue;
              }

                for (const ElementId neighbor_id : neighbors) {
                  if (neighbor_id == kInvalidElementId ||
                      current_edge_count(
                          make_edge(vertex_id, offset.offset_vertex_id)) != 1 ||
                      current_edge_count(make_edge(
                          neighbor_id, offset.offset_vertex_id)) != 1 ||
                      current_edge_count(make_edge(vertex_id, neighbor_id)) !=
                          1) {
                    continue;
                  }

                    std::array<ElementId, 3> support_key {
                        vertex_id,
                        neighbor_id,
                        offset.offset_vertex_id,
                    };
                    std::ranges::sort(support_key);
                    if (!terminal_support_faces.insert(support_key).second) {
                        continue;
                    }

                    std::vector<ElementId> support_face {
                        vertex_id,
                        neighbor_id,
                        offset.offset_vertex_id,
                    };
                    if (append_edge_bevel_face(
                            candidate,
                            bevel_faces,
                            generated_face_ids,
                            support_face,
                            face.material_slot,
                            face_normal(document, face))) {
                        record_support_face_edges(support_face);
                    }
                }
            }
        }
    }

    for (const auto& [vertex_id, selected_count] : selected_edge_counts) {
        if (selected_count <= 1 || !offsets_by_vertex.contains(vertex_id)) {
            continue;
        }
        const bool fully_selected = fully_selected_vertices.contains(vertex_id) && fully_selected_vertices.at(vertex_id);
        if (selected_count == 2 && !fully_selected) {
            continue;
        }

        std::vector<EdgeBevelCornerArc> corner_arcs;
        std::vector<std::pair<ElementId, quader::QVec3>> patch_points;
        for (const EdgeBevelBuild& build : builds) {
            int endpoint_index = -1;
            if (build.edge.a == vertex_id) {
                endpoint_index = 0;
            } else if (build.edge.b == vertex_id) {
                endpoint_index = 1;
            }
            if (endpoint_index < 0) {
                continue;
            }
            EdgeBevelCornerArc arc;
            arc.edge = build.edge;
            arc.profile_middle = profile_middle_for_endpoint(build, vertex_id);
            arc.has_profile_middle = true;
            arc.vertices.reserve(build.rows.size());
            for (const std::array<ElementId, 2>& row : build.rows) {
                const ElementId patch_vertex_id = row[static_cast<std::size_t>(endpoint_index)];
                arc.vertices.push_back(patch_vertex_id);
                const Vertex* patch_vertex = find_vertex(candidate, patch_vertex_id);
                if (patch_vertex != nullptr) {
                    add_unique_edge_bevel_patch_vertex(patch_points, patch_vertex_id, patch_vertex->position);
                }
            }
            if (arc.vertices.size() == static_cast<std::size_t>(settings.segments + 1)) {
                corner_arcs.push_back(std::move(arc));
            }
        }
        if (const auto patch_miter_arcs = patch_miter_arcs_by_vertex.find(vertex_id);
            patch_miter_arcs != patch_miter_arcs_by_vertex.end()) {
            for (const EdgeBevelCornerArc& arc : patch_miter_arcs->second) {
                for (const ElementId patch_vertex_id : arc.vertices) {
                    const Vertex* patch_vertex = find_vertex(candidate, patch_vertex_id);
                    if (patch_vertex != nullptr) {
                        add_unique_edge_bevel_patch_vertex(patch_points, patch_vertex_id, patch_vertex->position);
                    }
                }
                corner_arcs.push_back(arc);
            }
        }

        std::vector<ElementId> patch_vertices = ordered_edge_bevel_patch_vertices(candidate, vertex_id, std::move(patch_points));
        patch_vertices = compact_edge_bevel_face_loop(std::move(patch_vertices));
        if (patch_vertices.size() < 3) {
            continue;
        }

        std::uint32_t material_slot = 0;
        if (const auto offsets = offsets_by_vertex.find(vertex_id); offsets != offsets_by_vertex.end() && !offsets->second.empty()) {
            if (const Face* face = find_face(document, offsets->second.front().source_face_id)) {
                material_slot = face->material_slot;
            }
        }

        Face patch_face;
        patch_face.vertices = patch_vertices;
        const Vertex* source_vertex = find_vertex(candidate, vertex_id);
        quader::QVec3 expected_normal;
        if (source_vertex != nullptr) {
            const quader::QVec3 patch_centroid = face_centroid(candidate, patch_face);
            expected_normal = fully_selected ? source_vertex->position - patch_centroid : patch_centroid - source_vertex->position;
        }
        if (settings.segments > 1 &&
            append_edge_bevel_vmesh_corner_patch_faces(
                candidate,
                bevel_faces,
                generated_face_ids,
                corner_arcs,
                vertex_id,
                material_slot,
                expected_normal,
                settings.segments,
                settings.profile,
                settings.profile_type)) {
            continue;
        }
        if (!append_edge_bevel_corner_patch_faces(
                candidate,
                bevel_faces,
                generated_face_ids,
                std::move(patch_vertices),
                vertex_id,
                selected_count,
                material_slot,
                expected_normal,
                settings.segments,
                settings.profile,
                settings.profile_type)) {
            continue;
        }
    }

    auto append_terminal_open_patch_faces = [&]() {
        std::map<std::pair<ElementId, ElementId>, int> edge_counts =
            combined_face_edge_counts();
        const auto record_patch_face_edges = [&edge_counts](const std::vector<ElementId>& vertices) {
            for (std::size_t index = 0; index < vertices.size(); ++index) {
                const Edge edge = make_edge(vertices[index], vertices[(index + 1U) % vertices.size()]);
                ++edge_counts[edge_key(edge)];
            }
        };
        const auto append_local_loop = [&](ElementId source_vertex_id, std::vector<ElementId> loop) {
            loop = compact_edge_bevel_face_loop(std::move(loop));
            if (loop.size() < 3U) {
                return false;
            }

            std::uint32_t material_slot = 0;
            for (const EdgeBevelBuild& build : builds) {
                if (build.edge.a == source_vertex_id || build.edge.b == source_vertex_id) {
                    material_slot = build.sides[0].material_slot;
                    break;
                }
            }

            quader::QVec3 expected_normal;
            if (const Vertex* source_vertex = find_vertex(candidate, source_vertex_id)) {
                Face probe;
                probe.vertices = loop;
                expected_normal = face_centroid(candidate, probe) - source_vertex->position;
            }
            if (length_squared(expected_normal) <= kEpsilon) {
                Face probe;
                probe.vertices = loop;
                expected_normal = face_normal(candidate, probe);
            }
            if (length_squared(expected_normal) <= kEpsilon) {
                return false;
            }

            if (!append_edge_bevel_face(
                    candidate,
                    bevel_faces,
                    generated_face_ids,
                    loop,
                    material_slot,
                    expected_normal)) {
                return false;
            }
            record_patch_face_edges(loop);
            return true;
        };

        std::map<ElementId, std::vector<ElementId>> local_profiles_by_vertex =
            terminal_profiles_by_vertex;
        for (const auto& [vertex_id, selected_count] : selected_edge_counts) {
            if (selected_count <= 1 || !offsets_by_vertex.contains(vertex_id)) {
                continue;
            }
            auto& profile_vertices = local_profiles_by_vertex[vertex_id];
            for (const EdgeBevelBuild& build : builds) {
                if (build.edge.a != vertex_id && build.edge.b != vertex_id) {
                    continue;
                }
                const std::vector<ElementId> endpoint_profile =
                    edge_bevel_endpoint_profile_vertices(build, vertex_id);
                for (const ElementId profile_vertex_id : endpoint_profile) {
                    add_unique_id(profile_vertices, profile_vertex_id);
                }
            }
        }

        const auto expand_local_open_edge_component = [&edge_counts](std::set<ElementId>& local_vertex_ids) {
            bool changed = true;
            while (changed) {
                changed = false;
                for (const auto& [edge_pair, count] : edge_counts) {
                    if (count != 1) {
                        continue;
                    }
                    const bool has_first = local_vertex_ids.contains(edge_pair.first);
                    const bool has_second = local_vertex_ids.contains(edge_pair.second);
                    if (has_first == has_second) {
                        continue;
                    }
                    local_vertex_ids.insert(has_first ? edge_pair.second : edge_pair.first);
                    changed = true;
                }
            }
        };

        for (const auto& [vertex_id, profile_vertices] : local_profiles_by_vertex) {
            if (profile_vertices.size() < 2U) {
                continue;
            }

            std::set<ElementId> local_vertex_ids(profile_vertices.begin(), profile_vertices.end());
            if (const auto offsets = offsets_by_vertex.find(vertex_id); offsets != offsets_by_vertex.end()) {
                for (const EdgeBevelFaceVertexOffset& offset : offsets->second) {
                    if (offset.offset_vertex_id != kInvalidElementId) {
                        local_vertex_ids.insert(offset.offset_vertex_id);
                    }
                }
            }
            for (const auto& [terminal_edge, boundary_vertex_id] : terminal_boundary_vertices_by_edge) {
                if (terminal_edge.first == vertex_id && boundary_vertex_id != kInvalidElementId) {
                    local_vertex_ids.insert(boundary_vertex_id);
                }
            }
            if (local_vertex_ids.size() < 3U) {
                continue;
            }
            expand_local_open_edge_component(local_vertex_ids);

            std::vector<Edge> local_open_edges;
            for (const auto& [edge_pair, count] : edge_counts) {
                if (count != 1) {
                    continue;
                }
                if (!local_vertex_ids.contains(edge_pair.first) ||
                    !local_vertex_ids.contains(edge_pair.second)) {
                    continue;
                }
                local_open_edges.push_back(make_edge(edge_pair.first, edge_pair.second));
            }
            if (local_open_edges.size() < 3U) {
                continue;
            }

            std::set<std::pair<ElementId, ElementId>> remaining_edges;
            for (const Edge& edge : local_open_edges) {
                remaining_edges.insert(edge_key(edge));
            }
            while (!remaining_edges.empty()) {
                const std::pair first_edge_key = *remaining_edges.begin();
                std::vector<ElementId> stack { first_edge_key.first, first_edge_key.second };
                std::vector<Edge> component_edges;
                while (!stack.empty()) {
                    const ElementId current_id = stack.back();
                    stack.pop_back();
                    for (auto edge = remaining_edges.begin(); edge != remaining_edges.end();) {
                        if (edge->first != current_id && edge->second != current_id) {
                            ++edge;
                            continue;
                        }
                        const ElementId next_id = edge->first == current_id ? edge->second : edge->first;
                        component_edges.push_back(make_edge(edge->first, edge->second));
                        edge = remaining_edges.erase(edge);
                        stack.push_back(next_id);
                    }
                }

                const std::optional<std::vector<ElementId>> loop =
                    closed_edge_loop_from_edges(component_edges);
                if (!loop.has_value()) {
                    continue;
                }
                append_local_loop(vertex_id, *loop);
            }
        }
    };

    append_terminal_open_patch_faces();

    rebuilt_faces.insert(rebuilt_faces.end(), bevel_faces.begin(), bevel_faces.end());
    candidate.faces = std::move(rebuilt_faces);
    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    restore_source_face_orientation(document, candidate);
    orient_generated_faces_from_source_adjacency(document, candidate);
    restore_source_face_orientation(document, candidate);

    std::vector<ElementId> selected_generated_faces;
    selected_generated_faces.reserve(generated_face_ids.size());
    for (const ElementId face_id : generated_face_ids) {
        if (find_face(candidate, face_id) != nullptr) {
            add_unique_id(selected_generated_faces, face_id);
        }
    }
    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(selected_generated_faces);
    activate_last_selection(selection);
    return { true, {} };
}

BevelEdgesOperation::BevelEdgesOperation(EdgeBevelSettings settings) : settings_(settings) {}

OperationResult BevelEdgesOperation::apply(Document& document, Selection& selection) const
{
    QDR_PROFILE_SCOPE("qdr_document.BevelEdgesOperation.apply");
    return bevel_selected_edges_impl(document, selection, settings_);
}

} // namespace

OperationResult bevel_selected_edges(Document& document, Selection& selection, const EdgeBevelSettings& requested_settings)
{
    return BevelEdgesOperation { requested_settings }.apply(document, selection);
}
} // namespace quader_poly
