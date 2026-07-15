////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <mesh/polygon/document.hpp>
#include <mesh/polygon/document_validation.hpp>

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

void require_document_compiles(const Document& document, const char* label)
{
    static_cast<void>(label);
    const CompiledMesh mesh = compile_document(document);
    require_test(!document.vertices.empty(), "primitive should contain vertices");
    require_test(!document.faces.empty(), "primitive should contain faces");
    require_test(!mesh.vertices.empty(), "primitive should compile vertices");
    require_test(!mesh.indices.empty() && mesh.indices.size() % 3U == 0U, "primitive should compile triangle indices");
    require_test(mesh.primitives.size() == document.faces.size(), "primitive should emit one compiled primitive per face");
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

void require_bounds(const Document& document, float width, float height, float depth)
{
    const Bounds bounds = document_bounds(document);
    require_test(nearly_equal(bounds.minimum.x, -width * 0.5F), "primitive minimum x should match its width");
    require_test(nearly_equal(bounds.maximum.x, width * 0.5F), "primitive maximum x should match its width");
    require_test(nearly_equal(bounds.minimum.y, -height * 0.5F), "primitive minimum y should match its height");
    require_test(nearly_equal(bounds.maximum.y, height * 0.5F), "primitive maximum y should match its height");
    require_test(nearly_equal(bounds.minimum.z, -depth * 0.5F), "primitive minimum z should match its depth");
    require_test(nearly_equal(bounds.maximum.z, depth * 0.5F), "primitive maximum z should match its depth");
}

void require_finite_vertices(const Document& document)
{
    for (const Vertex& vertex : document.vertices) {
        require_test(
            std::isfinite(vertex.position.x) &&
                std::isfinite(vertex.position.y) &&
                std::isfinite(vertex.position.z),
            "sanitized primitive vertices should remain finite");
    }
}

void require_valid_document(const Document& document)
{
    const PolygonDocumentValidationReport report = validate_polygon_document(document);
    require_test(report.ok(), "generated primitive should pass polygon document validation");
    require_test(report.diagnostics.empty(), "generated primitive should not produce validation diagnostics");
}

void require_faces_reversed(const Document& outward, const Document& flipped)
{
    require_test(outward.vertices.size() == flipped.vertices.size(), "flipping should preserve primitive vertices");
    require_test(outward.faces.size() == flipped.faces.size(), "flipping should preserve primitive faces");
    for (std::size_t face_index = 0; face_index < outward.faces.size(); ++face_index) {
        const Face& outward_face = outward.faces[face_index];
        const Face& flipped_face = flipped.faces[face_index];
        require_test(
            std::ranges::equal(outward_face.vertices, std::views::reverse(flipped_face.vertices)),
            "flip faces should reverse every authored face loop");
        require_test(
            outward_face.normal_shading == flipped_face.normal_shading,
            "flip faces should preserve authored shading");
    }
}

quader::QVec3 authored_face_normal(const Document& document, const Face& face)
{
    require_test(face.vertices.size() >= 3U, "face normal requires at least three vertices");
    const Vertex* a = find_vertex(document, face.vertices[0]);
    const Vertex* b = find_vertex(document, face.vertices[1]);
    const Vertex* c = find_vertex(document, face.vertices[2]);
    require_test(a != nullptr && b != nullptr && c != nullptr, "face normal vertices should resolve");
    if (a == nullptr || b == nullptr || c == nullptr) {
        return {};
    }
    return quader::cross(b->position - a->position, c->position - a->position);
}

quader::QVec3 face_centroid(const Document& document, const Face& face)
{
    quader::QVec3 centroid;
    for (ElementId vertex_id : face.vertices) {
        const Vertex* vertex = find_vertex(document, vertex_id);
        require_test(vertex != nullptr, "face centroid vertices should resolve");
        if (vertex != nullptr) {
            centroid += vertex->position;
        }
    }
    return centroid / static_cast<float>(face.vertices.size());
}

void cube_factory_keeps_segment_counts()
{
    CubePrimitiveSpec cube_spec;
    cube_spec.width_segments = 2;
    cube_spec.height_segments = 1;
    cube_spec.depth_segments = 3;
    const Document cube = make_cube(cube_spec);
    require_test(cube.faces.size() == 22U, "segmented cube face count should be stable");
    require_test(std::ranges::all_of(cube.faces, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Flat;
    }), "cube faces should use flat authored shading");
    require_document_compiles(cube, "segmented cube");
    require_valid_document(cube);
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
    require_document_compiles(cube, "sanitized cube");
    require_finite_vertices(cube);

    const Vertex* first = find_vertex(cube, cube.faces.front().vertices.front());
    require_test(first != nullptr, "sanitized cube should resolve its first vertex");
    require_test(nearly_equal(std::abs(first->position.z), 0.0005F), "negative primitive dimensions should clamp to the package minimum");
}

