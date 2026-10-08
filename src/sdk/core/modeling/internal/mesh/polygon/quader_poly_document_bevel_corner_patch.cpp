////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_bevel_corner_patch.hpp>
#include <quader/modeling/neutral/polygon/internal/quader_poly_document_mesh_internal.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <limits>
#include <map>
#include <utility>

namespace quader_poly::document_internal {
namespace {

/** A node in a boundary-constrained, purely combinatorial patch grid. */
struct PatchNode {
    quader::QVec3 position;
    ElementId vertex_id = kInvalidElementId;
    bool boundary = false;
    std::vector<std::size_t> neighbors;
};

/** A sphere reconstructed from boundary samples, when their shape permits it. */
struct BoundarySphere {
    quader::QVec3 center;
    float radius = 0.0F;
};

bool finite_position(quader::QVec3 position)
{
    return std::isfinite(position.x) && std::isfinite(position.y) &&
        std::isfinite(position.z);
}

quader::QVec3 interpolate_position(
    quader::QVec3 first, quader::QVec3 second, float factor)
{
    return first + (second - first) * factor;
}

quader::QVec3 evaluate_profile_curve(
    quader::QVec3 origin,
    quader::QVec3 first,
    quader::QVec3 second,
    float tangent_weight,
    float profile,
    float parameter)
{
    const float complement = 1.0F - parameter;
    const float first_weight = complement * complement;
    const float middle_weight = 2.0F * tangent_weight * parameter * complement;
    const float last_weight = parameter * parameter;
    const quader::QVec3 circular =
        (first * first_weight + origin * middle_weight + second * last_weight) /
        (first_weight + middle_weight + last_weight);
    if (profile == 0.5F) {
        return circular;
    }

    const bool inward = profile < 0.5F;
    const quader::QVec3 turn = inward ? first + second - origin : origin;
    const quader::QVec3 square = parameter <= 0.5F
        ? interpolate_position(first, turn, parameter * 2.0F)
        : interpolate_position(turn, second, parameter * 2.0F - 1.0F);
    const float blend = inward ? (0.5F - profile) * 2.0F
                               : (profile - 0.5F) / 0.45F;
    return interpolate_position(circular, square, std::clamp(blend, 0.0F, 1.0F));
}

bool order_patch_sides(
    std::span<const BevelCornerSide> sides,
    std::vector<BevelCornerSide>& ordered)
{
    if (sides.empty()) {
        return false;
    }
    for (const BevelCornerSide& side : sides) {
        if (side.vertices.size() < 2U ||
            side.vertices.front() == side.vertices.back()) {
            return false;
        }
    }
    std::vector<bool> used(sides.size(), false);
    ordered.push_back(sides.front());
    used.front() = true;
    while (ordered.size() < sides.size()) {
        const ElementId last = ordered.back().vertices.back();
        bool found = false;
        for (std::size_t index = 0U; index < sides.size(); ++index) {
            if (used[index]) {
                continue;
            }
            if (sides[index].vertices.front() != last &&
                sides[index].vertices.back() != last) {
                continue;
            }
            ordered.push_back(sides[index]);
            if (ordered.back().vertices.front() != last) {
                std::reverse(ordered.back().vertices.begin(), ordered.back().vertices.end());
            }
            used[index] = true;
            found = true;
            break;
        }
        if (!found) {
            return false;
        }
    }
    return ordered.back().vertices.back() == ordered.front().vertices.front();
}

bool separate_collapsed_samples(
    Document& document,
    const std::vector<BevelCornerSide>& sides,
    float coincidence_tolerance)
{
    /** A cached boundary position and the chord available to separate a pole. */
    struct BoundarySample {
        Vertex* vertex = nullptr;
        quader::QVec3 chord;
        bool movable = false;
    };

    std::vector<BoundarySample> boundary;
    std::map<ElementId, std::size_t> sample_by_id;
    for (const BevelCornerSide& side : sides) {
        const Vertex* first = find_vertex(document, side.vertices.front());
        const Vertex* last = find_vertex(document, side.vertices.back());
        if (first == nullptr || last == nullptr) {
            return false;
        }
        for (std::size_t index = 0U; index + 1U < side.vertices.size(); ++index) {
            const ElementId vertex_id = side.vertices[index];
            Vertex* vertex = find_vertex(document, vertex_id);
            if (vertex == nullptr || !finite_position(vertex->position) ||
                sample_by_id.contains(vertex_id)) {
                return false;
            }
            const float factor = static_cast<float>(index) /
                static_cast<float>(side.vertices.size() - 1U);
            sample_by_id.emplace(vertex_id, boundary.size());
            boundary.push_back({
                vertex, interpolate_position(first->position, last->position, factor), index != 0U});
        }
    }

    // Offset polylines can share a geometric pole while retaining distinct
    // boundary identities. Move only those samples toward their own side chord;
    // the strip and the patch continue to share exactly the same vertices.
    const float tolerance_squared = coincidence_tolerance * coincidence_tolerance;
    for (int iteration = 0; iteration <= 4; ++iteration) {
        std::vector<bool> collapsed(boundary.size(), false);
        bool found_collision = false;
        for (std::size_t first = 0U; first < boundary.size(); ++first) {
            const quader::QVec3 first_position = boundary[first].vertex->position;
            for (std::size_t second = first + 1U; second < boundary.size(); ++second) {
                const quader::QVec3 second_position = boundary[second].vertex->position;
                const float distance_squared = length_squared(first_position - second_position);
                const bool fixed_pair = !boundary[first].movable && !boundary[second].movable;
                if (distance_squared <= tolerance_squared && (!fixed_pair || distance_squared == 0.0F)) {
                    collapsed[first] = true;
                    collapsed[second] = true;
                    found_collision = true;
                }
            }
        }
        if (!found_collision) {
            return true;
        }
        if (iteration == 4) {
            return false;
        }
        bool changed = false;
        for (std::size_t index = 0U; index < boundary.size(); ++index) {
            BoundarySample& sample = boundary[index];
            if (collapsed[index] && sample.movable) {
                sample.vertex->position = interpolate_position(sample.vertex->position, sample.chord, 0.25F);
                changed = true;
            }
        }
        if (!changed) {
            return false;
        }
    }
    return false;
}

quader::QVec3 boundary_area_vector(
    const Document& document, const std::vector<BevelCornerSide>& sides)
{
    const quader::QVec3 reference =
        find_vertex(document, sides.front().vertices.front())->position;
    quader::QVec3 area;
    for (const BevelCornerSide& side : sides) {
        for (std::size_t index = 0U; index + 1U < side.vertices.size(); ++index) {
            const quader::QVec3 first = find_vertex(document, side.vertices[index])->position;
            const quader::QVec3 second = find_vertex(document, side.vertices[index + 1U])->position;
            area += cross(first - reference, second - reference);
        }
    }
    return area;
}

std::optional<BoundarySphere> fit_boundary_sphere(
    std::span<const PatchNode> boundary, quader::QVec3 centroid)
{
    std::array<std::array<double, 4>, 3> equations {};
    double extent_squared = 0.0;
    for (const PatchNode& node : boundary) {
        const quader::QVec3 relative = node.position - centroid;
        const std::array<double, 3> components {relative.x, relative.y, relative.z};
        const double squared = static_cast<double>(length_squared(relative));
        extent_squared = std::max(extent_squared, squared);
        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                equations[row][column] += components[row] * components[column];
            }
            equations[row][3] += components[row] * squared * 0.5;
        }
    }
    if (extent_squared <= std::numeric_limits<double>::epsilon()) {
        return std::nullopt;
    }
    for (std::size_t column = 0U; column < 3U; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1U; row < 3U; ++row) {
            if (std::abs(equations[row][column]) > std::abs(equations[pivot][column])) {
                pivot = row;
            }
        }
        if (std::abs(equations[pivot][column]) <= extent_squared * 0.0000001) {
            return std::nullopt;
        }
        std::swap(equations[pivot], equations[column]);
        const double divisor = equations[column][column];
        for (std::size_t entry = column; entry < 4U; ++entry) {
            equations[column][entry] /= divisor;
        }
        for (std::size_t row = 0U; row < 3U; ++row) {
            if (row == column) {
                continue;
            }
            const double multiplier = equations[row][column];
            for (std::size_t entry = column; entry < 4U; ++entry) {
                equations[row][entry] -= multiplier * equations[column][entry];
            }
        }
    }
    BoundarySphere sphere;
    sphere.center = centroid + quader::QVec3 {
        static_cast<float>(equations[0][3]),
        static_cast<float>(equations[1][3]),
        static_cast<float>(equations[2][3])};
    if (!finite_position(sphere.center)) {
        return std::nullopt;
    }
    for (const PatchNode& node : boundary) {
        sphere.radius += length(node.position - sphere.center);
    }
    sphere.radius /= static_cast<float>(boundary.size());
    if (!std::isfinite(sphere.radius) || sphere.radius <= 0.0F ||
        sphere.radius * sphere.radius > extent_squared * 16.0) {
        return std::nullopt;
    }
    for (const PatchNode& node : boundary) {
        if (std::abs(length(node.position - sphere.center) - sphere.radius) >
            sphere.radius * 0.001F) {
            return std::nullopt;
        }
    }
    return sphere;
}

