////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.h>
#include <mesh/polygon/poly_operation.h>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.h>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.h>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <utility>
#include <vector>

#include <string_view>

namespace quader_poly {

/**
 * Implements the Slice Quads Operation modeling operation for the polygon document and mesh editing core.
 */
class SliceQuadsOperation final : public PolyOperation {
public:
    SliceQuadsOperation(int x_slices = 1, int y_slices = 1);

    [[nodiscard]] std::string_view id() const override { return "slice_selected_quads"; }
    [[nodiscard]] std::string_view label() const override { return "Slice Selected Quads"; }
    [[nodiscard]] OperationResult apply(Document& document, Selection& selection) const override;

private:
    int x_slices_ = 1;
    int y_slices_ = 1;
};

using namespace document_internal;

namespace {
int normalize_quad_slice_count(int count)
{
    return std::clamp(count, 0, 64);
}

const Face* find_original_face(const std::vector<Face>& faces, ElementId face_id)
{
    const auto found = std::ranges::find_if(faces, [face_id](const Face& face) {
        return face.id == face_id;
    });
    return found == faces.end() ? nullptr : &*found;
}

/**
 * Represents a Quad Slice Basis value used by the polygon document and mesh editing core.
 */
struct QuadSliceBasis {
    std::array<ElementId, 4> vertices {};
    std::array<Vec3, 4> positions {};
};

/**
 * Represents an Edge Split Point value used by the polygon document and mesh editing core.
 */
struct EdgeSplitPoint {
    float factor = 0.0F;
    ElementId vertex_id = invalid_element_id;
};

using EdgeSplitMap = std::map<std::pair<ElementId, ElementId>, std::vector<EdgeSplitPoint>>;

std::pair<ElementId, ElementId> edge_split_key(ElementId a, ElementId b)
{
    const Edge edge = make_edge(a, b);
    return { edge.a, edge.b };
}

std::optional<QuadSliceBasis> stable_quad_slice_basis(const Document& document, const Face& face)
{
    if (face.vertices.size() != 4U) {
        return std::nullopt;
    }

    std::array<Vertex, 4> source_vertices {};
    Vec3 center;
    for (std::size_t index = 0; index < source_vertices.size(); ++index) {
        const ElementId vertex_id = face.vertices[index];
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return std::nullopt;
        }
        source_vertices[index] = *vertex;
        center += vertex->position;
    }
    center = center / 4.0F;

    const Vec3 normal = normalize_or_zero(face_normal(document, face));
    if (length_squared(normal) <= epsilon) {
        return std::nullopt;
    }

    const std::array<Vec3, 3> world_axes {
        Vec3 { 1.0F, 0.0F, 0.0F },
        Vec3 { 0.0F, 1.0F, 0.0F },
        Vec3 { 0.0F, 0.0F, 1.0F },
    };
    const auto project_to_plane = [normal](Vec3 axis) {
        return axis - (normal * dot(axis, normal));
    };

    std::size_t u_axis_index = 0;
    float best_u_length = -1.0F;
    std::array<Vec3, 3> projected_axes {};
    for (std::size_t index = 0; index < world_axes.size(); ++index) {
        projected_axes[index] = project_to_plane(world_axes[index]);
        const float projected_length = length_squared(projected_axes[index]);
        if (projected_length > best_u_length) {
            best_u_length = projected_length;
            u_axis_index = index;
        }
    }
    if (best_u_length <= epsilon) {
        return std::nullopt;
    }

