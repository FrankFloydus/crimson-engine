////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>
#include <quader/modeling/neutral/polygon/document_validation.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <ranges>
#include <set>
#include <tuple>
#include <vector>

namespace {

using namespace quader_poly;

bool nearly_equal(float left, float right, float epsilon = 0.0001F)
{
    return std::abs(left - right) <= epsilon;
}

bool vec_nearly_equal(quader::QVec3 left, quader::QVec3 right, float epsilon = 0.0001F)
{
    return nearly_equal(left.x, right.x, epsilon) &&
        nearly_equal(left.y, right.y, epsilon) &&
        nearly_equal(left.z, right.z, epsilon);
}

::testing::AssertionResult require_document_compiles(
    const Document& document,
    const char* label)
{
    static_cast<void>(label);
    const CompiledMesh mesh = compile_document(document);
    const ::testing::AssertionResult authored_vertices =
        require_test(!document.vertices.empty(), "primitive should contain vertices");
    if (!authored_vertices) {
        return authored_vertices;
    }
    const ::testing::AssertionResult authored_faces =
        require_test(!document.faces.empty(), "primitive should contain faces");
    if (!authored_faces) {
        return authored_faces;
    }
    const ::testing::AssertionResult compiled_vertices =
        require_test(!mesh.vertices.empty(), "primitive should compile vertices");
    if (!compiled_vertices) {
        return compiled_vertices;
    }
    const ::testing::AssertionResult triangle_indices = require_test(
        !mesh.indices.empty() && mesh.indices.size() % 3U == 0U,
        "primitive should compile triangle indices");
    if (!triangle_indices) {
        return triangle_indices;
    }
    return require_test(
        mesh.primitives.size() == document.faces.size(),
        "primitive should emit one compiled primitive per face");
}

/// Stores the axis-aligned bounds of one authored primitive document.
struct Bounds {
    quader::QVec3 minimum;
    quader::QVec3 maximum;
};

Bounds document_bounds(const Document& document)
{
    Bounds bounds {
        { std::numeric_limits<float>::max(), std::numeric_limits<float>::max(), std::numeric_limits<float>::max() },
        { std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest(), std::numeric_limits<float>::lowest() },
    };
    for (const Vertex& vertex : document.vertices) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position.x);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position.y);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position.z);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position.x);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position.y);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position.z);
    }
    return bounds;
}

::testing::AssertionResult require_bounds(
    const Document& document,
    float width,
    float height,
    float depth)
{
    const Bounds bounds = document_bounds(document);
    const ::testing::AssertionResult minimum_x = require_test(
        nearly_equal(bounds.minimum.x, -width * 0.5F),
        "primitive minimum x should match its width");
    if (!minimum_x) {
        return minimum_x;
    }
    const ::testing::AssertionResult maximum_x = require_test(
        nearly_equal(bounds.maximum.x, width * 0.5F),
        "primitive maximum x should match its width");
    if (!maximum_x) {
        return maximum_x;
    }
    const ::testing::AssertionResult minimum_y = require_test(
        nearly_equal(bounds.minimum.y, -height * 0.5F),
        "primitive minimum y should match its height");
    if (!minimum_y) {
        return minimum_y;
    }
    const ::testing::AssertionResult maximum_y = require_test(
        nearly_equal(bounds.maximum.y, height * 0.5F),
        "primitive maximum y should match its height");
    if (!maximum_y) {
        return maximum_y;
    }
    const ::testing::AssertionResult minimum_z = require_test(
        nearly_equal(bounds.minimum.z, -depth * 0.5F),
        "primitive minimum z should match its depth");
    if (!minimum_z) {
        return minimum_z;
    }
    return require_test(
        nearly_equal(bounds.maximum.z, depth * 0.5F),
        "primitive maximum z should match its depth");
}