void cylinder_factory_honors_side_caps_dimensions_and_shading()
{
    CylinderPrimitiveSpec spec;
    spec.width = 4.0F;
    spec.height = 6.0F;
    spec.depth = 2.0F;
    spec.side_count = 8;
    const Document capped = make_cylinder(spec);

    require_test(capped.vertices.size() == 16U, "cylinder should create two vertices per side");
    require_test(capped.faces.size() == 10U, "capped cylinder should create one face per side and two caps");
    require_test(std::ranges::all_of(capped.faces.begin(), capped.faces.begin() + 8, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Smooth;
    }), "cylinder side faces should use smooth authored shading");
    require_test(capped.faces[8].normal_shading == SurfaceShading::Flat, "cylinder bottom cap should be flat");
    require_test(capped.faces[9].normal_shading == SurfaceShading::Flat, "cylinder top cap should be flat");
    require_bounds(capped, spec.width, spec.height, spec.depth);
    require_test(authored_face_normal(capped, capped.faces[8]).y < 0.0F, "cylinder bottom cap should wind outward");
    require_test(authored_face_normal(capped, capped.faces[9]).y > 0.0F, "cylinder top cap should wind outward");
    require_document_compiles(capped, "capped cylinder");
    require_valid_document(capped);

    spec.capped = false;
    const Document open = make_cylinder(spec);
    require_test(open.vertices.size() == 16U, "open cylinder should preserve its side vertices");
    require_test(open.faces.size() == 8U, "open cylinder should omit both cap faces");
    require_test(std::ranges::all_of(open.faces, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Smooth;
    }), "open cylinder should retain smooth side shading");
    require_document_compiles(open, "open cylinder");
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

    require_test(sphere.vertices.size() == 26U, "sphere should share its poles and radial ring vertices");
    require_test(sphere.faces.size() == 32U, "sphere face count should equal radial segments times rings");
    require_test(std::ranges::all_of(sphere.faces, [](const Face& face) {
        return face.normal_shading == SurfaceShading::Smooth;
    }), "full sphere faces should use smooth authored shading");
    require_bounds(sphere, spec.width, spec.height, spec.depth);
    require_test(
        quader::dot(authored_face_normal(sphere, sphere.faces.front()), face_centroid(sphere, sphere.faces.front())) > 0.0F,
        "sphere surface should wind outward");
    require_document_compiles(sphere, "full sphere");
    require_valid_document(sphere);

    spec.hemisphere = true;
    const Document hemisphere = make_sphere(spec);
    require_test(hemisphere.vertices.size() == 33U, "hemisphere should create one pole and the requested rings");
    require_test(hemisphere.faces.size() == 33U, "hemisphere should add one flat base to its curved faces");
    require_test(std::ranges::all_of(
        hemisphere.faces.begin(), hemisphere.faces.end() - 1, [](const Face& face) {
            return face.normal_shading == SurfaceShading::Smooth;
        }), "hemisphere curved faces should use smooth authored shading");
    require_test(hemisphere.faces.back().normal_shading == SurfaceShading::Flat, "hemisphere base should be flat");
    require_test(authored_face_normal(hemisphere, hemisphere.faces.back()).y < 0.0F, "hemisphere base should wind outward");
    require_bounds(hemisphere, spec.width, spec.height, spec.depth);
    require_document_compiles(hemisphere, "solid hemisphere");
    require_valid_document(hemisphere);
}

