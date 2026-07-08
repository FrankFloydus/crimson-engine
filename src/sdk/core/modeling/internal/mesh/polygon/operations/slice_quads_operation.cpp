////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/document.hpp>
#include <mesh/polygon/poly_operation.hpp>
#include <mesh/polygon/poly_operation_descriptor.hpp>

#include <mesh/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_knife_helpers.hpp>
#include <mesh/polygon/internal/quader_poly_document_topology_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <set>
#include <span>
#include <string>
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

    [[nodiscard]] std::string_view id() const override { return poly_operation_descriptor(PolyOperationKey::SliceSelectedQuads).id; }
    [[nodiscard]] std::string_view label() const override { return poly_operation_descriptor(PolyOperationKey::SliceSelectedQuads).label; }
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
 * Stores a projected vertex in a stable face-local Slice Quad grid.
 */
struct SliceGridVertex {
  ElementId id = kInvalidElementId;
  quader::QVec3 position;
  float u = 0.0F;
  float v = 0.0F;
};

/**
 * Stores the stable planar frame used to generate Slice Quad grid cuts.
 */
struct SliceFaceFrame {
  ElementId face_id = kInvalidElementId;
  quader::QVec3 center;
  quader::QVec3 normal;
  quader::QVec3 u_axis;
  quader::QVec3 v_axis;
  std::vector<SliceGridVertex> vertices;
  float min_u = 0.0F;
  float max_u = 0.0F;
  float min_v = 0.0F;
  float max_v = 0.0F;
};

/**
 * Stores a clipped Slice Quad grid line endpoint.
 */
struct SliceLineEndpoint {
    float along = 0.0F;
    quader::QVec3 position;
};

std::optional<SliceFaceFrame> stable_slice_face_frame(const Document& document, const Face& face)
{
    if (face.vertices.size() < 3U) {
        return std::nullopt;
    }

    quader::QVec3 center;
    std::vector<Vertex> source_vertices;
    source_vertices.reserve(face.vertices.size());
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return std::nullopt;
        }
        source_vertices.push_back(*vertex);
        center += vertex->position;
    }
    center = center / static_cast<float>(source_vertices.size());

    const quader::QVec3 normal = normalize_or_zero(face_normal(document, face));
    if (length_squared(normal) <= kEpsilon) {
      return std::nullopt;
    }

    const std::array<quader::QVec3, 3> world_axes {
        quader::QVec3 { 1.0F, 0.0F, 0.0F },
        quader::QVec3 { 0.0F, 1.0F, 0.0F },
        quader::QVec3 { 0.0F, 0.0F, 1.0F },
    };
    const auto project_to_plane = [normal](quader::QVec3 axis) {
        return axis - (normal * dot(axis, normal));
    };

    std::size_t u_axis_index = 0;
    float best_u_length = -1.0F;
    std::array<quader::QVec3, 3> projected_axes {};
    for (std::size_t index = 0; index < world_axes.size(); ++index) {
        projected_axes[index] = project_to_plane(world_axes[index]);
        const float projected_length = length_squared(projected_axes[index]);
        if (projected_length > best_u_length) {
            best_u_length = projected_length;
            u_axis_index = index;
        }
    }
    if (best_u_length <= kEpsilon) {
      return std::nullopt;
    }

    quader::QVec3 u_axis = normalize_or_zero(projected_axes[u_axis_index]);
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
    quader::QVec3 v_axis = normalize_or_zero(cross(normal, u_axis));
    if (length_squared(v_axis) <= kEpsilon) {
      return std::nullopt;
    }
    const quader::QVec3 preferred_v_axis = normalize_or_zero(projected_axes[v_world_axis_index]);
    if (length_squared(preferred_v_axis) > kEpsilon &&
        dot(v_axis, preferred_v_axis) < 0.0F) {
      v_axis = v_axis * -1.0F;
    }

    SliceFaceFrame frame;
    frame.face_id = face.id;
    frame.center = center;
    frame.normal = normal;
    frame.u_axis = u_axis;
    frame.v_axis = v_axis;
    frame.vertices.reserve(source_vertices.size());
    bool initialized_bounds = false;
    for (const Vertex& vertex : source_vertices) {
        const quader::QVec3 offset = vertex.position - center;
        if (std::abs(dot(offset, normal)) > 0.001F) {
            return std::nullopt;
        }
        const float u = dot(offset, u_axis);
        const float v = dot(offset, v_axis);
        frame.vertices.push_back({ vertex.id, vertex.position, u, v });
        if (!initialized_bounds) {
            frame.min_u = frame.max_u = u;
            frame.min_v = frame.max_v = v;
            initialized_bounds = true;
        } else {
            frame.min_u = std::min(frame.min_u, u);
            frame.max_u = std::max(frame.max_u, u);
            frame.min_v = std::min(frame.min_v, v);
            frame.max_v = std::max(frame.max_v, v);
        }
    }
    if (frame.max_u - frame.min_u <= kEpsilon ||
        frame.max_v - frame.min_v <= kEpsilon) {
      return std::nullopt;
    }
    return frame;
}