void add_patch_face(
    std::vector<PatchNode>& nodes,
    std::vector<std::vector<std::size_t>>& faces,
    std::vector<std::size_t> face)
{
    for (std::size_t index = 0U; index < face.size(); ++index) {
        const std::size_t first = face[index];
        const std::size_t second = face[(index + 1U) % face.size()];
        if (std::ranges::find(nodes[first].neighbors, second) == nodes[first].neighbors.end()) {
            nodes[first].neighbors.push_back(second);
        }
        if (std::ranges::find(nodes[second].neighbors, first) == nodes[second].neighbors.end()) {
            nodes[second].neighbors.push_back(first);
        }
    }
    faces.push_back(std::move(face));
}

void relax_patch_positions(std::vector<PatchNode>& nodes, float extent)
{
    const float threshold = extent * extent * 0.0000000001F;
    for (int iteration = 0; iteration < 512; ++iteration) {
        float largest_change = 0.0F;
        for (PatchNode& node : nodes) {
            if (node.boundary || node.neighbors.empty()) {
                continue;
            }
            quader::QVec3 average;
            for (const std::size_t neighbor : node.neighbors) {
                average += nodes[neighbor].position;
            }
            average = average / static_cast<float>(node.neighbors.size());
            largest_change = std::max(largest_change, length_squared(average - node.position));
            node.position = average;
        }
        if (largest_change <= threshold) {
            break;
        }
    }
}

