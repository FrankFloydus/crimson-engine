////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>
#include <quader/modeling/neutral/polygon/document_texture.hpp>
#include <quader/modeling/neutral/geometry/geometry.hpp>

#include <diagnostics/profile.hpp>

#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bridge_surface_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_hull_plane_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_topology_helpers.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_uv_helpers.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>
#include <set>
#include <unordered_map>

namespace quader_poly {

namespace document_internal {
OperationResult component_extrusion_receipt(const Document& source, const Document& candidate);
}

using namespace document_internal;

namespace {

/** Controls face shading for the side region generated around a copied cap. */
enum class FaceSideShadingPolicy {
    Default,
    InheritSource,
};

/** Keeps affine extrusion calculations independent of float storage precision. */
struct ExtrudeVector {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    ExtrudeVector operator+(ExtrudeVector other) const {
        return {x + other.x, y + other.y, z + other.z};
    }
    ExtrudeVector operator-(ExtrudeVector other) const {
        return {x - other.x, y - other.y, z - other.z};
    }
    ExtrudeVector operator*(double scale) const {
        return {x * scale, y * scale, z * scale};
    }
    ExtrudeVector& operator+=(ExtrudeVector other) {
        *this = *this + other;
        return *this;
    }
};

ExtrudeVector extrusion_vector(quader::QVec3 value) {
    return {value.x, value.y, value.z};
}

double extrusion_dot(ExtrudeVector first, ExtrudeVector second) {
    return first.x * second.x + first.y * second.y + first.z * second.z;
}

ExtrudeVector extrusion_cross(ExtrudeVector first, ExtrudeVector second) {
    return {first.y * second.z - first.z * second.y,
            first.z * second.x - first.x * second.z,
            first.x * second.y - first.y * second.x};
}

bool extrusion_finite(ExtrudeVector value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

double extrusion_length(ExtrudeVector value) {
    return std::hypot(value.x, value.y, value.z);
}

bool normalize_extrusion_vector(ExtrudeVector& value, double minimum_length = 1.0e-12) {
    const double magnitude = extrusion_length(value);
    if (!std::isfinite(magnitude) || magnitude <= minimum_length) {
        return false;
    }
    value = value * (1.0 / magnitude);
    return true;
}

quader::QVec3 extrusion_storage_vector(ExtrudeVector value) {
    return {static_cast<float>(value.x), static_cast<float>(value.y),
            static_cast<float>(value.z)};
}

/** Measures a face in its own anchored metric, independent of size and location. */
bool extrusion_face_metric(std::span<const ExtrudeVector> positions,
                           ExtrudeVector& normal, double& scale)
{
    if (positions.size() < 3U) {
        return false;
    }
    const ExtrudeVector anchor = positions.front();
    scale = 0.0;
    for (const auto position : positions) {
        const auto relative = position - anchor;
        scale = std::max({scale, std::abs(relative.x), std::abs(relative.y), std::abs(relative.z)});
    }
    if (!std::isfinite(scale) || scale == 0.0) {
        return false;
    }
    normal = {};
    for (std::size_t index = 0; index < positions.size(); ++index) {
        normal += extrusion_cross((positions[index] - anchor) * (1.0 / scale),
            (positions[(index + 1U) % positions.size()] - anchor) * (1.0 / scale));
    }
    return normalize_extrusion_vector(normal);
}

OperationResult validate_extrusion_structure(const Document& document)
{
    for (const Vertex& vertex : document.vertices) {
        if (vertex.id == kInvalidElementId || !extrusion_finite(extrusion_vector(vertex.position))) {
            return OperationResult::failure("Extrusion requires finite vertices with valid identities.");
        }
    }
    for (const Face& face : document.faces) {
        if (face.id == kInvalidElementId ||
            std::ranges::any_of(face.uvs, [](const auto uv) {
                return !std::isfinite(uv.x) || !std::isfinite(uv.y);
            })) {
            return OperationResult::failure("Extrusion requires valid face identities and finite UVs.");
        }
    }
    // Authored topology retains geometric degeneracies, while rejecting malformed
    // connectivity and attributes. Thin/tangent side walls are valid authored faces.
    const auto validation = validate_polygon_document(
        document, PolygonDocumentValidationPolicy::AuthoredTopology);
    if (!validation.ok()) {
        const auto error = std::ranges::find_if(validation.diagnostics, [](const auto& diagnostic) {
            return diagnostic.severity == PolygonDocumentDiagnosticSeverity::Error;
        });
        return OperationResult::failure("Extrusion requires valid authored topology: " + error->message);
    }
    return {};
}

template <typename Vertices>
bool extrusion_cap_is_usable(const Face& face, const Vertices& vertices)
{
    std::vector<ExtrudeVector> positions;
    positions.reserve(face.vertices.size());
    for (const ElementId id : face.vertices) {
        const Vertex* vertex = vertices.find(id);
        if (vertex == nullptr) {
            return false;
        }
        positions.push_back(extrusion_vector(vertex->position));
    }
    ExtrudeVector normal;
    double scale = 0.0;
    if (!extrusion_face_metric(positions, normal, scale)) {
        return false;
    }
    // Reuse the triangulation owner in a normalized face-local coordinate frame.
    // Its absolute thresholds must not turn distant or uniformly small caps invalid.
    Document normalized;
    Face normalized_face;
    normalized.vertices.reserve(positions.size());
    normalized_face.vertices.reserve(positions.size());
    for (const auto position : positions) {
        normalized_face.vertices.push_back(add_vertex(normalized,
            extrusion_storage_vector((position - positions.front()) * (1.0 / scale))));
    }
    return !triangulate_face_local_indices(normalized, normalized_face,
        VertexLookupIndex(normalized), extrusion_storage_vector(normal)).empty();
}

/** Rejects crossed wall boundaries without treating flat or skew walls as crossings. */
template <typename Vertices>
bool extrusion_wall_has_proper_crossing(const Face& face, const Vertices& vertices)
{
    if (face.vertices.size() != 4U) {
        return false;
    }
    using Vector = quader_geometry::QVec3d;
    std::array<Vector, 4> positions;
    for (std::size_t index = 0; index < positions.size(); ++index) {
        const Vertex* vertex = vertices.find(face.vertices[index]);
        if (vertex == nullptr) {
            return true;
        }
        positions[index] = Vector{vertex->position};
    }
    const Vector anchor = positions.front();
    double scale = 0.0;
    for (Vector& position : positions) {
        position = position - anchor;
        scale = std::max({scale, std::abs(position.x), std::abs(position.y), std::abs(position.z)});
    }
    if (scale == 0.0) {
        return false;
    }
    for (Vector& position : positions) {
        position = position / scale;
    }
    const auto crosses = [&](std::size_t first, std::size_t second) {
        const quader_geometry::QSegment3<double> a{
            positions[first], positions[(first + 1U) % positions.size()]};
        const quader_geometry::QSegment3<double> b{
            positions[second], positions[(second + 1U) % positions.size()]};
        const Vector a_direction = a.b - a.a;
        const Vector b_direction = b.b - b.a;
        const double direction_product = quader_geometry::length_squared(a_direction) *
            quader_geometry::length_squared(b_direction);
        // Parallel overlap is the expected result of tangential extrusion.
        if (quader_geometry::length_squared(quader_geometry::cross(a_direction, b_direction)) <=
            direction_product * 1.0e-24) {
            return false;
        }
        const auto closest = quader_geometry::closest_points_segment_segment<double>(a, b, 1.0e-14);
        constexpr double kInteriorEpsilon = 1.0e-10;
        return closest.valid && closest.distance_squared <= 1.0e-20 &&
            closest.first_factor > kInteriorEpsilon && closest.first_factor < 1.0 - kInteriorEpsilon &&
            closest.second_factor > kInteriorEpsilon && closest.second_factor < 1.0 - kInteriorEpsilon;
    };
    return crosses(0U, 2U) || crosses(1U, 3U);
}

/** Caches selected source geometry before any cap or wall is authored. */
struct ExtrudeSourceFace {
    Face face;
    std::vector<ExtrudeVector> positions;
    ExtrudeVector normal;
};

/** Holds an invertible affine metric and its immutable selected source faces. */
struct PreparedFaceExtrusion {
    std::array<ExtrudeVector, 3> axes;
    std::array<ExtrudeVector, 3> inverse_rows;
    std::vector<ExtrudeSourceFace> faces;
    ExtrudeVector summed_normal;
    ExtrudeVector active_normal;
    ExtrudeVector center;
    std::size_t vertex_count = 0;
};

OperationResult prepare_extrusion_frame_data(
    const Document& document, const Selection& selection,
    const Transform3& transform, PreparedFaceExtrusion& prepared)
{
    prepared.axes = {extrusion_vector(transform.x_axis),
                     extrusion_vector(transform.y_axis),
                     extrusion_vector(transform.z_axis)};
    if (!extrusion_finite(extrusion_vector(transform.origin)) ||
        !std::ranges::all_of(prepared.axes, extrusion_finite)) {
        return OperationResult::failure("Extrusion transform must be finite.");
    }
    const auto& [x_axis, y_axis, z_axis] = prepared.axes;
    const double determinant = extrusion_dot(x_axis, extrusion_cross(y_axis, z_axis));
    const double determinant_scale = extrusion_length(x_axis) *
        extrusion_length(y_axis) * extrusion_length(z_axis);
    if (determinant_scale == 0.0 ||
        std::abs(determinant) <= determinant_scale * 1.0e-12) {
        return OperationResult::failure("Extrusion transform must be invertible.");
    }
    prepared.inverse_rows = {extrusion_cross(y_axis, z_axis) * (1.0 / determinant),
                             extrusion_cross(z_axis, x_axis) * (1.0 / determinant),
                             extrusion_cross(x_axis, y_axis) * (1.0 / determinant)};
    if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
        return OperationResult::failure("Select one or more faces to extrude.");
    }
    const VertexLookupIndex vertices(document);
    const std::set<ElementId> face_ids(selection.faces.begin(), selection.faces.end());
    const ElementId active = active_face_or_invalid(selection);
    std::set<ElementId> center_vertices;
    prepared.faces.reserve(face_ids.size());
    for (const Face& face : selected_face_copies(document, selection)) {
        if (face.vertices.size() < 3U) {
            return OperationResult::failure("Selected face has an invalid vertex loop.");
        }
        ExtrudeSourceFace source;
        source.face = face;
        source.positions.reserve(face.vertices.size());
        for (const ElementId id : face.vertices) {
            const Vertex* vertex = vertices.find(id);
            if (vertex == nullptr || !extrusion_finite(extrusion_vector(vertex->position))) {
                return OperationResult::failure("Selected face references an invalid vertex.");
            }
            const auto local = extrusion_vector(vertex->position);
            const auto position = x_axis * local.x + y_axis * local.y + z_axis * local.z;
            source.positions.push_back(position);
            if (center_vertices.insert(id).second) {
                prepared.center += position;
            }
        }
        // Translation does not enter normal calculation, including for distant occurrences.
        // Geometric transformed winding naturally includes a mirrored basis's sign.
        double scale = 0.0;
        if (!extrusion_face_metric(source.positions, source.normal, scale)) {
            return OperationResult::failure("Selected face has no usable extrusion normal.");
        }
        prepared.summed_normal += source.normal;
        if (prepared.faces.empty() || face.id == active) {
            prepared.active_normal = source.normal;
        }
        prepared.faces.push_back(std::move(source));
    }
    if (prepared.faces.size() != face_ids.size()) {
        return OperationResult::failure("Selected face no longer exists.");
    }
    prepared.vertex_count = center_vertices.size();
    prepared.center = prepared.center * (1.0 / static_cast<double>(prepared.vertex_count)) +
        extrusion_vector(transform.origin);
    return {};
}

ExtrudeVector extrusion_local_offset(
    const PreparedFaceExtrusion& prepared, ExtrudeVector direction, float amount)
{
    const ExtrudeVector displacement = direction * static_cast<double>(amount);
    return {extrusion_dot(prepared.inverse_rows[0], displacement),
            extrusion_dot(prepared.inverse_rows[1], displacement),
            extrusion_dot(prepared.inverse_rows[2], displacement)};
}

OperationResult extrusion_normal_directions(
    const PreparedFaceExtrusion& prepared,
    std::map<ElementId, ExtrudeVector>& offsets)
{
    for (const ExtrudeSourceFace& source : prepared.faces) {
        const std::size_t count = source.positions.size();
        for (std::size_t index = 0; index < count; ++index) {
            ExtrudeVector previous = source.positions[(index + count - 1U) % count] -
                source.positions[index];
            ExtrudeVector next = source.positions[(index + 1U) % count] - source.positions[index];
            if (!normalize_extrusion_vector(previous, 0.0) || !normalize_extrusion_vector(next, 0.0)) {
                return OperationResult::failure("Selected face has a collapsed extrusion corner.");
            }
            // Corner-normal weighting uses the smaller angle, including concave corners.
            const double angle = std::acos(std::clamp(extrusion_dot(previous, next), -1.0, 1.0));
            offsets[source.face.vertices[index]] += source.normal * angle;
        }
    }
    for (auto& [id, direction] : offsets) {
        static_cast<void>(id);
        if (!normalize_extrusion_vector(direction)) {
            return OperationResult::failure("Selected vertex has no usable extrusion normal.");
        }
    }
    return {};
}

OperationResult extrusion_normal_offsets(
    const PreparedFaceExtrusion& prepared, float amount,
    std::map<ElementId, ExtrudeVector>& offsets)
{
    const auto result = extrusion_normal_directions(prepared, offsets);
    if (!result.success) {
        return result;
    }
    for (auto& [id, direction] : offsets) {
        static_cast<void>(id);
        direction = extrusion_local_offset(prepared, direction, amount);
    }
    return {};
}

quader::QVec3 basis_transform_point(const Transform3& transform, quader::QVec3 point)
{
    return {
        (transform.x_axis.x * point.x) + (transform.y_axis.x * point.y) + (transform.z_axis.x * point.z),
        (transform.x_axis.y * point.x) + (transform.y_axis.y * point.y) + (transform.z_axis.y * point.z),
        (transform.x_axis.z * point.x) + (transform.y_axis.z * point.y) + (transform.z_axis.z * point.z),
    };
}

quader::QVec3 transform_point_around_pivot(const Transform3& transform, quader::QVec3 pivot, quader::QVec3 point)
{
    const quader::QVec3 pivot_after_basis = basis_transform_point(transform, pivot);
    const quader::QVec3 translation = pivot + transform.origin - pivot_after_basis;
    return basis_transform_point(transform, point) + translation;
}

ElementId copied_vertex_with_offset(Document& document, std::map<ElementId, ElementId>& copied_vertices, ElementId source_id, quader::QVec3 offset)
{
    const auto existing = copied_vertices.find(source_id);
    if (existing != copied_vertices.end()) {
        return existing->second;
    }

    const Vertex* source = find_vertex(document, source_id);
    if (source == nullptr) {
      return kInvalidElementId;
    }

    const ElementId copied_id = add_vertex(document, source->position + offset);
    copied_vertices[source_id] = copied_id;
    return copied_id;
}

ElementId copied_vertex_with_transform(
    Document& document,
    std::map<ElementId, ElementId>& copied_vertices,
    ElementId source_id,
    const Transform3& transform,
    quader::QVec3 pivot)
{
    const auto existing = copied_vertices.find(source_id);
    if (existing != copied_vertices.end()) {
        return existing->second;
    }

    const Vertex* source = find_vertex(document, source_id);
    if (source == nullptr) {
      return kInvalidElementId;
    }

    const ElementId copied_id = add_vertex(document, transform_point_around_pivot(transform, pivot, source->position));
    copied_vertices[source_id] = copied_id;
    return copied_id;
}

template <typename CopyVertex>
OperationResult extrude_selected_faces_with_copied_vertices(
    Document& document,
    Selection& selection,
    CopyVertex&& copy_vertex,
    FaceSideShadingPolicy side_shading_policy,
    bool prune_source_vertices,
    std::string_view empty_message,
    std::string_view missing_message,
    std::string_view unchanged_message,
    bool preserve_authored_faces = false,
    std::vector<ElementId>* generated_side_faces = nullptr)
{
  if (selection.mode != SelectionMode::Face || selection.faces.empty()) {
    return OperationResult::failure(std::string(empty_message));
  }

    const ElementId active_face_id = active_face_or_invalid(selection);
    const std::vector<Face> faces = selected_face_copies(document, selection);
    if (faces.empty()) {
        return OperationResult::failure(std::string(missing_message));
    }

    std::map<std::pair<ElementId, ElementId>, int> selected_edge_counts;
    for (const Face& face : faces) {
        for (const Edge& edge : face_edges(face)) {
            ++selected_edge_counts[{ edge.a, edge.b }];
        }
    }

    std::map<ElementId, ElementId> copied_vertices;
    std::vector<ElementId> selected_faces;
    selected_faces.reserve(faces.size());
    bool changed = false;

    for (const Face& original_face : faces) {
        Face* face = find_face(document, original_face.id);
        if (face == nullptr || original_face.vertices.size() < 3) {
            continue;
        }

        std::vector<ElementId> cap_vertices;
        cap_vertices.reserve(original_face.vertices.size());
        bool valid_face = true;
        for (const ElementId vertex_id : original_face.vertices) {
            const ElementId copied_id = copy_vertex(copied_vertices, vertex_id);
            if (copied_id == kInvalidElementId) {
              valid_face = false;
              break;
            }
            cap_vertices.push_back(copied_id);
        }

        if (!valid_face || cap_vertices.size() != original_face.vertices.size()) {
            continue;
        }

        face->vertices = cap_vertices;
        // Copied cap corners retain their source order even when the extrusion transforms them.
        face->uvs = original_face.uvs;
        selected_faces.push_back(original_face.id);
        changed = true;
    }

    if (!changed) {
        return OperationResult::failure(std::string(unchanged_message));
    }

    for (const Face& original_face : faces) {
        if (original_face.vertices.size() < 3) {
            continue;
        }

        for (std::size_t index = 0; index < original_face.vertices.size(); ++index) {
            const ElementId from_id = original_face.vertices[index];
            const ElementId to_id = original_face.vertices[(index + 1U) % original_face.vertices.size()];
            const Edge edge = make_edge(from_id, to_id);
            const auto edge_count = selected_edge_counts.find({ edge.a, edge.b });
            if (edge_count == selected_edge_counts.end() || edge_count->second != 1) {
                continue;
            }

            const auto copied_from = copied_vertices.find(from_id);
            const auto copied_to = copied_vertices.find(to_id);
            if (copied_from == copied_vertices.end() || copied_to == copied_vertices.end()) {
                continue;
            }

            const std::array side {
                from_id,
                to_id,
                copied_to->second,
                copied_from->second,
            };
            const ElementId side_face_id = add_face_with_texture_transform(
                document, side, original_face.material_slot, original_face.texture_transform);
            if (generated_side_faces != nullptr && side_face_id != kInvalidElementId) {
                generated_side_faces->push_back(side_face_id);
            }
            if (side_shading_policy == FaceSideShadingPolicy::InheritSource) {
                if (Face* side_face = find_face(document, side_face_id)) {
                    side_face->normal_shading = original_face.normal_shading;
                }
            }
        }
    }

    std::vector<EdgeNormalModeCopy> normal_mode_copies;
    for (const Face& original_face : faces) {
        for (const Edge source_edge : face_edges(original_face)) {
            const auto copied_a = copied_vertices.find(source_edge.a);
            const auto copied_b = copied_vertices.find(source_edge.b);
            if (copied_a == copied_vertices.end() ||
                copied_b == copied_vertices.end()) {
                continue;
            }
            normal_mode_copies.push_back({
                source_edge,
                make_edge(copied_a->second, copied_b->second),
            });
        }
    }
    static_cast<void>(
        copy_edge_normal_modes(document, document, normal_mode_copies));

    if (!preserve_authored_faces) {
        prune_invalid_faces(document);
    }
    // The cap replaces the selected surface, including a fully open region. Remove only
    // its abandoned interior vertices; unrelated authored loose vertices remain intact.
    if (prune_source_vertices) {
        std::set<ElementId> abandoned_source_vertices;
        for (const auto& copied_vertex : copied_vertices) {
            abandoned_source_vertices.insert(copied_vertex.first);
        }
        for (const Face& face : document.faces) {
            for (const ElementId id : face.vertices) {
                abandoned_source_vertices.erase(id);
            }
        }
        std::erase_if(document.vertices, [&](const Vertex& vertex) {
            return abandoned_source_vertices.contains(vertex.id);
        });
    }
    prune_normal_edge_states(document);
    selection.clear();
    selection.mode = SelectionMode::Face;
    selection.faces = std::move(selected_faces);
    activate_face_or_last_selection(selection, active_face_id);
    return { true, {} };
}

using ExtrusionEdgeKey = std::pair<ElementId, ElementId>;

/** Captures one face's directed use of an edge before support topology changes. */
struct ExtrusionEdgeUse {
    ElementId face = kInvalidElementId;
    ElementId from = kInvalidElementId;
    ElementId to = kInvalidElementId;
};

using ExtrusionEdgeUses = std::map<ExtrusionEdgeKey, std::vector<ExtrusionEdgeUse>>;

/** Selects the established support-width source without changing topology ownership. */
enum class EdgeExtrusionSupportPolicy {
    Grid,
    ExplicitLedge,
    Displacement,
};

ExtrusionEdgeKey extrusion_edge_key(ElementId first, ElementId second)
{
    const Edge edge = make_edge(first, second);
    return {edge.a, edge.b};
}

bool collect_extrusion_edge_uses(const Document& document, ExtrusionEdgeUses& uses)
{
    for (const Face& face : document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const ElementId from = face.vertices[index];
            const ElementId to = face.vertices[(index + 1U) % face.vertices.size()];
            auto& edge_uses = uses[extrusion_edge_key(from, to)];
            // Existing face winding does not change edge incidence. Each new
            // boundary wall follows its own donor's directed edge use.
            if (edge_uses.size() == 2U) {
                return false;
            }
            edge_uses.push_back({face.id, from, to});
        }
    }
    return true;
}

ElementId append_edge_extrusion_vertex(Document& document, quader::QVec3 position)
{
    if (!extrusion_finite(extrusion_vector(position)) ||
        document.next_vertex_id == kInvalidElementId ||
        document.next_vertex_id == std::numeric_limits<ElementId>::max()) {
        return kInvalidElementId;
    }
    return add_vertex(document, position);
}

quader::QVec3 edge_extrusion_position(const Transform3& transform,
    quader::QVec3 pivot, quader::QVec3 position)
{
    const ExtrudeVector relative = extrusion_vector(position) - extrusion_vector(pivot);
    return extrusion_storage_vector(extrusion_vector(pivot) + extrusion_vector(transform.origin) +
        extrusion_vector(transform.x_axis) * relative.x +
        extrusion_vector(transform.y_axis) * relative.y +
        extrusion_vector(transform.z_axis) * relative.z);
}

bool append_edge_extrusion_face(Document& candidate, const Document& source,
    const Face& donor, std::vector<ElementId> vertices)
{
    if (candidate.next_face_id == kInvalidElementId ||
        candidate.next_face_id == std::numeric_limits<ElementId>::max()) {
        return false;
    }
    Face face = donor;
    face.id = candidate.next_face_id++;
    face.vertices = std::move(vertices);
    if (!preserve_face_uvs(source, donor, candidate, face)) {
        return false;
    }
    candidate.faces.push_back(std::move(face));
    return true;
}

double closed_edge_legacy_support_width(const Document& source,
    const std::vector<ExtrusionEdgeUse>& uses, const Transform3& transform,
    quader::QVec3 pivot, double requested_width)
{
    const auto first_position = find_vertex(source, uses.front().from)->position;
    const auto second_position = find_vertex(source, uses.front().to)->position;
    const auto average_delta = ((edge_extrusion_position(transform, pivot, first_position) - first_position) +
        (edge_extrusion_position(transform, pivot, second_position) - second_position)) * 0.5F;
    const auto direction = normalize_or_zero(average_delta);
    const Face& first_face = *find_face(source, uses[0].face);
    const Face& second_face = *find_face(source, uses[1].face);
    const float first_score = std::abs(dot(normalize_or_zero(face_normal(source, first_face)), direction));
    const float second_score = std::abs(dot(normalize_or_zero(face_normal(source, second_face)), direction));
    const std::size_t cap_index = first_score <= second_score ? 1U : 0U;
    const Face& cap = cap_index == 0U ? first_face : second_face;
    const auto& cap_use = uses[cap_index];
    const std::size_t from_index = static_cast<std::size_t>(
        std::ranges::find(cap.vertices, cap_use.from) - cap.vertices.begin());
    const auto previous = find_vertex(source, cap.vertices[(from_index + cap.vertices.size() - 1U) % cap.vertices.size()])->position;
    const auto next = find_vertex(source, cap.vertices[(from_index + 2U) % cap.vertices.size()])->position;
    const auto from = find_vertex(source, cap_use.from)->position;
    const auto to = find_vertex(source, cap_use.to)->position;
    const double half_spoke_limit = 0.5 * std::min(
        extrusion_length(extrusion_vector(previous) - extrusion_vector(from)),
        extrusion_length(extrusion_vector(next) - extrusion_vector(to)));
    const double depth = requested_width > kEpsilon ? requested_width
        : extrusion_length(extrusion_vector(average_delta));
    return std::min(depth, half_spoke_limit);
}

// Each original face owns its outer region and the strips around its moving
// ridge runs. Shared spoke cuts keep neighboring faces welded at the endpoints.
bool prepare_closed_edge_extrusion(Document& candidate, const Document& source,
    const std::set<ExtrusionEdgeKey>& ridges, const std::set<ElementId>& moving,
    const ExtrusionEdgeUses& edge_uses, const std::map<ElementId, double>& support_widths)
{
    std::map<ExtrusionEdgeKey, ElementId> supports;
    std::vector<EdgeNormalModeAssignment> normal_modes;
    for (const auto& [edge, uses] : edge_uses) {
        static_cast<void>(uses);
        if (ridges.contains(edge) ||
            (!moving.contains(edge.first) && !moving.contains(edge.second))) {
            continue;
        }
        const ExtrudeVector first = extrusion_vector(find_vertex(source, edge.first)->position);
        const ExtrudeVector second = extrusion_vector(find_vertex(source, edge.second)->position);
        const double distance = extrusion_length(second - first);
        constexpr double kMinimumSupportSpoke = 0.000001;
        constexpr double kMaximumSupportFraction = 0.9999997615814208984375;
        if (distance <= kMinimumSupportSpoke ||
            (moving.contains(edge.first) && moving.contains(edge.second) &&
                support_widths.at(edge.first) + support_widths.at(edge.second) >= distance)) {
            return false;
        }
        std::vector<ElementId> split_loop{edge.first};
        for (const auto& [apex, neighbor] :
            {ExtrusionEdgeKey{edge.first, edge.second}, ExtrusionEdgeKey{edge.second, edge.first}}) {
            if (!moving.contains(apex)) {
                continue;
            }
            const double fraction = std::min(support_widths.at(apex) / distance, kMaximumSupportFraction);
            const ExtrudeVector origin = extrusion_vector(find_vertex(source, apex)->position);
            const ExtrudeVector endpoint = extrusion_vector(find_vertex(source, neighbor)->position);
            const ElementId support = append_edge_extrusion_vertex(candidate,
                extrusion_storage_vector(origin + (endpoint - origin) * fraction));
            if (support == kInvalidElementId) {
                return false;
            }
            supports.emplace(ExtrusionEdgeKey{apex, neighbor}, support);
            split_loop.push_back(support);
        }
        split_loop.push_back(edge.second);
        const auto mode = edge_normal_mode(source, make_edge(edge.first, edge.second));
        for (std::size_t index = 1; index < split_loop.size(); ++index) {
            normal_modes.push_back({make_edge(split_loop[index - 1U], split_loop[index]), mode});
        }
    }

    candidate.faces.clear();
    for (const Face& donor : source.faces) {
        const std::size_t count = donor.vertices.size();
        const auto ridge_edge = [&](std::size_t index) {
            return ridges.contains(extrusion_edge_key(
                donor.vertices[index], donor.vertices[(index + 1U) % count]));
        };
        if (std::ranges::none_of(donor.vertices, [&](ElementId id) { return moving.contains(id); })) {
            candidate.faces.push_back(donor);
            continue;
        }
        std::size_t start = 0;
        while (start < count && ridge_edge((start + count - 1U) % count)) {
            ++start;
        }
        if (start == count) {
            candidate.faces.push_back(donor);
            continue;
        }
        Face outside = donor;
        outside.vertices.clear();
        std::size_t processed = 0;
        while (processed < count) {
            const std::size_t index = (start + processed) % count;
            const ElementId vertex = donor.vertices[index];
            if (!moving.contains(vertex)) {
                outside.vertices.push_back(vertex);
                ++processed;
                continue;
            }
            const ElementId previous = donor.vertices[(index + count - 1U) % count];
            const ElementId left = supports.at({vertex, previous});
            std::vector<ElementId> strip{left, vertex};
            std::size_t last = index;
            ++processed;
            while (ridge_edge(last)) {
                last = (last + 1U) % count;
                strip.push_back(donor.vertices[last]);
                ++processed;
            }
            const ElementId next = donor.vertices[(last + 1U) % count];
            const ElementId right = supports.at({donor.vertices[last], next});
            strip.push_back(right);
            outside.vertices.push_back(left);
            outside.vertices.push_back(right);
            if (!append_edge_extrusion_face(candidate, source, donor, std::move(strip))) {
                return false;
            }
        }
        if (outside.vertices.size() < 3U ||
            !preserve_face_uvs(source, donor, candidate, outside)) {
            return false;
        }
        candidate.faces.push_back(std::move(outside));
    }
    prune_normal_edge_states(candidate);
    static_cast<void>(set_edge_normal_modes(candidate, normal_modes));
    return true;
}

OperationResult extrude_selected_edges_through_transform(Document& document,
    Selection& selection, const Transform3& transform, quader::QVec3 pivot,
    float extrusion_grid_size, EdgeExtrusionSupportPolicy support_policy = EdgeExtrusionSupportPolicy::Grid)
{
    if (selection.edges.empty()) {
        return OperationResult::failure("Select one or more edges to extrude.");
    }
    if (!std::isfinite(extrusion_grid_size) ||
        (support_policy != EdgeExtrusionSupportPolicy::Displacement && extrusion_grid_size <= 0.0F) ||
        !extrusion_finite(extrusion_vector(pivot)) ||
        !extrusion_finite(extrusion_vector(transform.origin)) ||
        !extrusion_finite(extrusion_vector(transform.x_axis)) ||
        !extrusion_finite(extrusion_vector(transform.y_axis)) ||
        !extrusion_finite(extrusion_vector(transform.z_axis))) {
        return OperationResult::failure("Edge extrusion requires a finite transform and positive grid size.");
    }
    if (!validate_polygon_document(document, PolygonDocumentValidationPolicy::AuthoredTopology).ok() ||
        std::ranges::any_of(document.faces, [](const Face& face) { return face.id == kInvalidElementId; })) {
        return OperationResult::failure("Edge extrusion requires valid authored topology.");
    }
    ExtrusionEdgeUses edge_uses;
    if (!collect_extrusion_edge_uses(document, edge_uses)) {
        return OperationResult::failure("Edge extrusion requires manifold edges.");
    }
    std::set<ExtrusionEdgeKey> ridges;
    std::set<ElementId> moving;
    std::vector<Edge> edges;
    std::vector<ExtrusionEdgeUse> boundary;
    for (const Edge selected : selection.edges) {
        const auto key = extrusion_edge_key(selected.a, selected.b);
        const auto found = edge_uses.find(key);
        if (found == edge_uses.end()) {
            return OperationResult::failure("A selected extrusion edge is missing.");
        }
        const Edge edge = make_edge(selected.a, selected.b);
        if (std::ranges::find(edges, edge) != edges.end()) {
            continue;
        }
        edges.push_back(edge);
        if (found->second.size() == 2U) {
            ridges.insert(key);
            moving.insert(edge.a);
            moving.insert(edge.b);
        } else {
            boundary.push_back(found->second.front());
        }
    }
    if (std::ranges::any_of(boundary, [&](const ExtrusionEdgeUse& edge) {
            return moving.contains(edge.from) || moving.contains(edge.to);
        })) {
        return OperationResult::failure("Boundary and closed extrusion edges cannot share an endpoint.");
    }
    const Edge active = make_edge(selection.active_edge.a, selection.active_edge.b);
    if (selection.has_active && (selection.active_kind != ElementKind::Edge ||
            std::ranges::find(edges, active) == edges.end())) {
        return OperationResult::failure("The active extrusion edge is not selected.");
    }
    if (transform.origin.x == 0.0F && transform.origin.y == 0.0F && transform.origin.z == 0.0F &&
        transform.x_axis.x == 1.0F && transform.x_axis.y == 0.0F && transform.x_axis.z == 0.0F &&
        transform.y_axis.x == 0.0F && transform.y_axis.y == 1.0F && transform.y_axis.z == 0.0F &&
        transform.z_axis.x == 0.0F && transform.z_axis.y == 0.0F && transform.z_axis.z == 1.0F) {
        return {false, {}};
    }

    std::map<ElementId, double> support_widths;
    for (const auto& ridge : ridges) {
        const double width = support_policy == EdgeExtrusionSupportPolicy::Grid
            ? static_cast<double>(extrusion_grid_size) * 0.25
            : closed_edge_legacy_support_width(document, edge_uses.at(ridge), transform, pivot,
                support_policy == EdgeExtrusionSupportPolicy::ExplicitLedge ? extrusion_grid_size : 0.0);
        if (!std::isfinite(width) ||
            (support_policy != EdgeExtrusionSupportPolicy::Grid && width <= kEpsilon)) {
            return OperationResult::failure("Closed edge extrusion requires a non-zero supporting width.");
        }
        for (const ElementId endpoint : {ridge.first, ridge.second}) {
            const auto [entry, inserted] = support_widths.emplace(endpoint, width);
            if (!inserted) {
                // One welded junction uses the most restrictive incident support.
                entry->second = std::min(entry->second, width);
            }
        }
    }
    Document candidate = document;
    if (!ridges.empty() && !prepare_closed_edge_extrusion(candidate, document,
            ridges, moving, edge_uses, support_widths)) {
        return OperationResult::failure("Closed edge extrusion support cuts overlap or cannot be constructed.");
    }
    std::map<ElementId, ElementId> copied;
    for (const ExtrusionEdgeUse& edge : boundary) {
        for (const ElementId vertex : {edge.from, edge.to}) {
            if (!copied.contains(vertex)) {
                const ElementId copy = append_edge_extrusion_vertex(candidate,
                    edge_extrusion_position(transform, pivot, find_vertex(document, vertex)->position));
                if (copy == kInvalidElementId) {
                    return OperationResult::failure("Extrusion vertex identity allocation failed.");
                }
                copied.emplace(vertex, copy);
            }
        }
        // Boundary extrusion retains its existing offset-edge and side-wall contract.
        const Face& donor = *find_face(document, edge.face);
        const std::array side{edge.to, edge.from, copied.at(edge.from), copied.at(edge.to)};
        if (candidate.next_face_id == kInvalidElementId ||
            candidate.next_face_id == std::numeric_limits<ElementId>::max() ||
            add_face_with_texture_transform(candidate, side, donor.material_slot, donor.texture_transform) == kInvalidElementId) {
            return OperationResult::failure("Extrusion face identity allocation failed.");
        }
    }
    for (const ElementId vertex : moving) {
        Vertex* target = find_vertex(candidate, vertex);
        target->position = edge_extrusion_position(transform, pivot, target->position);
        if (!extrusion_finite(extrusion_vector(target->position))) {
            return OperationResult::failure("Extrusion produced a non-finite vertex.");
        }
    }
    std::vector<EdgeNormalModeCopy> normal_copies;
    for (const ExtrusionEdgeUse& edge : boundary) {
        normal_copies.push_back({make_edge(edge.from, edge.to), make_edge(copied.at(edge.from), copied.at(edge.to))});
    }
    static_cast<void>(copy_edge_normal_modes(document, candidate, normal_copies));
    ExtrusionEdgeUses candidate_uses;
    if (!validate_polygon_document(candidate, PolygonDocumentValidationPolicy::AuthoredTopology).ok() ||
        !collect_extrusion_edge_uses(candidate, candidate_uses)) {
        return OperationResult::failure("Extrusion could not preserve valid manifold topology.");
    }
    Selection result_selection;
    result_selection.mode = SelectionMode::Edge;
    for (const Edge edge : edges) {
        if (ridges.contains({edge.a, edge.b})) {
            result_selection.edges.push_back(edge);
        }
    }
    for (const ExtrusionEdgeUse& edge : boundary) {
        result_selection.edges.push_back(make_edge(copied.at(edge.from), copied.at(edge.to)));
    }
    activate_last_selection(result_selection);
    OperationResult result = component_extrusion_receipt(document, candidate);
    document = std::move(candidate);
    selection = std::move(result_selection);
    return result;
}

} // namespace