::testing::AssertionResult require_finite_vertices(const Document& document)
{
    for (const Vertex& vertex : document.vertices) {
        const ::testing::AssertionResult finite = require_test(
            std::isfinite(vertex.position.x) &&
                std::isfinite(vertex.position.y) &&
                std::isfinite(vertex.position.z),
            "sanitized primitive vertices should remain finite");
        if (!finite) {
            return finite;
        }
    }
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult require_valid_document(const Document& document)
{
    const PolygonDocumentValidationReport report = validate_polygon_document(document);
    const ::testing::AssertionResult valid = require_test(
        report.ok(), "generated primitive should pass polygon document validation");
    if (!valid) {
        return valid;
    }
    return require_test(
        report.diagnostics.empty(),
        "generated primitive should not produce validation diagnostics");
}

int edge_face_incidence(const Document& document, Edge edge)
{
    int result = 0;
    for (const Face& face : document.faces) {
        for (std::size_t index = 0; index < face.vertices.size(); ++index) {
            const ElementId a = face.vertices[index];
            const ElementId b = face.vertices[(index + 1U) % face.vertices.size()];
            if (make_edge(a, b) == edge) {
                ++result;
            }
        }
    }
    return result;
}

std::vector<quader::QVec3> compiled_normals_for_source_vertex(
    const CompiledMesh& mesh,
    ElementId vertex_id)
{
    std::vector<quader::QVec3> normals;
    for (const CompiledVertex& vertex : mesh.vertices) {
        if (vertex.source_vertex_id == vertex_id) {
            normals.push_back(vertex.normal);
        }
    }
    return normals;
}

::testing::AssertionResult require_faces_reversed(
    const Document& outward,
    const Document& flipped)
{
    const ::testing::AssertionResult vertex_count = require_test(
        outward.vertices.size() == flipped.vertices.size(),
        "flipping should preserve primitive vertices");
    if (!vertex_count) {
        return vertex_count;
    }
    const ::testing::AssertionResult face_count = require_test(
        outward.faces.size() == flipped.faces.size(),
        "flipping should preserve primitive faces");
    if (!face_count) {
        return face_count;
    }
    for (std::size_t face_index = 0; face_index < outward.faces.size(); ++face_index) {
        const Face& outward_face = outward.faces[face_index];
        const Face& flipped_face = flipped.faces[face_index];
        const ::testing::AssertionResult reversed = require_test(
            std::ranges::equal(outward_face.vertices, std::views::reverse(flipped_face.vertices)),
            "flip faces should reverse every authored face loop");
        if (!reversed) {
            return reversed;
        }
        const ::testing::AssertionResult shading = require_test(
            outward_face.normal_shading == flipped_face.normal_shading,
            "flip faces should preserve authored shading");
        if (!shading) {
            return shading;
        }
    }
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult authored_face_normal(
    const Document& document,
    const Face& face,
    quader::QVec3& normal)
{
    const ::testing::AssertionResult face_size =
        require_test(face.vertices.size() >= 3U, "face normal requires at least three vertices");
    if (!face_size) {
        return face_size;
    }
    const Vertex* a = find_vertex(document, face.vertices[0]);
    const Vertex* b = find_vertex(document, face.vertices[1]);
    const Vertex* c = find_vertex(document, face.vertices[2]);
    const ::testing::AssertionResult vertices = require_test(
        a != nullptr && b != nullptr && c != nullptr,
        "face normal vertices should resolve");
    if (!vertices) {
        return vertices;
    }
    normal = quader::cross(b->position - a->position, c->position - a->position);
    return ::testing::AssertionSuccess();
}

::testing::AssertionResult face_centroid(
    const Document& document,
    const Face& face,
    quader::QVec3& centroid)
{
    centroid = {};
    for (ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        const ::testing::AssertionResult resolved =
            require_test(vertex != nullptr, "face centroid vertices should resolve");
        if (!resolved) {
            return resolved;
        }
        centroid += vertex->position;
    }
    centroid = centroid / static_cast<float>(face.vertices.size());
    return ::testing::AssertionSuccess();
}

void cube_factory_keeps_segment_counts()
{
    CubePrimitiveSpec cube_spec;
    cube_spec.width_segments = 2;
    cube_spec.height_segments = 1;
    cube_spec.depth_segments = 3;
    const Document cube = make_cube(cube_spec);
    ASSERT_TRUE(require_test(cube.faces.size() == 22U, "segmented cube face count should be stable"));
    ASSERT_TRUE(require_test(std::ranges::all_of(cube.faces, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Authored;
    }), "cube faces should use automatic edge normals"));
    ASSERT_TRUE(require_document_compiles(cube, "segmented cube"));
    ASSERT_TRUE(require_valid_document(cube));
}

void cube_dimension_sanitization_stays_private_to_factory_source()
{
    CubePrimitiveSpec cube_spec;
    cube_spec.width = std::numeric_limits<float>::quiet_NaN();
    cube_spec.height = std::numeric_limits<float>::infinity();
    cube_spec.depth = -42.0F;
    cube_spec.width_segments = -100;
    cube_spec.height_segments = 0;
    cube_spec.depth_segments = 0;

    const Document cube = make_cube(cube_spec);
    ASSERT_TRUE(require_document_compiles(cube, "sanitized cube"));
    ASSERT_TRUE(require_finite_vertices(cube));

    const Vertex* first = find_vertex(cube, cube.faces.front().vertices.front());
    ASSERT_TRUE(require_test(first != nullptr, "sanitized cube should resolve its first vertex"));
    ASSERT_TRUE(require_test(nearly_equal(std::abs(first->position.z), 0.0005F), "negative primitive dimensions should clamp to the package minimum"));
}

void cylinder_factory_honors_side_caps_dimensions_and_shading()
{
    CylinderPrimitiveSpec spec;
    spec.width = 4.0F;
    spec.height = 6.0F;
    spec.depth = 2.0F;
    spec.side_count = 8;
    const Document capped = make_cylinder(spec);

    ASSERT_TRUE(require_test(capped.vertices.size() == 16U, "cylinder should create two vertices per side"));
    ASSERT_TRUE(require_test(capped.faces.size() == 10U, "capped cylinder should create one face per side and two caps"));
    ASSERT_TRUE(require_test(std::ranges::all_of(capped.faces.begin(), capped.faces.begin() + 8, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Authored;
    }), "cylinder side faces should use automatic edge normals"));
    ASSERT_TRUE(require_test(capped.faces[8].normal_shading == SurfaceShading::Authored, "cylinder bottom cap should use automatic edge normals"));
    ASSERT_TRUE(require_test(capped.faces[9].normal_shading == SurfaceShading::Authored, "cylinder top cap should use automatic edge normals"));
    ASSERT_TRUE(require_bounds(capped, spec.width, spec.height, spec.depth));
    quader::QVec3 bottom_cap_normal;
    ASSERT_TRUE(authored_face_normal(capped, capped.faces[8], bottom_cap_normal));
    ASSERT_TRUE(require_test(
        bottom_cap_normal.y < 0.0F, "cylinder bottom cap should wind outward"));
    quader::QVec3 top_cap_normal;
    ASSERT_TRUE(authored_face_normal(capped, capped.faces[9], top_cap_normal));
    ASSERT_TRUE(require_test(
        top_cap_normal.y > 0.0F, "cylinder top cap should wind outward"));
    ASSERT_TRUE(require_document_compiles(capped, "capped cylinder"));
    ASSERT_TRUE(require_valid_document(capped));

    spec.capped = false;
    const Document open = make_cylinder(spec);
    ASSERT_TRUE(require_test(open.vertices.size() == 16U, "open cylinder should preserve its side vertices"));
    ASSERT_TRUE(require_test(open.faces.size() == 8U, "open cylinder should omit both cap faces"));
    ASSERT_TRUE(require_test(std::ranges::all_of(open.faces, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Authored;
    }), "open cylinder should retain automatic edge normals"));
    ASSERT_TRUE(require_document_compiles(open, "open cylinder"));

    CylinderPrimitiveSpec smooth_spec;
    smooth_spec.side_count = 32;
    smooth_spec.capped = false;
    const Document smooth = make_cylinder(smooth_spec);
    const CompiledMesh smooth_mesh = compile_document(smooth);
    for (const Vertex& vertex : smooth.vertices) {
        const std::vector<quader::QVec3> normals =
            compiled_normals_for_source_vertex(smooth_mesh, vertex.id);
        ASSERT_TRUE(require_test(
            normals.size() == 2U,
            "open cylinder wall vertex should be shared by two side corners"));
        ASSERT_TRUE(require_test(
            vec_nearly_equal(normals.front(), normals.back()),
            "open 32-side cylinder wall corners should share a smoothed normal"));
    }
}