quader::QVec3 patch_face_area(
    const std::vector<PatchNode>& nodes,
    const std::vector<std::size_t>& face,
    quader::QVec3 reference)
{
    quader::QVec3 area;
    for (std::size_t index = 0U; index < face.size(); ++index) {
        area += cross(
            nodes[face[index]].position - reference,
            nodes[face[(index + 1U) % face.size()]].position - reference);
    }
    return area;
}

bool unfold_star_shaped_patch(
    std::vector<PatchNode>& nodes,
    const std::vector<std::vector<std::size_t>>& faces,
    std::size_t side_count,
    std::size_t segments,
    quader::QVec3 centroid,
    quader::QVec3 expected_normal)
{
    const quader::QVec3 normal = normalize_or_zero(expected_normal);
    if (length_squared(normal) == 0.0F ||
        std::ranges::none_of(faces, [&](const std::vector<std::size_t>& face) {
            return dot(patch_face_area(nodes, face, centroid), normal) <= 0.0F;
        })) {
        return true;
    }
    const std::size_t boundary_count = side_count * segments;
    for (std::size_t index = 0U; index < boundary_count; ++index) {
        const quader::QVec3 first = nodes[index].position - centroid;
        const quader::QVec3 second = nodes[(index + 1U) % boundary_count].position - centroid;
        if (dot(cross(first, second), normal) <= 0.0F) {
            // A folded projection of a non-star-shaped boundary is not proof of
            // a folded surface. Such corners keep their three-dimensional solve.
            return true;
        }
    }

    // A convex parameter polygon has a nonfolding harmonic embedding. Pull its
    // radial lines back to the sampled boundary, which is star-shaped in this
    // chart. This retains every boundary vertex even for deeply inset profiles.
    std::vector<PatchNode> reference_nodes = nodes;
    std::vector<quader::QVec3> reference_corners;
    reference_corners.reserve(side_count);
    constexpr double kTau = 6.283185307179586476925286766559;
    for (std::size_t side = 0U; side < side_count; ++side) {
        const double angle = kTau * static_cast<double>(side) / static_cast<double>(side_count);
        reference_corners.push_back({static_cast<float>(std::cos(angle)), static_cast<float>(std::sin(angle)), 0.0F});
    }
    for (std::size_t side = 0U; side < side_count; ++side) {
        for (std::size_t index = 0U; index < segments; ++index) {
            reference_nodes[side * segments + index].position = interpolate_position(
                reference_corners[side], reference_corners[(side + 1U) % side_count],
                static_cast<float>(index) / static_cast<float>(segments));
        }
    }
    for (std::size_t index = boundary_count; index < reference_nodes.size(); ++index) {
        reference_nodes[index].position = {};
    }
    relax_patch_positions(reference_nodes, 1.0F);
    for (std::size_t index = boundary_count; index < nodes.size(); ++index) {
        const quader::QVec3 ray = reference_nodes[index].position;
        if (length_squared(ray) <= 0.000000000001F) {
            nodes[index].position = centroid;
            continue;
        }
        bool found = false;
        for (std::size_t side = 0U; side < side_count; ++side) {
            const quader::QVec3 first = reference_corners[side];
            const quader::QVec3 edge = reference_corners[(side + 1U) % side_count] - first;
            const double denominator = static_cast<double>(ray.x) * edge.y - static_cast<double>(ray.y) * edge.x;
            if (std::abs(denominator) <= 0.000000000001) {
                continue;
            }
            const double distance =
                (static_cast<double>(first.x) * edge.y - static_cast<double>(first.y) * edge.x) / denominator;
            const double side_factor =
                (static_cast<double>(first.x) * ray.y - static_cast<double>(first.y) * ray.x) / denominator;
            if (distance < 0.99999 || side_factor < -0.00001 || side_factor > 1.00001) {
                continue;
            }
            const double sample = std::clamp(side_factor, 0.0, 1.0) * static_cast<double>(segments);
            const std::size_t lower = std::min(static_cast<std::size_t>(sample), segments - 1U);
            const std::size_t first_index = side * segments + lower;
            const std::size_t second_index = (first_index + 1U) % boundary_count;
            const quader::QVec3 boundary_position = interpolate_position(
                nodes[first_index].position, nodes[second_index].position,
                static_cast<float>(sample - static_cast<double>(lower)));
            const double radius = std::clamp(1.0 / distance, 0.0, 1.0);
            const double complement = 1.0 - radius;
            // Redistribute central samples without crowding the boundary. The
            // derivative stays between .8 and 1.6, limiting radial distortion
            // while preserving dimensionless, scale-independent spacing.
            const double redistributed_radius = radius + 0.6 * radius * complement * complement;
            nodes[index].position = interpolate_position(centroid, boundary_position, static_cast<float>(redistributed_radius));
            found = true;
            break;
        }
        if (!found) {
            return false;
        }
    }
    return std::ranges::all_of(faces, [&](const std::vector<std::size_t>& face) {
        if (dot(patch_face_area(nodes, face, centroid), normal) <= 0.0F) {
            return false;
        }
        for (std::size_t index = 0U; index < face.size(); ++index) {
            const quader::QVec3 first = nodes[face[index]].position;
            const quader::QVec3 second = nodes[face[(index + 1U) % face.size()]].position;
            const quader::QVec3 third = nodes[face[(index + 2U) % face.size()]].position;
            if (dot(cross(second - first, third - first), normal) <= 0.0F) {
                return false;
            }
        }
        return true;
    });
}

