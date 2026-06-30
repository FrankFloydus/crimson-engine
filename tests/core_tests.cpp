////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/bake_service.hpp"
#include "assets/material_document.hpp"
#include "app/app_window_settings.hpp"
#include "core/localization_service.hpp"
#include "core/logger.hpp"
#include "core/public_command_targets.hpp"
#include "core/undo_redo_manager.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/scene_transform_preview_transaction.hpp"
#include "editor/transform/scene_transform_drag_service.hpp"
#include "editor/commands/document/apply_entity_transform_command.hpp"
#include "editor/commands/document/batch_document_command.hpp"
#include "editor/commands/document/create_entity_command.hpp"
#include "editor/commands/document/destroy_entity_command.hpp"
#include "editor/commands/document/duplicate_entity_command.hpp"
#include "editor/commands/document/group_selection_command.hpp"
#include "editor/commands/document/rename_entity_command.hpp"
#include "editor/commands/document/reparent_entity_command.hpp"
#include "editor/commands/document/select_entities_command.hpp"
#include "editor/commands/document/set_lock_command.hpp"
#include "editor/commands/document/set_visibility_command.hpp"
#include "editor/commands/document/ungroup_selection_command.hpp"
#include "scene/document/project_document.hpp"
#include "scene/document/project_session.hpp"
#include "editor/commands/editor_command_service.hpp"
#include "editor/history/editor_history_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/metadata/host_contract.hpp"
#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"
#include "editor/projection/selection_overlay_projection.hpp"
#include "editor/selection/editor_selection_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/tools/viewport_tool.hpp"
#include "editor/tools/viewport_tool_descriptor_catalog.hpp"
#include "render/filament_material_library.hpp"
#include "render/light_renderable_cache.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_cache.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/overlay/viewport_overlay_style_resolver.hpp"
#include "render/mesh_renderable_proxy.hpp"
#include "render/render_system.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/scene_render_sync_service.hpp"
#include "render/texture_binding_service.hpp"
#include "render/texture_registry.hpp"
#include "render/tool_preview_renderable_cache.hpp"
#include "render/viewport_grid_renderer.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/overlay_renderable_proxy.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/viewport_renderer.hpp"
#include "render/viewport_render_target.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_document.hpp"
#include "scene/scene_entity_naming.hpp"
#include "scene/transform_pivot.hpp"
#include "platform/platform_window.hpp"
#include "platform/sdl_event_utils.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "ui/console_model.hpp"
#include "ui/ui_settings.hpp"
#include "ui/viewport_context_menu_policy.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_grid_placement_policy.hpp"
#include "viewport/viewport_input_router.hpp"
#include "viewport/viewport_layout.hpp"
#include "viewport/viewport_settings.hpp"
#include "viewport/viewport_workspace_service.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <editor/interactions/viewport_navigation.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

using namespace quader;
using quader::test::guardrail::readSourceFile;

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

std::size_t countOccurrences(std::string_view haystack, std::string_view needle) {
    std::size_t count = 0;
    std::size_t offset = 0;
    while (!needle.empty()) {
        offset = haystack.find(needle, offset);
        if (offset == std::string_view::npos) {
            break;
        }
        ++count;
        offset += needle.size();
    }
    return count;
}

template <typename T>
constexpr bool renderOwnerIsNonTransferable() {
    static_assert(std::is_default_constructible_v<T>);
    static_assert(!std::is_copy_constructible_v<T>);
    static_assert(!std::is_copy_assignable_v<T>);
    static_assert(!std::is_move_constructible_v<T>);
    static_assert(!std::is_move_assignable_v<T>);
    return true;
}

bool metadataMethodReturnsLiteral(std::string_view source, std::string_view signature) {
    std::size_t offset = 0;
    while (true) {
        const std::size_t signaturePos = source.find(signature, offset);
        if (signaturePos == std::string_view::npos) {
            return false;
        }
        const std::size_t bodyStart = source.find('{', signaturePos);
        const std::size_t bodyEnd = source.find('}', bodyStart);
        if (bodyStart == std::string_view::npos || bodyEnd == std::string_view::npos) {
            return false;
        }
        const std::size_t literalReturn = source.find("return \"", bodyStart);
        if (literalReturn != std::string_view::npos && literalReturn < bodyEnd) {
            return true;
        }
        offset = bodyEnd + 1;
    }
}

ViewportShortcut testToolShortcut(ViewportToolId tool) {
    return {
            .kind = ViewportShortcutKind::ActivateTool,
            .context = ViewportShortcutContext::Viewport,
            .tool = tool,
    };
}

ViewportShortcut testSelectionShortcut(ViewportSelectionMode mode) {
    return {
            .kind = ViewportShortcutKind::SetSelectionMode,
            .context = ViewportShortcutContext::Viewport,
            .selectionMode = mode,
    };
}

ViewportShortcut testGridShortcut(int preset) {
    return {
            .kind = ViewportShortcutKind::SetGridPreset,
            .context = ViewportShortcutContext::Viewport,
            .gridPreset = preset,
    };
}

ViewportShortcut testViewShortcut(ViewportView view) {
    return {
            .kind = ViewportShortcutKind::SetView,
            .context = ViewportShortcutContext::Viewport,
            .view = view,
    };
}

ViewportShortcut testRouterShortcut(
        ViewportShortcutKind kind,
        ViewportShortcutContext context = ViewportShortcutContext::Viewport) {
    return {
            .kind = kind,
            .context = context,
    };
}

ViewportShortcut testMirrorShortcut(ViewportMirrorAxis axis) {
    return {
            .kind = ViewportShortcutKind::MirrorAxis,
            .context = ViewportShortcutContext::Viewport,
            .mirrorAxis = axis,
    };
}

void setResolvedShortcut(ViewportInputFrame& input, ViewportShortcut shortcut) {
    input.resolvedShortcuts = {shortcut};
}

void setNavigationPressed(ViewportInputFrame& input, ViewportInputActionKind action) {
    input.navigationBindings = {};
    switch (action) {
    case ViewportInputActionKind::Orbit:
        input.navigationBindings.orbitPressed = true;
        input.navigationBindings.orbitDown = true;
        break;
    case ViewportInputActionKind::OrbitSnap:
        input.navigationBindings.orbitSnapPressed = true;
        input.navigationBindings.orbitSnapDown = true;
        break;
    case ViewportInputActionKind::Pan:
        input.navigationBindings.panPressed = true;
        input.navigationBindings.panDown = true;
        break;
    case ViewportInputActionKind::Fly:
        input.navigationBindings.flyPressed = true;
        input.navigationBindings.flyDown = true;
        break;
    default:
        break;
    }
}

void setNavigationDown(ViewportInputFrame& input, ViewportInputActionKind action) {
    input.navigationBindings = {};
    switch (action) {
    case ViewportInputActionKind::Orbit:
        input.navigationBindings.orbitDown = true;
        break;
    case ViewportInputActionKind::OrbitSnap:
        input.navigationBindings.orbitSnapDown = true;
        break;
    case ViewportInputActionKind::Pan:
        input.navigationBindings.panDown = true;
        break;
    case ViewportInputActionKind::Fly:
        input.navigationBindings.flyDown = true;
        break;
    default:
        break;
    }
}

void setNavigationReleased(ViewportInputFrame& input, ViewportInputActionKind action) {
    input.navigationBindings = {};
    switch (action) {
    case ViewportInputActionKind::Orbit:
        input.navigationBindings.orbitReleased = true;
        break;
    case ViewportInputActionKind::OrbitSnap:
        input.navigationBindings.orbitSnapReleased = true;
        break;
    case ViewportInputActionKind::Pan:
        input.navigationBindings.panReleased = true;
        break;
    case ViewportInputActionKind::Fly:
        input.navigationBindings.flyReleased = true;
        break;
    default:
        break;
    }
}

/**
 * Carries EditorSelectionTestHarness state used by the test layer.
 */
struct EditorSelectionTestHarness {
    explicit EditorSelectionTestHarness(
            SceneDocument& scene,
            AssetRegistry& assets,
            ModelingSessionService& modeling)
        : documentCommands(scene, history)
        , selection(scene, documentCommands, modeling) {
        (void)assets;
    }

    EditorHistoryService history;
    DocumentCommandService documentCommands;
    EditorSelectionService selection;
};

DocumentCommandResult selectThroughEditor(
        EditorSelectionService& selection,
        std::initializer_list<EntityId> entities,
        EntityId active = InvalidEntityId) {
    std::vector<EntityId> selected(entities);
    const EntityId resolvedActive = active != InvalidEntityId || selected.empty() ? active : selected.back();
    return selection.setSceneSelection(std::span<const EntityId>(selected.data(), selected.size()), resolvedActive);
}

static_assert(std::is_base_of_v<IUndoableCommand, ModelingMementoCommand>);
static_assert(std::is_base_of_v<ModelingMementoCommand, DuplicateMeshesHistoryCommand>);

/**
 * Applies Test Value Command behavior as a command in the automated test suite.
 */
class TestValueCommand final : public IUndoableCommand {
public:
    TestValueCommand(
            std::string label,
            int& value,
            int before,
            int after,
            bool* failUndo = nullptr,
            bool* failRedo = nullptr,
            bool undoChanged = true,
            bool redoChanged = true,
            std::string mergeKey = {})
        : label_(std::move(label))
        , value_(value)
        , before_(before)
        , after_(after)
        , failUndo_(failUndo)
        , failRedo_(failRedo)
        , undoChanged_(undoChanged)
        , redoChanged_(redoChanged)
        , mergeKey_(std::move(mergeKey)) {}

    [[nodiscard]] const std::string& label() const override { return label_; }

    CommandResult undo() override {
        if (failUndo_ != nullptr && *failUndo_) {
            return commandInvalidResult("undo failed");
        }
        value_ = before_;
        return {
                .ok = true,
                .changed = undoChanged_,
        };
    }

    CommandResult redo() override {
        if (failRedo_ != nullptr && *failRedo_) {
            return commandInvalidResult("redo failed");
        }
        value_ = after_;
        return {
                .ok = true,
                .changed = redoChanged_,
        };
    }

    bool mergeWith(const IUndoableCommand& newerCommand, std::string_view mergeKey) override {
        const auto* newer = dynamic_cast<const TestValueCommand*>(&newerCommand);
        if (newer == nullptr ||
                mergeKey.empty() ||
                mergeKey_ != mergeKey ||
                newer->mergeKey_ != mergeKey ||
                &newer->value_ != &value_) {
            return false;
        }
        after_ = newer->after_;
        redoChanged_ = redoChanged_ || newer->redoChanged_;
        return true;
    }

private:
    std::string label_;
    int& value_;
    int before_ = 0;
    int after_ = 0;
    bool* failUndo_ = nullptr;
    bool* failRedo_ = nullptr;
    bool undoChanged_ = true;
    bool redoChanged_ = true;
    std::string mergeKey_;
};

void expectColorNear(ViewportSettingColor actual, ViewportSettingColor expected) {
    EXPECT_NEAR(actual.r, expected.r, 0.001f);
    EXPECT_NEAR(actual.g, expected.g, 0.001f);
    EXPECT_NEAR(actual.b, expected.b, 0.001f);
    EXPECT_NEAR(actual.a, expected.a, 0.001f);
}

ViewportSettingColor overlayPolicySettingColor(std::uint32_t abgr) {
    return {
            static_cast<float>(abgr & 0xffu) / 255.0f,
            static_cast<float>((abgr >> 8) & 0xffu) / 255.0f,
            static_cast<float>((abgr >> 16) & 0xffu) / 255.0f,
            static_cast<float>((abgr >> 24) & 0xffu) / 255.0f,
    };
}

void expectVec3Near(QVec3 actual, QVec3 expected) {
    EXPECT_NEAR(actual.x, expected.x, 0.001f);
    EXPECT_NEAR(actual.y, expected.y, 0.001f);
    EXPECT_NEAR(actual.z, expected.z, 0.001f);
}

double meshPositionFingerprint(const MeshAsset& mesh) {
    double value = 0.0;
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        const QVec3& position = mesh.vertices[index].position;
        const double weight = static_cast<double>(index + 1);
        value += weight * (
                static_cast<double>(position.x * position.x) * 0.31 +
                static_cast<double>(position.y * position.y) * 0.53 +
                static_cast<double>(position.z * position.z) * 0.71);
    }
    return value;
}

int previewElementId(quader_poly::ElementId id) {
    return static_cast<int>(id);
}

std::pair<int, int> previewEdgePair(quader_poly::Edge edge) {
    return {previewElementId(edge.a), previewElementId(edge.b)};
}

/**
 * Stores closed-edge fixture data for edge-bevel preview selection rebuild tests.
 */
struct PreviewClosedEdgeFixture {
    quader_poly::Document document;
    std::pair<int, int> firstEdge = {-1, -1};
    std::pair<int, int> secondEdge = {-1, -1};
};

PreviewClosedEdgeFixture makePreviewClosedEdgeFixture() {
    PreviewClosedEdgeFixture fixture;
    fixture.document = quader_poly::make_cube(quader_poly::CubePrimitiveSpec{});
    const std::vector<quader_poly::Edge> edges = quader_poly::document_edges(fixture.document);
    EXPECT_GE(edges.size(), 2u);
    if (edges.size() >= 2u) {
        fixture.firstEdge = previewEdgePair(edges[0]);
        fixture.secondEdge = previewEdgePair(edges[1]);
    }
    return fixture;
}

/**
 * Stores two disconnected quad faces for face-preview selection rebuild tests.
 */
struct PreviewParallelQuadFixture {
    quader_poly::Document document;
    int frontFace = -1;
    int backFace = -1;
};

void appendPreviewParallelQuadPair(
        quader_poly::Document& document,
        float xOffset,
        int& frontFace,
        int& backFace) {
    const quader_poly::ElementId a = quader_poly::add_vertex(document, {-1.0f + xOffset, -1.0f, -1.0f});
    const quader_poly::ElementId b = quader_poly::add_vertex(document, {1.0f + xOffset, -1.0f, -1.0f});
    const quader_poly::ElementId c = quader_poly::add_vertex(document, {1.0f + xOffset, 1.0f, -1.0f});
    const quader_poly::ElementId d = quader_poly::add_vertex(document, {-1.0f + xOffset, 1.0f, -1.0f});
    const quader_poly::ElementId e = quader_poly::add_vertex(document, {-1.0f + xOffset, -1.0f, 1.0f});
    const quader_poly::ElementId f = quader_poly::add_vertex(document, {1.0f + xOffset, -1.0f, 1.0f});
    const quader_poly::ElementId g = quader_poly::add_vertex(document, {1.0f + xOffset, 1.0f, 1.0f});
    const quader_poly::ElementId h = quader_poly::add_vertex(document, {-1.0f + xOffset, 1.0f, 1.0f});
    const quader_poly::ElementId firstFace = quader_poly::add_face(document, std::array{a, b, c, d});
    const quader_poly::ElementId secondFace = quader_poly::add_face(document, std::array{h, g, f, e});
    EXPECT_NE(firstFace, quader_poly::invalid_element_id);
    EXPECT_NE(secondFace, quader_poly::invalid_element_id);
    frontFace = previewElementId(firstFace);
    backFace = previewElementId(secondFace);
}

PreviewParallelQuadFixture makePreviewParallelQuadFixture(float xOffset = 0.0f) {
    PreviewParallelQuadFixture fixture;
    appendPreviewParallelQuadPair(fixture.document, xOffset, fixture.frontFace, fixture.backFace);
    return fixture;
}

/**
 * Stores two independent bridgeable face pairs for bridge preview selection rebuild tests.
 */
struct PreviewBridgeFixture {
    quader_poly::Document document;
    int firstFrontFace = -1;
    int firstBackFace = -1;
    int secondFrontFace = -1;
    int secondBackFace = -1;
};

PreviewBridgeFixture makePreviewBridgeFixture() {
    PreviewBridgeFixture fixture;
    appendPreviewParallelQuadPair(fixture.document, 0.0f, fixture.firstFrontFace, fixture.firstBackFace);
    appendPreviewParallelQuadPair(fixture.document, 5.0f, fixture.secondFrontFace, fixture.secondBackFace);
    return fixture;
}

/**
 * Stores independent near-vertex pairs for merge-by-distance preview selection rebuild tests.
 */
struct PreviewMergeDistanceFixture {
    quader_poly::Document document;
    int firstNearA = -1;
    int firstNearB = -1;
    int secondNearA = -1;
    int secondNearB = -1;
};

PreviewMergeDistanceFixture makePreviewMergeDistanceFixture() {
    PreviewMergeDistanceFixture fixture;
    auto addPair = [&](float xOffset, int& nearA, int& nearB) {
        const quader_poly::ElementId a = quader_poly::add_vertex(fixture.document, {xOffset, 0.0f, 0.0f});
        const quader_poly::ElementId b = quader_poly::add_vertex(fixture.document, {xOffset + 1.0f, 0.0f, 0.0f});
        const quader_poly::ElementId c = quader_poly::add_vertex(fixture.document, {xOffset, 1.0f, 0.0f});
        const quader_poly::ElementId d = quader_poly::add_vertex(fixture.document, {xOffset, 1.1f, 0.0f});
        const quader_poly::ElementId e = quader_poly::add_vertex(fixture.document, {xOffset - 1.0f, 1.0f, 0.0f});
        const quader_poly::ElementId f = quader_poly::add_vertex(fixture.document, {xOffset - 1.0f, 0.0f, 0.0f});
        const quader_poly::ElementId firstFace = quader_poly::add_face(fixture.document, std::array{a, b, c});
        const quader_poly::ElementId secondFace = quader_poly::add_face(fixture.document, std::array{d, e, f});
        EXPECT_NE(firstFace, quader_poly::invalid_element_id);
        EXPECT_NE(secondFace, quader_poly::invalid_element_id);
        nearA = previewElementId(c);
        nearB = previewElementId(d);
    };
    addPair(0.0f, fixture.firstNearA, fixture.firstNearB);
    addPair(5.0f, fixture.secondNearA, fixture.secondNearB);
    return fixture;
}

ModelingViewportCamera testOrthographicViewportCamera() {
    return {
            .orthographic = true,
            .origin = {0.0f, 0.0f, 10.0f},
            .xAxis = {1.0f, 0.0f, 0.0f},
            .yAxis = {0.0f, 1.0f, 0.0f},
            .zAxis = {0.0f, 0.0f, 1.0f},
            .viewportSize = {100.0f, 100.0f},
            .orthographicSize = 100.0,
            .fovDegrees = 60.0,
    };
}

ModelingRay testViewportForwardRay(float x = 0.0f) {
    return {
            .origin = {x, 0.0f, 10.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
}

ModelingToolRayEvent testInsertEdgeLoopEvent(float edgeY = 0.0f, float edgeX = 1.0f) {
    return {
            .screenPosition = {},
            .ray = {
                    .origin = {edgeX, edgeY, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
            .camera = testOrthographicViewportCamera(),
            .gridSize = 1.0,
            .vertexRadius = 0.3,
            .edgeRadius = 0.2,
            .snapEnabled = false,
    };
}

ModelingToolRayEvent testDrawnPlaneEvent(float x, bool dragging = false) {
    ModelingViewportCamera camera = testOrthographicViewportCamera();
    camera.orthographic = false;
    return {
            .screenPosition = {},
            .ray = {
                    .origin = {x, 2.0f, 0.0f},
                    .direction = {0.0f, -1.0f, 0.0f},
            },
            .camera = camera,
            .gridSize = 0.5,
            .snapEnabled = true,
            .dragging = dragging,
    };
}

ModelingTransformDragBeginRequest testXMoveDragBeginRequest(
        QVec3 pivot = {},
        ModelingTransformOperation operation = ModelingTransformOperation::Transform,
        bool duplicateSelected = false) {
    return {
            .tool = ModelingTransformTool::Move,
            .operation = operation,
            .axis = ModelingTransformAxis::X,
            .pivot = pivot,
            .screenPosition = {0.0f, 0.0f},
            .screenPivot = {0.0f, 0.0f},
            .screenTip = {10.0f, 0.0f},
            .hasWorldDirection = true,
            .worldDirection = {1.0f, 0.0f, 0.0f},
            .ray = testViewportForwardRay(),
            .camera = testOrthographicViewportCamera(),
            .gridSize = 1.0,
            .vertexSnapRadius = 0.3,
            .gridSnapEnabled = true,
            .duplicateSelected = duplicateSelected,
    };
}

ModelingTransformDragUpdateRequest testXMoveDragUpdateRequest(float screenX) {
    return {
            .screenPosition = {screenX, 0.0f},
            .ray = testViewportForwardRay(),
            .camera = testOrthographicViewportCamera(),
            .gridSize = 1.0,
            .vertexSnapRadius = 0.3,
            .gridSnapEnabled = true,
            .precision = false,
    };
}

QuaderRay testApiViewportForwardRay(float x = 0.0f) {
    return {
            .origin = {x, 0.0f, 10.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
}

QuaderViewportCamera testApiOrthographicViewportCamera() {
    return {
            .orthographic = true,
            .origin = {0.0f, 0.0f, 10.0f},
            .xAxis = {1.0f, 0.0f, 0.0f},
            .yAxis = {0.0f, 1.0f, 0.0f},
            .zAxis = {0.0f, 0.0f, 1.0f},
            .viewportSize = {100.0f, 100.0f},
            .orthographicSize = 100.0,
            .fovDegrees = 60.0,
    };
}

QuaderTransformDragBeginRequest testApiXMoveDragBeginRequest() {
    return {
            .tool = QuaderTransformTool::Move,
            .operation = QuaderTransformOperation::Transform,
            .axis = QuaderTransformAxis::X,
            .screenPosition = {0.0f, 0.0f},
            .screenPivot = {0.0f, 0.0f},
            .screenTip = {10.0f, 0.0f},
            .hasWorldDirection = true,
            .worldDirection = {1.0f, 0.0f, 0.0f},
            .ray = testApiViewportForwardRay(),
            .camera = testApiOrthographicViewportCamera(),
            .gridSize = 1.0,
            .vertexSnapRadius = 0.3,
            .gridSnapEnabled = true,
    };
}

QuaderTransformDragUpdateRequest testApiXMoveDragUpdateRequest(float screenX) {
    return {
            .screenPosition = {screenX, 0.0f},
            .ray = testApiViewportForwardRay(),
            .camera = testApiOrthographicViewportCamera(),
            .gridSize = 1.0,
            .vertexSnapRadius = 0.3,
            .gridSnapEnabled = true,
            .precision = false,
    };
}

MeshAsset makeTestTriangleMesh(std::string name = "Test Mesh") {
    MeshAsset mesh;
    mesh.name = std::move(name);
    mesh.vertices = {
            MeshVertex{.position = {-0.5f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}},
            MeshVertex{.position = {0.5f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {1.0f, 0.0f}},
            MeshVertex{.position = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.5f, 1.0f}},
    };
    mesh.indices = {0u, 1u, 2u};
    mesh.primitives = {MeshPrimitiveSpan{.indexOffset = 0u, .indexCount = 3u, .materialSlot = 0u}};
    return mesh;
}

TEST(LoggerTests, RoutesMessagesToMemorySink) {
    Logger::initialize(LoggerConfig{.name = "test", .filePath = "test.log", .maxEntries = 16});
    Logger::clearMemory();

    LOG_INFO("hello {}", "quader");

    const auto entries = Logger::entriesSnapshot();
    ASSERT_FALSE(entries.empty());
    EXPECT_NE(entries.back().message.find("hello quader"), std::string::npos);
}

TEST(FatalErrorHandlerTests, MainInstallsReportableFatalErrorBoundary) {
    const std::string mainSource = readSourceFile("src/main.cpp");
    const std::string handlerSource = readSourceFile("src/platform/fatal_error_handler.cpp");
    const std::string cmakeSource = readSourceFile("CMakeLists.txt");

    EXPECT_NE(mainSource.find("installFatalErrorHandler(logPath)"), std::string::npos);
    EXPECT_NE(mainSource.find("reportFatalErrorAndExit"), std::string::npos);
    EXPECT_NE(mainSource.find("FatalErrorCode::UnhandledCppException"), std::string::npos);
    EXPECT_NE(handlerSource.find("Quader Fatal Error"), std::string::npos);
    EXPECT_NE(handlerSource.find("Error code:"), std::string::npos);
    EXPECT_NE(handlerSource.find("Quick message:"), std::string::npos);
    EXPECT_NE(handlerSource.find("Crash report:"), std::string::npos);
    EXPECT_NE(handlerSource.find("crash-reports"), std::string::npos);
    EXPECT_NE(handlerSource.find("MiniDumpWriteDump"), std::string::npos);
    EXPECT_NE(handlerSource.find("writeCrashArtifacts"), std::string::npos);
    EXPECT_NE(handlerSource.find("appendTextFile(gLogPath"), std::string::npos);
    EXPECT_NE(handlerSource.find("moduleDescriptionForAddress"), std::string::npos);
    EXPECT_NE(handlerSource.find("Stack trace (most recent call first):"), std::string::npos);
    EXPECT_NE(handlerSource.find("CaptureStackBackTrace"), std::string::npos);
    EXPECT_NE(handlerSource.find("StackWalk64"), std::string::npos);
    EXPECT_NE(handlerSource.find("SymFromAddr"), std::string::npos);
    EXPECT_NE(handlerSource.find("QDR-FATAL-0000"), std::string::npos);
    EXPECT_NE(handlerSource.find("QDR-FATAL-0001"), std::string::npos);
    EXPECT_NE(handlerSource.find("QDR-FATAL-0002"), std::string::npos);
    EXPECT_NE(handlerSource.find("Internal runtime abort."), std::string::npos);
    EXPECT_NE(handlerSource.find("SIGABRT"), std::string::npos);
    EXPECT_NE(handlerSource.find("_set_abort_behavior"), std::string::npos);
    EXPECT_NE(cmakeSource.find("Dbghelp"), std::string::npos);
}

TEST(ConsoleModelTests, FiltersByLevelAndSearch) {
    std::vector<LogEntry> entries = {
        {.level = spdlog::level::info, .message = "engine ready"},
        {.level = spdlog::level::err, .message = "vulkan failed"},
    };

    const auto filtered = ConsoleModel::filter(entries, {.minimumLevel = spdlog::level::warn, .searchText = "vulkan"});

    ASSERT_EQ(filtered.size(), 1u);
    EXPECT_EQ(filtered[0].message, "vulkan failed");
}

TEST(ConsoleModelTests, RespectsMinimumSeverityLevels) {
    std::vector<LogEntry> entries = {
        {.level = spdlog::level::trace, .message = "trace"},
        {.level = spdlog::level::debug, .message = "debug"},
        {.level = spdlog::level::info, .message = "info"},
        {.level = spdlog::level::warn, .message = "warn"},
        {.level = spdlog::level::err, .message = "error"},
        {.level = spdlog::level::critical, .message = "critical"},
    };

    EXPECT_EQ(ConsoleModel::filter(entries, {.minimumLevel = spdlog::level::trace}).size(), 6u);
    EXPECT_EQ(ConsoleModel::filter(entries, {.minimumLevel = spdlog::level::info}).size(), 4u);
    EXPECT_EQ(ConsoleModel::filter(entries, {.minimumLevel = spdlog::level::warn}).size(), 3u);
    EXPECT_EQ(ConsoleModel::filter(entries, {.minimumLevel = spdlog::level::err}).size(), 2u);
    EXPECT_TRUE(ConsoleModel::filter(entries, {.minimumLevel = spdlog::level::off}).empty());
}

TEST(AppWindowSettingsTests, RoundTripsNormalBoundsAndMaximizedState) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_window_settings_round_trip.ini";
    std::filesystem::remove(path);

    AppWindowSettings settings = AppWindowSettings::defaults();
    settings.setPlacement({
            .normalFrame = {
                    .x = -120,
                    .y = 84,
                    .width = 1440,
                    .height = 900,
            },
            .maximized = true,
    });

    ASSERT_TRUE(settings.save(path));
    const AppWindowSettings loaded = AppWindowSettings::load(path);

    ASSERT_TRUE(loaded.placement().has_value());
    EXPECT_EQ(loaded.placement()->normalFrame.x, -120);
    EXPECT_EQ(loaded.placement()->normalFrame.y, 84);
    EXPECT_EQ(loaded.placement()->normalFrame.width, 1440);
    EXPECT_EQ(loaded.placement()->normalFrame.height, 900);
    EXPECT_TRUE(loaded.placement()->maximized);
}

TEST(AppWindowSettingsTests, IgnoresIncompleteWindowPlacementFiles) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_window_settings_incomplete.ini";
    std::filesystem::remove(path);

    {
        std::ofstream file(path, std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << "[Window]\n";
        file << "NormalX=10\n";
        file << "NormalY=20\n";
        file << "Maximized=true\n";
    }

    const AppWindowSettings loaded = AppWindowSettings::load(path);
    EXPECT_FALSE(loaded.placement().has_value());
}

TEST(UISettingsTests, DefaultsKeepPanelVisibilityLocaleAndNavigationGizmo) {
    const UISettings settings = UISettings::defaults();

    EXPECT_EQ(settings.locale(), "en-US");
    EXPECT_TRUE(settings.isPanelVisible(UIPanelId::Viewport));
    EXPECT_TRUE(settings.isPanelVisible(UIPanelId::Outliner));
    EXPECT_TRUE(settings.isPanelVisible(UIPanelId::Inspector));
    EXPECT_FALSE(settings.isPanelVisible(UIPanelId::Console));
    EXPECT_TRUE(settings.navigationGizmoVisible());
}

TEST(UISettingsTests, RoundTripsPanelVisibilityLocaleAndNavigationGizmo) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_round_trip.ini";
    std::filesystem::remove(path);

    UISettings settings = UISettings::defaults();
    settings.setLocale(" it-IT ");
    settings.setPanelVisible(UIPanelId::Outliner, false);
    settings.setPanelVisible(UIPanelId::Inspector, false);
    settings.setPanelVisible(UIPanelId::Console, true);
    settings.setNavigationGizmoVisible(false);

    ASSERT_TRUE(settings.save(path));
    std::ifstream saved(path);
    ASSERT_TRUE(saved.is_open());
    const std::string savedText{
            std::istreambuf_iterator<char>(saved),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(savedText.find("outliner=false"), std::string::npos);
    EXPECT_NE(savedText.find("inspector=false"), std::string::npos);
    EXPECT_NE(savedText.find("console=true"), std::string::npos);
    EXPECT_EQ(savedText.find("Outliner="), std::string::npos);
    EXPECT_EQ(savedText.find("Inspector="), std::string::npos);
    EXPECT_EQ(savedText.find("Console="), std::string::npos);
    saved.close();

    const UISettings loaded = UISettings::load(path);

    EXPECT_EQ(loaded.locale(), "it-IT");
    EXPECT_FALSE(loaded.isPanelVisible(UIPanelId::Outliner));
    EXPECT_FALSE(loaded.isPanelVisible(UIPanelId::Inspector));
    EXPECT_TRUE(loaded.isPanelVisible(UIPanelId::Console));
    EXPECT_FALSE(loaded.navigationGizmoVisible());

    std::filesystem::remove(path);
}

TEST(UISettingsTests, LoadsLegacyDisplayPanelKeysAndSavesStableKeys) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_legacy_panel_keys.ini";
    std::filesystem::remove(path);

    {
        std::ofstream file(path, std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << "[Panels]\n";
        file << "Outliner=false\n";
        file << "Inspector=false\n";
        file << "Console=true\n";
    }

    UISettings loaded = UISettings::load(path);
    EXPECT_FALSE(loaded.isPanelVisible(UIPanelId::Outliner));
    EXPECT_FALSE(loaded.isPanelVisible(UIPanelId::Inspector));
    EXPECT_TRUE(loaded.isPanelVisible(UIPanelId::Console));

    ASSERT_TRUE(loaded.save(path));
    std::ifstream saved(path);
    ASSERT_TRUE(saved.is_open());
    const std::string savedText{
            std::istreambuf_iterator<char>(saved),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(savedText.find("outliner=false"), std::string::npos);
    EXPECT_NE(savedText.find("inspector=false"), std::string::npos);
    EXPECT_NE(savedText.find("console=true"), std::string::npos);
    EXPECT_EQ(savedText.find("Outliner="), std::string::npos);
    EXPECT_EQ(savedText.find("Inspector="), std::string::npos);
    EXPECT_EQ(savedText.find("Console="), std::string::npos);
    saved.close();

    std::filesystem::remove(path);
}

TEST(UISettingsTests, IgnoresMalformedPanelVisibilityAndEmptyLocale) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_malformed.ini";
    std::filesystem::remove(path);

    {
        std::ofstream file(path, std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << "[Panels]\n";
        file << "Outliner=maybe\n";
        file << "Inspector=0\n";
        file << "Console=yes\n";
        file << "[Show]\n";
        file << "NavigationGizmo=no\n";
        file << "[Localization]\n";
        file << "Locale=   \n";
    }

    const UISettings loaded = UISettings::load(path);

    EXPECT_EQ(loaded.locale(), "en-US");
    EXPECT_TRUE(loaded.isPanelVisible(UIPanelId::Outliner));
    EXPECT_FALSE(loaded.isPanelVisible(UIPanelId::Inspector));
    EXPECT_TRUE(loaded.isPanelVisible(UIPanelId::Console));
    EXPECT_FALSE(loaded.navigationGizmoVisible());

    std::filesystem::remove(path);
}

TEST(PlatformWindowChromeTests, HitTestSupportsResizeEdgesToolbarDragAndWindowControls) {
    PlatformWindowChromeHitTest hitTest{
            .windowWidth = 800,
            .windowHeight = 600,
            .resizeBorder = 6,
            .titleBarHeight = 35,
            .leftInteractiveWidth = 440,
            .rightControlsWidth = 105,
    };

    auto hit = [&](int x, int y) {
        hitTest.pointX = x;
        hitTest.pointY = y;
        return platformWindowChromeHitTestResult(hitTest);
    };

    EXPECT_EQ(hit(0, 0), SDL_HITTEST_RESIZE_TOPLEFT);
    EXPECT_EQ(hit(799, 0), SDL_HITTEST_RESIZE_TOPRIGHT);
    EXPECT_EQ(hit(799, 599), SDL_HITTEST_RESIZE_BOTTOMRIGHT);
    EXPECT_EQ(hit(0, 599), SDL_HITTEST_RESIZE_BOTTOMLEFT);
    EXPECT_EQ(hit(400, 0), SDL_HITTEST_RESIZE_TOP);
    EXPECT_EQ(hit(799, 300), SDL_HITTEST_RESIZE_RIGHT);
    EXPECT_EQ(hit(400, 599), SDL_HITTEST_RESIZE_BOTTOM);
    EXPECT_EQ(hit(0, 300), SDL_HITTEST_RESIZE_LEFT);
    EXPECT_EQ(hit(80, 24), SDL_HITTEST_NORMAL);
    EXPECT_EQ(hit(400, 24), SDL_HITTEST_NORMAL);
    EXPECT_EQ(hit(500, 24), SDL_HITTEST_DRAGGABLE);
    EXPECT_EQ(hit(700, 24), SDL_HITTEST_NORMAL);
    EXPECT_EQ(hit(400, 60), SDL_HITTEST_NORMAL);

    hitTest.maximized = true;
    EXPECT_EQ(hit(80, 24), SDL_HITTEST_NORMAL);
    EXPECT_EQ(hit(400, 0), SDL_HITTEST_NORMAL);
    EXPECT_EQ(hit(500, 0), SDL_HITTEST_DRAGGABLE);
    EXPECT_EQ(hit(799, 599), SDL_HITTEST_NORMAL);
}

TEST(PlatformWindowChromeTests, ResizeBorderUsesSystemMetricsWhenLargerThanFallback) {
    EXPECT_EQ(platformWindowChromeResizeBorderForSystemMetrics(6, 4, 4), 8);
    EXPECT_EQ(platformWindowChromeResizeBorderForSystemMetrics(6, 1, 1), 6);
    EXPECT_EQ(platformWindowChromeResizeBorderForSystemMetrics(0, 0, 0), 1);
}

TEST(PlatformWindowChromeTests, RecoversPersistedWindowFrameToVisibleWorkArea) {
    const std::vector<PlatformWindowChromeFrame> workAreas = {
            {.x = 0, .y = 0, .width = 1920, .height = 1040},
            {.x = -1280, .y = 0, .width = 1280, .height = 984},
    };

    const PlatformWindowChromeFrame offscreen = platformWindowRecoverFrameToDisplayWorkArea(
            {.x = 4000, .y = 2000, .width = 1600, .height = 900},
            workAreas);
    EXPECT_EQ(offscreen.x, 320);
    EXPECT_EQ(offscreen.y, 140);
    EXPECT_EQ(offscreen.width, 1600);
    EXPECT_EQ(offscreen.height, 900);

    const PlatformWindowChromeFrame secondary = platformWindowRecoverFrameToDisplayWorkArea(
            {.x = -1200, .y = 100, .width = 900, .height = 700},
            workAreas);
    EXPECT_EQ(secondary.x, -1200);
    EXPECT_EQ(secondary.y, 100);
    EXPECT_EQ(secondary.width, 900);
    EXPECT_EQ(secondary.height, 700);

    const PlatformWindowChromeFrame oversized = platformWindowRecoverFrameToDisplayWorkArea(
            {.x = 0, .y = 0, .width = 5000, .height = 3000},
            workAreas);
    EXPECT_EQ(oversized.x, 0);
    EXPECT_EQ(oversized.y, 0);
    EXPECT_EQ(oversized.width, 1920);
    EXPECT_EQ(oversized.height, 1040);
}

TEST(PlatformWindowChromeTests, ManualResizeFrameSupportsEdgesAndCorners) {
    PlatformWindowChromeFrame frame{
            .x = 100,
            .y = 80,
            .width = 800,
            .height = 600,
    };

    auto resize = [&](SDL_HitTestResult edge, int dx, int dy) {
        return platformWindowChromeResizeFrame({
                .start = frame,
                .deltaX = dx,
                .deltaY = dy,
                .minWidth = 320,
                .minHeight = 240,
                .edge = edge,
        });
    };

    EXPECT_EQ(resize(SDL_HITTEST_RESIZE_RIGHT, 40, 0).width, 840);
    EXPECT_EQ(resize(SDL_HITTEST_RESIZE_BOTTOM, 0, 30).height, 630);

    const PlatformWindowChromeFrame left = resize(SDL_HITTEST_RESIZE_LEFT, 50, 0);
    EXPECT_EQ(left.x, 150);
    EXPECT_EQ(left.width, 750);

    const PlatformWindowChromeFrame topLeft = resize(SDL_HITTEST_RESIZE_TOPLEFT, -20, -10);
    EXPECT_EQ(topLeft.x, 80);
    EXPECT_EQ(topLeft.y, 70);
    EXPECT_EQ(topLeft.width, 820);
    EXPECT_EQ(topLeft.height, 610);

    const PlatformWindowChromeFrame clamped = resize(SDL_HITTEST_RESIZE_TOPLEFT, 700, 500);
    EXPECT_EQ(clamped.x, 580);
    EXPECT_EQ(clamped.y, 440);
    EXPECT_EQ(clamped.width, 320);
    EXPECT_EQ(clamped.height, 240);
}

TEST(PlatformWindowChromeTests, ManualResizeFrameDefaultsToApplicationMinimumSize) {
    EXPECT_EQ(kPlatformWindowMinimumWidth, 800);
    EXPECT_EQ(kPlatformWindowMinimumHeight, 600);

    const PlatformWindowChromeFrame frame{
            .x = 100,
            .y = 80,
            .width = 1200,
            .height = 900,
    };

    const PlatformWindowChromeFrame clamped = platformWindowChromeResizeFrame({
            .start = frame,
            .deltaX = 1000,
            .deltaY = 800,
            .edge = SDL_HITTEST_RESIZE_TOPLEFT,
    });

    EXPECT_EQ(clamped.x, 500);
    EXPECT_EQ(clamped.y, 380);
    EXPECT_EQ(clamped.width, 800);
    EXPECT_EQ(clamped.height, 600);
}

TEST(PlatformWindowChromeTests, RenderFrameAvailabilityRejectsNonDrawableWindowStates) {
    EXPECT_TRUE(platformWindowCanRenderFrames(0u, 1600, 900));
    EXPECT_TRUE(platformWindowCanRenderFrames(SDL_WINDOW_INPUT_FOCUS, 1600, 900));
    EXPECT_TRUE(platformWindowCanRenderFrames(SDL_WINDOW_MAXIMIZED, 1600, 900));

    EXPECT_FALSE(platformWindowCanRenderFrames(SDL_WINDOW_MINIMIZED, 1600, 900));
    EXPECT_FALSE(platformWindowCanRenderFrames(SDL_WINDOW_HIDDEN, 1600, 900));
    EXPECT_FALSE(platformWindowCanRenderFrames(SDL_WINDOW_OCCLUDED, 1600, 900));
    EXPECT_FALSE(platformWindowCanRenderFrames(SDL_WINDOW_MINIMIZED | SDL_WINDOW_INPUT_FOCUS, 1600, 900));
    EXPECT_FALSE(platformWindowCanRenderFrames(0u, 0, 900));
    EXPECT_FALSE(platformWindowCanRenderFrames(0u, 1600, 0));
}

TEST(SdlEventUtilsTests, ExtractsWindowIdsForRuntimeRoutedEvents) {
    constexpr SDL_WindowID windowId = 42u;
    SDL_Event event{};

    event.type = SDL_EVENT_WINDOW_RESIZED;
    event.window.windowID = windowId;
    EXPECT_EQ(sdlEventWindowId(event), windowId);

    event = {};
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.windowID = windowId;
    EXPECT_EQ(sdlEventWindowId(event), windowId);

    event = {};
    event.type = SDL_EVENT_MOUSE_BUTTON_DOWN;
    event.button.windowID = windowId;
    EXPECT_EQ(sdlEventWindowId(event), windowId);

    event = {};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = windowId;
    EXPECT_EQ(sdlEventWindowId(event), windowId);

    event = {};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.windowID = windowId;
    EXPECT_EQ(sdlEventWindowId(event), windowId);

    event = {};
    event.type = SDL_EVENT_TEXT_INPUT;
    event.text.windowID = windowId;
    EXPECT_EQ(sdlEventWindowId(event), windowId);

    event = {};
    event.type = SDL_EVENT_WINDOW_OCCLUDED;
    event.window.windowID = windowId;
    EXPECT_EQ(sdlEventWindowId(event), windowId);

    event = {};
    event.type = SDL_EVENT_QUIT;
    EXPECT_FALSE(sdlEventWindowId(event).has_value());
}

TEST(ViewportSettingsTests, DefaultsMatchNativeViewportSettingsState) {
    const ViewportSettings settings = ViewportSettings::defaults();

    ASSERT_EQ(viewportSettingsSections().size(), 6u);
    EXPECT_EQ(viewportSettingsSections()[0].label, "Grid");
    EXPECT_FALSE(viewportSettingsSections()[0].defaultExpanded);
    EXPECT_EQ(viewportSettingsSections()[1].label, "Selection");
    EXPECT_EQ(viewportSettingsSections()[2].label, "Overlay");
    EXPECT_EQ(viewportSettingsSections()[3].label, "Vertices");
    EXPECT_EQ(viewportSettingsSections()[4].label, "Picking");
    EXPECT_EQ(viewportSettingsSections()[5].label, "Gizmo");

    expectColorNear(settings.color(ViewportColorSettingId::SelectionWire), overlayPolicySettingColor(kOverlaySelectionWireColor));
    expectColorNear(settings.color(ViewportColorSettingId::SourceWire), overlayPolicySettingColor(kOverlaySourceWireColor));
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::GridMinorLineSize), 0.32f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::SelectionEdgeLineSize), 2.5f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::PickVertexRadius), 0.3f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::GizmoSize), 1.0f);
    ASSERT_EQ(viewportBoolSettings().size(), 6u);
    EXPECT_TRUE(settings.boolValue(ViewportBoolSettingId::ShowGrid));
    EXPECT_TRUE(settings.boolValue(ViewportBoolSettingId::ShowGizmos));
    EXPECT_TRUE(settings.boolValue(ViewportBoolSettingId::ShowOverlays));
    EXPECT_FALSE(settings.boolValue(ViewportBoolSettingId::ShowInfo));
    EXPECT_TRUE(settings.boolValue(ViewportBoolSettingId::GridSnap));
    EXPECT_TRUE(settings.boolValue(ViewportBoolSettingId::VertexSnap));
}

TEST(ViewportSettingsTests, SnapSettingsOwnTemporaryGridToggleAndGlobalVertexSnap) {
    ViewportSettings settings = ViewportSettings::defaults();

    EXPECT_TRUE(viewportGridSnapActive(settings, false));
    EXPECT_FALSE(viewportGridSnapActive(settings, true));
    EXPECT_FLOAT_EQ(viewportVertexSnapRadius(settings), 0.3f);

    EXPECT_TRUE(settings.setBoolValue(ViewportBoolSettingId::GridSnap, false));
    EXPECT_FALSE(viewportGridSnapActive(settings, false));
    EXPECT_TRUE(viewportGridSnapActive(settings, true));

    EXPECT_TRUE(settings.setBoolValue(ViewportBoolSettingId::VertexSnap, false));
    EXPECT_FLOAT_EQ(viewportVertexSnapRadius(settings), 0.0f);
}

TEST(ViewportSettingsTests, OverlayDefaultsComeFromRenderOverlayPolicy) {
    const ViewportSettings settings = ViewportSettings::defaults();
    const auto& colorSettings = viewportColorSettings();
    const auto& floatSettings = viewportFloatSettings();

    const auto colorDefault = [&](ViewportColorSettingId id) {
        const std::size_t idx = static_cast<std::size_t>(id);
        EXPECT_EQ(colorSettings[idx].id, id);
        return colorSettings[idx].defaultValue;
    };
    const auto floatDefault = [&](ViewportFloatSettingId id) {
        const std::size_t idx = static_cast<std::size_t>(id);
        EXPECT_EQ(floatSettings[idx].id, id);
        return floatSettings[idx].defaultValue;
    };
    const auto expectOverlayColorDefault = [&](ViewportColorSettingId id, std::uint32_t policyColor) {
        const ViewportSettingColor expected = overlayPolicySettingColor(policyColor);
        expectColorNear(colorDefault(id), expected);
        expectColorNear(settings.color(id), expected);
    };
    const auto expectOverlayFloatDefault = [&](ViewportFloatSettingId id, float policyValue) {
        EXPECT_FLOAT_EQ(floatDefault(id), policyValue);
        EXPECT_FLOAT_EQ(settings.floatValue(id), policyValue);
    };

    expectOverlayColorDefault(ViewportColorSettingId::SelectionFace, kOverlaySelectionFaceColor);
    expectOverlayColorDefault(ViewportColorSettingId::SelectionWire, kOverlaySelectionWireColor);
    expectOverlayColorDefault(ViewportColorSettingId::SourceWire, kOverlaySourceWireColor);
    expectOverlayColorDefault(ViewportColorSettingId::OpenEdge, kOverlayOpenEdgeColor);
    expectOverlayColorDefault(ViewportColorSettingId::DiagnosticEdge, kOverlayDiagnosticEdgeColor);
    expectOverlayColorDefault(ViewportColorSettingId::HoverFace, kOverlayHoverFaceColor);
    expectOverlayColorDefault(ViewportColorSettingId::HoverWire, kOverlayHoverWireColor);
    expectOverlayColorDefault(ViewportColorSettingId::RemoveSelectionFace, kOverlayRemoveFaceColor);
    expectOverlayColorDefault(ViewportColorSettingId::RemoveSelectionWire, kOverlayRemoveWireColor);
    expectOverlayColorDefault(ViewportColorSettingId::Vertex, kOverlayVertexColor);
    expectOverlayColorDefault(ViewportColorSettingId::SelectedVertex, kOverlaySelectedVertexColor);
    expectOverlayColorDefault(ViewportColorSettingId::HoverVertex, kOverlayHoverVertexColor);
    expectOverlayColorDefault(ViewportColorSettingId::RemoveSelectionVertex, kOverlayRemoveWireColor);

    expectOverlayFloatDefault(ViewportFloatSettingId::SourceWireLineSize, kOverlaySourceWireLineWidthPixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::SelectionFaceWireLineSize, kOverlaySelectionWireLineWidthPixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::SelectionEdgeLineSize, kOverlaySelectionWireLineWidthPixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::HoverWireLineSize, kOverlayHoverWireLineWidthPixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::OpenEdgeLineSize, kOverlayOpenEdgeLineWidthPixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::DiagnosticEdgeLineSize, kOverlayDiagnosticEdgeLineWidthPixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::VertexSize, kOverlayVertexSizePixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::SelectedVertexGrowth, kOverlaySelectedVertexGrowthPixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::HoverVertexGrowth, kOverlayHoverVertexGrowthPixels);
    expectOverlayFloatDefault(ViewportFloatSettingId::VertexOutlineSize, kOverlayVertexOutlineSizePixels);
}

TEST(ViewportSettingsTests, SavesLoadsAndClampsViewportPreferenceKeys) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_viewport_settings_round_trip.ini";
    std::filesystem::remove(path);

    ViewportSettings settings = ViewportSettings::defaults();
    EXPECT_TRUE(settings.setColor(ViewportColorSettingId::GridMinor, {0.25f, 0.5f, 0.75f, 0.6f}));
    EXPECT_TRUE(settings.setFloatValue(ViewportFloatSettingId::GizmoSize, 10.0f));
    EXPECT_TRUE(settings.setBoolValue(ViewportBoolSettingId::ShowGrid, false));
    EXPECT_TRUE(settings.setBoolValue(ViewportBoolSettingId::ShowInfo, true));
    EXPECT_TRUE(settings.setBoolValue(ViewportBoolSettingId::GridSnap, false));
    EXPECT_TRUE(settings.setBoolValue(ViewportBoolSettingId::VertexSnap, false));
    ASSERT_TRUE(settings.save(path));

    const ViewportSettings loaded = ViewportSettings::load(path);
    expectColorNear(loaded.color(ViewportColorSettingId::GridMinor), {0.25098f, 0.501961f, 0.74902f, 0.6f});
    EXPECT_FLOAT_EQ(loaded.floatValue(ViewportFloatSettingId::GizmoSize), 2.0f);
    EXPECT_FALSE(loaded.boolValue(ViewportBoolSettingId::ShowGrid));
    EXPECT_TRUE(loaded.boolValue(ViewportBoolSettingId::ShowInfo));
    EXPECT_FALSE(loaded.boolValue(ViewportBoolSettingId::GridSnap));
    EXPECT_FALSE(loaded.boolValue(ViewportBoolSettingId::VertexSnap));

    {
        std::ifstream sourceFile(path);
        ASSERT_TRUE(sourceFile.is_open()) << path.string();
        const std::string source{
                std::istreambuf_iterator<char>(sourceFile),
                std::istreambuf_iterator<char>()};
        EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.GridMinorColor"), std::string::npos);
        EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.GizmoSize"), std::string::npos);
        EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.ShowGrid"), std::string::npos);
        EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.ShowInfo"), std::string::npos);
        EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.GridSnap"), std::string::npos);
        EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.VertexSnap"), std::string::npos);
        EXPECT_EQ(source.find("QVertexWorks.Quader.Viewport.BackgroundColor"), std::string::npos);
        EXPECT_EQ(source.find("QVertexWorks.Quader.Viewport.ShowSpecialTextures"), std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST(GroundGridMaterialTests, UsesUserWorldAnchoring) {
    const std::string source = readSourceFile("resources/materials/ground_grid.mat");
    const std::string gridRendererSource = readSourceFile("src/render/viewport_grid_renderer.cpp");

    EXPECT_NE(source.find("shadingModel : unlit"), std::string::npos);
    EXPECT_NE(source.find("depthWrite : false"), std::string::npos);
    EXPECT_NE(source.find("getUserWorldPosition()"), std::string::npos);
    EXPECT_EQ(source.find("getWorldPosition()"), std::string::npos);
    EXPECT_NE(source.find("localPosition"), std::string::npos);
    EXPECT_NE(source.find("variable_localPosition"), std::string::npos);
    EXPECT_NE(source.find("cameraSize"), std::string::npos);
    EXPECT_NE(source.find("visibleGridSize"), std::string::npos);
    EXPECT_NE(source.find("minGridWorldSpacing"), std::string::npos);
    EXPECT_NE(source.find("vec4(color * alpha, alpha)"), std::string::npos);
    EXPECT_EQ(source.find("getUV0()"), std::string::npos);
    EXPECT_EQ(source.find("planeCenter"), std::string::npos);
    EXPECT_NE(gridRendererSource.find(".castShadows(false)"), std::string::npos);
    EXPECT_NE(gridRendererSource.find(".receiveShadows(false)"), std::string::npos);
}

TEST(QuaderMeshMaterialTests, UsesOverlaySelectionInsteadOfStandaloneTint) {
    const std::string source = readSourceFile("resources/materials/quader_mesh.mat");

    EXPECT_EQ(source.find("selectionTint"), std::string::npos);
    EXPECT_EQ(source.find("getColor()"), std::string::npos);
    EXPECT_EQ(source.find("vertexColor"), std::string::npos);
    EXPECT_EQ(source.find("color,"), std::string::npos);
}

TEST(QuaderMeshMaterialTests, UsesLitTextureTransformTimesColorAndPbrFactors) {
    const std::string shadedSource = readSourceFile("resources/materials/quader_mesh.mat");
    const std::string source = readSourceFile("resources/materials/quader_mesh_lit.mat");

    EXPECT_NE(shadedSource.find("shadingModel : unlit"), std::string::npos);
    EXPECT_NE(shadedSource.find("vec2 uv = vec2(getUV0().x, 1.0 - getUV0().y)"), std::string::npos);
    EXPECT_NE(shadedSource.find("texture(materialParams_baseColorTexture, uv)"), std::string::npos);
    EXPECT_NE(shadedSource.find("material.baseColor = vec4(baseColor, baseSample.a * materialParams.baseColorFactor.a)"), std::string::npos);
    EXPECT_EQ(shadedSource.find("roughnessTexture"), std::string::npos);
    EXPECT_EQ(shadedSource.find("editorUnlit"), std::string::npos);
    EXPECT_NE(source.find("shadingModel : lit"), std::string::npos);
    EXPECT_EQ(source.find("doubleSided : true"), std::string::npos);
    EXPECT_NE(source.find("vec2 uv = vec2(getUV0().x, 1.0 - getUV0().y)"), std::string::npos);
    EXPECT_NE(source.find("texture(materialParams_baseColorTexture, uv)"), std::string::npos);
    EXPECT_NE(source.find("texture(materialParams_roughnessTexture, uv).r"), std::string::npos);
    EXPECT_NE(source.find("baseSample.rgb * materialParams.baseColorFactor.rgb"), std::string::npos);
    EXPECT_NE(source.find("material.baseColor = vec4(baseColor, baseSample.a * materialParams.baseColorFactor.a)"), std::string::npos);
    EXPECT_NE(source.find("material.roughness = clamp(roughnessSample * materialParams.roughnessFactor"), std::string::npos);
    EXPECT_NE(source.find("material.metallic = clamp(materialParams.metallicFactor"), std::string::npos);
    EXPECT_EQ(source.find("editorUnlit"), std::string::npos);
}

TEST(OverlayMaterialTests, PremultipliesVertexAlphaForTransparentFaceFill) {
    const std::string source = readSourceFile("resources/materials/vertex_color_overlay.mat");

    EXPECT_NE(source.find("blending : transparent"), std::string::npos);
    EXPECT_NE(source.find("depthWrite : false"), std::string::npos);
    EXPECT_NE(source.find("vec4(vertexColor.rgb * vertexColor.a, vertexColor.a)"), std::string::npos);
}

TEST(OverlayMaterialTests, LineAndPointMaterialsUseScreenSpaceOverlayExpansion) {
    const std::string lineSource = readSourceFile("resources/materials/quader_overlay_line.mat");

    EXPECT_NE(lineSource.find("vertexDomain : device"), std::string::npos);
    EXPECT_NE(lineSource.find("requires : [\n        color,\n        custom0"), std::string::npos);
    EXPECT_NE(lineSource.find("material.lineDistancePixels = getCustom0()"), std::string::npos);
    EXPECT_EQ(lineSource.find("material.worldPosition"), std::string::npos);
    EXPECT_EQ(lineSource.find("getClipFromViewMatrix()"), std::string::npos);
    EXPECT_EQ(lineSource.find("name : nearPlane"), std::string::npos);
    EXPECT_NE(lineSource.find("variable_lineDistancePixels.x"), std::string::npos);

    const std::string pointSource = readSourceFile("resources/materials/quader_overlay_point.mat");

    EXPECT_NE(pointSource.find("vertexDomain : device"), std::string::npos);
    EXPECT_NE(pointSource.find("requires : [\n        uv0,\n        color"), std::string::npos);
    EXPECT_EQ(pointSource.find("material.worldPosition"), std::string::npos);
    EXPECT_EQ(pointSource.find("getClipFromViewMatrix()"), std::string::npos);
    EXPECT_EQ(pointSource.find("name : nearPlane"), std::string::npos);
    EXPECT_NE(pointSource.find("variable_pointUv.xy"), std::string::npos);
    EXPECT_NE(pointSource.find("materialParams.innerRadius"), std::string::npos);
    const std::size_t innerRadiusGuard = pointSource.find("if (materialParams.innerRadius > 0.0)");
    const std::size_t outerCircleDiscard = pointSource.find("if (distanceFromCenter > 0.5)");
    ASSERT_NE(outerCircleDiscard, std::string::npos);
    ASSERT_NE(innerRadiusGuard, std::string::npos);
    EXPECT_LT(innerRadiusGuard, outerCircleDiscard);
}

TEST(OverlayMaterialTests, ImGuiMaterialCanCompositePremultipliedDisplayTextures) {
    const std::string source = readSourceFile("resources/materials/imgui.mat");
    const std::string renderer = readSourceFile("src/render/imgui_filament_renderer.cpp");

    EXPECT_NE(source.find("name : premultipliedAlpha"), std::string::npos);
    EXPECT_NE(source.find("vec3 straightRgb = sampled.rgb * tint.rgb * alpha"), std::string::npos);
    EXPECT_NE(source.find("vec3 premultipliedRgb = sampled.rgb * tint.rgb * tint.a"), std::string::npos);
    EXPECT_NE(source.find("mix(straightRgb, premultipliedRgb, premultipliedAlpha)"), std::string::npos);
    EXPECT_NE(renderer.find("TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha"), std::string::npos);
    EXPECT_NE(renderer.find("materialInstance->setParameter(\"premultipliedAlpha\", premultipliedAlpha)"), std::string::npos);
}

TEST(ViewportOverlayPolicyTests, MatchesNativeLayerDrawOrder) {
    const std::array<ViewportOverlayLayer, kViewportOverlayLayerCount> expected{{
            ViewportOverlayLayer::SelectedVisibleFace,
            ViewportOverlayLayer::SelectedFaceMask,
            ViewportOverlayLayer::WireframeBase,
            ViewportOverlayLayer::WireframeXray,
            ViewportOverlayLayer::SelectedXrayFace,
            ViewportOverlayLayer::HoverFace,
            ViewportOverlayLayer::SourceWireDepthStamp,
            ViewportOverlayLayer::SourceWire,
            ViewportOverlayLayer::OpenEdge,
            ViewportOverlayLayer::DiagnosticEdge,
            ViewportOverlayLayer::SelectedFaceEdge,
            ViewportOverlayLayer::SelectedEdge,
            ViewportOverlayLayer::GroupBounds,
            ViewportOverlayLayer::HoverFaceEdge,
            ViewportOverlayLayer::HoverEdge,
            ViewportOverlayLayer::UnselectedVertex,
            ViewportOverlayLayer::SelectedVertex,
            ViewportOverlayLayer::HoverVertex,
            ViewportOverlayLayer::SnapTargetVertex,
            ViewportOverlayLayer::ToolPlane,
            ViewportOverlayLayer::ToolPreviewFill,
            ViewportOverlayLayer::ToolPlaneLine,
            ViewportOverlayLayer::ToolPreviewLine,
            ViewportOverlayLayer::ToolPreviewSeam,
            ViewportOverlayLayer::ToolHandle,
    }};

    EXPECT_EQ(kViewportOverlayDrawOrder, expected);
    for (std::size_t index = 0; index < expected.size(); ++index) {
        EXPECT_EQ(viewportOverlayDrawOrder(expected[index]), static_cast<int>(index));
    }
}

TEST(ViewportOverlayPolicyTests, MatchesDepthAndHoverSuppressionContract) {
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::WireframeBase, ViewportOverlayGeometryKind::Lines));
    EXPECT_FALSE(viewportOverlayDepthTested(ViewportOverlayLayer::WireframeXray, ViewportOverlayGeometryKind::Lines));
    EXPECT_FALSE(viewportOverlayDepthTested(ViewportOverlayLayer::SourceWire, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::OpenEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_FALSE(viewportOverlayDepthTested(ViewportOverlayLayer::HoverFaceEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_FALSE(viewportOverlayDepthTested(ViewportOverlayLayer::HoverEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::UnselectedVertex, ViewportOverlayGeometryKind::Points));

    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::SourceWire, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::WireframeXray, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::SelectedFaceEdge, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::SelectedEdge, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::HoverFaceEdge, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::HoverEdge, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::GroupBounds, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::DiagnosticEdge, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::SelectedVertex, ViewportOverlayGeometryKind::Points),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::HoverVertex, ViewportOverlayGeometryKind::Points),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::ToolPlane, ViewportOverlayGeometryKind::Triangles),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::ToolPlaneLine, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::ToolHandle, ViewportOverlayGeometryKind::Points),
            ViewportOverlayDepthMode::DrawOnTop);

    EXPECT_TRUE(viewportOverlayLayerUsesDepthStampedFaceFill(ViewportOverlayLayer::SelectedVisibleFace));
    EXPECT_TRUE(viewportOverlayLayerUsesDepthStampedFaceFill(ViewportOverlayLayer::SelectedXrayFace));
    EXPECT_TRUE(viewportOverlayLayerUsesDepthStampedFaceFill(ViewportOverlayLayer::SelectedFaceMask));
    EXPECT_TRUE(viewportOverlayLayerUsesDepthStampedFaceFill(ViewportOverlayLayer::HoverFace));
    EXPECT_TRUE(viewportOverlayTriangleIsTwoSided(ViewportOverlayLayer::SelectedVisibleFace));
    EXPECT_TRUE(viewportOverlayTriangleIsTwoSided(ViewportOverlayLayer::SelectedXrayFace));
    EXPECT_TRUE(viewportOverlayTriangleIsTwoSided(ViewportOverlayLayer::SelectedFaceMask));
    EXPECT_TRUE(viewportOverlayTriangleIsTwoSided(ViewportOverlayLayer::HoverFace));
    EXPECT_FALSE(viewportOverlayLayerUsesDepthOnlyFaceFill(ViewportOverlayLayer::SourceWireDepthStamp));
    EXPECT_FALSE(viewportOverlayTriangleIsTwoSided(ViewportOverlayLayer::SourceWireDepthStamp));

    EXPECT_TRUE(viewportOverlayLayerWritesTransientHoverSuppressionMask(ViewportOverlayLayer::HoverFace));
    EXPECT_TRUE(viewportOverlayLayerWritesTransientHoverSuppressionMask(ViewportOverlayLayer::HoverEdge));
    EXPECT_FALSE(viewportOverlayLayerWritesTransientHoverSuppressionMask(ViewportOverlayLayer::SourceWire));
    EXPECT_TRUE(viewportOverlayLayerIsSuppressedByTransientHover(ViewportOverlayLayer::SelectedEdge));
    EXPECT_FALSE(viewportOverlayLayerIsSuppressedByTransientHover(ViewportOverlayLayer::SourceWire));
}

TEST(ViewportOverlayPolicyTests, MatchesNativeColorsAndRuntimePointSizes) {
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SelectedVisibleFace, rgba8(255, 255, 255)), kOverlaySelectionFaceColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SelectedFaceEdge, rgba8(255, 255, 255)), kOverlaySelectionWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::WireframeBase, rgba8(255, 255, 255)), rgba8(255, 255, 255));
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::WireframeXray, rgba8(255, 255, 255)), rgba8(255, 255, 255));
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SourceWire, kOverlaySelectedVertexColor), kOverlaySelectionWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SourceWire, rgba8(255, 255, 255)), kOverlaySourceWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::HoverFace, kOverlayRemoveWireColor), kOverlayRemoveFaceColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::HoverEdge, kOverlayRemoveFaceColor), kOverlayRemoveWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::GroupBounds, rgba8(255, 255, 255)), kOverlayGroupBoundsColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolPlane, rgba8(255, 255, 255)), kOverlayToolPlaneColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolPlaneLine, rgba8(255, 255, 255)), kOverlayToolPlaneLineColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolHandle, rgba8(255, 255, 255)), kOverlayToolHandleColor);

    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::WireframeBase, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::WireframeXray, 0.0f), kOverlayWireframeXrayLineWidthPixels);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::SourceWire, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::SelectedEdge, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::HoverEdge, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::OpenEdge, 2.0f), 2.0f);
    EXPECT_FLOAT_EQ(viewportOverlayDepthBias(ViewportOverlayLayer::SourceWire, ViewportOverlayGeometryKind::Lines),
            0.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::GroupBounds, 0.0f), 1.5f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::ToolPlaneLine, 1.0f), 1.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::ToolHandle, 2.0f), 2.0f);
    EXPECT_FLOAT_EQ(viewportOverlayPointSizePixels(ViewportOverlayLayer::UnselectedVertex, 8.0f), 8.0f);
    EXPECT_FLOAT_EQ(viewportOverlayPointSizePixels(ViewportOverlayLayer::SelectedVertex, 9.0f), 9.0f);
    EXPECT_FLOAT_EQ(viewportOverlayPointSizePixels(ViewportOverlayLayer::HoverVertex, 8.0f), 8.0f);
    EXPECT_FLOAT_EQ(viewportOverlayPointSizePixels(ViewportOverlayLayer::ToolHandle, 8.0f), 8.0f);
    EXPECT_TRUE(viewportOverlayPointHasOutline(ViewportOverlayLayer::SelectedVertex));
    EXPECT_FLOAT_EQ(viewportOverlayPointOutlineGrowthPixels(ViewportOverlayLayer::SelectedVertex), 1.0f);
    EXPECT_TRUE(viewportOverlayPointIsOutlineOnly(ViewportOverlayLayer::SnapTargetVertex));
    EXPECT_FLOAT_EQ(viewportOverlayPointInnerRadius(ViewportOverlayLayer::SnapTargetVertex), 0.62f);
}

TEST(ViewportOverlayPolicyTests, RuntimeSelectionOverlayDefaultsAreSemanticRoleTokens) {
    const quader_editor_core::SelectionOverlayStyle style;
    EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(style.source_wire_color_abgr));
    EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(style.selection_face_color_abgr));
    EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(style.selected_edge_color_abgr));
    EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(style.hover_selected_line_color_abgr));
    EXPECT_TRUE(quader_editor_core::selection_overlay_style_token_is_semantic(style.diagnostic_edge_color_abgr));

    quader_editor_core::SelectionOverlayFrame frame;
    frame.triangles.push_back({
            .layer = quader_editor_core::SelectionOverlayLayer::hover_face,
            .a = {0.0f, 0.0f, 0.0f},
            .b = {1.0f, 0.0f, 0.0f},
            .c = {0.0f, 1.0f, 0.0f},
            .color_abgr = style.hover_selected_face_color_abgr,
    });
    frame.lines.push_back({
            .layer = quader_editor_core::SelectionOverlayLayer::source_wire,
            .start = {0.0f, 0.0f, 0.0f},
            .end = {1.0f, 0.0f, 0.0f},
            .color_abgr = style.source_wire_color_abgr,
    });
    frame.points.push_back({
            .layer = quader_editor_core::SelectionOverlayLayer::hover_vertex,
            .position = {0.0f, 0.0f, 0.0f},
            .color_abgr = style.hover_selected_line_color_abgr,
    });

    ModelingObjectRuntimeMap objectRegistry;
    SceneDocument scene;
    ViewportOverlayFrame projected =
            SelectionOverlayProjection::projectSelectionOverlayFrame(frame, objectRegistry, scene);

    ASSERT_EQ(projected.triangles.size(), 1u);
    EXPECT_EQ(projected.triangles[0].styleRole, ViewportOverlayStyleRole::RemoveSelectionFace);
    EXPECT_EQ(projected.triangles[0].color, kOverlayRemoveFaceColor);
    ASSERT_EQ(projected.lines.size(), 1u);
    EXPECT_EQ(projected.lines[0].styleRole, ViewportOverlayStyleRole::SourceWire);
    EXPECT_EQ(projected.lines[0].color, kOverlaySourceWireColor);
    ASSERT_EQ(projected.points.size(), 1u);
    EXPECT_EQ(projected.points[0].styleRole, ViewportOverlayStyleRole::RemoveSelectionVertex);
    EXPECT_EQ(projected.points[0].color, kOverlayRemoveWireColor);

    const std::string runtimeHeader = readSourceFile("src/editor/modeling/runtime/session.hpp");
    const std::string projection = readSourceFile("src/editor/projection/selection_overlay_projection.cpp");
    EXPECT_NE(runtimeHeader.find("SelectionOverlayStyleRole"), std::string::npos);
    EXPECT_NE(runtimeHeader.find("selection_overlay_style_token("), std::string::npos);
    EXPECT_EQ(runtimeHeader.find("0xebffe05cU"), std::string::npos);
    EXPECT_EQ(runtimeHeader.find("0xff1fadffU"), std::string::npos);
    EXPECT_EQ(runtimeHeader.find("0xffffe66bU"), std::string::npos);
    EXPECT_NE(projection.find("selection_overlay_style_role_from_token("), std::string::npos);
    EXPECT_EQ(projection.find("viewportOverlayIsNativeRemoveColor(colorAbgr)"), std::string::npos);
    EXPECT_EQ(projection.find("viewportOverlayIsNativeSelectionWireColor(colorAbgr)"), std::string::npos);
}

TEST(ViewportOverlayStyleResolverTests, AppliesUserSettingsToRuntimeOverlayFrame) {
    ViewportSettings settings = ViewportSettings::defaults();
    ASSERT_TRUE(settings.setColor(ViewportColorSettingId::SourceWire, {1.0f, 0.0f, 0.0f, 1.0f}));
    ASSERT_TRUE(settings.setColor(ViewportColorSettingId::SelectionWire, {0.0f, 1.0f, 0.0f, 1.0f}));
    ASSERT_TRUE(settings.setColor(ViewportColorSettingId::RemoveSelectionWire, {0.0f, 0.0f, 1.0f, 1.0f}));
    ASSERT_TRUE(settings.setColor(ViewportColorSettingId::RemoveSelectionFace, {1.0f, 0.0f, 1.0f, 1.0f}));
    ASSERT_TRUE(settings.setColor(ViewportColorSettingId::HoverVertex, {1.0f, 1.0f, 0.0f, 1.0f}));
    ASSERT_TRUE(settings.setFloatValue(ViewportFloatSettingId::SourceWireLineSize, 2.0f));
    ASSERT_TRUE(settings.setFloatValue(ViewportFloatSettingId::SelectionFaceWireLineSize, 5.0f));
    ASSERT_TRUE(settings.setFloatValue(ViewportFloatSettingId::SelectionEdgeLineSize, 3.0f));
    ASSERT_TRUE(settings.setFloatValue(ViewportFloatSettingId::HoverWireLineSize, 4.0f));
    ASSERT_TRUE(settings.setFloatValue(ViewportFloatSettingId::VertexSize, 8.0f));
    ASSERT_TRUE(settings.setFloatValue(ViewportFloatSettingId::HoverVertexGrowth, 2.0f));
    ASSERT_TRUE(settings.setFloatValue(ViewportFloatSettingId::VertexOutlineSize, 3.0f));

    ViewportOverlayFrame frame;
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::HoverFace,
            .color = rgba8(255, 255, 255),
            .styleRole = ViewportOverlayStyleRole::RemoveSelectionFace,
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .color = kOverlaySelectionWireColor,
            .styleRole = ViewportOverlayStyleRole::SourceWire,
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .color = rgba8(255, 255, 255),
            .styleRole = ViewportOverlayStyleRole::SelectedSourceWire,
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::HoverEdge,
            .color = rgba8(255, 255, 255),
            .styleRole = ViewportOverlayStyleRole::RemoveSelectionWire,
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SelectedEdge,
            .color = rgba8(255, 255, 255),
            .styleRole = ViewportOverlayStyleRole::SelectionWire,
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::GroupBounds,
            .color = rgba8(255, 255, 255),
            .widthPixels = 1.5f,
            .styleRole = ViewportOverlayStyleRole::GroupBounds,
    });
    frame.points.push_back({
            .layer = ViewportOverlayLayer::HoverVertex,
            .color = rgba8(255, 255, 255),
            .sizePixels = 6.0f,
            .styleRole = ViewportOverlayStyleRole::HoverVertex,
    });
    frame.points.push_back({.layer = ViewportOverlayLayer::SnapTargetVertex, .color = rgba8(255, 0, 0), .sizePixels = 14.0f});

    ViewportOverlayStyleResolver::apply(frame, settings);

    ASSERT_EQ(frame.triangles.size(), 1u);
    ASSERT_EQ(frame.lines.size(), 5u);
    ASSERT_EQ(frame.points.size(), 2u);
    EXPECT_EQ(frame.triangles[0].color, rgba8(255, 0, 255));
    EXPECT_EQ(frame.lines[0].color, rgba8(255, 0, 0));
    EXPECT_FLOAT_EQ(frame.lines[0].widthPixels, 2.0f);
    EXPECT_EQ(frame.lines[1].color, rgba8(0, 255, 0));
    EXPECT_FLOAT_EQ(frame.lines[1].widthPixels, 5.0f);
    EXPECT_EQ(frame.lines[2].color, rgba8(0, 0, 255));
    EXPECT_FLOAT_EQ(frame.lines[2].widthPixels, 4.0f);
    EXPECT_EQ(frame.lines[3].color, rgba8(0, 255, 0));
    EXPECT_FLOAT_EQ(frame.lines[3].widthPixels, 3.0f);
    EXPECT_EQ(frame.lines[4].color, kOverlayGroupBoundsColor);
    EXPECT_FLOAT_EQ(frame.lines[4].widthPixels, 1.5f);
    EXPECT_EQ(frame.points[0].color, rgba8(255, 255, 0));
    EXPECT_FLOAT_EQ(frame.points[0].sizePixels, 10.0f);
    ASSERT_TRUE(frame.points[0].outlineGrowthPixels.has_value());
    EXPECT_FLOAT_EQ(*frame.points[0].outlineGrowthPixels, 3.0f);
    EXPECT_EQ(frame.points[1].color, rgba8(255, 0, 0));
    EXPECT_FLOAT_EQ(frame.points[1].sizePixels, 14.0f);
    ASSERT_TRUE(frame.points[1].outlineGrowthPixels.has_value());
    EXPECT_FLOAT_EQ(*frame.points[1].outlineGrowthPixels, 3.0f);
}

TEST(ViewportOverlayStyleResolverTests, SupportsSelectionModeScopedOverrides) {
    ViewportSettings settings = ViewportSettings::defaults();
    ASSERT_TRUE(settings.setColor(ViewportColorSettingId::SourceWire, {1.0f, 0.0f, 0.0f, 1.0f}));

    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .color = rgba8(255, 255, 255),
            .styleRole = ViewportOverlayStyleRole::SourceWire,
    });

    ViewportOverlayStyleResolver::apply(
            frame,
            settings,
            ViewportOverlayStyleContext{
                    .selectionMode = ViewportOverlaySelectionContext::Edge,
                    .overrides = {
                            {.role = ViewportOverlayStyleRole::SourceWire,
                                    .selectionMode = ViewportOverlaySelectionContext::Face,
                                    .color = rgba8(128, 0, 255)},
                            {.role = ViewportOverlayStyleRole::SourceWire,
                                    .selectionMode = ViewportOverlaySelectionContext::Edge,
                                    .color = rgba8(255, 0, 128)},
                    },
            });

    ASSERT_EQ(frame.lines.size(), 1u);
    EXPECT_EQ(frame.lines[0].color, rgba8(255, 0, 128));
}

TEST(ViewportOverlayStyleResolverTests, PrioritizesDiagnosticsThenExternalOverrides) {
    ViewportSettings settings = ViewportSettings::defaults();
    ASSERT_TRUE(settings.setColor(ViewportColorSettingId::SelectionWire, {0.0f, 1.0f, 0.0f, 1.0f}));
    ASSERT_TRUE(settings.setColor(ViewportColorSettingId::DiagnosticEdge, {0.25f, 0.0f, 1.0f, 1.0f}));
    ASSERT_TRUE(settings.setFloatValue(ViewportFloatSettingId::DiagnosticEdgeLineSize, 6.0f));

    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::DiagnosticEdge,
            .color = rgba8(0, 255, 0),
            .widthPixels = 1.0f,
    });

    ViewportOverlayStyleResolver::apply(frame, settings);

    ASSERT_EQ(frame.lines.size(), 1u);
    EXPECT_EQ(frame.lines[0].color, rgba8(64, 0, 255));
    EXPECT_FLOAT_EQ(frame.lines[0].widthPixels, 6.0f);

    ViewportOverlayStyleResolver::apply(
            frame,
            settings,
            ViewportOverlayStyleContext{
                    .selectionMode = ViewportOverlaySelectionContext::Edge,
                    .overrides = {
                            {.role = ViewportOverlayStyleRole::DiagnosticEdge,
                                    .selectionMode = ViewportOverlaySelectionContext::Face,
                                    .color = rgba8(128, 0, 255),
                                    .lineWidthPixels = 3.0f},
                            {.role = ViewportOverlayStyleRole::DiagnosticEdge,
                                    .selectionMode = ViewportOverlaySelectionContext::Edge,
                                    .color = rgba8(255, 0, 128),
                                    .lineWidthPixels = 2.0f},
                    },
            });

    EXPECT_EQ(frame.lines[0].color, rgba8(255, 0, 128));
    EXPECT_FLOAT_EQ(frame.lines[0].widthPixels, 2.0f);
}

TEST(PolygonOperationRegistryTests, MetadataComesFromDescriptorOwner) {
    const std::string descriptor = readSourceFile("src/mesh/polygon/poly_operation_descriptor.hpp");
    const std::string registry = readSourceFile("src/mesh/polygon/operations/poly_operation_registry.cpp");

    EXPECT_NE(descriptor.find("k_poly_operation_descriptors"), std::string::npos);
    EXPECT_NE(registry.find("poly_operation_descriptors()"), std::string::npos);
    EXPECT_EQ(registry.find("std::array<PolyOperationMetadata"), std::string::npos);
    EXPECT_EQ(registry.find("k_poly_operations"), std::string::npos);

    const std::filesystem::path sourceRoot = std::filesystem::path(QUADER_SOURCE_DIR);
    const std::filesystem::path operationsDir = sourceRoot / "src/mesh/polygon/operations";
    ASSERT_TRUE(std::filesystem::exists(operationsDir));
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(operationsDir)) {
        if (entry.path().extension() != ".cpp" ||
                entry.path().filename() == "poly_operation_registry.cpp") {
            continue;
        }
        const std::string relativePath = std::filesystem::relative(entry.path(), sourceRoot).generic_string();
        const std::string source = readSourceFile(relativePath);
        if (source.find("PolyOperation") == std::string::npos) {
            continue;
        }
        SCOPED_TRACE(relativePath);
        EXPECT_FALSE(metadataMethodReturnsLiteral(source, "std::string_view id() const override"));
        EXPECT_FALSE(metadataMethodReturnsLiteral(source, "std::string_view label() const override"));
    }
}

TEST(ModelingSessionServiceOverlayTests, MapsRuntimeOverlayLayersExplicitly) {
    const std::string source = readSourceFile("src/editor/projection/selection_overlay_projection.cpp");
    const std::string modelingService = readSourceFile("src/editor/modeling/services/modeling_session_service.cpp");
    const std::string overlayService = readSourceFile("src/editor/modeling/services/modeling_session_services.cpp");

    EXPECT_EQ(source.find("static_cast<ViewportOverlayLayer>(static_cast<int>(layer))"), std::string::npos);
    EXPECT_NE(source.find("case quader_editor_core::SelectionOverlayLayer::source_wire:"), std::string::npos);
    EXPECT_NE(source.find("case quader_editor_core::SelectionOverlayLayer::snap_target_vertex:"), std::string::npos);
    EXPECT_NE(source.find("case quader_editor_core::SelectionOverlayLayer::diagnostic_edge:"), std::string::npos);
    EXPECT_NE(source.find("case quader_editor_core::SelectionOverlayLayer::source_wire_depth_stamp:"), std::string::npos);
    EXPECT_NE(source.find("lineStyleRole"), std::string::npos);
    EXPECT_NE(source.find("ViewportOverlayStyleRole::SelectedSourceWire"), std::string::npos);
    EXPECT_NE(source.find("appendProjectedOverlayLines"), std::string::npos);
    EXPECT_EQ(modelingService.find("runtime_.knife_state().preview_lines"), std::string::npos);
    EXPECT_NE(overlayService.find("runtime.knife_state().preview_lines"), std::string::npos);
    EXPECT_NE(overlayService.find("ViewportOverlayLayer::ToolPreviewLine"), std::string::npos);
    EXPECT_EQ(overlayService.find("knife.preview_lines.begin()"), std::string::npos);
}

TEST(ViewportCameraAdapterTests, BuildsOverlayAndModelingCamerasFromSharedBasis) {
    ViewportCameraController camera;
    const ViewportOverlayCamera overlay = ViewportCameraAdapter::overlayCamera(camera, {0.0f, 240.0f});
    const ModelingViewportCamera modeling = ViewportCameraAdapter::modelingCamera(camera, {640.0f, 360.0f});

    EXPECT_EQ(overlay.projection, ViewportOverlayProjection::Perspective);
    EXPECT_FALSE(modeling.orthographic);
    EXPECT_FLOAT_EQ(overlay.viewportSize.x, 1.0f);
    EXPECT_FLOAT_EQ(overlay.viewportSize.y, 240.0f);
    expectVec3Near(modeling.origin, overlay.eye);
    expectVec3Near(modeling.xAxis, overlay.right);
    expectVec3Near(modeling.yAxis, overlay.up);
    expectVec3Near(modeling.zAxis, overlay.forward * -1.0f);
    EXPECT_NEAR(length(overlay.forward), 1.0f, 0.001f);
    EXPECT_NEAR(length(overlay.right), 1.0f, 0.001f);
    EXPECT_NEAR(length(overlay.up), 1.0f, 0.001f);
}

TEST(ViewportGridPlacementPolicyTests, ResolvesPerspectiveAndOrthographicPlanesWithoutFilament) {
    ViewportCameraController camera;
    camera.setViewportSize(200.0f, 100.0f);

    const ViewportGridPlacement perspective =
            ViewportGridPlacementPolicy::placement(camera, 200u, 100u);
    EXPECT_EQ(perspective.plane, ViewportGridPlane::XZ);
    EXPECT_EQ(perspective.rotation, ViewportGridRotation::Identity);
    EXPECT_FLOAT_EQ(perspective.planeSize.x, kViewportGridDefaultPlaneSize);
    EXPECT_FLOAT_EQ(perspective.planeSize.y, kViewportGridDefaultPlaneSize);
    expectVec3Near(perspective.axes.uAxis, {1.0f, 0.0f, 0.0f});
    expectVec3Near(perspective.axes.vAxis, {0.0f, 0.0f, -1.0f});
    EXPECT_EQ(perspective.axes.uColor, ViewportColorSettingId::GridXAxis);
    EXPECT_EQ(perspective.axes.vColor, ViewportColorSettingId::GridZAxis);

    camera.viewAxis(ViewportViewAxis::X, 1);
    const ViewportGridPlacement side =
            ViewportGridPlacementPolicy::placement(camera, 200u, 100u);
    const float orthoPlaneHeight = camera.state().orthographicSize * kViewportGridOrthographicMargin;
    EXPECT_EQ(side.plane, ViewportGridPlane::YZ);
    EXPECT_EQ(side.rotation, ViewportGridRotation::NegativeHalfPiAroundZ);
    EXPECT_FLOAT_EQ(side.origin.x, kViewportGridPlaneOffset);
    EXPECT_FLOAT_EQ(side.planeSize.x, orthoPlaneHeight);
    EXPECT_FLOAT_EQ(side.planeSize.y, orthoPlaneHeight * 2.0f);
    expectVec3Near(side.axes.uAxis, {0.0f, 0.0f, -1.0f});
    expectVec3Near(side.axes.vAxis, {0.0f, 1.0f, 0.0f});
    EXPECT_EQ(side.axes.uColor, ViewportColorSettingId::GridZAxis);
    EXPECT_EQ(side.axes.vColor, ViewportColorSettingId::GridYAxis);

    camera.viewAxis(ViewportViewAxis::Z, 1);
    const ViewportGridPlacement front =
            ViewportGridPlacementPolicy::placement(camera, 200u, 100u);
    EXPECT_EQ(front.plane, ViewportGridPlane::XY);
    EXPECT_EQ(front.rotation, ViewportGridRotation::PositiveHalfPiAroundX);
    EXPECT_FLOAT_EQ(front.origin.z, kViewportGridPlaneOffset);
    EXPECT_FLOAT_EQ(front.planeSize.x, camera.state().orthographicSize * kViewportGridOrthographicMargin * 2.0f);
    EXPECT_FLOAT_EQ(front.planeSize.y, camera.state().orthographicSize * kViewportGridOrthographicMargin);
    expectVec3Near(front.axes.uAxis, {1.0f, 0.0f, 0.0f});
    expectVec3Near(front.axes.vAxis, {0.0f, 1.0f, 0.0f});
}

TEST(SceneRenderBridgeTests, UsesDepthStampedFaceFillForOverlayTriangles) {
    const std::string bridgeSource = readSourceFile("src/render/scene_render_bridge.cpp");
    const std::string builderSource = readSourceFile("src/render/overlay/overlay_renderable_builder.cpp");
    const std::string proxyFactorySource = readSourceFile("src/render/overlay/overlay_proxy_factory.cpp");
    const std::string toolPreviewCacheSource = readSourceFile("src/render/tool_preview_renderable_cache.cpp");

    EXPECT_NE(builderSource.find("viewportOverlayLayerUsesDepthStampedFaceFill"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("setColorWrite(state.colorWrite)"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("setDepthWrite(state.depthWrite)"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("setDepthFunc(toFilamentDepthFunc(state.depthFunc))"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("setTransparencyMode(toFilamentTransparencyMode(state.transparencyMode))"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("setPolygonOffset(state.polygonOffsetScale, state.polygonOffsetConstant)"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("DepthFunc::GE"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("DepthFunc::E"), std::string::npos);
    EXPECT_NE(builderSource.find("OverlayCullingMode::Back"), std::string::npos);
    EXPECT_NE(builderSource.find("OverlayTransparencyMode::TwoPassesOneSide"), std::string::npos);
    EXPECT_NE(builderSource.find("viewportOverlayTriangleIsTwoSided(layer)"), std::string::npos);
    EXPECT_NE(builderSource.find("OverlayCullingMode::None"), std::string::npos);
    EXPECT_NE(builderSource.find("OverlayTransparencyMode::TwoPassesTwoSides"), std::string::npos);
    EXPECT_NE(bridgeSource.find("setToolPreviewMeshes(std::vector<PreviewMesh> meshes"), std::string::npos);
    EXPECT_NE(bridgeSource.find("toolPreviewCache_.setMeshes(meshes, assets)"), std::string::npos);
    EXPECT_NE(toolPreviewCacheSource.find("meshProxies_"), std::string::npos);
    EXPECT_NE(toolPreviewCacheSource.find("void ToolPreviewRenderableCache::clear()"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesQuadMaterialsForSelectionLineAndPointOverlay) {
    const std::string proxyFactorySource = readSourceFile("src/render/overlay/overlay_proxy_factory.cpp");
    const std::string lineBuilderSource = readSourceFile("src/render/overlay/overlay_line_mesh_builder.cpp");
    const std::string pointBuilderSource = readSourceFile("src/render/overlay/overlay_point_mesh_builder.cpp");
    const std::string renderableBuilderSource = readSourceFile("src/render/overlay/overlay_renderable_builder.hpp");
    const std::string projectorSource = readSourceFile("src/render/overlay/overlay_camera_projector.cpp");

    EXPECT_NE(lineBuilderSource.find("appendLineQuad"), std::string::npos);
    EXPECT_NE(pointBuilderSource.find("appendPointQuad"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("material.createInstance()"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("*overlayLineMaterial_"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("*overlayPointMaterial_"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("filament::VertexAttribute::CUSTOM0"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("AttributeType::FLOAT4, offsetof(OverlayLineVertex, position)"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("AttributeType::FLOAT4, offsetof(OverlayPointVertex, position)"), std::string::npos);
    EXPECT_NE(projectorSource.find("OverlayProjectedPoint project"), std::string::npos);
    EXPECT_NE(projectorSource.find("nearPlaneDistance(a)"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("setParameter(\"lineWidth\", batch.widthPixels)"), std::string::npos);
    EXPECT_EQ(proxyFactorySource.find("setParameter(\"pointSize\", pointSizePixels)"), std::string::npos);
    EXPECT_EQ(proxyFactorySource.find("setParameter(\"nearPlane\""), std::string::npos);
    EXPECT_NE(renderableBuilderSource.find("LineBatchKey"), std::string::npos);
    EXPECT_EQ(proxyFactorySource.find("PrimitiveType::LINES"), std::string::npos);
    EXPECT_EQ(proxyFactorySource.find("PrimitiveType::POINTS"), std::string::npos);
}

TEST(SceneRenderBridgeTests, BindsNormalsAndBuildsPrimitiveSpans) {
    const std::string source = readSourceFile("src/render/mesh_renderable_factory.cpp");

    EXPECT_NE(source.find("tangentFrameFromNormal(vertex.normal)"), std::string::npos);
    EXPECT_NE(source.find("filament::VertexAttribute::TANGENTS"), std::string::npos);
    EXPECT_NE(source.find("normalizedMeshPrimitiveSpans(mesh)"), std::string::npos);
    EXPECT_NE(source.find("primitive.indexOffset"), std::string::npos);
    EXPECT_NE(source.find("primitive.indexCount"), std::string::npos);
    EXPECT_NE(source.find("primitive.materialSlot"), std::string::npos);
    EXPECT_NE(source.find("material->materialForSlot(primitive.materialSlot)"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesAssetRevisionsForIncrementalMeshSync) {
    const std::string syncSource = readSourceFile("src/render/scene_render_sync_service.cpp");
    const std::string extractionHeader = readSourceFile("src/render/scene_render_extraction.hpp");
    const std::string extractionSource = readSourceFile("src/render/scene_render_extraction.cpp");
    const std::string cacheSource = readSourceFile("src/render/mesh_renderable_cache.cpp");

    EXPECT_NE(syncSource.find("syncedAssetRevision_"), std::string::npos);
    EXPECT_NE(syncSource.find("renderAssetRevisionsChanged"), std::string::npos);
    EXPECT_NE(syncSource.find("buildSceneRenderExtractionPlan(document, assets, *materialBinding_)"), std::string::npos);
    EXPECT_NE(syncSource.find("meshCache_->needsRebuild"), std::string::npos);
    EXPECT_NE(extractionHeader.find("struct SceneRenderExtractionPlan"), std::string::npos);
    EXPECT_NE(extractionSource.find("materialBinding.materialRevision(assets, materialComponent)"), std::string::npos);
    EXPECT_NE(extractionSource.find("document.isEffectivelyVisible(entityId)"), std::string::npos);
    EXPECT_NE(extractionSource.find("document.worldTransformMatrix(entityId)"), std::string::npos);
    EXPECT_NE(cacheSource.find("existing->second.meshRevision != mesh.revision.value"), std::string::npos);
    EXPECT_NE(cacheSource.find("existing->second.materialRevision != materialRevision"), std::string::npos);
    EXPECT_NE(cacheSource.find("factory_->updateTransform(existing->second, worldTransform)"), std::string::npos);
    const std::size_t changeStart = syncSource.find("bool SceneRenderSyncService::renderAssetRevisionsChanged");
    ASSERT_NE(changeStart, std::string::npos);
    const std::size_t rebuildStart = syncSource.find("void SceneRenderSyncService::rebuild(");
    ASSERT_NE(rebuildStart, std::string::npos);
    const std::string changeSource = syncSource.substr(changeStart, rebuildStart - changeStart);
    EXPECT_EQ(changeSource.find("document.entities()"), std::string::npos);
    EXPECT_EQ(changeSource.find("document.isEffectivelyVisible"), std::string::npos);
    EXPECT_EQ(changeSource.find("assets.mesh("), std::string::npos);
    EXPECT_EQ(changeSource.find("materialBinding_->materialRevision"), std::string::npos);
    const std::string rebuildSource = syncSource.substr(rebuildStart);
    EXPECT_NE(rebuildSource.find("meshCache_->removeExcept(plan.liveMeshEntities)"), std::string::npos);
    EXPECT_EQ(rebuildSource.find("meshProxies_.clear()"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesAnisotropicSamplingForMeshTextures) {
    const std::string source = readSourceFile("src/render/material_binding_service.cpp");

    EXPECT_NE(source.find("kMeshTextureAnisotropy"), std::string::npos);
    EXPECT_NE(source.find("sampler.setAnisotropy(kMeshTextureAnisotropy)"), std::string::npos);
    EXPECT_NE(source.find("filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR"), std::string::npos);
}

TEST(RenderSystemTests, ShadedViewportUsesClearColorWithoutSkybox) {
    const std::string source = readSourceFile("src/render/render_system.cpp");

    EXPECT_NE(source.find("renderer_->setClearOptions(clear)"), std::string::npos);
    EXPECT_NE(source.find("clear.clearColor = {environment.clearColor.x"), std::string::npos);
    EXPECT_EQ(source.find("scene_->setSkybox(skybox_)"), std::string::npos);
    EXPECT_EQ(source.find("Skybox::Builder()"), std::string::npos);
}

TEST(RenderSystemTests, ExposesCompletedFramePerformanceStatsFromRendererHistory) {
    const std::string header = readSourceFile("src/render/render_system.hpp");
    const std::string source = readSourceFile("src/render/render_system.cpp");

    EXPECT_NE(header.find("RenderFramePerformanceStats framePerformanceStats() const"), std::string::npos);
    EXPECT_NE(header.find("RenderFramePerformanceStats framePerformanceStats_"), std::string::npos);
    EXPECT_NE(source.find("frameCpuBegin_ = std::chrono::steady_clock::now()"), std::string::npos);
    EXPECT_NE(source.find("std::chrono::duration<double, std::milli>"), std::string::npos);
    EXPECT_NE(source.find("renderer_->getFrameInfoHistory(historySize)"), std::string::npos);
    EXPECT_NE(source.find("renderer_->getMaxFrameHistorySize()"), std::string::npos);
    EXPECT_NE(source.find("info.denoisedGpuFrameDuration"), std::string::npos);
    EXPECT_NE(source.find("info.gpuFrameDuration"), std::string::npos);
    EXPECT_NE(source.find("filament::Renderer::FrameInfo::INVALID"), std::string::npos);
    EXPECT_NE(source.find("filament::Renderer::FrameInfo::PENDING"), std::string::npos);
}

TEST(ViewportRendererTests, EnvironmentClearColorAndRenderedBackgroundPathsStaySceneOwned) {
    const std::string renderer = readSourceFile("src/render/viewport_renderer.cpp");
    const std::string environmentLibrary = readSourceFile("src/render/filament_environment_library.cpp");

    EXPECT_NE(renderer.find("viewportClearColor(settings.environment, renderedMode)"), std::string::npos);
    EXPECT_NE(renderer.find("if (renderedMode)"), std::string::npos);
    EXPECT_NE(renderer.find("environment.clearColor.x"), std::string::npos);
    EXPECT_EQ(renderer.find("ViewportColorSettingId::Background"), std::string::npos);
    EXPECT_EQ(renderer.find("settings.color(ViewportColorSettingId"), std::string::npos);
    EXPECT_NE(environmentLibrary.find("settings.showBackground && settings.backgroundIntensity > 0.0f"), std::string::npos);
}

TEST(RenderSystemTests, InitializeUnwindsPartialFilamentStateOnFailure) {
    const std::string header = readSourceFile("src/render/render_system.hpp");
    const std::string source = readSourceFile("src/render/render_system.cpp");

    EXPECT_NE(header.find("[[nodiscard]] bool initialized() const"), std::string::npos);
    EXPECT_NE(source.find("shutdown();\n    LOG_INFO(\"Initializing Filament renderer"), std::string::npos);
    EXPECT_NE(source.find("const auto fail = [&](std::string_view message)"), std::string::npos);
    EXPECT_NE(source.find("shutdown();\n        return false;"), std::string::npos);
    EXPECT_NE(source.find("if (!swapChain_)"), std::string::npos);
    EXPECT_NE(source.find("if (!renderer_)"), std::string::npos);
    EXPECT_NE(source.find("if (!scene_)"), std::string::npos);
    EXPECT_NE(source.find("if (!materialLibrary_.initialize(*engine_, executableDir))"), std::string::npos);
    EXPECT_NE(source.find("return beginFrameWithSwapChain(swapChain_)"),
            std::string::npos);
}

TEST(RenderProxyLifecycleTests, RenderableProxyHandlesAreMoveOnly) {
    static_assert(!std::is_copy_constructible_v<MeshRenderableProxy>);
    static_assert(!std::is_copy_assignable_v<MeshRenderableProxy>);
    static_assert(std::is_move_constructible_v<MeshRenderableProxy>);
    static_assert(!std::is_copy_constructible_v<OverlayRenderableProxy>);
    static_assert(!std::is_copy_assignable_v<OverlayRenderableProxy>);
    static_assert(std::is_move_constructible_v<OverlayRenderableProxy>);

    const std::string meshProxyHeader = readSourceFile("src/render/mesh_renderable_proxy.hpp");
    const std::string meshFactorySource = readSourceFile("src/render/mesh_renderable_factory.cpp");
    const std::string overlayProxyHeader = readSourceFile("src/render/overlay/overlay_renderable_proxy.hpp");
    const std::string overlayFactorySource = readSourceFile("src/render/overlay/overlay_proxy_factory.cpp");
    const std::string lightCacheHeader = readSourceFile("src/render/light_renderable_cache.hpp");
    const std::string lightCacheSource = readSourceFile("src/render/light_renderable_cache.cpp");

    EXPECT_NE(meshProxyHeader.find("MeshRenderableProxy(const MeshRenderableProxy&) = delete"), std::string::npos);
    EXPECT_NE(meshProxyHeader.find("MeshRenderableProxy(MeshRenderableProxy&& other) noexcept"), std::string::npos);
    EXPECT_NE(meshProxyHeader.find("std::exchange(other.vertexBuffer, nullptr)"), std::string::npos);
    EXPECT_NE(meshFactorySource.find("class MeshRenderableCreationGuard"), std::string::npos);
    EXPECT_NE(meshFactorySource.find("destroyMeshRenderableProxyResources"), std::string::npos);
    EXPECT_NE(meshFactorySource.find("creationGuard.release()"), std::string::npos);
    EXPECT_NE(overlayProxyHeader.find("OverlayRenderableProxy(const OverlayRenderableProxy&) = delete"), std::string::npos);
    EXPECT_NE(overlayProxyHeader.find("OverlayRenderableProxy(OverlayRenderableProxy&& other) noexcept"), std::string::npos);
    EXPECT_NE(overlayFactorySource.find("class OverlayProxyCreationGuard"), std::string::npos);
    EXPECT_NE(overlayFactorySource.find("destroyOverlayProxyResources"), std::string::npos);
    EXPECT_NE(overlayFactorySource.find("creationGuard.release()"), std::string::npos);
    EXPECT_NE(overlayFactorySource.find("proxies.push_back(std::move(proxy))"), std::string::npos);
    EXPECT_NE(overlayFactorySource.find("void appendOverlayProxy("), std::string::npos);
    EXPECT_EQ(countOccurrences(overlayFactorySource, "filament::RenderableManager::Builder(1)"), 1u);
    EXPECT_EQ(countOccurrences(overlayFactorySource, "scene.addEntity(entity)"), 1u);
    EXPECT_EQ(overlayFactorySource.find("scene_->addEntity(entity)"), std::string::npos);
    EXPECT_EQ(countOccurrences(overlayFactorySource, "proxies.push_back(std::move(proxy))"), 1u);
    EXPECT_NE(lightCacheHeader.find("LightProxy(const LightProxy&) = delete"), std::string::npos);
    EXPECT_NE(lightCacheHeader.find("LightProxy(LightProxy&& other) noexcept"), std::string::npos);
    EXPECT_NE(lightCacheSource.find("lightProxies_.emplace(entityId, std::move(proxy))"), std::string::npos);
    EXPECT_NE(lightCacheSource.find("LightProxyCreationGuard creationGuard"), std::string::npos);
    EXPECT_NE(lightCacheSource.find("creationGuard.release()"), std::string::npos);
}

TEST(RenderProxyLifecycleTests, FilamentResourceCreationUsesRenderOwnedGuards) {
    const std::string lightCacheSource = readSourceFile("src/render/light_renderable_cache.cpp");
    const std::string gridRendererSource = readSourceFile("src/render/viewport_grid_renderer.cpp");
    const std::string renderTargetSource = readSourceFile("src/render/viewport_render_target.cpp");

    EXPECT_NE(lightCacheSource.find("LightProxyCreationGuard creationGuard"), std::string::npos);
    EXPECT_NE(lightCacheSource.find("creationGuard.release()"), std::string::npos);
    EXPECT_NE(gridRendererSource.find("struct GroundGridCreationGuard"), std::string::npos);
    EXPECT_NE(gridRendererSource.find("creationGuard.vertexBuffer"), std::string::npos);
    EXPECT_NE(gridRendererSource.find("creationGuard.release()"), std::string::npos);
    EXPECT_NE(renderTargetSource.find("struct RenderTargetCreationGuard"), std::string::npos);
    EXPECT_NE(renderTargetSource.find("creationGuard.colorTexture"), std::string::npos);
    EXPECT_NE(renderTargetSource.find("creationGuard.release()"), std::string::npos);
    EXPECT_LT(renderTargetSource.find("textureRegistry_->registerTexture(creationGuard.colorTexture"),
            renderTargetSource.find("creationGuard.release()"));
}

TEST(RenderProxyLifecycleTests, RenderResourceOwnersAreNonTransferable) {
    EXPECT_TRUE(renderOwnerIsNonTransferable<RenderSystem>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<FilamentMaterialLibrary>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<TextureRegistry>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<TextureBindingService>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<MaterialBindingService>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<MeshRenderableFactory>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<MeshRenderableCache>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<ToolPreviewRenderableCache>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<LightRenderableCache>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<SceneRenderSyncService>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<SceneRenderBridge>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<OverlayProxyFactory>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<OverlayRenderableCache>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<ViewportGridRenderer>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<ViewportRenderTarget>());
    EXPECT_TRUE(renderOwnerIsNonTransferable<ViewportRenderer>());

    const std::string renderSystemHeader = readSourceFile("src/render/render_system.hpp");
    const std::string sceneRenderBridgeHeader = readSourceFile("src/render/scene_render_bridge.hpp");
    const std::string viewportRendererHeader = readSourceFile("src/render/viewport_renderer.hpp");

    EXPECT_NE(renderSystemHeader.find("RenderSystem(const RenderSystem&) = delete"), std::string::npos);
    EXPECT_NE(sceneRenderBridgeHeader.find("SceneRenderBridge(SceneRenderBridge&&) = delete"), std::string::npos);
    EXPECT_NE(viewportRendererHeader.find("ViewportRenderer(const ViewportRenderer&) = delete"), std::string::npos);
}

TEST(ViewportContextMenuPolicyTests, OpensForRightClickReleaseInsideViewportWithActions) {
    EXPECT_TRUE(shouldOpenViewportContextMenu({
            .rightPressTracked = true,
            .rightReleased = true,
            .releaseInsideViewport = true,
            .targetHit = true,
            .hasActions = true,
            .pressScreen = {200.0f, 180.0f},
            .releaseScreen = {201.0f, 181.0f},
    }));
}

TEST(ViewportContextMenuPolicyTests, BlocksModalToolsMissingActionsAndDrags) {
    ViewportContextMenuOpenRequest request{
            .rightPressTracked = true,
            .rightReleased = true,
            .releaseInsideViewport = true,
            .targetHit = true,
            .hasActions = true,
            .pressScreen = {200.0f, 180.0f},
            .releaseScreen = {201.0f, 181.0f},
    };

    request.modalToolOwnsViewport = true;
    EXPECT_FALSE(shouldOpenViewportContextMenu(request));

    request.modalToolOwnsViewport = false;
    request.targetHit = false;
    EXPECT_FALSE(shouldOpenViewportContextMenu(request));

    request.targetHit = true;
    request.hasActions = false;
    EXPECT_FALSE(shouldOpenViewportContextMenu(request));

    request.hasActions = true;
    request.releaseScreen = {240.0f, 180.0f};
    EXPECT_FALSE(shouldOpenViewportContextMenu(request));

    request.releaseScreen = {201.0f, 181.0f};
    request.accumulatedDragDelta = {20.0f, 0.0f};
    EXPECT_FALSE(shouldOpenViewportContextMenu(request));
}

TEST(ViewportContextMenuPolicyTests, PreservesHoverOnlyForClickLikeRightPress) {
    EXPECT_TRUE(shouldPreserveViewportContextMenuHover({
            .rightPressTracked = true,
            .flyHudActive = false,
            .accumulatedDragDelta = {1.0f, 1.0f},
    }));

    EXPECT_FALSE(shouldPreserveViewportContextMenuHover({
            .rightPressTracked = false,
            .flyHudActive = false,
            .accumulatedDragDelta = {1.0f, 1.0f},
    }));
    EXPECT_FALSE(shouldPreserveViewportContextMenuHover({
            .rightPressTracked = true,
            .flyHudActive = true,
            .accumulatedDragDelta = {1.0f, 1.0f},
    }));
    EXPECT_FALSE(shouldPreserveViewportContextMenuHover({
            .rightPressTracked = true,
            .flyHudActive = false,
            .accumulatedDragDelta = {20.0f, 0.0f},
    }));
}

TEST(ModelingInteractionTests, AxisDragDistanceMathLivesInSharedHelper) {
    const std::string extrudeSource = readSourceFile("src/editor/interactions/extrude_tool.cpp");
    const std::string boxSource = readSourceFile("src/editor/interactions/box_tool.cpp");
    const std::string dragMathSource = readSourceFile("src/editor/interactions/tool_drag_math.cpp");

    EXPECT_NE(dragMathSource.find("DragDistanceResult axis_distance_from_ray"), std::string::npos);
    EXPECT_NE(extrudeSource.find("axis_distance_from_ray("), std::string::npos);
    EXPECT_NE(boxSource.find("axis_distance_from_ray("), std::string::npos);
    EXPECT_EQ(extrudeSource.find("geometry_axis_drag_distance"), std::string::npos);
    EXPECT_EQ(boxSource.find("box_geometry_axis_drag_distance"), std::string::npos);
}

TEST(ModelingSessionServiceTests, SelectedHoverOverlayKeepsRemovePreviewContract) {
    const std::string appOverlay = readSourceFile("src/editor/modeling/services/modeling_session_services.cpp");
    const std::string runtimeOverlay = readSourceFile("src/editor/modeling/runtime/session_selection_overlay.cpp");

    EXPECT_EQ(appOverlay.find("selectedHoverTarget && !hover.selected_modifier_pressed"), std::string::npos);
    EXPECT_EQ(appOverlay.find("selection_hover_target_matches_selection(session.objects(), session.selection_mode(), hover)"), std::string::npos);
    EXPECT_NE(appOverlay.find("selection_hover_target_is_selected(session.objects(), session.selection_mode(), hover)"), std::string::npos);
    EXPECT_EQ(appOverlay.find("if (hoverFrame.hover_target_selected)"), std::string::npos);
    EXPECT_EQ(runtimeOverlay.find("suppress_hover_overlay"), std::string::npos);
    EXPECT_NE(
            runtimeOverlay.find("frame.hover_target_selected = hover.selected_modifier_pressed && selected_hover_target"),
            std::string::npos);
    EXPECT_NE(
            runtimeOverlay.find("if (object.id != hover.object_id || hover.kind == SelectionHoverKind::none)"),
            std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesExplicitTextureColorSpaceForMaterialUploads) {
    const std::string textureSource = readSourceFile("src/render/texture_binding_service.cpp");
    const std::string materialSource = readSourceFile("src/render/material_binding_service.cpp");
    const std::string factorySource = readSourceFile("src/render/mesh_renderable_factory.cpp");
    const std::string appSource = readSourceFile("src/app/app.cpp");
    const std::string materialLibrarySource = readSourceFile("src/render/filament_material_library.cpp");
    const std::string sceneBridgeSource = readSourceFile("src/render/scene_render_bridge.cpp");

    EXPECT_NE(textureSource.find("createSolidTexture(255, 255, 255, 255, TextureColorSpace::Srgb)"), std::string::npos);
    EXPECT_NE(textureSource.find("createSolidTexture(255, 255, 255, 255, TextureColorSpace::Linear)"), std::string::npos);
    EXPECT_NE(textureSource.find("texture.colorSpace == TextureColorSpace::Srgb ? filament::Texture::InternalFormat::SRGB8_A8 : filament::Texture::InternalFormat::RGBA8"), std::string::npos);
    EXPECT_NE(materialSource.find("MinFilter::LINEAR_MIPMAP_LINEAR"), std::string::npos);
    EXPECT_NE(textureSource.find("mipLevelCount(width, height)"), std::string::npos);
    EXPECT_NE(textureSource.find("Texture::Usage::GEN_MIPMAPPABLE"), std::string::npos);
    EXPECT_NE(textureSource.find("generateMipmaps(*engine_)"), std::string::npos);
    EXPECT_NE(materialSource.find("materialInstance->setCullingMode(doubleSided"), std::string::npos);
    EXPECT_EQ(materialSource.find("colorCulling"), std::string::npos);
    EXPECT_EQ(materialSource.find("setCullingMode(\n            colorCulling"), std::string::npos);
    EXPECT_NE(materialSource.find("filament::MaterialInstance::CullingMode::NONE"), std::string::npos);
    EXPECT_NE(materialSource.find(": filament::MaterialInstance::CullingMode::BACK"), std::string::npos);
    EXPECT_EQ(materialSource.find("setCullingMode(filament::MaterialInstance::CullingMode::BACK);"), std::string::npos);
    EXPECT_NE(appSource.find("projectSession_.assets().addTexture(std::move(roughness));"), std::string::npos);
    EXPECT_NE(appSource.find("setMaterialTextureSlot(defaultMaterial, MaterialTextureSlot::BaseColor, albedoTexture)"), std::string::npos);
    EXPECT_EQ(appSource.find("makeDefaultQuaderMaterial(\"Default Quader\", albedoTexture"), std::string::npos);
    EXPECT_EQ(appSource.find("const AssetId roughnessTexture = projectSession_.assets().addTexture"), std::string::npos);
    EXPECT_NE(appSource.find("renderSystem_.quaderMeshMaterial()"), std::string::npos);
    EXPECT_NE(appSource.find("renderSystem_.quaderMeshLitMaterial()"), std::string::npos);
    EXPECT_NE(materialLibrarySource.find("filament_material_catalog.hpp"), std::string::npos);
    EXPECT_NE(materialLibrarySource.find("std::string(kFilamentMaterialDirectory)"), std::string::npos);
    EXPECT_NE(materialLibrarySource.find("std::string(kQuaderMeshMaterialFile)"), std::string::npos);
    EXPECT_NE(materialLibrarySource.find("std::string(kQuaderMeshLitMaterialFile)"), std::string::npos);
    EXPECT_EQ(materialLibrarySource.find("materials\" / \"quader_mesh.filamat\""), std::string::npos);
    EXPECT_EQ(materialLibrarySource.find("materials\" / \"quader_mesh_lit.filamat\""), std::string::npos);
    EXPECT_NE(sceneBridgeSource.find("materialBinding_.initialize(shadedMeshMaterial, renderedMeshMaterial, textureBinding_)"), std::string::npos);
    EXPECT_NE(factorySource.find("materialBinding_->createMeshMaterialInstance(assets, primitiveMaterial, doubleSided, editorUnlit)"), std::string::npos);
    EXPECT_NE(materialSource.find("editorUnlit ? *shadedMeshMaterial_ : *renderedMeshMaterial_"), std::string::npos);
    EXPECT_NE(materialSource.find("if (!editorUnlit)"), std::string::npos);
    EXPECT_EQ(materialSource.find("materialInstance->setParameter(\"editorUnlit\""), std::string::npos);
    EXPECT_EQ(factorySource.find(".culling(!doubleSided)"), std::string::npos);
    EXPECT_NE(textureSource.find("TextureCacheKey cacheKey"), std::string::npos);
    EXPECT_NE(textureSource.find(".colorSpace = texture->colorSpace"), std::string::npos);
    EXPECT_EQ(textureSource.find("bool srgb"), std::string::npos);
    EXPECT_EQ(materialSource.find("bool srgb"), std::string::npos);
}

TEST(SceneRenderBridgeTests, TextureBindingServiceEvictsObsoleteTextureRevisions) {
    const std::string textureHeader = readSourceFile("src/render/texture_binding_service.hpp");
    const std::string textureSource = readSourceFile("src/render/texture_binding_service.cpp");

    EXPECT_NE(textureHeader.find("void evictObsoleteTextureRevisions(const TextureCacheKey& currentKey);"), std::string::npos);
    EXPECT_NE(textureSource.find("evictObsoleteTextureRevisions(cacheKey);"), std::string::npos);
    EXPECT_NE(textureSource.find("void TextureBindingService::evictObsoleteTextureRevisions"), std::string::npos);
    EXPECT_NE(textureSource.find("it->first.id == currentKey.id"), std::string::npos);
    EXPECT_NE(textureSource.find("it->first.colorSpace == currentKey.colorSpace"), std::string::npos);
    EXPECT_NE(textureSource.find("it->first.revision != currentKey.revision"), std::string::npos);
    EXPECT_NE(textureSource.find("destroyTexture(it->second);"), std::string::npos);
    EXPECT_NE(textureSource.find("textureCache_.erase(it)"), std::string::npos);
}

TEST(SceneRenderBridgeTests, SkipsOverlayProxyRebuildWhenOverlayPayloadIsUnchanged) {
    const std::string cacheSource = readSourceFile("src/render/overlay/overlay_renderable_cache.cpp");
    const std::string cacheHeader = readSourceFile("src/render/overlay/overlay_renderable_cache.hpp");
    const std::string builderSource = readSourceFile("src/render/overlay/overlay_renderable_builder.cpp");

    EXPECT_NE(cacheSource.find("OverlayRenderableBuilder::frameSignature"), std::string::npos);
    EXPECT_NE(cacheSource.find("OverlayRenderableBuilder::layerSignature"), std::string::npos);
    EXPECT_NE(cacheHeader.find("std::array<OverlaySlot, kMaxViewportOverlaySlots> slots_"), std::string::npos);
    EXPECT_NE(cacheSource.find("slot.proxyGroups"), std::string::npos);
    EXPECT_NE(builderSource.find("RenderSignatureBuilder::overlayFrameSignature(frame)"), std::string::npos);
    EXPECT_NE(builderSource.find("RenderSignatureBuilder::overlayLayerSignature(frame, layer)"), std::string::npos);
    EXPECT_NE(cacheSource.find("slot.frameSignature"), std::string::npos);
    EXPECT_NE(cacheSource.find("OverlayRenderableBuilder::cameraSignature"), std::string::npos);
    EXPECT_NE(cacheSource.find("const bool cameraChanged = cameraSignature != slot.cameraSignature"), std::string::npos);
    EXPECT_NE(cacheSource.find("const bool layerChanged = layerMask != slot.layerMask"), std::string::npos);
    EXPECT_NE(cacheSource.find("if (signature == slot.frameSignature && !cameraChanged && !layerChanged)"), std::string::npos);
    EXPECT_NE(cacheSource.find("slot.frame = std::move(frame);\n        return false;"), std::string::npos);
    EXPECT_NE(cacheSource.find("group.signature = 0;"), std::string::npos);
    EXPECT_NE(cacheSource.find("rebuildSlot(slot);"), std::string::npos);
    EXPECT_NE(cacheSource.find("if (group.signature == layerSignature)"), std::string::npos);
    EXPECT_NE(cacheSource.find("factory_->createLayerProxies(plan, slot.layerMask)"), std::string::npos);
    EXPECT_NE(builderSource.find("viewportOverlayDrawOrder(layer) * 64"), std::string::npos);
}

TEST(ModelingSessionServiceTests, CachesSelectionOverlayFrameByRevisionAndHoverState) {
    const std::string source = readSourceFile("src/editor/modeling/services/modeling_session_service.cpp");
    const std::string splitSource = readSourceFile("src/editor/modeling/services/modeling_session_services.cpp");
    const std::string overlayHeader = readSourceFile("src/editor/modeling/services/modeling_overlay_service.hpp");
    const std::string drawnPlaneProjection = readSourceFile("src/editor/projection/drawn_plane_overlay_projection.cpp");
    const std::string cutPreviewProjection = readSourceFile("src/editor/projection/cut_preview_overlay_projection.cpp");
    const std::string toolPreviewProjection = readSourceFile("src/editor/projection/tool_preview_mesh_projection.cpp");
    const std::string selectionHeader = readSourceFile("src/editor/modeling/services/modeling_selection_service.hpp");

    EXPECT_NE(overlayHeader.find("SelectionOverlayCache selectionOverlayCache_"), std::string::npos);
    EXPECT_NE(splitSource.find("selectionOverlayCache_.matches("), std::string::npos);
    EXPECT_NE(splitSource.find("contentRevision,\n                selectionRevision,\n                sceneRevision"), std::string::npos);
    EXPECT_NE(splitSource.find("selectionMode,\n                overlaySignature,\n                hover"), std::string::npos);
    EXPECT_NE(source.find("return overlayService_.selectionOverlaySignature(runtime_);"), std::string::npos);
    EXPECT_EQ(source.find("selectionOverlayToolSignature()"), std::string::npos);
    EXPECT_NE(splitSource.find("ModelingOverlayService::selectionOverlaySignature("), std::string::npos);
    EXPECT_NE(splitSource.find("runtime.cut_state()"), std::string::npos);
    EXPECT_NE(splitSource.find("runtime.mirror_state()"), std::string::npos);
    EXPECT_NE(splitSource.find("selectionOverlayCache_.frame()"), std::string::npos);
    EXPECT_NE(splitSource.find("SelectionOverlayProjection::projectSelectionOverlayFrame("), std::string::npos);
    EXPECT_EQ(source.find("SelectionOverlayProjection::projectSelectionOverlayFrame("), std::string::npos);
    EXPECT_NE(splitSource.find("selectionOverlayCache_.store("), std::string::npos);
    EXPECT_NE(splitSource.find("selectionOverlayBaseFrame(session)"), std::string::npos);
    EXPECT_NE(overlayHeader.find("overlayBaseCache_"), std::string::npos);
    EXPECT_NE(splitSource.find("build_selection_hover_overlay_frame"), std::string::npos);
    EXPECT_NE(splitSource.find("appendSelectedHoverFilteredBaseFrame"), std::string::npos);
    EXPECT_NE(splitSource.find("DrawnPlaneOverlayProjection::append("), std::string::npos);
    EXPECT_NE(splitSource.find("CutPreviewOverlayProjection::appendResultLines("), std::string::npos);
    EXPECT_NE(splitSource.find("ToolPreviewMeshProjection::mirrorPreviewMeshes("), std::string::npos);
    EXPECT_NE(drawnPlaneProjection.find("DrawnPlanePreviewFrame"), std::string::npos);
    EXPECT_NE(cutPreviewProjection.find("CutPreviewEdgeAccumulator"), std::string::npos);
    EXPECT_NE(toolPreviewProjection.find("ToolPreviewMeshProjection::boxPreviewMeshFromDescriptor"), std::string::npos);
    EXPECT_EQ(splitSource.find("struct DrawnPlanePreviewFrame"), std::string::npos);
    EXPECT_EQ(splitSource.find("struct CutPreviewEdgeAccumulator"), std::string::npos);
    EXPECT_EQ(splitSource.find("previewMeshFromDocuments("), std::string::npos);
    EXPECT_EQ(splitSource.find("std::optional<MeshAsset> boxPreviewMeshFromDescriptor"), std::string::npos);
    EXPECT_NE(source.find("cachedPickObjectBounds()"), std::string::npos);
    EXPECT_NE(source.find("cachedPickFaceGeometry(pickOptions)"), std::string::npos);
    EXPECT_NE(splitSource.find("ModelingSelectionService::applyPickSelection("), std::string::npos);
    EXPECT_NE(splitSource.find("quader_editor_core::apply_pick_selection("), std::string::npos);
    EXPECT_NE(source.find("selectionService_.applyPickSelection(session_, scene_, objectRegistry_, effectiveRequest)"), std::string::npos);
    EXPECT_NE(splitSource.find("cachedPickObjectBounds(session, scene, objectRegistry)"), std::string::npos);
    EXPECT_NE(splitSource.find("cachedPickFaceGeometry(session, scene, objectRegistry, pickOptions)"), std::string::npos);
    EXPECT_NE(splitSource.find("pickingCache_.faceGeometryEntries"), std::string::npos);
    EXPECT_NE(splitSource.find("PickingCacheProjection::objectFaceGeometrySignature(objects[index], pickOptions)"), std::string::npos);
    EXPECT_EQ(splitSource.find("applyPickedSelectionToSession"), std::string::npos);
    EXPECT_EQ(splitSource.find("sessionHasComponentSelection"), std::string::npos);
    EXPECT_EQ(splitSource.find("pickSelectRequestHitsTarget"), std::string::npos);
    const std::string pickingProjection = readSourceFile("src/editor/projection/picking_cache_projection.cpp");
    EXPECT_NE(pickingProjection.find("pickOptions.vertex_radius"), std::string::npos);
    EXPECT_NE(pickingProjection.find("pickOptions.edge_radius"), std::string::npos);
    EXPECT_NE(pickingProjection.find("pickOptions.has_hidden_material_slot"), std::string::npos);
    EXPECT_EQ(splitSource.find("(void)pickOptions;"), std::string::npos);
}

TEST(SceneRenderBridgeTests, ConvertsSceneOverlayColorsToLinearBeforeViewportEncode) {
    const std::string source = readSourceFile("src/render/overlay/overlay_face_fill_builder.cpp");

    EXPECT_NE(source.find("rgba8SrgbToLinear(triangle.color)"), std::string::npos);
    EXPECT_NE(source.find("linearColor"), std::string::npos);
}

TEST(SceneRenderBridgeTests, MeshRenderablesReceiveShadowsAndViewportEnablesShadowing) {
    const std::string meshFactory = readSourceFile({"src", "render", "mesh_renderable_factory.cpp"});
    const std::string meshCache = readSourceFile({"src", "render", "mesh_renderable_cache.cpp"});
    const std::string viewportRenderer = readSourceFile({"src", "render", "viewport_renderer.cpp"});
    const std::string viewportPanel = readSourceFile({"src", "ui", "viewport_panel.cpp"});

    EXPECT_NE(meshFactory.find(".castShadows(castShadows)"), std::string::npos);
    EXPECT_NE(meshFactory.find(".receiveShadows(!editorUnlit && !depthOnly)"), std::string::npos);
    EXPECT_NE(meshFactory.find("materialInstance->setColorWrite(false)"), std::string::npos);
    EXPECT_NE(meshFactory.find("materialInstance->setDepthWrite(true)"), std::string::npos);
    EXPECT_NE(meshFactory.find("materialInstance->setDepthCulling(true)"), std::string::npos);
    EXPECT_NE(meshFactory.find(".channel(channel)"), std::string::npos);
    EXPECT_NE(meshCache.find("kViewportRenderedSceneLayerMask"), std::string::npos);
    EXPECT_NE(meshCache.find("kViewportShadedSceneLayerMask"), std::string::npos);
    EXPECT_NE(meshCache.find("kViewportOverlayLayerMask"), std::string::npos);
    EXPECT_NE(meshCache.find("kViewportOverlayDepthChannel"), std::string::npos);
    EXPECT_NE(meshCache.find("renderedMeshProxies_"), std::string::npos);
    EXPECT_NE(meshCache.find("shadedMeshProxies_"), std::string::npos);
    EXPECT_NE(meshCache.find("overlayDepthMeshProxies_"), std::string::npos);
    EXPECT_NE(viewportRenderer.find("view_->setShadowingEnabled(true)"), std::string::npos);
    EXPECT_NE(viewportRenderer.find("view_->setShadowType(filament::ShadowType::PCF)"), std::string::npos);
    EXPECT_NE(viewportRenderer.find("overlayView_->setPostProcessingEnabled(false)"), std::string::npos);
    EXPECT_NE(viewportRenderer.find("overlayView_->setAntiAliasing(filament::AntiAliasing::NONE)"), std::string::npos);
    EXPECT_NE(viewportRenderer.find("overlayView_->setDithering(filament::Dithering::NONE)"), std::string::npos);
    EXPECT_NE(viewportRenderer.find("overlayView_->setColorGrading(nullptr)"), std::string::npos);
    EXPECT_NE(viewportRenderer.find("overlayView_->setShadowingEnabled(false)"), std::string::npos);
    EXPECT_NE(viewportRenderer.find("overlayView_->setBlendMode(filament::View::BlendMode::TRANSLUCENT)"), std::string::npos);
    EXPECT_NE(viewportPanel.find("paneState.renderer.setSceneLayerMask"), std::string::npos);
    EXPECT_NE(viewportPanel.find("kViewportRenderedSceneLayerMask"), std::string::npos);
    EXPECT_NE(viewportPanel.find("kViewportShadedSceneLayerMask"), std::string::npos);
    EXPECT_NE(viewportPanel.find("paneState.renderer.setShadowingEnabled(paneWorkspace.shading == ViewportShadingMode::Rendered)"), std::string::npos);
}

TEST(ViewportRendererTests, OverlayPassUsesNeutralExposureCamera) {
    const std::string header = readSourceFile({"src", "render", "viewport_renderer.hpp"});
    const std::string source = readSourceFile({"src", "render", "viewport_renderer.cpp"});

    EXPECT_NE(header.find("filament::Camera* overlayCamera_ = nullptr"), std::string::npos);
    EXPECT_NE(header.find("utils::Entity overlayCameraEntity_"), std::string::npos);
    EXPECT_NE(source.find("overlayCamera_ = engine_->createCamera(overlayCameraEntity_)"), std::string::npos);
    EXPECT_NE(source.find("overlayView_->setCamera(overlayCamera_)"), std::string::npos);
    EXPECT_EQ(source.find("overlayView_->setCamera(camera_)"), std::string::npos);
    EXPECT_NE(source.find("applyLookAt(overlayCamera_)"), std::string::npos);
    EXPECT_NE(source.find("applyProjection(overlayCamera_)"), std::string::npos);
    EXPECT_NE(source.find("camera_->setExposure(\n            std::max(settings.camera.aperture"), std::string::npos);
    EXPECT_NE(source.find("applyNeutralOverlayExposure(filament::Camera* camera)"), std::string::npos);
    EXPECT_NE(source.find("kViewportOverlayCameraAperture"), std::string::npos);
    EXPECT_NE(source.find("kViewportOverlayCameraShutterSpeed"), std::string::npos);
    EXPECT_NE(source.find("kViewportOverlayCameraSensitivity"), std::string::npos);
    EXPECT_EQ(countOccurrences(source, "overlayCamera_->setExposure("), 0u);
    EXPECT_GE(countOccurrences(source, "applyNeutralOverlayExposure(overlayCamera_)"), 3u);
}

TEST(ViewportRendererTests, PresentsSceneAndOverlayViewportTexturesThroughUiEncoder) {
    const std::string source = readSourceFile("src/render/viewport_render_target.cpp");
    const std::string rendererHeader = readSourceFile("src/render/viewport_renderer.hpp");
    const std::string rendererSource = readSourceFile("src/render/viewport_renderer.cpp");
    const std::string coordinatorSource = readSourceFile("src/render/viewport_render_pass_coordinator.cpp");
    const std::string viewportPanelSource = readSourceFile("src/ui/viewport_panel.cpp");
    const std::string gridSource = readSourceFile("src/render/viewport_grid_renderer.cpp");

    EXPECT_NE(source.find(".format(Texture::InternalFormat::RGBA8)"), std::string::npos);
    EXPECT_NE(source.find("textureRegistry_->registerTexture(creationGuard.colorTexture, sampleColorSpace_)"), std::string::npos);
    EXPECT_NE(rendererHeader.find("RenderTextureHandle overlayTextureHandle() const"), std::string::npos);
    EXPECT_NE(rendererHeader.find("ViewportRenderTarget overlayRenderTarget_"), std::string::npos);
    EXPECT_NE(rendererSource.find("renderTarget_.resize(width_, height_, view_, TextureSampleColorSpace::LinearSrgb)"), std::string::npos);
    EXPECT_NE(rendererSource.find("TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("overlayClear.clear = overlayUsesSeparateTarget"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("overlayClear.clearColor = {0.0f, 0.0f, 0.0f, 0.0f}"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("overlayVisibleLayerMask() == 0u && !overlayUsesSeparateTarget"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.overlayTextureHandle()"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ImGui::GetWindowDrawList()->AddImage("), std::string::npos);
    EXPECT_NE(gridSource.find("settings.color(ViewportColorSettingId::GridMinor)"), std::string::npos);
    EXPECT_NE(gridSource.find("settings.floatValue(ViewportFloatSettingId::GridMinorLineSize)"), std::string::npos);
    EXPECT_NE(gridSource.find("setDepthCulling(controller.state().projection != ViewportProjection::Orthographic)"), std::string::npos);
    EXPECT_EQ(source.find(".format(Texture::InternalFormat::SRGB8_A8)"), std::string::npos);
}

TEST(ViewportRendererTests, ViewportInfoOverlayUsesRendererTimingAndVisibleMeshSubmissionStats) {
    const std::string appSource = readSourceFile("src/app/app.cpp");
    const std::string uiContextHeader = readSourceFile("src/ui/ui_context.hpp");
    const std::string uiSystemHeader = readSourceFile("src/ui/ui_system.hpp");
    const std::string uiSystemSource = readSourceFile("src/ui/ui_system.cpp");
    const std::string viewportPanelHeader = readSourceFile("src/ui/viewport_panel.hpp");
    const std::string viewportPanelSource = readSourceFile("src/ui/viewport_panel.cpp");

    EXPECT_NE(appSource.find("ui_.setRenderPerformanceStats(renderSystem_.framePerformanceStats())"), std::string::npos);
    EXPECT_NE(uiContextHeader.find("RenderFramePerformanceStats renderPerformance"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("void setRenderPerformanceStats(RenderFramePerformanceStats stats)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find(".renderPerformance = renderPerformanceStats_"), std::string::npos);
    EXPECT_NE(viewportPanelHeader.find("drawViewportInfoOverlay"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("collectViewportInfoSceneStats"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("document.isEffectivelyVisible(entityId)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("normalizedMeshPrimitiveSpans(*mesh)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("stats.drawCalls += spans.size()"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("stats.primitives += span.indexCount / 3u"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ViewportBoolSettingId::ShowInfo"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ui.viewport.info.draw_calls"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("1.0 / uiContext.frameTiming.rawDeltaSeconds"), std::string::npos);
    EXPECT_EQ(viewportPanelSource.find("1000.0 / renderStats.gpuFrameMs"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("drawViewportInfoText("), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("drawList.AddText"), std::string::npos);
    const std::size_t textHelper = viewportPanelSource.find("void drawViewportInfoText(");
    const std::size_t nextHelper = viewportPanelSource.find("std::optional<int> hitDrawnPlaneCapturedPoint", textHelper);
    ASSERT_NE(textHelper, std::string::npos);
    ASSERT_NE(nextHelper, std::string::npos);
    const std::string textHelperSource = viewportPanelSource.substr(textHelper, nextHelper - textHelper);
    EXPECT_EQ(textHelperSource.find("AddRectFilled"), std::string::npos);
}

TEST(ViewportRendererTests, MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame) {
    const std::string rendererSource = readSourceFile("src/render/viewport_renderer.cpp");
    const std::string gridRendererSource = readSourceFile("src/render/viewport_grid_renderer.cpp");
    const std::string coordinatorSource = readSourceFile("src/render/viewport_render_pass_coordinator.cpp");
    const std::string appSource = readSourceFile("src/app/app.cpp");
    const std::string uiSystemSource = readSourceFile("src/ui/ui_system.cpp");
    const std::string viewportPanelSource = readSourceFile("src/ui/viewport_panel.cpp");
    const std::string overlayCacheSource = readSourceFile("src/render/overlay/overlay_renderable_cache.cpp");
    const std::string overlayProxyFactorySource = readSourceFile("src/render/overlay/overlay_proxy_factory.cpp");

    const std::size_t renderStart = rendererSource.find("void ViewportRenderer::render");
    const std::size_t renderEnd = rendererSource.find("void ViewportRenderer::updateVisibleLayers");
    ASSERT_NE(renderStart, std::string::npos);
    ASSERT_NE(renderEnd, std::string::npos);
    const std::string renderFunction = rendererSource.substr(renderStart, renderEnd - renderStart);
    EXPECT_EQ(renderFunction.find("scene_->addEntity"), std::string::npos);
    EXPECT_EQ(renderFunction.find("scene_->remove"), std::string::npos);
    EXPECT_NE(rendererSource.find("gridRenderer_.update(controller, width(), height(), gridWorldSpacing, settings, visible)"), std::string::npos);
    EXPECT_NE(gridRendererSource.find(".layerMask(kViewportAllLayerMask, gridLayerMask_)"), std::string::npos);
    EXPECT_NE(rendererSource.find("renderPassCoordinator_.applyVisibleLayers(view_, overlayView_)"), std::string::npos);
    EXPECT_EQ(rendererSource.find("setChannelDepthClearEnabled("), std::string::npos);
    EXPECT_NE(rendererSource.find("renderPassCoordinator_.render(\n            renderer,\n            view_,\n            overlayView_,\n            surface_ == ViewportRenderSurface::Texture)"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("sceneView->setVisibleLayers(kViewportAllLayerMask, sceneVisibleLayerMask())"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("overlayView->setVisibleLayers(kViewportAllLayerMask, overlayVisibleLayerMask())"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("layerMask | kViewportOverlayLayerMask"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("clear.discard = false"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("overlayClear.clear = overlayUsesSeparateTarget"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("renderer.setClearOptions(clear);"), std::string::npos);

    const std::size_t prepareViewports =
            appSource.find("ui_.prepareViewports(*editor_, renderBridge_, projectSession_.scene(), projectSession_.assets())");
    const std::size_t uiBeginFrame =
            appSource.find("ui_.beginFrame(frameTiming)");
    const std::size_t prepareRenderData =
            appSource.find("ui_.prepareRenderData(window_.widthPixels(), window_.heightPixels())");
    const std::size_t beginFrame = appSource.find("renderSystem_.beginFrame()");
    ASSERT_NE(prepareViewports, std::string::npos);
    ASSERT_NE(uiBeginFrame, std::string::npos);
    ASSERT_NE(prepareRenderData, std::string::npos);
    ASSERT_NE(beginFrame, std::string::npos);
    EXPECT_LT(uiBeginFrame, prepareViewports);
    EXPECT_LT(prepareViewports, prepareRenderData);
    EXPECT_LT(uiBeginFrame, beginFrame);
    EXPECT_LT(prepareViewports, beginFrame);
    EXPECT_LT(prepareRenderData, beginFrame);

    EXPECT_NE(uiSystemSource.find("void UISystem::beginFrame("), std::string::npos);
    EXPECT_NE(uiSystemSource.find("void UISystem::prepareViewports("), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportPanel_->prepareViewports("), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ViewportWorkspaceService& workspace"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("const SceneDocument& document"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("const AssetRegistry& assets"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.updateCamera(paneWorkspace.camera)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.setSceneLayerMask"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.setShadowingEnabled"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.setSceneVisible"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ViewportBoolSettingId::ShowGrid"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ViewportBoolSettingId::ShowGizmos"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ViewportBoolSettingId::ShowOverlays"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ViewportBoolSettingId::ShowInfo"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.updateGroundGrid(paneWorkspace.camera, gridWorldSize, settings, gridVisible)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.setOverlayVisible(overlaysVisible ||"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("renderBridge.setOverlayFrame("), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("viewportGridLayerMask(viewportPaneIndex(paneId))"), std::string::npos);
    EXPECT_NE(overlayProxyFactorySource.find(".layerMask(kViewportAllLayerMask, layerMask)"), std::string::npos);
    EXPECT_NE(overlayCacheSource.find("void OverlayRenderableCache::rebuildSlot(OverlaySlot& slot, bool rebuildCameraIndependentLayers)"), std::string::npos);
}

TEST(RenderLifecycleTests, ResourceOwnersShutdownBeforeInitialize) {
    for (const char* relativePath : {
                 "src/render/scene_render_bridge.cpp",
                 "src/render/texture_binding_service.cpp",
                 "src/render/viewport_grid_renderer.cpp",
         }) {
        const std::string source = readSourceFile(relativePath);
        const std::size_t initialize = source.find("::initialize");
        ASSERT_NE(initialize, std::string::npos) << relativePath;
        const std::size_t shutdown = source.find("shutdown();", initialize);
        ASSERT_NE(shutdown, std::string::npos) << relativePath;
        EXPECT_LT(shutdown, source.find("engine_ =", initialize)) << relativePath;
    }
}

TEST(TextureRegistryTests, TracksPerTexturePresentationColorSpace) {
    const std::string headerSource = readSourceFile("src/render/texture_registry.hpp");

    EXPECT_NE(headerSource.find("enum class TextureSampleColorSpace"), std::string::npos);
    EXPECT_NE(headerSource.find("DisplaySrgb"), std::string::npos);
    EXPECT_NE(headerSource.find("DisplaySrgbPremultipliedAlpha"), std::string::npos);
    EXPECT_NE(headerSource.find("LinearSrgb"), std::string::npos);
    EXPECT_NE(headerSource.find("using RenderTextureHandle = std::uintptr_t"), std::string::npos);
    EXPECT_NE(headerSource.find("TextureSampleColorSpace sampleColorSpace(RenderTextureHandle id) const"), std::string::npos);
    EXPECT_NE(headerSource.find("void updateTexture(RenderTextureHandle id"), std::string::npos);

    const std::string source = readSourceFile("src/render/texture_registry.cpp");

    EXPECT_NE(source.find(".sampleColorSpace = sampleColorSpace"), std::string::npos);
    EXPECT_NE(source.find("TextureSampleColorSpace::DisplaySrgb : it->second.sampleColorSpace"), std::string::npos);
}

TEST(AppStartupTests, DoesNotCreateDefaultModelingMesh) {
    const std::string source = readSourceFile("src/app/app.cpp");

    EXPECT_EQ(source.find("createBoxFromBounds({"), std::string::npos);
    EXPECT_EQ(source.find("Startup scene created modeling box"), std::string::npos);
    EXPECT_EQ(source.find("startup box"), std::string::npos);
    EXPECT_EQ(source.find("Fake Directional Light"), std::string::npos);
    EXPECT_EQ(source.find("document_.setLight"), std::string::npos);
}

TEST(AppLoopTests, SuspendsGpuSubmissionForNonDrawableWindowsAndIdlesBetweenFrames) {
    const std::string appHeader = readSourceFile("src/app/app.hpp");
    const std::string appSource = readSourceFile("src/app/app.cpp");
    const std::string platformHeader = readSourceFile("src/platform/platform_window.hpp");
    const std::string platformSource = readSourceFile("src/platform/platform_window.cpp");
    const std::string uiHeader = readSourceFile("src/ui/ui_system.hpp");
    const std::string uiSource = readSourceFile("src/ui/ui_system.cpp");
    const std::string viewportPanelHeader = readSourceFile("src/ui/viewport_panel.hpp");

    EXPECT_NE(platformHeader.find("platformWindowCanRenderFrames"), std::string::npos);
    EXPECT_NE(platformHeader.find("[[nodiscard]] bool canRenderFrames() const"), std::string::npos);
    EXPECT_NE(platformSource.find("SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN | SDL_WINDOW_OCCLUDED"), std::string::npos);
    EXPECT_NE(platformSource.find("pixelWidth > 0 && pixelHeight > 0"), std::string::npos);
    EXPECT_NE(appHeader.find("waitUntilNextFrame()"), std::string::npos);
    EXPECT_NE(appSource.find("if (window_.canRenderFrames()) {\n                renderFrame(frameTiming);"), std::string::npos);
    EXPECT_NE(appSource.find("if (!window_.canRenderFrames()) {\n        renderingFrame_ = false;"), std::string::npos);
    EXPECT_NE(appSource.find("ui_.endFrameWithoutRender();"), std::string::npos);
    EXPECT_NE(appSource.find("SDL_WaitEventTimeout(nullptr, waitMs)"), std::string::npos);
    EXPECT_NE(appSource.find("kIdleFrameWaitMs = 100"), std::string::npos);
    EXPECT_NE(appSource.find("kSuspendedWindowWaitMs = 250"), std::string::npos);
    EXPECT_NE(appSource.find("!window_.canRenderFrames())"), std::string::npos);
    EXPECT_NE(uiHeader.find("endFrameWithoutRender()"), std::string::npos);
    EXPECT_NE(uiHeader.find("[[nodiscard]] bool needsContinuousRendering() const"), std::string::npos);
    EXPECT_NE(uiSource.find("relativeMouseCaptured_ ||"), std::string::npos);
    EXPECT_NE(viewportPanelHeader.find("lastFlyNavigationActive_"), std::string::npos);
    EXPECT_NE(viewportPanelHeader.find("activeViewportSettings().boolValue(ViewportBoolSettingId::ShowInfo)"), std::string::npos);
    EXPECT_NE(viewportPanelHeader.find("navigationGizmo_.inputLocked()"), std::string::npos);
    EXPECT_NE(viewportPanelHeader.find("interactionController_.dragging()"), std::string::npos);
}

TEST(SceneDocumentTests, CreatesStableEntitiesAndComponents) {
    SceneDocument scene;
    const EntityId sphere = scene.createEntity("Sphere");
    const EntityId light = scene.createEntity("Light");

    scene.setMesh(sphere, MeshComponent{.mesh = 11});
    scene.setModelingObject(sphere, ModelingObjectComponent{.objectId = 42, .materialSlot = 3});
    scene.setSelected(sphere, true);
    scene.renameEntity(sphere, "Box");
    scene.setVisible(light, false);
    scene.setLight(light, LightComponent{});

    EXPECT_NE(sphere, light);
    EXPECT_FALSE(scene.uuid(sphere).empty());
    EXPECT_FALSE(scene.uuid(light).empty());
    EXPECT_NE(scene.uuid(sphere), scene.uuid(light));
    EXPECT_EQ(scene.entityForUuid(scene.uuid(sphere)), sphere);
    EXPECT_EQ(scene.entityForUuid(scene.uuid(light)), light);
    EXPECT_TRUE(scene.exists(sphere));
    EXPECT_TRUE(scene.enttEntity(sphere) != entt::null);
    EXPECT_EQ(scene.entityId(scene.enttEntity(sphere)), sphere);
    ASSERT_NE(scene.name(sphere), nullptr);
    EXPECT_EQ(scene.name(sphere)->name, "Box");
    ASSERT_NE(scene.mesh(sphere), nullptr);
    EXPECT_EQ(scene.mesh(sphere)->mesh, 11u);
    EXPECT_FALSE(scene.mesh(sphere)->doubleSided);
    ASSERT_NE(scene.modelingObject(sphere), nullptr);
    EXPECT_EQ(scene.modelingObject(sphere)->objectId, 42);
    EXPECT_EQ(scene.modelingObject(sphere)->materialSlot, 3u);
    ASSERT_NE(scene.light(light), nullptr);
    ASSERT_NE(scene.visibility(light), nullptr);
    EXPECT_FALSE(scene.visibility(light)->visible);
    EXPECT_EQ(scene.primarySelection(), sphere);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), sphere);

    const std::vector<EntityRecord> records = scene.entityRecords();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].id, sphere);
    EXPECT_EQ(records[0].uuid, scene.uuid(sphere));
    EXPECT_EQ(records[0].name, "Box");
    EXPECT_TRUE(records[0].selected);
    EXPECT_TRUE(records[0].hasMesh);
    EXPECT_TRUE(records[0].hasModelingObject);
    EXPECT_EQ(records[1].id, light);
    EXPECT_FALSE(records[1].visible);
    EXPECT_TRUE(records[1].hasLight);

    scene.clearSelection();
    EXPECT_TRUE(scene.selectedEntities().empty());
    EXPECT_EQ(scene.primarySelection(), InvalidEntityId);
    EXPECT_GT(scene.revision(), 0u);
}

TEST(SceneDocumentTests, ComponentAccessorsAreReadOnlyOutsideDocumentMutators) {
    static_assert(std::is_same_v<decltype(std::declval<SceneDocument&>().name(EntityId{})), const NameComponent*>);
    static_assert(std::is_same_v<decltype(std::declval<SceneDocument&>().transform(EntityId{})), const TransformComponent*>);
    static_assert(std::is_same_v<decltype(std::declval<SceneDocument&>().visibility(EntityId{})), const VisibilityComponent*>);
    static_assert(std::is_same_v<decltype(std::declval<SceneDocument&>().selection(EntityId{})), const SelectionComponent*>);
    static_assert(std::is_same_v<decltype(std::declval<SceneDocument&>().mesh(EntityId{})), const MeshComponent*>);
    static_assert(std::is_same_v<decltype(std::declval<SceneDocument&>().material(EntityId{})), const MaterialComponent*>);
    static_assert(std::is_same_v<decltype(std::declval<SceneDocument&>().light(EntityId{})), const LightComponent*>);

    const std::string header = readSourceFile("src/scene/scene_document.hpp");
    EXPECT_EQ(header.find("TransformComponent* transform(EntityId id);"), std::string::npos);
    EXPECT_EQ(header.find("VisibilityComponent* visibility(EntityId id);"), std::string::npos);
    EXPECT_EQ(header.find("MeshComponent* mesh(EntityId id);"), std::string::npos);
    EXPECT_NE(header.find("TransformComponent* mutableTransform(EntityId id);"), std::string::npos);
    EXPECT_NE(header.find("VisibilityComponent* mutableVisibility(EntityId id);"), std::string::npos);

    SceneDocument scene;
    const EntityId entity = scene.createEntity("Box");
    const std::uint64_t afterCreate = scene.revision();
    ASSERT_NE(scene.transform(entity), nullptr);

    TransformComponent moved = *scene.transform(entity);
    moved.position.x = 5.0f;
    scene.setTransform(entity, moved);

    EXPECT_GT(scene.revision(), afterCreate);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 5.0f);
}

TEST(SceneEntityNamingTests, MeshNamesStartAtBaseNameThenUseOneBasedSuffixes) {
    SceneDocument scene;

    EXPECT_EQ(SceneEntityNaming::nextNameForKind(scene, SceneEntityKind::Mesh), "Mesh");

    const EntityId first = scene.createEntity("Mesh");
    scene.setMesh(first, MeshComponent{.mesh = 1});
    EXPECT_EQ(SceneEntityNaming::nextNameForKind(scene, SceneEntityKind::Mesh), "Mesh_1");

    const EntityId second = scene.createEntity("Mesh_1");
    scene.setMesh(second, MeshComponent{.mesh = 2});
    EXPECT_EQ(SceneEntityNaming::nextNameForKind(scene, SceneEntityKind::Mesh), "Mesh_2");
    EXPECT_NE(SceneEntityNaming::nextNameForKind(scene, SceneEntityKind::Mesh), "Mesh_0");
}

TEST(SceneEntityNamingTests, BatchAllocatorUsesDocumentNamesAndReservesGeneratedNames) {
    SceneDocument scene;
    const EntityId first = scene.createEntity("Mesh");
    scene.setMesh(first, MeshComponent{.mesh = 1});
    const EntityId second = scene.createEntity("Mesh_1");
    scene.setMesh(second, MeshComponent{.mesh = 2});

    SceneEntityNameAllocator names(scene);

    EXPECT_EQ(names.nextNameForKind(SceneEntityKind::Mesh), "Mesh_2");
    EXPECT_EQ(names.nextNameForKind(SceneEntityKind::Mesh), "Mesh_3");

    names.reserveName("Mesh_4");
    EXPECT_EQ(names.nextNameForKind(SceneEntityKind::Mesh), "Mesh_5");
}

TEST(SceneDocumentTests, TracksMultipleSelectedEntitiesInStableOrder) {
    SceneDocument scene;
    const EntityId first = scene.createEntity("First");
    const EntityId second = scene.createEntity("Second");
    const EntityId third = scene.createEntity("Third");

    scene.setSelected(third, true);
    EXPECT_EQ(scene.primarySelection(), third);
    scene.setSelected(first, true);

    const std::vector<EntityId> selected = scene.selectedEntities();
    ASSERT_EQ(selected.size(), 2u);
    EXPECT_EQ(selected[0], first);
    EXPECT_EQ(selected[1], third);
    EXPECT_EQ(scene.primarySelection(), first);

    const std::vector<EntityRecord> records = scene.entityRecords();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_TRUE(records[0].selected);
    EXPECT_TRUE(records[0].active);
    EXPECT_FALSE(records[1].selected);
    EXPECT_FALSE(records[1].active);
    EXPECT_TRUE(records[2].selected);
    EXPECT_FALSE(records[2].active);

    EXPECT_FALSE(scene.setActiveSelection(second));
    EXPECT_TRUE(scene.setActiveSelection(third));
    EXPECT_EQ(scene.primarySelection(), third);

    scene.setSelected(third, false);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.primarySelection(), first);

    scene.setSelected(first, false);
    EXPECT_TRUE(scene.selectedEntities().empty());
    EXPECT_EQ(scene.primarySelection(), InvalidEntityId);
}

TEST(SceneDocumentTests, PromotesPrimarySelectionWhenActiveEntityIsDestroyed) {
    SceneDocument scene;
    const EntityId first = scene.createEntity("First");
    const EntityId second = scene.createEntity("Second");

    scene.setSelected(first, true);
    scene.setSelected(second, true);
    EXPECT_EQ(scene.primarySelection(), second);

    EXPECT_TRUE(scene.destroyEntity(second));
    EXPECT_EQ(scene.primarySelection(), first);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), first);
}

TEST(SceneDocumentTests, TracksHierarchyTreeOrderAndInheritedState) {
    SceneDocument scene;
    const EntityId group = scene.createEmpty("Empty");
    const EntityId child = scene.createEntity("Mesh");
    const EntityId grandchild = scene.createEntity("Nested");
    const EntityId sibling = scene.createEntity("Sibling");

    scene.setTransform(group, TransformComponent{.position = {10.0f, 0.0f, 0.0f}});
    scene.setTransform(child, TransformComponent{.position = {2.0f, 0.0f, 0.0f}});
    scene.setTransform(grandchild, TransformComponent{.position = {0.0f, 3.0f, 0.0f}});
    ASSERT_TRUE(scene.setParent(child, group, false));
    ASSERT_TRUE(scene.setParent(grandchild, child, false));

    EXPECT_EQ(scene.parent(child), group);
    EXPECT_TRUE(scene.isDescendantOf(grandchild, group));
    EXPECT_FALSE(scene.setParent(group, grandchild));

    const std::vector<EntityId> treeOrder = scene.entityTreeOrder();
    ASSERT_EQ(treeOrder.size(), 4u);
    EXPECT_EQ(treeOrder[0], group);
    EXPECT_EQ(treeOrder[1], child);
    EXPECT_EQ(treeOrder[2], grandchild);
    EXPECT_EQ(treeOrder[3], sibling);

    const std::vector<EntityRecord> records = scene.entityRecordsTreeOrder();
    ASSERT_EQ(records.size(), 4u);
    EXPECT_EQ(records[0].kind, EntityKind::Empty);
    EXPECT_TRUE(records[0].hasChildren);
    EXPECT_EQ(records[1].parent, group);
    EXPECT_EQ(records[1].depth, 1);
    EXPECT_EQ(records[2].depth, 2);

    const QVec3 worldPoint = transformPoint(scene.worldTransformMatrix(grandchild), {});
    EXPECT_FLOAT_EQ(worldPoint.x, 12.0f);
    EXPECT_FLOAT_EQ(worldPoint.y, 3.0f);
    EXPECT_FLOAT_EQ(worldPoint.z, 0.0f);

    ASSERT_TRUE(scene.setVisible(group, false));
    EXPECT_TRUE(scene.visibility(child)->visible);
    EXPECT_FALSE(scene.isEffectivelyVisible(child));
    ASSERT_TRUE(scene.setLocked(group, true));
    EXPECT_TRUE(scene.isEffectivelyLocked(grandchild));
}

TEST(SceneDocumentTests, ReparentCanPreserveWorldTransformAndDestroyChildren) {
    SceneDocument scene;
    const EntityId group = scene.createEmpty("Group");
    const EntityId child = scene.createEntity("Child");
    const EntityId leaf = scene.createEntity("Leaf");

    scene.setTransform(group, TransformComponent{.position = {10.0f, 0.0f, 0.0f}});
    scene.setTransform(child, TransformComponent{.position = {2.0f, 0.0f, 0.0f}});
    scene.setTransform(leaf, TransformComponent{.position = {1.0f, 0.0f, 0.0f}});
    ASSERT_TRUE(scene.setParent(child, group, false));

    const QVec3 before = transformPoint(scene.worldTransformMatrix(child), {});
    ASSERT_TRUE(scene.setParent(child, InvalidEntityId, true));
    const QVec3 after = transformPoint(scene.worldTransformMatrix(child), {});
    EXPECT_FLOAT_EQ(before.x, after.x);
    EXPECT_FLOAT_EQ(before.y, after.y);
    ASSERT_NE(scene.transform(child), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 12.0f);

    ASSERT_TRUE(scene.setParent(leaf, group, false));
    scene.setSelected(leaf, true);
    EXPECT_TRUE(scene.destroyEntity(group));
    EXPECT_FALSE(scene.exists(group));
    EXPECT_FALSE(scene.exists(leaf));
    EXPECT_TRUE(scene.exists(child));
    EXPECT_TRUE(scene.selectedEntities().empty());
}

TEST(SceneBoundsTests, ComputesSelectedWorldBoundsFromSpatialTransforms) {
    AssetRegistry assets;
    MeshAsset cube;
    cube.vertices = {
            {.position = {-1.0f, -1.0f, -1.0f}},
            {.position = {1.0f, 1.0f, 1.0f}},
    };
    const AssetId cubeMesh = assets.addMesh(std::move(cube));

    SceneDocument scene;
    const EntityId meshEntity = scene.createEntity("Scaled Mesh");
    scene.setMesh(meshEntity, MeshComponent{.mesh = cubeMesh});
    scene.setTransform(meshEntity, TransformComponent{
            .position = {10.0f, 0.0f, 0.0f},
            .scale = {2.0f, 3.0f, 4.0f},
    });
    scene.setSelected(meshEntity, true);

    const EntityId pointEntity = scene.createEntity("Light");
    scene.setTransform(pointEntity, TransformComponent{.position = {-4.0f, 5.0f, 2.0f}});
    scene.setSelected(pointEntity, true);

    const EntityId hiddenEntity = scene.createEntity("Hidden");
    scene.setTransform(hiddenEntity, TransformComponent{.position = {-100.0f, -100.0f, -100.0f}});
    scene.setVisible(hiddenEntity, false);
    scene.setSelected(hiddenEntity, true);

    const std::optional<SceneBounds> bounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->min.x, -4.0f, 0.001f);
    EXPECT_NEAR(bounds->min.y, -3.0f, 0.001f);
    EXPECT_NEAR(bounds->min.z, -4.0f, 0.001f);
    EXPECT_NEAR(bounds->max.x, 12.0f, 0.001f);
    EXPECT_NEAR(bounds->max.y, 5.0f, 0.001f);
    EXPECT_NEAR(bounds->max.z, 4.0f, 0.001f);

    const QVec3 center = boundsCenter(*bounds);
    EXPECT_NEAR(center.x, 4.0f, 0.001f);
    EXPECT_NEAR(center.y, 1.0f, 0.001f);
    EXPECT_NEAR(center.z, 0.0f, 0.001f);
}

TEST(SceneBoundsTests, CentersBottomOriginMeshOnItsSelectedWorldBounds) {
    AssetRegistry assets;
    MeshAsset bottomOriginBox;
    bottomOriginBox.vertices = {
            {.position = {-1.0f, 0.0f, -1.0f}},
            {.position = {1.0f, 2.0f, 1.0f}},
    };
    const AssetId boxMesh = assets.addMesh(std::move(bottomOriginBox));

    SceneDocument scene;
    const EntityId entity = scene.createEntity("Bottom Origin Box");
    scene.setMesh(entity, MeshComponent{.mesh = boxMesh});
    scene.setTransform(entity, TransformComponent{.position = {0.0f, 0.0f, 0.0f}});
    scene.setSelected(entity, true);

    const std::optional<SceneBounds> bounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(bounds.has_value());
    const QVec3 center = boundsCenter(*bounds);
    EXPECT_NEAR(center.x, 0.0f, 0.001f);
    EXPECT_NEAR(center.y, 1.0f, 0.001f);
    EXPECT_NEAR(center.z, 0.0f, 0.001f);
}

TEST(TransformPivotTests, KeepsNonOriginVisualPivotFixedDuringRotation) {
    TransformComponent transform;
    const QVec3 worldPivot{0.0f, 1.0f, 0.0f};
    const QVec3 localPivot = localPivotForWorldPoint(transform, worldPivot);

    TransformComponent rotated = transform;
    rotated.rotationEuler.z = 1.57079632679f;
    pinLocalPivotToWorldPoint(rotated, localPivot, worldPivot);

    const QVec3 pinnedWorldPivot = transformPoint(composeTransform(rotated.spatialTransform()), localPivot);
    EXPECT_NEAR(pinnedWorldPivot.x, worldPivot.x, 0.001f);
    EXPECT_NEAR(pinnedWorldPivot.y, worldPivot.y, 0.001f);
    EXPECT_NEAR(pinnedWorldPivot.z, worldPivot.z, 0.001f);
    EXPECT_NEAR(rotated.position.x, 1.0f, 0.001f);
    EXPECT_NEAR(rotated.position.y, 1.0f, 0.001f);
}

TEST(TransformPivotTests, KeepsNonOriginVisualPivotFixedDuringScale) {
    TransformComponent transform;
    const QVec3 worldPivot{0.0f, 1.0f, 0.0f};
    const QVec3 localPivot = localPivotForWorldPoint(transform, worldPivot);

    TransformComponent scaled = transform;
    scaled.scale = {2.0f, 3.0f, 4.0f};
    pinLocalPivotToWorldPoint(scaled, localPivot, worldPivot);

    const QVec3 pinnedWorldPivot = transformPoint(composeTransform(scaled.spatialTransform()), localPivot);
    EXPECT_NEAR(pinnedWorldPivot.x, worldPivot.x, 0.001f);
    EXPECT_NEAR(pinnedWorldPivot.y, worldPivot.y, 0.001f);
    EXPECT_NEAR(pinnedWorldPivot.z, worldPivot.z, 0.001f);
    EXPECT_NEAR(scaled.position.x, 0.0f, 0.001f);
    EXPECT_NEAR(scaled.position.y, -2.0f, 0.001f);
}

TEST(TransformPivotTests, AppliesRotationDeltaInGlobalSpace) {
    constexpr float HalfPi = 1.57079632679f;
    TransformComponent transform;
    transform.rotationEuler = {0.0f, 0.0f, HalfPi};

    TransformComponent rotated = transform;
    applyWorldRotationDeltaAroundPivot(rotated, {HalfPi, 0.0f, 0.0f}, {}, {});

    const glm::mat4 current = glm::mat4_cast(toGlm(quatFromEulerRadians(transform.rotationEuler)));
    const glm::mat4 delta = glm::mat4_cast(toGlm(quatFromEulerRadians({HalfPi, 0.0f, 0.0f})));
    const QMat4 expectedWorldRotation = fromGlm(delta * current);
    const QMat4 actualWorldRotation = composeTransform(rotated.spatialTransform());

    const std::array<QVec3, 3> basis = {{
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
    }};
    for (const QVec3 axis : basis) {
        const QVec3 expected = transformVector(expectedWorldRotation, axis);
        const QVec3 actual = transformVector(actualWorldRotation, axis);
        EXPECT_NEAR(actual.x, expected.x, 0.001f);
        EXPECT_NEAR(actual.y, expected.y, 0.001f);
        EXPECT_NEAR(actual.z, expected.z, 0.001f);
    }

    TransformComponent locallyAdded = transform;
    locallyAdded.rotationEuler = locallyAdded.rotationEuler + QVec3{HalfPi, 0.0f, 0.0f};
    const QVec3 worldSpaceY = transformVector(actualWorldRotation, {0.0f, 1.0f, 0.0f});
    const QVec3 localSpaceY = transformVector(composeTransform(locallyAdded.spatialTransform()), {0.0f, 1.0f, 0.0f});
    EXPECT_GT(length(worldSpaceY - localSpaceY), 0.1f);
}

TEST(TransformPivotTests, KeepsEquivalentEulerAnglesNearReferenceValues) {
    constexpr float Pi = 3.14159265358979323846f;
    const QVec3 reference{};
    const QVec3 decomposedEquivalent{Pi, Pi / 3.0f, Pi};

    const QVec3 stable = equivalentEulerRadiansClosestTo(decomposedEquivalent, reference);
    EXPECT_NEAR(stable.x, 0.0f, 0.001f);
    EXPECT_NEAR(stable.y, 2.0f * Pi / 3.0f, 0.001f);
    EXPECT_NEAR(stable.z, 0.0f, 0.001f);
}

TEST(TransformPivotTests, NormalizesEulerDisplayAnglesToPositiveTurn) {
    constexpr float Pi = 3.14159265358979323846f;
    const QVec3 normalized = positiveEulerRadians({0.0f, -Pi / 12.0f, Pi * 2.0f});

    EXPECT_NEAR(normalized.x, 0.0f, 0.001f);
    EXPECT_NEAR(normalized.y, Pi * 23.0f / 12.0f, 0.001f);
    EXPECT_NEAR(normalized.z, 0.0f, 0.001f);
}

TEST(MathTypesTests, ComposeTransformRoundTripsPoints) {
    const QTransform transform{
            .position = {2.0f, 3.0f, 4.0f},
            .rotation = {},
            .scale = {2.0f, 3.0f, 4.0f},
    };

    const QMat4 localToWorld = composeTransform(transform);
    const QMat4 worldToLocal = inverse(localToWorld);

    const QVec3 worldPoint = transformPoint(localToWorld, {1.0f, 1.0f, 1.0f});
    EXPECT_NEAR(worldPoint.x, 4.0f, 0.001f);
    EXPECT_NEAR(worldPoint.y, 6.0f, 0.001f);
    EXPECT_NEAR(worldPoint.z, 8.0f, 0.001f);

    const QVec3 localPoint = transformPoint(worldToLocal, worldPoint);
    EXPECT_NEAR(localPoint.x, 1.0f, 0.001f);
    EXPECT_NEAR(localPoint.y, 1.0f, 0.001f);
    EXPECT_NEAR(localPoint.z, 1.0f, 0.001f);
}

namespace {

void expectViewportPlacement(
        const ViewportPanePlacement& placement,
        ViewportPaneId paneId,
        float x,
        float y,
        float width,
        float height) {
    EXPECT_EQ(placement.paneId, paneId);
    EXPECT_NEAR(placement.normalizedRect.x, x, 0.001f);
    EXPECT_NEAR(placement.normalizedRect.y, y, 0.001f);
    EXPECT_NEAR(placement.normalizedRect.width, width, 0.001f);
    EXPECT_NEAR(placement.normalizedRect.height, height, 0.001f);
}

void expectViewportSplitHandle(
        const ViewportSplitHandle& handle,
        ViewportSplitHandleId id,
        ViewportSplitHandleAxis axis,
        float position,
        float start,
        float end) {
    EXPECT_EQ(handle.id, id);
    EXPECT_EQ(handle.axis, axis);
    EXPECT_NEAR(handle.position, position, 0.001f);
    EXPECT_NEAR(handle.start, start, 0.001f);
    EXPECT_NEAR(handle.end, end, 0.001f);
}

ViewportLayoutState makeDistinctViewportLayoutState() {
    ViewportLayoutState state;
    state.setHandlePosition(ViewportSplitHandleId::TwoHorizontal, 0.25f);
    state.setHandlePosition(ViewportSplitHandleId::TwoVertical, 0.35f);
    state.setHandlePosition(ViewportSplitHandleId::ThreePrimary, 0.60f);
    state.setHandlePosition(ViewportSplitHandleId::ThreeSecondary, 0.40f);
    state.setHandlePosition(ViewportSplitHandleId::FourVertical, 0.30f);
    state.setHandlePosition(ViewportSplitHandleId::FourHorizontal, 0.70f);
    return state;
}

} // namespace

TEST(ViewportLayoutTests, OptionLabelsMatchViewportLayoutModel) {
    const auto& options = viewportLayoutOptions();

    ASSERT_EQ(options.size(), 8u);
    EXPECT_EQ(options[0].id, ViewportLayoutId::Single);
    EXPECT_EQ(options[0].label, "Single View");
    EXPECT_EQ(options[1].id, ViewportLayoutId::TwoHorizontal);
    EXPECT_EQ(options[1].label, "Two-View Horizontal");
    EXPECT_EQ(options[2].id, ViewportLayoutId::TwoVertical);
    EXPECT_EQ(options[2].label, "Two-View Vertical");
    EXPECT_EQ(options[3].id, ViewportLayoutId::ThreePrimaryRight);
    EXPECT_EQ(options[3].label, "Three-View Primary Right");
    EXPECT_EQ(options[4].id, ViewportLayoutId::ThreePrimaryLeft);
    EXPECT_EQ(options[4].label, "Three-View Primary Left");
    EXPECT_EQ(options[5].id, ViewportLayoutId::ThreePrimaryBottom);
    EXPECT_EQ(options[5].label, "Three-View Primary Bottom");
    EXPECT_EQ(options[6].id, ViewportLayoutId::ThreePrimaryTop);
    EXPECT_EQ(options[6].label, "Three-View Primary Top");
    EXPECT_EQ(options[7].id, ViewportLayoutId::FourQuad);
    EXPECT_EQ(options[7].label, "Four-View Quad");

    EXPECT_EQ(viewportLayoutLabel(ViewportLayoutId::ThreePrimaryTop), "Three-View Primary Top");
}

TEST(ViewportLayoutTests, BuildPlacementsMatchNormalizedRects) {
    const ViewportLayoutState state = makeDistinctViewportLayoutState();

    std::vector<ViewportPanePlacement> placements = viewportLayoutPlacements(ViewportLayoutId::Single, state);
    ASSERT_EQ(placements.size(), 1u);
    expectViewportPlacement(placements[0], ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, 1.0f);

    placements = viewportLayoutPlacements(ViewportLayoutId::TwoHorizontal, state);
    ASSERT_EQ(placements.size(), 2u);
    expectViewportPlacement(placements[0], ViewportPaneId::Perspective, 0.0f, 0.0f, 0.25f, 1.0f);
    expectViewportPlacement(placements[1], ViewportPaneId::Top, 0.25f, 0.0f, 0.75f, 1.0f);

    placements = viewportLayoutPlacements(ViewportLayoutId::TwoVertical, state);
    ASSERT_EQ(placements.size(), 2u);
    expectViewportPlacement(placements[0], ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, 0.35f);
    expectViewportPlacement(placements[1], ViewportPaneId::Top, 0.0f, 0.35f, 1.0f, 0.65f);

    placements = viewportLayoutPlacements(ViewportLayoutId::ThreePrimaryRight, state);
    ASSERT_EQ(placements.size(), 3u);
    expectViewportPlacement(placements[0], ViewportPaneId::Top, 0.0f, 0.0f, 0.60f, 0.40f);
    expectViewportPlacement(placements[1], ViewportPaneId::Side, 0.0f, 0.40f, 0.60f, 0.60f);
    expectViewportPlacement(placements[2], ViewportPaneId::Perspective, 0.60f, 0.0f, 0.40f, 1.0f);

    placements = viewportLayoutPlacements(ViewportLayoutId::ThreePrimaryLeft, state);
    ASSERT_EQ(placements.size(), 3u);
    expectViewportPlacement(placements[0], ViewportPaneId::Perspective, 0.0f, 0.0f, 0.60f, 1.0f);
    expectViewportPlacement(placements[1], ViewportPaneId::Top, 0.60f, 0.0f, 0.40f, 0.40f);
    expectViewportPlacement(placements[2], ViewportPaneId::Side, 0.60f, 0.40f, 0.40f, 0.60f);

    placements = viewportLayoutPlacements(ViewportLayoutId::ThreePrimaryBottom, state);
    ASSERT_EQ(placements.size(), 3u);
    expectViewportPlacement(placements[0], ViewportPaneId::Top, 0.0f, 0.0f, 0.40f, 0.60f);
    expectViewportPlacement(placements[1], ViewportPaneId::Side, 0.40f, 0.0f, 0.60f, 0.60f);
    expectViewportPlacement(placements[2], ViewportPaneId::Perspective, 0.0f, 0.60f, 1.0f, 0.40f);

    placements = viewportLayoutPlacements(ViewportLayoutId::ThreePrimaryTop, state);
    ASSERT_EQ(placements.size(), 3u);
    expectViewportPlacement(placements[0], ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, 0.60f);
    expectViewportPlacement(placements[1], ViewportPaneId::Top, 0.0f, 0.60f, 0.40f, 0.40f);
    expectViewportPlacement(placements[2], ViewportPaneId::Side, 0.40f, 0.60f, 0.60f, 0.40f);

    placements = viewportLayoutPlacements(ViewportLayoutId::FourQuad, state);
    ASSERT_EQ(placements.size(), 4u);
    expectViewportPlacement(placements[0], ViewportPaneId::Perspective, 0.0f, 0.0f, 0.30f, 0.70f);
    expectViewportPlacement(placements[1], ViewportPaneId::Top, 0.30f, 0.0f, 0.70f, 0.70f);
    expectViewportPlacement(placements[2], ViewportPaneId::Front, 0.0f, 0.70f, 0.30f, 0.30f);
    expectViewportPlacement(placements[3], ViewportPaneId::Side, 0.30f, 0.70f, 0.70f, 0.30f);
}

TEST(ViewportLayoutTests, BuildSplitHandlesMatchAxesAndSpans) {
    const ViewportLayoutState state = makeDistinctViewportLayoutState();

    EXPECT_TRUE(viewportLayoutSplitHandles(ViewportLayoutId::Single, state).empty());

    std::vector<ViewportSplitHandle> handles = viewportLayoutSplitHandles(ViewportLayoutId::TwoHorizontal, state);
    ASSERT_EQ(handles.size(), 1u);
    expectViewportSplitHandle(handles[0], ViewportSplitHandleId::TwoHorizontal, ViewportSplitHandleAxis::Vertical, 0.25f, 0.0f, 1.0f);

    handles = viewportLayoutSplitHandles(ViewportLayoutId::TwoVertical, state);
    ASSERT_EQ(handles.size(), 1u);
    expectViewportSplitHandle(handles[0], ViewportSplitHandleId::TwoVertical, ViewportSplitHandleAxis::Horizontal, 0.35f, 0.0f, 1.0f);

    handles = viewportLayoutSplitHandles(ViewportLayoutId::ThreePrimaryRight, state);
    ASSERT_EQ(handles.size(), 2u);
    expectViewportSplitHandle(handles[0], ViewportSplitHandleId::ThreePrimary, ViewportSplitHandleAxis::Vertical, 0.60f, 0.0f, 1.0f);
    expectViewportSplitHandle(handles[1], ViewportSplitHandleId::ThreeSecondary, ViewportSplitHandleAxis::Horizontal, 0.40f, 0.0f, 0.60f);

    handles = viewportLayoutSplitHandles(ViewportLayoutId::ThreePrimaryLeft, state);
    ASSERT_EQ(handles.size(), 2u);
    expectViewportSplitHandle(handles[0], ViewportSplitHandleId::ThreePrimary, ViewportSplitHandleAxis::Vertical, 0.60f, 0.0f, 1.0f);
    expectViewportSplitHandle(handles[1], ViewportSplitHandleId::ThreeSecondary, ViewportSplitHandleAxis::Horizontal, 0.40f, 0.60f, 1.0f);

    handles = viewportLayoutSplitHandles(ViewportLayoutId::ThreePrimaryBottom, state);
    ASSERT_EQ(handles.size(), 2u);
    expectViewportSplitHandle(handles[0], ViewportSplitHandleId::ThreePrimary, ViewportSplitHandleAxis::Horizontal, 0.60f, 0.0f, 1.0f);
    expectViewportSplitHandle(handles[1], ViewportSplitHandleId::ThreeSecondary, ViewportSplitHandleAxis::Vertical, 0.40f, 0.0f, 0.60f);

    handles = viewportLayoutSplitHandles(ViewportLayoutId::ThreePrimaryTop, state);
    ASSERT_EQ(handles.size(), 2u);
    expectViewportSplitHandle(handles[0], ViewportSplitHandleId::ThreePrimary, ViewportSplitHandleAxis::Horizontal, 0.60f, 0.0f, 1.0f);
    expectViewportSplitHandle(handles[1], ViewportSplitHandleId::ThreeSecondary, ViewportSplitHandleAxis::Vertical, 0.40f, 0.60f, 1.0f);

    handles = viewportLayoutSplitHandles(ViewportLayoutId::FourQuad, state);
    ASSERT_EQ(handles.size(), 2u);
    expectViewportSplitHandle(handles[0], ViewportSplitHandleId::FourVertical, ViewportSplitHandleAxis::Vertical, 0.30f, 0.0f, 1.0f);
    expectViewportSplitHandle(handles[1], ViewportSplitHandleId::FourHorizontal, ViewportSplitHandleAxis::Horizontal, 0.70f, 0.0f, 1.0f);
}

TEST(ViewportLayoutTests, SplitStateDefaultsAndClamps) {
    ViewportLayoutState state;

    EXPECT_FLOAT_EQ(state.twoHorizontalSplit(), 0.5f);
    EXPECT_FLOAT_EQ(state.twoVerticalSplit(), 0.5f);
    EXPECT_FLOAT_EQ(state.threePrimarySplit(), 0.5f);
    EXPECT_FLOAT_EQ(state.threeSecondarySplit(), 0.5f);
    EXPECT_FLOAT_EQ(state.fourVerticalSplit(), 0.5f);
    EXPECT_FLOAT_EQ(state.fourHorizontalSplit(), 0.5f);

    state.setHandlePosition(ViewportSplitHandleId::TwoHorizontal, -1.0f);
    state.setHandlePosition(ViewportSplitHandleId::TwoVertical, 2.0f);
    state.setHandlePosition(ViewportSplitHandleId::ThreePrimary, 0.14f);
    state.setHandlePosition(ViewportSplitHandleId::ThreeSecondary, 0.86f);
    state.setHandlePosition(ViewportSplitHandleId::FourVertical, 0.40f);
    state.setHandlePosition(ViewportSplitHandleId::FourHorizontal, 0.65f);

    EXPECT_FLOAT_EQ(state.twoHorizontalSplit(), ViewportLayoutState::MinSplit);
    EXPECT_FLOAT_EQ(state.twoVerticalSplit(), ViewportLayoutState::MaxSplit);
    EXPECT_FLOAT_EQ(state.threePrimarySplit(), ViewportLayoutState::MinSplit);
    EXPECT_FLOAT_EQ(state.threeSecondarySplit(), ViewportLayoutState::MaxSplit);
    EXPECT_FLOAT_EQ(state.fourVerticalSplit(), 0.40f);
    EXPECT_FLOAT_EQ(state.fourHorizontalSplit(), 0.65f);
}

TEST(ViewportWorkspaceServiceTests, EmbeddedSinglePanePolicyRejectsMultiPaneLayouts) {
    ViewportWorkspaceService workspace(ViewportWorkspaceLayoutPolicy::SinglePaneOnly);

    EXPECT_EQ(workspace.layoutPolicy(), ViewportWorkspaceLayoutPolicy::SinglePaneOnly);
    EXPECT_TRUE(workspace.supportsLayout(ViewportLayoutId::Single));
    EXPECT_FALSE(workspace.supportsLayout(ViewportLayoutId::FourQuad));
    EXPECT_FALSE(workspace.setLayout(ViewportLayoutId::FourQuad));
    EXPECT_EQ(workspace.layout(), ViewportLayoutId::Single);

    std::vector<ViewportPanePlacement> placements = workspace.activePanePlacements();
    ASSERT_EQ(placements.size(), 1u);
    expectViewportPlacement(placements[0], ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, 1.0f);
}

TEST(ViewportWorkspaceServiceTests, MaximizedPaneStateOwnsActiveLayout) {
    ViewportWorkspaceService workspace(ViewportWorkspaceLayoutPolicy::MultiPane);
    EXPECT_EQ(workspace.gridPreset(), viewportDefaultGridPreset());
    ASSERT_TRUE(workspace.setLayout(ViewportLayoutId::FourQuad));
    ASSERT_TRUE(workspace.focusPane(ViewportPaneId::Side));

    EXPECT_FALSE(workspace.focusedPaneMaximized());
    EXPECT_EQ(workspace.activePanePlacements().size(), 4u);
    EXPECT_EQ(workspace.activeSplitHandles().size(), 2u);

    ASSERT_TRUE(workspace.toggleFocusedPaneMaximized());

    EXPECT_TRUE(workspace.focusedPaneMaximized());
    std::vector<ViewportPanePlacement> placements = workspace.activePanePlacements();
    ASSERT_EQ(placements.size(), 1u);
    expectViewportPlacement(placements[0], ViewportPaneId::Side, 0.0f, 0.0f, 1.0f, 1.0f);
    EXPECT_TRUE(workspace.activeSplitHandles().empty());

    ASSERT_TRUE(workspace.setFocusedPaneMaximized(false));

    EXPECT_FALSE(workspace.focusedPaneMaximized());
    EXPECT_EQ(workspace.activePanePlacements().size(), 4u);
    EXPECT_EQ(workspace.activeSplitHandles().size(), 2u);
}

TEST(ViewportWorkspaceServiceTests, GridPresetIsClampedAtWorkspaceOwner) {
    ViewportWorkspaceService workspace;
    ASSERT_TRUE(workspace.setGridPreset(-50));
    EXPECT_EQ(workspace.gridPreset(), 1);

    ASSERT_TRUE(workspace.setGridPreset(999));
    EXPECT_EQ(workspace.gridPreset(), static_cast<int>(quader_host_contract::grid_preset_catalog().size()));

    ViewportInputRouter router;
    router.syncGridPresetSnapshot(999);
    EXPECT_EQ(router.state().gridPreset, static_cast<int>(quader_host_contract::grid_preset_catalog().size()));

    const std::string routerSource = readSourceFile("src/viewport/viewport_input_router.cpp");
    const std::string runtimeSource = readSourceFile("src/editor/tools/viewport_tool_runtime.cpp");
    const std::string viewportPanelSource = readSourceFile("src/ui/viewport_panel.cpp");

    EXPECT_NE(routerSource.find("void ViewportInputRouter::syncGridPresetSnapshot(int preset)"), std::string::npos);
    EXPECT_NE(runtimeSource.find("result.requestedGridPreset = viewportClampGridPreset(shortcut.gridPreset)"), std::string::npos);
    EXPECT_EQ(runtimeSource.find("state.gridPreset = viewportClampGridPreset(shortcut.gridPreset)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("viewportWorkspace.setGridPreset(result.requestedGridPreset)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("inputRouter_.syncGridPresetSnapshot(viewportWorkspace.gridPreset())"), std::string::npos);
}

TEST(ViewportWorkspaceServiceTests, WorkspaceFocusDistancesUseHostViewportDefaults) {
    const std::string source = readSourceFile("src/viewport/viewport_workspace_service.cpp");
    EXPECT_NE(source.find("viewportHostDefaults().focusOriginDistance"), std::string::npos);
    EXPECT_EQ(source.find("focusOrigin(64.0f)"), std::string::npos);
}

TEST(ViewportCameraTests, FlySpeedDefaultsUseHostViewportDefaults) {
    const ViewportHostDefaults defaults = viewportHostDefaults();
    EXPECT_EQ(defaults.flyBaseSpeed, quader_host_contract::viewport_defaults().fly_base_speed);
    EXPECT_EQ(defaults.flyFastMultiplier, quader_host_contract::viewport_defaults().fly_fast_multiplier);
    EXPECT_EQ(defaults.flySlowMultiplier, quader_host_contract::viewport_defaults().fly_slow_multiplier);

    ViewportCameraController camera;
    EXPECT_NEAR(camera.flySpeed(), static_cast<float>(defaults.flyBaseSpeed), 0.001f);

    const std::string source = readSourceFile("src/viewport/viewport_camera_controller.cpp");
    EXPECT_EQ(source.find("kDefaultFlySpeed"), std::string::npos);
    EXPECT_NE(source.find("viewportHostDefaults().flyBaseSpeed"), std::string::npos);
}

TEST(ViewportWorkspaceServiceTests, LayoutChangeClearsMaximizedPaneState) {
    ViewportWorkspaceService workspace(ViewportWorkspaceLayoutPolicy::MultiPane);
    ASSERT_TRUE(workspace.setLayout(ViewportLayoutId::FourQuad));
    ASSERT_TRUE(workspace.focusPane(ViewportPaneId::Side));
    ASSERT_TRUE(workspace.setFocusedPaneMaximized(true));

    ASSERT_TRUE(workspace.setLayout(ViewportLayoutId::Single));

    EXPECT_FALSE(workspace.focusedPaneMaximized());
    EXPECT_EQ(workspace.focusedPane(), ViewportPaneId::Perspective);
    std::vector<ViewportPanePlacement> placements = workspace.activePanePlacements();
    ASSERT_EQ(placements.size(), 1u);
    expectViewportPlacement(placements[0], ViewportPaneId::Perspective, 0.0f, 0.0f, 1.0f, 1.0f);
    EXPECT_TRUE(workspace.activeSplitHandles().empty());
}

TEST(ViewportWorkspaceServiceTests, ViewReflectsCameraMutationsOutsideSetViewForConstPaneState) {
    ViewportWorkspaceService workspace;
    ASSERT_TRUE(workspace.setView(ViewportPaneId::Perspective, ViewportView::Top));
    ASSERT_EQ(workspace.view(ViewportPaneId::Perspective), ViewportView::Top);

    resetViewportCameraForView(workspace.camera(ViewportPaneId::Perspective), ViewportView::Front);
    const ViewportWorkspaceService& constWorkspace = workspace;
    const ViewportWorkspacePaneState& constPane = constWorkspace.pane(ViewportPaneId::Perspective);

    EXPECT_EQ(constWorkspace.view(ViewportPaneId::Perspective), ViewportView::Front);
    EXPECT_EQ(viewportViewForCamera(constPane.camera), ViewportView::Front);
}

TEST(ViewportWorkspaceServiceTests, AuxiliaryViewportRecordsOwnWindowedCameraAndRenderState) {
    ViewportWorkspaceService workspace;

    ViewportWorkspaceAuxiliaryViewportState& created =
            workspace.ensureAuxiliaryViewport("viewport-2", 1u);
    EXPECT_EQ(created.stableId, "viewport-2");
    EXPECT_EQ(created.layerIndex, 1u);
    EXPECT_EQ(workspace.auxiliaryViewportCount(), 1u);
    EXPECT_NE(workspace.auxiliaryViewport("viewport-2"), nullptr);
    EXPECT_EQ(workspace.auxiliaryViewport("missing"), nullptr);

    EXPECT_TRUE(workspace.setAuxiliaryViewportRenderSize("viewport-2", {640.0f, 360.0f}));
    ASSERT_NE(workspace.auxiliaryViewport("viewport-2"), nullptr);
    EXPECT_EQ(workspace.auxiliaryViewport("viewport-2")->renderSize.x, 640.0f);
    EXPECT_EQ(workspace.auxiliaryViewport("viewport-2")->renderSize.y, 360.0f);
    EXPECT_EQ(workspace.auxiliaryViewportView("viewport-2"), ViewportView::Perspective);
    EXPECT_TRUE(workspace.setAuxiliaryViewportView("viewport-2", ViewportView::Left));
    EXPECT_EQ(workspace.auxiliaryViewportView("viewport-2"), ViewportView::Left);
    EXPECT_FALSE(workspace.setAuxiliaryViewportView("viewport-2", ViewportView::Left));
    EXPECT_FALSE(workspace.setAuxiliaryViewportView("missing", ViewportView::Top));
    workspace.setAuxiliaryViewportVisible("viewport-2", true);
    EXPECT_TRUE(workspace.auxiliaryViewportVisible("viewport-2"));

    workspace.clearPaneVisibility();
    EXPECT_FALSE(workspace.auxiliaryViewportVisible("viewport-2"));

    ViewportWorkspaceAuxiliaryViewportState& existing =
            workspace.ensureAuxiliaryViewport("viewport-2", 2u);
    EXPECT_EQ(workspace.auxiliaryViewportCount(), 1u);
    EXPECT_EQ(existing.layerIndex, 2u);

    EXPECT_TRUE(workspace.removeAuxiliaryViewport("viewport-2"));
    EXPECT_EQ(workspace.auxiliaryViewportCount(), 0u);
    EXPECT_FALSE(workspace.removeAuxiliaryViewport("viewport-2"));
}

TEST(ViewportWorkspaceServiceTests, PaneStateDoesNotStoreDuplicateView) {
    const std::string header = readSourceFile("src/viewport/viewport_workspace_service.hpp");
    const std::string source = readSourceFile("src/viewport/viewport_workspace_service.cpp");

    EXPECT_EQ(header.find("ViewportView view ="), std::string::npos);
    EXPECT_EQ(source.find("paneState.view"), std::string::npos);
}

TEST(ViewportCameraTests, OrbitPanAndZoomChangePose) {
    ViewportCameraController camera;
    const auto before = camera.pose();
    const float initialPitch = camera.state().pitchRadians;
    const float initialDistance = camera.state().distance;

    camera.setViewportSize(800.0f, 600.0f);
    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({160.0f, 120.0f});
    camera.endDrag();
    camera.scroll(1.0f);

    const auto after = camera.pose();
    EXPECT_NE(before.eye.x, after.eye.x);
    EXPECT_LT(camera.state().pitchRadians, initialPitch);
    EXPECT_LT(camera.state().distance, initialDistance);
}

TEST(ViewportCameraTests, DefaultPerspectiveMatchesViewportContract) {
    ViewportCameraController camera;
    const ViewportCameraPose pose = camera.pose();

    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_NEAR(camera.state().fovDegrees, 60.0f, 0.001f);
    EXPECT_NEAR(camera.state().distance, 11.18034f, 0.001f);
    EXPECT_NEAR(pose.eye.x, 6.0f, 0.001f);
    EXPECT_NEAR(pose.eye.y, 5.0f, 0.001f);
    EXPECT_NEAR(pose.eye.z, 8.0f, 0.001f);
}

TEST(ViewportCameraTests, CtrlWheelZoomsTowardCursor) {
    ViewportCameraController camera;
    const float initialDistance = camera.state().distance;
    camera.setViewportSize(800.0f, 600.0f);

    camera.scrollToCursor(1.0f, {700.0f, 100.0f});

    EXPECT_LT(camera.state().distance, initialDistance);
    EXPECT_GT(camera.state().target.x, 0.0f);
    EXPECT_GT(camera.state().target.y, 0.0f);
}

TEST(ViewportCameraTests, BuildsPickingRaysFromViewportPoints) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);

    const ViewportRay centerRay = camera.rayFromViewportPoint({400.0f, 300.0f});
    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);

    EXPECT_NEAR(centerRay.origin.x, pose.eye.x, 0.001f);
    EXPECT_NEAR(centerRay.origin.y, pose.eye.y, 0.001f);
    EXPECT_NEAR(centerRay.origin.z, pose.eye.z, 0.001f);
    EXPECT_NEAR(dot(centerRay.direction, forward), 1.0f, 0.001f);

    const ViewportRay cornerRay = camera.rayFromViewportPoint({800.0f, 0.0f});
    EXPECT_GT(cornerRay.direction.x, centerRay.direction.x);
    EXPECT_GT(cornerRay.direction.y, centerRay.direction.y);
    EXPECT_NEAR(length(cornerRay.direction), 1.0f, 0.001f);
}

TEST(ViewportCameraTests, ProjectsWorldPointsToViewportPixels) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);

    const ViewportScreenPoint center = camera.projectWorldPoint(camera.pose().target);
    ASSERT_TRUE(center.visible);
    EXPECT_NEAR(center.position.x, 400.0f, 0.001f);
    EXPECT_NEAR(center.position.y, 300.0f, 0.001f);

    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);
    const QVec3 right = normalize(cross(forward, pose.up));
    const QVec3 up = normalize(cross(right, forward));
    const ViewportScreenPoint rightUp = camera.projectWorldPoint(pose.target + right * 0.5f + up * 0.5f);

    ASSERT_TRUE(rightUp.visible);
    EXPECT_GT(rightUp.position.x, center.position.x);
    EXPECT_LT(rightUp.position.y, center.position.y);
    EXPECT_GT(rightUp.depth, 0.0f);
}

TEST(ViewportCameraTests, FocusBoundsFramesDocumentCenter) {
    ViewportCameraController camera;

    camera.focusBounds({10.0f, -2.0f, -4.0f}, {14.0f, 2.0f, 4.0f});

    EXPECT_NEAR(camera.state().target.x, 12.0f, 0.001f);
    EXPECT_NEAR(camera.state().target.y, 0.0f, 0.001f);
    EXPECT_NEAR(camera.state().target.z, 0.0f, 0.001f);
    EXPECT_GT(camera.state().distance, 4.0f);
}

TEST(ViewportCameraTests, SetViewDirectionKeepsTargetDistanceAndSnapsAxis) {
    ViewportCameraController camera;
    camera.focusBounds({10.0f, -2.0f, -4.0f}, {14.0f, 2.0f, 4.0f});

    const QVec3 targetBefore = camera.state().target;
    const float distanceBefore = camera.state().distance;
    camera.setViewDirection({-1.0f, 0.0f, 0.0f});

    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);
    EXPECT_NEAR(camera.state().target.x, targetBefore.x, 0.001f);
    EXPECT_NEAR(camera.state().target.y, targetBefore.y, 0.001f);
    EXPECT_NEAR(camera.state().target.z, targetBefore.z, 0.001f);
    EXPECT_FLOAT_EQ(camera.state().distance, distanceBefore);
    EXPECT_NEAR(forward.x, -1.0f, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, 0.0f, 0.001f);
}

TEST(ViewportCameraTests, ViewAxisUsesRuntimeNavigationPolicy) {
    ViewportCameraController camera;
    camera.focusBounds({10.0f, -2.0f, -4.0f}, {14.0f, 2.0f, 4.0f});

    const QVec3 targetBefore = camera.state().target;
    const float distanceBefore = camera.state().distance;
    camera.viewAxis(ViewportViewAxis::Y, 1);

    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);
    EXPECT_NEAR(camera.state().target.x, targetBefore.x, 0.001f);
    EXPECT_NEAR(camera.state().target.y, targetBefore.y, 0.001f);
    EXPECT_NEAR(camera.state().target.z, targetBefore.z, 0.001f);
    EXPECT_NEAR(camera.state().distance, distanceBefore, 0.001f);
    EXPECT_NEAR(forward.x, 0.0f, 0.001f);
    EXPECT_NEAR(forward.y, -1.0f, 0.001f);
    EXPECT_NEAR(forward.z, 0.0f, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_TRUE(camera.orthographicViewLocked());
    EXPECT_GT(camera.state().orthographicSize, 0.0f);
    EXPECT_GT(length(pose.up), 0.99f);
}

TEST(ViewportCameraTests, ViewCubeAxisSnapDoesNotLockOrbit) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.viewAxis(ViewportViewAxis::Z, 1, ViewportAxisLock::Unlocked);

    ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    ASSERT_FALSE(camera.orthographicViewLocked());

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({160.0f, 120.0f});
    camera.endDrag();

    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
}

TEST(ViewportCameraTests, ViewCubePrimitivesExposeVisibleAxisEdges) {
    quader_editor_core::ViewCubePrimitiveInput input;
    input.viewport_size = {200.0f, 200.0f};
    input.widget_size = 70.0;
    input.camera_transform.x_axis = {0.70710678f, 0.0f, -0.70710678f};
    input.camera_transform.y_axis = {-0.40824829f, 0.81649658f, -0.40824829f};
    input.camera_transform.z_axis = {0.57735027f, 0.57735027f, 0.57735027f};

    const quader_editor_core::ViewCubePrimitives primitives =
            quader_editor_core::build_view_cube_primitives(input);

    EXPECT_TRUE(primitives.ok);
    EXPECT_EQ(primitives.faces.size(), 3u);
    EXPECT_EQ(primitives.edges.size(), 9u);

    std::array<bool, 3> hasAxis = {false, false, false};
    for (const quader_editor_core::ViewCubeEdgePrimitive& edge : primitives.edges) {
        const float dx = edge.screen_end.x - edge.screen_start.x;
        const float dy = edge.screen_end.y - edge.screen_start.y;
        EXPECT_GT(dx * dx + dy * dy, 1.0f);
        switch (edge.axis) {
        case quader_editor_core::ViewAxis::x:
            hasAxis[0] = true;
            break;
        case quader_editor_core::ViewAxis::y:
            hasAxis[1] = true;
            break;
        case quader_editor_core::ViewAxis::z:
            hasAxis[2] = true;
            break;
        }
    }
    EXPECT_TRUE(hasAxis[0]);
    EXPECT_TRUE(hasAxis[1]);
    EXPECT_TRUE(hasAxis[2]);

    EXPECT_EQ(primitives.axis_edges.size(), 3u);
    for (const quader_editor_core::ViewCubeEdgePrimitive& edge : primitives.axis_edges) {
        EXPECT_FLOAT_EQ(edge.local_start.x, 1.0f);
        EXPECT_FLOAT_EQ(edge.local_start.y, -1.0f);
        EXPECT_FLOAT_EQ(edge.local_start.z, 1.0f);
    }
}

TEST(ViewportCameraTests, OrthographicAxisViewBuildsParallelPickingRays) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.viewAxis(ViewportViewAxis::Z, 1);

    ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    const ViewportRay centerRay = camera.rayFromViewportPoint({400.0f, 300.0f});
    const ViewportRay cornerRay = camera.rayFromViewportPoint({800.0f, 0.0f});
    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);

    EXPECT_NEAR(dot(centerRay.direction, forward), 1.0f, 0.001f);
    EXPECT_NEAR(dot(cornerRay.direction, centerRay.direction), 1.0f, 0.001f);
    EXPECT_NE(centerRay.origin.x, cornerRay.origin.x);
    EXPECT_NE(centerRay.origin.y, cornerRay.origin.y);
}

TEST(ViewportCameraTests, FocusOriginResetsOrthographicSize) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.viewAxis(ViewportViewAxis::Y, 1);
    ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    const float initialOrthographicSize = camera.state().orthographicSize;

    camera.scroll(3.0f);
    ASSERT_NE(camera.state().orthographicSize, initialOrthographicSize);

    camera.focusOrigin();

    EXPECT_NEAR(camera.state().target.x, 0.0f, 0.001f);
    EXPECT_NEAR(camera.state().target.y, 0.0f, 0.001f);
    EXPECT_NEAR(camera.state().target.z, 0.0f, 0.001f);
    EXPECT_NEAR(camera.state().distance, 64.0f, 0.001f);
    EXPECT_NEAR(camera.state().orthographicSize, 64.0f, 0.001f);
}

TEST(ViewportCameraTests, SetViewDirectionClampsVerticalViewWithoutDegenerateBasis) {
    ViewportCameraController camera;
    camera.setViewDirection({0.0f, -1.0f, 0.0f});

    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);
    EXPECT_LT(forward.y, -0.99f);
    EXPECT_GT(length(pose.up), 0.99f);
}

TEST(ViewportCameraTests, OrbitSnapUsesRuntimeOrthogonalSnap) {
    ViewportCameraController camera;
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
    camera.updateDrag({100.0f, 100.0f});
    camera.endDrag();

    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);
    EXPECT_NEAR(forward.x, 0.0f, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, -1.0f, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_GT(camera.state().orthographicSize, 0.0f);

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({118.0f, 100.0f});
    camera.endDrag();
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
}

TEST(ViewportCameraTests, OrbitSnapPreservesPerspectiveZoomLevel) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(30.0f);
    camera.scroll(3.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    const float expectedVisibleHeight = 2.0f * camera.state().distance *
            std::tan(camera.state().fovDegrees * static_cast<float>(std::acos(-1.0)) / 360.0f);

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
    camera.endDrag();

    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_NEAR(camera.state().orthographicSize, expectedVisibleHeight, 0.001f);
}

TEST(ViewportCameraTests, OrbitSnapTransitionSamplesCurrentViewBeforeMouseDelta) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({220.0f, 100.0f}, ViewportDragMode::OrbitSnap);
    camera.endDrag();

    const QVec3 forward = normalize(camera.pose().target - camera.pose().eye);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_NEAR(forward.x, 0.0f, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, -1.0f, 0.001f);
}

TEST(ViewportCameraTests, OrbitSnapUsesViewAxisWhenNavigationPivotIsOffCenter) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});
    camera.scrollToCursor(1.0f, {700.0f, 100.0f});
    ASSERT_GT(length(camera.state().target), 0.001f);

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
    camera.endDrag();

    const QVec3 forward = normalize(camera.pose().target - camera.pose().eye);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_NEAR(forward.x, 0.0f, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, -1.0f, 0.001f);
}

TEST(ViewportCameraTests, OrbitFromUnlockedOrthographicPreservesZoomLevel) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(30.0f);
    camera.viewAxis(ViewportViewAxis::Z, 1, ViewportAxisLock::Unlocked);
    camera.scroll(3.0f);

    const float orthographicSize = camera.state().orthographicSize;

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({118.0f, 100.0f});
    camera.endDrag();

    const float visibleHeight = 2.0f * camera.state().distance *
            std::tan(camera.state().fovDegrees * static_cast<float>(std::acos(-1.0)) / 360.0f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_NEAR(visibleHeight, orthographicSize, 0.001f);
}

TEST(ViewportCameraTests, OrbitSnapUsesBlenderDiagonalAxisTargets) {
    ViewportCameraController camera;
    camera.focusOrigin(8.0f);
    camera.setViewDirection({-0.63f, 0.02f, -0.78f});

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
    camera.updateDrag({100.0f, 100.0f});
    camera.endDrag();

    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);
    constexpr float kDiagonalComponent = 0.70710678f;
    EXPECT_NEAR(forward.x, -kDiagonalComponent, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, -kDiagonalComponent, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
}

TEST(ViewportCameraTests, FlyLookKeepsEyeFixedAndFlyMoveAdvancesCamera) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);

    const ViewportCameraPose startPose = camera.pose();
    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Fly);
    camera.updateDrag({160.0f, 80.0f});
    const ViewportCameraPose lookPose = camera.pose();
    const QVec3 startForward = normalize(startPose.target - startPose.eye);
    const QVec3 lookForward = normalize(lookPose.target - lookPose.eye);

    EXPECT_NEAR(length(lookPose.eye - startPose.eye), 0.0f, 0.001f);
    EXPECT_LT(dot(lookForward, startForward), 0.999f);

    const float defaultFlySpeed = static_cast<float>(viewportHostDefaults().flyBaseSpeed);
    EXPECT_NEAR(camera.flySpeed(), defaultFlySpeed, 0.001f);
    camera.adjustFlySpeed(1.0f);
    EXPECT_NEAR(camera.flySpeed(), defaultFlySpeed + 1.0f, 0.001f);

    camera.flyMove({0.0f, 0.0f, 1.0f}, 0.25f);
    const ViewportCameraPose movedPose = camera.pose();
    const QVec3 movedDelta = movedPose.eye - lookPose.eye;

    EXPECT_GT(dot(movedDelta, lookForward), (defaultFlySpeed + 1.0f) * 0.24f);
    EXPECT_NEAR(length((movedPose.target - lookPose.target) - movedDelta), 0.0f, 0.001f);
}

TEST(ViewportCameraTests, FlyLookDoesNotSnapToTopOrBottomView) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Fly);
    camera.updateDrag({100.0f, -1200.0f});
    camera.endDrag();

    ViewportCameraPose pose = camera.pose();
    QVec3 forward = normalize(pose.target - pose.eye);
    EXPECT_GT(std::sqrt(forward.x * forward.x + forward.z * forward.z), 0.001f);
    EXPECT_GT(length(pose.up), 0.99f);
    EXPECT_NEAR(dot(forward, pose.up), 0.0f, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Fly);
    camera.updateDrag({100.0f, 1400.0f});
    camera.endDrag();

    pose = camera.pose();
    forward = normalize(pose.target - pose.eye);
    EXPECT_GT(std::sqrt(forward.x * forward.x + forward.z * forward.z), 0.001f);
    EXPECT_GT(length(pose.up), 0.99f);
    EXPECT_NEAR(dot(forward, pose.up), 0.0f, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
}

TEST(ViewportCameraTests, PlainMiddleOrbitNearOrthogonalAxisDoesNotSnapToOrthographic) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({100.0f, 100.0f});
    camera.endDrag();

    const ViewportCameraPose pose = camera.pose();
    const QVec3 forward = normalize(pose.target - pose.eye);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_GT(std::abs(forward.x), 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
}

TEST(ViewportInputRouterTests, ImplementedToolHotkeysMatchViewportContract) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};

    setResolvedShortcut(input, testToolShortcut(ViewportTool::Box));
    auto result = router.handleFrame(input, camera);
    EXPECT_EQ(result.shortcutHandled, viewportToolHasNativeController(ViewportTool::Box));
    EXPECT_EQ(router.state().activeTool,
            viewportToolHasNativeController(ViewportTool::Box) ? ViewportTool::Box : ViewportTool::Select);

    setResolvedShortcut(input, testToolShortcut(ViewportTool::Select));
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Select);

    setResolvedShortcut(input, testToolShortcut(ViewportTool::Move));
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Move);

    setResolvedShortcut(input, testToolShortcut(ViewportTool::Rotate));
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Rotate);

    setResolvedShortcut(input, testToolShortcut(ViewportTool::Scale));
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Scale);
}

TEST(ViewportInputRouterTests, ResolvedNavigationBindingsDriveMouseNavigation) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {200.0f, 150.0f};

    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.navigationActive);
    EXPECT_FALSE(result.consumed);

    setNavigationPressed(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.navigationActive);
    EXPECT_TRUE(result.consumed);

    setNavigationDown(input, ViewportInputActionKind::Orbit);
    input.mouseScreen = {220.0f, 165.0f};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_TRUE(result.navigationActive);

    setNavigationReleased(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.navigationActive);
    EXPECT_TRUE(result.consumed);
}

TEST(ViewportInputRouterTests, ResolvedMiddleOrbitSamplesAltDuringDragForAxisSnap) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::Orbit);

    ViewportInputResult result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    ASSERT_TRUE(router.navigating());
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);

    setNavigationDown(input, ViewportInputActionKind::Orbit);
    input.modifiers.alt = true;
    input.mouseScreen = {220.0f, 100.0f};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.cameraChanged);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_FALSE(camera.orthographicViewLocked());

    input.modifiers.alt = false;
    input.mouseScreen = {116.0f, 100.0f};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.cameraChanged);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
}

TEST(ViewportInputRouterTests, ResolvedWheelBindingsDriveViewportZoom) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseLocal = {400.0f, 300.0f};
    input.mouseWheel = 1.0f;

    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.cameraChanged);
    EXPECT_FALSE(result.consumed);

    input.navigationBindings.zoomWheel = true;
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.zoomedToCursor);

    input.navigationBindings.zoomWheel = false;
    input.navigationBindings.zoomToCursorWheel = true;
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_TRUE(result.zoomedToCursor);
}

TEST(ViewportInputRouterTests, ResolvedResetFlySpeedDoesNotPreemptActiveFlyNavigation) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.adjustFlySpeed(5.0f);
    ASSERT_FALSE(camera.flySpeedIsDefault());

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    ViewportInputResult result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    setNavigationDown(input, ViewportInputActionKind::Fly);
    input.navigationBindings.orbitPressed = true;
    input.navigationBindings.orbitDown = true;
    input.navigationBindings.resetFlySpeedPressed = true;
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flySpeedChanged);
    EXPECT_TRUE(result.flySpeedReset);
    EXPECT_NEAR(camera.flySpeed(), static_cast<float>(viewportHostDefaults().flyBaseSpeed), 0.001f);
    EXPECT_TRUE(camera.flySpeedIsDefault());
}

TEST(ViewportInputRouterTests, ResolvedFlyNavigationKeepsWasdMovementAheadOfToolShortcuts) {
    ViewportInputRouter router;
    ViewportCameraController camera;

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    ViewportInputResult result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    setNavigationDown(input, ViewportInputActionKind::Fly);
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    input.resolvedShortcuts = {{
            .kind = ViewportShortcutKind::ActivateTool,
            .context = ViewportShortcutContext::Viewport,
            .tool = ViewportToolId::Move,
    }};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Select);
}

TEST(ViewportInputRouterTests, ResolvedFlyNavigationContinuesWhileShiftIsHeld) {
    ViewportInputRouter router;
    ViewportCameraController camera;

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    ViewportInputResult result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    setNavigationDown(input, ViewportInputActionKind::Fly);
    input.modifiers.shift = true;
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_TRUE(result.flyNavigationActive);
}

TEST(ViewportInputRouterTests, ModalToolHotkeysActivateNativeControllers) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};

    setResolvedShortcut(input, testToolShortcut(ViewportTool::InsertEdgeLoop));
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::InsertEdgeLoop);

    setResolvedShortcut(input, testToolShortcut(ViewportTool::Cut));
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Cut);

    setResolvedShortcut(input, testToolShortcut(ViewportTool::Knife));
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Knife);

    setResolvedShortcut(input, testToolShortcut(ViewportTool::Poly));
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Poly);
}

TEST(ViewportInputRouterTests, OperationShortcutsMatchViewportContract) {
    /**
     * Stores the Shortcut Case data contract used by the automated test suite.
     */
    struct ShortcutCase {
        const char* label;
        ViewportShortcut shortcut;
    };

    const std::array<ShortcutCase, 21> shortcuts{{
            {"Ctrl+A select all", testRouterShortcut(ViewportShortcutKind::SelectAll, ViewportShortcutContext::Global)},
            {"Ctrl+I invert selection", testRouterShortcut(ViewportShortcutKind::InvertSelection, ViewportShortcutContext::Global)},
            {"Delete delete selection", testRouterShortcut(ViewportShortcutKind::DeleteSelection, ViewportShortcutContext::Global)},
            {"Backspace delete selection", testRouterShortcut(ViewportShortcutKind::DeleteSelection, ViewportShortcutContext::Global)},
            {"Ctrl+D duplicate", testRouterShortcut(ViewportShortcutKind::Duplicate, ViewportShortcutContext::Global)},
            {"Ctrl+C copy", testRouterShortcut(ViewportShortcutKind::Copy, ViewportShortcutContext::Global)},
            {"Ctrl+V paste", testRouterShortcut(ViewportShortcutKind::Paste, ViewportShortcutContext::Global)},
            {"Ctrl+Z undo", testRouterShortcut(ViewportShortcutKind::Undo, ViewportShortcutContext::Global)},
            {"Ctrl+Y redo", testRouterShortcut(ViewportShortcutKind::Redo, ViewportShortcutContext::Global)},
            {"Ctrl+Shift+Z redo", testRouterShortcut(ViewportShortcutKind::Redo, ViewportShortcutContext::Global)},
            {"Ctrl+R repeat", testRouterShortcut(ViewportShortcutKind::RepeatLastAction, ViewportShortcutContext::Global)},
            {"M merge active", testRouterShortcut(ViewportShortcutKind::MergeActive)},
            {"Shift+M merge center", testRouterShortcut(ViewportShortcutKind::MergeCenter)},
            {"X delete popup", testRouterShortcut(ViewportShortcutKind::DeletePopup)},
            {"Shift+X mirror X", testMirrorShortcut(ViewportMirrorAxis::X)},
            {"Shift+Y mirror Y", testMirrorShortcut(ViewportMirrorAxis::Y)},
            {"Shift+Z mirror Z", testMirrorShortcut(ViewportMirrorAxis::Z)},
            {"F flip mesh normals", testRouterShortcut(ViewportShortcutKind::FlipMeshNormals)},
            {"Shift+H flip horizontal", testRouterShortcut(ViewportShortcutKind::FlipHorizontal)},
            {"Shift+V flip vertical", testRouterShortcut(ViewportShortcutKind::FlipVertical)},
            {"Shift+T apply material", testRouterShortcut(ViewportShortcutKind::ApplyMaterial)},
    }};

    for (const ShortcutCase& shortcutCase : shortcuts) {
        SCOPED_TRACE(shortcutCase.label);
        ViewportInputRouter router;
        ViewportCameraController camera;
        ViewportInputFrame input;
        input.viewportFocused = true;
        input.viewportSize = {800.0f, 600.0f};
        setResolvedShortcut(input, shortcutCase.shortcut);

        const auto result = router.handleFrame(input, camera);

        EXPECT_TRUE(result.shortcutHandled);
        EXPECT_TRUE(result.consumed);
        EXPECT_EQ(result.shortcut.kind, shortcutCase.shortcut.kind);
        EXPECT_EQ(result.shortcut.context, shortcutCase.shortcut.context);
        if (shortcutCase.shortcut.kind == ViewportShortcutKind::MirrorAxis) {
            EXPECT_EQ(result.shortcut.mirrorAxis, shortcutCase.shortcut.mirrorAxis);
        }
    }
}

TEST(ViewportInputRouterTests, LegacyShortcutAdapterSourceIsNotBuilt) {
    const std::string cmake = readSourceFile("CMakeLists.txt");

    EXPECT_EQ(cmake.find("src/viewport/viewport_legacy_shortcut_adapter.cpp"), std::string::npos);
    EXPECT_EQ(cmake.find("src/viewport/viewport_legacy_shortcut_adapter.hpp"), std::string::npos);
}

TEST(ViewportInputRouterTests, FlipShortcutsOnlyApplyInObjectSelectionMode) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.modifiers.shift = true;

    router.setSelectionMode(ViewportSelectionMode::Face);
    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::FlipMeshNormals));
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.consumed);

    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::FlipHorizontal));
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.consumed);

    router.setSelectionMode(ViewportSelectionMode::Edge);
    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::FlipVertical));
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.consumed);

    router.setSelectionMode(ViewportSelectionMode::Object);
    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::FlipMeshNormals));
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FlipMeshNormals);

    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::FlipHorizontal));
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FlipHorizontal);
}

TEST(ViewportInputRouterTests, SelectionModeHotkeysMatchViewportShortcuts) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};

    setResolvedShortcut(input, testSelectionShortcut(ViewportSelectionMode::Vertex));
    router.handleFrame(input, camera);
    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Vertex);

    setResolvedShortcut(input, testSelectionShortcut(ViewportSelectionMode::Edge));
    router.handleFrame(input, camera);
    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Edge);

    setResolvedShortcut(input, testSelectionShortcut(ViewportSelectionMode::Face));
    router.handleFrame(input, camera);
    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Face);

    setResolvedShortcut(input, testSelectionShortcut(ViewportSelectionMode::Object));
    router.handleFrame(input, camera);
    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Object);
}

TEST(ViewportInputRouterTests, GridPresetWorldSizesMatchMapUnitContract) {
    ViewportInputRouter router;
    EXPECT_EQ(router.state().gridPreset, viewportDefaultGridPreset());
    EXPECT_EQ(viewportDefaultGridPreset(), 6);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(1), 1.0f / 32.0f, 0.0001f);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(6), 1.0f, 0.0001f);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(10), 16.0f, 0.0001f);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(-4), 1.0f / 32.0f, 0.0001f);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(99), 16.0f, 0.0001f);
}

TEST(ViewportInputRouterTests, GridPresetShortcutReportsOnlyActualChanges) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};

    setResolvedShortcut(input, testGridShortcut(7));
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_TRUE(result.gridPresetChanged);
    EXPECT_EQ(result.requestedGridPreset, 7);
    EXPECT_EQ(router.state().gridPreset, viewportDefaultGridPreset());

    router.syncGridPresetSnapshot(result.requestedGridPreset);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_FALSE(result.gridPresetChanged);
    EXPECT_EQ(result.requestedGridPreset, 7);
    EXPECT_EQ(router.state().gridPreset, 7);
}

TEST(ViewportInputRouterTests, ViewShortcutReportsFocusedPaneRequest) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};

    setResolvedShortcut(input, testViewShortcut(ViewportView::Front));
    const ViewportInputResult result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_TRUE(result.viewChanged);
    EXPECT_EQ(result.requestedView, ViewportView::Front);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::SetView);
}

TEST(ViewportInputRouterTests, ToolActivationSupportTracksNativeControllers) {
    EXPECT_TRUE(viewportToolHasNativeController(ViewportTool::Select));
    EXPECT_TRUE(viewportToolHasNativeController(ViewportTool::Move));
    EXPECT_TRUE(viewportToolHasNativeController(ViewportTool::Rotate));
    EXPECT_TRUE(viewportToolHasNativeController(ViewportTool::Scale));
    EXPECT_EQ(viewportToolHasNativeController(ViewportTool::Box), viewportToolHasHostContract(ViewportTool::Box));
    EXPECT_EQ(viewportToolHasNativeController(ViewportTool::InsertEdgeLoop), viewportToolHasHostContract(ViewportTool::InsertEdgeLoop));
    EXPECT_EQ(viewportToolHasNativeController(ViewportTool::Knife), viewportToolHasHostContract(ViewportTool::Knife));
    EXPECT_EQ(viewportToolHasNativeController(ViewportTool::Cut), viewportToolHasHostContract(ViewportTool::Cut));
    EXPECT_EQ(viewportToolHasNativeController(ViewportTool::Poly), viewportToolHasHostContract(ViewportTool::Poly));
    EXPECT_TRUE(viewportToolHasNativeController(ViewportTool::Mirror));
}

TEST(ViewportInputRouterTests, ModalToolGestureKeysAreNotConsumedByViewShortcuts) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};

    router.setActiveTool(ViewportTool::Cut);
    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::FocusDocument));
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.focusDocumentRequested);

    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::ToggleMaximized));
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.maximizedToggleRequested);

    router.setActiveTool(ViewportTool::Mirror);
    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::ToggleMaximized));
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.maximizedToggleRequested);
}

TEST(ViewportInputRouterTests, ToggleMaximizedShortcutReportsWorkspaceRequest) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::ToggleMaximized));

    const ViewportInputResult result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_TRUE(result.maximizedToggleRequested);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::ToggleMaximized);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Select);
    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Object);
    EXPECT_EQ(router.state().gridPreset, viewportDefaultGridPreset());
}

TEST(ViewportInputRouterTests, FocusGateBlocksViewportToolHotkeys) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = false;
    input.viewportSize = {800.0f, 600.0f};
    setResolvedShortcut(input, testToolShortcut(ViewportTool::Box));

    const auto result = router.handleFrame(input, camera);

    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Select);
}

TEST(ViewportInputRouterTests, EscapeRequestsActiveToolCancel) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::CancelActiveTool));

    const auto result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_TRUE(result.cancelRequested);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::CancelActiveTool);
}

TEST(ViewportInputRouterTests, ExternalSelectionModeSyncUpdatesToolState) {
    ViewportInputRouter router;

    router.setSelectionMode(ViewportSelectionMode::Face);

    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Face);
}

TEST(ViewportInputRouterTests, FocusShortcutsMatchViewportContract) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::FocusDocument));

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_TRUE(result.focusDocumentRequested);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FocusDocument);

    setResolvedShortcut(input, testRouterShortcut(ViewportShortcutKind::FocusOrigin));
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_FALSE(result.focusDocumentRequested);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FocusOrigin);
    EXPECT_FLOAT_EQ(camera.state().distance, 64.0f);
}

TEST(ViewportInputRouterTests, MiddleMouseNavigationAndWheelAreViewportGated) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::Orbit);

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.mouseScreen = {160.0f, 120.0f};
    setNavigationDown(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.cameraChanged);

    setNavigationReleased(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(router.navigating());

    const float distanceBeforeIgnoredWheel = camera.state().distance;
    input = {};
    input.viewportHovered = false;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseWheel = 1.0f;
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.consumed);
    EXPECT_FLOAT_EQ(camera.state().distance, distanceBeforeIgnoredWheel);
}

TEST(ViewportInputRouterTests, AltMiddleMouseStartsOrbitSnapNavigation) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.modifiers.alt = true;
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::OrbitSnap);

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.mouseScreen = {100.0f, 100.0f};
    setNavigationDown(input, ViewportInputActionKind::OrbitSnap);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_FALSE(camera.orthographicViewLocked());
    const QVec3 forward = normalize(camera.pose().target - camera.pose().eye);
    EXPECT_NEAR(forward.x, 0.0f, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, -1.0f, 0.001f);

    setNavigationReleased(input, ViewportInputActionKind::OrbitSnap);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(router.navigating());
}

TEST(ViewportInputRouterTests, MiddleOrbitSamplesAltDuringDragForOrthographicSnap) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::Orbit);

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    ASSERT_TRUE(router.navigating());
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);

    input.modifiers.alt = true;
    input.mouseScreen = {220.0f, 100.0f};
    setNavigationDown(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.cameraChanged);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_FALSE(camera.orthographicViewLocked());

    input.modifiers.alt = false;
    input.mouseScreen = {116.0f, 100.0f};
    setNavigationDown(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.cameraChanged);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
}

TEST(ViewportInputRouterTests, PlainMiddleOrbitAfterAltSnapDoesNotSnap) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.modifiers.alt = true;
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::OrbitSnap);

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationDown(input, ViewportInputActionKind::OrbitSnap);
    result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.cameraChanged);
    ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    ASSERT_FALSE(camera.orthographicViewLocked());

    setNavigationReleased(input, ViewportInputActionKind::OrbitSnap);
    result = router.handleFrame(input, camera);
    ASSERT_FALSE(router.navigating());

    input = {};
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
}

TEST(ViewportInputRouterTests, RightMouseAfterAltSnapPansOrthographicView) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.modifiers.alt = true;
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::OrbitSnap);

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationDown(input, ViewportInputActionKind::OrbitSnap);
    result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.cameraChanged);
    ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    ASSERT_FALSE(camera.orthographicViewLocked());

    setNavigationReleased(input, ViewportInputActionKind::OrbitSnap);
    result = router.handleFrame(input, camera);
    ASSERT_FALSE(router.navigating());

    input = {};
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());
    EXPECT_FALSE(result.flyNavigationActive);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    EXPECT_FALSE(camera.orthographicViewLocked());
}

TEST(ViewportInputRouterTests, LockedOrthographicViewConsumesMiddleOrbitWithoutMovingCamera) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.viewAxis(ViewportViewAxis::Z, 1);
    ASSERT_TRUE(camera.orthographicViewLocked());

    const ViewportCameraState before = camera.state();

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::Orbit);

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.mouseScreen = {180.0f, 140.0f};
    setNavigationDown(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.cameraChanged);
    EXPECT_NEAR(camera.state().target.x, before.target.x, 0.001f);
    EXPECT_NEAR(camera.state().target.y, before.target.y, 0.001f);
    EXPECT_NEAR(camera.state().target.z, before.target.z, 0.001f);
    EXPECT_NEAR(camera.state().yawRadians, before.yawRadians, 0.001f);
    EXPECT_NEAR(camera.state().pitchRadians, before.pitchRadians, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);

    setNavigationReleased(input, ViewportInputActionKind::Orbit);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(router.navigating());
}

TEST(ViewportInputRouterTests, ShiftMiddleMousePansLockedOrthographicView) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.viewAxis(ViewportViewAxis::Z, 1);
    ASSERT_TRUE(camera.orthographicViewLocked());

    const QVec3 targetBefore = camera.state().target;

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.modifiers.shift = true;
    input.mouseScreen = {100.0f, 100.0f};
    setNavigationPressed(input, ViewportInputActionKind::Pan);

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.mouseScreen = {180.0f, 140.0f};
    setNavigationDown(input, ViewportInputActionKind::Pan);
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_GT(length(camera.state().target - targetBefore), 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);
}

TEST(ViewportInputRouterTests, RightMousePansOrthographicViewInsteadOfFlying) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.viewAxis(ViewportViewAxis::Z, 1);
    ASSERT_TRUE(camera.orthographicViewLocked());

    const ViewportCameraState before = camera.state();

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {240.0f, 180.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.mouseScreen = {310.0f, 210.0f};
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    input.deltaSeconds = 0.25f;
    setNavigationDown(input, ViewportInputActionKind::Fly);
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_GT(length(camera.state().target - before.target), 0.001f);
    EXPECT_NEAR(camera.state().yawRadians, before.yawRadians, 0.001f);
    EXPECT_NEAR(camera.state().pitchRadians, before.pitchRadians, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);

    input.flyLocalDirection = {};
    setNavigationReleased(input, ViewportInputActionKind::Fly);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(router.navigating());
}

TEST(ViewportInputRouterTests, RightMouseFlyNavigationConsumesMovementHotkeys) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flyCursorCaptureActive);
    EXPECT_FALSE(result.flyHudActive);

    const ViewportCameraPose beforeMove = camera.pose();
    input.mouseDelta = {40.0f, 0.0f};
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    input.deltaSeconds = 0.25f;
    setNavigationDown(input, ViewportInputActionKind::Fly);
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.cameraChanged);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flyCursorCaptureActive);
    EXPECT_TRUE(result.flyHudActive);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Select);
    EXPECT_GT(length(camera.pose().eye - beforeMove.eye), 1.0f);

    input.flyLocalDirection = {};
    input.mouseDelta = {};
    setNavigationDown(input, ViewportInputActionKind::Fly);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flyCursorCaptureActive);
    EXPECT_TRUE(result.flyHudActive);

    setNavigationReleased(input, ViewportInputActionKind::Fly);
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(router.navigating());
    EXPECT_FALSE(result.flyHudActive);
}

TEST(ViewportInputRouterTests, RightMouseFlyNavigationIgnoresPressFrameMouseDelta) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    input.mouseDelta = {250.0f, -180.0f};
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    input.deltaSeconds = 0.25f;
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    const ViewportCameraPose before = camera.pose();
    const auto result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_FALSE(result.flyHudActive);
    EXPECT_FALSE(result.cameraChanged);
    EXPECT_NEAR(length(camera.pose().eye - before.eye), 0.0f, 0.001f);
    EXPECT_NEAR(
            dot(normalize(camera.pose().target - camera.pose().eye), normalize(before.target - before.eye)),
            1.0f,
            0.001f);
}

TEST(ViewportInputRouterTests, RightMouseFlyLookNearOrthogonalAxisStaysPerspective) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    ASSERT_TRUE(result.flyNavigationActive);

    const ViewportCameraPose before = camera.pose();
    input.mouseDelta = {0.0f, -2.0f};
    input.deltaSeconds = 0.016f;
    setNavigationDown(input, ViewportInputActionKind::Fly);
    result = router.handleFrame(input, camera);

    const ViewportCameraPose after = camera.pose();
    const QVec3 afterForward = normalize(after.target - after.eye);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_NEAR(length(after.eye - before.eye), 0.0f, 0.001f);
    EXPECT_GT(std::abs(afterForward.x), 0.001f);
}

TEST(ViewportInputRouterTests, FlyWheelAdjustsNavigationSpeedInsteadOfZooming) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    const float distanceBeforeWheel = camera.state().distance;
    const float speedBeforeWheel = camera.flySpeed();

    input.mouseWheel = 1.0f;
    input.mouseDelta = {};
    setNavigationDown(input, ViewportInputActionKind::Fly);
    input.navigationBindings.zoomWheel = true;
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flySpeedChanged);
    EXPECT_NEAR(result.flySpeed, speedBeforeWheel + 1.0f, 0.001f);
    EXPECT_NEAR(camera.flySpeed(), speedBeforeWheel + 1.0f, 0.001f);
    EXPECT_NEAR(camera.state().distance, distanceBeforeWheel, 0.001f);

    input.mouseWheel = -2.0f;
    setNavigationDown(input, ViewportInputActionKind::Fly);
    input.navigationBindings.zoomWheel = true;
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.flySpeedChanged);
    EXPECT_NEAR(camera.flySpeed(), speedBeforeWheel - 1.0f, 0.001f);
}

TEST(ViewportInputRouterTests, MiddleMouseResetsFlyNavigationSpeed) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.adjustFlySpeed(5.0f);
    ASSERT_NEAR(camera.flySpeed(), static_cast<float>(viewportHostDefaults().flyBaseSpeed) + 5.0f, 0.001f);
    ASSERT_FALSE(camera.flySpeedIsDefault());

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    setNavigationPressed(input, ViewportInputActionKind::Fly);

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    input.mouseDelta = {};
    setNavigationDown(input, ViewportInputActionKind::Fly);
    input.navigationBindings.resetFlySpeedPressed = true;
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flySpeedChanged);
    EXPECT_TRUE(result.flySpeedReset);
    EXPECT_NEAR(result.flySpeed, static_cast<float>(viewportHostDefaults().flyBaseSpeed), 0.001f);
    EXPECT_NEAR(camera.flySpeed(), static_cast<float>(viewportHostDefaults().flyBaseSpeed), 0.001f);
    EXPECT_TRUE(camera.flySpeedIsDefault());
}

TEST(ViewportInputRouterTests, ShiftRightMouseDoesNotStartFlyNavigation) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.modifiers.shift = true;
    input.mouseScreen = {300.0f, 240.0f};

    const auto result = router.handleFrame(input, camera);

    EXPECT_FALSE(result.consumed);
    EXPECT_FALSE(router.navigating());
}

TEST(AssetRegistryTests, StoresMaterialMeshAndTextureMetadata) {
    AssetRegistry registry;
    const AssetId mesh = registry.addMesh(makeTestTriangleMesh());
    const AssetId material = registry.addMaterial(makeDefaultQuaderMaterial());
    TextureAsset textureAsset;
    textureAsset.name = "Curvature";
    textureAsset.usage = TextureUsage::Curvature;
    textureAsset.colorSpace = TextureColorSpace::Linear;
    textureAsset.width = 512;
    textureAsset.height = 512;
    textureAsset.generated = true;
    const AssetId texture = registry.addTexture(std::move(textureAsset));

    ASSERT_NE(registry.mesh(mesh), nullptr);
    ASSERT_NE(registry.material(material), nullptr);
    ASSERT_NE(registry.texture(texture), nullptr);
    EXPECT_EQ(registry.texture(texture)->usage, TextureUsage::Curvature);
    EXPECT_EQ(registry.texture(texture)->colorSpace, TextureColorSpace::Linear);
}

TEST(AssetRegistryTests, TracksTypedRevisionsAndScopedUpdates) {
    AssetRegistry registry;
    MeshAsset meshAsset = makeTestTriangleMesh("Source");
    const AssetId mesh = registry.addMesh(std::move(meshAsset));
    const AssetId material = registry.addMaterial(makeDefaultQuaderMaterial());
    TextureAsset textureAsset;
    textureAsset.name = "Mask";
    textureAsset.usage = TextureUsage::Mask;
    textureAsset.width = 16;
    textureAsset.height = 16;
    textureAsset.generated = true;
    const AssetId texture = registry.addTexture(std::move(textureAsset));

    EXPECT_EQ(registry.meshRevision(mesh).value, 1u);
    EXPECT_EQ(registry.materialRevision(material).value, 1u);
    EXPECT_EQ(registry.textureRevision(texture).value, 1u);
    EXPECT_EQ(registry.revision(), 3u);

    MeshAsset replacement = makeTestTriangleMesh();
    replacement.id = 999;
    replacement.revision = MeshAssetRevision{99};
    replacement.name = "Replacement";
    ASSERT_TRUE(registry.replaceMesh(mesh, std::move(replacement)));

    const MeshAsset* replacedMesh = registry.mesh(mesh);
    ASSERT_NE(replacedMesh, nullptr);
    EXPECT_EQ(replacedMesh->id, mesh);
    EXPECT_EQ(replacedMesh->revision.value, 2u);
    EXPECT_EQ(replacedMesh->name, "Replacement");
    EXPECT_EQ(registry.revision(), 4u);

    ASSERT_TRUE(registry.updateMesh(mesh, [](MeshAsset& asset) {
        asset.id = 999;
        asset.revision = MeshAssetRevision{99};
        asset.name = "Scoped Mesh";
    }));
    ASSERT_TRUE(registry.updateMaterial(material, [](MaterialDocument& asset) {
        asset.id = 999;
        asset.revision = MaterialAssetRevision{99};
        asset.name = "Scoped Material";
    }));
    ASSERT_TRUE(registry.updateTexture(texture, [](TextureAsset& asset) {
        asset.id = 999;
        asset.revision = TextureAssetRevision{99};
        asset.width = 32;
    }));

    ASSERT_NE(registry.mesh(mesh), nullptr);
    ASSERT_NE(registry.material(material), nullptr);
    ASSERT_NE(registry.texture(texture), nullptr);
    EXPECT_EQ(registry.mesh(mesh)->id, mesh);
    EXPECT_EQ(registry.material(material)->id, material);
    EXPECT_EQ(registry.texture(texture)->id, texture);
    EXPECT_EQ(registry.mesh(mesh)->revision.value, 3u);
    EXPECT_EQ(registry.material(material)->revision.value, 2u);
    EXPECT_EQ(registry.texture(texture)->revision.value, 2u);
    EXPECT_EQ(registry.texture(texture)->width, 32u);
    EXPECT_EQ(registry.revision(), 7u);
    EXPECT_FALSE(registry.updateMesh(InvalidAssetId, [](MeshAsset&) {}));
    EXPECT_EQ(registry.meshRevision(InvalidAssetId).value, 0u);
}

TEST(AssetRegistryTests, RejectsDuplicateExplicitIdsAndEnumeratesAssetsDeterministically) {
    AssetRegistry registry;
    EXPECT_EQ(registry.addMesh(20u, makeTestTriangleMesh("Mesh 20")), 20u);
    EXPECT_EQ(registry.addMesh(10u, makeTestTriangleMesh("Mesh 10")), 10u);
    EXPECT_EQ(registry.addMesh(20u, makeTestTriangleMesh("Duplicate")), InvalidAssetId);
    EXPECT_EQ(registry.meshIds(), std::vector<AssetId>({10u, 20u}));

    TextureAsset textureA;
    textureA.name = "Texture 30";
    TextureAsset textureB;
    textureB.name = "Texture 25";
    EXPECT_EQ(registry.addTexture(30u, std::move(textureA)), 30u);
    EXPECT_EQ(registry.addTexture(25u, std::move(textureB)), 25u);
    TextureAsset duplicateTexture;
    duplicateTexture.name = "Duplicate Texture";
    EXPECT_EQ(registry.addTexture(25u, std::move(duplicateTexture)), InvalidAssetId);
    EXPECT_EQ(registry.textureIds(), std::vector<AssetId>({25u, 30u}));

    MaterialDocument materialA = makeDefaultQuaderMaterial("Material 40");
    MaterialDocument materialB = makeDefaultQuaderMaterial("Material 35");
    EXPECT_EQ(registry.addMaterial(40u, std::move(materialA)), 40u);
    EXPECT_EQ(registry.addMaterial(35u, std::move(materialB)), 35u);
    MaterialDocument duplicateMaterial = makeDefaultQuaderMaterial("Duplicate Material");
    EXPECT_EQ(registry.addMaterial(35u, std::move(duplicateMaterial)), InvalidAssetId);
    EXPECT_EQ(registry.materialIds(), std::vector<AssetId>({35u, 40u}));
}

TEST(AssetRegistryTests, NormalizesMaterialTextureSlotsAtRegistryBoundary) {
    AssetRegistry registry;
    TextureAsset baseColor;
    baseColor.name = "Base Color";
    const AssetId baseColorTexture = registry.addTexture(std::move(baseColor));
    TextureAsset roughness;
    roughness.name = "Roughness";
    const AssetId roughnessTexture = registry.addTexture(std::move(roughness));

    MaterialDocument material = makeDefaultQuaderMaterial("Aliased Material");
    material.textureSlots.clear();
    material.textureSlots.emplace("baseColor", baseColorTexture);

    const AssetId materialId = registry.addMaterial(std::move(material));
    ASSERT_NE(materialId, InvalidAssetId);

    const MaterialDocument* stored = registry.material(materialId);
    ASSERT_NE(stored, nullptr);
    ASSERT_TRUE(stored->textureSlots.contains(MaterialTextureSlot::BaseColor));
    EXPECT_EQ(stored->textureSlots.at(MaterialTextureSlot::BaseColor), baseColorTexture);
    EXPECT_FALSE(stored->textureSlots.contains("baseColor"));

    ASSERT_TRUE(registry.updateMaterial(materialId, [roughnessTexture](MaterialDocument& asset) {
        asset.textureSlots.emplace("roughness", roughnessTexture);
    }));

    stored = registry.material(materialId);
    ASSERT_NE(stored, nullptr);
    ASSERT_TRUE(stored->textureSlots.contains(MaterialTextureSlot::Roughness));
    EXPECT_EQ(stored->textureSlots.at(MaterialTextureSlot::Roughness), roughnessTexture);
    EXPECT_FALSE(stored->textureSlots.contains("roughness"));
}

TEST(AssetRegistryTests, RejectsInvalidMaterialTextureReferencesAtRegistryBoundary) {
    AssetRegistry registry;
    MaterialDocument unsupported = makeDefaultQuaderMaterial("Unsupported");
    unsupported.textureSlots.emplace("normalTexture", 99u);
    EXPECT_EQ(registry.addMaterial(std::move(unsupported)), InvalidAssetId);

    MaterialDocument missing = makeDefaultQuaderMaterial("Missing Texture");
    setMaterialTextureSlot(missing, MaterialTextureSlot::BaseColor, 99u);
    EXPECT_EQ(registry.addMaterial(std::move(missing)), InvalidAssetId);

    TextureAsset texture;
    texture.name = "Valid";
    const AssetId textureId = registry.addTexture(std::move(texture));
    MaterialDocument valid = makeDefaultQuaderMaterial("Valid", textureId, textureId);
    const AssetId materialId = registry.addMaterial(std::move(valid));
    ASSERT_NE(materialId, InvalidAssetId);

    EXPECT_FALSE(registry.updateMaterial(materialId, [](MaterialDocument& asset) {
        asset.textureSlots.emplace("unsupported", 5u);
    }));
    ASSERT_NE(registry.material(materialId), nullptr);
    EXPECT_FALSE(registry.material(materialId)->textureSlots.contains("unsupported"));
}

TEST(AssetRegistryTests, MaterialTextureMutationOwnsSlotTextureAndReplacementValidation) {
    AssetRegistry registry;
    const AssetId material = registry.addMaterial(makeDefaultQuaderMaterial("Mutable Material"));
    TextureAsset textureAsset;
    textureAsset.name = "Base Color";
    const AssetId texture = registry.addTexture(std::move(textureAsset));
    ASSERT_NE(material, InvalidAssetId);
    ASSERT_NE(texture, InvalidAssetId);

    AssetRegistryMutationResult result = registry.setMaterialTexture(material, "baseColor", texture);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    EXPECT_EQ(result.message, "Material updated");
    ASSERT_NE(registry.material(material), nullptr);
    ASSERT_TRUE(registry.material(material)->textureSlots.contains(MaterialTextureSlot::BaseColor));
    EXPECT_EQ(registry.material(material)->textureSlots.at(MaterialTextureSlot::BaseColor), texture);
    EXPECT_FALSE(registry.material(material)->textureSlots.contains("baseColor"));

    result = registry.setMaterialTexture(material, MaterialTextureSlot::BaseColor, texture);
    EXPECT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);
    EXPECT_EQ(result.message, "Material unchanged");

    result = registry.setMaterialTexture(material, "", texture);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "Material texture slot is empty");

    result = registry.setMaterialTexture(material, "normalTexture", texture);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "Unsupported material texture slot");

    result = registry.setMaterialTexture(material, MaterialTextureSlot::BaseColor, texture + 1000u);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "Texture asset does not exist");

    result = registry.setMaterialTexture(material + 1000u, MaterialTextureSlot::BaseColor, texture);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "Material asset does not exist");
}

TEST(AssetProjectArchitectureTests, TypedStoreAliasesStampDocumentsWithoutAssetRegistryCeremony) {
    static_assert(std::is_same_v<MeshAssetStore, TypedAssetStore<MeshAsset, MeshAssetRevision>>);
    static_assert(std::is_same_v<MaterialAssetStore, TypedAssetStore<MaterialDocument, MaterialAssetRevision>>);
    static_assert(std::is_same_v<TextureAssetStore, TypedAssetStore<TextureAsset, TextureAssetRevision>>);

    MeshAssetStore meshes;
    MeshAsset mesh;
    mesh.name = "Mesh Source";
    const AssetId meshId = meshes.add(41, std::move(mesh));
    ASSERT_EQ(meshId, 41u);
    ASSERT_NE(meshes.get(meshId), nullptr);
    EXPECT_EQ(meshes.get(meshId)->id, meshId);
    EXPECT_EQ(meshes.get(meshId)->revision.value, 1u);
    EXPECT_EQ(meshes.get(meshId)->name, "Mesh Source");

    ASSERT_TRUE(meshes.update(meshId, [](MeshAsset& asset) {
        asset.name = "Mesh Updated";
    }));
    ASSERT_NE(meshes.get(meshId), nullptr);
    EXPECT_EQ(meshes.get(meshId)->revision.value, 2u);
    EXPECT_EQ(meshes.get(meshId)->name, "Mesh Updated");

    MaterialAssetStore materials;
    const AssetId materialId = materials.add(7, makeDefaultQuaderMaterial("Material Source"));
    ASSERT_NE(materials.get(materialId), nullptr);
    EXPECT_EQ(materials.get(materialId)->revision.value, 1u);
    EXPECT_EQ(materials.get(materialId)->name, "Material Source");

    TextureAssetStore textures;
    TextureAsset texture;
    texture.name = "Texture Source";
    texture.usage = TextureUsage::Mask;
    const AssetId textureId = textures.add(9, std::move(texture));
    ASSERT_NE(textures.get(textureId), nullptr);
    EXPECT_EQ(textures.get(textureId)->revision.value, 1u);
    EXPECT_EQ(textures.get(textureId)->usage, TextureUsage::Mask);
}

TEST(ProjectDocumentTests, OwnsSceneAssetsAndStableObjectRecords) {
    ProjectDocument document(ProjectDocumentRecord{
        .id = 17,
        .name = "Authored Project",
    });

    MeshAsset meshAsset = makeTestTriangleMesh("Authored Mesh");
    const AssetId mesh = document.assets().addMesh(std::move(meshAsset));
    const EntityId entity = document.scene().createEntity("Authored Mesh");
    document.scene().setMesh(entity, MeshComponent{.mesh = mesh});

    const EditorObjectId object = document.createObjectRecord(ProjectObjectRecord{
        .sceneEntity = entity,
        .mesh = mesh,
        .authoredName = "Authored Mesh",
    });

    ASSERT_NE(object, InvalidEditorObjectId);
    EXPECT_EQ(document.id(), 17u);
    EXPECT_EQ(document.name(), "Authored Project");
    EXPECT_EQ(document.revision(), 1u);
    const std::optional<ProjectObjectRecord> record = document.objectRecord(object);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->id, object);
    EXPECT_EQ(record->sceneEntity, entity);
    EXPECT_EQ(record->mesh, mesh);
    EXPECT_EQ(record->authoredName, "Authored Mesh");
    ASSERT_NE(document.scene().mesh(entity), nullptr);
    EXPECT_EQ(document.scene().mesh(entity)->mesh, mesh);

    EXPECT_EQ(document.upsertObjectRecord(ProjectObjectRecord{
        .id = 42,
        .sceneEntity = entity,
        .mesh = mesh,
        .authoredName = "Imported Stable Record",
    }), 42u);
    EXPECT_EQ(document.objectIdForEntity(entity), 42u);
    EXPECT_EQ(document.createObjectRecord(ProjectObjectRecord{}), 43u);
    const EntityId sceneRoot = document.scene().sceneRoot();
    const std::vector<ProjectObjectRecord> records = document.objectRecords();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].sceneEntity, sceneRoot);
    EXPECT_EQ(records[0].authoredName, "scene");
    EXPECT_EQ(records[1].id, 42u);
    EXPECT_EQ(records[2].id, 43u);

    document.rename("Renamed Project");
    EXPECT_EQ(document.name(), "Renamed Project");
    EXPECT_EQ(document.revision(), 4u);
}

TEST(ProjectDocumentTests, ValidatesAssetReferencesAtDocumentOwner) {
    ProjectDocument document(ProjectDocumentRecord{
            .id = 21,
            .name = "Reference Validation Project",
    });
    const EntityId entity = document.scene().createEntity("Missing Mesh", EntityKind::Mesh);
    document.scene().setMesh(entity, MeshComponent{.mesh = 404u});
    document.scene().setMaterial(entity, MaterialComponent{
            .material = 405u,
            .materialSlots = {{2u, 406u}},
    });

    const ProjectDocumentAssetReferenceReport sceneReport = document.validateAssetReferences();
    ASSERT_FALSE(sceneReport.ok());
    ASSERT_GE(sceneReport.issues.size(), 3u);
    EXPECT_NE(sceneReport.issues[0].message.find("missing mesh asset"), std::string::npos);
    EXPECT_NE(sceneReport.issues[1].message.find("missing material asset"), std::string::npos);

    const ProjectDocumentAssetReferenceReport recordReport = document.validateAssetReferences(ProjectObjectRecord{
            .id = 99u,
            .sceneEntity = InvalidEntityId,
            .mesh = 404u,
            .material = 405u,
    });
    ASSERT_FALSE(recordReport.ok());
    EXPECT_NE(recordReport.issues.front().message.find("object record points at a missing scene entity"),
            std::string::npos);

    const std::string projectDocumentSource = readSourceFile("src/scene/document/project_document.cpp");
    const std::string qdrReaderSource = readSourceFile("src/io/qdr/qdr_document_reader.cpp");
    EXPECT_NE(projectDocumentSource.find("ProjectDocument::validateAssetReferences"), std::string::npos);
    EXPECT_NE(qdrReaderSource.find("document.validateAssetReferences()"), std::string::npos);
    EXPECT_NE(qdrReaderSource.find("document.validateAssetReferences(record)"), std::string::npos);
    EXPECT_EQ(qdrReaderSource.find("document.assets().mesh(mesh->mesh)"), std::string::npos);
    EXPECT_EQ(qdrReaderSource.find("document.assets().material(material->material)"), std::string::npos);
}

TEST(ProjectDocumentTests, RejectsEditorObjectIdCollisionsBetweenSceneEntities) {
    ProjectDocument document(ProjectDocumentRecord{
            .id = 19,
            .name = "Identity Collision Project",
    });

    const EntityId first = document.scene().createEntity("First", EntityKind::Mesh);
    const EntityId second = document.scene().createEntity("Second", EntityKind::Mesh);
    const EditorObjectId firstObject = document.scene().editorObjectId(first);
    const EditorObjectId secondObject = document.scene().editorObjectId(second);
    ASSERT_NE(firstObject, InvalidEditorObjectId);
    ASSERT_NE(secondObject, InvalidEditorObjectId);
    ASSERT_NE(firstObject, secondObject);

    EXPECT_EQ(document.upsertObjectRecord(ProjectObjectRecord{
            .id = firstObject,
            .sceneEntity = second,
            .authoredName = "Bad Collision",
    }), InvalidEditorObjectId);
    EXPECT_EQ(document.scene().editorObjectId(first), firstObject);
    EXPECT_EQ(document.scene().editorObjectId(second), secondObject);
    EXPECT_EQ(document.entityForObjectId(firstObject), first);
    EXPECT_EQ(document.entityForObjectId(secondObject), second);

    const ProjectDocumentIdentityReport report = document.validateObjectIdentityInvariants();
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? std::string{} : report.issues.front().message);
}

TEST(ProjectDocumentTests, MirrorsEverySceneEntityIntoStableEditorObjectRecords) {
    ProjectDocument document(ProjectDocumentRecord{
            .id = 18,
            .name = "Identity Project",
    });

    const EntityId mesh = document.scene().createEntity("Mesh", EntityKind::Mesh);
    const EntityId group = document.scene().createEmpty("Group");
    ASSERT_TRUE(document.scene().setParent(mesh, group, false));

    const EditorObjectId meshObject = document.scene().editorObjectId(mesh);
    const EditorObjectId groupObject = document.scene().editorObjectId(group);
    const EditorObjectId rootObject = document.scene().editorObjectId(document.scene().sceneRoot());
    ASSERT_NE(rootObject, InvalidEditorObjectId);
    ASSERT_NE(meshObject, InvalidEditorObjectId);
    ASSERT_NE(groupObject, InvalidEditorObjectId);
    EXPECT_NE(rootObject, meshObject);
    EXPECT_NE(rootObject, groupObject);
    EXPECT_NE(meshObject, groupObject);

    const std::vector<ProjectObjectRecord> records = document.objectRecords();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(document.objectIdForEntity(document.scene().sceneRoot()), rootObject);
    EXPECT_EQ(document.objectIdForEntity(mesh), meshObject);
    EXPECT_EQ(document.objectIdForEntity(group), groupObject);
    EXPECT_EQ(document.entityForObjectId(rootObject), document.scene().sceneRoot());
    EXPECT_EQ(document.entityForObjectId(meshObject), mesh);
    EXPECT_EQ(document.entityForObjectId(groupObject), group);
    EXPECT_TRUE(document.objectRecord(meshObject).has_value());
    const std::optional<ProjectObjectRecord> groupRecord = document.objectRecord(groupObject);
    ASSERT_TRUE(groupRecord.has_value());
    EXPECT_EQ(groupRecord->modelingObject, InvalidModelingObjectId);

    const ProjectDocumentIdentityReport report = document.validateObjectIdentityInvariants();
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? std::string{} : report.issues.front().message);
}

TEST(ProjectDocumentTests, SceneEntitiesReserveStableObjectIdsFromProjectDocumentAuthority) {
    ProjectDocument document(ProjectDocumentRecord{
            .id = 20,
            .name = "Identity Authority Project",
    });

    const EntityId first = document.scene().createEntity("First", EntityKind::Mesh);
    ASSERT_NE(first, InvalidEntityId);
    EXPECT_EQ(document.scene().editorObjectId(first), 2u);

    const EditorObjectId detached = document.createObjectRecord(ProjectObjectRecord{
            .authoredName = "Detached Object Record",
    });
    EXPECT_EQ(detached, 3u);

    const EntityId second = document.scene().createEntity("Second", EntityKind::Mesh);
    ASSERT_NE(second, InvalidEntityId);
    EXPECT_EQ(document.scene().editorObjectId(second), 4u);

    const EntityId imported = document.scene().createEntityWithId(50u, "Imported", EntityKind::Mesh, 99u);
    ASSERT_NE(imported, InvalidEntityId);
    EXPECT_EQ(document.scene().editorObjectId(imported), 99u);

    const EditorObjectId afterImported = document.createObjectRecord(ProjectObjectRecord{
            .authoredName = "After Imported",
    });
    EXPECT_EQ(afterImported, 100u);

    ProjectDocument moved(std::move(document));
    const EntityId afterMove = moved.scene().createEntity("After Move", EntityKind::Mesh);
    ASSERT_NE(afterMove, InvalidEntityId);
    EXPECT_EQ(moved.scene().editorObjectId(afterMove), 101u);

    const ProjectDocumentIdentityReport report = moved.validateObjectIdentityInvariants();
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? std::string{} : report.issues.front().message);
}

TEST(ProjectDocumentTests, ConstObjectRecordQueriesDoNotMutateProjectRevision) {
    ProjectDocument document(ProjectDocumentRecord{
            .id = 21,
            .name = "Const Projection Project",
    });
    const EntityId entity = document.scene().createEntity("Projected Mesh", EntityKind::Mesh);
    const EditorObjectId object = document.scene().editorObjectId(entity);
    ASSERT_NE(object, InvalidEditorObjectId);
    const std::uint64_t revisionBeforeQueries = document.revision();

    const std::vector<ProjectObjectRecord> records = std::as_const(document).objectRecords();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records.front().sceneEntity, document.scene().sceneRoot());
    EXPECT_EQ(records.back().id, object);
    const std::optional<ProjectObjectRecord> record = std::as_const(document).objectRecord(object);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->sceneEntity, entity);
    EXPECT_EQ(std::as_const(document).objectIdForEntity(entity), object);
    EXPECT_EQ(std::as_const(document).entityForObjectId(object), entity);
    EXPECT_TRUE(std::as_const(document).validateObjectIdentityInvariants().ok());
    EXPECT_EQ(document.revision(), revisionBeforeQueries);

    EXPECT_TRUE(document.synchronizeObjectRecordsWithScene());
    EXPECT_EQ(document.revision(), revisionBeforeQueries + 1u);
    EXPECT_FALSE(document.synchronizeObjectRecordsWithScene());
    EXPECT_EQ(document.revision(), revisionBeforeQueries + 1u);
}

TEST(ProjectDocumentTests, ExplicitObjectRecordSyncParticipatesInProjectSessionDirtyState) {
    ProjectSession session;
    const EntityId entity = session.scene().createEntity("Session Projected Mesh", EntityKind::Mesh);
    ASSERT_NE(entity, InvalidEntityId);
    session.markClean();
    EXPECT_FALSE(session.dirty());

    EXPECT_EQ(std::as_const(session.document()).objectRecords().size(), 2u);
    EXPECT_FALSE(session.dirty());

    EXPECT_TRUE(session.document().synchronizeObjectRecordsWithScene());
    EXPECT_TRUE(session.dirty());
    session.markClean();
    EXPECT_FALSE(session.dirty());
    EXPECT_FALSE(session.document().synchronizeObjectRecordsWithScene());
    EXPECT_FALSE(session.dirty());
}

TEST(ProjectSessionTests, DirtyStateAggregatesProjectSceneAssetAndModelingContentRevisions) {
    ProjectSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::fromProjectSession(session));

    EXPECT_FALSE(editor.project().dirty());

    session.document().rename("Dirty Project");
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    QuaderCreateEntityArgs entityArgs;
    entityArgs.name = "Dirty Entity";
    const QuaderApiResult<QuaderEntityRef> entity = editor.scene().createEntity(entityArgs);
    ASSERT_TRUE(entity.ok()) << entity.errorMessage();
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    QuaderTransform moved;
    moved.position = {1.0f, 2.0f, 3.0f};
    const QuaderApiVoidResult transformed = editor.scene().setTransform(entity.value().id, moved);
    ASSERT_TRUE(transformed.ok()) << transformed.errorMessage();
    EXPECT_TRUE(transformed.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const QuaderApiVoidResult hidden = editor.scene().setVisible(entity.value().id, false);
    ASSERT_TRUE(hidden.ok()) << hidden.errorMessage();
    EXPECT_TRUE(hidden.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const QuaderApiVoidResult locked = editor.scene().setLocked(entity.value().id, true);
    ASSERT_TRUE(locked.ok()) << locked.errorMessage();
    EXPECT_TRUE(locked.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    QuaderMaterialArgs materialArgs;
    materialArgs.name = "Dirty Material";
    materialArgs.baseColor = {0.8f, 0.7f, 0.6f};
    const QuaderApiResult<QuaderAssetRef> material = editor.assets().addMaterial(materialArgs);
    ASSERT_TRUE(material.ok()) << material.errorMessage();
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const QuaderApiVoidResult assigned =
            editor.assets().assignMaterialToEntity(entity.value().id, material.value().id);
    ASSERT_TRUE(assigned.ok()) << assigned.errorMessage();
    EXPECT_TRUE(assigned.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const QuaderApiVoidResult recolored =
            editor.assets().setMaterialBaseColor(material.value().id, {0.1f, 0.2f, 0.3f});
    ASSERT_TRUE(recolored.ok()) << recolored.errorMessage();
    EXPECT_TRUE(recolored.changed());
    EXPECT_TRUE(editor.project().dirty());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const QuaderApiResult<QuaderModelingObjectRef> box =
            editor.modeling().createBox(
                    QuaderBounds{.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
                    "Dirty Modeling Box");
    ASSERT_TRUE(box.ok()) << box.errorMessage();
    ASSERT_TRUE(editor.selection().selectObject(box.value()).ok());
    session.markClean();
    EXPECT_FALSE(editor.project().dirty());

    const QuaderApiVoidResult movedSelection = editor.modeling().moveSelection({0.25f, 0.0f, 0.0f});
    ASSERT_TRUE(movedSelection.ok()) << movedSelection.errorMessage();
    EXPECT_TRUE(movedSelection.changed());
    EXPECT_TRUE(editor.project().dirty());
}

TEST(ProjectSessionTests, ReplaceDocumentAdvancesSceneRevisionPastPreviousDocument) {
    ProjectSession session;
    for (int i = 0; i < 8; ++i) {
        const EntityId entity = session.scene().createEntity("Previous Entity " + std::to_string(i));
        ASSERT_NE(entity, InvalidEntityId);
    }
    const std::uint64_t previousSceneRevision = session.scene().revision();
    session.markClean();
    EXPECT_FALSE(session.dirty());

    ProjectDocument replacement;
    const EntityId light = replacement.scene().createEntity("Loaded Light", EntityKind::Light);
    ASSERT_NE(light, InvalidEntityId);
    replacement.scene().setLight(light, LightComponent{.type = LightType::Point});
    ASSERT_LT(replacement.scene().revision(), previousSceneRevision);

    session.replaceDocument(std::move(replacement));

    EXPECT_GT(session.scene().revision(), previousSceneRevision);
    EXPECT_FALSE(session.dirty());
    ASSERT_NE(session.scene().light(light), nullptr);
    EXPECT_EQ(session.scene().light(light)->type, LightType::Point);
}

TEST(ProjectSessionTests, PublicHistoryIgnoresUntrackedModelingSessionHistory) {
    ProjectSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::fromProjectSession(session));
    const auto entityNamed = [&](std::string_view name) {
        const std::vector<QuaderEntitySnapshot> entities = editor.query().entities();
        return std::find_if(
                entities.begin(),
                entities.end(),
                [&](const QuaderEntitySnapshot& snapshot) {
                    return snapshot.name == name;
                }) != entities.end();
    };

    const auto hidden = session.modeling().createBoxFromBounds({
            .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
            .name = "Hidden Modeling Box",
    });
    ASSERT_TRUE(hidden.ok) << hidden.message;
    ASSERT_TRUE(entityNamed("Hidden Modeling Box"));

    EXPECT_FALSE(editor.history().canUndo());
    EXPECT_FALSE(editor.history().canRedo());

    const QuaderApiVoidResult directUndo = editor.history().undo();
    EXPECT_FALSE(directUndo.ok());
    EXPECT_FALSE(directUndo.changed());
    EXPECT_EQ(directUndo.errorMessage(), "No command to undo");
    EXPECT_TRUE(entityNamed("Hidden Modeling Box"));

    const QuaderApiVoidResult commandUndo = editor.commands().execute(QuaderCommand::HistoryUndo);
    EXPECT_FALSE(commandUndo.ok());
    EXPECT_FALSE(commandUndo.changed());
    EXPECT_EQ(commandUndo.errorMessage(), "Command is disabled.");
    EXPECT_EQ(commandUndo.revisions.sceneRevision, editor.revisions().sceneRevision);
    EXPECT_EQ(commandUndo.revisions.modelingContentRevision, editor.revisions().modelingContentRevision);
    EXPECT_TRUE(entityNamed("Hidden Modeling Box"));

    const QuaderApiVoidResult unknownTool = editor.tools().activate("__missing_tool__");
    EXPECT_FALSE(unknownTool.ok());
    EXPECT_EQ(unknownTool.errorMessage(), "Unknown viewport tool.");
    EXPECT_EQ(unknownTool.revisions.sceneRevision, editor.revisions().sceneRevision);
    EXPECT_EQ(unknownTool.revisions.modelingContentRevision, editor.revisions().modelingContentRevision);
}

TEST(ProjectSessionTests, ServiceCreatesSessionsWithDocumentAssetsAndCommandsWiredTogether) {
    ProjectSessionService service;
    std::unique_ptr<ProjectSession> session = service.createSession(ProjectDocumentRecord{
            .id = 42,
            .name = "Architecture Session",
    });
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->document().id(), 42u);
    EXPECT_EQ(session->document().name(), "Architecture Session");
    EXPECT_EQ(&session->scene(), &session->document().scene());
    EXPECT_EQ(&session->assets(), &session->document().assets());

    const AssetId material = session->assets().addMaterial(makeDefaultQuaderMaterial("Session Material"));
    ASSERT_NE(session->document().assets().material(material), nullptr);
    EXPECT_EQ(session->document().assets().material(material)->name, "Session Material");

    const DocumentCreateEntityResult result = session->documentCommands().createEntity(
            "Session Entity",
            EntityKind::Mesh,
            InvalidEntityId,
            {});
    ASSERT_TRUE(result.ok) << result.message;
    const EntityId entity = result.entity;
    ASSERT_NE(entity, InvalidEntityId);
    EXPECT_TRUE(session->scene().exists(entity));
    EXPECT_TRUE(session->history().canUndo());
    ASSERT_TRUE(session->history().undo().ok);
    EXPECT_FALSE(session->scene().exists(entity));

    ProjectDocument existing(ProjectDocumentRecord{
            .id = 99,
            .name = "Opened Session",
    });
    const EntityId authoredEntity = existing.scene().createEntity("Authored Mesh");
    const EditorObjectId object = existing.createObjectRecord(ProjectObjectRecord{
            .sceneEntity = authoredEntity,
            .authoredName = "Authored Mesh",
    });
    std::unique_ptr<ProjectSession> opened = service.openSession(std::move(existing));
    ASSERT_NE(opened, nullptr);
    EXPECT_EQ(opened->document().id(), 99u);
    EXPECT_EQ(opened->document().name(), "Opened Session");
    EXPECT_TRUE(opened->document().objectRecord(object).has_value());
    EXPECT_EQ(opened->document().entityForObjectId(object), authoredEntity);
}

TEST(AssetProjectArchitectureTests, SourceExposesNamedAssetAndProjectArchitectureTypes) {
    const std::string assetDocumentSource = readSourceFile("src/assets/asset_document.hpp");
    const std::string typedStoreSource = readSourceFile("src/assets/typed_asset_store.hpp");
    const std::string registrySource = readSourceFile("src/assets/asset_registry.hpp");
    const std::string projectDocumentSource = readSourceFile("src/scene/document/project_document.hpp");
    const std::string projectSessionSource = readSourceFile("src/scene/document/project_session.hpp");

    EXPECT_NE(assetDocumentSource.find("struct AssetDocumentBase"), std::string::npos);
    EXPECT_NE(typedStoreSource.find("class TypedAssetStore"), std::string::npos);
    EXPECT_EQ(typedStoreSource.find("    Document* get(AssetId id) {"), std::string::npos);
    EXPECT_NE(typedStoreSource.find("const Document* get(AssetId id) const"), std::string::npos);
    EXPECT_NE(registrySource.find("using MeshAssetStore = TypedAssetStore<MeshAsset, MeshAssetRevision>;"),
              std::string::npos);
    EXPECT_NE(
            registrySource.find(
                    "using MaterialAssetStore = TypedAssetStore<MaterialDocument, MaterialAssetRevision>;"),
            std::string::npos);
    EXPECT_NE(registrySource.find("using TextureAssetStore = TypedAssetStore<TextureAsset, TextureAssetRevision>;"),
              std::string::npos);
    EXPECT_EQ(registrySource.find("class MeshAssetStore"), std::string::npos);
    EXPECT_EQ(registrySource.find("class MaterialAssetStore"), std::string::npos);
    EXPECT_EQ(registrySource.find("class TextureAssetStore"), std::string::npos);
    EXPECT_NE(registrySource.find("MeshAssetStore meshes_"), std::string::npos);
    EXPECT_NE(registrySource.find("MaterialAssetStore materials_"), std::string::npos);
    EXPECT_NE(registrySource.find("TextureAssetStore textures_"), std::string::npos);
    EXPECT_EQ(registrySource.find("    MeshAsset* mesh(AssetId id);"), std::string::npos);
    EXPECT_EQ(registrySource.find("    MaterialDocument* material(AssetId id);"), std::string::npos);
    EXPECT_EQ(registrySource.find("    TextureAsset* texture(AssetId id);"), std::string::npos);
    EXPECT_NE(registrySource.find("const MeshAsset* mesh(AssetId id) const"), std::string::npos);
    EXPECT_NE(registrySource.find("const MaterialDocument* material(AssetId id) const"), std::string::npos);
    EXPECT_NE(registrySource.find("const TextureAsset* texture(AssetId id) const"), std::string::npos);
    EXPECT_NE(projectDocumentSource.find("bool synchronizeObjectRecordsWithScene()"), std::string::npos);
    EXPECT_NE(projectDocumentSource.find("bool syncObjectRecordsWithScene()"), std::string::npos);
    EXPECT_EQ(projectDocumentSource.find("syncObjectRecordsWithScene() const"), std::string::npos);
    EXPECT_EQ(projectDocumentSource.find("mutable std::unordered_map"), std::string::npos);
    EXPECT_NE(projectSessionSource.find("class ProjectSession"), std::string::npos);
    EXPECT_NE(projectSessionSource.find("class ProjectSessionService final"), std::string::npos);
    EXPECT_NE(projectSessionSource.find("openSession(ProjectDocument document)"), std::string::npos);
}

TEST(DocumentCommandServiceTests, RecordsUndoRedoForNativeSceneEdits) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);

    DocumentCommandResult result = commands.renameEntity(entity, "Brush_1");
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");
    EXPECT_TRUE(history.canUndo());
    EXPECT_FALSE(history.canRedo());
    ASSERT_NE(history.undoLabel(), nullptr);
    EXPECT_EQ(*history.undoLabel(), "Rename Entity");

    result = history.undo();
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush");
    EXPECT_FALSE(history.canUndo());
    EXPECT_TRUE(history.canRedo());

    result = history.redo();
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");

    TransformComponent transform;
    transform.position = {3.0f, 2.0f, 1.0f};
    ASSERT_TRUE(commands.setTransform(entity, transform).ok);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 3.0f);
    EXPECT_TRUE(history.undo().ok);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
}

TEST(EditorCommandServiceTests, ExecutesCommandsThroughSharedUndoStack) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    EditorHistoryService history;
    DocumentCommandService documentCommands(scene, history);
    EditorCommandService editorCommands(history, documentCommands);

    CommandResult result = editorCommands.execute(
            std::make_unique<RenameEntityCommand>(scene, entity, "Brush", "Brush_1"));
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");
    EXPECT_TRUE(editorCommands.canUndo());

    result = editorCommands.undo();
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush");
    EXPECT_TRUE(editorCommands.canRedo());

    result = editorCommands.redo();
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");
}

TEST(EditorHistoryServiceTests, TransactionsRecordAlreadyExecutedCommandsAsOneUndoItem) {
    EditorHistoryService history;
    int first = 0;
    int second = 0;

    EditorTransaction transaction = history.beginTransaction("Two Edits");
    ASSERT_TRUE(transaction.execute(std::make_unique<TestValueCommand>("First", first, 0, 1)).ok);
    ASSERT_TRUE(transaction.execute(std::make_unique<TestValueCommand>("Second", second, 0, 2)).ok);
    CommandResult result = transaction.commit();
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(history.canUndo());
    ASSERT_NE(history.undoLabel(), nullptr);
    EXPECT_EQ(*history.undoLabel(), "Two Edits");

    ASSERT_TRUE(history.undo().ok);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 0);
    EXPECT_TRUE(history.canRedo());

    ASSERT_TRUE(history.redo().ok);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
}

TEST(DocumentCommandTests, TypedCommandsExecuteDirectlyThroughUndoRedoManager) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    UndoRedoManager undoRedo;

    UndoRedoResult result = undoRedo.execute(
            std::make_unique<RenameEntityCommand>(scene, entity, "Brush", "Brush_1"));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");
    ASSERT_NE(undoRedo.undoLabel(), nullptr);
    EXPECT_EQ(*undoRedo.undoLabel(), "Rename Entity");

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_EQ(scene.name(entity)->name, "Brush");
    ASSERT_TRUE(undoRedo.redo().ok);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");

    TransformComponent beforeTransform = *scene.transform(entity);
    TransformComponent afterTransform = beforeTransform;
    afterTransform.position = {4.0f, 5.0f, 6.0f};

    std::vector<std::unique_ptr<IUndoableCommand>> batchCommands;
    batchCommands.push_back(std::make_unique<SetVisibilityCommand>(scene, entity, true, false));
    batchCommands.push_back(std::make_unique<SetLockCommand>(scene, entity, false, true));
    batchCommands.push_back(std::make_unique<ApplyEntityTransformCommand>(
            scene,
            entity,
            beforeTransform,
            afterTransform,
            "Set Transform"));
    result = undoRedo.execute(
            std::make_unique<BatchDocumentCommand>("Batch Edit", std::move(batchCommands)));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(scene.visibility(entity)->visible);
    EXPECT_TRUE(scene.lock(entity)->locked);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 4.0f);
    ASSERT_NE(undoRedo.undoLabel(), nullptr);
    EXPECT_EQ(*undoRedo.undoLabel(), "Batch Edit");

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(scene.visibility(entity)->visible);
    EXPECT_FALSE(scene.lock(entity)->locked);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);

    int value = 0;
    result = undoRedo.execute(std::make_unique<TestValueCommand>("Value Edit", value, 0, 1));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(value, 1);
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_EQ(value, 0);
}

TEST(DocumentCommandTests, UndoRedoManagerCoalescesOnlyExplicitMergeKeys) {
    int value = 0;
    UndoRedoManager undoRedo;

    UndoRedoResult result = undoRedo.execute(
            std::make_unique<TestValueCommand>("Set Value", value, 0, 1, nullptr, nullptr, true, true, "value.drag"),
            UndoRedoExecuteOptions{.mergeKey = "value.drag"});
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(value, 1);

    result = undoRedo.execute(
            std::make_unique<TestValueCommand>("Set Value", value, 1, 2, nullptr, nullptr, true, true, "value.drag"),
            UndoRedoExecuteOptions{.mergeKey = "value.drag"});
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(value, 2);

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(undoRedo.canUndo());
    ASSERT_TRUE(undoRedo.redo().ok);
    EXPECT_EQ(value, 2);

    result = undoRedo.execute(std::make_unique<TestValueCommand>("Set Value", value, 2, 3));
    ASSERT_TRUE(result.ok) << result.message;
    result = undoRedo.execute(std::make_unique<TestValueCommand>("Set Value", value, 3, 4));
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_EQ(value, 3);
}

TEST(DocumentCommandTests, EditorTransactionsCoalesceExplicitMergeKeysBeforeCommit) {
    int value = 0;
    EditorHistoryService history;

    EditorTransaction transaction = history.beginTransaction("Value Drag");
    ASSERT_TRUE(transaction.execute(
            std::make_unique<TestValueCommand>("Set Value", value, 0, 1, nullptr, nullptr, true, true, "value.drag"),
            UndoRedoExecuteOptions{.mergeKey = "value.drag"})
                    .ok);
    ASSERT_TRUE(transaction.execute(
            std::make_unique<TestValueCommand>("Set Value", value, 1, 2, nullptr, nullptr, true, true, "value.drag"),
            UndoRedoExecuteOptions{.mergeKey = "value.drag"})
                    .ok);
    ASSERT_TRUE(transaction.commit().ok);

    EXPECT_EQ(value, 2);
    ASSERT_TRUE(history.undo().ok);
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(history.canUndo());
    ASSERT_TRUE(history.redo().ok);
    EXPECT_EQ(value, 2);
}

TEST(DocumentCommandTests, SceneBeforeAfterCommandsSupportExplicitMergeKeys) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    UndoRedoManager undoRedo;

    TransformComponent before = *scene.transform(entity);
    TransformComponent middle = before;
    middle.position.x = 1.0f;
    TransformComponent after = before;
    after.position.x = 2.0f;

    UndoRedoResult result = undoRedo.execute(
            std::make_unique<ApplyEntityTransformCommand>(scene, entity, before, middle, "Set Transform"),
            UndoRedoExecuteOptions{.mergeKey = "transform.drag"});
    ASSERT_TRUE(result.ok) << result.message;
    result = undoRedo.execute(
            std::make_unique<ApplyEntityTransformCommand>(scene, entity, middle, after, "Set Transform"),
            UndoRedoExecuteOptions{.mergeKey = "transform.drag"});
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
    EXPECT_FALSE(undoRedo.canUndo());
    ASSERT_TRUE(undoRedo.redo().ok);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);

    TransformComponent third = after;
    third.position.x = 3.0f;
    TransformComponent fourth = after;
    fourth.position.x = 4.0f;

    result = undoRedo.execute(
            std::make_unique<ApplyEntityTransformCommand>(scene, entity, after, third, "Set Transform"),
            UndoRedoExecuteOptions{.mergeKey = "transform.drag.second"});
    ASSERT_TRUE(result.ok) << result.message;
    result = undoRedo.execute(
            std::make_unique<ApplyEntityTransformCommand>(scene, entity, third, fourth, "Set Transform"),
            UndoRedoExecuteOptions{.mergeKey = "transform.drag.second"});
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 4.0f);

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
}

TEST(DocumentCommandTests, BatchDocumentCommandRollsBackFailedRedoAndUndo) {
    UndoRedoManager undoRedo;

    int first = 0;
    int second = 0;
    bool failSecondRedo = true;
    std::vector<std::unique_ptr<IUndoableCommand>> redoCommands;
    redoCommands.push_back(std::make_unique<TestValueCommand>("First", first, 0, 1));
    redoCommands.push_back(std::make_unique<TestValueCommand>("Second", second, 0, 2, nullptr, &failSecondRedo));

    UndoRedoResult result = undoRedo.execute(
            std::make_unique<BatchDocumentCommand>("Failing Redo Batch", std::move(redoCommands)));
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 0);
    EXPECT_FALSE(undoRedo.canUndo());

    bool failFirstUndo = false;
    std::vector<std::unique_ptr<IUndoableCommand>> undoCommands;
    undoCommands.push_back(std::make_unique<TestValueCommand>("First", first, 0, 1, &failFirstUndo));
    undoCommands.push_back(std::make_unique<TestValueCommand>("Second", second, 0, 2));

    result = undoRedo.execute(
            std::make_unique<BatchDocumentCommand>("Failing Undo Batch", std::move(undoCommands)));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);

    failFirstUndo = true;
    result = undoRedo.undo();
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(first, 1);
    EXPECT_EQ(second, 2);
    EXPECT_TRUE(undoRedo.canUndo());
    EXPECT_FALSE(undoRedo.canRedo());
}

TEST(DocumentCommandTests, SceneTransformPreviewTransactionCommitsAndCancelsThroughEditorHistory) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    EditorHistoryService history;

    TransformComponent previewTransform = *scene.transform(entity);
    previewTransform.position = {2.0f, 0.0f, 0.0f};
    {
        SceneTransformPreviewTransaction transaction(scene, {entity}, "Move Preview");
        DocumentCommandResult result = transaction.previewTransform(entity, previewTransform);
        ASSERT_TRUE(result.ok) << result.message;
        EXPECT_TRUE(result.changed);
        ASSERT_NE(scene.transform(entity), nullptr);
        EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 2.0f);

        result = transaction.cancel();
        ASSERT_TRUE(result.ok) << result.message;
        EXPECT_TRUE(result.changed);
        ASSERT_NE(scene.transform(entity), nullptr);
        EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
    }

    previewTransform.position = {4.0f, 0.0f, 0.0f};
    SceneTransformPreviewTransaction transaction(scene, {entity}, "Move Preview");
    ASSERT_TRUE(transaction.previewTransform(entity, previewTransform).ok);
    DocumentCommandResult result = transaction.commit(history);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 4.0f);
    ASSERT_NE(history.undoLabel(), nullptr);
    EXPECT_EQ(*history.undoLabel(), "Move Preview");

    ASSERT_TRUE(history.undo().ok);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
    ASSERT_TRUE(history.redo().ok);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 4.0f);
}

TEST(DocumentCommandTests, SceneTransformDragServiceFiltersTargetsAndRecordsScenePreviewThroughApi) {
    ProjectSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::fromProjectSession(session));
    SceneDocument& scene = session.scene();

    const EntityId parent = scene.createEntity("Parent");
    const EntityId child = scene.createEntity("Child");
    ASSERT_TRUE(scene.setParent(child, parent));
    ASSERT_TRUE(scene.setSelected(parent, true));
    ASSERT_TRUE(scene.setSelected(child, true));
    ASSERT_TRUE(scene.setActiveSelection(parent));

    SceneTransformDragService service;
    ASSERT_TRUE(service.begin(scene, QVec3{}, true, "Move Preview"));
    ASSERT_TRUE(service.previewActive());
    ASSERT_EQ(service.targets().size(), 1u);
    EXPECT_EQ(service.targets().front().entity, parent);

    TransformComponent preview = service.targets().front().transform;
    preview.position = {2.0f, 0.0f, 0.0f};
    DocumentCommandResult previewResult = service.previewTransform(parent, preview);
    ASSERT_TRUE(previewResult.ok) << previewResult.message;
    EXPECT_TRUE(previewResult.changed);
    ASSERT_NE(scene.transform(parent), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 2.0f);

    DocumentCommandResult cancelResult = service.cancel();
    ASSERT_TRUE(cancelResult.ok) << cancelResult.message;
    EXPECT_TRUE(cancelResult.changed);
    EXPECT_FALSE(service.previewActive());
    EXPECT_TRUE(service.targets().empty());
    ASSERT_NE(scene.transform(parent), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 0.0f);

    ASSERT_TRUE(service.begin(scene, QVec3{}, true, "Move Preview"));
    preview = service.targets().front().transform;
    preview.position = {4.0f, 0.0f, 0.0f};
    ASSERT_TRUE(service.previewTransform(parent, preview).ok);
    QuaderApiVoidResult commit = service.commit(editor);
    ASSERT_TRUE(commit.ok) << commit.message;
    EXPECT_TRUE(commit.changed);
    EXPECT_FALSE(service.previewActive());
    EXPECT_TRUE(service.targets().empty());
    ASSERT_NE(scene.transform(parent), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 4.0f);
    ASSERT_NE(session.history().undoLabel(), nullptr);
    EXPECT_EQ(*session.history().undoLabel(), "Move Preview");

    ASSERT_TRUE(session.history().undo().ok);
    ASSERT_NE(scene.transform(parent), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 0.0f);
    ASSERT_TRUE(session.history().redo().ok);
    ASSERT_NE(scene.transform(parent), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 4.0f);
}

TEST(DocumentCommandTests, SceneTransformDragServiceInspectorMoveUsesSharedTargetPolicy) {
    ProjectSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::fromProjectSession(session));
    SceneDocument& scene = session.scene();

    const EntityId parent = scene.createEntity("Parent");
    const EntityId child = scene.createEntity("Child");
    const EntityId locked = scene.createEntity("Locked");
    ASSERT_TRUE(scene.setParent(child, parent, false));

    TransformComponent childTransform;
    childTransform.position = {1.0f, 0.0f, 0.0f};
    scene.setTransform(child, childTransform);
    TransformComponent lockedTransform;
    lockedTransform.position = {7.0f, 0.0f, 0.0f};
    scene.setTransform(locked, lockedTransform);
    ASSERT_TRUE(scene.setLocked(locked, true));

    ASSERT_TRUE(scene.setSelected(parent, true));
    ASSERT_TRUE(scene.setSelected(child, true));
    ASSERT_TRUE(scene.setSelected(locked, true));
    ASSERT_TRUE(scene.setActiveSelection(parent));

    SceneTransformDragService service;
    const QuaderApiVoidResult result = service.moveSelectedToPrimaryPosition(
            editor,
            scene,
            parent,
            {3.0f, 0.0f, 0.0f},
            "Set Transform");
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);

    ASSERT_NE(scene.transform(parent), nullptr);
    ASSERT_NE(scene.transform(child), nullptr);
    ASSERT_NE(scene.transform(locked), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 3.0f);
    EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 1.0f);
    EXPECT_FLOAT_EQ(scene.transform(locked)->position.x, 7.0f);
    ASSERT_NE(session.history().undoLabel(), nullptr);
    EXPECT_EQ(*session.history().undoLabel(), "Set Transform");

    ASSERT_TRUE(session.history().undo().ok);
    EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 0.0f);
    EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 1.0f);
    EXPECT_FLOAT_EQ(scene.transform(locked)->position.x, 7.0f);
    ASSERT_TRUE(session.history().redo().ok);
    EXPECT_FLOAT_EQ(scene.transform(parent)->position.x, 3.0f);
    EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 1.0f);
    EXPECT_FLOAT_EQ(scene.transform(locked)->position.x, 7.0f);
}

TEST(DocumentCommandTests, SceneTransformDragServiceInspectorRotateAndScalePinParentedSelectionPivot) {
    constexpr float HalfPi = 1.57079632679f;

    ProjectSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::fromProjectSession(session));
    SceneDocument& scene = session.scene();
    AssetRegistry& assets = session.assets();

    MeshAsset mesh;
    mesh.vertices = {
            {.position = {0.0f, 1.0f, 0.0f}},
            {.position = {0.0f, 3.0f, 0.0f}},
    };
    const AssetId meshId = assets.addMesh(std::move(mesh));

    const EntityId parent = scene.createEntity("Parent");
    TransformComponent parentTransform;
    parentTransform.position = {10.0f, 0.0f, 0.0f};
    scene.setTransform(parent, parentTransform);

    const EntityId child = scene.createEntity("Child");
    TransformComponent childTransform;
    childTransform.position = {1.0f, 0.0f, 0.0f};
    scene.setTransform(child, childTransform);
    scene.setMesh(child, MeshComponent{.mesh = meshId});
    ASSERT_TRUE(scene.setParent(child, parent, false));
    ASSERT_TRUE(scene.setSelected(child, true));
    ASSERT_TRUE(scene.setActiveSelection(child));

    const auto selectedPivot = [&]() {
        const std::optional<SceneBounds> bounds = selectedWorldBounds(scene, assets);
        EXPECT_TRUE(bounds.has_value());
        return bounds ? boundsCenter(*bounds) : QVec3{};
    };
    const QVec3 pivotBeforeRotate = selectedPivot();

    SceneTransformDragService service;
    QuaderApiVoidResult rotate = service.rotateSelectedToPrimaryEulerAroundSelectionPivot(
            editor,
            scene,
            assets,
            child,
            {0.0f, 0.0f, HalfPi},
            "Set Transform");
    ASSERT_TRUE(rotate.ok) << rotate.message;
    EXPECT_TRUE(rotate.changed);
    const QVec3 pivotAfterRotate = selectedPivot();
    EXPECT_NEAR(pivotAfterRotate.x, pivotBeforeRotate.x, 0.001f);
    EXPECT_NEAR(pivotAfterRotate.y, pivotBeforeRotate.y, 0.001f);
    EXPECT_NEAR(pivotAfterRotate.z, pivotBeforeRotate.z, 0.001f);
    ASSERT_NE(scene.transform(child), nullptr);
    EXPECT_NEAR(scene.transform(child)->position.x, 3.0f, 0.001f);
    EXPECT_NEAR(scene.transform(child)->position.y, 2.0f, 0.001f);

    ASSERT_TRUE(session.history().undo().ok);
    ASSERT_NE(scene.transform(child), nullptr);
    EXPECT_NEAR(scene.transform(child)->position.x, 1.0f, 0.001f);
    EXPECT_NEAR(scene.transform(child)->position.y, 0.0f, 0.001f);

    const QVec3 pivotBeforeScale = selectedPivot();
    QuaderApiVoidResult scale = service.scaleSelectedToPrimaryScaleAroundSelectionPivot(
            editor,
            scene,
            assets,
            child,
            {2.0f, 3.0f, 4.0f},
            "Set Transform");
    ASSERT_TRUE(scale.ok) << scale.message;
    EXPECT_TRUE(scale.changed);
    const QVec3 pivotAfterScale = selectedPivot();
    EXPECT_NEAR(pivotAfterScale.x, pivotBeforeScale.x, 0.001f);
    EXPECT_NEAR(pivotAfterScale.y, pivotBeforeScale.y, 0.001f);
    EXPECT_NEAR(pivotAfterScale.z, pivotBeforeScale.z, 0.001f);
    ASSERT_NE(scene.transform(child), nullptr);
    EXPECT_NEAR(scene.transform(child)->scale.x, 2.0f, 0.001f);
    EXPECT_NEAR(scene.transform(child)->scale.y, 3.0f, 0.001f);
    EXPECT_NEAR(scene.transform(child)->scale.z, 4.0f, 0.001f);
    EXPECT_NEAR(scene.transform(child)->position.x, 1.0f, 0.001f);
    EXPECT_NEAR(scene.transform(child)->position.y, -4.0f, 0.001f);
}

TEST(DocumentCommandTests, TypedEntityCommandsRoundTripHierarchySelectionAndTransforms) {
    SceneDocument scene;
    const EntityId parent = scene.createEntity("Parent");
    TransformComponent parentTransform;
    parentTransform.position = {10.0f, 0.0f, 0.0f};
    scene.setTransform(parent, parentTransform);

    const EntityId child = scene.createEntity("Child");
    TransformComponent childTransform;
    childTransform.position = {1.0f, 0.0f, 0.0f};
    scene.setTransform(child, childTransform);
    ASSERT_TRUE(scene.setParent(child, parent, false));

    UndoRedoManager undoRedo;
    UndoRedoResult result = undoRedo.execute(
            std::make_unique<ReparentEntityCommand>(scene, child, InvalidEntityId, true));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(scene.parent(child), InvalidEntityId);
    ASSERT_NE(scene.transform(child), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 11.0f);

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_EQ(scene.parent(child), parent);
    ASSERT_NE(scene.transform(child), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 1.0f);
    ASSERT_TRUE(undoRedo.redo().ok);
    EXPECT_EQ(scene.parent(child), InvalidEntityId);
    EXPECT_FLOAT_EQ(scene.transform(child)->position.x, 11.0f);

    TransformComponent moved = *scene.transform(child);
    moved.position.y = 4.0f;
    result = undoRedo.execute(
            std::make_unique<ApplyEntityTransformCommand>(scene, child, *scene.transform(child), moved));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FLOAT_EQ(scene.transform(child)->position.y, 4.0f);
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FLOAT_EQ(scene.transform(child)->position.y, 0.0f);

    result = undoRedo.execute(
            std::make_unique<SelectEntitiesCommand>(scene, std::vector<EntityId>{parent, child}, parent));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(scene.selectedEntities().size(), 2u);
    EXPECT_EQ(scene.primarySelection(), parent);
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(scene.selectedEntities().empty());
}

TEST(DocumentCommandServiceTests, SceneBatchReparentUsesSceneOwnedReparentCommand) {
    const std::string source = readSourceFile("src/editor/commands/document_command_service.cpp");
    const std::size_t batchStart = source.find("DocumentCommandService::applySceneBatch");
    ASSERT_NE(batchStart, std::string::npos);
    const std::string batchSource = source.substr(batchStart);

    EXPECT_NE(batchSource.find("std::make_unique<ReparentEntityCommand>("), std::string::npos);
    EXPECT_NE(batchSource.find("operation.preserveWorldTransform"), std::string::npos);
    EXPECT_EQ(batchSource.find("simulatedWorldMatrix"), std::string::npos);
    EXPECT_EQ(batchSource.find("simulatedIsDescendantOf"), std::string::npos);
    EXPECT_EQ(batchSource.find("sceneLocalMatrixFromWorldMatrix"), std::string::npos);
    EXPECT_EQ(batchSource.find("worldTransformMatrix"), std::string::npos);
}

TEST(DocumentCommandTests, TypedLifecycleCommandsRestoreSceneSnapshots) {
    SceneDocument scene;
    UndoRedoManager undoRedo;

    auto create = std::make_unique<CreateEntityCommand>(
            scene,
            "Created",
            EntityKind::Light,
            InvalidEntityId,
            TransformComponent{.position = {2.0f, 0.0f, 0.0f}},
            true);
    CreateEntityCommand* createCommand = create.get();
    UndoRedoResult result = undoRedo.execute(std::move(create));
    ASSERT_TRUE(result.ok) << result.message;
    EntityId created = createCommand->entity();
    ASSERT_NE(created, InvalidEntityId);
    const std::string createdUuid = scene.uuid(created);
    EXPECT_FALSE(createdUuid.empty());
    EXPECT_TRUE(scene.exists(created));
    EXPECT_EQ(scene.kind(created), EntityKind::Light);
    ASSERT_NE(scene.transform(created), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(created)->position.x, 2.0f);
    EXPECT_EQ(scene.primarySelection(), created);
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FALSE(scene.exists(created));
    ASSERT_TRUE(undoRedo.redo().ok);
    created = createCommand->entity();
    EXPECT_TRUE(scene.exists(created));
    EXPECT_EQ(scene.uuid(created), createdUuid);
    EXPECT_EQ(scene.entityForUuid(createdUuid), created);

    const EntityId source = scene.createEntity("Source", EntityKind::Mesh);
    scene.setMesh(source, MeshComponent{.mesh = 42});
    scene.setMaterial(source, MaterialComponent{.material = 7});
    TransformComponent sourceTransform;
    sourceTransform.position = {3.0f, 0.0f, 0.0f};
    scene.setTransform(source, sourceTransform);
    const EntityId leaf = scene.createEntity("Leaf");
    TransformComponent leafTransform;
    leafTransform.position = {0.0f, 1.0f, 0.0f};
    scene.setTransform(leaf, leafTransform);
    ASSERT_TRUE(scene.setParent(leaf, source, false));

    auto duplicate = std::make_unique<DuplicateEntityCommand>(scene, source);
    DuplicateEntityCommand* duplicateCommand = duplicate.get();
    result = undoRedo.execute(std::move(duplicate));
    ASSERT_TRUE(result.ok) << result.message;
    const EntityId duplicateRoot = duplicateCommand->duplicateEntity();
    ASSERT_TRUE(scene.exists(duplicateRoot));
    const std::string duplicateUuid = scene.uuid(duplicateRoot);
    EXPECT_FALSE(duplicateUuid.empty());
    EXPECT_NE(duplicateUuid, scene.uuid(source));
    ASSERT_NE(scene.name(duplicateRoot), nullptr);
    EXPECT_EQ(scene.name(duplicateRoot)->name, "Source_1");
    ASSERT_NE(scene.mesh(duplicateRoot), nullptr);
    EXPECT_EQ(scene.mesh(duplicateRoot)->mesh, 42u);
    ASSERT_EQ(scene.children(duplicateRoot).size(), 1u);
    EXPECT_EQ(scene.primarySelection(), duplicateRoot);
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FALSE(scene.exists(duplicateRoot));

    scene.clearSelection();
    scene.setSelected(source, true);
    auto destroy = std::make_unique<DestroyEntityCommand>(scene, source);
    DestroyEntityCommand* destroyCommand = destroy.get();
    result = undoRedo.execute(std::move(destroy));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(scene.exists(source));
    EXPECT_FALSE(scene.exists(leaf));
    ASSERT_TRUE(undoRedo.undo().ok);
    const EntityId restored = destroyCommand->entity();
    ASSERT_TRUE(scene.exists(restored));
    ASSERT_NE(scene.name(restored), nullptr);
    EXPECT_EQ(scene.name(restored)->name, "Source");
    ASSERT_NE(scene.mesh(restored), nullptr);
    EXPECT_EQ(scene.mesh(restored)->mesh, 42u);
    ASSERT_EQ(scene.children(restored).size(), 1u);
    EXPECT_EQ(scene.primarySelection(), restored);
    ASSERT_TRUE(undoRedo.redo().ok);
    EXPECT_FALSE(scene.exists(restored));
}

TEST(DocumentCommandTests, DuplicateMintsEditorObjectIdsOnceAndRedoReusesThem) {
    SceneDocument scene;
    UndoRedoManager undoRedo;

    const EntityId source = scene.createEntity("Source", EntityKind::Mesh);
    const EntityId leaf = scene.createEntity("Leaf");
    ASSERT_TRUE(scene.setParent(leaf, source, false));
    const EditorObjectId sourceObject = scene.editorObjectId(source);
    const EditorObjectId leafObject = scene.editorObjectId(leaf);
    const std::string sourceUuid = scene.uuid(source);
    const std::string leafUuid = scene.uuid(leaf);

    auto duplicate = std::make_unique<DuplicateEntityCommand>(scene, source);
    DuplicateEntityCommand* duplicateCommand = duplicate.get();
    UndoRedoResult result = undoRedo.execute(std::move(duplicate));
    ASSERT_TRUE(result.ok) << result.message;

    const EntityId duplicateRoot = duplicateCommand->duplicateEntity();
    ASSERT_TRUE(scene.exists(duplicateRoot));
    ASSERT_EQ(scene.children(duplicateRoot).size(), 1u);
    const EntityId duplicateLeaf = scene.children(duplicateRoot).front();
    const EditorObjectId duplicateObject = scene.editorObjectId(duplicateRoot);
    const EditorObjectId duplicateLeafObject = scene.editorObjectId(duplicateLeaf);
    const std::string duplicateUuid = scene.uuid(duplicateRoot);
    const std::string duplicateLeafUuid = scene.uuid(duplicateLeaf);
    EXPECT_NE(duplicateObject, InvalidEditorObjectId);
    EXPECT_NE(duplicateLeafObject, InvalidEditorObjectId);
    EXPECT_NE(duplicateObject, sourceObject);
    EXPECT_NE(duplicateLeafObject, leafObject);
    EXPECT_NE(duplicateUuid, sourceUuid);
    EXPECT_NE(duplicateLeafUuid, leafUuid);

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FALSE(scene.exists(duplicateRoot));
    ASSERT_TRUE(undoRedo.redo().ok);

    const EntityId redoneRoot = duplicateCommand->duplicateEntity();
    ASSERT_TRUE(scene.exists(redoneRoot));
    ASSERT_EQ(scene.children(redoneRoot).size(), 1u);
    const EntityId redoneLeaf = scene.children(redoneRoot).front();
    EXPECT_EQ(scene.editorObjectId(redoneRoot), duplicateObject);
    EXPECT_EQ(scene.editorObjectId(redoneLeaf), duplicateLeafObject);
    EXPECT_EQ(scene.uuid(redoneRoot), duplicateUuid);
    EXPECT_EQ(scene.uuid(redoneLeaf), duplicateLeafUuid);
    EXPECT_EQ(scene.entityForUuid(duplicateUuid), redoneRoot);
    EXPECT_EQ(scene.entityForUuid(duplicateLeafUuid), redoneLeaf);
}

TEST(DocumentCommandTests, DestroyUndoRestoresEditorObjectIdsForSubtrees) {
    SceneDocument scene;
    UndoRedoManager undoRedo;

    const EntityId source = scene.createEntity("Source", EntityKind::Mesh);
    const EntityId leaf = scene.createEntity("Leaf");
    ASSERT_TRUE(scene.setParent(leaf, source, false));
    const EditorObjectId sourceObject = scene.editorObjectId(source);
    const EditorObjectId leafObject = scene.editorObjectId(leaf);
    const std::string sourceUuid = scene.uuid(source);
    const std::string leafUuid = scene.uuid(leaf);

    auto destroy = std::make_unique<DestroyEntityCommand>(scene, source);
    DestroyEntityCommand* destroyCommand = destroy.get();
    UndoRedoResult result = undoRedo.execute(std::move(destroy));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(scene.exists(source));
    EXPECT_FALSE(scene.exists(leaf));

    ASSERT_TRUE(undoRedo.undo().ok);
    const EntityId restoredRoot = destroyCommand->entity();
    ASSERT_TRUE(scene.exists(restoredRoot));
    ASSERT_EQ(scene.children(restoredRoot).size(), 1u);
    const EntityId restoredLeaf = scene.children(restoredRoot).front();
    EXPECT_EQ(scene.editorObjectId(restoredRoot), sourceObject);
    EXPECT_EQ(scene.editorObjectId(restoredLeaf), leafObject);
    EXPECT_EQ(scene.uuid(restoredRoot), sourceUuid);
    EXPECT_EQ(scene.uuid(restoredLeaf), leafUuid);
    EXPECT_EQ(scene.entityForUuid(sourceUuid), restoredRoot);
    EXPECT_EQ(scene.entityForUuid(leafUuid), restoredLeaf);
}

TEST(DocumentCommandServiceTests, BatchesVisibilityAndTransforms) {
    SceneDocument scene;
    const EntityId first = scene.createEntity("First");
    const EntityId second = scene.createEntity("Second");
    const EntityId locked = scene.createEntity("Locked");
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);

    DocumentCommandResult result = commands.setVisibility({
            {.entity = first, .before = true, .after = false},
            {.entity = second, .before = true, .after = false},
    });
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.visibility(first), nullptr);
    ASSERT_NE(scene.visibility(second), nullptr);
    EXPECT_FALSE(scene.visibility(first)->visible);
    EXPECT_FALSE(scene.visibility(second)->visible);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_TRUE(scene.visibility(first)->visible);
    EXPECT_TRUE(scene.visibility(second)->visible);
    ASSERT_TRUE(history.redo().ok);
    EXPECT_FALSE(scene.visibility(first)->visible);
    EXPECT_FALSE(scene.visibility(second)->visible);

    TransformComponent firstAfter;
    firstAfter.position = {1.0f, 0.0f, 0.0f};
    TransformComponent secondAfter;
    secondAfter.position = {0.0f, 2.0f, 0.0f};
    result = commands.setTransforms({
            {.entity = first, .before = *scene.transform(first), .after = firstAfter},
            {.entity = second, .before = *scene.transform(second), .after = secondAfter},
    });
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 1.0f);
    EXPECT_FLOAT_EQ(scene.transform(second)->position.y, 2.0f);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 0.0f);
    EXPECT_FLOAT_EQ(scene.transform(second)->position.y, 0.0f);

    result = commands.setLocked(locked, true);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.lock(locked), nullptr);
    EXPECT_TRUE(scene.lock(locked)->locked);

    TransformComponent lockedAfter;
    lockedAfter.position = {5.0f, 0.0f, 0.0f};
    result = commands.setTransform(locked, lockedAfter);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);
    EXPECT_FLOAT_EQ(scene.transform(locked)->position.x, 0.0f);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_FALSE(scene.lock(locked)->locked);
}

TEST(DocumentCommandServiceTests, LockCommandsClearSelectionInsideUndoableCommands) {
    SceneDocument scene;
    const EntityId parent = scene.createEntity("Lock Parent");
    const EntityId child = scene.createEntity("Lock Child");
    ASSERT_TRUE(scene.setParent(child, parent, false));
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);

    ASSERT_TRUE(commands.selectEntities({parent, child}, child).ok);
    DocumentCommandResult result = commands.setLocked(parent, true);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.lock(parent), nullptr);
    EXPECT_TRUE(scene.lock(parent)->locked);
    EXPECT_TRUE(scene.selectedEntities().empty());
    EXPECT_EQ(scene.primarySelection(), InvalidEntityId);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_FALSE(scene.lock(parent)->locked);
    EXPECT_EQ(scene.selectedEntities().size(), 2u);
    EXPECT_EQ(scene.primarySelection(), child);

    ASSERT_TRUE(history.redo().ok);
    EXPECT_TRUE(scene.lock(parent)->locked);
    EXPECT_TRUE(scene.selectedEntities().empty());
    EXPECT_EQ(scene.primarySelection(), InvalidEntityId);

    ASSERT_TRUE(history.undo().ok);
    ASSERT_TRUE(commands.selectEntities({child}, child).ok);

    result = commands.applySceneBatch("Batch Lock Parent", {
            DocumentSceneBatchOperation{
                    .kind = DocumentSceneBatchOperationKind::SetLocked,
                    .entity = parent,
                    .flag = true,
            },
    });
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(history.undoLabel(), nullptr);
    EXPECT_EQ(*history.undoLabel(), "Batch Lock Parent");
    EXPECT_TRUE(scene.lock(parent)->locked);
    EXPECT_TRUE(scene.selectedEntities().empty());
    EXPECT_EQ(scene.primarySelection(), InvalidEntityId);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_FALSE(scene.lock(parent)->locked);
    EXPECT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.primarySelection(), child);

    ASSERT_TRUE(history.redo().ok);
    EXPECT_TRUE(scene.lock(parent)->locked);
    EXPECT_TRUE(scene.selectedEntities().empty());
}

TEST(DocumentCommandServiceTests, AssetAwareServiceRejectsDanglingAssetReferences) {
    SceneDocument scene;
    AssetRegistry assets;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history, &assets);

    MeshAsset mesh;
    const AssetId meshId = assets.addMesh(mesh);
    const EntityId entity = scene.createEntity("Asset Checked", EntityKind::Mesh);
    scene.setMesh(entity, MeshComponent{.mesh = meshId});

    MeshComponent invalidMesh = *scene.mesh(entity);
    invalidMesh.mesh = meshId + 1000u;
    DocumentCommandResult result = commands.setMeshComponent(entity, invalidMesh);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "Mesh asset does not exist");

    result = commands.setMaterial(entity, meshId + 2000u);
    EXPECT_FALSE(result.ok);
    EXPECT_EQ(result.message, "Material asset does not exist");
}

TEST(DocumentCommandServiceTests, ModelingOwnedMeshComponentAllowsMetadataButRejectsMeshAssetReplacement) {
    SceneDocument scene;
    AssetRegistry assets;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history, &assets);

    const AssetId sourceMesh = assets.addMesh(MeshAsset{});
    const AssetId replacementMesh = assets.addMesh(MeshAsset{});
    ASSERT_NE(sourceMesh, InvalidAssetId);
    ASSERT_NE(replacementMesh, InvalidAssetId);

    const EntityId entity = scene.createEntity("Modeling Owned Mesh", EntityKind::Mesh);
    scene.setMesh(entity, MeshComponent{.mesh = sourceMesh});
    scene.setModelingObject(entity, ModelingObjectComponent{.objectId = 42});

    MeshComponent metadata = *scene.mesh(entity);
    metadata.doubleSided = true;
    metadata.generateCollisions = false;
    metadata.collisionType = MeshCollisionType::ConvexHull;
    metadata.displayCollision = true;
    metadata.bodyType = MeshPhysicsBodyType::RigidBody;
    DocumentCommandResult result = commands.setMeshComponent(entity, metadata);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.mesh(entity), nullptr);
    EXPECT_EQ(scene.mesh(entity)->mesh, sourceMesh);
    EXPECT_TRUE(scene.mesh(entity)->doubleSided);
    EXPECT_FALSE(scene.mesh(entity)->generateCollisions);
    EXPECT_EQ(scene.mesh(entity)->collisionType, MeshCollisionType::ConvexHull);
    EXPECT_TRUE(scene.mesh(entity)->displayCollision);
    EXPECT_EQ(scene.mesh(entity)->bodyType, MeshPhysicsBodyType::RigidBody);

    ASSERT_TRUE(history.undo().ok);
    ASSERT_NE(scene.mesh(entity), nullptr);
    EXPECT_EQ(scene.mesh(entity)->mesh, sourceMesh);
    EXPECT_FALSE(scene.mesh(entity)->doubleSided);
    EXPECT_TRUE(scene.mesh(entity)->generateCollisions);

    MeshComponent replacement = *scene.mesh(entity);
    replacement.mesh = replacementMesh;
    result = commands.setMeshComponent(entity, replacement);
    EXPECT_FALSE(result.ok);
    EXPECT_NE(result.message.find("modeling object"), std::string::npos);
    EXPECT_EQ(scene.mesh(entity)->mesh, sourceMesh);
}

TEST(DocumentCommandServiceTests, SetMaterialComparesFullMaterialComponentState) {
    SceneDocument scene;
    AssetRegistry assets;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history, &assets);

    const AssetId baseMaterial = assets.addMaterial(makeDefaultQuaderMaterial("Base Material"));
    const AssetId slotMaterial = assets.addMaterial(makeDefaultQuaderMaterial("Slot Material"));
    ASSERT_NE(baseMaterial, InvalidAssetId);
    ASSERT_NE(slotMaterial, InvalidAssetId);

    const EntityId entity = scene.createEntity("Slotted Material", EntityKind::Mesh);
    scene.setMaterial(entity, MaterialComponent{
            .material = baseMaterial,
            .materialSlots = {{2u, slotMaterial}},
    });

    const DocumentCommandResult result = commands.setMaterial(entity, baseMaterial);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.material(entity), nullptr);
    EXPECT_EQ(scene.material(entity)->material, baseMaterial);
    EXPECT_TRUE(scene.material(entity)->materialSlots.empty());

    ASSERT_TRUE(history.undo().ok);
    ASSERT_NE(scene.material(entity), nullptr);
    EXPECT_EQ(scene.material(entity)->material, baseMaterial);
    ASSERT_TRUE(scene.material(entity)->materialSlots.contains(2u));
    EXPECT_EQ(scene.material(entity)->materialSlots.at(2u), slotMaterial);

    ASSERT_TRUE(history.redo().ok);
    ASSERT_NE(scene.material(entity), nullptr);
    EXPECT_EQ(scene.material(entity)->material, baseMaterial);
    EXPECT_TRUE(scene.material(entity)->materialSlots.empty());
}

TEST(DocumentCommandServiceTests, CreatePointLightAndSetLightComponentAreUndoable) {
    SceneDocument scene;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);

    TransformComponent transform;
    transform.position = {1.0f, 2.0f, 3.0f};

    LightComponent light;
    light.type = LightType::Point;
    light.color = {1.0f, 0.8f, 0.6f};
    light.intensity = 900.0f;
    light.range = 12.0f;
    light.castsShadows = true;

    const DocumentCreateEntityResult created =
            commands.createPointLight("Key Light", light, InvalidEntityId, transform, true);
    ASSERT_TRUE(created.ok) << created.message;
    EXPECT_TRUE(created.changed);
    ASSERT_NE(created.entity, InvalidEntityId);
    EXPECT_TRUE(scene.exists(created.entity));
    EXPECT_EQ(scene.kind(created.entity), EntityKind::Light);
    ASSERT_NE(scene.transform(created.entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(created.entity)->position.z, 3.0f);
    ASSERT_NE(scene.light(created.entity), nullptr);
    EXPECT_EQ(scene.light(created.entity)->type, LightType::Point);
    EXPECT_FLOAT_EQ(scene.light(created.entity)->intensity, 900.0f);
    EXPECT_FLOAT_EQ(scene.light(created.entity)->range, 12.0f);
    EXPECT_TRUE(scene.light(created.entity)->castsShadows);
    EXPECT_EQ(scene.primarySelection(), created.entity);

    LightComponent edited = *scene.light(created.entity);
    edited.color = {0.25f, 0.5f, 1.0f};
    edited.intensity = 1250.0f;
    edited.range = 6.5f;
    edited.castsShadows = false;
    DocumentCommandResult result = commands.setLightComponent(created.entity, edited);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    EXPECT_FLOAT_EQ(scene.light(created.entity)->color.z, 1.0f);
    EXPECT_FLOAT_EQ(scene.light(created.entity)->range, 6.5f);
    EXPECT_FALSE(scene.light(created.entity)->castsShadows);

    result = commands.setLightComponent(created.entity, edited);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);

    ASSERT_TRUE(history.undo().ok);
    ASSERT_NE(scene.light(created.entity), nullptr);
    EXPECT_FLOAT_EQ(scene.light(created.entity)->color.z, 0.6f);
    EXPECT_FLOAT_EQ(scene.light(created.entity)->range, 12.0f);
    EXPECT_TRUE(scene.light(created.entity)->castsShadows);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_FALSE(scene.exists(created.entity));

    ASSERT_TRUE(history.redo().ok);
    EXPECT_TRUE(scene.exists(created.entity));
    ASSERT_NE(scene.light(created.entity), nullptr);
    EXPECT_EQ(scene.light(created.entity)->type, LightType::Point);
    EXPECT_EQ(scene.primarySelection(), created.entity);

    ASSERT_TRUE(history.redo().ok);
    ASSERT_NE(scene.light(created.entity), nullptr);
    EXPECT_FLOAT_EQ(scene.light(created.entity)->range, 6.5f);
    EXPECT_FALSE(scene.light(created.entity)->castsShadows);
}

TEST(DocumentCommandServiceTests, DefaultGroupNameStaysLiteralGroup) {
    SceneDocument scene;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);

    const EntityId first = scene.createEntity("First");
    const EntityId second = scene.createEntity("Second");
    ASSERT_NE(first, InvalidEntityId);
    ASSERT_NE(second, InvalidEntityId);

    ASSERT_TRUE(commands.selectEntities({first}, first).ok);
    const DocumentGroupSelectionResult firstGroup = commands.groupSelection();
    ASSERT_TRUE(firstGroup.ok) << firstGroup.message;
    ASSERT_TRUE(scene.exists(firstGroup.groupEntity));
    ASSERT_NE(scene.name(firstGroup.groupEntity), nullptr);
    EXPECT_EQ(scene.name(firstGroup.groupEntity)->name, "Group");

    ASSERT_TRUE(commands.selectEntities({second}, second).ok);
    const DocumentGroupSelectionResult secondGroup = commands.groupSelection();
    ASSERT_TRUE(secondGroup.ok) << secondGroup.message;
    ASSERT_TRUE(scene.exists(secondGroup.groupEntity));
    ASSERT_NE(scene.name(secondGroup.groupEntity), nullptr);
    EXPECT_EQ(scene.name(secondGroup.groupEntity)->name, "Group");
    EXPECT_NE(firstGroup.groupEntity, secondGroup.groupEntity);
}

TEST(DocumentCommandServiceTests, CoversEntityHierarchySelectionAndLifecycleCommands) {
    SceneDocument scene;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);

    const DocumentCreateEntityResult firstCreated =
            commands.createEntity("First", EntityKind::Generic, InvalidEntityId, {});
    ASSERT_TRUE(firstCreated.ok) << firstCreated.message;
    const EntityId first = firstCreated.entity;
    ASSERT_NE(first, InvalidEntityId);
    const DocumentCreateEntityResult secondCreated =
            commands.createEntity("Second", EntityKind::Generic, InvalidEntityId, {});
    ASSERT_TRUE(secondCreated.ok) << secondCreated.message;
    const EntityId second = secondCreated.entity;
    ASSERT_NE(second, InvalidEntityId);

    TransformComponent moved;
    moved.position = {5.0f, 0.0f, 0.0f};
    DocumentCommandResult result = commands.applyEntityTransform(first, moved);
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(scene.transform(first), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 5.0f);
    ASSERT_TRUE(history.undo().ok);
    EXPECT_FLOAT_EQ(scene.transform(first)->position.x, 0.0f);
    ASSERT_TRUE(history.redo().ok);

    result = commands.selectEntities({first, second}, second);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(scene.selectedEntities().size(), 2u);
    EXPECT_EQ(scene.primarySelection(), second);

    TransformComponent groupTransform;
    groupTransform.position = {2.0f, 0.0f, 0.0f};
    const DocumentGroupSelectionResult grouped = commands.groupSelection("Group", groupTransform);
    ASSERT_TRUE(grouped.ok) << grouped.message;
    const EntityId group = grouped.groupEntity;
    ASSERT_TRUE(scene.exists(group));
    const std::string groupUuid = scene.uuid(group);
    EXPECT_FALSE(groupUuid.empty());
    EXPECT_EQ(scene.parent(first), group);
    EXPECT_EQ(scene.parent(second), group);
    EXPECT_EQ(scene.primarySelection(), group);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_FALSE(scene.exists(group));
    EXPECT_EQ(scene.parent(first), InvalidEntityId);
    EXPECT_EQ(scene.parent(second), InvalidEntityId);
    EXPECT_EQ(scene.selectedEntities().size(), 2u);
    EXPECT_EQ(scene.primarySelection(), second);

    ASSERT_TRUE(history.redo().ok);
    const EntityId regrouped = scene.primarySelection();
    ASSERT_TRUE(scene.exists(regrouped));
    EXPECT_EQ(scene.uuid(regrouped), groupUuid);
    EXPECT_EQ(scene.entityForUuid(groupUuid), regrouped);
    EXPECT_EQ(scene.parent(first), regrouped);
    EXPECT_EQ(scene.parent(second), regrouped);

    result = commands.ungroupSelection();
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(scene.exists(regrouped));
    EXPECT_EQ(scene.parent(first), InvalidEntityId);
    EXPECT_EQ(scene.parent(second), InvalidEntityId);
    EXPECT_EQ(scene.selectedEntities().size(), 2u);

    ASSERT_TRUE(history.undo().ok);
    const EntityId restoredGroup = scene.primarySelection();
    ASSERT_TRUE(scene.exists(restoredGroup));
    EXPECT_EQ(scene.parent(first), restoredGroup);
    EXPECT_EQ(scene.parent(second), restoredGroup);

    result = commands.reparentEntity(first, InvalidEntityId, true);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(scene.parent(first), InvalidEntityId);
    ASSERT_TRUE(history.undo().ok);
    EXPECT_EQ(scene.parent(first), restoredGroup);

    const DocumentDuplicateEntityResult duplicated = commands.duplicateEntity(first);
    ASSERT_TRUE(duplicated.ok) << duplicated.message;
    const EntityId duplicate = duplicated.duplicateEntity;
    ASSERT_TRUE(scene.exists(duplicate));
    EXPECT_EQ(scene.parent(duplicate), restoredGroup);
    EXPECT_EQ(scene.primarySelection(), duplicate);
    ASSERT_TRUE(history.undo().ok);
    EXPECT_FALSE(scene.exists(duplicate));

    result = commands.destroyEntity(second);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(scene.exists(second));
    ASSERT_TRUE(history.undo().ok);
    EXPECT_TRUE(scene.exists(scene.primarySelection()));
}

TEST(DocumentCommandServiceTests, EntityPayloadResultsKeepInvalidIdsWhenCommandsFailOrNoop) {
    SceneDocument scene;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);

    const DocumentCreateEntityResult invalidCreate =
            commands.createEntity("Child", EntityKind::Generic, 999u, {});
    EXPECT_FALSE(invalidCreate.ok);
    EXPECT_FALSE(invalidCreate.changed);
    EXPECT_EQ(invalidCreate.entity, InvalidEntityId);
    EXPECT_FALSE(history.canUndo());

    const DocumentGroupSelectionResult emptyGroup = commands.groupSelection("Empty Group", {});
    EXPECT_TRUE(emptyGroup.ok) << emptyGroup.message;
    EXPECT_FALSE(emptyGroup.changed);
    EXPECT_EQ(emptyGroup.groupEntity, InvalidEntityId);
    EXPECT_FALSE(history.canUndo());

    const DocumentDuplicateEntityResult missingDuplicate = commands.duplicateEntity(999u);
    EXPECT_FALSE(missingDuplicate.ok);
    EXPECT_FALSE(missingDuplicate.changed);
    EXPECT_EQ(missingDuplicate.duplicateEntity, InvalidEntityId);
    EXPECT_FALSE(history.canUndo());
}

TEST(DocumentCommandServiceTests, GroupsAreSceneOwnedEditorObjectsNotModelingObjects) {
    ProjectSession session;

    const DocumentCreateEntityResult firstCreated = session.documentCommands().createEntity(
            "First",
            EntityKind::Mesh,
            InvalidEntityId,
            {});
    ASSERT_TRUE(firstCreated.ok) << firstCreated.message;
    const EntityId first = firstCreated.entity;
    const DocumentCreateEntityResult secondCreated = session.documentCommands().createEntity(
            "Second",
            EntityKind::Mesh,
            InvalidEntityId,
            {});
    ASSERT_TRUE(secondCreated.ok) << secondCreated.message;
    const EntityId second = secondCreated.entity;
    ASSERT_TRUE(session.documentCommands().selectEntities({first, second}, second).ok);

    const DocumentGroupSelectionResult grouped = session.documentCommands().groupSelection("Group", {});
    ASSERT_TRUE(grouped.ok) << grouped.message;
    const EntityId group = grouped.groupEntity;
    ASSERT_TRUE(session.scene().exists(group));
    EXPECT_EQ(session.scene().kind(group), EntityKind::Empty);
    EXPECT_EQ(session.scene().modelingObject(group), nullptr);

    const EditorObjectId groupObject = session.scene().editorObjectId(group);
    ASSERT_NE(groupObject, InvalidEditorObjectId);
    const std::optional<ProjectObjectRecord> record = session.document().objectRecord(groupObject);
    ASSERT_TRUE(record.has_value());
    EXPECT_EQ(record->sceneEntity, group);
    EXPECT_EQ(record->modelingObject, InvalidModelingObjectId);

    const ProjectDocumentIdentityReport report = session.document().validateObjectIdentityInvariants();
    EXPECT_TRUE(report.ok()) << (report.issues.empty() ? std::string{} : report.issues.front().message);
}

TEST(EditorHistoryServiceTests, NoopCommandsDoNotClearRedoButNewCommandsDo) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);

    ASSERT_TRUE(commands.renameEntity(entity, "Brush_1").ok);
    ASSERT_TRUE(history.undo().ok);
    EXPECT_TRUE(history.canRedo());

    DocumentCommandResult result = commands.renameEntity(entity, "Brush");
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(history.canRedo());

    result = commands.setVisible(entity, true);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(history.canRedo());

    result = commands.renameEntity(entity, "Brush_2");
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    EXPECT_FALSE(history.canRedo());
}

TEST(EditorHistoryServiceTests, ExternalCommandsShareUndoOrderWithDocumentCommands) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);
    int externalValue = 0;

    CommandResult result = history.execute(
            std::make_unique<TestValueCommand>("External Edit", externalValue, 0, 1));
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(commands.renameEntity(entity, "Brush_1").ok);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_EQ(scene.name(entity)->name, "Brush");
    EXPECT_EQ(externalValue, 1);

    ASSERT_TRUE(history.undo().ok);
    EXPECT_EQ(externalValue, 0);

    ASSERT_TRUE(history.redo().ok);
    EXPECT_EQ(externalValue, 1);

    ASSERT_TRUE(history.redo().ok);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");
}

TEST(UndoRedoManagerTests, FailedExecuteAndFailedRestoreDoNotCorruptStacks) {
    UndoRedoManager undoRedo;
    int value = 0;
    bool failUndo = false;
    bool failRedo = false;

    UndoRedoResult result = undoRedo.execute(
            std::make_unique<TestValueCommand>("Set Value", value, 0, 1));
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(undoRedo.canRedo());

    result = undoRedo.execute(
            std::make_unique<TestValueCommand>("Noop", value, value, value, nullptr, nullptr, true, false));
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(undoRedo.canRedo());

    failRedo = true;
    result = undoRedo.execute(
            std::make_unique<TestValueCommand>("Failed", value, 0, 2, nullptr, &failRedo));
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(undoRedo.canRedo());
    failRedo = false;

    result = undoRedo.redo();
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(undoRedo.canUndo());
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(undoRedo.canRedo());

    failUndo = true;
    result = undoRedo.execute(
            std::make_unique<TestValueCommand>("Fragile", value, 1, 2, &failUndo, &failRedo));
    ASSERT_TRUE(result.ok) << result.message;
    result = undoRedo.undo();
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(undoRedo.canUndo());
    EXPECT_FALSE(undoRedo.canRedo());

    failUndo = false;
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(undoRedo.canRedo());

    failRedo = true;
    result = undoRedo.redo();
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(undoRedo.canRedo());

    failRedo = false;
    ASSERT_TRUE(undoRedo.redo().ok);
    EXPECT_TRUE(undoRedo.canUndo());
}

TEST(ModelingSessionServiceTests, ExposesBoxCommandContract) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCommandDescriptor box = service.boxToolCommand();
    const ToolDescriptor* viewportBox = findViewportToolDescriptor(ModelingViewportTool::Box);
    ASSERT_NE(viewportBox, nullptr);

    EXPECT_EQ(box.command, ModelingCommandId::ActivateBoxTool);
    EXPECT_EQ(box.id, std::string{viewportBox->id});
    EXPECT_EQ(box.label, std::string{viewportBox->label});
    EXPECT_EQ(box.shortcut, std::string{viewportBox->shortcut});
    EXPECT_EQ(box.viewportScoped, viewportBox->ownsViewport);
    EXPECT_TRUE(box.rebuildsMeshOnSuccess);
}

const ModelingActionPanel* findActionPanel(const ModelingActionSet& actions, std::string_view panelId) {
    const auto found = std::find_if(actions.panels.begin(), actions.panels.end(), [&](const ModelingActionPanel& panel) {
        return std::string_view(panel.panelId) == panelId;
    });
    return found == actions.panels.end() ? nullptr : &(*found);
}

const ModelingActionItem* findActionItem(const ModelingActionPanel& panel, std::string_view actionId) {
    const auto found = std::find_if(panel.actions.begin(), panel.actions.end(), [&](const ModelingActionItem& action) {
        return std::string_view(action.actionId) == actionId;
    });
    return found == panel.actions.end() ? nullptr : &(*found);
}

const ModelingActionItem* findActionItem(const std::vector<ModelingActionItem>& actions, std::string_view actionId) {
    const auto found = std::find_if(actions.begin(), actions.end(), [&](const ModelingActionItem& action) {
        return std::string_view(action.actionId) == actionId;
    });
    return found == actions.end() ? nullptr : &(*found);
}

TEST(ModelingSessionServiceTests, ContextActionsExposeObjectPanelAndExecuteHostActionIds) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Action Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    const ModelingActionSet actions = service.contextActionSet({
            .surface = ModelingActionSurface::OperationsPanel,
            .includeDisabled = true,
    });
    EXPECT_EQ(actions.surface, ModelingActionSurface::OperationsPanel);
    EXPECT_EQ(actions.surfaceId, "operations_top_bar");
    ASSERT_EQ(actions.panels.size(), 1u);

    const ModelingActionPanel* objectPanel = findActionPanel(actions, "object_operations");
    ASSERT_NE(objectPanel, nullptr);
    EXPECT_EQ(objectPanel->label, "Object Operations");
    EXPECT_EQ(objectPanel->selectionModeId, "object");
    EXPECT_EQ(objectPanel->visibility, ModelingActionVisibility::Enabled);
    EXPECT_EQ(findActionPanel(actions, "edge_operations"), nullptr);
    EXPECT_EQ(findActionPanel(actions, "face_operations"), nullptr);

    const ModelingActionItem* invert = findActionItem(*objectPanel, "invert_mesh");
    ASSERT_NE(invert, nullptr);
    EXPECT_EQ(invert->label, "Flip Mesh Normals");
    EXPECT_EQ(invert->operationId, "flip_face_normals");
    EXPECT_EQ(invert->visibility, ModelingActionVisibility::Enabled);

    const ModelingActionItem* combine = findActionItem(*objectPanel, "combine_mesh");
    ASSERT_NE(combine, nullptr);
    EXPECT_EQ(combine->operationId, "combine_meshes");
    EXPECT_EQ(combine->visibility, ModelingActionVisibility::Disabled);
    EXPECT_EQ(combine->disabledReasonKey, "modeling.action.disabled.minimum_objects");
    EXPECT_EQ(combine->disabledReasonCount, 2u);

    const ModelingCommandResult disabled = service.executeContextAction("combine_mesh");
    EXPECT_FALSE(disabled.ok);

    const ModelingCommandResult executed = service.executeContextAction("invert_mesh");
    EXPECT_TRUE(executed.ok) << executed.message;
}

TEST(ModelingSessionServiceTests, ContextActionsExposeEdgePanelAndDisableInsufficientCounts) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Edge Action Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Edge).ok);

    const ModelingActionSet actions = service.contextActionSet({
            .surface = ModelingActionSurface::OperationsPanel,
            .includeDisabled = true,
    });
    ASSERT_EQ(actions.panels.size(), 1u);
    EXPECT_EQ(findActionPanel(actions, "object_operations"), nullptr);

    const ModelingActionPanel* edgePanel = findActionPanel(actions, "edge_operations");
    ASSERT_NE(edgePanel, nullptr);
    EXPECT_EQ(edgePanel->selectionModeId, "edge");
    EXPECT_EQ(edgePanel->visibility, ModelingActionVisibility::Enabled);

    const ModelingActionItem* bevel = findActionItem(*edgePanel, "bevel_edges");
    ASSERT_NE(bevel, nullptr);
    EXPECT_EQ(bevel->operationId, "edge_bevel");
    EXPECT_EQ(bevel->previewKind, ModelingActionPreviewKind::EdgeBevel);

    const ModelingActionItem* interpolatedBridge = findActionItem(*edgePanel, "interpolated_bridge_edges");
    ASSERT_NE(interpolatedBridge, nullptr);
    EXPECT_EQ(interpolatedBridge->operationId, "bridge");
    EXPECT_EQ(interpolatedBridge->previewKind, ModelingActionPreviewKind::Bridge);

    const ModelingActionItem* split = findActionItem(*edgePanel, "split_edges");
    ASSERT_NE(split, nullptr);
    EXPECT_EQ(split->operationId, "split_edges");
    EXPECT_EQ(split->visibility, ModelingActionVisibility::Disabled);

    const ModelingActionItem* fillHole = findActionItem(*edgePanel, "fill_hole");
    ASSERT_NE(fillHole, nullptr);
    EXPECT_EQ(fillHole->visibility, ModelingActionVisibility::Disabled);
    EXPECT_EQ(fillHole->disabledReasonKey, "modeling.action.disabled.minimum_edges");
    EXPECT_EQ(fillHole->disabledReasonCount, 3u);

    ModelingActionQuery hiddenQuery;
    hiddenQuery.surface = ModelingActionSurface::OperationsPanel;
    hiddenQuery.includeDisabled = true;
    hiddenQuery.includeHidden = true;
    const ModelingActionSet allPanels = service.contextActionSet(hiddenQuery);
    const ModelingActionPanel* hiddenObjectPanel = findActionPanel(allPanels, "object_operations");
    ASSERT_NE(hiddenObjectPanel, nullptr);
    EXPECT_EQ(hiddenObjectPanel->visibility, ModelingActionVisibility::Hidden);
}

TEST(ModelingSessionServiceTests, PreviewActionsExposeOnlyActiveToolSettingsAndLiveUpdate) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Bevel Preview Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Edge).ok);
    ASSERT_TRUE(service.selectAll().ok);

    const ModelingCommandResult begin = service.executeContextAction("bevel_edges");
    ASSERT_TRUE(begin.ok) << begin.message;

    ModelingToolSettingsTarget target = service.toolSettingsTarget();
    EXPECT_TRUE(target.active);
    EXPECT_TRUE(target.operationPreviewActive);
    EXPECT_EQ(target.kind, ModelingToolSettingsTargetKind::EdgeBevel);
    EXPECT_EQ(target.actionId, "bevel_edges");
    EXPECT_EQ(target.operationId, "edge_bevel");
    EXPECT_EQ(target.label, "Bevel Edges");

    const std::optional<MeshAsset> previewBefore = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(previewBefore.has_value());

    ModelingInteractionSettings settings = service.interactionSettings();
    settings.edgeBevelWidth = 0.25;
    const ModelingCommandResult update = service.setInteractionSettings(settings);
    EXPECT_TRUE(update.ok) << update.message;
    EXPECT_TRUE(update.changed);

    const std::optional<MeshAsset> previewAfter = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(previewAfter.has_value());
    EXPECT_NE(meshPositionFingerprint(*previewAfter), meshPositionFingerprint(*previewBefore));

    const ModelingCommandResult cancel = service.cancelActiveOperationPreview();
    EXPECT_TRUE(cancel.ok) << cancel.message;
    EXPECT_FALSE(service.toolSettingsTarget().active);
}

TEST(ModelingSessionServiceTests, EdgeBevelPreviewRebuildsAfterDirectAddEdgeSelectionWhileActive) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    PreviewClosedEdgeFixture fixture = makePreviewClosedEdgeFixture();

    const ModelingCreateBoxResult mesh = service.createMesh({
            .document = fixture.document,
            .name = "Edge Bevel Preview Add Edge Mesh",
    });
    ASSERT_TRUE(mesh.ok) << mesh.message;

    ModelingInteractionSettings settings = service.interactionSettings();
    settings.edgeBevelWidth = 0.1;
    ASSERT_TRUE(service.setInteractionSettings(settings).ok);

    ModelingCommandResult selected = service.setComponentSelection({
            .modelingObjectId = mesh.object.modelingObjectId,
            .mode = ModelingSelectionMode::Edge,
            .edges = {fixture.firstEdge},
            .activeKind = ModelingActiveSelectionKind::Edge,
            .activeEdge = fixture.firstEdge,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    ASSERT_TRUE(selected.changed);

    const ModelingCommandResult begin = service.executeContextAction("bevel_edges");
    ASSERT_TRUE(begin.ok) << begin.message;
    const std::optional<MeshAsset> firstPreview = service.compileObjectMesh(mesh.object.modelingObjectId);
    ASSERT_TRUE(firstPreview.has_value());

    selected = service.setComponentSelection({
            .modelingObjectId = mesh.object.modelingObjectId,
            .mode = ModelingSelectionMode::Edge,
            .edges = {fixture.secondEdge},
            .edit = ModelingSelectionEdit::Add,
            .activeKind = ModelingActiveSelectionKind::Edge,
            .activeEdge = fixture.secondEdge,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    EXPECT_TRUE(selected.changed);
    EXPECT_GT(service.selectionSummary().selectedEdgeCount, 1u);
    EXPECT_TRUE(service.toolSettingsTarget().operationPreviewActive);

    const std::optional<MeshAsset> addedPreview = service.compileObjectMesh(mesh.object.modelingObjectId);
    ASSERT_TRUE(addedPreview.has_value());
    EXPECT_NE(meshPositionFingerprint(*firstPreview), meshPositionFingerprint(*addedPreview));
}

TEST(ModelingSessionServiceTests, ThickenPreviewRebuildsAfterDirectAddFaceSelectionWhileActive) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    PreviewParallelQuadFixture fixture = makePreviewParallelQuadFixture();

    const ModelingCreateBoxResult mesh = service.createMesh({
            .document = fixture.document,
            .name = "Thicken Preview Add Face Mesh",
    });
    ASSERT_TRUE(mesh.ok) << mesh.message;

    ModelingCommandResult selected = service.setComponentSelection({
            .modelingObjectId = mesh.object.modelingObjectId,
            .mode = ModelingSelectionMode::Face,
            .faces = {fixture.frontFace},
            .activeKind = ModelingActiveSelectionKind::Face,
            .activeFace = fixture.frontFace,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    ASSERT_TRUE(selected.changed);

    const ModelingCommandResult begin = service.executeContextAction("thicken_faces");
    ASSERT_TRUE(begin.ok) << begin.message;
    const std::optional<MeshAsset> firstPreview = service.compileObjectMesh(mesh.object.modelingObjectId);
    ASSERT_TRUE(firstPreview.has_value());

    selected = service.setComponentSelection({
            .modelingObjectId = mesh.object.modelingObjectId,
            .mode = ModelingSelectionMode::Face,
            .faces = {fixture.backFace},
            .edit = ModelingSelectionEdit::Add,
            .activeKind = ModelingActiveSelectionKind::Face,
            .activeFace = fixture.backFace,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    EXPECT_TRUE(selected.changed);
    EXPECT_GT(service.selectionSummary().selectedFaceCount, 1u);
    EXPECT_TRUE(service.toolSettingsTarget().operationPreviewActive);

    const std::optional<MeshAsset> addedPreview = service.compileObjectMesh(mesh.object.modelingObjectId);
    ASSERT_TRUE(addedPreview.has_value());
    EXPECT_NE(meshPositionFingerprint(*firstPreview), meshPositionFingerprint(*addedPreview));
}

TEST(ModelingSessionServiceTests, BridgePreviewRebuildsAfterDirectReplaceFaceSelectionWhileActive) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    PreviewBridgeFixture fixture = makePreviewBridgeFixture();

    const ModelingCreateBoxResult mesh = service.createMesh({
            .document = fixture.document,
            .name = "Bridge Preview Replace Face Mesh",
    });
    ASSERT_TRUE(mesh.ok) << mesh.message;

    ModelingCommandResult selected = service.setComponentSelection({
            .modelingObjectId = mesh.object.modelingObjectId,
            .mode = ModelingSelectionMode::Face,
            .faces = {fixture.firstFrontFace, fixture.firstBackFace},
            .activeKind = ModelingActiveSelectionKind::Face,
            .activeFace = fixture.firstBackFace,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    ASSERT_TRUE(selected.changed);

    const ModelingCommandResult begin = service.executeContextAction("interpolated_bridge_faces");
    ASSERT_TRUE(begin.ok) << begin.message;
    const std::optional<MeshAsset> firstPreview = service.compileObjectMesh(mesh.object.modelingObjectId);
    ASSERT_TRUE(firstPreview.has_value());

    selected = service.setComponentSelection({
            .modelingObjectId = mesh.object.modelingObjectId,
            .mode = ModelingSelectionMode::Face,
            .faces = {fixture.secondFrontFace, fixture.secondBackFace},
            .activeKind = ModelingActiveSelectionKind::Face,
            .activeFace = fixture.secondBackFace,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    EXPECT_TRUE(selected.changed);
    EXPECT_GE(service.selectionSummary().selectedFaceCount, 2u);
    EXPECT_TRUE(service.toolSettingsTarget().operationPreviewActive);

    const std::optional<MeshAsset> replacedPreview = service.compileObjectMesh(mesh.object.modelingObjectId);
    ASSERT_TRUE(replacedPreview.has_value());
    EXPECT_NE(meshPositionFingerprint(*firstPreview), meshPositionFingerprint(*replacedPreview));
}

TEST(ModelingSessionServiceTests, MergeByDistancePreviewRebuildsAfterDirectAddVertexSelectionWhileActive) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    PreviewMergeDistanceFixture fixture = makePreviewMergeDistanceFixture();

    const ModelingCreateBoxResult mesh = service.createMesh({
            .document = fixture.document,
            .name = "Merge Distance Preview Add Vertex Mesh",
    });
    ASSERT_TRUE(mesh.ok) << mesh.message;

    ModelingCommandResult selected = service.setComponentSelection({
            .modelingObjectId = mesh.object.modelingObjectId,
            .mode = ModelingSelectionMode::Vertex,
            .vertices = {fixture.firstNearA, fixture.firstNearB},
            .activeKind = ModelingActiveSelectionKind::Vertex,
            .activeVertex = fixture.firstNearB,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    ASSERT_TRUE(selected.changed);

    const ModelingCommandResult begin = service.executeContextAction("merge_by_distance");
    ASSERT_TRUE(begin.ok) << begin.message;
    const std::optional<MeshAsset> firstPreview = service.compileObjectMesh(mesh.object.modelingObjectId);
    ASSERT_TRUE(firstPreview.has_value());

    selected = service.setComponentSelection({
            .modelingObjectId = mesh.object.modelingObjectId,
            .mode = ModelingSelectionMode::Vertex,
            .vertices = {fixture.secondNearA, fixture.secondNearB},
            .edit = ModelingSelectionEdit::Add,
            .activeKind = ModelingActiveSelectionKind::Vertex,
            .activeVertex = fixture.secondNearB,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    EXPECT_TRUE(selected.changed);
    EXPECT_GT(service.selectionSummary().selectedVertexCount, 0u);
    EXPECT_TRUE(service.toolSettingsTarget().operationPreviewActive);

    const std::optional<MeshAsset> addedPreview = service.compileObjectMesh(mesh.object.modelingObjectId);
    ASSERT_TRUE(addedPreview.has_value());
    EXPECT_NE(meshPositionFingerprint(*firstPreview), meshPositionFingerprint(*addedPreview));
}

TEST(ModelingSessionServiceTests, SliceQuadPreviewRebuildsAfterFacePickWhileActive) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Slice Preview Rebuild Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingSelectionSummary frontPicked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
    });
    ASSERT_EQ(frontPicked.selectedFaceCount, 1u);

    const ModelingCommandResult begin = service.executeContextAction("slice_quad");
    ASSERT_TRUE(begin.ok) << begin.message;
    const std::optional<MeshAsset> frontPreview = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(frontPreview.has_value());

    const ModelingSelectionSummary sidePicked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {5.0f, 0.0f, 0.0f}, .direction = {-1.0f, 0.0f, 0.0f}},
            .clickCount = 1,
    });
    ASSERT_EQ(sidePicked.selectedFaceCount, 4u);
    EXPECT_TRUE(service.toolSettingsTarget().operationPreviewActive);
    const std::optional<QVec3> sideCenter = service.selectionCenter();
    ASSERT_TRUE(sideCenter.has_value());
    EXPECT_NEAR(sideCenter->x, 1.0f, 0.001f);

    const std::optional<MeshAsset> sidePreview = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(sidePreview.has_value());
    EXPECT_NE(meshPositionFingerprint(*frontPreview), meshPositionFingerprint(*sidePreview));
}

TEST(ModelingSessionServiceTests, SliceQuadPreviewRebuildsAfterAddFacePickWhileActive) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Slice Preview Add Face Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingSelectionSummary frontPicked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
    });
    ASSERT_EQ(frontPicked.selectedFaceCount, 1u);

    const ModelingCommandResult begin = service.executeContextAction("slice_quad");
    ASSERT_TRUE(begin.ok) << begin.message;
    const std::optional<MeshAsset> frontPreview = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(frontPreview.has_value());
    const MeshComponent* frontSceneMeshComponent = scene.mesh(box.object.entity);
    ASSERT_NE(frontSceneMeshComponent, nullptr);
    const MeshAsset* frontScenePreview = assets.mesh(frontSceneMeshComponent->mesh);
    ASSERT_NE(frontScenePreview, nullptr);
    const double frontScenePreviewFingerprint = meshPositionFingerprint(*frontScenePreview);

    const ModelingSelectionSummary added = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {5.0f, 0.0f, 0.0f}, .direction = {-1.0f, 0.0f, 0.0f}},
            .clickCount = 1,
            .shiftPressed = true,
    });
    EXPECT_GT(added.selectedFaceCount, 4u);
    EXPECT_TRUE(service.toolSettingsTarget().operationPreviewActive);

    const std::optional<MeshAsset> addPreview = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(addPreview.has_value());
    EXPECT_NE(meshPositionFingerprint(*frontPreview), meshPositionFingerprint(*addPreview));
    const MeshComponent* addSceneMeshComponent = scene.mesh(box.object.entity);
    ASSERT_NE(addSceneMeshComponent, nullptr);
    const MeshAsset* addScenePreview = assets.mesh(addSceneMeshComponent->mesh);
    ASSERT_NE(addScenePreview, nullptr);
    EXPECT_NE(frontScenePreviewFingerprint, meshPositionFingerprint(*addScenePreview));
}

TEST(ModelingSessionServiceTests, SliceQuadPreviewRebuildsAfterDirectAddFaceSelectionWhileActive) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Slice Preview Direct Add Face Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    ModelingCommandResult selected = service.setComponentSelection({
            .modelingObjectId = box.object.modelingObjectId,
            .mode = ModelingSelectionMode::Face,
            .faces = {1},
            .activeKind = ModelingActiveSelectionKind::Face,
            .activeFace = 1,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    ASSERT_TRUE(selected.changed);

    const ModelingCommandResult begin = service.executeContextAction("slice_quad");
    ASSERT_TRUE(begin.ok) << begin.message;
    const std::optional<MeshAsset> firstPreview = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(firstPreview.has_value());

    selected = service.setComponentSelection({
            .modelingObjectId = box.object.modelingObjectId,
            .mode = ModelingSelectionMode::Face,
            .faces = {2},
            .edit = ModelingSelectionEdit::Add,
            .activeKind = ModelingActiveSelectionKind::Face,
            .activeFace = 2,
    });
    ASSERT_TRUE(selected.ok) << selected.message;
    EXPECT_TRUE(selected.changed);
    const ModelingSelectionSummary summary = service.selectionSummary();
    EXPECT_EQ(summary.mode, ModelingSelectionMode::Face);
    EXPECT_GT(summary.selectedFaceCount, 1u);
    EXPECT_TRUE(service.toolSettingsTarget().operationPreviewActive);

    const std::optional<MeshAsset> addedPreview = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(addedPreview.has_value());
    EXPECT_NE(meshPositionFingerprint(*firstPreview), meshPositionFingerprint(*addedPreview));
}

TEST(ModelingSessionServiceTests, SliceQuadPreviewRebuildsAfterFaceBoxSelectWhileActive) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Slice Preview Box Select Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingSelectionSummary frontPicked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
    });
    ASSERT_EQ(frontPicked.selectedFaceCount, 1u);

    const ModelingCommandResult begin = service.executeContextAction("slice_quad");
    ASSERT_TRUE(begin.ok) << begin.message;
    const std::optional<MeshAsset> frontPreview = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(frontPreview.has_value());

    ModelingViewportCamera sideCamera = testOrthographicViewportCamera();
    sideCamera.origin = {10.0f, 0.0f, 0.0f};
    sideCamera.xAxis = {0.0f, 1.0f, 0.0f};
    sideCamera.yAxis = {0.0f, 0.0f, 1.0f};
    sideCamera.zAxis = {1.0f, 0.0f, 0.0f};
    sideCamera.orthographicSize = 4.0;

    const ModelingSelectionSummary boxSelected = service.boxSelect(ModelingBoxSelectRequest{
            .camera = sideCamera,
            .start = {20.0f, 20.0f},
            .end = {80.0f, 80.0f},
            .edit = ModelingSelectionEdit::Replace,
            .rule = ModelingBoxSelectionRule::Contain,
    });
    EXPECT_GT(boxSelected.selectedFaceCount, 4u);
    EXPECT_TRUE(service.toolSettingsTarget().operationPreviewActive);

    const std::optional<MeshAsset> boxPreview = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(boxPreview.has_value());
    EXPECT_NE(meshPositionFingerprint(*frontPreview), meshPositionFingerprint(*boxPreview));
}

TEST(ModelingSessionServiceTests, ContextActionsExposeFacePanelAndKeepBridgeDisabledForOneFace) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Face Action Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
    });
    ASSERT_EQ(picked.selectedFaceCount, 1u);

    const ModelingActionSet actions = service.contextActionSet({
            .surface = ModelingActionSurface::OperationsPanel,
            .includeDisabled = true,
    });
    ASSERT_EQ(actions.panels.size(), 1u);
    EXPECT_EQ(findActionPanel(actions, "object_operations"), nullptr);
    EXPECT_EQ(findActionPanel(actions, "edge_operations"), nullptr);

    const ModelingActionPanel* facePanel = findActionPanel(actions, "face_operations");
    ASSERT_NE(facePanel, nullptr);
    EXPECT_EQ(facePanel->selectionModeId, "face");
    EXPECT_EQ(facePanel->visibility, ModelingActionVisibility::Enabled);

    const ModelingActionItem* invert = findActionItem(*facePanel, "invert_faces");
    ASSERT_NE(invert, nullptr);
    EXPECT_EQ(invert->operationId, "flip_face_normals");
    EXPECT_EQ(invert->visibility, ModelingActionVisibility::Enabled);

    const ModelingActionItem* bridge = findActionItem(*facePanel, "bridge_faces");
    ASSERT_NE(bridge, nullptr);
    EXPECT_EQ(bridge->operationId, "bridge");
    EXPECT_EQ(bridge->visibility, ModelingActionVisibility::Disabled);

    const ModelingActionItem* interpolatedBridge = findActionItem(*facePanel, "interpolated_bridge_faces");
    ASSERT_NE(interpolatedBridge, nullptr);
    EXPECT_EQ(interpolatedBridge->operationId, "bridge");
    EXPECT_EQ(interpolatedBridge->previewKind, ModelingActionPreviewKind::Bridge);

    const ModelingCommandResult executed = service.executeContextAction("invert_faces");
    EXPECT_TRUE(executed.ok) << executed.message;
}

TEST(ModelingSessionServiceTests, RmbMeshPickWithExistingFaceSelectionPreservesSelectionAndAllowsContextMenu) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "RMB Face Context Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
    });
    ASSERT_TRUE(picked.hasSelection);
    ASSERT_EQ(picked.selectedFaceCount, 1u);

    const ModelingSelectionSummary multiPicked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, -5.0f}, .direction = {0.0f, 0.0f, 1.0f}},
            .clickCount = 1,
            .shiftPressed = true,
    });
    ASSERT_TRUE(multiPicked.hasSelection);
    ASSERT_EQ(multiPicked.selectedFaceCount, 2u);
    const int activeObjectBeforeContext = multiPicked.activeObjectId;
    const int activeFaceBeforeContext = multiPicked.activeFaceId;

    const ModelingSelectionSummary rmbOnUnselectedWithExistingSelection = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {5.0f, 0.0f, 0.0f}, .direction = {-1.0f, 0.0f, 0.0f}},
            .clickCount = 1,
            .preserveSelectionOnMiss = true,
            .preserveSelectionOnSelectedHit = true,
            .preserveExistingComponentSelectionOnHit = true,
    });
    ASSERT_TRUE(rmbOnUnselectedWithExistingSelection.hasSelection);
    EXPECT_EQ(rmbOnUnselectedWithExistingSelection.activeObjectId, activeObjectBeforeContext);
    EXPECT_EQ(rmbOnUnselectedWithExistingSelection.activeFaceId, activeFaceBeforeContext);
    EXPECT_EQ(rmbOnUnselectedWithExistingSelection.selectedFaceCount, 2u);

    const ModelingSelectionSummary rmbOnAlreadySelected = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
            .preserveSelectionOnMiss = true,
            .preserveSelectionOnSelectedHit = true,
            .preserveExistingComponentSelectionOnHit = true,
    });
    ASSERT_TRUE(rmbOnAlreadySelected.hasSelection);
    EXPECT_EQ(rmbOnAlreadySelected.selectedFaceCount, 2u);

    ModelingActionQuery query;
    query.surface = ModelingActionSurface::ViewportContextMenu;
    query.includeDisabled = true;
    const std::vector<ModelingActionItem> actions = service.contextActions(query);
    ASSERT_FALSE(actions.empty());

    EXPECT_TRUE(shouldOpenViewportContextMenu({
            .rightPressTracked = true,
            .rightReleased = true,
            .releaseInsideViewport = true,
            .targetHit = rmbOnAlreadySelected.hasSelection,
            .hasActions = !actions.empty(),
            .pressScreen = {320.0f, 240.0f},
            .releaseScreen = {320.0f, 240.0f},
    }));

    const ModelingSelectionSummary missedWithPreservedSelection = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {8.0f, 8.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
            .preserveSelectionOnMiss = true,
    });
    ASSERT_TRUE(missedWithPreservedSelection.hasSelection);
    EXPECT_EQ(missedWithPreservedSelection.selectedFaceCount, 2u);
    EXPECT_TRUE(shouldOpenViewportContextMenu({
            .rightPressTracked = true,
            .rightReleased = true,
            .releaseInsideViewport = true,
            .targetHit = missedWithPreservedSelection.hasSelection,
            .hasActions = true,
            .pressScreen = {100.0f, 100.0f},
            .releaseScreen = {100.0f, 100.0f},
    }));

    const ModelingSelectionSummary missedWithoutPreservingSelection = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {8.0f, 8.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
    });
    EXPECT_FALSE(missedWithoutPreservingSelection.hasSelection);
}

TEST(ModelingSessionServiceTests, RmbObjectPickOnSelectedMultiSelectionPreservesSelection) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult left = service.createBoxFromBounds({
            .bounds = {.min = {-4.0f, -1.0f, -1.0f}, .max = {-2.0f, 1.0f, 1.0f}},
            .name = "Left RMB Context Box",
    });
    ASSERT_TRUE(left.ok) << left.message;
    const ModelingCreateBoxResult right = service.createBoxFromBounds({
            .bounds = {.min = {2.0f, -1.0f, -1.0f}, .max = {4.0f, 1.0f, 1.0f}},
            .name = "Right RMB Context Box",
    });
    ASSERT_TRUE(right.ok) << right.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Object).ok);
    ASSERT_TRUE(service.clearSelection().ok);

    const ModelingRay leftRay{
            .origin = {-3.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const ModelingRay rightRay{
            .origin = {3.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };

    ModelingSelectionSummary summary = service.pickSelect(ModelingPickSelectRequest{
            .ray = leftRay,
            .clickCount = 1,
    });
    ASSERT_EQ(summary.selectedObjectCount, 1u);

    summary = service.pickSelect(ModelingPickSelectRequest{
            .ray = rightRay,
            .clickCount = 1,
            .shiftPressed = true,
    });
    ASSERT_EQ(summary.selectedObjectCount, 2u);

    summary = service.pickSelect(ModelingPickSelectRequest{
            .ray = leftRay,
            .clickCount = 1,
            .preserveSelectionOnMiss = true,
            .preserveSelectionOnSelectedHit = true,
    });
    EXPECT_EQ(summary.selectedObjectCount, 2u);
}

TEST(ModelingSessionServiceTests, ContextActionsHideWhileBoxOrModalOwnsViewport) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    ModelingActionQuery contextQuery;
    contextQuery.surface = ModelingActionSurface::ViewportContextMenu;
    contextQuery.includeDisabled = true;

    ModelingActionQuery explicitModalQuery = contextQuery;
    explicitModalQuery.modalToolOwnsViewport = true;
    EXPECT_TRUE(service.contextActionSet(explicitModalQuery).panels.empty());
    EXPECT_TRUE(service.contextActions(explicitModalQuery).empty());

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Modal Action Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_FALSE(service.contextActions(contextQuery).empty());

    ASSERT_TRUE(service.activateBoxTool().ok);
    EXPECT_TRUE(service.contextActionSet().panels.empty());
    EXPECT_TRUE(service.contextActions(contextQuery).empty());
    EXPECT_FALSE(service.executeContextAction("invert_mesh").ok);

    ModelingActionQuery shelfQuery;
    shelfQuery.surface = ModelingActionSurface::OperationsPanel;
    shelfQuery.includeDisabled = true;
    shelfQuery.includeHidden = false;
    shelfQuery.suppressWhenModalToolOwnsViewport = false;
    EXPECT_FALSE(service.contextActionSet(shelfQuery).panels.empty());

    const ModelingCommandResult cancel = service.cancelBox();
    ASSERT_TRUE(cancel.ok) << cancel.message;
    EXPECT_FALSE(service.contextActions(contextQuery).empty());
}

TEST(ModelingSessionServiceTests, CreatesBoxThroughRuntimeWhenAvailable) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult result = service.createBoxFromBounds({
        .bounds = {
            .min = {-2.0f, -1.0f, -3.0f},
            .max = {2.0f, 1.0f, 3.0f},
        },
        .name = "Test Box",
    });

    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    EXPECT_GT(result.object.modelingObjectId, 0);
    EXPECT_NE(result.object.entity, InvalidEntityId);
    EXPECT_NE(result.object.mesh, InvalidAssetId);
    EXPECT_EQ(service.entityForModelingObject(result.object.modelingObjectId), result.object.entity);
    EXPECT_EQ(service.modelingObjectForEntity(result.object.entity), result.object.modelingObjectId);

    const MeshAsset* storedMesh = assets.mesh(result.object.mesh);
    ASSERT_NE(storedMesh, nullptr);
    EXPECT_EQ(storedMesh->name, "Test Box");
    EXPECT_FALSE(storedMesh->vertices.empty());
    EXPECT_FALSE(storedMesh->indices.empty());
    EXPECT_TRUE(storedMesh->hasStableCpuTopology);
    const std::optional<SceneBounds> selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    const QVec3 selectedCenter = boundsCenter(*selectedBounds);
    EXPECT_NEAR(selectedCenter.x, 0.0f, 0.001f);
    EXPECT_NEAR(selectedCenter.y, 0.0f, 0.001f);
    EXPECT_NEAR(selectedCenter.z, 0.0f, 0.001f);
    ASSERT_NE(scene.mesh(result.object.entity), nullptr);
    EXPECT_EQ(scene.mesh(result.object.entity)->mesh, result.object.mesh);
    ASSERT_NE(scene.modelingObject(result.object.entity), nullptr);
    EXPECT_EQ(scene.modelingObject(result.object.entity)->objectId, result.object.modelingObjectId);
}

TEST(ModelingSessionServiceTests, InsertEdgeLoopDragCommitsMesh) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Edge Loop Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    const std::optional<MeshAsset> before = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(before.has_value());

    ModelingCommandResult command = service.activateViewportTool(ModelingViewportTool::InsertEdgeLoop);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Edge);
    EXPECT_TRUE(service.toolStatus().insertEdgeLoopActive);

    command = service.beginInsertEdgeLoopDrag(testInsertEdgeLoopEvent());
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().insertEdgeLoopDragging);
    EXPECT_TRUE(service.toolStatus().insertEdgeLoopHasPreview);

    command = service.commitInsertEdgeLoopDrag();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_FALSE(service.toolStatus().insertEdgeLoopActive);
    EXPECT_FALSE(service.toolStatus().insertEdgeLoopDragging);
    EXPECT_FALSE(service.toolStatus().insertEdgeLoopHasPreview);
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Edge);

    const std::optional<MeshAsset> after = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(after->vertices.size(), before->vertices.size());
    EXPECT_GT(after->indices.size(), before->indices.size());
}

TEST(ModelingSessionServiceTests, InsertEdgeLoopCommitSurvivesReleaseFrameHover) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Release Hover Edge Loop Source",
    });
    ASSERT_TRUE(box.ok) << box.message;

    std::optional<MeshAsset> before = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(before.has_value());

    ModelingCommandResult command = service.activateViewportTool(ModelingViewportTool::InsertEdgeLoop);
    ASSERT_TRUE(command.ok) << command.message;
    command = service.beginInsertEdgeLoopDrag(testInsertEdgeLoopEvent());
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(service.toolStatus().insertEdgeLoopDragging);

    command = service.updateInsertEdgeLoopHover(testInsertEdgeLoopEvent());
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().insertEdgeLoopDragging);

    command = service.commitInsertEdgeLoopDrag();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_FALSE(service.toolStatus().insertEdgeLoopActive);

    std::optional<MeshAsset> after = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(after->vertices.size(), before->vertices.size());
}

TEST(ModelingSessionServiceTests, InsertEdgeLoopUsesViewportPickRadius) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Near Edge Loop Source",
    });
    ASSERT_TRUE(box.ok) << box.message;

    ModelingCommandResult command = service.activateViewportTool(ModelingViewportTool::InsertEdgeLoop);
    ASSERT_TRUE(command.ok) << command.message;

    ModelingToolRayEvent event = testInsertEdgeLoopEvent(0.0f, 0.86f);
    event.edgeRadius = 0.2;
    command = service.beginInsertEdgeLoopDrag(event);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().insertEdgeLoopDragging);
    EXPECT_TRUE(service.toolStatus().insertEdgeLoopHasPreview);

    command = service.commitInsertEdgeLoopDrag();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_FALSE(service.toolStatus().insertEdgeLoopActive);
}

TEST(ModelingSessionServiceTests, CutAndMirrorEmitDrawnPlaneOverlay) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Plane Overlay Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    const AssetId previewMaterial = assets.addMaterial(makeDefaultQuaderMaterial("Mirror Preview Material"));
    scene.setMaterial(box.object.entity, MaterialComponent{.material = previewMaterial});

    auto expectDrawnPlaneOverlay = [&](ModelingViewportTool tool) {
        ModelingCommandResult command = service.activateViewportTool(tool);
        ASSERT_TRUE(command.ok) << command.message;
        command = tool == ModelingViewportTool::Cut
                ? service.clickCut(testDrawnPlaneEvent(0.0f))
                : service.clickMirror(testDrawnPlaneEvent(0.0f));
        ASSERT_TRUE(command.ok) << command.message;
        command = tool == ModelingViewportTool::Cut
                ? service.updateCutHover(testDrawnPlaneEvent(1.0f, false))
                : service.updateMirrorHover(testDrawnPlaneEvent(1.0f, false));
        ASSERT_TRUE(command.ok) << command.message;

        const ViewportOverlayFrame hoverOnlyFrame = service.selectionOverlayFrame();
        const auto hoverOnlyHandleLines = static_cast<std::size_t>(std::count_if(
                hoverOnlyFrame.lines.begin(),
                hoverOnlyFrame.lines.end(),
                [](const ViewportOverlayLine& line) {
                    return line.layer == ViewportOverlayLayer::ToolHandle &&
                            line.color == kOverlayToolHandleColor &&
                            line.widthPixels == 2.0f;
                }));
        const auto hoverOnlyHandlePoints = static_cast<std::size_t>(std::count_if(
                hoverOnlyFrame.points.begin(),
                hoverOnlyFrame.points.end(),
                [](const ViewportOverlayPoint& point) {
                    return point.layer == ViewportOverlayLayer::ToolHandle &&
                            point.color == kOverlayToolHandleColor &&
                            point.sizePixels == 8.0f;
                }));
        const auto hoverOnlyPreviewFills = static_cast<std::size_t>(std::count_if(
                hoverOnlyFrame.triangles.begin(),
                hoverOnlyFrame.triangles.end(),
                [](const ViewportOverlayTriangle& triangle) {
                    return triangle.layer == ViewportOverlayLayer::ToolPreviewFill;
                }));
        const auto hoverOnlyPreviewLines = static_cast<std::size_t>(std::count_if(
                hoverOnlyFrame.lines.begin(),
                hoverOnlyFrame.lines.end(),
                [](const ViewportOverlayLine& line) {
                    return line.layer == ViewportOverlayLayer::ToolPreviewLine ||
                            line.layer == ViewportOverlayLayer::ToolPreviewSeam;
                }));
        EXPECT_EQ(hoverOnlyHandleLines, 1u);
        EXPECT_EQ(hoverOnlyHandlePoints, 2u);
        EXPECT_EQ(hoverOnlyPreviewFills, 0u);
        EXPECT_EQ(hoverOnlyPreviewLines, 0u);

        command = tool == ModelingViewportTool::Cut
                ? service.updateCutHover(testDrawnPlaneEvent(1.0f, true))
                : service.updateMirrorHover(testDrawnPlaneEvent(1.0f, true));
        ASSERT_TRUE(command.ok) << command.message;

        const ViewportOverlayFrame frame = service.selectionOverlayFrame();
        const auto toolPlanes = static_cast<std::size_t>(std::count_if(
                frame.triangles.begin(),
                frame.triangles.end(),
                [](const ViewportOverlayTriangle& triangle) {
                    return triangle.layer == ViewportOverlayLayer::ToolPlane &&
                            triangle.color == kOverlayToolPlaneColor;
                }));
        const auto planeLines = static_cast<std::size_t>(std::count_if(
                frame.lines.begin(),
                frame.lines.end(),
                [](const ViewportOverlayLine& line) {
                    return line.layer == ViewportOverlayLayer::ToolPlaneLine &&
                            line.color == kOverlayToolPlaneLineColor &&
                            line.widthPixels == 1.0f;
                }));
        const auto handleLines = static_cast<std::size_t>(std::count_if(
                frame.lines.begin(),
                frame.lines.end(),
                [](const ViewportOverlayLine& line) {
                    return line.layer == ViewportOverlayLayer::ToolHandle &&
                            line.color == kOverlayToolHandleColor &&
                            line.widthPixels == 2.0f;
                }));
        const auto handlePoints = static_cast<std::size_t>(std::count_if(
                frame.points.begin(),
                frame.points.end(),
                [](const ViewportOverlayPoint& point) {
                    return point.layer == ViewportOverlayLayer::ToolHandle &&
                            point.color == kOverlayToolHandleColor &&
                            point.sizePixels == 8.0f;
                }));
        EXPECT_EQ(toolPlanes, 2u);
        EXPECT_EQ(planeLines, 4u);
        EXPECT_EQ(handleLines, 1u);
        EXPECT_EQ(handlePoints, 2u);

        command = tool == ModelingViewportTool::Cut
                ? service.endCutDrag()
                : service.endMirrorDrag();
        ASSERT_TRUE(command.ok) << command.message;

        const ViewportOverlayFrame previewFrame = service.selectionOverlayFrame();
        const auto previewFills = static_cast<std::size_t>(std::count_if(
                previewFrame.triangles.begin(),
                previewFrame.triangles.end(),
                [](const ViewportOverlayTriangle& triangle) {
                    return triangle.layer == ViewportOverlayLayer::ToolPreviewFill;
                }));
        const auto previewLines = static_cast<std::size_t>(std::count_if(
                previewFrame.lines.begin(),
                previewFrame.lines.end(),
                [](const ViewportOverlayLine& line) {
                    return line.layer == ViewportOverlayLayer::ToolPreviewLine ||
                            line.layer == ViewportOverlayLayer::ToolPreviewSeam;
                }));
        if (tool == ModelingViewportTool::Cut) {
            EXPECT_GT(previewFills, 0u);
            EXPECT_GT(previewLines, 0u);

            const ViewportOverlayFrame keepBothFrame = previewFrame;
            ModelingCommandResult modeResult = service.setCutMode(ModelingCutMode::DiscardLeft);
            ASSERT_TRUE(modeResult.ok) << modeResult.message;
            const ViewportOverlayFrame discardLeftFrame = service.selectionOverlayFrame();
            const auto keepBothOrangeFills = static_cast<std::size_t>(std::count_if(
                    keepBothFrame.triangles.begin(),
                    keepBothFrame.triangles.end(),
                    [](const ViewportOverlayTriangle& triangle) {
                        return triangle.layer == ViewportOverlayLayer::ToolPreviewFill &&
                                triangle.color == kOverlayCutPreviewKeepBothDiscardFillColor;
                    }));
            const auto discardLeftRedFills = static_cast<std::size_t>(std::count_if(
                    discardLeftFrame.triangles.begin(),
                    discardLeftFrame.triangles.end(),
                    [](const ViewportOverlayTriangle& triangle) {
                        return triangle.layer == ViewportOverlayLayer::ToolPreviewFill &&
                                triangle.color == kOverlayCutPreviewDiscardFillColor;
                    }));
            EXPECT_GT(keepBothOrangeFills, 0u);
            EXPECT_GT(discardLeftRedFills, 0u);
        } else {
            EXPECT_EQ(previewFills, 0u);
            EXPECT_EQ(previewLines, 0u);
            const std::vector<ModelingToolPreviewMesh> previewMeshes = service.toolPreviewMeshes();
            ASSERT_FALSE(previewMeshes.empty());
            EXPECT_EQ(previewMeshes.front().sourceEntity, box.object.entity);
            EXPECT_EQ(previewMeshes.front().material, previewMaterial);
            EXPECT_GT(previewMeshes.front().mesh.vertices.size(), 0u);
            EXPECT_GT(previewMeshes.front().mesh.indices.size(), 0u);
        }
        (void)service.cancelActiveModalTool();
    };

    expectDrawnPlaneOverlay(ModelingViewportTool::Cut);
    expectDrawnPlaneOverlay(ModelingViewportTool::Mirror);
}

TEST(ModelingSessionServiceTests, CutToolDragEndAndFinalizeSplitMesh) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Cut Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(scene.entities().size(), 1u);

    ModelingCommandResult command = service.activateViewportTool(ModelingViewportTool::Cut);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().cutActive);

    command = service.clickCut(testDrawnPlaneEvent(0.0f));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().cutDragging);
    EXPECT_TRUE(service.toolStatus().cutDraggingNewPoint);
    EXPECT_EQ(service.toolStatus().cutPointCount, 2);
    EXPECT_EQ(service.toolStatus().cutCapturedPointCount, 1);
    EXPECT_TRUE(service.toolStatus().cutHasLine);
    EXPECT_FALSE(service.toolStatus().cutCanPreview);

    command = service.updateCutHover(testDrawnPlaneEvent(1.0f, false));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().cutDragging);
    EXPECT_TRUE(service.toolStatus().cutDraggingNewPoint);
    EXPECT_EQ(service.toolStatus().cutCapturedPointCount, 1);
    EXPECT_EQ(service.toolStatus().cutPointCount, 2);
    EXPECT_TRUE(service.toolStatus().cutHasLine);

    command = service.updateCutHover(testDrawnPlaneEvent(1.0f, true));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().cutDragging);
    EXPECT_TRUE(service.toolStatus().cutDraggingNewPoint);
    EXPECT_EQ(service.toolStatus().cutCapturedPointCount, 2);
    EXPECT_EQ(service.toolStatus().cutPointCount, 2);
    EXPECT_TRUE(service.toolStatus().cutHasLine);
    command = service.endCutDrag();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_FALSE(service.toolStatus().cutDragging);
    EXPECT_EQ(service.toolStatus().cutCapturedPointCount, 2);
    EXPECT_EQ(service.toolStatus().cutPointCount, 2);
    EXPECT_TRUE(service.toolStatus().cutCanPreview);
    EXPECT_TRUE(service.toolStatus().cutPreviewValid);

    command = service.finalizeCutTool();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(service.toolStatus().activeTool, ModelingViewportTool::Select);
    EXPECT_FALSE(service.toolStatus().cutActive);
    EXPECT_FALSE(service.toolSettingsTarget().active);
    EXPECT_GT(scene.entities().size(), 1u);
}

TEST(ModelingSessionServiceTests, CutToolActivationPromotesSelectedSceneMeshToObjectMode) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Cut Activation Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(scene.selectedEntities().size(), 1u);

    ModelingCommandResult command = service.setSelectionMode(ModelingSelectionMode::Vertex);
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Vertex);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);

    command = service.activateViewportTool(ModelingViewportTool::Cut);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Object);
    EXPECT_EQ(service.selectionSummary().selectedObjectCount, 1u);
    EXPECT_EQ(service.toolStatus().activeTool, ModelingViewportTool::Cut);
    EXPECT_TRUE(service.toolStatus().cutActive);
    EXPECT_TRUE(service.toolSettingsTarget().active);
    EXPECT_EQ(service.toolSettingsTarget().kind, ModelingToolSettingsTargetKind::Cut);
}

TEST(ModelingSessionServiceTests, CutAndMirrorActivationPromotesRuntimeSelectionWhenSceneSelectionIsStale) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Drawn Plane Activation Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(service.selectionSummary().selectedObjectCount, 1u);

    scene.clearSelection();
    ASSERT_TRUE(scene.selectedEntities().empty());

    ModelingCommandResult command = service.activateViewportTool(ModelingViewportTool::Mirror);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Object);
    EXPECT_EQ(service.selectionSummary().selectedObjectCount, 1u);
    EXPECT_EQ(service.toolStatus().activeTool, ModelingViewportTool::Mirror);
    EXPECT_TRUE(service.toolStatus().mirrorActive);
    EXPECT_FALSE(service.toolSettingsTarget().active);
    EXPECT_EQ(scene.selectedEntities().size(), 1u);

    command = service.activateViewportTool(ModelingViewportTool::Cut);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Object);
    EXPECT_EQ(service.selectionSummary().selectedObjectCount, 1u);
    EXPECT_EQ(service.toolStatus().activeTool, ModelingViewportTool::Cut);
    EXPECT_TRUE(service.toolStatus().cutActive);
}

TEST(ModelingSessionServiceTests, MirrorToolDrawnPlaneFinalizeDuplicatesSelection) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
        .name = "Mirror Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(scene.entities().size(), 1u);

    ModelingCommandResult command = service.activateViewportTool(ModelingViewportTool::Mirror);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().mirrorActive);

    command = service.clickMirror(testDrawnPlaneEvent(0.0f));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().mirrorDragging);
    EXPECT_TRUE(service.toolStatus().mirrorDraggingNewPoint);
    EXPECT_EQ(service.toolStatus().mirrorPointCount, 2);
    EXPECT_EQ(service.toolStatus().mirrorCapturedPointCount, 1);
    EXPECT_TRUE(service.toolStatus().mirrorHasLine);
    EXPECT_FALSE(service.toolStatus().mirrorCanPreview);

    command = service.updateMirrorHover(testDrawnPlaneEvent(1.0f, false));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().mirrorDragging);
    EXPECT_TRUE(service.toolStatus().mirrorDraggingNewPoint);
    EXPECT_EQ(service.toolStatus().mirrorCapturedPointCount, 1);
    EXPECT_EQ(service.toolStatus().mirrorPointCount, 2);
    EXPECT_TRUE(service.toolStatus().mirrorHasLine);

    command = service.updateMirrorHover(testDrawnPlaneEvent(1.0f, true));
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(service.toolStatus().mirrorDragging);
    EXPECT_TRUE(service.toolStatus().mirrorDraggingNewPoint);
    EXPECT_EQ(service.toolStatus().mirrorCapturedPointCount, 2);
    EXPECT_EQ(service.toolStatus().mirrorPointCount, 2);
    EXPECT_TRUE(service.toolStatus().mirrorHasLine);
    command = service.endMirrorDrag();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_FALSE(service.toolStatus().mirrorDragging);
    EXPECT_EQ(service.toolStatus().mirrorCapturedPointCount, 2);
    EXPECT_EQ(service.toolStatus().mirrorPointCount, 2);
    EXPECT_TRUE(service.toolStatus().mirrorCanPreview);
    EXPECT_TRUE(service.toolStatus().mirrorPreviewValid);

    command = service.finalizeMirrorTool();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(service.toolStatus().activeTool, ModelingViewportTool::Select);
    EXPECT_FALSE(service.toolStatus().mirrorActive);
    EXPECT_EQ(scene.entities().size(), 2u);
}

TEST(ModelingSessionServiceTests, MirrorToolActivationPromotesSelectedSceneMeshToObjectMode) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
        .name = "Mirror Activation Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(scene.selectedEntities().size(), 1u);

    ModelingCommandResult command = service.setSelectionMode(ModelingSelectionMode::Face);
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Face);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);

    command = service.activateViewportTool(ModelingViewportTool::Mirror);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Object);
    EXPECT_EQ(service.selectionSummary().selectedObjectCount, 1u);
    EXPECT_EQ(service.toolStatus().activeTool, ModelingViewportTool::Mirror);
    EXPECT_TRUE(service.toolStatus().mirrorActive);
}

TEST(ModelingSessionServiceTests, EditorSelectionProjectsSceneSelectionToRuntimeSelectionCenter) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult first = service.createBoxFromBounds({
        .bounds = {
            .min = {-2.0f, -1.0f, -3.0f},
            .max = {2.0f, 1.0f, 3.0f},
        },
        .name = "First Box",
    });
    ASSERT_TRUE(first.ok) << first.message;

    const ModelingCreateBoxResult second = service.createBoxFromBounds({
        .bounds = {
            .min = {9.0f, 2.0f, -4.0f},
            .max = {13.0f, 6.0f, 2.0f},
        },
        .name = "Second Box",
    });
    ASSERT_TRUE(second.ok) << second.message;

    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {first.object.entity}).ok);
    std::optional<QVec3> center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_FLOAT_EQ(center->x, 0.0f);
    EXPECT_FLOAT_EQ(center->y, 0.0f);
    EXPECT_FLOAT_EQ(center->z, 0.0f);
    const std::uint64_t firstSelectionRevision = service.selectionSummary().selectionRevision;
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {first.object.entity}).ok);
    EXPECT_EQ(service.selectionSummary().selectionRevision, firstSelectionRevision);

    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {second.object.entity}).ok);
    center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_FLOAT_EQ(center->x, 11.0f);
    EXPECT_FLOAT_EQ(center->y, 4.0f);
    EXPECT_FLOAT_EQ(center->z, -1.0f);
    const std::uint64_t secondSelectionRevision = service.selectionSummary().selectionRevision;
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {second.object.entity}).ok);
    EXPECT_EQ(service.selectionSummary().selectionRevision, secondSelectionRevision);
}

TEST(ModelingSessionServiceTests, SelectionCommandsStaySynchronizedWithScene) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult first = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "First Box",
    });
    ASSERT_TRUE(first.ok) << first.message;
    const ModelingCreateBoxResult second = service.createBoxFromBounds({
        .bounds = {.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
        .name = "Second Box",
    });
    ASSERT_TRUE(second.ok) << second.message;

    ASSERT_TRUE(service.clearSelection().ok);
    EXPECT_TRUE(scene.selectedEntities().empty());

    const ModelingCommandResult selectAll = service.selectAll();
    ASSERT_TRUE(selectAll.ok) << selectAll.message;
    EXPECT_TRUE(selectAll.changed);
    EXPECT_EQ(service.selectionSummary().selectedObjectCount, 2u);
    EXPECT_EQ(scene.selectedEntities().size(), 2u);

    const ModelingCommandResult invertAll = service.invertSelection();
    ASSERT_TRUE(invertAll.ok) << invertAll.message;
    EXPECT_TRUE(scene.selectedEntities().empty());

    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {first.object.entity}).ok);
    const ModelingCommandResult invertOne = service.invertSelection();
    ASSERT_TRUE(invertOne.ok) << invertOne.message;
    EXPECT_EQ(service.selectionSummary().selectedObjectCount, 1u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), second.object.entity);
}

TEST(ModelingSessionServiceTests, SceneSelectionEditOverloadsShareActiveFallbackPolicy) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult first = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "First Selection Edit Box",
    });
    ASSERT_TRUE(first.ok) << first.message;
    const ModelingCreateBoxResult second = service.createBoxFromBounds({
        .bounds = {.min = {3.0f, -1.0f, -1.0f}, .max = {5.0f, 1.0f, 1.0f}},
        .name = "Second Selection Edit Box",
    });
    ASSERT_TRUE(second.ok) << second.message;

    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {first.object.entity, second.object.entity}).ok);
    ASSERT_TRUE(editorSelection.selection.editSceneSelection(first.object.entity, EditorSelectionEdit::Remove).ok);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), second.object.entity);
    EXPECT_EQ(scene.primarySelection(), second.object.entity);

    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {first.object.entity, second.object.entity}).ok);
    const std::array<EntityId, 1> removeFirst{{first.object.entity}};
    ASSERT_TRUE(editorSelection.selection.editSceneSelection(removeFirst, EditorSelectionEdit::Remove).ok);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), second.object.entity);
    EXPECT_EQ(scene.primarySelection(), second.object.entity);

    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {first.object.entity, second.object.entity}).ok);
    ASSERT_TRUE(editorSelection.selection.editSceneSelection(second.object.entity, EditorSelectionEdit::Toggle).ok);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), first.object.entity);
    EXPECT_EQ(scene.primarySelection(), first.object.entity);

    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {first.object.entity, second.object.entity}).ok);
    const std::array<EntityId, 1> toggleSecond{{second.object.entity}};
    ASSERT_TRUE(editorSelection.selection.editSceneSelection(toggleSecond, EditorSelectionEdit::Toggle).ok);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), first.object.entity);
    EXPECT_EQ(scene.primarySelection(), first.object.entity);
}

TEST(ModelingSessionServiceTests, ElementSelectionCommandsWorkAcrossDifferentMeshes) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult first = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "First Component Box",
    });
    ASSERT_TRUE(first.ok) << first.message;
    const ModelingCreateBoxResult second = service.createBoxFromBounds({
        .bounds = {.min = {4.0f, -1.0f, -1.0f}, .max = {6.0f, 1.0f, 1.0f}},
        .name = "Second Component Box",
    });
    ASSERT_TRUE(second.ok) << second.message;

    ASSERT_TRUE(service.clearSelection().ok);
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    ModelingSelectionSummary summary = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
    });
    ASSERT_EQ(summary.mode, ModelingSelectionMode::Face);
    ASSERT_EQ(summary.selectedFaceCount, 1u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), first.object.entity);

    summary = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {5.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
            .shiftPressed = true,
    });
    EXPECT_EQ(summary.selectedFaceCount, 2u);
    EXPECT_EQ(scene.selectedEntities().size(), 2u);

    const std::optional<QVec3> center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_NEAR(center->x, 2.5f, 0.001f);
    EXPECT_NEAR(center->z, 1.0f, 0.001f);

    ASSERT_TRUE(service.clearSelection().ok);
    ASSERT_TRUE(service.selectAll().ok);
    summary = service.selectionSummary();
    EXPECT_EQ(summary.mode, ModelingSelectionMode::Face);
    EXPECT_EQ(summary.selectedFaceCount, 12u);
    EXPECT_EQ(scene.selectedEntities().size(), 2u);

    ASSERT_TRUE(service.invertSelection().ok);
    summary = service.selectionSummary();
    EXPECT_EQ(summary.selectedFaceCount, 0u);
    EXPECT_TRUE(scene.selectedEntities().empty());
}

TEST(ModelingSessionServiceTests, ComponentSelectionRejectsInvalidPolygonIdsWithoutMutation) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Validated Component Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    ModelingCommandResult command = service.setComponentSelection({
            .modelingObjectId = box.object.modelingObjectId,
            .mode = ModelingSelectionMode::Face,
            .faces = {1},
            .activeKind = ModelingActiveSelectionKind::Face,
            .activeFace = 1,
    });
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(command.changed);
    ModelingSelectionSummary summary = service.selectionSummary();
    EXPECT_EQ(summary.mode, ModelingSelectionMode::Face);
    EXPECT_EQ(summary.activeKind, ModelingActiveSelectionKind::Face);
    EXPECT_EQ(summary.activeFaceId, 1);
    EXPECT_EQ(summary.selectedFaceCount, 1u);

    const std::uint64_t selectionRevision = summary.selectionRevision;
    command = service.setComponentSelection({
            .modelingObjectId = box.object.modelingObjectId,
            .mode = ModelingSelectionMode::Face,
            .faces = {999999},
    });
    EXPECT_FALSE(command.ok);
    summary = service.selectionSummary();
    EXPECT_EQ(summary.selectionRevision, selectionRevision);
    EXPECT_EQ(summary.selectedFaceCount, 1u);
    EXPECT_EQ(summary.activeFaceId, 1);
}

TEST(ModelingSessionServiceTests, SelectionCenterAndOverlayRespectSceneTransforms) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Moved Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    TransformComponent moved;
    moved.position = {7.0f, 2.0f, -3.0f};
    scene.setTransform(box.object.entity, moved);
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {box.object.entity}).ok);

    const std::optional<QVec3> center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_NEAR(center->x, 7.0f, 0.001f);
    EXPECT_NEAR(center->y, 2.0f, 0.001f);
    EXPECT_NEAR(center->z, -3.0f, 0.001f);

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    ASSERT_FALSE(frame.lines.empty());
    std::optional<SceneBounds> overlayBounds;
    for (const ViewportOverlayLine& line : frame.lines) {
        if (!overlayBounds) {
            overlayBounds = SceneBounds{.min = line.start, .max = line.start};
        } else {
            includePoint(*overlayBounds, line.start);
        }
        includePoint(*overlayBounds, line.end);
    }
    ASSERT_TRUE(overlayBounds.has_value());
    const QVec3 overlayCenter = boundsCenter(*overlayBounds);
    EXPECT_NEAR(overlayCenter.x, 7.0f, 0.001f);
    EXPECT_NEAR(overlayCenter.y, 2.0f, 0.001f);
    EXPECT_NEAR(overlayCenter.z, -3.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, PickAndHoverRespectSceneTransformsForLocalMeshes) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult left = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Left Local Box",
    });
    ASSERT_TRUE(left.ok) << left.message;
    const ModelingCreateBoxResult right = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Right Local Box",
    });
    ASSERT_TRUE(right.ok) << right.message;

    TransformComponent leftTransform;
    leftTransform.position = {-4.0f, 0.0f, 0.0f};
    scene.setTransform(left.object.entity, leftTransform);
    TransformComponent rightTransform;
    rightTransform.position = {4.0f, 0.0f, 0.0f};
    scene.setTransform(right.object.entity, rightTransform);

    ASSERT_TRUE(service.clearSelection().ok);
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Object).ok);

    const ModelingCommandResult hover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = {
                    .origin = {4.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
    });
    ASSERT_TRUE(hover.ok) << hover.message;
    const ViewportOverlayFrame hoverFrame = service.selectionOverlayFrame();
    const bool hoverHasSourceWire = std::any_of(
            hoverFrame.lines.begin(),
            hoverFrame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::SourceWire;
            });
    EXPECT_FALSE(hoverHasSourceWire);

    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {
                    .origin = {4.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
            .clickCount = 1,
    });
    ASSERT_EQ(picked.selectedObjectCount, 1u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), right.object.entity);
    EXPECT_EQ(picked.activeObjectId, right.object.modelingObjectId);

    const ViewportOverlayFrame selectedFrame = service.selectionOverlayFrame();
    std::optional<SceneBounds> selectedWireBounds;
    for (const ViewportOverlayLine& line : selectedFrame.lines) {
        if (line.layer != ViewportOverlayLayer::SelectedEdge) {
            continue;
        }
        if (!selectedWireBounds) {
            selectedWireBounds = SceneBounds{.min = line.start, .max = line.start};
        } else {
            includePoint(*selectedWireBounds, line.start);
        }
        includePoint(*selectedWireBounds, line.end);
    }
    ASSERT_TRUE(selectedWireBounds.has_value());
    EXPECT_NEAR(boundsCenter(*selectedWireBounds).x, 4.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, VertexHoverCandidatePointsRespectSceneTransforms) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Hover Candidate Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    TransformComponent transform;
    transform.position = {4.0f, 0.0f, 0.0f};
    scene.setTransform(box.object.entity, transform);

    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    ASSERT_TRUE(service.clearSelection().ok);

    const ModelingCommandResult hover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = {
                    .origin = {4.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
    });
    ASSERT_TRUE(hover.ok) << hover.message;

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    std::optional<SceneBounds> pointBounds;
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer != ViewportOverlayLayer::UnselectedVertex) {
            continue;
        }
        if (!pointBounds) {
            pointBounds = SceneBounds{.min = point.position, .max = point.position};
        } else {
            includePoint(*pointBounds, point.position);
        }
    }

    ASSERT_TRUE(pointBounds.has_value());
    EXPECT_NEAR(boundsCenter(*pointBounds).x, 4.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, SelectedHoverAndModifierRemovePreviewStayDistinct) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Selected Hover Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingRay frontFaceRay{
            .origin = {0.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = frontFaceRay,
            .clickCount = 1,
    });
    ASSERT_EQ(picked.selectedFaceCount, 1u);
    const ModelingSelectionSummary selectedBeforeHover = service.selectionSummary();
    const std::uint64_t sceneRevisionBeforeHover = scene.revision();

    const ModelingCommandResult hover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontFaceRay,
            .selectedModifierPressed = false,
    });
    ASSERT_TRUE(hover.ok) << hover.message;

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    const auto hoverFaceCount = std::count_if(frame.triangles.begin(), frame.triangles.end(), [](const ViewportOverlayTriangle& triangle) {
        return triangle.layer == ViewportOverlayLayer::HoverFace;
    });
    const auto hoverFaceEdgeCount = std::count_if(frame.lines.begin(), frame.lines.end(), [](const ViewportOverlayLine& line) {
        return line.layer == ViewportOverlayLayer::HoverFaceEdge;
    });
    const auto selectedFaceEdgeCount = std::count_if(frame.lines.begin(), frame.lines.end(), [](const ViewportOverlayLine& line) {
        return line.layer == ViewportOverlayLayer::SelectedFaceEdge;
    });
    const bool normalHoverUsesHoverFaceColor = std::any_of(
            frame.triangles.begin(),
            frame.triangles.end(),
            [](const ViewportOverlayTriangle& triangle) {
                return triangle.layer == ViewportOverlayLayer::HoverFace &&
                        triangle.color == kOverlayHoverFaceColor;
            });
    const bool normalHoverUsesHoverWireColor = std::any_of(
            frame.lines.begin(),
            frame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::HoverFaceEdge &&
                        line.color == kOverlayHoverWireColor;
            });

    EXPECT_FALSE(frame.hoverTargetSelected);
    EXPECT_GT(hoverFaceCount, 0);
    EXPECT_GT(hoverFaceEdgeCount, 0);
    EXPECT_GT(selectedFaceEdgeCount, 0);
    EXPECT_TRUE(normalHoverUsesHoverFaceColor);
    EXPECT_TRUE(normalHoverUsesHoverWireColor);
    EXPECT_EQ(scene.revision(), sceneRevisionBeforeHover);
    EXPECT_EQ(service.selectionSummary().selectedFaceCount, selectedBeforeHover.selectedFaceCount);
    EXPECT_EQ(service.selectionSummary().contentRevision, selectedBeforeHover.contentRevision);
    EXPECT_EQ(service.selectionSummary().selectionRevision, selectedBeforeHover.selectionRevision);

    const ModelingCommandResult modifierHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontFaceRay,
            .selectedModifierPressed = true,
    });
    ASSERT_TRUE(modifierHover.ok) << modifierHover.message;

    const ViewportOverlayFrame modifierFrame = service.selectionOverlayFrame();
    EXPECT_TRUE(modifierFrame.hoverTargetSelected);
    const auto modifierHoverFaceCount = std::count_if(
            modifierFrame.triangles.begin(),
            modifierFrame.triangles.end(),
            [](const ViewportOverlayTriangle& triangle) {
                return triangle.layer == ViewportOverlayLayer::HoverFace;
            });
    const auto modifierHoverFaceEdgeCount = std::count_if(
            modifierFrame.lines.begin(),
            modifierFrame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::HoverFaceEdge;
            });
    const bool modifierUsesRemoveFaceColor = std::any_of(
            modifierFrame.triangles.begin(),
            modifierFrame.triangles.end(),
            [](const ViewportOverlayTriangle& triangle) {
                return triangle.layer == ViewportOverlayLayer::HoverFace &&
                        triangle.color == kOverlayRemoveFaceColor;
            });
    const bool modifierUsesRemoveWireColor = std::any_of(
            modifierFrame.lines.begin(),
            modifierFrame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::HoverFaceEdge &&
                        line.color == kOverlayRemoveWireColor;
            });

    EXPECT_GT(modifierHoverFaceCount, 0);
    EXPECT_GT(modifierHoverFaceEdgeCount, 0);
    EXPECT_TRUE(modifierUsesRemoveFaceColor);
    EXPECT_TRUE(modifierUsesRemoveWireColor);
    EXPECT_EQ(scene.revision(), sceneRevisionBeforeHover);
    EXPECT_EQ(service.selectionSummary().selectedFaceCount, selectedBeforeHover.selectedFaceCount);
    EXPECT_EQ(service.selectionSummary().contentRevision, selectedBeforeHover.contentRevision);
    EXPECT_EQ(service.selectionSummary().selectionRevision, selectedBeforeHover.selectionRevision);
}

TEST(ModelingSessionServiceTests, ClearingHoverAfterSelectionLetsSelectionColorWinAndCtrlShowsRemovePreview) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Hover Preserved Selection Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingRay frontFaceRay{
            .origin = {0.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const ModelingCommandResult initialHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontFaceRay,
            .selectedModifierPressed = false,
    });
    ASSERT_TRUE(initialHover.ok) << initialHover.message;
    ASSERT_TRUE(initialHover.changed);

    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = frontFaceRay,
            .clickCount = 1,
    });
    ASSERT_EQ(picked.selectedFaceCount, 1u);
    const ModelingHoverSummary pickedHover = service.previewSelectionHover(ModelingHoverRequest{
            .ray = frontFaceRay,
            .selectedModifierPressed = false,
    });
    EXPECT_EQ(pickedHover.target.kind, ModelingActiveSelectionKind::Face);
    EXPECT_TRUE(pickedHover.targetSelected);

    const ModelingCommandResult clearAfterPick = service.clearSelectionHover();
    ASSERT_TRUE(clearAfterPick.ok) << clearAfterPick.message;

    const ViewportOverlayFrame afterClickFrame = service.selectionOverlayFrame();
    const bool hoverFaceStillVisible = std::any_of(
            afterClickFrame.triangles.begin(),
            afterClickFrame.triangles.end(),
            [](const ViewportOverlayTriangle& triangle) {
                return triangle.layer == ViewportOverlayLayer::HoverFace &&
                        triangle.color == kOverlayHoverFaceColor;
            });
    const bool hoverFaceEdgeStillVisible = std::any_of(
            afterClickFrame.lines.begin(),
            afterClickFrame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::HoverFaceEdge &&
                        line.color == kOverlayHoverWireColor;
            });
    const bool selectedFaceWireVisibleUnderHover = std::any_of(
            afterClickFrame.lines.begin(),
            afterClickFrame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::SelectedFaceEdge;
            });
    EXPECT_FALSE(afterClickFrame.hoverTargetSelected);
    EXPECT_FALSE(hoverFaceStillVisible);
    EXPECT_FALSE(hoverFaceEdgeStillVisible);
    EXPECT_TRUE(selectedFaceWireVisibleUnderHover);

    const ModelingCommandResult modifierHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontFaceRay,
            .selectedModifierPressed = true,
    });
    ASSERT_TRUE(modifierHover.ok) << modifierHover.message;

    const ViewportOverlayFrame modifierHoverFrame = service.selectionOverlayFrame();
    const bool removeFaceVisible = std::any_of(
            modifierHoverFrame.triangles.begin(),
            modifierHoverFrame.triangles.end(),
            [](const ViewportOverlayTriangle& triangle) {
                return triangle.layer == ViewportOverlayLayer::HoverFace &&
                        triangle.color == kOverlayRemoveFaceColor;
            });
    const bool removeFaceEdgeVisible = std::any_of(
            modifierHoverFrame.lines.begin(),
            modifierHoverFrame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::HoverFaceEdge &&
                        line.color == kOverlayRemoveWireColor;
            });
    EXPECT_TRUE(modifierHoverFrame.hoverTargetSelected);
    EXPECT_TRUE(removeFaceVisible);
    EXPECT_TRUE(removeFaceEdgeVisible);

    const ModelingCommandResult clearHover = service.clearSelectionHover();
    ASSERT_TRUE(clearHover.ok) << clearHover.message;

    const ViewportOverlayFrame afterClearFrame = service.selectionOverlayFrame();
    const bool hoverFaceCleared = std::none_of(
            afterClearFrame.triangles.begin(),
            afterClearFrame.triangles.end(),
            [](const ViewportOverlayTriangle& triangle) {
                return triangle.layer == ViewportOverlayLayer::HoverFace;
            });
    const bool selectedFaceWireAfterClear = std::any_of(
            afterClearFrame.lines.begin(),
            afterClearFrame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::SelectedFaceEdge;
            });
    EXPECT_TRUE(hoverFaceCleared);
    EXPECT_TRUE(selectedFaceWireAfterClear);
}

TEST(ModelingSessionServiceTests, ClearingHoverAfterEdgeSelectionShowsSelectionBeforeHoverReturns) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Hover Suppressed Edge Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Edge).ok);

    const ModelingRay frontRightEdgeRay{
            .origin = {1.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const ModelingCommandResult initialHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontRightEdgeRay,
            .selectedModifierPressed = false,
            .edgeRadius = 0.4f,
    });
    ASSERT_TRUE(initialHover.ok) << initialHover.message;
    ASSERT_TRUE(initialHover.changed);

    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = frontRightEdgeRay,
            .clickCount = 1,
            .edgeRadius = 0.4f,
    });
    ASSERT_GT(picked.selectedEdgeCount, 0u);
    const ModelingCommandResult clearAfterPick = service.clearSelectionHover();
    ASSERT_TRUE(clearAfterPick.ok) << clearAfterPick.message;

    const auto hasHoverEdge = [](const ViewportOverlayFrame& frame) {
        return std::any_of(
                frame.lines.begin(),
                frame.lines.end(),
                [](const ViewportOverlayLine& line) {
                    return line.layer == ViewportOverlayLayer::HoverEdge &&
                            line.color == kOverlayHoverWireColor;
                });
    };
    const auto hasSelectedEdge = [](const ViewportOverlayFrame& frame) {
        return std::any_of(
                frame.lines.begin(),
                frame.lines.end(),
                [](const ViewportOverlayLine& line) {
                    return line.layer == ViewportOverlayLayer::SelectedEdge;
                });
    };

    const ViewportOverlayFrame afterClickFrame = service.selectionOverlayFrame();
    EXPECT_FALSE(hasHoverEdge(afterClickFrame));
    EXPECT_TRUE(hasSelectedEdge(afterClickFrame));

    const ModelingCommandResult sameTargetHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontRightEdgeRay,
            .selectedModifierPressed = false,
            .edgeRadius = 0.4f,
    });
    ASSERT_TRUE(sameTargetHover.ok) << sameTargetHover.message;
    const ViewportOverlayFrame sameTargetFrame = service.selectionOverlayFrame();
    EXPECT_TRUE(hasHoverEdge(sameTargetFrame));
    EXPECT_TRUE(hasSelectedEdge(sameTargetFrame));

    const ModelingCommandResult clearHover = service.clearSelectionHover();
    ASSERT_TRUE(clearHover.ok) << clearHover.message;
    ASSERT_TRUE(clearHover.changed);
    const ModelingCommandResult rehover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontRightEdgeRay,
            .selectedModifierPressed = false,
            .edgeRadius = 0.4f,
    });
    ASSERT_TRUE(rehover.ok) << rehover.message;
    const ViewportOverlayFrame rehoverFrame = service.selectionOverlayFrame();
    EXPECT_TRUE(hasHoverEdge(rehoverFrame));
    EXPECT_TRUE(hasSelectedEdge(rehoverFrame));
}

TEST(ModelingSessionServiceTests, ClearingHoverAfterVertexSelectionShowsSelectionBeforeHoverReturns) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Hover Suppressed Vertex Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);

    const ModelingRay frontCornerRay{
            .origin = {1.0f, 1.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const ModelingCommandResult initialHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontCornerRay,
            .selectedModifierPressed = false,
            .vertexRadius = 0.4f,
    });
    ASSERT_TRUE(initialHover.ok) << initialHover.message;
    ASSERT_TRUE(initialHover.changed);

    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = frontCornerRay,
            .clickCount = 1,
            .vertexRadius = 0.4f,
    });
    ASSERT_GT(picked.selectedVertexCount, 0u);
    const ModelingCommandResult clearAfterPick = service.clearSelectionHover();
    ASSERT_TRUE(clearAfterPick.ok) << clearAfterPick.message;

    const auto hasHoverVertex = [](const ViewportOverlayFrame& frame) {
        return std::any_of(
                frame.points.begin(),
                frame.points.end(),
                [](const ViewportOverlayPoint& point) {
                    return point.layer == ViewportOverlayLayer::HoverVertex &&
                            point.color == kOverlayHoverVertexColor;
                });
    };
    const auto hasSelectedVertex = [](const ViewportOverlayFrame& frame) {
        return std::any_of(
                frame.points.begin(),
                frame.points.end(),
                [](const ViewportOverlayPoint& point) {
                    return point.layer == ViewportOverlayLayer::SelectedVertex;
                });
    };

    const ViewportOverlayFrame afterClickFrame = service.selectionOverlayFrame();
    EXPECT_FALSE(hasHoverVertex(afterClickFrame));
    EXPECT_TRUE(hasSelectedVertex(afterClickFrame));

    const ModelingCommandResult sameTargetHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontCornerRay,
            .selectedModifierPressed = false,
            .vertexRadius = 0.4f,
    });
    ASSERT_TRUE(sameTargetHover.ok) << sameTargetHover.message;
    const ViewportOverlayFrame sameTargetFrame = service.selectionOverlayFrame();
    EXPECT_TRUE(hasHoverVertex(sameTargetFrame));
    EXPECT_TRUE(hasSelectedVertex(sameTargetFrame));

    const ModelingCommandResult clearHover = service.clearSelectionHover();
    ASSERT_TRUE(clearHover.ok) << clearHover.message;
    ASSERT_TRUE(clearHover.changed);
    const ModelingCommandResult rehover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontCornerRay,
            .selectedModifierPressed = false,
            .vertexRadius = 0.4f,
    });
    ASSERT_TRUE(rehover.ok) << rehover.message;
    const ViewportOverlayFrame rehoverFrame = service.selectionOverlayFrame();
    EXPECT_TRUE(hasHoverVertex(rehoverFrame));
    EXPECT_TRUE(hasSelectedVertex(rehoverFrame));
}

TEST(ModelingSessionServiceTests, SelectedEdgeHoverAndModifierRemovePreviewStayDistinct) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Selected Edge Remove Hover Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Edge).ok);

    const ModelingRay frontRightEdgeRay{
            .origin = {1.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = frontRightEdgeRay,
            .clickCount = 1,
            .edgeRadius = 0.4f,
    });
    ASSERT_GT(picked.selectedEdgeCount, 0u);
    const ModelingSelectionSummary selectedBeforeHover = service.selectionSummary();

    const ModelingCommandResult hover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontRightEdgeRay,
            .selectedModifierPressed = false,
            .edgeRadius = 0.4f,
    });
    ASSERT_TRUE(hover.ok) << hover.message;

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    EXPECT_FALSE(frame.hoverTargetSelected);
    const bool hasSelectedEdge = std::any_of(
            frame.lines.begin(),
            frame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::SelectedEdge ||
                        line.layer == ViewportOverlayLayer::SelectedFaceEdge;
            });
    const bool hasNormalHoverEdge = std::any_of(
            frame.lines.begin(),
            frame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::HoverEdge &&
                        line.color == kOverlayHoverWireColor;
            });
    EXPECT_TRUE(hasSelectedEdge);
    EXPECT_TRUE(hasNormalHoverEdge);
    EXPECT_EQ(service.selectionSummary().selectedEdgeCount, selectedBeforeHover.selectedEdgeCount);
    EXPECT_EQ(service.selectionSummary().selectionRevision, selectedBeforeHover.selectionRevision);

    const ModelingCommandResult modifierHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontRightEdgeRay,
            .selectedModifierPressed = true,
            .edgeRadius = 0.4f,
    });
    ASSERT_TRUE(modifierHover.ok) << modifierHover.message;

    const ViewportOverlayFrame modifierFrame = service.selectionOverlayFrame();
    EXPECT_TRUE(modifierFrame.hoverTargetSelected);
    const bool hasRemoveHoverEdge = std::any_of(
            modifierFrame.lines.begin(),
            modifierFrame.lines.end(),
            [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::HoverEdge &&
                        line.color == kOverlayRemoveWireColor;
            });
    EXPECT_TRUE(hasRemoveHoverEdge);
    EXPECT_EQ(service.selectionSummary().selectedEdgeCount, selectedBeforeHover.selectedEdgeCount);
    EXPECT_EQ(service.selectionSummary().selectionRevision, selectedBeforeHover.selectionRevision);
}

TEST(ModelingSessionServiceTests, SelectedVertexHoverAndModifierRemovePreviewStayDistinct) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Selected Vertex Remove Hover Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);

    const ModelingCommandResult selectAll = service.selectAll();
    ASSERT_TRUE(selectAll.ok) << selectAll.message;
    ASSERT_GT(service.selectionSummary().selectedVertexCount, 0u);
    const ModelingSelectionSummary selectedBeforeHover = service.selectionSummary();

    const ModelingRay frontCornerRay{
            .origin = {1.0f, 1.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const ModelingCommandResult normalHover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontCornerRay,
            .selectedModifierPressed = false,
            .vertexRadius = 0.4f,
    });
    ASSERT_TRUE(normalHover.ok) << normalHover.message;

    const ViewportOverlayFrame normalFrame = service.selectionOverlayFrame();
    EXPECT_FALSE(normalFrame.hoverTargetSelected);
    const bool hasSelectedHandle = std::any_of(
            normalFrame.points.begin(),
            normalFrame.points.end(),
            [](const ViewportOverlayPoint& point) {
                return point.layer == ViewportOverlayLayer::SelectedVertex;
            });
    const bool hasNormalHoverHandle = std::any_of(
            normalFrame.points.begin(),
            normalFrame.points.end(),
            [](const ViewportOverlayPoint& point) {
                return point.layer == ViewportOverlayLayer::HoverVertex &&
                        point.color == kOverlayHoverVertexColor;
            });
    EXPECT_TRUE(hasSelectedHandle);
    EXPECT_TRUE(hasNormalHoverHandle);
    EXPECT_EQ(service.selectionSummary().selectedVertexCount, selectedBeforeHover.selectedVertexCount);
    EXPECT_EQ(service.selectionSummary().selectionRevision, selectedBeforeHover.selectionRevision);

    const ModelingCommandResult hover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = frontCornerRay,
            .selectedModifierPressed = true,
            .vertexRadius = 0.4f,
    });
    ASSERT_TRUE(hover.ok) << hover.message;

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    EXPECT_TRUE(frame.hoverTargetSelected);
    const bool hasRemoveHoverHandle = std::any_of(
            frame.points.begin(),
            frame.points.end(),
            [](const ViewportOverlayPoint& point) {
                return point.layer == ViewportOverlayLayer::HoverVertex &&
                        point.color == kOverlayRemoveWireColor;
            });
    EXPECT_TRUE(hasRemoveHoverHandle);
    EXPECT_EQ(service.selectionSummary().selectedVertexCount, selectedBeforeHover.selectedVertexCount);
    EXPECT_EQ(service.selectionSummary().selectionRevision, selectedBeforeHover.selectionRevision);
}

TEST(ModelingSessionServiceTests, InstantOperationsRefreshSceneObjectsAndSelection) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Source Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    const AssetId defaultMaterial = assets.addMaterial(makeDefaultQuaderMaterial());
    scene.setMaterial(box.object.entity, MaterialComponent{.material = defaultMaterial});
    TransformComponent sourceTransform;
    sourceTransform.position = {2.0f, 0.0f, -3.0f};
    sourceTransform.rotationEuler = {0.25f, 0.75f, 0.0f};
    scene.setTransform(box.object.entity, sourceTransform);
    const MeshComponent* sourceMeshComponent = scene.mesh(box.object.entity);
    ASSERT_NE(sourceMeshComponent, nullptr);
    MeshComponent sourceMeshSettings = *sourceMeshComponent;
    sourceMeshSettings.doubleSided = true;
    scene.setMesh(box.object.entity, sourceMeshSettings);
    const MeshAsset* sourceMeshAsset = assets.mesh(sourceMeshSettings.mesh);
    ASSERT_NE(sourceMeshAsset, nullptr);
    EXPECT_EQ(scene.entities().size(), 1u);

    const std::vector<ModelingInstantOperationDescriptor> objectOps = service.instantOperations();
    const auto duplicate = std::find_if(objectOps.begin(), objectOps.end(), [](const ModelingInstantOperationDescriptor& operation) {
        return operation.operation == ModelingInstantOperationId::DuplicateMeshes;
    });
    ASSERT_NE(duplicate, objectOps.end());
    EXPECT_TRUE(duplicate->enabled);

    const ModelingCommandResult duplicated = service.executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes);
    ASSERT_TRUE(duplicated.ok) << duplicated.message;
    EXPECT_TRUE(duplicated.changed);
    EXPECT_EQ(scene.entities().size(), 2u);
    EXPECT_EQ(service.objects().size(), 2u);
    EXPECT_EQ(service.selectionSummary().selectedObjectCount, 1u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    const EntityId duplicateEntity = scene.selectedEntities().front();
    EXPECT_NE(duplicateEntity, box.object.entity);
    ASSERT_NE(scene.name(duplicateEntity), nullptr);
    EXPECT_EQ(scene.name(duplicateEntity)->name, "Mesh");
    EXPECT_EQ(scene.name(duplicateEntity)->name.find("Runtime Object"), std::string::npos);
    const MeshComponent* duplicateMesh = scene.mesh(duplicateEntity);
    ASSERT_NE(duplicateMesh, nullptr);
    EXPECT_TRUE(duplicateMesh->doubleSided);
    const MeshAsset* duplicateMeshAsset = assets.mesh(duplicateMesh->mesh);
    ASSERT_NE(duplicateMeshAsset, nullptr);
    EXPECT_EQ(duplicateMeshAsset->name, "Mesh");
    ASSERT_NE(scene.transform(duplicateEntity), nullptr);
    expectVec3Near(scene.transform(duplicateEntity)->position, sourceTransform.position);
    expectVec3Near(scene.transform(duplicateEntity)->rotationEuler, sourceTransform.rotationEuler);
    ASSERT_EQ(duplicateMeshAsset->vertices.size(), sourceMeshAsset->vertices.size());
    for (std::size_t index = 0; index < sourceMeshAsset->vertices.size(); ++index) {
        expectVec3Near(duplicateMeshAsset->vertices[index].position, sourceMeshAsset->vertices[index].position);
    }
    const MaterialComponent* duplicateMaterial = scene.material(duplicateEntity);
    ASSERT_NE(duplicateMaterial, nullptr);
    EXPECT_EQ(duplicateMaterial->material, defaultMaterial);

    const ModelingCommandResult legacyDelete =
            service.executeInstantOperation(ModelingInstantOperationId::DeleteSelection);
    EXPECT_FALSE(legacyDelete.ok);
    EXPECT_FALSE(legacyDelete.changed);
    EXPECT_EQ(scene.entities().size(), 2u);

    EditorHistoryService history;
    const CommandResult deleted = history.execute(service.createDeleteSelectionCommand());
    ASSERT_TRUE(deleted.ok) << deleted.message;
    EXPECT_TRUE(deleted.changed);
    EXPECT_EQ(scene.entities().size(), 1u);
    EXPECT_EQ(service.objects().size(), 1u);
    EXPECT_TRUE(scene.selectedEntities().empty());
}

TEST(ModelingSessionServiceTests, RepeatedObjectSelectionProjectionDoesNotDirtySceneOrResyncMeshes) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Selection Sync Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {box.object.entity}).ok);
    ASSERT_TRUE(editorSelection.selection.setObjectSelection(
            std::span<const EntityId>(&box.object.entity, 1),
            box.object.entity).ok);

    const std::uint64_t sceneRevision = scene.revision();
    const ModelingSelectionSummary summary = service.selectionSummary();
    ASSERT_TRUE(editorSelection.selection.setObjectSelection(
            std::span<const EntityId>(&box.object.entity, 1),
            box.object.entity).ok);

    EXPECT_EQ(scene.revision(), sceneRevision);
    EXPECT_EQ(service.selectionSummary().contentRevision, summary.contentRevision);
    EXPECT_EQ(service.selectionSummary().selectionRevision, summary.selectionRevision);
}

TEST(ModelingSessionServiceTests, TransformPreviewZeroDeltaRestoresSceneMesh) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Zero Delta Preview Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);
    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {
                    .origin = {0.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
            .clickCount = 1,
    });
    ASSERT_EQ(picked.selectedFaceCount, 1u);

    const MeshComponent* MeshComponent = scene.mesh(box.object.entity);
    ASSERT_NE(MeshComponent, nullptr);
    const MeshAsset* sourceMesh = assets.mesh(MeshComponent->mesh);
    ASSERT_NE(sourceMesh, nullptr);
    const std::size_t sourceIndexCount = sourceMesh->indices.size();

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformExtrudePreview({0.0f, 0.0f, 1.0f}, 1.0f).ok);
    const MeshAsset* extrudedMesh = assets.mesh(MeshComponent->mesh);
    ASSERT_NE(extrudedMesh, nullptr);
    ASSERT_GT(extrudedMesh->indices.size(), sourceIndexCount);

    ASSERT_TRUE(service.updateTransformExtrudePreview({0.0f, 0.0f, 0.0f}, 1.0f).ok);
    const MeshAsset* restoredMesh = assets.mesh(MeshComponent->mesh);
    ASSERT_NE(restoredMesh, nullptr);
    EXPECT_EQ(restoredMesh->indices.size(), sourceIndexCount);

    ASSERT_TRUE(service.cancelTransformPreview().ok);
}

TEST(ModelingSessionServiceTests, ComponentTransformPreviewRefreshesSelectionOverlayEveryUpdate) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Preview Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);
    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {
                    .origin = {0.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
            .clickCount = 1,
    });
    ASSERT_EQ(picked.selectedFaceCount, 1u);

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformTranslatePreview({1.0f, 0.0f, 0.0f}).ok);
    const ViewportOverlayFrame firstFrame = service.selectionOverlayFrame();

    ASSERT_TRUE(service.updateTransformTranslatePreview({3.0f, 0.0f, 0.0f}).ok);
    const ViewportOverlayFrame secondFrame = service.selectionOverlayFrame();

    auto selectedFaceEdgeBounds = [](const ViewportOverlayFrame& frame) {
        std::optional<SceneBounds> bounds;
        for (const ViewportOverlayLine& line : frame.lines) {
            if (line.layer != ViewportOverlayLayer::SelectedFaceEdge &&
                    line.layer != ViewportOverlayLayer::SelectedEdge) {
                continue;
            }
            if (!bounds) {
                bounds = SceneBounds{.min = line.start, .max = line.start};
            } else {
                includePoint(*bounds, line.start);
            }
            includePoint(*bounds, line.end);
        }
        return bounds;
    };

    const std::optional<SceneBounds> firstBounds = selectedFaceEdgeBounds(firstFrame);
    const std::optional<SceneBounds> secondBounds = selectedFaceEdgeBounds(secondFrame);
    ASSERT_TRUE(firstBounds.has_value());
    ASSERT_TRUE(secondBounds.has_value());
    EXPECT_NEAR(boundsCenter(*firstBounds).x, 1.0f, 0.001f);
    EXPECT_NEAR(boundsCenter(*secondBounds).x, 3.0f, 0.001f);

    ASSERT_TRUE(service.cancelTransformPreview().ok);
}

TEST(ModelingSessionServiceTests, ComponentTransformPreviewMovesVertexHandlesEveryUpdate) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Preview Vertex Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    const ModelingCommandResult selectAll = service.selectAll();
    ASSERT_TRUE(selectAll.ok) << selectAll.message;
    ASSERT_GT(service.selectionSummary().selectedVertexCount, 0u);

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformTranslatePreview({1.0f, 0.0f, 0.0f}).ok);
    const ViewportOverlayFrame firstFrame = service.selectionOverlayFrame();

    ASSERT_TRUE(service.updateTransformTranslatePreview({3.0f, 0.0f, 0.0f}).ok);
    const ViewportOverlayFrame secondFrame = service.selectionOverlayFrame();

    auto selectedVertexBounds = [](const ViewportOverlayFrame& frame) {
        std::optional<SceneBounds> bounds;
        for (const ViewportOverlayPoint& point : frame.points) {
            if (point.layer != ViewportOverlayLayer::SelectedVertex) {
                continue;
            }
            if (!bounds) {
                bounds = SceneBounds{.min = point.position, .max = point.position};
            } else {
                includePoint(*bounds, point.position);
            }
        }
        return bounds;
    };

    const std::optional<SceneBounds> firstBounds = selectedVertexBounds(firstFrame);
    const std::optional<SceneBounds> secondBounds = selectedVertexBounds(secondFrame);
    ASSERT_TRUE(firstBounds.has_value());
    ASSERT_TRUE(secondBounds.has_value());
    EXPECT_NEAR(boundsCenter(*firstBounds).x, 1.0f, 0.001f);
    EXPECT_NEAR(boundsCenter(*secondBounds).x, 3.0f, 0.001f);

    ASSERT_TRUE(service.cancelTransformPreview().ok);
}

TEST(ModelingSessionServiceTests, ComponentTransformPreviewMovesEdgeHandlesEveryUpdate) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Preview Edge Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Edge).ok);
    const ModelingCommandResult selectAll = service.selectAll();
    ASSERT_TRUE(selectAll.ok) << selectAll.message;
    ASSERT_GT(service.selectionSummary().selectedEdgeCount, 0u);

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformTranslatePreview({1.0f, 0.0f, 0.0f}).ok);
    const ViewportOverlayFrame firstFrame = service.selectionOverlayFrame();

    ASSERT_TRUE(service.updateTransformTranslatePreview({3.0f, 0.0f, 0.0f}).ok);
    const ViewportOverlayFrame secondFrame = service.selectionOverlayFrame();

    auto selectedEdgeBounds = [](const ViewportOverlayFrame& frame) {
        std::optional<SceneBounds> bounds;
        for (const ViewportOverlayLine& line : frame.lines) {
            if (line.layer != ViewportOverlayLayer::SelectedEdge &&
                    line.layer != ViewportOverlayLayer::SelectedFaceEdge) {
                continue;
            }
            if (!bounds) {
                bounds = SceneBounds{.min = line.start, .max = line.start};
            } else {
                includePoint(*bounds, line.start);
            }
            includePoint(*bounds, line.end);
        }
        return bounds;
    };

    const std::optional<SceneBounds> firstBounds = selectedEdgeBounds(firstFrame);
    const std::optional<SceneBounds> secondBounds = selectedEdgeBounds(secondFrame);
    ASSERT_TRUE(firstBounds.has_value());
    ASSERT_TRUE(secondBounds.has_value());
    EXPECT_NEAR(boundsCenter(*firstBounds).x, 1.0f, 0.001f);
    EXPECT_NEAR(boundsCenter(*secondBounds).x, 3.0f, 0.001f);

    ASSERT_TRUE(service.cancelTransformPreview().ok);
}

TEST(ModelingSessionServiceTests, ComponentTransformCommitKeepsHoverHandlesOnCommittedGeometry) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Commit Hover Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    ASSERT_TRUE(service.selectAll().ok);

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformTranslatePreview({2.0f, 0.0f, 0.0f}).ok);
    const ModelingCommandResult commit = service.commitTransformPreview();
    ASSERT_TRUE(commit.ok) << commit.message;

    ASSERT_TRUE(service.clearSelection().ok);
    const ModelingCommandResult hover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = {
                    .origin = {2.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
    });
    ASSERT_TRUE(hover.ok) << hover.message;

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    std::optional<SceneBounds> pointBounds;
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer != ViewportOverlayLayer::UnselectedVertex) {
            continue;
        }
        if (!pointBounds) {
            pointBounds = SceneBounds{.min = point.position, .max = point.position};
        } else {
            includePoint(*pointBounds, point.position);
        }
    }

    ASSERT_TRUE(pointBounds.has_value());
    EXPECT_NEAR(boundsCenter(*pointBounds).x, 2.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, ComponentTransformCancelRestoresMeshAndOverlayTogether) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Cancel Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    ASSERT_TRUE(service.selectAll().ok);

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformTranslatePreview({2.0f, 0.0f, 0.0f}).ok);
    std::optional<SceneBounds> selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    EXPECT_NEAR(boundsCenter(*selectedBounds).x, 2.0f, 0.001f);

    ModelingCommandResult cancel = service.cancelTransformPreview();
    ASSERT_TRUE(cancel.ok) << cancel.message;
    selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    EXPECT_NEAR(boundsCenter(*selectedBounds).x, 0.0f, 0.001f);

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    std::optional<SceneBounds> pointBounds;
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer != ViewportOverlayLayer::SelectedVertex) {
            continue;
        }
        if (!pointBounds) {
            pointBounds = SceneBounds{.min = point.position, .max = point.position};
        } else {
            includePoint(*pointBounds, point.position);
        }
    }

    ASSERT_TRUE(pointBounds.has_value());
    EXPECT_NEAR(boundsCenter(*pointBounds).x, 0.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, TransformDragComponentSelectionCommitsOverlayOnCommittedGeometry) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Drag Commit Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    ASSERT_TRUE(service.selectAll().ok);

    ModelingTransformDragPreview begin = service.beginTransformDrag(testXMoveDragBeginRequest());
    ASSERT_TRUE(begin.ok) << begin.message;
    EXPECT_TRUE(begin.active);
    EXPECT_TRUE(begin.previewDriven);

    ModelingTransformDragPreview update = service.updateTransformDrag(testXMoveDragUpdateRequest(2.0f));
    ASSERT_TRUE(update.ok) << update.message;
    EXPECT_TRUE(update.previewDriven);
    EXPECT_NEAR(update.moveDelta.x, 2.0f, 0.001f);

    ModelingTransformDragPreview commit = service.commitTransformDrag();
    ASSERT_TRUE(commit.ok) << commit.message;
    EXPECT_TRUE(commit.previewDriven);
    EXPECT_TRUE(commit.changed);

    ASSERT_TRUE(service.clearSelection().ok);
    const ModelingCommandResult hover = service.updateSelectionHover(ModelingHoverRequest{
            .ray = {
                    .origin = {2.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
    });
    ASSERT_TRUE(hover.ok) << hover.message;

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    std::optional<SceneBounds> pointBounds;
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer != ViewportOverlayLayer::UnselectedVertex) {
            continue;
        }
        if (!pointBounds) {
            pointBounds = SceneBounds{.min = point.position, .max = point.position};
        } else {
            includePoint(*pointBounds, point.position);
        }
    }

    ASSERT_TRUE(pointBounds.has_value());
    EXPECT_NEAR(boundsCenter(*pointBounds).x, 2.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, FaceTransformDragKeepsComponentSelectionAndMovesOnlyFaceGeometry) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Face Drag Regression Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {
                    .origin = {0.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
            .clickCount = 1,
    });
    ASSERT_EQ(picked.mode, ModelingSelectionMode::Face);
    ASSERT_EQ(picked.activeKind, ModelingActiveSelectionKind::Face);
    ASSERT_EQ(picked.selectedFaceCount, 1u);
    ASSERT_EQ(scene.primarySelection(), box.object.entity);

    const std::optional<MeshAsset> beforeMesh = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(beforeMesh.has_value());
    const std::optional<SceneBounds> beforeBounds = localMeshBounds(*beforeMesh);
    ASSERT_TRUE(beforeBounds.has_value());
    EXPECT_NEAR(beforeBounds->min.x, -1.0f, 0.001f);
    EXPECT_NEAR(beforeBounds->max.x, 1.0f, 0.001f);

    const std::optional<QVec3> selectionCenter = service.selectionCenter();
    ASSERT_TRUE(selectionCenter.has_value());
    ModelingTransformDragPreview begin = service.beginTransformDrag(testXMoveDragBeginRequest(*selectionCenter));
    ASSERT_TRUE(begin.ok) << begin.message;
    EXPECT_TRUE(begin.active);
    EXPECT_TRUE(begin.previewDriven);

    ModelingTransformDragPreview update = service.updateTransformDrag(testXMoveDragUpdateRequest(2.0f));
    ASSERT_TRUE(update.ok) << update.message;
    EXPECT_TRUE(update.previewDriven);
    EXPECT_NEAR(update.moveDelta.x, 2.0f, 0.001f);

    ModelingTransformDragPreview commit = service.commitTransformDrag();
    ASSERT_TRUE(commit.ok) << commit.message;
    EXPECT_TRUE(commit.previewDriven);
    EXPECT_TRUE(commit.changed);

    const ModelingSelectionSummary afterSelection = service.selectionSummary();
    EXPECT_EQ(afterSelection.mode, ModelingSelectionMode::Face);
    EXPECT_EQ(afterSelection.activeKind, ModelingActiveSelectionKind::Face);
    EXPECT_EQ(afterSelection.selectedFaceCount, 1u);
    EXPECT_EQ(scene.primarySelection(), box.object.entity);

    const std::optional<MeshAsset> afterMesh = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(afterMesh.has_value());
    const std::optional<SceneBounds> afterBounds = localMeshBounds(*afterMesh);
    ASSERT_TRUE(afterBounds.has_value());
    EXPECT_NEAR(afterBounds->min.x, -1.0f, 0.001f);
    EXPECT_NEAR(afterBounds->max.x, 3.0f, 0.001f);
    EXPECT_NEAR(afterBounds->min.y, -1.0f, 0.001f);
    EXPECT_NEAR(afterBounds->max.y, 1.0f, 0.001f);
    EXPECT_NEAR(afterBounds->min.z, -1.0f, 0.001f);
    EXPECT_NEAR(afterBounds->max.z, 1.0f, 0.001f);

    const MeshComponent* sceneMesh = scene.mesh(box.object.entity);
    ASSERT_NE(sceneMesh, nullptr);
    const MeshAsset* storedMesh = assets.mesh(sceneMesh->mesh);
    ASSERT_NE(storedMesh, nullptr);
    const std::optional<SceneBounds> storedBounds = localMeshBounds(*storedMesh);
    ASSERT_TRUE(storedBounds.has_value());
    EXPECT_NEAR(storedBounds->min.x, -1.0f, 0.001f);
    EXPECT_NEAR(storedBounds->max.x, 3.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, ComponentTransformSyncKeepsUntouchedMeshAssetRevision) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult edited = service.createBoxFromBounds({
            .bounds = {.min = {-3.0f, -1.0f, -1.0f}, .max = {-1.0f, 1.0f, 1.0f}},
            .name = "Edited Component Sync Box",
    });
    ASSERT_TRUE(edited.ok) << edited.message;
    const ModelingCreateBoxResult untouched = service.createBoxFromBounds({
            .bounds = {.min = {1.0f, -1.0f, -1.0f}, .max = {3.0f, 1.0f, 1.0f}},
            .name = "Untouched Component Sync Box",
    });
    ASSERT_TRUE(untouched.ok) << untouched.message;

    const MeshComponent* editedMesh = scene.mesh(edited.object.entity);
    ASSERT_NE(editedMesh, nullptr);
    const MeshComponent* untouchedMesh = scene.mesh(untouched.object.entity);
    ASSERT_NE(untouchedMesh, nullptr);
    const AssetId editedMeshId = editedMesh->mesh;
    const AssetId untouchedMeshId = untouchedMesh->mesh;
    const std::uint64_t editedRevisionBefore = assets.meshRevision(editedMeshId).value;
    const std::uint64_t untouchedRevisionBefore = assets.meshRevision(untouchedMeshId).value;

    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);
    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {-2.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
    });
    ASSERT_EQ(picked.activeObjectId, edited.object.modelingObjectId);
    ASSERT_EQ(picked.selectedFaceCount, 1u);

    const std::optional<QVec3> selectionCenter = service.selectionCenter();
    ASSERT_TRUE(selectionCenter.has_value());
    ModelingTransformDragPreview begin = service.beginTransformDrag(testXMoveDragBeginRequest(*selectionCenter));
    ASSERT_TRUE(begin.ok) << begin.message;
    ASSERT_TRUE(begin.previewDriven);

    ModelingTransformDragPreview update = service.updateTransformDrag(testXMoveDragUpdateRequest(2.0f));
    ASSERT_TRUE(update.ok) << update.message;
    EXPECT_TRUE(update.previewDriven);

    EXPECT_GT(assets.meshRevision(editedMeshId).value, editedRevisionBefore);
    EXPECT_EQ(assets.meshRevision(untouchedMeshId).value, untouchedRevisionBefore);

    ModelingTransformDragPreview commit = service.commitTransformDrag();
    ASSERT_TRUE(commit.ok) << commit.message;
    EXPECT_EQ(assets.meshRevision(untouchedMeshId).value, untouchedRevisionBefore);
}

TEST(ModelingSessionServiceTests, TransformDragObjectSelectionUsesSceneTransformPreview) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Object Drag Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(service.selectionMode(), ModelingSelectionMode::Object);

    ModelingTransformDragPreview begin = service.beginTransformDrag(testXMoveDragBeginRequest());
    ASSERT_TRUE(begin.ok) << begin.message;
    EXPECT_TRUE(begin.active);
    EXPECT_FALSE(begin.previewDriven);
    EXPECT_TRUE(begin.sceneTransform);
    EXPECT_TRUE(begin.hasScenePreviewTransform);

    ModelingTransformDragPreview update = service.updateTransformDrag(testXMoveDragUpdateRequest(2.0f));
    ASSERT_TRUE(update.ok) << update.message;
    EXPECT_FALSE(update.previewDriven);
    EXPECT_TRUE(update.sceneTransform);
    EXPECT_TRUE(update.hasScenePreviewTransform);
    EXPECT_NEAR(update.moveDelta.x, 2.0f, 0.001f);
    EXPECT_NEAR(update.scenePreviewTransform.origin.x, 2.0f, 0.001f);
    ASSERT_NE(scene.transform(box.object.entity), nullptr);
    EXPECT_NEAR(scene.transform(box.object.entity)->position.x, 2.0f, 0.001f);

    ModelingTransformDragPreview commit = service.commitTransformDrag();
    ASSERT_TRUE(commit.ok) << commit.message;
    EXPECT_FALSE(commit.previewDriven);
    EXPECT_TRUE(commit.changed);
    ASSERT_NE(scene.transform(box.object.entity), nullptr);
    EXPECT_NEAR(scene.transform(box.object.entity)->position.x, 2.0f, 0.001f);
    const ModelingTransformDragPreview afterCommitUpdate = service.updateTransformDrag(testXMoveDragUpdateRequest(3.0f));
    EXPECT_TRUE(afterCommitUpdate.ok);
    EXPECT_FALSE(afterCommitUpdate.active);
}

TEST(ModelingSessionServiceTests, PublicTransformDragObjectSelectionRecordsSceneTransformHistory) {
    ProjectSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::fromProjectSession(session));

    const QuaderApiResult<QuaderModelingObjectRef> box = editor.modeling().createBox({
            .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
            .name = "History Object Drag Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(session.modeling().selectionMode(), ModelingSelectionMode::Object);

    QuaderApiResult<QuaderTransformDragPreview> begin =
            editor.modeling().beginTransformDrag(testApiXMoveDragBeginRequest());
    ASSERT_TRUE(begin.ok) << begin.message;
    EXPECT_TRUE(begin.value.active);
    EXPECT_TRUE(begin.value.sceneTransform);

    QuaderApiResult<QuaderTransformDragPreview> update =
            editor.modeling().updateTransformDrag(testApiXMoveDragUpdateRequest(2.0f));
    ASSERT_TRUE(update.ok) << update.message;
    EXPECT_TRUE(update.value.hasScenePreviewTransform);
    ASSERT_NE(session.scene().transform(box.value.entity), nullptr);
    EXPECT_NEAR(session.scene().transform(box.value.entity)->position.x, 2.0f, 0.001f);

    QuaderApiResult<QuaderTransformDragPreview> commit = editor.modeling().commitTransformDrag();
    ASSERT_TRUE(commit.ok) << commit.message;
    EXPECT_TRUE(commit.changed);
    ASSERT_NE(session.scene().transform(box.value.entity), nullptr);
    EXPECT_NEAR(session.scene().transform(box.value.entity)->position.x, 2.0f, 0.001f);
    EXPECT_TRUE(editor.history().canUndo());

    const QuaderApiVoidResult undo = editor.history().undo();
    ASSERT_TRUE(undo.ok) << undo.message;
    ASSERT_NE(session.scene().transform(box.value.entity), nullptr);
    EXPECT_NEAR(session.scene().transform(box.value.entity)->position.x, 0.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, PublicTransformDragFaceSelectionRecordsModelingHistory) {
    ProjectSession session;
    QuaderEditorApi editor(QuaderApiSessionInternalAccess::fromProjectSession(session));

    const QuaderApiResult<QuaderModelingObjectRef> box = editor.modeling().createBox({
            .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
            .name = "History Face Drag Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(editor.selection().setMode(QuaderSelectionMode::Face).ok());

    QuaderPickSelectRequest pickFace;
    pickFace.ray = {
            .origin = {0.0f, 0.0f, 5.0f},
            .direction = {0.0f, 0.0f, -1.0f},
    };
    const QuaderApiResult<QuaderSelectionSummary> picked = editor.selection().pickSelect(pickFace);
    ASSERT_TRUE(picked.ok) << picked.message;
    ASSERT_EQ(picked.value.selectedFaceCount, 1u);

    auto meshBounds = [&]() {
        const std::optional<MeshAsset> mesh = session.modeling().compileObjectMesh(box.value.modelingObjectId);
        EXPECT_TRUE(mesh.has_value());
        return mesh.has_value() ? localMeshBounds(*mesh) : std::optional<SceneBounds>{};
    };
    std::optional<SceneBounds> bounds = meshBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->max.x, 1.0f, 0.001f);

    auto dragFaceBy = [&](float screenX) {
        QuaderTransformDragBeginRequest beginRequest = testApiXMoveDragBeginRequest();
        if (const std::optional<QVec3> center = editor.query().selectionCenter()) {
            beginRequest.pivot = *center;
        }
        QuaderApiResult<QuaderTransformDragPreview> begin = editor.modeling().beginTransformDrag(beginRequest);
        EXPECT_TRUE(begin.ok) << begin.message;
        EXPECT_TRUE(begin.value.active);
        EXPECT_TRUE(begin.value.previewDriven);

        QuaderApiResult<QuaderTransformDragPreview> update =
                editor.modeling().updateTransformDrag(testApiXMoveDragUpdateRequest(screenX));
        EXPECT_TRUE(update.ok) << update.message;
        EXPECT_TRUE(update.value.previewDriven);

        QuaderApiResult<QuaderTransformDragPreview> commit = editor.modeling().commitTransformDrag();
        EXPECT_TRUE(commit.ok) << commit.message;
        EXPECT_TRUE(commit.changed);
    };

    dragFaceBy(2.0f);
    bounds = meshBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->max.x, 3.0f, 0.001f);

    dragFaceBy(2.0f);
    bounds = meshBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->max.x, 5.0f, 0.001f);

    QuaderApiVoidResult undo = editor.history().undo();
    ASSERT_TRUE(undo.ok) << undo.message;
    EXPECT_TRUE(session.scene().exists(box.value.entity));
    bounds = meshBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->max.x, 3.0f, 0.001f);

    undo = editor.history().undo();
    ASSERT_TRUE(undo.ok) << undo.message;
    EXPECT_TRUE(session.scene().exists(box.value.entity));
    bounds = meshBounds();
    ASSERT_TRUE(bounds.has_value());
    EXPECT_NEAR(bounds->max.x, 1.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, TransformDragShiftDuplicateUsesScenePreviewForRotatedObject) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Rotated Shift Duplicate Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    TransformComponent sourceTransform;
    sourceTransform.rotationEuler = {0.0f, 0.5f, 0.25f};
    scene.setTransform(box.object.entity, sourceTransform);
    const MeshComponent* sourceMesh = scene.mesh(box.object.entity);
    ASSERT_NE(sourceMesh, nullptr);
    MeshComponent sourceMeshSettings = *sourceMesh;
    sourceMeshSettings.doubleSided = true;
    scene.setMesh(box.object.entity, sourceMeshSettings);

    ModelingTransformDragPreview begin =
            service.beginTransformDrag(testXMoveDragBeginRequest({}, ModelingTransformOperation::Transform, true));
    ASSERT_TRUE(begin.ok) << begin.message;
    EXPECT_TRUE(begin.active);
    EXPECT_FALSE(begin.previewDriven);
    EXPECT_TRUE(begin.sceneTransform);
    EXPECT_TRUE(begin.hasScenePreviewTransform);
    ASSERT_EQ(scene.entities().size(), 2u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    const EntityId duplicateEntity = scene.selectedEntities().front();
    EXPECT_NE(duplicateEntity, box.object.entity);
    ASSERT_NE(scene.transform(duplicateEntity), nullptr);
    expectVec3Near(scene.transform(duplicateEntity)->rotationEuler, sourceTransform.rotationEuler);
    ASSERT_NE(scene.mesh(duplicateEntity), nullptr);
    EXPECT_TRUE(scene.mesh(duplicateEntity)->doubleSided);

    ModelingTransformDragPreview update = service.updateTransformDrag(testXMoveDragUpdateRequest(2.0f));
    ASSERT_TRUE(update.ok) << update.message;
    EXPECT_FALSE(update.previewDriven);
    EXPECT_TRUE(update.sceneTransform);
    EXPECT_TRUE(update.hasScenePreviewTransform);
    EXPECT_NEAR(update.moveDelta.x, 2.0f, 0.001f);
    EXPECT_NEAR(update.scenePreviewTransform.origin.x, 2.0f, 0.001f);

    const ModelingCommandResult cancel = service.cancelTransformDrag();
    ASSERT_TRUE(cancel.ok) << cancel.message;
    EXPECT_EQ(scene.entities().size(), 1u);
}

TEST(ModelingSessionServiceTests, TransformDragShiftDuplicateKeepsGroupedMeshInsideParent) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult first = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Grouped Shift Source",
    });
    ASSERT_TRUE(first.ok) << first.message;
    const ModelingCreateBoxResult second = service.createBoxFromBounds({
        .bounds = {.min = {2.0f, -1.0f, -1.0f}, .max = {4.0f, 1.0f, 1.0f}},
        .name = "Grouped Sibling",
    });
    ASSERT_TRUE(second.ok) << second.message;

    const EntityId group = scene.createEmpty("Parent Group");
    ASSERT_TRUE(scene.setParent(first.object.entity, group, true));
    ASSERT_TRUE(scene.setParent(second.object.entity, group, true));
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Object).ok);
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {first.object.entity}, first.object.entity).ok);

    ModelingTransformDragPreview begin =
            service.beginTransformDrag(testXMoveDragBeginRequest({}, ModelingTransformOperation::Transform, true));
    ASSERT_TRUE(begin.ok) << begin.message;
    EXPECT_TRUE(begin.active);
    EXPECT_FALSE(begin.previewDriven);
    EXPECT_TRUE(begin.sceneTransform);
    ASSERT_EQ(scene.entities().size(), 4u);
    ASSERT_EQ(service.objects().size(), 3u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    const EntityId duplicateEntity = scene.selectedEntities().front();
    EXPECT_NE(duplicateEntity, first.object.entity);
    EXPECT_NE(duplicateEntity, second.object.entity);
    EXPECT_EQ(scene.parent(duplicateEntity), group);
    EXPECT_EQ(scene.children(group).size(), 3u);

    const ModelingCommandResult cancel = service.cancelTransformDrag();
    ASSERT_TRUE(cancel.ok) << cancel.message;
    EXPECT_EQ(scene.entities().size(), 3u);
    EXPECT_EQ(scene.children(group).size(), 2u);
}

TEST(ModelingSessionServiceTests, TransformDragComponentCancelRestoresMeshOverlayAndRuntime) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Drag Cancel Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    ASSERT_TRUE(service.selectAll().ok);

    ModelingTransformDragPreview begin = service.beginTransformDrag(testXMoveDragBeginRequest());
    ASSERT_TRUE(begin.ok) << begin.message;
    ASSERT_TRUE(begin.previewDriven);

    ModelingTransformDragPreview update = service.updateTransformDrag(testXMoveDragUpdateRequest(2.0f));
    ASSERT_TRUE(update.ok) << update.message;
    std::optional<SceneBounds> selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    EXPECT_NEAR(boundsCenter(*selectedBounds).x, 2.0f, 0.001f);

    const ModelingCommandResult cancel = service.cancelTransformDrag();
    ASSERT_TRUE(cancel.ok) << cancel.message;
    selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    EXPECT_NEAR(boundsCenter(*selectedBounds).x, 0.0f, 0.001f);

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    std::optional<SceneBounds> pointBounds;
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer != ViewportOverlayLayer::SelectedVertex) {
            continue;
        }
        if (!pointBounds) {
            pointBounds = SceneBounds{.min = point.position, .max = point.position};
        } else {
            includePoint(*pointBounds, point.position);
        }
    }

    ASSERT_TRUE(pointBounds.has_value());
    EXPECT_NEAR(boundsCenter(*pointBounds).x, 0.0f, 0.001f);

    ModelingTransformDragPreview afterCancelUpdate = service.updateTransformDrag(testXMoveDragUpdateRequest(3.0f));
    EXPECT_TRUE(afterCancelUpdate.ok);
    EXPECT_FALSE(afterCancelUpdate.active);
}

TEST(ModelingSessionServiceTests, TransformPreviewDuplicateTranslateRefreshesMeshAndKeepsDuplicateSelectable) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Shift Duplicate Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    TransformComponent sourceTransform;
    scene.setTransform(box.object.entity, sourceTransform);
    const MeshComponent* sourceMesh = scene.mesh(box.object.entity);
    ASSERT_NE(sourceMesh, nullptr);
    MeshComponent sourceMeshSettings = *sourceMesh;
    sourceMeshSettings.doubleSided = true;
    scene.setMesh(box.object.entity, sourceMeshSettings);
    ASSERT_EQ(scene.entities().size(), 1u);

    const ModelingCommandResult begin = service.beginTransformPreview(true);
    ASSERT_TRUE(begin.ok) << begin.message;
    ASSERT_TRUE(begin.changed);
    ASSERT_EQ(scene.entities().size(), 2u);
    ASSERT_EQ(service.objects().size(), 2u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);

    const EntityId duplicateEntity = scene.selectedEntities().front();
    EXPECT_NE(duplicateEntity, box.object.entity);
    ASSERT_NE(scene.transform(duplicateEntity), nullptr);
    expectVec3Near(scene.transform(duplicateEntity)->position, sourceTransform.position);
    expectVec3Near(scene.transform(duplicateEntity)->rotationEuler, sourceTransform.rotationEuler);
    ASSERT_NE(scene.mesh(duplicateEntity), nullptr);
    EXPECT_TRUE(scene.mesh(duplicateEntity)->doubleSided);
    const std::optional<int> duplicateModelingObjectId = service.modelingObjectForEntity(duplicateEntity);
    ASSERT_TRUE(duplicateModelingObjectId.has_value());
    EXPECT_NE(*duplicateModelingObjectId, box.object.modelingObjectId);

    const ModelingCommandResult firstMove = service.updateTransformTranslatePreview({1.0f, 0.0f, 0.0f});
    ASSERT_TRUE(firstMove.ok) << firstMove.message;
    ASSERT_TRUE(firstMove.changed);
    std::optional<SceneBounds> selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    EXPECT_NEAR(boundsCenter(*selectedBounds).x, 1.0f, 0.001f);

    const std::uint64_t revisionAfterFirstMove = scene.revision();
    const ModelingCommandResult secondMove = service.updateTransformTranslatePreview({3.0f, 0.0f, 0.0f});
    ASSERT_TRUE(secondMove.ok) << secondMove.message;
    ASSERT_TRUE(secondMove.changed);
    EXPECT_GT(scene.revision(), revisionAfterFirstMove);
    selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    EXPECT_NEAR(boundsCenter(*selectedBounds).x, 3.0f, 0.001f);
    ASSERT_NE(scene.transform(duplicateEntity), nullptr);
    expectVec3Near(scene.transform(duplicateEntity)->rotationEuler, sourceTransform.rotationEuler);
    ASSERT_NE(scene.mesh(duplicateEntity), nullptr);
    EXPECT_TRUE(scene.mesh(duplicateEntity)->doubleSided);

    const ModelingCommandResult commit = service.commitTransformPreview();
    ASSERT_TRUE(commit.ok) << commit.message;
    ASSERT_TRUE(commit.changed);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), duplicateEntity);
    selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    EXPECT_NEAR(boundsCenter(*selectedBounds).x, 3.0f, 0.001f);

    ASSERT_TRUE(service.clearSelection().ok);
    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {
                    .origin = {3.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
            .clickCount = 1,
    });
    EXPECT_EQ(picked.selectedObjectCount, 1u);
    EXPECT_EQ(picked.activeObjectId, *duplicateModelingObjectId);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(service.modelingObjectForEntity(scene.selectedEntities().front()), duplicateModelingObjectId);
}

TEST(ModelingSessionServiceTests, ShortcutCommandWrappersRefreshSceneAndHistory) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Shortcut Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    EXPECT_EQ(scene.entities().size(), 1u);

    ModelingCommandResult command = service.executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 2u);

    command = service.repeatLastAction();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 3u);

    command = service.copySelection();
    ASSERT_TRUE(command.ok) << command.message;

    const ModelingSessionMemento beforePaste = service.captureHistoryMemento();
    command = service.pasteSelection();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 4u);
    const ModelingSessionMemento afterPaste = service.captureHistoryMemento();

    command = service.restoreHistoryMemento(beforePaste);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 3u);

    command = service.restoreHistoryMemento(afterPaste);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 4u);

    command = service.mirrorSelection(ModelingMirrorAxis::X);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 5u);

    const std::size_t entityCountBeforeFlip = scene.entities().size();
    command = service.flipSelection(ModelingFlipOrientation::Horizontal);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), entityCountBeforeFlip);

    command = service.applyActiveMaterial(7u);
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
}

TEST(ModelingSessionServiceTests, RepeatLastActionReplaysRecordedTransformPreview) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Repeat Move Source",
    });
    ASSERT_TRUE(box.ok) << box.message;

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformTranslatePreview({2.0f, 0.0f, 0.0f}).ok);
    ModelingCommandResult command = service.commitTransformPreview();
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(command.changed);

    command = service.recordRepeatableTransform({
        .active = true,
        .tool = ModelingTransformTool::Move,
        .operation = ModelingTransformOperation::Transform,
        .axis = ModelingTransformAxis::X,
        .moveDelta = {2.0f, 0.0f, 0.0f},
    });
    ASSERT_TRUE(command.ok) << command.message;

    std::optional<QVec3> center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_NEAR(center->x, 2.0f, 0.001f);

    command = service.repeatLastAction();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_NEAR(center->x, 4.0f, 0.001f);
}

TEST(ModelingSessionServiceTests, RepeatLastActionReplaysRecordedExtrudePreview) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Repeat Extrude Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);
    const ModelingSelectionSummary picked = service.pickSelect(ModelingPickSelectRequest{
            .ray = {
                    .origin = {0.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
            .clickCount = 1,
    });
    ASSERT_EQ(picked.selectedFaceCount, 1u);

    const std::optional<MeshAsset> before = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(before.has_value());

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformExtrudePreview({0.0f, 0.0f, 1.0f}, 1.0f).ok);
    ModelingCommandResult command = service.commitTransformPreview();
    ASSERT_TRUE(command.ok) << command.message;
    ASSERT_TRUE(command.changed);

    command = service.recordRepeatableTransform({
        .active = true,
        .tool = ModelingTransformTool::Move,
        .operation = ModelingTransformOperation::Extrude,
        .axis = ModelingTransformAxis::Z,
        .moveDelta = {0.0f, 0.0f, 1.0f},
    });
    ASSERT_TRUE(command.ok) << command.message;

    const std::optional<MeshAsset> afterFirst = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(afterFirst.has_value());
    ASSERT_GT(afterFirst->indices.size(), before->indices.size());

    command = service.repeatLastAction();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);

    const std::optional<MeshAsset> afterRepeat = service.compileObjectMesh(box.object.modelingObjectId);
    ASSERT_TRUE(afterRepeat.has_value());
    EXPECT_GT(afterRepeat->indices.size(), afterFirst->indices.size());
}

TEST(ModelingSessionServiceTests, EditorSceneSelectionForcesEntityObjectModeFromComponentMode) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Component Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    EXPECT_EQ(service.selectionMode(), ModelingSelectionMode::Vertex);

    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {box.object.entity}).ok);

    EXPECT_EQ(service.selectionMode(), ModelingSelectionMode::Object);
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Object);
    EXPECT_EQ(service.selectionSummary().activeObjectId, box.object.modelingObjectId);
}

TEST(ModelingSessionServiceTests, SelectionModeSwitchPreservesSceneOnlySelection) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Stale Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    const EntityId empty = scene.createEmpty("Empty");
    scene.clearSelection();
    scene.setSelected(empty, true);

    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Object).ok);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), empty);
    EXPECT_EQ(scene.primarySelection(), empty);
}

TEST(ModelingSessionServiceTests, GroupSelectionSurvivesTransformToolActivation) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Grouped Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    const EntityId group = scene.createEmpty("Group");
    ASSERT_TRUE(scene.setParent(box.object.entity, group, true));
    scene.clearSelection();
    scene.setSelected(group, true);
    scene.setActiveSelection(group);
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Group).ok);

    ASSERT_TRUE(service.activateViewportTool(ModelingViewportTool::Move).ok);
    EXPECT_EQ(service.selectionMode(), ModelingSelectionMode::Group);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.primarySelection(), group);
    EXPECT_EQ(scene.selectedEntities().front(), group);

    ASSERT_TRUE(service.activateViewportTool(ModelingViewportTool::Rotate).ok);
    EXPECT_EQ(service.selectionMode(), ModelingSelectionMode::Group);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.primarySelection(), group);
}

TEST(ModelingSessionServiceTests, GroupShiftDragDuplicateClonesHierarchyWithoutRootMeshStrays) {
    SceneDocument scene;
    AssetRegistry assets;
    EditorHistoryService history;
    DocumentCommandService commands(scene, history);
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult left = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Left Grouped Box",
    });
    ASSERT_TRUE(left.ok) << left.message;
    const ModelingCreateBoxResult right = service.createBoxFromBounds({
        .bounds = {
            .min = {-0.5f, -0.5f, -0.5f},
            .max = {0.5f, 0.5f, 0.5f},
        },
        .name = "Right Grouped Box",
    });
    ASSERT_TRUE(right.ok) << right.message;

    TransformComponent leftTransform;
    leftTransform.position = {4.0f, 0.0f, 0.0f};
    leftTransform.rotationEuler = {0.0f, 0.5f, 0.0f};
    scene.setTransform(left.object.entity, leftTransform);
    TransformComponent rightTransform;
    rightTransform.position = {6.0f, 0.0f, 2.0f};
    rightTransform.rotationEuler = {0.25f, 0.0f, 0.5f};
    scene.setTransform(right.object.entity, rightTransform);

    ASSERT_TRUE(commands.selectEntities({left.object.entity, right.object.entity}, left.object.entity).ok);
    const DocumentGroupSelectionResult grouped = commands.groupSelection("Box Pair", {});
    ASSERT_TRUE(grouped.ok) << grouped.message;
    const EntityId group = grouped.groupEntity;
    ASSERT_NE(group, InvalidEntityId);
    ASSERT_EQ(scene.parent(left.object.entity), group);
    ASSERT_EQ(scene.parent(right.object.entity), group);
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Group).ok);

    const TransformComponent sourceLeftTransform = *scene.transform(left.object.entity);
    const TransformComponent sourceRightTransform = *scene.transform(right.object.entity);
    const ModelingCommandResult duplicate = service.duplicateSelectedHierarchyForGroupDrag(commands);
    ASSERT_TRUE(duplicate.ok) << duplicate.message;
    ASSERT_TRUE(duplicate.changed);
    EXPECT_EQ(service.selectionMode(), ModelingSelectionMode::Group);
    ASSERT_EQ(scene.entities().size(), 6u);
    ASSERT_EQ(service.objects().size(), 4u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    const EntityId duplicateGroup = scene.selectedEntities().front();
    ASSERT_NE(duplicateGroup, group);
    EXPECT_EQ(scene.parent(duplicateGroup), InvalidEntityId);
    EXPECT_EQ(scene.modelingObject(duplicateGroup), nullptr);

    const std::vector<EntityId> duplicateChildren = scene.children(duplicateGroup);
    ASSERT_EQ(duplicateChildren.size(), 2u);
    ASSERT_NE(scene.modelingObject(duplicateChildren[0]), nullptr);
    ASSERT_NE(scene.modelingObject(duplicateChildren[1]), nullptr);
    EXPECT_EQ(scene.parent(duplicateChildren[0]), duplicateGroup);
    EXPECT_EQ(scene.parent(duplicateChildren[1]), duplicateGroup);
    ASSERT_NE(scene.transform(duplicateChildren[0]), nullptr);
    ASSERT_NE(scene.transform(duplicateChildren[1]), nullptr);
    expectVec3Near(scene.transform(duplicateChildren[0])->position, sourceLeftTransform.position);
    expectVec3Near(scene.transform(duplicateChildren[0])->rotationEuler, sourceLeftTransform.rotationEuler);
    expectVec3Near(scene.transform(duplicateChildren[1])->position, sourceRightTransform.position);
    expectVec3Near(scene.transform(duplicateChildren[1])->rotationEuler, sourceRightTransform.rotationEuler);

    std::size_t rootModelingMeshes = 0;
    for (const EntityId entity : scene.entities()) {
        if (scene.modelingObject(entity) != nullptr && scene.parent(entity) == InvalidEntityId) {
            ++rootModelingMeshes;
        }
    }
    EXPECT_EQ(rootModelingMeshes, 0u);
}

TEST(ModelingSessionServiceTests, VertexSelectionModeExposesOverlayPoints) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Vertex Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    const ViewportOverlayFrame frame = service.selectionOverlayFrame();

    EXPECT_FALSE(frame.points.empty());
    const auto vertexPoint = std::find_if(frame.points.begin(), frame.points.end(), [](const ViewportOverlayPoint& point) {
        return point.layer == ViewportOverlayLayer::UnselectedVertex ||
                point.layer == ViewportOverlayLayer::SelectedVertex;
    });
    ASSERT_NE(vertexPoint, frame.points.end());
    EXPECT_EQ(vertexPoint->color, rgba8(107, 230, 255, 255));
    EXPECT_GT(vertexPoint->sizePixels, 0.0f);
}

TEST(ModelingSessionServiceTests, SwitchingSelectedObjectToVertexModeShowsHandlesImmediately) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Mode Switch Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {box.object.entity}).ok);

    const ViewportOverlayFrame objectFrame = service.selectionOverlayFrame();
    ASSERT_FALSE(objectFrame.lines.empty());

    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Vertex).ok);
    const ModelingSelectionSummary vertexSummary = service.selectionSummary();
    EXPECT_EQ(vertexSummary.mode, ModelingSelectionMode::Vertex);
    EXPECT_EQ(vertexSummary.selectedObjectCount, 1u);
    const ViewportOverlayFrame vertexFrame = service.selectionOverlayFrame();
    const auto vertexPoint = std::find_if(vertexFrame.points.begin(), vertexFrame.points.end(), [](const ViewportOverlayPoint& point) {
        return point.layer == ViewportOverlayLayer::UnselectedVertex ||
                point.layer == ViewportOverlayLayer::SelectedVertex;
    });
    ASSERT_NE(vertexPoint, vertexFrame.points.end());
    EXPECT_GT(vertexPoint->sizePixels, 0.0f);
}

TEST(ModelingSessionServiceTests, SwitchingSelectedObjectToEdgeModeShowsHandlesImmediately) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Edge Mode Switch Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {box.object.entity}).ok);

    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Edge).ok);
    const ModelingSelectionSummary edgeSummary = service.selectionSummary();
    EXPECT_EQ(edgeSummary.mode, ModelingSelectionMode::Edge);
    EXPECT_EQ(edgeSummary.selectedObjectCount, 1u);
    const ViewportOverlayFrame edgeFrame = service.selectionOverlayFrame();
    const auto edgeLine = std::find_if(edgeFrame.lines.begin(), edgeFrame.lines.end(), [](const ViewportOverlayLine& line) {
        return line.layer == ViewportOverlayLayer::SourceWire ||
                line.layer == ViewportOverlayLayer::SelectedEdge ||
                line.layer == ViewportOverlayLayer::SelectedFaceEdge;
    });
    ASSERT_NE(edgeLine, edgeFrame.lines.end());
    EXPECT_GT(edgeLine->widthPixels, 0.0f);
}

TEST(ModelingSessionServiceTests, SwitchingSelectedObjectToFaceModeShowsHandlesImmediately) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Face Mode Switch Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {box.object.entity}).ok);

    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);
    const ModelingSelectionSummary faceSummary = service.selectionSummary();
    EXPECT_EQ(faceSummary.mode, ModelingSelectionMode::Face);
    EXPECT_EQ(faceSummary.selectedObjectCount, 1u);
    const ViewportOverlayFrame faceFrame = service.selectionOverlayFrame();
    const auto visiblePrimitive =
            !faceFrame.triangles.empty() ||
            std::any_of(faceFrame.lines.begin(), faceFrame.lines.end(), [](const ViewportOverlayLine& line) {
                return line.layer == ViewportOverlayLayer::SourceWire ||
                        line.layer == ViewportOverlayLayer::SelectedFaceEdge ||
                        line.layer == ViewportOverlayLayer::SelectedEdge;
            });
    EXPECT_TRUE(visiblePrimitive);
}

TEST(ModelingSessionServiceTests, ObjectSelectionOverlayUsesSelectionColors) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);
    EditorSelectionTestHarness editorSelection(scene, assets, service);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Object Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(selectThroughEditor(editorSelection.selection, {box.object.entity}).ok);

    const ViewportOverlayFrame frame = service.selectionOverlayFrame();
    ASSERT_FALSE(frame.triangles.empty());
    ASSERT_FALSE(frame.lines.empty());

    const auto triangle = std::find_if(frame.triangles.begin(), frame.triangles.end(), [](const ViewportOverlayTriangle& value) {
        return value.layer == ViewportOverlayLayer::SelectedXrayFace ||
                value.layer == ViewportOverlayLayer::SelectedFaceMask ||
                value.layer == ViewportOverlayLayer::SelectedVisibleFace;
    });
    ASSERT_NE(triangle, frame.triangles.end());
    for (const ViewportOverlayTriangle& value : frame.triangles) {
        if (value.layer == ViewportOverlayLayer::SelectedXrayFace ||
                value.layer == ViewportOverlayLayer::SelectedFaceMask ||
                value.layer == ViewportOverlayLayer::SelectedVisibleFace) {
            EXPECT_EQ(value.color, rgba8(255, 172, 31, 8));
        }
    }

    const auto wire = std::find_if(frame.lines.begin(), frame.lines.end(), [](const ViewportOverlayLine& value) {
        return value.layer == ViewportOverlayLayer::SelectedEdge;
    });
    ASSERT_NE(wire, frame.lines.end());
    EXPECT_EQ(wire->color, rgba8(255, 211, 31, 255));
}

TEST(ModelingSessionServiceTests, FacePickKeepsSceneEntitySelectedForInspectorAndGizmo) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Face Pick Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_TRUE(service.setSelectionMode(ModelingSelectionMode::Face).ok);

    const ModelingSelectionSummary summary = service.pickSelect(ModelingPickSelectRequest{
            .ray = {
                    .origin = {0.0f, 0.0f, 5.0f},
                    .direction = {0.0f, 0.0f, -1.0f},
            },
            .clickCount = 1,
    });

    EXPECT_EQ(summary.mode, ModelingSelectionMode::Face);
    EXPECT_TRUE(summary.hasSelection);
    EXPECT_EQ(summary.activeKind, ModelingActiveSelectionKind::Face);
    EXPECT_EQ(summary.selectedFaceCount, 1u);
    EXPECT_EQ(scene.primarySelection(), box.object.entity);
    ASSERT_NE(scene.selection(box.object.entity), nullptr);
    EXPECT_TRUE(scene.selection(box.object.entity)->selected);
}

TEST(ModelingSessionServiceTests, SupportsRuntimeBoxPreviewLifecycle) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

    const ModelingBoxToolEvent start{
            .screenPosition = {400.0f, 300.0f},
            .ray = {
                    .origin = {4.0f, 4.0f, 4.0f},
                    .direction = {0.0f, -1.0f, 0.0f},
            },
            .gridSize = 1.0,
    };
    const ModelingBoxToolEvent footprintEnd{
            .screenPosition = {400.0f, 300.0f},
            .ray = {
                    .origin = {6.0f, 4.0f, 7.0f},
                    .direction = {0.0f, -1.0f, 0.0f},
            },
            .gridSize = 1.0,
    };
    const ModelingBoxToolEvent heightEnd{
            .screenPosition = {400.0f, 360.0f},
            .ray = start.ray,
            .gridSize = 1.0,
    };

    ASSERT_TRUE(service.activateBoxTool().ok);

    const ModelingBoxPreview hover = service.updateBoxHover(start);
    EXPECT_TRUE(hover.ok);
    EXPECT_TRUE(hover.valid);
    EXPECT_EQ(hover.dragState, ModelingBoxDragState::Idle);

    const EntityId previousSelection = scene.createEntity("Previous Selection");
    scene.setSelected(previousSelection, true);
    EXPECT_EQ(scene.primarySelection(), previousSelection);

    const ModelingBoxPreview footprintBegin = service.beginBoxFootprint(start);
    EXPECT_TRUE(footprintBegin.ok);
    EXPECT_EQ(footprintBegin.dragState, ModelingBoxDragState::Footprint);
    EXPECT_TRUE(scene.selectedEntities().empty());
    EXPECT_EQ(scene.primarySelection(), InvalidEntityId);

    const ModelingBoxPreview footprintUpdate = service.updateBoxFootprint(footprintEnd);
    EXPECT_TRUE(footprintUpdate.ok);
    EXPECT_TRUE(footprintUpdate.valid);
    EXPECT_EQ(footprintUpdate.dragState, ModelingBoxDragState::Footprint);

    const ModelingBoxPreview heightBegin = service.beginBoxHeight(start);
    EXPECT_TRUE(heightBegin.ok);
    EXPECT_EQ(heightBegin.dragState, ModelingBoxDragState::Height);

    const ModelingBoxPreview heightUpdate = service.updateBoxHeight(heightEnd);
    EXPECT_TRUE(heightUpdate.ok);
    EXPECT_TRUE(heightUpdate.valid);
    EXPECT_EQ(heightUpdate.dragState, ModelingBoxDragState::Height);
    EXPECT_GT(heightUpdate.distance, 0.0);
    EXPECT_GE(heightUpdate.cornerCount, 8u);

    const ModelingCreateBoxResult commit = service.commitBox("Preview Box");
    ASSERT_TRUE(commit.ok) << commit.message;
    EXPECT_TRUE(commit.changed);
    EXPECT_NE(commit.object.entity, InvalidEntityId);
    EXPECT_EQ(scene.primarySelection(), commit.object.entity);
    EXPECT_EQ(assets.meshCount(), 1u);
}

TEST(MaterialDocumentTests, DefaultQuaderMaterialUsesDefaultMaterialTextureSlots) {
    const MaterialDocument material = makeDefaultQuaderMaterial("Default", 41, 42);
    EXPECT_TRUE(material.nodeGraphReserved);
    EXPECT_FLOAT_EQ(material.baseColor.x, 1.0f);
    EXPECT_FLOAT_EQ(material.baseColor.y, 1.0f);
    EXPECT_FLOAT_EQ(material.baseColor.z, 1.0f);
    EXPECT_FLOAT_EQ(material.metallic, 0.0f);
    EXPECT_FLOAT_EQ(material.roughness, 1.0f);
    ASSERT_TRUE(material.textureSlots.contains(MaterialTextureSlot::BaseColor));
    ASSERT_TRUE(material.textureSlots.contains(MaterialTextureSlot::Roughness));
    EXPECT_EQ(material.textureSlots.at(MaterialTextureSlot::BaseColor), 41u);
    EXPECT_EQ(material.textureSlots.at(MaterialTextureSlot::Roughness), 42u);
}

TEST(MaterialDocumentTests, MaterialTextureSlotPolicyOwnsAliasesAndSupportedSlots) {
    ASSERT_EQ(materialTextureSlotDescriptors().size(), 2u);
    EXPECT_EQ(std::string_view(materialTextureSlotDescriptors()[0].id), std::string_view(MaterialTextureSlot::BaseColor));
    EXPECT_EQ(std::string_view(materialTextureSlotDescriptors()[0].legacyAlias), std::string_view("baseColor"));
    EXPECT_EQ(std::string_view(materialTextureSlotDescriptors()[1].id), std::string_view(MaterialTextureSlot::Roughness));
    EXPECT_EQ(std::string_view(materialTextureSlotDescriptors()[1].legacyAlias), std::string_view("roughness"));

    ASSERT_TRUE(canonicalMaterialTextureSlot("baseColor").has_value());
    EXPECT_EQ(*canonicalMaterialTextureSlot("baseColor"), MaterialTextureSlot::BaseColor);
    ASSERT_TRUE(canonicalMaterialTextureSlot(MaterialTextureSlot::Roughness).has_value());
    EXPECT_EQ(*canonicalMaterialTextureSlot(MaterialTextureSlot::Roughness), MaterialTextureSlot::Roughness);
    EXPECT_FALSE(canonicalMaterialTextureSlot("normalTexture").has_value());

    MaterialDocument material;
    material.textureSlots["baseColor"] = 41u;
    material.textureSlots["roughness"] = 42u;
    material.textureSlots["normalTexture"] = 43u;
    normalizeMaterialTextureSlots(material);

    ASSERT_TRUE(material.textureSlots.contains(MaterialTextureSlot::BaseColor));
    ASSERT_TRUE(material.textureSlots.contains(MaterialTextureSlot::Roughness));
    EXPECT_EQ(material.textureSlots.at(MaterialTextureSlot::BaseColor), 41u);
    EXPECT_EQ(material.textureSlots.at(MaterialTextureSlot::Roughness), 42u);
    EXPECT_FALSE(material.textureSlots.contains("baseColor"));
    EXPECT_FALSE(material.textureSlots.contains("roughness"));
    EXPECT_FALSE(material.textureSlots.contains("normalTexture"));
}

TEST(MaterialDocumentTests, MaterialTextureSlotPolicyOwnsApiQdrAndRenderSlotNormalization) {
    const std::string materialHeader = readSourceFile("src/assets/material_document.hpp");
    const std::string materialSource = readSourceFile("src/assets/material_document.cpp");
    const std::string registryHeader = readSourceFile("src/assets/asset_registry.hpp");
    const std::string registrySource = readSourceFile("src/assets/asset_registry.cpp");
    const std::string apiSource = readSourceFile("src/api/quader_asset_api.cpp");
    const std::string documentCommandSource = readSourceFile("src/editor/commands/document_command_service.cpp");
    const std::string qdrReaderSource = readSourceFile("src/io/qdr/qdr_document_reader.cpp");
    const std::string qdrWriterSource = readSourceFile("src/io/qdr/qdr_document_writer.cpp");
    const std::string textureBindingSource = readSourceFile("src/render/texture_binding_service.cpp");
    const std::string renderSignatureSource = readSourceFile("src/render/render_signature_builder.cpp");
    const std::size_t setTextureStart =
            apiSource.find("QuaderApiVoidResult QuaderAssetApi::setMaterialTexture");
    const std::size_t assignMaterialStart =
            apiSource.find("QuaderApiVoidResult QuaderAssetApi::assignMaterialToEntity");
    const std::size_t assignObjectStart =
            apiSource.find("QuaderApiVoidResult QuaderAssetApi::assignMaterialToObject");
    ASSERT_NE(setTextureStart, std::string::npos);
    ASSERT_NE(assignMaterialStart, std::string::npos);
    ASSERT_NE(assignObjectStart, std::string::npos);
    const std::string setTextureSource =
            apiSource.substr(setTextureStart, assignMaterialStart - setTextureStart);
    const std::string assignMaterialSource =
            apiSource.substr(assignMaterialStart, assignObjectStart - assignMaterialStart);

    EXPECT_NE(materialHeader.find("struct MaterialTextureSlotDescriptor"), std::string::npos);
    EXPECT_NE(materialSource.find("kMaterialTextureSlotDescriptors"), std::string::npos);
    EXPECT_NE(materialSource.find("normalizeMaterialTextureSlots"), std::string::npos);
    EXPECT_NE(registryHeader.find("AssetRegistryMutationResult setMaterialTexture"), std::string::npos);
    EXPECT_NE(registrySource.find("AssetRegistry::setMaterialTexture"), std::string::npos);
    EXPECT_NE(registrySource.find("canonicalMaterialTextureSlot(slot)"), std::string::npos);
    EXPECT_NE(registrySource.find("validateMaterial(updated)"), std::string::npos);
    EXPECT_NE(setTextureSource.find(".assets().setMaterialTexture(material, slot, texture)"), std::string::npos);
    EXPECT_NE(documentCommandSource.find("DocumentCommandService::setMaterial"), std::string::npos);
    EXPECT_NE(documentCommandSource.find("assets_->material(material)"), std::string::npos);
    EXPECT_EQ(setTextureSource.find("canonicalMaterialTextureSlot"), std::string::npos);
    EXPECT_EQ(setTextureSource.find("setMaterialTextureSlot("), std::string::npos);
    EXPECT_EQ(setTextureSource.find(".texture(texture)"), std::string::npos);
    EXPECT_EQ(assignMaterialSource.find(".assets().material(material)"), std::string::npos);
    EXPECT_NE(qdrReaderSource.find("setMaterialTextureSlot(material, slot"), std::string::npos);
    EXPECT_NE(qdrWriterSource.find("materialTextureSlotDescriptors()"), std::string::npos);
    EXPECT_NE(textureBindingSource.find("materialTextureSlotAsset(*material, slotName)"), std::string::npos);
    EXPECT_NE(renderSignatureSource.find("materialTextureSlotDescriptors()"), std::string::npos);
    EXPECT_EQ(textureBindingSource.find("alternateSlot"), std::string::npos);
    EXPECT_EQ(textureBindingSource.find("ends_with(\"Texture\")"), std::string::npos);
}

TEST(BakeServiceTests, ValidatesDescriptors) {
    const auto invalid = BakeService::validate({});
    EXPECT_FALSE(invalid.valid);

    const auto valid = BakeService::validate({
        .mesh = 42,
        .maps = {BakeMapType::Curvature, BakeMapType::Thickness},
        .width = 1024,
        .height = 1024,
    });
    EXPECT_TRUE(valid.valid);
}