/** Stores one immutable topology generation and its absolute displacement inputs. */
struct FaceExtrusionPreparationData {
    struct MovingVertex {
        ElementId id = kInvalidElementId;
        std::size_t index = 0;
        ExtrudeVector source_position;
        ExtrudeVector direction;
    };

    Document source;
    Selection source_selection;
    Document topology;
    Selection cap_selection;
    PreparedFaceExtrusion frame;
    FaceExtrudeMode mode = FaceExtrudeMode::Region;
    std::vector<MovingVertex> moving_vertices;
    std::vector<std::size_t> cap_faces;
    std::vector<std::size_t> side_faces;
    std::unordered_map<ElementId, std::size_t> vertex_indices;
    OperationResult receipt;
    std::string nonzero_error;
};

namespace {

/** Resolves captured storage indices without rebuilding an index on each sample. */
class PreparedExtrusionVertexLookup final {
public:
    PreparedExtrusionVertexLookup(const Document& document,
        const std::unordered_map<ElementId, std::size_t>& indices)
        : document_(document), indices_(indices) {}

    [[nodiscard]] const Vertex* find(ElementId id) const {
        const auto found = indices_.find(id);
        return found == indices_.end() ? nullptr : &document_.vertices[found->second];
    }

private:
    const Document& document_;
    const std::unordered_map<ElementId, std::size_t>& indices_;
};

/** Checks position-dependent mapping while retaining the authored texture metadata. */
template <typename Vertices>
bool extrusion_face_texture_is_finite(const Face& face, const Vertices& vertices)
{
    if (face.texture_transform == FaceTextureTransform{}) {
        return true;
    }
    Document local;
    Face local_face;
    if (!face_has_loop_uvs(face)) {
        // A bounded dense copy reuses the canonical normal calculation, even when
        // the original document has sparse IDs far beyond its storage indices.
        local.vertices.reserve(face.vertices.size());
        local_face.vertices.reserve(face.vertices.size());
        for (const ElementId id : face.vertices) {
            const Vertex* vertex = vertices.find(id);
            if (vertex == nullptr) {
                return false;
            }
            local_face.vertices.push_back(add_vertex(local, vertex->position));
        }
    }
    const auto normal = local.vertices.empty() ? quader::QVec3{} : face_normal(local, local_face);
    for (std::size_t index = 0; index < face.vertices.size(); ++index) {
        const auto base = face_has_loop_uvs(face) ? face.uvs[index] :
            generated_texture_uv(local.vertices[index].position, normal);
        const auto uv = apply_face_texture_transform(base, face.texture_transform);
        if (!std::isfinite(uv.x) || !std::isfinite(uv.y)) {
            return false;
        }
    }
    return true;
}

} // namespace