bool point_on_segment_2d(float px, float py, float ax, float ay, float bx, float by)
{
    const float ab_x = bx - ax;
    const float ab_y = by - ay;
    const float ap_x = px - ax;
    const float ap_y = py - ay;
    const float cross_2d = (ab_x * ap_y) - (ab_y * ap_x);
    if (std::abs(cross_2d) > 0.0001F) {
        return false;
    }
    const float dot_2d = (ap_x * ab_x) + (ap_y * ab_y);
    if (dot_2d < -0.0001F) {
        return false;
    }
    const float length_2d = (ab_x * ab_x) + (ab_y * ab_y);
    return dot_2d <= length_2d + 0.0001F;
}

bool point_on_frame_boundary(const SliceFaceFrame& frame, float u, float v)
{
    for (std::size_t index = 0; index < frame.vertices.size(); ++index) {
        const SliceGridVertex& a = frame.vertices[index];
        const SliceGridVertex& b = frame.vertices[(index + 1U) % frame.vertices.size()];
        if (point_on_segment_2d(u, v, a.u, a.v, b.u, b.v)) {
            return true;
        }
    }
    return false;
}

bool point_in_or_on_frame(const SliceFaceFrame& frame, float u, float v)
{
    if (point_on_frame_boundary(frame, u, v)) {
        return true;
    }

    bool inside = false;
    for (std::size_t index = 0, previous = frame.vertices.size() - 1U;
            index < frame.vertices.size();
            previous = index++) {
        const SliceGridVertex& a = frame.vertices[index];
        const SliceGridVertex& b = frame.vertices[previous];
        const bool crosses = (a.v > v) != (b.v > v);
        if (!crosses) {
            continue;
        }
        const float intersect_u = ((b.u - a.u) * (v - a.v) / (b.v - a.v)) + a.u;
        if (u < intersect_u) {
            inside = !inside;
        }
    }
    return inside;
}