    Vec3 u_axis = normalize_or_zero(projected_axes[u_axis_index]);
    std::size_t v_world_axis_index = u_axis_index == 0U ? 1U : 0U;
    float best_v_length = -1.0F;
    for (std::size_t index = 0; index < projected_axes.size(); ++index) {
        if (index == u_axis_index) {
            continue;
        }
        const float projected_length = length_squared(projected_axes[index]);
        if (projected_length > best_v_length) {
            best_v_length = projected_length;
            v_world_axis_index = index;
        }
    }
    Vec3 v_axis = normalize_or_zero(cross(normal, u_axis));
    if (length_squared(v_axis) <= epsilon) {
        return std::nullopt;
    }
    const Vec3 preferred_v_axis = normalize_or_zero(projected_axes[v_world_axis_index]);
    if (length_squared(preferred_v_axis) > epsilon && dot(v_axis, preferred_v_axis) < 0.0F) {
        v_axis = v_axis * -1.0F;
    }

    /**
     * Represents a Scored Vertex value used by the polygon document and mesh editing core.
     */
    struct ScoredVertex {
        ElementId id = invalid_element_id;
        Vec3 position;
        float u = 0.0F;
        float v = 0.0F;
    };

    std::array<ScoredVertex, 4> scored {};
    for (std::size_t index = 0; index < source_vertices.size(); ++index) {
        const Vec3 offset = source_vertices[index].position - center;
        scored[index] = {
            source_vertices[index].id,
            source_vertices[index].position,
            dot(offset, u_axis),
            dot(offset, v_axis),
        };
    }

    std::ranges::sort(scored, [](const ScoredVertex& left, const ScoredVertex& right) {
        if (std::abs(left.v - right.v) > epsilon) {
            return left.v < right.v;
        }
        return left.u < right.u;
    });
    if (scored[0].u > scored[1].u) {
        std::swap(scored[0], scored[1]);
    }
    if (scored[2].u > scored[3].u) {
        std::swap(scored[2], scored[3]);
    }

    QuadSliceBasis basis;
    basis.vertices = { scored[0].id, scored[1].id, scored[3].id, scored[2].id };
    basis.positions = { scored[0].position, scored[1].position, scored[3].position, scored[2].position };
    return basis;
}

Vec3 bilinear_quad_point(Vec3 a, Vec3 b, Vec3 c, Vec3 d, float x_factor, float y_factor)
{
    const Vec3 top = a + ((b - a) * x_factor);
    const Vec3 bottom = d + ((c - d) * x_factor);
    return top + ((bottom - top) * y_factor);
}

ElementId boundary_split_vertex(
    Document& document,
    EdgeSplitMap& edge_splits,
    ElementId from,
    ElementId to,
    float factor_from_start,
    Vec3 position)
{
    if (factor_from_start <= epsilon) {
        return from;
    }
    if (factor_from_start >= 1.0F - epsilon) {
        return to;
    }

    const Edge edge = make_edge(from, to);
    const float factor = edge.a == from ? factor_from_start : 1.0F - factor_from_start;
    std::vector<EdgeSplitPoint>& split_points = edge_splits[edge_split_key(edge.a, edge.b)];
    for (const EdgeSplitPoint& split_point : split_points) {
        if (std::abs(split_point.factor - factor) <= 0.0001F) {
            return split_point.vertex_id;
        }
    }

    const ElementId vertex_id = add_vertex(document, position);
    split_points.push_back({ factor, vertex_id });
    return vertex_id;
}

ElementId boundary_grid_vertex(
    Document& document,
    EdgeSplitMap& edge_splits,
    const QuadSliceBasis& basis,
    int x,
    int y,
    int columns,
    int rows,
    Vec3 position)
{
    if (y == 0) {
        return boundary_split_vertex(
            document,
            edge_splits,
            basis.vertices[0],
            basis.vertices[1],
            static_cast<float>(x) / static_cast<float>(columns),
            position);
    }
    if (x == columns) {
        return boundary_split_vertex(
            document,
            edge_splits,
            basis.vertices[1],
            basis.vertices[2],
            static_cast<float>(y) / static_cast<float>(rows),
            position);
    }
    if (y == rows) {
        return boundary_split_vertex(
            document,
            edge_splits,
            basis.vertices[3],
            basis.vertices[2],
            static_cast<float>(x) / static_cast<float>(columns),
            position);
    }
    if (x == 0) {
        return boundary_split_vertex(
            document,
            edge_splits,
            basis.vertices[0],
            basis.vertices[3],
            static_cast<float>(y) / static_cast<float>(rows),
            position);
    }
    return invalid_element_id;
}

std::vector<ElementId> split_vertices_for_face_edge(
    const EdgeSplitMap& edge_splits,
    ElementId from,
    ElementId to)
{
    const Edge edge = make_edge(from, to);
    const auto found = edge_splits.find(edge_split_key(edge.a, edge.b));
    if (found == edge_splits.end()) {
        return {};
    }

    std::vector<EdgeSplitPoint> split_points = found->second;
    std::ranges::sort(split_points, [from, edge](const EdgeSplitPoint& left, const EdgeSplitPoint& right) {
        return edge.a == from ? left.factor < right.factor : left.factor > right.factor;
    });

    std::vector<ElementId> vertices;
    vertices.reserve(split_points.size());
    for (const EdgeSplitPoint& split_point : split_points) {
        if (split_point.factor <= epsilon || split_point.factor >= 1.0F - epsilon) {
            continue;
        }
        vertices.push_back(split_point.vertex_id);
    }
    return vertices;
}

void propagate_boundary_splits_to_neighbor_faces(
    Document& document,
    const std::set<ElementId>& selected_face_ids,
    const EdgeSplitMap& edge_splits)
{
    if (edge_splits.empty()) {
        return;
    }

    for (Face& face : document.faces) {
        if (selected_face_ids.contains(face.id) || face.vertices.size() < 2U) {
            continue;
        }

        std::vector<ElementId> expanded_vertices;
        expanded_vertices.reserve(face.vertices.size());
        bool changed = false;
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const ElementId from = face.vertices[index];
            const ElementId to = face.vertices[(index + 1U) % face.vertices.size()];
            expanded_vertices.push_back(from);
            const std::vector<ElementId> split_vertices = split_vertices_for_face_edge(edge_splits, from, to);
            if (!split_vertices.empty()) {
                changed = true;
                expanded_vertices.insert(expanded_vertices.end(), split_vertices.begin(), split_vertices.end());
            }
        }

        if (changed) {
            face.vertices = std::move(expanded_vertices);
            face.uvs.clear();
        }
    }
}