OperationResult selected_face_extrude_frame(
    const Document& document, const Selection& selection,
    const Transform3& extrusion_from_local, FaceExtrudeFrame& frame)
{
    PreparedFaceExtrusion prepared;
    const auto result = prepare_extrusion_frame_data(document, selection, extrusion_from_local, prepared);
    if (!result.success) {
        return result;
    }
    FaceExtrudeFrame candidate;
    candidate.center = extrusion_storage_vector(prepared.center);
    candidate.summed_normal = extrusion_storage_vector(prepared.summed_normal);
    candidate.active_normal = extrusion_storage_vector(prepared.active_normal);
    candidate.face_count = prepared.faces.size();
    candidate.vertex_count = prepared.vertex_count;
    if (!extrusion_finite(extrusion_vector(candidate.center)) ||
        !extrusion_finite(extrusion_vector(candidate.summed_normal))) {
        return OperationResult::failure("Extrusion frame is outside the supported coordinate range.");
    }
    frame = candidate;
    return {};
}

OperationResult extrude_selected_faces(
    Document& document, Selection& selection, const FaceExtrudeOptions& options)
{
    if (!std::isfinite(options.amount)) {
        return OperationResult::failure("Extrusion amount must be finite.");
    }
    if (options.mode != FaceExtrudeMode::Region &&
        options.mode != FaceExtrudeMode::AlongNormals &&
        options.mode != FaceExtrudeMode::Individual) {
        return OperationResult::failure("Extrusion type is invalid.");
    }
    ExtrudeVector explicit_direction;
    if (options.region_direction) {
        explicit_direction = extrusion_vector(*options.region_direction);
        if (!normalize_extrusion_vector(explicit_direction)) {
            return OperationResult::failure("Extrusion direction must be finite and non-zero.");
        }
    }
    const auto source_validation = validate_extrusion_structure(document);
    if (!source_validation.success) {
        return source_validation;
    }
    PreparedFaceExtrusion prepared;
    const auto preparation = prepare_extrusion_frame_data(
        document, selection, options.extrusion_from_local, prepared);
    if (!preparation.success) {
        return preparation;
    }
    const VertexLookupIndex source_vertices(document);
    for (const ExtrudeSourceFace& source : prepared.faces) {
        if (!extrusion_cap_is_usable(source.face, source_vertices)) {
            return OperationResult::failure("Selected face has no usable extrusion cap.");
        }
    }
    if (options.amount == 0.0F) {
        return {};
    }
    std::map<ElementId, ExtrudeVector> offsets;
    if (options.mode == FaceExtrudeMode::Region) {
        ExtrudeVector direction = options.region_direction
            ? explicit_direction : prepared.summed_normal;
        if (!normalize_extrusion_vector(direction)) {
            direction = prepared.active_normal;
        }
        const ExtrudeVector offset = extrusion_local_offset(prepared, direction, options.amount);
        for (const ExtrudeSourceFace& source : prepared.faces) {
            for (const ElementId id : source.face.vertices) {
                offsets[id] = offset;
            }
        }
    } else if (options.mode == FaceExtrudeMode::AlongNormals) {
        const auto normal_result = extrusion_normal_offsets(prepared, options.amount, offsets);
        if (!normal_result.success) {
            return normal_result;
        }
    }

    Document candidate = document;
    Selection candidate_selection = selection;
    const std::size_t source_face_count = candidate.faces.size();
    bool valid_positions = true;
    const auto copy_vertex = [&](std::map<ElementId, ElementId>& copied_vertices, ElementId id) {
        const auto copied = copied_vertices.find(id);
        if (copied != copied_vertices.end()) {
            return copied->second;
        }
        const Vertex* source = find_vertex(candidate, id);
        const auto offset = offsets.find(id);
        if (source == nullptr || offset == offsets.end()) {
            valid_positions = false;
            return kInvalidElementId;
        }
        const auto position = extrusion_storage_vector(extrusion_vector(source->position) + offset->second);
        if (!extrusion_finite(extrusion_vector(position))) {
            valid_positions = false;
            return kInvalidElementId;
        }
        const ElementId copied_id = add_vertex(candidate, position);
        copied_vertices.emplace(id, copied_id);
        return copied_id;
    };
    std::vector<ElementId> generated_side_faces;
    const auto apply_region = [&](Selection& target) {
        return extrude_selected_faces_with_copied_vertices(
            candidate, target, copy_vertex, FaceSideShadingPolicy::InheritSource, true,
            "Select one or more faces to extrude.", "No selected faces were found.",
            "No selected faces were extruded.", true, &generated_side_faces);
    };
    if (options.mode == FaceExtrudeMode::Individual) {
        for (const ExtrudeSourceFace& source : prepared.faces) {
            offsets.clear();
            const auto offset = extrusion_local_offset(prepared, source.normal, options.amount);
            for (const ElementId id : source.face.vertices) {
                offsets[id] = offset;
            }
            Selection face_selection;
            face_selection.mode = SelectionMode::Face;
            face_selection.faces = {source.face.id};
            face_selection.active_face = source.face.id;
            const auto result = apply_region(face_selection);
            if (!result.success || !valid_positions) {
                return OperationResult::failure("Could not prepare individual extrusion geometry.");
            }
        }
        candidate_selection.clear();
        candidate_selection.mode = SelectionMode::Face;
        for (const ExtrudeSourceFace& source : prepared.faces) {
            candidate_selection.faces.push_back(source.face.id);
        }
        activate_face_or_last_selection(candidate_selection, active_face_or_invalid(selection));
    } else {
        const auto result = apply_region(candidate_selection);
        if (!result.success || !valid_positions) {
            return OperationResult::failure("Could not prepare extrusion geometry.");
        }
    }
    const auto validation = validate_extrusion_structure(candidate);
    if (!validation.success) {
        return validation;
    }
    // Tangent and thin walls remain authored geometry, but their opposite boundary
    // edges must not cross through each other. Skew edges on nonplanar walls are valid.
    const VertexLookupIndex candidate_vertices(candidate);
    for (const ElementId face_id : generated_side_faces) {
        const Face* wall = find_face(candidate, face_id);
        if (wall == nullptr || extrusion_wall_has_proper_crossing(*wall, candidate_vertices)) {
            return OperationResult::failure("Extrusion would cross a side wall boundary.");
        }
    }
    for (const ElementId face_id : candidate_selection.faces) {
        const Face* cap = find_face(candidate, face_id);
        if (cap == nullptr || !extrusion_cap_is_usable(*cap, candidate_vertices)) {
            return OperationResult::failure("Extrusion would collapse a selected cap.");
        }
    }
    OperationResult result;
    result.changed = true;
    result.affected.faces = candidate_selection.faces;
    for (const Vertex& vertex : candidate.vertices) {
        if (source_vertices.find(vertex.id) == nullptr) {
            result.created.vertices.push_back(vertex.id);
        }
    }
    for (const Vertex& vertex : document.vertices) {
        if (candidate_vertices.find(vertex.id) == nullptr) {
            result.deleted.vertices.push_back(vertex.id);
        }
    }
    for (std::size_t index = source_face_count; index < candidate.faces.size(); ++index) {
        result.created.faces.push_back(candidate.faces[index].id);
    }
    const auto original_edges = document_edges(document);
    std::set<std::pair<ElementId, ElementId>> source_edges;
    for (const Edge edge : original_edges) {
        source_edges.emplace(edge.a, edge.b);
    }
    for (const Edge edge : document_edges(candidate)) {
        if (source_edges.erase({edge.a, edge.b}) == 0U) {
            result.created.edges.push_back(edge);
        }
    }
    for (const auto& [first, second] : source_edges) {
        result.deleted.edges.push_back({first, second});
    }
    document = std::move(candidate);
    selection = std::move(candidate_selection);
    return result;
}