void cone_factory_honors_side_cap_dimensions_and_shading()
{
    ConePrimitiveSpec spec;
    spec.width = 4.0F;
    spec.height = 6.0F;
    spec.depth = 2.0F;
    spec.side_count = 8;
    const Document capped = make_cone(spec);

    ASSERT_TRUE(require_test(capped.vertices.size() == 9U, "cone should share one apex across all sides"));
    ASSERT_TRUE(require_test(capped.faces.size() == 9U, "capped cone should create one face per side and one cap"));
    ASSERT_TRUE(require_test(std::ranges::all_of(capped.faces.begin(), capped.faces.begin() + 8, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Authored;
    }), "cone side faces should use automatic edge normals"));
    ASSERT_TRUE(require_test(capped.faces.back().normal_shading == SurfaceShading::Authored, "cone base cap should use automatic edge normals"));
    ASSERT_TRUE(require_test(std::ranges::all_of(capped.faces, [](const Face& face) {
        return face.uvs.empty();
    }), "cone should preserve the primitive authored-UV contract"));
    ASSERT_TRUE(require_bounds(capped, spec.width, spec.height, spec.depth));
    quader::QVec3 side_normal;
    ASSERT_TRUE(authored_face_normal(capped, capped.faces.front(), side_normal));
    quader::QVec3 side_centroid;
    ASSERT_TRUE(face_centroid(capped, capped.faces.front(), side_centroid));
    ASSERT_TRUE(require_test(
        quader::dot(side_normal, side_centroid) > 0.0F,
        "cone side should wind outward"));
    quader::QVec3 base_normal;
    ASSERT_TRUE(authored_face_normal(capped, capped.faces.back(), base_normal));
    ASSERT_TRUE(require_test(base_normal.y < 0.0F, "cone base cap should wind outward"));
    ASSERT_TRUE(require_document_compiles(capped, "capped cone"));
    ASSERT_TRUE(require_valid_document(capped));

    spec.capped = false;
    const Document open = make_cone(spec);
    ASSERT_TRUE(require_test(open.vertices.size() == 9U, "open cone should preserve its side vertices"));
    ASSERT_TRUE(require_test(open.faces.size() == 8U, "open cone should omit its base cap"));
    ASSERT_TRUE(require_document_compiles(open, "open cone"));
    ASSERT_TRUE(require_valid_document(open));
}

