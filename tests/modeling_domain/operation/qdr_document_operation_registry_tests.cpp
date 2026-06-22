////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <mesh/polygon/poly_operation_registry.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <string>
#include <string_view>

namespace qp = quader_poly;

namespace {

/**
 * Implements the Expected Operation modeling operation data used by the automated test suite.
 */
struct ExpectedOperation {
    std::string_view id;
    std::string_view class_name;
};

bool ends_with(std::string_view value, std::string_view suffix)
{
    return value.size() >= suffix.size()
        && value.substr(value.size() - suffix.size()) == suffix;
}

constexpr std::array<ExpectedOperation, 44> k_expected_operations {{
    { "apply_uv", "ApplyUvOperation" },
    { "assign_selected_face_material_slot", "AssignMaterialSlotOperation" },
    { "translate_selection", "TranslateSelectionOperation" },
    { "transform_selection", "TransformSelectionOperation" },
    { "snap_selected_vertices_to_active", "SnapVerticesToActiveOperation" },
    { "merge_selected_vertices_to_active", "MergeVerticesToActiveOperation" },
    { "merge_selected_vertices_to_center", "MergeVerticesToCenterOperation" },
    { "merge_selected_vertices_by_distance", "MergeVerticesByDistanceOperation" },
    { "bevel_selected_vertices", "BevelVerticesOperation" },
    { "connect_selected_vertices", "ConnectVerticesOperation" },
    { "dissolve_selected_vertices", "DissolveVerticesOperation" },
    { "connect_selected_edges", "ConnectEdgesOperation" },
    { "split_selected_edges", "SplitEdgesOperation" },
    { "dissolve_selected_edges", "DissolveEdgesOperation" },
    { "merge_selected_edges", "MergeEdgesOperation" },
    { "collapse_selected_edges", "CollapseEdgesOperation" },
    { "fill_hole_from_selected_edges", "FillHoleFromEdgesOperation" },
    { "bevel_selected_edges", "BevelEdgesOperation" },
    { "bridge_edge_pairs", "BridgeEdgePairsOperation" },
    { "bridge_edge_boundaries", "BridgeEdgeBoundariesOperation" },
    { "bridge_selected_edges", "BridgeEdgesOperation" },
    { "combine_selected_faces", "CombineFacesOperation" },
    { "collapse_selected_faces", "CollapseFacesOperation" },
    { "radial_align_selection", "RadialAlignOperation" },
    { "flip_selected_face_normals", "FlipFaceNormalsOperation" },
    { "recalculate_selected_face_normals", "RecalculateFaceNormalsOperation" },
    { "shade_selected_faces_smooth", "ShadeFacesOperation" },
    { "shade_selected_faces_flat", "ShadeFacesOperation" },
    { "harden_selected_edge_normals", "EdgeNormalHardnessOperation" },
    { "soften_selected_edge_normals", "EdgeNormalHardnessOperation" },
    { "delete_selection", "DeleteSelectionOperation" },
    { "plane_cut", "PlaneCutOperation" },
    { "insert_edge_loop", "InsertEdgeLoopOperation" },
    { "slice_selected_quads", "SliceQuadsOperation" },
    { "knife_segment", "KnifeSegmentOperation" },
    { "knife_stroke", "KnifeStrokeOperation" },
    { "extrude_selected_elements", "ExtrudeElementsOperation" },
    { "inset_selected_elements", "InsetElementsOperation" },
    { "inset_selected_faces", "InsetFacesOperation" },
    { "extrude_selected_faces", "ExtrudeFacesOperation" },
    { "detach_selected_faces", "DetachFacesOperation" },
    { "extract_selected_faces", "ExtractFacesOperation" },
    { "bridge_selected_faces", "BridgeFacesOperation" },
    { "thicken_selected_faces", "ThickenFacesOperation" },
}};

} // namespace

TEST_CASE("poly operation registry exposes every public poly operation id", "[operation][registry]")
{
    const qp::PolyOperationRegistry& registry = qp::poly_operation_registry();
    const auto operations = registry.operations();
    REQUIRE(operations.size() == k_expected_operations.size());

    for (const qp::PolyOperationMetadata& operation : operations) {
        CHECK(ends_with(operation.class_name, "Operation"));
    }

    for (const ExpectedOperation& expected : k_expected_operations) {
        const qp::PolyOperationMetadata* operation = registry.find_by_id(expected.id);
        REQUIRE(operation != nullptr);
        CHECK(std::string(operation->id) == std::string(expected.id));
        CHECK_FALSE(operation->label.empty());
        CHECK(std::string(operation->class_name) == std::string(expected.class_name));
        const qp::PolyOperationMetadata* routed_operation = qp::poly_operation_for_id(expected.id);
        REQUIRE(routed_operation != nullptr);
        CHECK(routed_operation == operation);
        CHECK(registry.contains(expected.id));
    }

    CHECK(qp::poly_operation_for_id("missing") == nullptr);
    CHECK_FALSE(registry.contains("missing"));
}