OperationResult prepare_face_extrusion(
    const Document& document, const Selection& selection, FaceExtrudeMode mode,
    const Transform3& extrusion_from_local, FaceExtrusionPreparation& prepared)
{
    if (mode != FaceExtrudeMode::Region && mode != FaceExtrudeMode::AlongNormals &&
        mode != FaceExtrudeMode::Individual) {
        return OperationResult::failure("Extrusion type is invalid.");
    }
    const auto source_validation = validate_extrusion_structure(document);
    if (!source_validation.success) {
        return source_validation;
    }
    auto data = std::make_shared<FaceExtrusionPreparationData>();
    const auto frame_result = prepare_extrusion_frame_data(
        document, selection, extrusion_from_local, data->frame);
    if (!frame_result.success) {
        return frame_result;
    }
    const VertexLookupIndex source_vertices(document);
    for (const ExtrudeSourceFace& source : data->frame.faces) {
        if (!extrusion_cap_is_usable(source.face, source_vertices)) {
            return OperationResult::failure("Selected face has no usable extrusion cap.");
        }
    }
    data->source = document;
    data->source_selection = selection;
    data->topology = document;
    data->cap_selection = selection;
    data->mode = mode;
    const auto defer_nonzero_failure = [&](const OperationResult& failure) {
        // Exact zero preserves a valid source even when its selected normals or
        // boundary arrangement cannot form a nonzero extrusion.
        data->nonzero_error = failure.message;
        prepared.data_ = std::move(data);
        return OperationResult{};
    };
    std::map<ElementId, ExtrudeVector> directions;
    if (mode == FaceExtrudeMode::AlongNormals) {
        const auto normal_result = extrusion_normal_directions(data->frame, directions);
        if (!normal_result.success) {
            return defer_nonzero_failure(normal_result);
        }
    } else if (mode == FaceExtrudeMode::Region) {
        auto direction = data->frame.summed_normal;
        if (!normalize_extrusion_vector(direction)) {
            direction = data->frame.active_normal;
        }
        for (const ExtrudeSourceFace& source : data->frame.faces) {
            for (const ElementId id : source.face.vertices) {
                directions[id] = direction;
            }
        }
    }
    const auto copy_vertex = [&](std::map<ElementId, ElementId>& copied, ElementId id) {
        const auto existing = copied.find(id);
        if (existing != copied.end()) {
            return existing->second;
        }
        const Vertex* source = source_vertices.find(id);
        const auto direction = directions.find(id);
        if (source == nullptr || direction == directions.end()) {
            return kInvalidElementId;
        }
        const ElementId copied_id = add_vertex(data->topology, source->position);
        data->moving_vertices.push_back({copied_id, 0U, extrusion_vector(source->position),
                                        direction->second});
        copied.emplace(id, copied_id);
        return copied_id;
    };
    std::vector<ElementId> side_faces;
    const auto build_region = [&](Selection& target) {
        return extrude_selected_faces_with_copied_vertices(
            data->topology, target, copy_vertex, FaceSideShadingPolicy::InheritSource, true,
            "Select one or more faces to extrude.", "No selected faces were found.",
            "No selected faces were extruded.", true, &side_faces);
    };
    if (mode == FaceExtrudeMode::Individual) {
        for (const ExtrudeSourceFace& source : data->frame.faces) {
            directions.clear();
            for (const ElementId id : source.face.vertices) {
                directions[id] = source.normal;
            }
            Selection target;
            target.mode = SelectionMode::Face;
            target.faces = {source.face.id};
            target.active_face = source.face.id;
            const auto result = build_region(target);
            if (!result.success) {
                return defer_nonzero_failure(result);
            }
        }
        data->cap_selection.clear();
        data->cap_selection.mode = SelectionMode::Face;
        for (const ExtrudeSourceFace& source : data->frame.faces) {
            data->cap_selection.faces.push_back(source.face.id);
        }
        activate_face_or_last_selection(data->cap_selection, active_face_or_invalid(selection));
    } else {
        const auto result = build_region(data->cap_selection);
        if (!result.success) {
            return defer_nonzero_failure(result);
        }
    }
    std::vector<FaceTextureTransform> side_transforms;
    side_transforms.reserve(side_faces.size());
    for (const ElementId id : side_faces) {
        Face* face = find_face(data->topology, id);
        if (face == nullptr) {
            return defer_nonzero_failure(
                OperationResult::failure("Could not prepare an extrusion side wall."));
        }
        data->side_faces.push_back(static_cast<std::size_t>(face - data->topology.faces.data()));
        side_transforms.push_back(face->texture_transform);
        // The zero-displacement template has no wall normal yet. Validate its
        // connectivity here; evaluate its inherited mapping with each real sample.
        face->texture_transform = {};
    }
    const auto topology_validation = validate_extrusion_structure(data->topology);
    if (!topology_validation.success) {
        return defer_nonzero_failure(topology_validation);
    }
    for (std::size_t index = 0; index < data->side_faces.size(); ++index) {
        data->topology.faces[data->side_faces[index]].texture_transform = side_transforms[index];
    }
    for (const ElementId id : data->cap_selection.faces) {
        const Face* face = find_face(data->topology, id);
        if (face == nullptr) {
            return defer_nonzero_failure(
                OperationResult::failure("Could not prepare an extrusion cap."));
        }
        data->cap_faces.push_back(static_cast<std::size_t>(face - data->topology.faces.data()));
    }
    data->vertex_indices.reserve(data->topology.vertices.size());
    for (std::size_t index = 0; index < data->topology.vertices.size(); ++index) {
        data->vertex_indices.emplace(data->topology.vertices[index].id, index);
    }
    for (auto& moving : data->moving_vertices) {
        moving.index = data->vertex_indices.at(moving.id);
    }
    data->receipt.changed = true;
    data->receipt.affected.faces = data->cap_selection.faces;
    for (const Vertex& vertex : data->topology.vertices) {
        if (source_vertices.find(vertex.id) == nullptr) {
            data->receipt.created.vertices.push_back(vertex.id);
        }
    }
    for (const Vertex& vertex : document.vertices) {
        if (!data->vertex_indices.contains(vertex.id)) {
            data->receipt.deleted.vertices.push_back(vertex.id);
        }
    }
    for (std::size_t index = document.faces.size(); index < data->topology.faces.size(); ++index) {
        data->receipt.created.faces.push_back(data->topology.faces[index].id);
    }
    std::set<std::pair<ElementId, ElementId>> source_edges;
    for (const Edge edge : document_edges(document)) {
        source_edges.emplace(edge.a, edge.b);
    }
    for (const Edge edge : document_edges(data->topology)) {
        if (source_edges.erase({edge.a, edge.b}) == 0U) {
            data->receipt.created.edges.push_back(edge);
        }
    }
    for (const auto& [first, second] : source_edges) {
        data->receipt.deleted.edges.push_back({first, second});
    }
    data->frame.faces.clear();
    prepared.data_ = std::move(data);
    return {};
}

