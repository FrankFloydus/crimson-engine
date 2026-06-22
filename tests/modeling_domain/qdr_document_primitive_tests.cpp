////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.h"

#include <modeling/polygon/document.h>

#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

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

void primitive_factories_keep_segment_counts_after_split()
{
    CubePrimitiveSpec cube_spec;
    cube_spec.width_segments = 2;
    cube_spec.height_segments = 1;
    cube_spec.depth_segments = 3;
    const Document cube = make_cube(cube_spec);
    require_test(cube.faces.size() == 22U, "segmented cube face count should survive primitive source split");
    require_document_compiles(cube, "segmented cube");

    PlanePrimitiveSpec plane_spec;
    plane_spec.width_segments = 2;
    plane_spec.depth_segments = 3;
    const Document plane = make_plane(plane_spec);
    require_test(plane.vertices.size() == 12U, "segmented plane vertex grid should survive primitive source split");
    require_test(plane.faces.size() == 6U, "segmented plane face count should survive primitive source split");
    require_document_compiles(plane, "segmented plane");

    CylinderPrimitiveSpec cylinder_spec;
    cylinder_spec.sides = 8;
    cylinder_spec.cap_bottom = true;
    cylinder_spec.cap_top = true;
    const Document cylinder = make_cylinder(cylinder_spec);
    require_test(cylinder.faces.size() == 10U, "capped cylinder should contain side faces plus two caps");
    require_document_compiles(cylinder, "cylinder");

    ConePrimitiveSpec cone_spec;
    cone_spec.sides = 6;
    cone_spec.cap_bottom = true;
    const Document cone = make_cone(cone_spec);
    require_test(cone.faces.size() == 7U, "capped cone should contain side faces plus one cap");
    require_document_compiles(cone, "cone");

    TorusPrimitiveSpec torus_spec;
    torus_spec.major_segments = 5;
    torus_spec.minor_segments = 4;
    const Document torus = make_torus(torus_spec);
    require_test(torus.faces.size() == 20U, "torus face grid should survive primitive source split");
    require_document_compiles(torus, "torus");
}

void sphere_factories_keep_orientation_and_topology_after_split()
{
    QuadSpherePrimitiveSpec quad_sphere_spec;
    quad_sphere_spec.segments_per_face = 2;
    const Document quad_sphere = make_quad_sphere(quad_sphere_spec);
    require_test(quad_sphere.faces.size() == 24U, "quad sphere should create six subdivided cube faces");
    require_document_compiles(quad_sphere, "quad sphere");

    UvSpherePrimitiveSpec uv_sphere_spec;
    uv_sphere_spec.segments = 6;
    uv_sphere_spec.rings = 4;
    const Document uv_sphere = make_uv_sphere(uv_sphere_spec);
    require_test(uv_sphere.faces.size() == 24U, "UV sphere should create cap and latitude faces");
    require_document_compiles(uv_sphere, "UV sphere");

    IcoSpherePrimitiveSpec ico_sphere_spec;
    ico_sphere_spec.subdivisions = 1;
    const Document ico_sphere = make_ico_sphere(ico_sphere_spec);
    require_test(ico_sphere.faces.size() == 80U, "ico sphere subdivision should produce four triangles per source triangle");
    require_document_compiles(ico_sphere, "ico sphere");

    for (const Face& face : ico_sphere.faces) {
        Vec3 centroid;
        for (const ElementId vertex_id : face.vertices) {
            const Vertex* vertex = find_vertex(ico_sphere, vertex_id);
            require_test(vertex != nullptr, "ico sphere face should resolve every vertex");
            centroid += vertex->position;
        }
        centroid = centroid / static_cast<float>(face.vertices.size());
        require_test(dot(face_normal(ico_sphere, face), centroid) > 0.0F, "ico sphere faces should keep outward orientation");
    }
}

void primitive_dimension_sanitization_stays_private_to_factory_source()
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
    for (const Vertex& vertex : cube.vertices) {
        require_test(
            std::isfinite(vertex.position.x) && std::isfinite(vertex.position.y) && std::isfinite(vertex.position.z),
            "sanitized primitive vertices should remain finite");
    }

    const Vertex* first = find_vertex(cube, cube.faces.front().vertices.front());
    require_test(first != nullptr, "sanitized cube should resolve its first vertex");
    require_test(nearly_equal(std::abs(first->position.z), 0.0005F), "negative primitive dimensions should clamp to the package minimum");
}

} // namespace

TEST_CASE("qdr_document_primitive_tests", "[document-primitive]")
{
    primitive_factories_keep_segment_counts_after_split();
    sphere_factories_keep_orientation_and_topology_after_split();
    primitive_dimension_sanitization_stays_private_to_factory_source();
}