bool stage_patch_faces(
    Document& document,
    std::vector<PatchNode>& nodes,
    const std::vector<std::vector<std::size_t>>& faces,
    std::uint32_t material_slot,
    std::vector<Face>& staged_faces,
    std::vector<ElementId>& generated_faces)
{
    std::vector<Vertex> patch_vertices;
    patch_vertices.reserve(nodes.size());
    for (PatchNode& node : nodes) {
        if (!finite_position(node.position)) {
            return false;
        }
        if (node.vertex_id == kInvalidElementId) {
            node.vertex_id = add_vertex(document, node.position);
        }
        patch_vertices.push_back({node.vertex_id, node.position});
    }
    const VertexLookupIndex vertex_lookup {std::span<const Vertex>(patch_vertices)};
    for (const std::vector<std::size_t>& loop : faces) {
        Face face;
        face.id = document.next_face_id++;
        face.material_slot = material_slot;
        face.vertices.reserve(loop.size());
        for (const std::size_t index : loop) {
            const ElementId vertex_id = nodes[index].vertex_id;
            if (std::ranges::find(face.vertices, vertex_id) != face.vertices.end()) {
                return false;
            }
            face.vertices.push_back(vertex_id);
        }
        if (face.vertices.size() < 3U) {
            return false;
        }
        const quader::QVec3 normal = face_normal_from_vertex_lookup(face, vertex_lookup);
        const std::vector<Triangle> triangles = triangulate_face_local_indices(face, vertex_lookup, normal);
        if (triangles.empty() || !triangulation_preserves_face_boundary(face, vertex_lookup, triangles)) {
            return false;
        }
        for (const Triangle& triangle : triangles) {
            if (triangle.a >= loop.size() || triangle.b >= loop.size() || triangle.c >= loop.size()) {
                return false;
            }
            const quader::QVec3 first = nodes[loop[triangle.a]].position;
            const quader::QVec3 second = nodes[loop[triangle.b]].position;
            const quader::QVec3 third = nodes[loop[triangle.c]].position;
            const quader::QVec3 first_edge = second - first;
            const quader::QVec3 second_edge = third - first;
            const double cross_x = static_cast<double>(first_edge.y) * second_edge.z -
                static_cast<double>(first_edge.z) * second_edge.y;
            const double cross_y = static_cast<double>(first_edge.z) * second_edge.x -
                static_cast<double>(first_edge.x) * second_edge.z;
            const double cross_z = static_cast<double>(first_edge.x) * second_edge.y -
                static_cast<double>(first_edge.y) * second_edge.x;
            const double area_squared = cross_x * cross_x + cross_y * cross_y + cross_z * cross_z;
            const double scale_squared = std::max(
                static_cast<double>(length_squared(first_edge)),
                static_cast<double>(length_squared(second_edge)));
            if (!std::isfinite(area_squared) ||
                area_squared <= scale_squared * scale_squared * 0.0000000000000001) {
                return false;
            }
        }
        generated_faces.push_back(face.id);
        staged_faces.push_back(std::move(face));
    }
    return true;
}

} // namespace