void sphere_factory_honors_radial_ring_and_hemisphere_settings()
{
    SpherePrimitiveSpec spec;
    spec.width = 4.0F;
    spec.height = 6.0F;
    spec.depth = 2.0F;
    spec.radial_segments = 8;
    spec.rings = 4;
    const Document sphere = make_sphere(spec);

    ASSERT_TRUE(require_test(sphere.vertices.size() == 26U, "sphere should share its poles and radial ring vertices"));
    ASSERT_TRUE(require_test(sphere.faces.size() == 32U, "sphere face count should equal radial segments times rings"));
    ASSERT_TRUE(require_test(std::ranges::all_of(sphere.faces, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Authored;
    }), "full sphere faces should use automatic edge normals"));
    ASSERT_TRUE(require_bounds(sphere, spec.width, spec.height, spec.depth));
    quader::QVec3 sphere_normal;
    ASSERT_TRUE(authored_face_normal(sphere, sphere.faces.front(), sphere_normal));
    quader::QVec3 sphere_centroid;
    ASSERT_TRUE(face_centroid(sphere, sphere.faces.front(), sphere_centroid));
    ASSERT_TRUE(require_test(
        quader::dot(sphere_normal, sphere_centroid) > 0.0F,
        "sphere surface should wind outward"));
    ASSERT_TRUE(require_document_compiles(sphere, "full sphere"));
    ASSERT_TRUE(require_valid_document(sphere));

    spec.hemisphere = true;
    const Document hemisphere = make_sphere(spec);
    ASSERT_TRUE(require_test(hemisphere.vertices.size() == 33U, "hemisphere should create one pole and the requested rings"));
    ASSERT_TRUE(require_test(hemisphere.faces.size() == 33U, "hemisphere should add one flat base to its curved faces"));
    ASSERT_TRUE(require_test(std::ranges::all_of(
        hemisphere.faces.begin(), hemisphere.faces.end() - 1, [](const Face& face) {
            return face.normal_shading == SurfaceShading::Authored;
        }), "hemisphere curved faces should use automatic edge normals"));
    ASSERT_TRUE(require_test(hemisphere.faces.back().normal_shading == SurfaceShading::Authored, "hemisphere base should use automatic edge normals"));
    quader::QVec3 hemisphere_base_normal;
    ASSERT_TRUE(authored_face_normal(
        hemisphere, hemisphere.faces.back(), hemisphere_base_normal));
    ASSERT_TRUE(require_test(
        hemisphere_base_normal.y < 0.0F,
        "hemisphere base should wind outward"));
    ASSERT_TRUE(require_bounds(hemisphere, spec.width, spec.height, spec.depth));
    ASSERT_TRUE(require_document_compiles(hemisphere, "solid hemisphere"));
    ASSERT_TRUE(require_valid_document(hemisphere));
}

void torus_factory_honors_segment_ratio_dimension_and_shading_settings()
{
    TorusPrimitiveSpec spec;
    spec.width = 4.0F;
    spec.height = 1.5F;
    spec.depth = 2.0F;
    spec.major_segments = 8;
    spec.minor_segments = 4;
    spec.tube_radius_ratio = 0.25F;
    const Document torus = make_torus(spec);

    ASSERT_TRUE(require_test(torus.vertices.size() == 32U, "torus should create one vertex per major/minor segment pair"));
    ASSERT_TRUE(require_test(torus.faces.size() == 32U, "torus should create one quad per major/minor segment pair"));
    ASSERT_TRUE(require_test(std::ranges::all_of(torus.faces, [](const Face& face) {
        return face.vertices.size() == 4U && face.normal_shading == SurfaceShading::Authored;
    }), "torus quads should use automatic edge normals"));
    ASSERT_TRUE(require_test(std::ranges::all_of(torus.faces, [](const Face& face) {
        return face.uvs.empty();
    }), "torus should preserve the primitive authored-UV contract"));
    const std::vector<Edge> edges = document_edges(torus);
    ASSERT_TRUE(require_test(edges.size() == 64U, "closed torus should have two edges per quad"));
    ASSERT_TRUE(require_test(std::ranges::all_of(edges, [&torus](Edge edge) {
        return edge_face_incidence(torus, edge) == 2;
    }), "every torus edge should have exactly two incident faces"));
    ASSERT_TRUE(require_bounds(torus, spec.width, spec.height, spec.depth));
    ASSERT_TRUE(require_test(std::ranges::any_of(torus.vertices, [](const Vertex& vertex) {
        return nearly_equal(vertex.position.x, 1.0F) &&
            nearly_equal(vertex.position.y, 0.0F) &&
            nearly_equal(vertex.position.z, 0.0F);
    }), "torus tube ratio should preserve the expected center hole"));
    quader::QVec3 outer_normal;
    ASSERT_TRUE(authored_face_normal(torus, torus.faces.front(), outer_normal));
    ASSERT_TRUE(require_test(outer_normal.x > 0.0F, "torus outer tube should wind outward"));
    ASSERT_TRUE(require_document_compiles(torus, "torus"));
    ASSERT_TRUE(require_valid_document(torus));
}