OperationResult evaluate_face_extrusion(
    const FaceExtrusionPreparation& prepared, float amount,
    std::optional<quader::QVec3> region_direction,
    Document& document, Selection& selection)
{
    if (!prepared.valid()) {
        return OperationResult::failure("Extrusion preparation is unavailable.");
    }
    if (!std::isfinite(amount)) {
        return OperationResult::failure("Extrusion amount must be finite.");
    }
    ExtrudeVector explicit_direction;
    if (region_direction) {
        explicit_direction = extrusion_vector(*region_direction);
        if (!normalize_extrusion_vector(explicit_direction)) {
            return OperationResult::failure("Extrusion direction must be finite and non-zero.");
        }
    }
    const auto& data = *prepared.data_;
    if (amount == 0.0F) {
        document = data.source;
        selection = data.source_selection;
        return {};
    }
    if (!data.nonzero_error.empty()) {
        return OperationResult::failure(data.nonzero_error);
    }
    Document candidate = data.topology;
    for (const auto& moving : data.moving_vertices) {
        const auto direction = data.mode == FaceExtrudeMode::Region && region_direction
            ? explicit_direction : moving.direction;
        const auto position = extrusion_storage_vector(moving.source_position +
            extrusion_local_offset(data.frame, direction, amount));
        if (!extrusion_finite(extrusion_vector(position))) {
            return OperationResult::failure("Extrusion position is outside the supported coordinate range.");
        }
        candidate.vertices[moving.index].position = position;
    }
    const PreparedExtrusionVertexLookup vertices(candidate, data.vertex_indices);
    for (const auto index : data.side_faces) {
        const Face& face = candidate.faces[index];
        if (extrusion_wall_has_proper_crossing(face, vertices)) {
            return OperationResult::failure("Extrusion would cross a side wall boundary.");
        }
        if (!extrusion_face_texture_is_finite(face, vertices)) {
            return OperationResult::failure("Extrusion side wall texture settings produce non-finite UVs.");
        }
    }
    for (const auto index : data.cap_faces) {
        const Face& face = candidate.faces[index];
        if (!extrusion_cap_is_usable(face, vertices)) {
            return OperationResult::failure("Extrusion would collapse a selected cap.");
        }
        if (!extrusion_face_texture_is_finite(face, vertices)) {
            return OperationResult::failure("Extrusion cap texture settings produce non-finite UVs.");
        }
    }
    document = std::move(candidate);
    selection = data.cap_selection;
    return data.receipt;
}