std::vector<SliceLineEndpoint> line_polygon_intersections(
    const SliceFaceFrame& frame,
    bool vertical,
    float coordinate)
{
    std::vector<SliceLineEndpoint> intersections;
    for (std::size_t index = 0; index < frame.vertices.size(); ++index) {
        const SliceGridVertex& a = frame.vertices[index];
        const SliceGridVertex& b = frame.vertices[(index + 1U) % frame.vertices.size()];
        const float a_cross = vertical ? a.u : a.v;
        const float b_cross = vertical ? b.u : b.v;
        const float a_along = vertical ? a.v : a.u;
        const float b_along = vertical ? b.v : b.u;
        const float a_delta = a_cross - coordinate;
        const float b_delta = b_cross - coordinate;

        if (std::abs(a_delta) <= 0.0001F && std::abs(b_delta) <= 0.0001F) {
            intersections.push_back({ a_along, a.position });
            intersections.push_back({ b_along, b.position });
            continue;
        }
        if (coordinate < std::min(a_cross, b_cross) - 0.0001F ||
                coordinate > std::max(a_cross, b_cross) + 0.0001F ||
                std::abs(b_cross - a_cross) <= 0.0001F) {
            continue;
        }

        const float factor = std::clamp((coordinate - a_cross) / (b_cross - a_cross), 0.0F, 1.0F);
        const float along = a_along + ((b_along - a_along) * factor);
        intersections.push_back({ along, a.position + ((b.position - a.position) * factor) });
    }

    std::ranges::sort(intersections, [](const SliceLineEndpoint& left, const SliceLineEndpoint& right) {
        return left.along < right.along;
    });
    std::vector<SliceLineEndpoint> unique;
    unique.reserve(intersections.size());
    for (const SliceLineEndpoint& endpoint : intersections) {
        if (!unique.empty() && std::abs(unique.back().along - endpoint.along) <= 0.0001F) {
            continue;
        }
        unique.push_back(endpoint);
    }
    return unique;
}

void add_grid_segment_targets(
    const SliceFaceFrame& frame,
    const SliceLineEndpoint& first,
    const SliceLineEndpoint& second,
    std::vector<KnifePointTarget>& targets,
    std::vector<KnifeStrokeSegment>& segments)
{
  if (length_squared(second.position - first.position) <= kEpsilon) {
    return;
  }
    const std::uint32_t first_index = static_cast<std::uint32_t>(targets.size());
    targets.push_back({
        .kind = KnifePointTargetKind::FacePoint,
        .face_id = frame.face_id,
        .position = first.position,
    });
    const std::uint32_t second_index = static_cast<std::uint32_t>(targets.size());
    targets.push_back({
        .kind = KnifePointTargetKind::FacePoint,
        .face_id = frame.face_id,
        .position = second.position,
    });
    segments.push_back({ first_index, second_index });
}

std::size_t append_grid_line_segments(
    const SliceFaceFrame& frame,
    bool vertical,
    float coordinate,
    std::vector<KnifePointTarget>& targets,
    std::vector<KnifeStrokeSegment>& segments)
{
    const std::size_t segment_count_before = segments.size();
    const std::vector<SliceLineEndpoint> intersections = line_polygon_intersections(frame, vertical, coordinate);
    for (std::size_t index = 0; index + 1U < intersections.size(); ++index) {
        const SliceLineEndpoint& first = intersections[index];
        const SliceLineEndpoint& second = intersections[index + 1U];
        if (second.along - first.along <= 0.0001F) {
            continue;
        }
        const float midpoint_along = (first.along + second.along) * 0.5F;
        const float midpoint_u = vertical ? coordinate : midpoint_along;
        const float midpoint_v = vertical ? midpoint_along : coordinate;
        if (!point_in_or_on_frame(frame, midpoint_u, midpoint_v) ||
                point_on_frame_boundary(frame, midpoint_u, midpoint_v)) {
            continue;
        }
        add_grid_segment_targets(frame, first, second, targets, segments);
    }
    return segments.size() - segment_count_before;
}

std::size_t append_grid_segments(
    const SliceFaceFrame& frame,
    int x_slices,
    int y_slices,
    std::vector<KnifePointTarget>& targets,
    std::vector<KnifeStrokeSegment>& segments)
{
    std::size_t added_segments = 0;
    const int columns = x_slices + 1;
    const int rows = y_slices + 1;
    for (int column = 1; column < columns; ++column) {
        const float factor = static_cast<float>(column) / static_cast<float>(columns);
        const float u = frame.min_u + ((frame.max_u - frame.min_u) * factor);
        added_segments += append_grid_line_segments(frame, true, u, targets, segments);
    }
    for (int row = 1; row < rows; ++row) {
        const float factor = static_cast<float>(row) / static_cast<float>(rows);
        const float v = frame.min_v + ((frame.max_v - frame.min_v) * factor);
        added_segments += append_grid_line_segments(frame, false, v, targets, segments);
    }
    return added_segments;
}