void radial_primitive_factories_clamp_counts_and_dimensions()
{
    CylinderPrimitiveSpec cylinder_spec;
    cylinder_spec.width = std::numeric_limits<float>::quiet_NaN();
    cylinder_spec.height = std::numeric_limits<float>::infinity();
    cylinder_spec.depth = -42.0F;
    cylinder_spec.side_count = -10;
    const Document cylinder = make_cylinder(cylinder_spec);
    ASSERT_TRUE(require_test(cylinder.vertices.size() == 6U, "cylinder side count should clamp to three"));
    ASSERT_TRUE(require_test(cylinder.faces.size() == 5U, "clamped cylinder should remain capped"));
    ASSERT_TRUE(require_finite_vertices(cylinder));
    ASSERT_TRUE(require_document_compiles(cylinder, "sanitized cylinder"));

    SpherePrimitiveSpec sphere_spec;
    sphere_spec.width = std::numeric_limits<float>::quiet_NaN();
    sphere_spec.height = std::numeric_limits<float>::infinity();
    sphere_spec.depth = -42.0F;
    sphere_spec.radial_segments = -10;
    sphere_spec.rings = -10;
    const Document sphere = make_sphere(sphere_spec);
    ASSERT_TRUE(require_test(sphere.vertices.size() == 5U, "sphere counts should clamp to three radial segments and two rings"));
    ASSERT_TRUE(require_test(sphere.faces.size() == 6U, "clamped sphere should retain a complete surface"));
    ASSERT_TRUE(require_finite_vertices(sphere));
    ASSERT_TRUE(require_document_compiles(sphere, "sanitized sphere"));

    cylinder_spec.side_count = 1000;
    const Document maximum_cylinder = make_cylinder(cylinder_spec);
    ASSERT_TRUE(require_test(maximum_cylinder.vertices.size() == 256U, "cylinder side count should clamp to 128"));

    sphere_spec.radial_segments = 1000;
    sphere_spec.rings = 2;
    const Document maximum_radial_sphere = make_sphere(sphere_spec);
    ASSERT_TRUE(require_test(maximum_radial_sphere.faces.size() == 256U, "sphere radial segments should clamp to 128"));

    sphere_spec.radial_segments = 3;
    sphere_spec.rings = 1000;
    const Document maximum_ring_sphere = make_sphere(sphere_spec);
    ASSERT_TRUE(require_test(maximum_ring_sphere.faces.size() == 384U, "sphere rings should clamp to 128"));

    ConePrimitiveSpec cone_spec;
    cone_spec.width = std::numeric_limits<float>::quiet_NaN();
    cone_spec.height = std::numeric_limits<float>::infinity();
    cone_spec.depth = -42.0F;
    cone_spec.side_count = -10;
    const Document cone = make_cone(cone_spec);
    ASSERT_TRUE(require_test(cone.vertices.size() == 4U, "cone side count should clamp to three"));
    ASSERT_TRUE(require_test(cone.faces.size() == 4U, "clamped cone should remain capped"));
    ASSERT_TRUE(require_finite_vertices(cone));
    ASSERT_TRUE(require_document_compiles(cone, "sanitized cone"));

    cone_spec.side_count = 1000;
    const Document maximum_cone = make_cone(cone_spec);
    ASSERT_TRUE(require_test(maximum_cone.vertices.size() == 129U, "cone side count should clamp to 128"));

    TorusPrimitiveSpec torus_spec;
    torus_spec.width = std::numeric_limits<float>::quiet_NaN();
    torus_spec.height = std::numeric_limits<float>::infinity();
    torus_spec.depth = -42.0F;
    torus_spec.major_segments = -10;
    torus_spec.minor_segments = -10;
    torus_spec.tube_radius_ratio = std::numeric_limits<float>::quiet_NaN();
    const Document torus = make_torus(torus_spec);
    ASSERT_TRUE(require_test(torus.vertices.size() == 9U, "torus segment counts should clamp to three"));
    ASSERT_TRUE(require_test(torus.faces.size() == 9U, "clamped torus should retain a closed surface"));
    ASSERT_TRUE(require_finite_vertices(torus));
    ASSERT_TRUE(require_document_compiles(torus, "sanitized torus"));

    torus_spec.major_segments = 1000;
    torus_spec.minor_segments = 3;
    const Document maximum_major_torus = make_torus(torus_spec);
    ASSERT_TRUE(require_test(maximum_major_torus.vertices.size() == 384U, "torus major segments should clamp to 128"));

    torus_spec.major_segments = 3;
    torus_spec.minor_segments = 1000;
    const Document maximum_minor_torus = make_torus(torus_spec);
    ASSERT_TRUE(require_test(maximum_minor_torus.vertices.size() == 384U, "torus minor segments should clamp to 128"));
}