OperationResult slice_selected_quads_impl(Document& document, Selection& selection, int x_slices, int y_slices)
{
    if (selection.mode != SelectionMode::face || selection.faces.empty()) {
        return { false, "Select quad faces before slicing." };
    }

    x_slices = normalize_quad_slice_count(x_slices);
    y_slices = normalize_quad_slice_count(y_slices);
    if (x_slices == 0 && y_slices == 0) {
        return { false, "Slice Quad needs at least one slice." };
    }

    const int columns = x_slices + 1;
    const int rows = y_slices + 1;
    Document candidate = document;
    const std::vector<Face> original_faces = candidate.faces;
    std::set<ElementId> selected_face_ids;
    for (ElementId face_id : selection.faces) {
        if (face_id != invalid_element_id) {
            selected_face_ids.insert(face_id);
        }
    }

    std::vector<Face> sliced_faces;
    std::vector<ElementId> generated_face_ids;
    EdgeSplitMap boundary_edge_splits;
    bool found_selected_face = false;
    for (ElementId face_id : selected_face_ids) {
        const Face* face = find_original_face(original_faces, face_id);
        if (face == nullptr) {
            continue;
        }
        found_selected_face = true;
        if (face->vertices.size() != 4U) {
            return { false, "Slice Quad only works on selected quad faces." };
        }

        const std::optional<QuadSliceBasis> slice_basis = stable_quad_slice_basis(candidate, *face);
        if (!slice_basis.has_value()) {
            return { false, "Slice Quad could not resolve the selected quad vertices." };
        }
        const Vec3 source_normal = face_normal(candidate, *face);

        std::vector<ElementId> grid(static_cast<std::size_t>(columns + 1) * static_cast<std::size_t>(rows + 1), invalid_element_id);
        const auto grid_index = [columns](int x, int y) {
            return static_cast<std::size_t>(y * (columns + 1) + x);
        };
        for (int y = 0; y <= rows; ++y) {
            for (int x = 0; x <= columns; ++x) {
                ElementId vertex_id = invalid_element_id;
                if (x == 0 && y == 0) {
                    vertex_id = slice_basis->vertices[0];
                } else if (x == columns && y == 0) {
                    vertex_id = slice_basis->vertices[1];
                } else if (x == columns && y == rows) {
                    vertex_id = slice_basis->vertices[2];
                } else if (x == 0 && y == rows) {
                    vertex_id = slice_basis->vertices[3];
                } else {
                    const float x_factor = static_cast<float>(x) / static_cast<float>(columns);
                    const float y_factor = static_cast<float>(y) / static_cast<float>(rows);
                    const Vec3 position =
                        bilinear_quad_point(
                            slice_basis->positions[0],
                            slice_basis->positions[1],
                            slice_basis->positions[2],
                            slice_basis->positions[3],
                            x_factor,
                            y_factor);
                    vertex_id = (x == 0 || x == columns || y == 0 || y == rows) ?
                        boundary_grid_vertex(candidate, boundary_edge_splits, *slice_basis, x, y, columns, rows, position) :
                        add_vertex(candidate, position);
                }
                grid[grid_index(x, y)] = vertex_id;
            }
        }

        bool used_source_face_id = false;
        for (int y = 0; y < rows; ++y) {
            for (int x = 0; x < columns; ++x) {
                Face sliced_face;
                sliced_face.id = used_source_face_id ? candidate.next_face_id++ : face->id;
                used_source_face_id = true;
                sliced_face.vertices = {
                    grid[grid_index(x, y)],
                    grid[grid_index(x + 1, y)],
                    grid[grid_index(x + 1, y + 1)],
                    grid[grid_index(x, y + 1)],
                };
                sliced_face.material_slot = face->material_slot;
                sliced_face.uvs.clear();
                orient_face_toward_normal(candidate, sliced_face, source_normal);
                generated_face_ids.push_back(sliced_face.id);
                sliced_faces.push_back(std::move(sliced_face));
            }
        }
    }

    if (!found_selected_face || sliced_faces.empty()) {
        return { false, "Select quad faces before slicing." };
    }

    propagate_boundary_splits_to_neighbor_faces(candidate, selected_face_ids, boundary_edge_splits);
    std::erase_if(candidate.faces, [&selected_face_ids](const Face& face) {
        return selected_face_ids.contains(face.id);
    });
    candidate.faces.insert(candidate.faces.end(), sliced_faces.begin(), sliced_faces.end());
    prune_invalid_faces(candidate);
    prune_unused_vertices(candidate);
    restore_source_face_orientation(document, candidate);
    if (!every_face_triangulates(candidate)) {
        return { false, "Slice Quad would create invalid face geometry." };
    }

    document = std::move(candidate);
    selection.clear();
    selection.mode = SelectionMode::face;
    selection.faces = std::move(generated_face_ids);
    activate_last_selection(selection);
    return { true, {} };
}



} // namespace

SliceQuadsOperation::SliceQuadsOperation(int x_slices, int y_slices) :
    x_slices_(x_slices),
    y_slices_(y_slices)
{}

OperationResult SliceQuadsOperation::apply(Document& document, Selection& selection) const
{
    return slice_selected_quads_impl(document, selection, x_slices_, y_slices_);
}

OperationResult slice_selected_quads(Document& document, Selection& selection, int x_slices, int y_slices)
{
    return SliceQuadsOperation { x_slices, y_slices }.apply(document, selection);
}
} // namespace quader_poly