quader::QVec3 edge_bevel_profile_point(
    quader::QVec3 source_position,
    quader::QVec3 first_boundary,
    quader::QVec3 second_boundary,
    int segment,
    int segments,
    float profile,
    BevelProfileType profile_type)
{
    static_cast<void>(profile_type);
    if (segment <= 0 || segments <= 0) {
        return first_boundary;
    }
    if (segment >= segments) {
        return second_boundary;
    }
    profile = std::isfinite(profile) ? std::clamp(profile, 0.0F, 1.0F) : 0.5F;
    if (profile >= 0.95F) {
        profile = 0.95F;
    }
    const quader::QVec3 first_ray = first_boundary - source_position;
    const quader::QVec3 second_ray = second_boundary - source_position;
    const float ray_product = length(first_ray) * length(second_ray);
    const float cosine = ray_product > 0.0F
        ? std::clamp(dot(first_ray, second_ray) / ray_product, -1.0F, 1.0F)
        : -1.0F;
    const float tangent_weight = std::sqrt(std::max(0.0F, (1.0F - cosine) * 0.5F));
    const auto evaluate = [&](float parameter) {
        return evaluate_profile_curve(
            source_position, first_boundary, second_boundary,
            tangent_weight, profile, parameter);
    };
    const quader::QVec3 normalized_first = normalize_or_zero(first_ray);
    const quader::QVec3 normalized_second = normalize_or_zero(second_ray);
    const auto evaluate_distance = [&](float parameter) {
        return evaluate_profile_curve(
            {}, normalized_first, normalized_second,
            tangent_weight, profile, parameter);
    };

    // Arc-length inversion is deliberately independent of the shape parameter.
    // Measure in the support-ray frame: miter extension can stretch either ray,
    // but must not shift the shared profile's segment parameters.
    constexpr std::size_t kSamples = 256U;
    std::array<float, kSamples + 1U> distances {};
    quader::QVec3 previous = normalized_first;
    for (std::size_t index = 1U; index <= kSamples; ++index) {
        const quader::QVec3 current = evaluate_distance(static_cast<float>(index) / static_cast<float>(kSamples));
        distances[index] = distances[index - 1U] + length(current - previous);
        previous = current;
    }
    const float total = distances.back();
    const float factor = static_cast<float>(segment) / static_cast<float>(segments);
    if (!std::isfinite(total) || total <= 0.0F) {
        return interpolate_position(first_boundary, second_boundary, factor);
    }
    const float target = total * factor;
    const auto upper = std::lower_bound(distances.begin() + 1, distances.end(), target);
    const std::size_t index = static_cast<std::size_t>(upper - distances.begin());
    const float interval = distances[index] - distances[index - 1U];
    const float local = interval > 0.0F ? (target - distances[index - 1U]) / interval : 0.0F;
    return evaluate((static_cast<float>(index - 1U) + local) / static_cast<float>(kSamples));
}