namespace document_internal {

OperationResult extrude_selected_vertices_impl(
    Document &document, Selection &selection, const Transform3 &transform,
    quader::QVec3 pivot, float setback);

OperationResult extrude_selected_elements_impl(Document& document, Selection& selection, quader::QVec3 offset, float closed_edge_ledge_size)
{
    QDR_PROFILE_SCOPE("qdr_document.extrude_selected_elements_impl");
    if (selection.mode == SelectionMode::Vertex) {
      Transform3 transform;
      transform.origin = offset;
      return extrude_selected_vertices_impl(document, selection, transform, {},
          closed_edge_ledge_size == 0.0F ? 1.0F : closed_edge_ledge_size);
    }



    if (length(offset) <= kEpsilon) {
      return {false, "Extrude distance must be non-zero."};
    }

    if (selection.mode == SelectionMode::Edge) {
      Transform3 transform;
      transform.origin = offset;
      return extrude_selected_edges_through_transform(document, selection, transform, {},
          closed_edge_ledge_size, closed_edge_ledge_size == 0.0F
              ? EdgeExtrusionSupportPolicy::Displacement : EdgeExtrusionSupportPolicy::ExplicitLedge);
    }

    if (selection.mode == SelectionMode::Face) {
      return extrude_selected_faces_with_copied_vertices(
          document, selection,
          [&document, offset](std::map<ElementId, ElementId> &copied_vertices,
                              ElementId source_id) {
            return copied_vertex_with_offset(document, copied_vertices,
                                             source_id, offset);
          },
          FaceSideShadingPolicy::Default,
          true,
          "Select one or more faces to extrude.",
          "No selected faces were found.", "No selected faces were extruded.");
    }



    return OperationResult::failure("Extrude needs a face or edge selection.");
}

OperationResult transform_extrude_selected_elements_impl(Document& document, Selection& selection, const Transform3& transform, quader::QVec3 pivot, float extrusion_grid_size)
{
    QDR_PROFILE_SCOPE("qdr_document.transform_extrude_selected_elements_impl");
    if (selection.mode == SelectionMode::Vertex) {
      return extrude_selected_vertices_impl(document, selection, transform, pivot,
                                             extrusion_grid_size);
    }
    if (selection.mode == SelectionMode::Face) {
      return extrude_selected_faces_with_copied_vertices(
          document, selection,
          [&document, &transform,
           pivot](std::map<ElementId, ElementId> &copied_vertices,
                  ElementId source_id) {
            return copied_vertex_with_transform(document, copied_vertices,
                                                source_id, transform, pivot);
          },
          FaceSideShadingPolicy::Default,
          true,
          "Select one or more faces to extrude.",
          "No selected faces were found.", "No selected faces were extruded.");
    }

    if (selection.mode == SelectionMode::Edge) {
      return extrude_selected_edges_through_transform(document, selection, transform, pivot, extrusion_grid_size);
    }

    return OperationResult::failure("Extrude needs a face or edge selection.");
}

OperationResult inset_selected_elements_impl(Document& document, Selection& selection, const Transform3& transform, quader::QVec3 pivot)
{
    QDR_PROFILE_SCOPE("qdr_document.inset_selected_elements_impl");
    if (selection.mode == SelectionMode::Face) {
      return extrude_selected_faces_with_copied_vertices(
          document, selection,
          [&document, &transform,
           pivot](std::map<ElementId, ElementId> &copied_vertices,
                  ElementId source_id) {
            return copied_vertex_with_transform(document, copied_vertices,
                                                source_id, transform, pivot);
          },
          FaceSideShadingPolicy::InheritSource,
          false,
          "Select one or more faces to inset.", "No selected faces were found.",
          "No selected faces were inset.");
    }

    if (selection.mode == SelectionMode::Edge) {
      return extrude_selected_edges_through_transform(document, selection, transform, pivot, 0.0F,
          EdgeExtrusionSupportPolicy::Displacement);
    }

    return OperationResult::failure("Inset needs a face or edge selection.");
}

} // namespace document_internal

} // namespace quader_poly