void primitive_flip_faces_reverses_all_winding()
{
    CubePrimitiveSpec cube_spec;
    const Document cube = make_cube(cube_spec);
    cube_spec.flip_faces = true;
    const Document flipped_cube = make_cube(cube_spec);
    ASSERT_TRUE(require_faces_reversed(cube, flipped_cube));
    ASSERT_TRUE(require_document_compiles(flipped_cube, "flipped cube"));

    CylinderPrimitiveSpec cylinder_spec;
    cylinder_spec.side_count = 8;
    const Document cylinder = make_cylinder(cylinder_spec);
    cylinder_spec.flip_faces = true;
    const Document flipped_cylinder = make_cylinder(cylinder_spec);
    ASSERT_TRUE(require_faces_reversed(cylinder, flipped_cylinder));
    quader::QVec3 cylinder_centroid;
    ASSERT_TRUE(face_centroid(cylinder, cylinder.faces.front(), cylinder_centroid));
    quader::QVec3 flipped_cylinder_centroid;
    ASSERT_TRUE(face_centroid(
        flipped_cylinder,
        flipped_cylinder.faces.front(),
        flipped_cylinder_centroid));
    quader::QVec3 cylinder_normal;
    ASSERT_TRUE(authored_face_normal(
        cylinder, cylinder.faces.front(), cylinder_normal));
    ASSERT_TRUE(require_test(
        quader::dot(cylinder_normal, cylinder_centroid) > 0.0F,
        "default cylinder side winding should face outward"));
    quader::QVec3 flipped_cylinder_normal;
    ASSERT_TRUE(authored_face_normal(
        flipped_cylinder,
        flipped_cylinder.faces.front(),
        flipped_cylinder_normal));
    ASSERT_TRUE(require_test(
        quader::dot(flipped_cylinder_normal, flipped_cylinder_centroid) < 0.0F,
        "flipped cylinder side winding should face inward"));
    ASSERT_TRUE(require_document_compiles(flipped_cylinder, "flipped cylinder"));

    SpherePrimitiveSpec sphere_spec;
    sphere_spec.radial_segments = 8;
    sphere_spec.rings = 4;
    const Document sphere = make_sphere(sphere_spec);
    sphere_spec.flip_faces = true;
    const Document flipped_sphere = make_sphere(sphere_spec);
    ASSERT_TRUE(require_faces_reversed(sphere, flipped_sphere));
    ASSERT_TRUE(require_document_compiles(flipped_sphere, "flipped sphere"));

    ConePrimitiveSpec cone_spec;
    cone_spec.side_count = 8;
    const Document cone = make_cone(cone_spec);
    cone_spec.flip_faces = true;
    const Document flipped_cone = make_cone(cone_spec);
    ASSERT_TRUE(require_faces_reversed(cone, flipped_cone));
    ASSERT_TRUE(require_document_compiles(flipped_cone, "flipped cone"));

    TorusPrimitiveSpec torus_spec;
    torus_spec.major_segments = 8;
    torus_spec.minor_segments = 4;
    const Document torus = make_torus(torus_spec);
    torus_spec.flip_faces = true;
    const Document flipped_torus = make_torus(torus_spec);
    ASSERT_TRUE(require_faces_reversed(torus, flipped_torus));
    ASSERT_TRUE(require_document_compiles(flipped_torus, "flipped torus"));
}

} // namespace

TEST(QdrDocumentPrimitiveTests, CubeSegmentsRemainStable)
{
    cube_factory_keeps_segment_counts();
}

TEST(QdrDocumentPrimitiveTests, CubeDimensionsAreSanitized)
{
    cube_dimension_sanitization_stays_private_to_factory_source();
}

TEST(QdrDocumentPrimitiveTests, CylinderSettingsProduceExpectedTopology)
{
    cylinder_factory_honors_side_caps_dimensions_and_shading();
}

