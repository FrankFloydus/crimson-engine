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
#include <ranges>

namespace {

using namespace quader_poly;

bool nearly_equal(float left, float right, float epsilon = 0.0001F)
{
    return std::abs(left - right) <= epsilon;
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
        return face.normal_shading == SurfaceShading::Flat;
    }), "cube faces should use flat authored shading"));
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
        return face.normal_shading == SurfaceShading::Smooth;
    }), "cylinder side faces should use smooth authored shading"));
    ASSERT_TRUE(require_test(capped.faces[8].normal_shading == SurfaceShading::Flat, "cylinder bottom cap should be flat"));
    ASSERT_TRUE(require_test(capped.faces[9].normal_shading == SurfaceShading::Flat, "cylinder top cap should be flat"));
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
        return face.normal_shading == SurfaceShading::Smooth;
    }), "open cylinder should retain smooth side shading"));
    ASSERT_TRUE(require_document_compiles(open, "open cylinder"));
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
        return face.normal_shading == SurfaceShading::Smooth;
    }), "full sphere faces should use smooth authored shading"));
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
            return face.normal_shading == SurfaceShading::Smooth;
        }), "hemisphere curved faces should use smooth authored shading"));
    ASSERT_TRUE(require_test(hemisphere.faces.back().normal_shading == SurfaceShading::Flat, "hemisphere base should be flat"));
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

TEST(QdrDocumentPrimitiveTests, SphereSettingsProduceExpectedTopology)
{
    sphere_factory_honors_radial_ring_and_hemisphere_settings();
}

TEST(QdrDocumentPrimitiveTests, RadialSettingsClampToSupportedRanges)
{
    radial_primitive_factories_clamp_counts_and_dimensions();
}

TEST(QdrDocumentPrimitiveTests, FlipFacesReversesEveryPrimitiveLoop)
{
    primitive_flip_faces_reverses_all_winding();
}
