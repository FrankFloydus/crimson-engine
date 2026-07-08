////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "test_support.hpp"

#include <mesh/polygon/document.hpp>

#include <cmath>
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

void cube_factory_keeps_segment_counts()
{
    CubePrimitiveSpec cube_spec;
    cube_spec.width_segments = 2;
    cube_spec.height_segments = 1;
    cube_spec.depth_segments = 3;
    const Document cube = make_cube(cube_spec);
    require_test(cube.faces.size() == 22U, "segmented cube face count should be stable");
    require_document_compiles(cube, "segmented cube");
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

TEST(QdrDocumentPrimitiveTests, QdrDocumentPrimitiveTests) {
    cube_factory_keeps_segment_counts();
    cube_dimension_sanitization_stays_private_to_factory_source();
}