TEST(QdrDocumentPrimitiveTests,
     ThirtyTwoSideCylinderKeepsContinuousWallsAndSharpCapsInAuthoredCompilation)
{
    CylinderPrimitiveSpec spec;
    spec.side_count = 32;
    const Document cylinder = make_cylinder(spec);

    ASSERT_TRUE(require_test(cylinder.hard_normal_edges.empty(),
                             "new cylinder should not author hard wall edges"));
    ASSERT_TRUE(require_test(cylinder.soft_normal_edges.empty(),
                             "new cylinder should rely on default wall edges"));
    const CompiledMesh mesh =
        compile_document(cylinder, SurfaceShading::Authored);
    std::map<ElementId, std::vector<quader::QVec3>> normals_by_vertex;
    for (const CompiledVertex& vertex : mesh.vertices) {
        normals_by_vertex[vertex.source_vertex_id].push_back(vertex.normal);
    }

    ASSERT_EQ(normals_by_vertex.size(), cylinder.vertices.size());
    for (const Vertex& authored_vertex : cylinder.vertices) {
        const auto found = normals_by_vertex.find(authored_vertex.id);
        ASSERT_NE(found, normals_by_vertex.end());
        const std::vector<quader::QVec3>& normals = found->second;
        ASSERT_EQ(normals.size(), 3U);

        std::vector<quader::QVec3> wall_normals;
        std::vector<quader::QVec3> cap_normals;
        for (const quader::QVec3 normal : normals) {
            if (std::abs(normal.y) < 0.0001F) {
                wall_normals.push_back(normal);
            } else {
                cap_normals.push_back(normal);
            }
        }
        ASSERT_EQ(wall_normals.size(), 2U);
        ASSERT_EQ(cap_normals.size(), 1U);
        EXPECT_TRUE(nearly_equal(wall_normals[0].x, wall_normals[1].x) &&
                    nearly_equal(wall_normals[0].y, wall_normals[1].y) &&
                    nearly_equal(wall_normals[0].z, wall_normals[1].z));

        const float radial_length = std::sqrt(
            authored_vertex.position.x * authored_vertex.position.x +
            authored_vertex.position.z * authored_vertex.position.z);
        ASSERT_GT(radial_length, 0.0F);
        const quader::QVec3 expected_radial {
            authored_vertex.position.x / radial_length,
            0.0F,
            authored_vertex.position.z / radial_length,
        };
        EXPECT_TRUE(nearly_equal(
                        wall_normals.front().x, expected_radial.x, 0.0002F) &&
                    nearly_equal(
                        wall_normals.front().y, expected_radial.y, 0.0002F) &&
                    nearly_equal(
                        wall_normals.front().z, expected_radial.z, 0.0002F));
        EXPECT_GT(std::abs(cap_normals.front().y), 0.999F);
        EXPECT_LT(std::abs(quader::dot(
            wall_normals.front(), cap_normals.front())), 0.0001F);
    }
}

TEST(QdrDocumentPrimitiveTests, SphereSettingsProduceExpectedTopology)
{
    sphere_factory_honors_radial_ring_and_hemisphere_settings();
}

TEST(QdrDocumentPrimitiveTests, QuadSphereLevelsProduceWeldedClosedQuadSurfaces)
{
    for (bool hemisphere : { false, true }) {
        for (int subdivision : { 1, 2, 3, 6 }) {
            SCOPED_TRACE(::testing::Message() << "hemisphere=" << hemisphere
                << ", subdivision=" << subdivision);
            SpherePrimitiveSpec spec;
            spec.type = SphereType::Quad;
            spec.subdivision = subdivision;
            spec.hemisphere = hemisphere;
            const Document document = make_sphere(spec);
            const std::size_t divisions = std::size_t { 1 } << subdivision;
            const std::size_t face_count = (hemisphere ? 4U : 6U) * divisions * divisions;
            ASSERT_EQ(document.faces.size(), face_count);
            ASSERT_EQ(document.vertices.size(), face_count + 2U);

            std::map<std::pair<ElementId, ElementId>, std::pair<int, int>> incidence;
            std::map<ElementId, std::vector<ElementId>> adjacency;
            std::set<std::tuple<float, float, float>> positions;
            for (const Vertex& vertex : document.vertices) {
                EXPECT_TRUE(positions.emplace(vertex.position.x, vertex.position.y,
                    vertex.position.z).second) << "cube seams must share authored vertices";
            }
            for (const Face& face : document.faces) {
                ASSERT_EQ(face.vertices.size(), 4U);
                EXPECT_EQ(std::set<ElementId>(face.vertices.begin(), face.vertices.end()).size(), 4U);
                EXPECT_EQ(face.normal_shading, SurfaceShading::Authored);
                EXPECT_TRUE(face.uvs.empty());
                for (std::size_t corner = 0; corner < 4U; ++corner) {
                    const ElementId a = face.vertices[corner];
                    const ElementId b = face.vertices[(corner + 1U) % 4U];
                    auto& counts = incidence[{ std::min(a, b), std::max(a, b) }];
                    ++counts.first;
                    counts.second += a < b ? 1 : -1;
                    adjacency[a].push_back(b);
                    adjacency[b].push_back(a);
                }
            }
            EXPECT_EQ(incidence.size(), 2U * face_count);
            EXPECT_EQ(adjacency.size(), document.vertices.size());
            for (const auto& [edge, counts] : incidence) {
                EXPECT_EQ(counts.first, 2) << edge.first << ", " << edge.second;
                EXPECT_EQ(counts.second, 0) << "adjacent loops must traverse a shared edge oppositely";
            }
            std::vector<ElementId> pending { document.vertices.front().id };
            std::set<ElementId> visited;
            while (!pending.empty()) {
                const ElementId vertex = pending.back();
                pending.pop_back();
                if (visited.insert(vertex).second) {
                    const auto& neighbors = adjacency.at(vertex);
                    pending.insert(pending.end(), neighbors.begin(), neighbors.end());
                }
            }
            EXPECT_EQ(visited.size(), document.vertices.size());
            ASSERT_TRUE(require_valid_document(document));
            const CompiledMesh mesh = compile_document(document);
            EXPECT_EQ(mesh.primitives.size(), face_count);
            EXPECT_EQ(mesh.indices.size(), face_count * 6U);
        }
    }
}