bool append_bevel_corner_patch(
    Document& document,
    std::span<const BevelCornerSide> sides,
    quader::QVec3 source_position,
    quader::QVec3 expected_normal,
    const EdgeBevelSettings& settings,
    std::uint32_t material_slot,
    std::vector<Face>& staged_faces,
    std::vector<ElementId>& generated_faces)
{
    if (sides.empty()) {
        return true;
    }
    if (sides.size() == 2U && sides.front().vertices.size() == sides.back().vertices.size()) {
        const auto& first = sides.front().vertices;
        const auto& second = sides.back().vertices;
        if (first == second || std::equal(first.begin(), first.end(), second.rbegin())) {
            return true;
        }
    }
    std::vector<BevelCornerSide> ordered;
    if (!order_patch_sides(sides, ordered) || ordered.size() < 3U) {
        return false;
    }
    const std::size_t segments = ordered.front().vertices.size() - 1U;
    if (segments == 0U || std::ranges::any_of(ordered, [segments](const BevelCornerSide& side) {
            return side.vertices.size() != segments + 1U;
        })) {
        return false;
    }
    float extent = 0.0F;
    float representation_step = 0.0F;
    for (const BevelCornerSide& side : ordered) {
        for (const ElementId vertex_id : side.vertices) {
            const Vertex* vertex = find_vertex(document, vertex_id);
            if (vertex == nullptr || !finite_position(vertex->position)) {
                return false;
            }
            extent = std::max(extent, length(vertex->position - source_position));
            for (const float coordinate : {vertex->position.x, vertex->position.y, vertex->position.z}) {
                const float magnitude = std::abs(coordinate);
                representation_step = std::max(
                    representation_step, std::nextafter(magnitude, std::numeric_limits<float>::infinity()) - magnitude);
            }
        }
    }
    if (extent <= 0.0F || !std::isfinite(extent)) {
        return false;
    }
    const float coincidence_tolerance = std::max(extent * 0.000001F, representation_step * 4.0F);
    if (!separate_collapsed_samples(document, ordered, coincidence_tolerance)) {
        return false;
    }
    if (dot(boundary_area_vector(document, ordered), expected_normal) < 0.0F) {
        std::reverse(ordered.begin(), ordered.end());
        for (BevelCornerSide& side : ordered) {
            std::reverse(side.vertices.begin(), side.vertices.end());
        }
    }

    std::vector<PatchNode> nodes;
    std::map<ElementId, std::size_t> node_by_id;
    quader::QVec3 centroid;
    for (const BevelCornerSide& side : ordered) {
        for (std::size_t index = 0U; index < segments; ++index) {
            const ElementId vertex_id = side.vertices[index];
            const quader::QVec3 position = find_vertex(document, vertex_id)->position;
            node_by_id.emplace(vertex_id, nodes.size());
            nodes.push_back({position, vertex_id, true, {}});
            centroid += position;
        }
    }
    centroid = centroid / static_cast<float>(nodes.size());
    const std::size_t boundary_count = nodes.size();
    std::vector<std::vector<std::size_t>> ring;
    for (const BevelCornerSide& side : ordered) {
        std::vector<std::size_t> chain;
        for (const ElementId vertex_id : side.vertices) {
            chain.push_back(node_by_id.at(vertex_id));
        }
        ring.push_back(std::move(chain));
    }

    std::vector<std::vector<std::size_t>> faces;
    std::size_t remaining = segments;
    while (remaining > 1U) {
        std::vector<std::vector<std::size_t>> inner(ordered.size());
        if (remaining == 2U) {
            const std::size_t center = nodes.size();
            nodes.push_back({centroid, kInvalidElementId, false, {}});
            for (auto& side : inner) {
                side.push_back(center);
            }
        } else {
            for (auto& side : inner) {
                for (std::size_t index = 0U; index < remaining - 2U; ++index) {
                    side.push_back(nodes.size());
                    nodes.push_back({centroid, kInvalidElementId, false, {}});
                }
            }
            for (std::size_t side = 0U; side < inner.size(); ++side) {
                inner[side].push_back(inner[(side + 1U) % inner.size()].front());
            }
        }
        for (std::size_t side = 0U; side < ring.size(); ++side) {
            const std::size_t previous = (side + ring.size() - 1U) % ring.size();
            add_patch_face(nodes, faces, {
                ring[side][0], ring[side][1], inner[side][0], ring[previous][remaining - 1U]});
            for (std::size_t index = 1U; index + 1U < remaining; ++index) {
                add_patch_face(nodes, faces, {
                    ring[side][index], ring[side][index + 1U], inner[side][index], inner[side][index - 1U]});
            }
        }
        ring = std::move(inner);
        remaining -= 2U;
    }
    if (remaining == 1U) {
        std::vector<std::size_t> center;
        for (const auto& side : ring) {
            center.push_back(side.front());
        }
        add_patch_face(nodes, faces, std::move(center));
    }
    relax_patch_positions(nodes, extent);
    if (!unfold_star_shaped_patch(nodes, faces, ordered.size(), segments, centroid, expected_normal)) {
        return false;
    }

    const bool circular = std::ranges::all_of(ordered, [&settings](const BevelCornerSide& side) {
        return std::abs(side.profile.value_or(settings.profile) - 0.5F) <= 0.000001F;
    });
    if (circular) {
        const auto sphere = fit_boundary_sphere(std::span<const PatchNode>(nodes).first(boundary_count), centroid);
        if (sphere.has_value()) {
            for (std::size_t index = boundary_count; index < nodes.size(); ++index) {
                const quader::QVec3 direction = nodes[index].position - sphere->center;
                const float distance = length(direction);
                if (distance > sphere->radius * 0.00001F) {
                    nodes[index].position = sphere->center + direction * (sphere->radius / distance);
                }
            }
        }
    }
    return stage_patch_faces(document, nodes, faces, material_slot, staged_faces, generated_faces);
}

} // namespace quader_poly::document_internal
