////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/commands/ModelingCommandToolCatalog.h"

#include "editor/commands/ActivateViewportToolActionTool.h"
#include "editor/commands/CustomInstantCommandTool.h"
#include "editor/commands/ModelingCommandDispatcher.h"
#include "editor/commands/RuntimeCommandTool.h"
#include "editor/commands/ContextOperationCommandTool.h"
#include "editor/commands/InstantOperationCommandTool.h"
#include "editor/commands/PreviewOperationCommandTool.h"
#include "editor/commands/modeling/AssignMaterialTool.h"
#include "editor/commands/modeling/FlipHorizontalTool.h"
#include "editor/commands/modeling/MirrorSelectionTool.h"
#include "editor/commands/modeling/SelectAllTool.h"

#include <gtest/gtest.h>

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_set>

namespace quader {
namespace {

TEST(ModelingToolCatalogTests, ContainsEveryInstantOperationCommandTool) {
    constexpr std::array expectedOperations{
            ModelingInstantOperationId::DeleteSelection,
            ModelingInstantOperationId::DuplicateMeshes,
            ModelingInstantOperationId::CombineMeshes,
            ModelingInstantOperationId::ExtractFaces,
            ModelingInstantOperationId::DetachFaces,
            ModelingInstantOperationId::Bridge,
            ModelingInstantOperationId::SnapVerticesToActive,
            ModelingInstantOperationId::ConnectVertices,
            ModelingInstantOperationId::DissolveVertices,
            ModelingInstantOperationId::BevelVertices,
            ModelingInstantOperationId::MergeVerticesToActive,
            ModelingInstantOperationId::MergeVerticesToCenter,
            ModelingInstantOperationId::SnapEdgesToActive,
            ModelingInstantOperationId::ConnectEdges,
            ModelingInstantOperationId::SplitEdges,
            ModelingInstantOperationId::DissolveEdges,
            ModelingInstantOperationId::MergeEdges,
            ModelingInstantOperationId::CollapseEdges,
            ModelingInstantOperationId::FillHole,
            ModelingInstantOperationId::CombineFaces,
            ModelingInstantOperationId::CollapseFaces,
            ModelingInstantOperationId::RadialAlign,
            ModelingInstantOperationId::FlipFaceNormals,
            ModelingInstantOperationId::RecalculateFaceNormals,
            ModelingInstantOperationId::ShadeFacesSmooth,
            ModelingInstantOperationId::ShadeFacesFlat,
            ModelingInstantOperationId::HardenEdgeNormals,
            ModelingInstantOperationId::SoftenEdgeNormals,
    };

    const std::span<const InstantCommandTool* const> tools = modelingInstantOperationCommandTools();
    ASSERT_EQ(tools.size(), expectedOperations.size());

    for (const ModelingInstantOperationId operation : expectedOperations) {
        const InstantCommandTool* tool = findModelingInstantCommandTool(operation);
        ASSERT_NE(tool, nullptr);
        EXPECT_TRUE(tool->hasInstantOperation());
        EXPECT_EQ(tool->operation(), operation);
        EXPECT_EQ(tool->kind(), CommandToolKind::Instant);
        EXPECT_EQ(tool->previewKind(), ModelingActionPreviewKind::None);
        EXPECT_EQ(tool->historyPolicy().mode, ModelingCommandHistoryMode::ModelingUndoRedo);
        EXPECT_FALSE(tool->id().empty());
        EXPECT_FALSE(tool->label().empty());
        EXPECT_FALSE(tool->operationId().empty());
    }
}

TEST(ModelingToolCatalogTests, CommandFamiliesExposeTheApprovedHierarchy) {
    static_assert(std::is_base_of_v<CommandTool, InstantCommandTool>);
    static_assert(std::is_base_of_v<CommandTool, ContextActionTool>);
    static_assert(std::is_base_of_v<CommandTool, PreviewCommandTool>);
    static_assert(std::is_base_of_v<InstantCommandTool, InstantOperationCommandTool>);
    static_assert(std::is_base_of_v<InstantCommandTool, RuntimeCommandTool>);
    static_assert(std::is_base_of_v<InstantCommandTool, CustomInstantCommandTool>);
    static_assert(std::is_base_of_v<ContextActionTool, ContextOperationCommandTool>);
    static_assert(std::is_base_of_v<ContextActionTool, ActivateViewportToolActionTool>);
    static_assert(std::is_base_of_v<PreviewCommandTool, PreviewOperationCommandTool>);

    static_assert(std::is_base_of_v<CustomInstantCommandTool, SelectAllTool>);
    static_assert(std::is_base_of_v<CustomInstantCommandTool, AssignMaterialTool>);
    static_assert(std::is_base_of_v<RuntimeCommandTool, MirrorSelectionTool>);
    static_assert(std::is_base_of_v<RuntimeCommandTool, FlipHorizontalTool>);
}

TEST(ModelingToolCatalogTests, RegisteredToolsExposeConsistentDescriptorsAndPolicies) {
    std::unordered_set<std::string> ids;
    const std::span<const CommandTool* const> tools = modelingCommandTools();
    ASSERT_EQ(
            tools.size(),
            modelingInstantCommandTools().size() + modelingContextActionTools().size() + modelingPreviewCommandTools().size());

    for (const CommandTool* tool : tools) {
        ASSERT_NE(tool, nullptr);
        const CommandToolDescriptor descriptor = tool->descriptor();
        EXPECT_FALSE(descriptor.id.empty());
        EXPECT_FALSE(descriptor.operationId.empty());
        EXPECT_FALSE(descriptor.label.empty());
        EXPECT_EQ(tool->id(), descriptor.id);
        EXPECT_EQ(tool->operationId(), descriptor.operationId);
        EXPECT_EQ(tool->label(), descriptor.label);
        EXPECT_EQ(tool->kind(), descriptor.kind);
        EXPECT_EQ(tool->previewKind(), descriptor.previewKind);
        EXPECT_EQ(tool->rebuildsMeshOnSuccess(), descriptor.rebuildsMeshOnSuccess);
        EXPECT_TRUE(ids.emplace(std::string(tool->id())).second) << tool->id();

        const ModelingCommandHistoryPolicy history = tool->historyPolicy();
        if (tool->kind() == CommandToolKind::Preview || !tool->rebuildsMeshOnSuccess()) {
            EXPECT_EQ(history.mode, ModelingCommandHistoryMode::None) << tool->id();
        } else {
            EXPECT_EQ(history.mode, ModelingCommandHistoryMode::ModelingUndoRedo) << tool->id();
        }
    }
}

TEST(ModelingToolCatalogTests, HostOperationActionIdsResolveToCommandTools) {
    /**
     * Represents an Expected Action value used by the automated test suite.
     */
    struct ExpectedAction {
        std::string_view actionId;
        std::string_view operationId;
        CommandToolKind kind = CommandToolKind::Instant;
        ModelingActionPreviewKind previewKind = ModelingActionPreviewKind::None;
    };

    constexpr std::array expectedActions{
            ExpectedAction{"invert_mesh", "flip_face_normals", CommandToolKind::ContextAction},
            ExpectedAction{"shade_smooth_mesh", "shade_faces_smooth", CommandToolKind::ContextAction},
            ExpectedAction{"shade_flat_mesh", "shade_faces_flat", CommandToolKind::ContextAction},
            ExpectedAction{"combine_mesh", "combine_meshes", CommandToolKind::ContextAction},
            ExpectedAction{"create_outer_hull", "thicken", CommandToolKind::Preview, ModelingActionPreviewKind::Thicken},
            ExpectedAction{"snap_vertices_to_active", "snap_vertices_to_active"},
            ExpectedAction{"merge_vertices", "merge_vertices_to_active", CommandToolKind::ContextAction},
            ExpectedAction{"merge_vertices_at_center", "merge_vertices_to_center", CommandToolKind::ContextAction},
            ExpectedAction{"merge_by_distance", "merge_by_distance", CommandToolKind::Preview, ModelingActionPreviewKind::MergeByDistance},
            ExpectedAction{"bevel_vertices", "bevel_vertices"},
            ExpectedAction{"connect_vertices", "connect_vertices"},
            ExpectedAction{"dissolve_vertices", "dissolve_vertices"},
            ExpectedAction{"radial_align_vertices", "radial_align", CommandToolKind::ContextAction},
            ExpectedAction{"snap_edges_to_active", "snap_edges_to_active"},
            ExpectedAction{"connect_edges", "connect_edges"},
            ExpectedAction{"split_edges", "split_edges"},
            ExpectedAction{"harden_edge_normals", "harden_edge_normals"},
            ExpectedAction{"soften_edge_normals", "soften_edge_normals"},
            ExpectedAction{"bevel_edges", "edge_bevel", CommandToolKind::Preview, ModelingActionPreviewKind::EdgeBevel},
            ExpectedAction{"bridge_edges", "bridge", CommandToolKind::ContextAction},
            ExpectedAction{"interpolated_bridge_edges", "bridge", CommandToolKind::Preview, ModelingActionPreviewKind::Bridge},
            ExpectedAction{"dissolve_edges", "dissolve_edges"},
            ExpectedAction{"merge_edges", "merge_edges"},
            ExpectedAction{"collapse_edges", "collapse_edges"},
            ExpectedAction{"fill_hole", "fill_hole"},
            ExpectedAction{"radial_align_edges", "radial_align", CommandToolKind::ContextAction},
            ExpectedAction{"bridge_faces", "bridge", CommandToolKind::ContextAction},
            ExpectedAction{"interpolated_bridge_faces", "bridge", CommandToolKind::Preview, ModelingActionPreviewKind::Bridge},
            ExpectedAction{"invert_faces", "flip_face_normals", CommandToolKind::ContextAction},
            ExpectedAction{"combine_faces", "combine_faces"},
            ExpectedAction{"collapse_faces", "collapse_faces"},
            ExpectedAction{"radial_align_faces", "radial_align", CommandToolKind::ContextAction},
            ExpectedAction{"slice_quad", "slice_quad", CommandToolKind::Preview, ModelingActionPreviewKind::SliceQuad},
            ExpectedAction{"thicken_faces", "thicken", CommandToolKind::Preview, ModelingActionPreviewKind::Thicken},
            ExpectedAction{"extract_faces", "extract_faces"},
            ExpectedAction{"detach_faces", "detach_faces"},
            ExpectedAction{"apply_active_material", "assign_material", CommandToolKind::ContextAction},
    };

    for (const ExpectedAction& expected : expectedActions) {
        const CommandTool* tool = findModelingCommandTool(expected.actionId);
        ASSERT_NE(tool, nullptr) << expected.actionId;
        EXPECT_EQ(tool->operationId(), expected.operationId) << expected.actionId;
        EXPECT_EQ(tool->kind(), expected.kind) << expected.actionId;
        EXPECT_EQ(tool->previewKind(), expected.previewKind) << expected.actionId;
    }
}

TEST(ModelingToolCatalogTests, PreviewCommandsUseHostActionIds) {
    const PreviewCommandTool* edgeBevel = findModelingPreviewCommandTool("bevel_edges");
    ASSERT_NE(edgeBevel, nullptr);
    EXPECT_EQ(edgeBevel->operationId(), std::string_view("edge_bevel"));
    EXPECT_EQ(edgeBevel->previewKind(), ModelingActionPreviewKind::EdgeBevel);

    const PreviewCommandTool* edgeBridge = findModelingPreviewCommandTool("interpolated_bridge_edges");
    ASSERT_NE(edgeBridge, nullptr);
    EXPECT_EQ(edgeBridge->operationId(), std::string_view("bridge"));
    EXPECT_EQ(edgeBridge->previewKind(), ModelingActionPreviewKind::Bridge);

    const PreviewCommandTool* faceBridge = findModelingPreviewCommandTool("interpolated_bridge_faces");
    ASSERT_NE(faceBridge, nullptr);
    EXPECT_EQ(faceBridge->operationId(), std::string_view("bridge"));
    EXPECT_EQ(faceBridge->previewKind(), ModelingActionPreviewKind::Bridge);

    const PreviewCommandTool* thicken = findModelingPreviewCommandTool("thicken_faces");
    ASSERT_NE(thicken, nullptr);
    EXPECT_EQ(thicken->operationId(), std::string_view("thicken"));
    EXPECT_EQ(thicken->previewKind(), ModelingActionPreviewKind::Thicken);

    const PreviewCommandTool* outerHull = findModelingPreviewCommandTool("create_outer_hull");
    ASSERT_NE(outerHull, nullptr);
    EXPECT_EQ(outerHull->operationId(), std::string_view("thicken"));
    EXPECT_EQ(outerHull->previewKind(), ModelingActionPreviewKind::Thicken);

    const PreviewCommandTool* sliceQuad = findModelingPreviewCommandTool("slice_quad");
    ASSERT_NE(sliceQuad, nullptr);
    EXPECT_EQ(sliceQuad->operationId(), std::string_view("slice_quad"));
    EXPECT_EQ(sliceQuad->previewKind(), ModelingActionPreviewKind::SliceQuad);

    const PreviewCommandTool* mergeByDistance = findModelingPreviewCommandTool("merge_by_distance");
    ASSERT_NE(mergeByDistance, nullptr);
    EXPECT_EQ(mergeByDistance->operationId(), std::string_view("merge_by_distance"));
    EXPECT_EQ(mergeByDistance->previewKind(), ModelingActionPreviewKind::MergeByDistance);
}

TEST(ModelingToolCatalogTests, DispatcherUsesCatalogAndHistoryPolicy) {
    ModelingCommandDispatcher dispatcher;

    EXPECT_EQ(dispatcher.find("duplicate_meshes"), findModelingCommandTool("duplicate_meshes"));
    EXPECT_EQ(dispatcher.find("missing_command"), nullptr);
    EXPECT_EQ(dispatcher.historyPolicy("duplicate_meshes").mode, ModelingCommandHistoryMode::ModelingUndoRedo);
    EXPECT_EQ(dispatcher.historyPolicy("select_all").mode, ModelingCommandHistoryMode::None);
    EXPECT_EQ(dispatcher.historyPolicy("bevel_edges").mode, ModelingCommandHistoryMode::None);
    EXPECT_EQ(dispatcher.historyPolicy("missing_command").mode, ModelingCommandHistoryMode::None);
}

} // namespace
} // namespace quader