void radial_primitive_factories_clamp_counts_and_dimensions()
{
    CylinderPrimitiveSpec cylinder_spec;
    cylinder_spec.width = std::numeric_limits<float>::quiet_NaN();
    cylinder_spec.height = std::numeric_limits<float>::infinity();
    cylinder_spec.depth = -42.0F;
    cylinder_spec.side_count = -10;
    const Document cylinder = make_cylinder(cylinder_spec);
    require_test(cylinder.vertices.size() == 6U, "cylinder side count should clamp to three");
    require_test(cylinder.faces.size() == 5U, "clamped cylinder should remain capped");
    require_finite_vertices(cylinder);
    require_document_compiles(cylinder, "sanitized cylinder");

    SpherePrimitiveSpec sphere_spec;
    sphere_spec.width = std::numeric_limits<float>::quiet_NaN();
    sphere_spec.height = std::numeric_limits<float>::infinity();
    sphere_spec.depth = -42.0F;
    sphere_spec.radial_segments = -10;
    sphere_spec.rings = -10;
    const Document sphere = make_sphere(sphere_spec);
    require_test(sphere.vertices.size() == 5U, "sphere counts should clamp to three radial segments and two rings");
    require_test(sphere.faces.size() == 6U, "clamped sphere should retain a complete surface");
    require_finite_vertices(sphere);
    require_document_compiles(sphere, "sanitized sphere");

    cylinder_spec.side_count = 1000;
    const Document maximum_cylinder = make_cylinder(cylinder_spec);
    require_test(maximum_cylinder.vertices.size() == 256U, "cylinder side count should clamp to 128");

    sphere_spec.radial_segments = 1000;
    sphere_spec.rings = 2;
    const Document maximum_radial_sphere = make_sphere(sphere_spec);
    require_test(maximum_radial_sphere.faces.size() == 256U, "sphere radial segments should clamp to 128");

    sphere_spec.radial_segments = 3;
    sphere_spec.rings = 1000;
    const Document maximum_ring_sphere = make_sphere(sphere_spec);
    require_test(maximum_ring_sphere.faces.size() == 384U, "sphere rings should clamp to 128");
}

void primitive_flip_faces_reverses_all_winding()
{
    CubePrimitiveSpec cube_spec;
    const Document cube = make_cube(cube_spec);
    cube_spec.flip_faces = true;
    const Document flipped_cube = make_cube(cube_spec);
    require_faces_reversed(cube, flipped_cube);
    require_document_compiles(flipped_cube, "flipped cube");

    CylinderPrimitiveSpec cylinder_spec;
    cylinder_spec.side_count = 8;
    const Document cylinder = make_cylinder(cylinder_spec);
    cylinder_spec.flip_faces = true;
    const Document flipped_cylinder = make_cylinder(cylinder_spec);
    require_faces_reversed(cylinder, flipped_cylinder);
    const quader::QVec3 cylinder_centroid = face_centroid(cylinder, cylinder.faces.front());
    const quader::QVec3 flipped_cylinder_centroid =
        face_centroid(flipped_cylinder, flipped_cylinder.faces.front());
    require_test(
        quader::dot(authored_face_normal(cylinder, cylinder.faces.front()), cylinder_centroid) > 0.0F,
        "default cylinder side winding should face outward");
    require_test(
        quader::dot(authored_face_normal(flipped_cylinder, flipped_cylinder.faces.front()), flipped_cylinder_centroid) < 0.0F,
        "flipped cylinder side winding should face inward");
    require_document_compiles(flipped_cylinder, "flipped cylinder");

    SpherePrimitiveSpec sphere_spec;
    sphere_spec.radial_segments = 8;
    sphere_spec.rings = 4;
    const Document sphere = make_sphere(sphere_spec);
    sphere_spec.flip_faces = true;
    const Document flipped_sphere = make_sphere(sphere_spec);
    require_faces_reversed(sphere, flipped_sphere);
    require_document_compiles(flipped_sphere, "flipped sphere");
}

} // namespace

TEST(QdrDocumentPrimitiveTests, CubeSegmentsDimensionsAndSanitization)
{
    cube_factory_keeps_segment_counts();
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