std::optional<std::pair<float, float>> project_to_frame(const SliceFaceFrame& frame, quader::QVec3 position)
{
    const quader::QVec3 offset = position - frame.center;
    if (std::abs(dot(offset, frame.normal)) > 0.001F) {
        return std::nullopt;
    }
    return std::pair<float, float> { dot(offset, frame.u_axis), dot(offset, frame.v_axis) };
}

bool face_lies_inside_source_frame(const Document& document, const Face& face, const SliceFaceFrame& frame)
{
    if (face.vertices.size() < 3U) {
        return false;
    }

    quader::QVec3 center;
    std::size_t valid_vertex_count = 0;
    for (const ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        if (vertex == nullptr) {
            return false;
        }
        const std::optional<std::pair<float, float>> projected = project_to_frame(frame, vertex->position);
        if (!projected.has_value() || !point_in_or_on_frame(frame, projected->first, projected->second)) {
            return false;
        }
        center += vertex->position;
        ++valid_vertex_count;
    }
    center = center / static_cast<float>(valid_vertex_count);
    const std::optional<std::pair<float, float>> projected_center = project_to_frame(frame, center);
    return projected_center.has_value() &&
        point_in_or_on_frame(frame, projected_center->first, projected_center->second);
}

std::vector<ElementId> generated_face_selection(const Document& document, std::span<const SliceFaceFrame> source_frames)
{
    std::vector<ElementId> face_ids;
    for (const Face& face : document.faces) {
        for (const SliceFaceFrame& frame : source_frames) {
            if (!face_lies_inside_source_frame(document, face, frame)) {
                continue;
            }
            if (std::find(face_ids.begin(), face_ids.end(), face.id) == face_ids.end()) {
                face_ids.push_back(face.id);
            }
            break;
        }
    }
    return face_ids;
}

OperationResult slice_selected_quads_impl(Document& document, Selection& selection, int x_slices, int y_slices)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return {false, "Select faces before slicing."};
  }

    x_slices = normalize_quad_slice_count(x_slices);
    y_slices = normalize_quad_slice_count(y_slices);
    if (x_slices == 0 && y_slices == 0) {
        return { false, "Slice Quad needs at least one slice." };
    }

    const std::vector<Face> original_faces = document.faces;
    std::set<ElementId> selected_face_ids;
    for (ElementId face_id : selection.faces) {
      if (face_id != kInvalidElementId) {
        selected_face_ids.insert(face_id);
      }
    }

    std::vector<SliceFaceFrame> source_frames;
    std::vector<KnifePointTarget> targets;
    std::vector<KnifeStrokeSegment> segments;
    bool found_selected_face = false;
    for (ElementId face_id : selected_face_ids) {
        const Face* face = find_original_face(original_faces, face_id);
        if (face == nullptr) {
            continue;
        }
        found_selected_face = true;
        std::optional<SliceFaceFrame> frame = stable_slice_face_frame(document, *face);
        if (!frame.has_value()) {
            return { false, "Slice Quad could not resolve the selected face plane." };
        }
        const std::size_t face_segment_count = append_grid_segments(*frame, x_slices, y_slices, targets, segments);
        if (face_segment_count == 0U) {
            return { false, "Slice Quad did not create any face cuts." };
        }
        source_frames.push_back(std::move(*frame));
    }

    if (!found_selected_face || targets.size() < 2U || segments.empty()) {
        return { false, "Select faces before slicing." };
    }

    KnifeStrokeCandidate candidate = build_knife_stroke_candidate(document, targets, segments);
    if (!candidate.changed) {
        return { false, candidate.message.empty() ? std::string("Slice Quad could not split the selected faces.") : candidate.message };
    }

    std::vector<ElementId> generated_face_ids = generated_face_selection(candidate.document, source_frames);
    if (generated_face_ids.empty()) {
        return { false, "Slice Quad could not resolve the generated face selection." };
    }

    document = std::move(candidate.document);
    selection.clear();
    selection.mode = SelectionMode::Face;
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