TEST(QdrDocumentPrimitiveTests, QuadSpherePreservesEllipsoidBoundsCapAndFaceWinding)
{
    for (bool hemisphere : { false, true }) {
        SCOPED_TRACE(hemisphere);
        SpherePrimitiveSpec spec;
        spec.width = 4.0F;
        spec.height = 6.0F;
        spec.depth = 2.0F;
        spec.hemisphere = hemisphere;
        spec.type = SphereType::Quad;
        spec.subdivision = 3;
        const Document document = make_sphere(spec);
        ASSERT_TRUE(require_bounds(document, spec.width, spec.height, spec.depth));
        ASSERT_TRUE(require_finite_vertices(document));
        for (const Vertex& vertex : document.vertices) {
            const float x = vertex.position.x / (spec.width * 0.5F);
            const float y = hemisphere
                ? (vertex.position.y + spec.height * 0.5F) / spec.height
                : vertex.position.y / (spec.height * 0.5F);
            const float z = vertex.position.z / (spec.depth * 0.5F);
            const float radius_squared = x * x + y * y + z * z;
            if (hemisphere && nearly_equal(vertex.position.y, -spec.height * 0.5F)) {
                EXPECT_LE(radius_squared, 1.00001F);
            } else {
                EXPECT_NEAR(radius_squared, 1.0F, 0.00001F);
            }
        }
        std::size_t cap_faces = 0;
        for (const Face& face : document.faces) {
            quader::QVec3 normal;
            quader::QVec3 centroid;
            ASSERT_TRUE(authored_face_normal(document, face, normal));
            ASSERT_TRUE(face_centroid(document, face, centroid));
            EXPECT_GT(quader::dot(normal, centroid), 0.0F);
            const bool is_cap = hemisphere && std::ranges::all_of(face.vertices,
                [&](ElementId id) {
                    return nearly_equal(find_vertex(document, id)->position.y, -spec.height * 0.5F);
                });
            if (is_cap) {
                ++cap_faces;
                EXPECT_LT(normal.y, 0.0F);
                EXPECT_NEAR(normal.x, 0.0F, 0.00001F);
                EXPECT_NEAR(normal.z, 0.0F, 0.00001F);
            }
        }
        EXPECT_EQ(cap_faces, hemisphere ? 64U : 0U);
        spec.flip_faces = true;
        const Document flipped = make_sphere(spec);
        ASSERT_TRUE(require_faces_reversed(document, flipped));
        ASSERT_TRUE(require_document_compiles(flipped, "flipped quad sphere"));
        ASSERT_TRUE(require_valid_document(flipped));
    }
}

TEST(QdrDocumentPrimitiveTests, QuadSphereIsDeterministicAndUsesOnlyActiveTopologySettings)
{
    SpherePrimitiveSpec spec;
    spec.type = SphereType::Quad;
    EXPECT_EQ(spec.subdivision, 3);
    const Document initial = make_sphere(spec);
    spec.radial_segments = -10;
    spec.rings = std::numeric_limits<int>::max();
    const Document repeated = make_sphere(spec);
    ASSERT_EQ(initial.vertices.size(), repeated.vertices.size());
    ASSERT_EQ(initial.faces.size(), repeated.faces.size());
    for (std::size_t index = 0; index < initial.vertices.size(); ++index) {
        EXPECT_EQ(initial.vertices[index].id, repeated.vertices[index].id);
        EXPECT_FLOAT_EQ(initial.vertices[index].position.x, repeated.vertices[index].position.x);
        EXPECT_FLOAT_EQ(initial.vertices[index].position.y, repeated.vertices[index].position.y);
        EXPECT_FLOAT_EQ(initial.vertices[index].position.z, repeated.vertices[index].position.z);
    }
    for (std::size_t index = 0; index < initial.faces.size(); ++index) {
        EXPECT_EQ(initial.faces[index].id, repeated.faces[index].id);
        EXPECT_EQ(initial.faces[index].vertices, repeated.faces[index].vertices);
    }
    spec.subdivision = std::numeric_limits<int>::min();
    EXPECT_EQ(make_sphere(spec).faces.size(), 24U);
    spec.subdivision = std::numeric_limits<int>::max();
    EXPECT_EQ(make_sphere(spec).faces.size(), 24576U);
}

TEST(QdrDocumentPrimitiveTests, ConeSettingsProduceExpectedTopology)
{
    cone_factory_honors_side_cap_dimensions_and_shading();
}

TEST(QdrDocumentPrimitiveTests, TorusSettingsProduceExpectedTopology)
{
    torus_factory_honors_segment_ratio_dimension_and_shading_settings();
}

TEST(QdrDocumentPrimitiveTests, RadialSettingsClampToSupportedRanges)
{
    radial_primitive_factories_clamp_counts_and_dimensions();
}

TEST(QdrDocumentPrimitiveTests, FlipFacesReversesEveryPrimitiveLoop)
{
    primitive_flip_faces_reverses_all_winding();
}
