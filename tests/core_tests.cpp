////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/AssetRegistry.h"
#include "assets/BakeService.h"
#include "assets/MaterialDocument.h"
#include "core/Logger.h"
#include "core/UndoRedoManager.h"
#include "editor/commands/DocumentCommandService.h"
#include "editor/history/SceneTransformPreviewTransaction.h"
#include "editor/transform/SceneTransformDragService.h"
#include "editor/commands/document/ApplyEntityTransformCommand.h"
#include "editor/commands/document/BatchDocumentCommand.h"
#include "editor/commands/document/CreateEntityCommand.h"
#include "editor/commands/document/DestroyEntityCommand.h"
#include "editor/commands/document/DuplicateEntityCommand.h"
#include "editor/commands/document/GroupSelectionCommand.h"
#include "editor/commands/document/RenameEntityCommand.h"
#include "editor/commands/document/ReparentEntityCommand.h"
#include "editor/commands/document/SelectEntitiesCommand.h"
#include "editor/commands/document/SetLockCommand.h"
#include "editor/commands/document/SetVisibilityCommand.h"
#include "editor/commands/document/UngroupSelectionCommand.h"
#include "scene/document/ProjectDocument.h"
#include "scene/document/ProjectSession.h"
#include "editor/commands/EditorCommandService.h"
#include "editor/history/EditorHistoryService.h"
#include "editor/history/ModelingMementoCommand.h"
#include "editor/selection/EditorSelectionService.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "render/overlay/ViewportOverlayStyleResolver.h"
#include "render/ViewportOverlayPolicy.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"
#include "scene/SceneEntityNaming.h"
#include "scene/TransformPivot.h"
#include "platform/PlatformWindow.h"
#include "api/QuaderEditorApi.h"
#include "ui/ConsoleModel.h"
#include "ui/backends/rml/MainLayoutRmlBuilder.h"
#include "ui/backends/rml/RmlMarkupWriter.h"
#include "ui/components/atoms/UIButtonComponent.h"
#include "ui/components/atoms/UIChromeButtonComponent.h"
#include "ui/components/atoms/UIToolbarButtonComponent.h"
#include "ui/components/common/DockLayoutModel.h"
#include "ui/components/common/ResizableSplit.h"
#include "ui/components/common/UIDragDrop.h"
#include "ui/components/common/UITabStripModel.h"
#include "ui/components/organisms/MainLayoutModel.h"
#include "ui/components/organisms/ModelingModeShelfComponent.h"
#include "ui/components/organisms/WindowChromeControlsComponent.h"
#include "ui/core/UIClassList.h"
#include "ui/core/UIElement.h"
#include "ui/core/UIIconRegistry.h"
#include "ui/controllers/MainLayoutController.h"
#include "ui/controllers/MainLayoutDockDragController.h"
#include "ui/controllers/MainLayoutSashRegistry.h"
#include "ui/controllers/ResizableSplitController.h"
#include "ui/controllers/UIDragDropController.h"
#include "ui/DebugPaletteView.h"
#include "ui/UISettings.h"
#include "ui/UIUtilityTokens.h"
#include "ui/ViewportContextMenuPolicy.h"
#include "viewport/HostViewportContractAdapter.h"
#include "viewport/ViewportCameraAdapter.h"
#include "viewport/ViewportCameraController.h"
#include "viewport/ViewportGridPlacementPolicy.h"
#include "viewport/ViewportInputRouter.h"
#include "viewport/ViewportLayout.h"
#include "viewport/ViewportSettings.h"
#include "viewport/ViewportWorkspaceService.h"

#include <gtest/gtest.h>

#include <editor/interactions/viewport_navigation.h>

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
#include <type_traits>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

using namespace quader;

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

std::string readSourceFile(const std::filesystem::path& relativePath) {
    const std::filesystem::path path = std::filesystem::path(QUADER_SOURCE_DIR) / relativePath;
    std::ifstream sourceFile(path);
    EXPECT_TRUE(sourceFile.is_open()) << path.string();
    return {
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
}

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

/**
 * Carries EditorSelectionTestHarness state used by the test layer.
 */
struct EditorSelectionTestHarness {
    explicit EditorSelectionTestHarness(
            SceneDocument& scene,
            AssetRegistry& assets,
            ModelingSessionService& modeling)
        : documentCommands(scene, history)
        , selection(scene, documentCommands, assets, modeling) {}

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
            bool redoChanged = true)
        : label_(std::move(label))
        , value_(value)
        , before_(before)
        , after_(after)
        , failUndo_(failUndo)
        , failRedo_(failRedo)
        , undoChanged_(undoChanged)
        , redoChanged_(redoChanged) {}

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

private:
    std::string label_;
    int& value_;
    int before_ = 0;
    int after_ = 0;
    bool* failUndo_ = nullptr;
    bool* failRedo_ = nullptr;
    bool undoChanged_ = true;
    bool redoChanged_ = true;
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
    const std::filesystem::path mainPath = std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "main.cpp";
    std::ifstream mainFile(mainPath);
    ASSERT_TRUE(mainFile.is_open()) << mainPath.string();
    const std::string mainSource{std::istreambuf_iterator<char>(mainFile), std::istreambuf_iterator<char>()};

    const std::filesystem::path handlerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "platform" / "FatalErrorHandler.cpp";
    std::ifstream handlerFile(handlerPath);
    ASSERT_TRUE(handlerFile.is_open()) << handlerPath.string();
    const std::string handlerSource{std::istreambuf_iterator<char>(handlerFile), std::istreambuf_iterator<char>()};
    const std::filesystem::path cmakePath = std::filesystem::path(QUADER_SOURCE_DIR) / "CMakeLists.txt";
    std::ifstream cmakeFile(cmakePath);
    ASSERT_TRUE(cmakeFile.is_open()) << cmakePath.string();
    const std::string cmakeSource{std::istreambuf_iterator<char>(cmakeFile), std::istreambuf_iterator<char>()};

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

TEST(UISettingsTests, DefaultsKeepViewportShellLocale) {
    const UISettings settings = UISettings::defaults();

    EXPECT_EQ(settings.locale(), "en-US");
    EXPECT_EQ(settings.mainLayout().sidebarSide, MainLayoutSidebarSide::Right);
    EXPECT_EQ(settings.mainLayout().sidebar.widthDp, kMainLayoutSidebarDefaultWidthDp);
    EXPECT_EQ(settings.mainLayout().auxiliarySidebar.widthDp, kMainLayoutSidebarDefaultWidthDp);
    EXPECT_EQ(settings.mainLayout().outlinerHeightDp, kMainLayoutOutlinerDefaultHeightDp);
    EXPECT_EQ(settings.mainLayout().bottomDock.heightDp, kMainLayoutBottomDockDefaultHeightDp);
    EXPECT_FALSE(settings.mainLayout().sidebar.collapsed);
    EXPECT_FALSE(settings.mainLayout().auxiliarySidebar.collapsed);
    EXPECT_FALSE(settings.mainLayout().bottomDock.collapsed);
    EXPECT_TRUE(settings.mainLayout().outlinerVisible);
    EXPECT_TRUE(settings.mainLayout().inspectorVisible);
    ASSERT_TRUE(dockPanelLocation(settings.mainLayout().dockLayout, kMainLayoutPanelOutliner));
    ASSERT_TRUE(dockPanelLocation(settings.mainLayout().dockLayout, kMainLayoutPanelInspector));
    EXPECT_EQ(*dockPanelLocation(settings.mainLayout().dockLayout, kMainLayoutPanelOutliner), DockLocation::Sidebar);
    EXPECT_EQ(*dockPanelLocation(settings.mainLayout().dockLayout, kMainLayoutPanelInspector), DockLocation::Sidebar);
    ASSERT_TRUE(dockActivePanelId(settings.mainLayout().dockLayout, DockLocation::Sidebar));
    EXPECT_EQ(*dockActivePanelId(settings.mainLayout().dockLayout, DockLocation::Sidebar), kMainLayoutPanelOutliner);
}

TEST(UISettingsTests, RoundTripsLocaleAndMainLayout) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_round_trip.ini";
    std::filesystem::remove(path);

    UISettings settings = UISettings::defaults();
    settings.setLocale("it-IT");
    MainLayoutModel layout = settings.mainLayout();
    layout.sidebarSide = MainLayoutSidebarSide::Left;
    layout.sidebar.widthDp = 333;
    layout.auxiliarySidebar.widthDp = 277;
    layout.outlinerHeightDp = 271;
    layout.bottomDock.heightDp = 288;
    layout.sidebar.collapsed = true;
    layout.auxiliarySidebar.collapsed = true;
    layout.bottomDock.collapsed = true;
    layout.outlinerVisible = false;
    layout.inspectorVisible = true;
    ASSERT_TRUE(dockMovePanelToLocationAtPanel(
            layout.dockLayout,
            kMainLayoutPanelInspector,
            DockLocation::Sidebar,
            kMainLayoutPanelOutliner,
            DockPanelInsertPosition::Before));
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelInspector, DockLocation::Panel));
    ASSERT_TRUE(dockSplitPanelWithPanel(
            layout.dockLayout,
            kMainLayoutPanelOutliner,
            DockLocation::Panel,
            kMainLayoutPanelInspector,
            DockPanelInsertPosition::After));
    ASSERT_TRUE(dockSetSplitPanelSizeDp(
            layout.dockLayout,
            DockLocation::Panel,
            kMainLayoutPanelInspector,
            316));
    settings.setMainLayout(layout);

    ASSERT_TRUE(settings.save(path));
    {
        std::ifstream saved(path);
        ASSERT_TRUE(saved.is_open());
        const std::string savedText{
                std::istreambuf_iterator<char>(saved),
                std::istreambuf_iterator<char>()};
        EXPECT_NE(savedText.find("DockSplitPanels=Panel:inspector=inspector@316|outliner"), std::string::npos);
    }
    const UISettings loaded = UISettings::load(path);

    EXPECT_EQ(loaded.locale(), "it-IT");
    EXPECT_EQ(loaded.mainLayout().sidebarSide, MainLayoutSidebarSide::Left);
    EXPECT_EQ(loaded.mainLayout().sidebar.widthDp, 333);
    EXPECT_EQ(loaded.mainLayout().auxiliarySidebar.widthDp, 277);
    EXPECT_EQ(loaded.mainLayout().outlinerHeightDp, 271);
    EXPECT_EQ(loaded.mainLayout().bottomDock.heightDp, 288);
    EXPECT_TRUE(loaded.mainLayout().sidebar.collapsed);
    EXPECT_TRUE(loaded.mainLayout().auxiliarySidebar.collapsed);
    EXPECT_TRUE(loaded.mainLayout().bottomDock.collapsed);
    EXPECT_FALSE(loaded.mainLayout().outlinerVisible);
    EXPECT_TRUE(loaded.mainLayout().inspectorVisible);
    ASSERT_TRUE(dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelInspector));
    EXPECT_EQ(*dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelInspector), DockLocation::Panel);
    ASSERT_TRUE(dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelOutliner));
    EXPECT_EQ(*dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelOutliner), DockLocation::Panel);
    ASSERT_TRUE(dockActivePanelId(loaded.mainLayout().dockLayout, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(loaded.mainLayout().dockLayout, DockLocation::Panel), kMainLayoutPanelInspector);
    ASSERT_GE(loaded.mainLayout().dockLayout.panels.size(), 2u);
    EXPECT_EQ(loaded.mainLayout().dockLayout.panels[0].panelId, kMainLayoutPanelInspector);
    EXPECT_EQ(loaded.mainLayout().dockLayout.panels[1].panelId, kMainLayoutPanelOutliner);
    const std::vector<std::string_view> loadedSplitPanels =
            dockSplitPanelsInLocation(loaded.mainLayout().dockLayout, DockLocation::Panel);
    ASSERT_EQ(loadedSplitPanels.size(), 2u);
    EXPECT_EQ(loadedSplitPanels[0], kMainLayoutPanelInspector);
    EXPECT_EQ(loadedSplitPanels[1], kMainLayoutPanelOutliner);
    const std::vector<std::string_view> loadedBottomTabs =
            dockTabPanelsInLocation(loaded.mainLayout().dockLayout, DockLocation::Panel);
    ASSERT_EQ(loadedBottomTabs.size(), 1u);
    EXPECT_EQ(loadedBottomTabs[0], kMainLayoutPanelInspector);
    ASSERT_TRUE(dockSplitPanelSizeDp(
            loaded.mainLayout().dockLayout,
            DockLocation::Panel,
            kMainLayoutPanelInspector));
    EXPECT_EQ(
            *dockSplitPanelSizeDp(
                    loaded.mainLayout().dockLayout,
                    DockLocation::Panel,
                    kMainLayoutPanelInspector),
            316);

    std::filesystem::remove(path);
}

TEST(UISettingsTests, LoadsLegacyLocaleOnlySettingsWithDefaultLayout) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_legacy_locale_only.ini";
    {
        std::ofstream file(path, std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << "[Localization]\n";
        file << "Locale=it-IT\n";
    }

    const UISettings loaded = UISettings::load(path);

    EXPECT_EQ(loaded.locale(), "it-IT");
    EXPECT_EQ(loaded.mainLayout().sidebarSide, MainLayoutSidebarSide::Right);
    EXPECT_EQ(loaded.mainLayout().sidebar.widthDp, kMainLayoutSidebarDefaultWidthDp);
    EXPECT_EQ(loaded.mainLayout().auxiliarySidebar.widthDp, kMainLayoutSidebarDefaultWidthDp);
    EXPECT_EQ(loaded.mainLayout().outlinerHeightDp, kMainLayoutOutlinerDefaultHeightDp);
    EXPECT_EQ(loaded.mainLayout().bottomDock.heightDp, kMainLayoutBottomDockDefaultHeightDp);
    EXPECT_FALSE(loaded.mainLayout().sidebar.collapsed);
    EXPECT_FALSE(loaded.mainLayout().auxiliarySidebar.collapsed);
    EXPECT_FALSE(loaded.mainLayout().bottomDock.collapsed);
    EXPECT_TRUE(loaded.mainLayout().outlinerVisible);
    EXPECT_TRUE(loaded.mainLayout().inspectorVisible);
    ASSERT_TRUE(dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelOutliner));
    ASSERT_TRUE(dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelInspector));
    EXPECT_EQ(*dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelOutliner), DockLocation::Sidebar);
    EXPECT_EQ(*dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelInspector), DockLocation::Sidebar);

    std::filesystem::remove(path);
}

TEST(UISettingsTests, IgnoresMalformedLayoutSettingsAndClampsSizes) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_malformed_layout.ini";
    {
        std::ofstream file(path, std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << "[MainLayout]\n";
        file << "SidebarSide=Bottom\n";
        file << "SidebarWidthDp=12\n";
        file << "AuxiliarySidebarWidthDp=24\n";
        file << "SidebarCollapsed=maybe\n";
        file << "AuxiliarySidebarCollapsed=maybe\n";
        file << "BottomDockCollapsed=yes\n";
        file << "OutlinerHeightDp=-42\n";
        file << "BottomDockHeightDp=-1\n";
        file << "OutlinerVisible=what\n";
        file << "InspectorVisible=0\n";
        file << "OutlinerDockLocation=Nowhere\n";
        file << "InspectorDockLocation=Panel\n";
        file << "DockActivePanels=Panel:outliner,Panel:inspector,AuxiliaryBar:inspector\n";
    }

    const UISettings loaded = UISettings::load(path);

    EXPECT_EQ(loaded.mainLayout().sidebarSide, MainLayoutSidebarSide::Right);
    EXPECT_EQ(loaded.mainLayout().sidebar.widthDp, kMainLayoutSidebarMinWidthDp);
    EXPECT_EQ(loaded.mainLayout().auxiliarySidebar.widthDp, kMainLayoutSidebarMinWidthDp);
    EXPECT_EQ(loaded.mainLayout().outlinerHeightDp, kMainLayoutPanelMinHeightDp);
    EXPECT_EQ(loaded.mainLayout().bottomDock.heightDp, kMainLayoutPanelMinHeightDp);
    EXPECT_FALSE(loaded.mainLayout().sidebar.collapsed);
    EXPECT_FALSE(loaded.mainLayout().auxiliarySidebar.collapsed);
    EXPECT_TRUE(loaded.mainLayout().bottomDock.collapsed);
    EXPECT_TRUE(loaded.mainLayout().outlinerVisible);
    EXPECT_FALSE(loaded.mainLayout().inspectorVisible);
    ASSERT_TRUE(dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelOutliner));
    ASSERT_TRUE(dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelInspector));
    EXPECT_EQ(*dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelOutliner), DockLocation::Sidebar);
    EXPECT_EQ(*dockPanelLocation(loaded.mainLayout().dockLayout, kMainLayoutPanelInspector), DockLocation::Panel);
    ASSERT_TRUE(dockActivePanelId(loaded.mainLayout().dockLayout, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(loaded.mainLayout().dockLayout, DockLocation::Panel), kMainLayoutPanelInspector);

    std::filesystem::remove(path);
}

TEST(UISettingsTests, MigratesLegacyDisplayPanelNamesOnlyWhenLoadingSettings) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_legacy_panel_labels.ini";
    {
        std::ofstream file(path, std::ios::trunc);
        ASSERT_TRUE(file.is_open());
        file << "[MainLayout]\n";
        file << "DockPanelOrder=Inspector,Outliner,Viewport\n";
        file << "DockActivePanels=Panel:Inspector,Sidebar:Outliner\n";
        file << "DockSplitPanels=Panel:Inspector=Inspector@268|Outliner\n";
        file << "InspectorDockLocation=Panel\n";
        file << "OutlinerDockLocation=Panel\n";
    }

    UISettings loaded = UISettings::load(path);

    ASSERT_GE(loaded.mainLayout().dockLayout.panels.size(), 2u);
    EXPECT_EQ(loaded.mainLayout().dockLayout.panels[0].panelId, kMainLayoutPanelInspector);
    EXPECT_EQ(loaded.mainLayout().dockLayout.panels[1].panelId, kMainLayoutPanelOutliner);
    ASSERT_TRUE(dockActivePanelId(loaded.mainLayout().dockLayout, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(loaded.mainLayout().dockLayout, DockLocation::Panel), kMainLayoutPanelInspector);
    const std::vector<std::string_view> splitPanels =
            dockSplitPanelsInLocation(loaded.mainLayout().dockLayout, DockLocation::Panel);
    ASSERT_EQ(splitPanels.size(), 2u);
    EXPECT_EQ(splitPanels[0], kMainLayoutPanelInspector);
    EXPECT_EQ(splitPanels[1], kMainLayoutPanelOutliner);
    ASSERT_TRUE(dockSplitPanelSizeDp(
            loaded.mainLayout().dockLayout,
            DockLocation::Panel,
            kMainLayoutPanelInspector));
    EXPECT_EQ(
            *dockSplitPanelSizeDp(
                    loaded.mainLayout().dockLayout,
                    DockLocation::Panel,
                    kMainLayoutPanelInspector),
            268);

    ASSERT_TRUE(loaded.save(path));
    const std::string savedText = readSourceFile(path);
    EXPECT_NE(savedText.find("DockPanelOrder=inspector,outliner"), std::string::npos);
    EXPECT_NE(savedText.find("DockActivePanels=Panel:inspector"), std::string::npos);
    EXPECT_NE(savedText.find("DockSplitPanels=Panel:inspector=inspector@268|outliner"), std::string::npos);
    EXPECT_EQ(savedText.find("DockPanelOrder=Inspector"), std::string::npos);
    EXPECT_EQ(savedText.find("DockPanelOrder=Outliner"), std::string::npos);
    EXPECT_EQ(savedText.find("Panel:Inspector"), std::string::npos);
    EXPECT_EQ(savedText.find("Sidebar:Outliner"), std::string::npos);
    EXPECT_EQ(savedText.find("=Inspector@"), std::string::npos);
    EXPECT_EQ(savedText.find("|Outliner"), std::string::npos);

    std::filesystem::remove(path);
}

TEST(UITabStripModelTests, NormalizesItemsAndKeepsActiveItemValid) {
    UITabStripModel tabs = UITabStripModel::fromItems({"outliner", "inspector", "outliner"}, "inspector");

    ASSERT_EQ(tabs.itemIds().size(), 2u);
    EXPECT_EQ(tabs.itemIds()[0], "outliner");
    EXPECT_EQ(tabs.itemIds()[1], "inspector");
    ASSERT_TRUE(tabs.activeItemId());
    EXPECT_EQ(*tabs.activeItemId(), "inspector");
    EXPECT_TRUE(tabs.contains("outliner"));
    EXPECT_FALSE(tabs.contains("console"));
    EXPECT_TRUE(tabs.activateItem("outliner"));
    ASSERT_TRUE(tabs.activeItemId());
    EXPECT_EQ(*tabs.activeItemId(), "outliner");
    EXPECT_FALSE(tabs.activateItem("missing"));
    EXPECT_FALSE(tabs.activateItem("outliner"));

    EXPECT_TRUE(tabs.setItems({"console", "problems"}, "missing"));
    ASSERT_TRUE(tabs.activeItemId());
    EXPECT_EQ(*tabs.activeItemId(), "console");
    EXPECT_TRUE(tabs.setItems({}, "console"));
    EXPECT_TRUE(tabs.empty());
    EXPECT_FALSE(tabs.activeItemId());
}

TEST(DockLayoutModelTests, NormalizesKnownPanelsAndMovesBetweenLocations) {
    DockLayoutModel model;
    model.panels = {
            {.panelId = std::string(kMainLayoutPanelInspector), .location = DockLocation::Panel},
            {.panelId = "stale", .location = DockLocation::AuxiliaryBar},
    };
    model.activePanels = {
            {.location = DockLocation::Panel, .panelId = "stale"},
            {.location = DockLocation::AuxiliaryBar, .panelId = "stale"},
    };

    model = normalizedDockLayoutModel(
            std::move(model),
            mainLayoutDockablePanelIds(),
            DockLocation::Sidebar);

    EXPECT_EQ(model.panels.size(), 2u);
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Panel), kMainLayoutPanelInspector);
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Sidebar));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Sidebar), kMainLayoutPanelOutliner);
    EXPECT_FALSE(dockActivePanelId(model, DockLocation::AuxiliaryBar));
    ASSERT_TRUE(dockPanelLocation(model, kMainLayoutPanelOutliner));
    ASSERT_TRUE(dockPanelLocation(model, kMainLayoutPanelInspector));
    EXPECT_EQ(*dockPanelLocation(model, kMainLayoutPanelOutliner), DockLocation::Sidebar);
    EXPECT_EQ(*dockPanelLocation(model, kMainLayoutPanelInspector), DockLocation::Panel);
    EXPECT_FALSE(dockPanelLocation(model, kMainLayoutPanelViewport));
    EXPECT_TRUE(dockPanelIdIsKnown(model, kMainLayoutPanelOutliner));
    EXPECT_FALSE(dockPanelIdIsKnown(model, "console"));
    EXPECT_EQ(dockLocationName(DockLocation::AuxiliaryBar), "AuxiliaryBar");
    ASSERT_TRUE(dockLocationFromName("Panel"));
    EXPECT_EQ(*dockLocationFromName("Panel"), DockLocation::Panel);
    EXPECT_FALSE(dockLocationFromName("Bottom"));

    const std::vector<std::string_view> sidebarPanels = dockPanelsInLocation(model, DockLocation::Sidebar);
    ASSERT_EQ(sidebarPanels.size(), 1u);
    EXPECT_EQ(sidebarPanels.front(), kMainLayoutPanelOutliner);

    EXPECT_TRUE(dockMovePanelToLocation(model, kMainLayoutPanelOutliner, DockLocation::Panel));
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Panel), kMainLayoutPanelOutliner);
    EXPECT_FALSE(dockActivePanelId(model, DockLocation::Sidebar));
    const std::vector<std::string_view> panelPanels = dockPanelsInLocation(model, DockLocation::Panel);
    ASSERT_EQ(panelPanels.size(), 2u);
    EXPECT_EQ(panelPanels[0], kMainLayoutPanelInspector);
    EXPECT_EQ(panelPanels[1], kMainLayoutPanelOutliner);
    EXPECT_TRUE(dockMovePanelToLocationAtPanel(
            model,
            kMainLayoutPanelOutliner,
            DockLocation::Panel,
            kMainLayoutPanelInspector,
            DockPanelInsertPosition::Before));
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Panel), kMainLayoutPanelOutliner);
    const std::vector<std::string_view> reorderedPanelPanels = dockPanelsInLocation(model, DockLocation::Panel);
    ASSERT_EQ(reorderedPanelPanels.size(), 2u);
    EXPECT_EQ(reorderedPanelPanels[0], kMainLayoutPanelOutliner);
    EXPECT_EQ(reorderedPanelPanels[1], kMainLayoutPanelInspector);
    EXPECT_TRUE(dockSetActivePanel(model, DockLocation::Panel, kMainLayoutPanelInspector));
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Panel), kMainLayoutPanelInspector);
    EXPECT_FALSE(dockMovePanelToLocationAtPanel(
            model,
            kMainLayoutPanelOutliner,
            DockLocation::Panel,
            kMainLayoutPanelInspector,
            DockPanelInsertPosition::Before));
    EXPECT_TRUE(dockMovePanelToLocation(model, kMainLayoutPanelOutliner, DockLocation::AuxiliaryBar));
    ASSERT_TRUE(dockPanelLocation(model, kMainLayoutPanelOutliner));
    EXPECT_EQ(*dockPanelLocation(model, kMainLayoutPanelOutliner), DockLocation::AuxiliaryBar);
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::AuxiliaryBar));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::AuxiliaryBar), kMainLayoutPanelOutliner);
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Panel), kMainLayoutPanelInspector);
    EXPECT_FALSE(dockSetActivePanel(model, DockLocation::Panel, kMainLayoutPanelInspector));
    EXPECT_FALSE(dockSetActivePanel(model, DockLocation::Panel, kMainLayoutPanelOutliner));
    const UITabStripModel panelTabs = dockTabStripForLocation(model, DockLocation::Panel);
    ASSERT_TRUE(panelTabs.activeItemId());
    EXPECT_EQ(*panelTabs.activeItemId(), kMainLayoutPanelInspector);
    const std::vector<std::string_view> singlePanelBody = dockSplitPanelsInLocation(model, DockLocation::Panel);
    ASSERT_EQ(singlePanelBody.size(), 1u);
    EXPECT_EQ(singlePanelBody.front(), kMainLayoutPanelInspector);
    EXPECT_TRUE(dockSplitPanelWithPanel(
            model,
            kMainLayoutPanelOutliner,
            DockLocation::Panel,
            kMainLayoutPanelInspector,
            DockPanelInsertPosition::After));
    const std::vector<std::string_view> splitPanelBody = dockSplitPanelsInLocation(model, DockLocation::Panel);
    ASSERT_EQ(splitPanelBody.size(), 2u);
    EXPECT_EQ(splitPanelBody[0], kMainLayoutPanelInspector);
    EXPECT_EQ(splitPanelBody[1], kMainLayoutPanelOutliner);
    const std::vector<std::string_view> splitPanelTabs =
            dockTabPanelsInLocation(model, DockLocation::Panel);
    ASSERT_EQ(splitPanelTabs.size(), 1u);
    EXPECT_EQ(splitPanelTabs[0], kMainLayoutPanelInspector);
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Panel), kMainLayoutPanelInspector);
    EXPECT_FALSE(dockSetActivePanel(model, DockLocation::Panel, kMainLayoutPanelOutliner));
    EXPECT_FALSE(dockSplitPanelSizeDp(model, DockLocation::Panel, kMainLayoutPanelInspector));
    EXPECT_TRUE(dockSetSplitPanelSizeDp(model, DockLocation::Panel, kMainLayoutPanelInspector, 284));
    ASSERT_TRUE(dockSplitPanelSizeDp(model, DockLocation::Panel, kMainLayoutPanelInspector));
    EXPECT_EQ(*dockSplitPanelSizeDp(model, DockLocation::Panel, kMainLayoutPanelInspector), 284);
    EXPECT_FALSE(dockSetSplitPanelSizeDp(model, DockLocation::Panel, kMainLayoutPanelInspector, 284));
    EXPECT_TRUE(dockSetSplitPanels(
            model,
            DockLocation::Panel,
            {kMainLayoutPanelOutliner, kMainLayoutPanelInspector}));
    const std::vector<std::string_view> retabbedSplitPanelTabs =
            dockTabPanelsInLocation(model, DockLocation::Panel);
    ASSERT_EQ(retabbedSplitPanelTabs.size(), 1u);
    EXPECT_EQ(retabbedSplitPanelTabs[0], kMainLayoutPanelOutliner);
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Panel), kMainLayoutPanelOutliner);
    ASSERT_TRUE(dockSplitPanelSizeDp(model, DockLocation::Panel, kMainLayoutPanelInspector));
    EXPECT_EQ(*dockSplitPanelSizeDp(model, DockLocation::Panel, kMainLayoutPanelInspector), 284);
    EXPECT_FALSE(dockSplitPanelSizeDp(model, DockLocation::Panel, kMainLayoutPanelOutliner));
    EXPECT_TRUE(dockMovePanelToLocationAtPanel(
            model,
            kMainLayoutPanelInspector,
            DockLocation::Panel,
            kMainLayoutPanelOutliner,
            DockPanelInsertPosition::After));
    const std::vector<std::string_view> detachedPanelTabs =
            dockTabPanelsInLocation(model, DockLocation::Panel);
    ASSERT_EQ(detachedPanelTabs.size(), 2u);
    EXPECT_EQ(detachedPanelTabs[0], kMainLayoutPanelOutliner);
    EXPECT_EQ(detachedPanelTabs[1], kMainLayoutPanelInspector);
    EXPECT_TRUE(model.splitPanels.empty());
    ASSERT_TRUE(dockActivePanelId(model, DockLocation::Panel));
    EXPECT_EQ(*dockActivePanelId(model, DockLocation::Panel), kMainLayoutPanelInspector);
    EXPECT_FALSE(dockMovePanelToLocation(model, "console", DockLocation::Panel));
}

TEST(MainLayoutModelTests, DefaultLayoutContainsViewportAndRightSidebarPanels) {
    const MainLayoutModel layout = MainLayoutModel::defaults();

    EXPECT_EQ(layout.sidebarSide, MainLayoutSidebarSide::Right);
    EXPECT_EQ(layout.sidebar.widthDp, kMainLayoutSidebarDefaultWidthDp);
    EXPECT_EQ(layout.auxiliarySidebar.widthDp, kMainLayoutSidebarDefaultWidthDp);
    EXPECT_EQ(layout.outlinerHeightDp, kMainLayoutOutlinerDefaultHeightDp);
    EXPECT_EQ(layout.bottomDock.heightDp, kMainLayoutBottomDockDefaultHeightDp);
    EXPECT_FALSE(layout.sidebar.collapsed);
    EXPECT_FALSE(layout.auxiliarySidebar.collapsed);
    EXPECT_FALSE(layout.bottomDock.collapsed);
    EXPECT_TRUE(layout.outlinerVisible);
    EXPECT_TRUE(layout.inspectorVisible);
    ASSERT_TRUE(dockPanelLocation(layout.dockLayout, kMainLayoutPanelOutliner));
    ASSERT_TRUE(dockPanelLocation(layout.dockLayout, kMainLayoutPanelInspector));
    EXPECT_EQ(*dockPanelLocation(layout.dockLayout, kMainLayoutPanelOutliner), DockLocation::Sidebar);
    EXPECT_EQ(*dockPanelLocation(layout.dockLayout, kMainLayoutPanelInspector), DockLocation::Sidebar);
    EXPECT_TRUE(mainLayoutPanelIdIsViewport(kMainLayoutPanelViewport));
    EXPECT_TRUE(mainLayoutPanelIdIsKnown(kMainLayoutPanelViewport));
    EXPECT_TRUE(mainLayoutPanelIdIsKnown(kMainLayoutPanelOutliner));
    EXPECT_TRUE(mainLayoutPanelIdIsKnown(kMainLayoutPanelInspector));
    EXPECT_FALSE(mainLayoutPanelIdIsViewport("Viewport"));
    EXPECT_FALSE(mainLayoutPanelIdIsKnown("Outliner"));
    EXPECT_FALSE(mainLayoutPanelIdIsKnown("Inspector"));
    EXPECT_FALSE(mainLayoutCanonicalPanelId("Outliner"));
    EXPECT_FALSE(mainLayoutCanonicalPanelId("Inspector"));
    const std::optional<std::string_view> legacyOutliner = mainLayoutPanelIdFromLegacySettings("Outliner");
    const std::optional<std::string_view> legacyInspector = mainLayoutPanelIdFromLegacySettings("Inspector");
    ASSERT_TRUE(legacyOutliner);
    ASSERT_TRUE(legacyInspector);
    EXPECT_EQ(*legacyOutliner, kMainLayoutPanelOutliner);
    EXPECT_EQ(*legacyInspector, kMainLayoutPanelInspector);
    EXPECT_FALSE(mainLayoutPanelIdIsKnown("Console"));
    EXPECT_EQ(mainLayoutOppositeSidebarSide(MainLayoutSidebarSide::Right), MainLayoutSidebarSide::Left);
    ASSERT_TRUE(mainLayoutSidebarSideForDockLocation(layout, DockLocation::Sidebar));
    EXPECT_EQ(*mainLayoutSidebarSideForDockLocation(layout, DockLocation::Sidebar), MainLayoutSidebarSide::Right);
    ASSERT_TRUE(mainLayoutSidebarSideForDockLocation(layout, DockLocation::AuxiliaryBar));
    EXPECT_EQ(*mainLayoutSidebarSideForDockLocation(layout, DockLocation::AuxiliaryBar), MainLayoutSidebarSide::Left);
    ASSERT_TRUE(mainLayoutDockLocationForSidebarSide(layout, MainLayoutSidebarSide::Left));
    EXPECT_EQ(*mainLayoutDockLocationForSidebarSide(layout, MainLayoutSidebarSide::Left), DockLocation::AuxiliaryBar);
    ASSERT_TRUE(mainLayoutDockLocationForSidebarSide(layout, MainLayoutSidebarSide::Right));
    EXPECT_EQ(*mainLayoutDockLocationForSidebarSide(layout, MainLayoutSidebarSide::Right), DockLocation::Sidebar);
    EXPECT_TRUE(mainLayoutLocationIsSideDock(DockLocation::Sidebar));
    EXPECT_TRUE(mainLayoutLocationIsSideDock(DockLocation::AuxiliaryBar));
    EXPECT_FALSE(mainLayoutLocationIsSideDock(DockLocation::Panel));
    EXPECT_EQ(mainLayoutSideDockWidthDp(layout, DockLocation::Sidebar), kMainLayoutSidebarDefaultWidthDp);
    EXPECT_EQ(mainLayoutSideDockWidthDp(layout, DockLocation::AuxiliaryBar), kMainLayoutSidebarDefaultWidthDp);
    EXPECT_FALSE(mainLayoutSideDockCollapsed(layout, DockLocation::Sidebar));
    EXPECT_FALSE(mainLayoutSideDockCollapsed(layout, DockLocation::AuxiliaryBar));
    EXPECT_EQ(mainLayoutBottomDockHeightDp(layout), kMainLayoutBottomDockDefaultHeightDp);
    EXPECT_FALSE(mainLayoutBottomDockCollapsed(layout));
}

TEST(MainLayoutControllerTests, ResizesSidebarAndPanelSplitWithClamps) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    MainLayoutController controller(layout);

    EXPECT_TRUE(controller.resizeSidebar(12));
    EXPECT_EQ(layout.sidebar.widthDp, kMainLayoutSidebarMinWidthDp);
    EXPECT_TRUE(controller.resizeSidebar(800));
    EXPECT_EQ(layout.sidebar.widthDp, 800);
    EXPECT_TRUE(controller.resizeSidebar(280));
    EXPECT_EQ(layout.sidebar.widthDp, 280);
    EXPECT_TRUE(controller.resizeSidebar(DockLocation::AuxiliaryBar, 290));
    EXPECT_EQ(layout.auxiliarySidebar.widthDp, 290);
    EXPECT_FALSE(layout.sidebar.collapsed);
    EXPECT_FALSE(layout.auxiliarySidebar.collapsed);
    EXPECT_FALSE(controller.resizeSidebar(DockLocation::AuxiliaryBar, 290));

    EXPECT_TRUE(controller.setSidebarCollapsed(true));
    EXPECT_TRUE(layout.sidebar.collapsed);
    EXPECT_TRUE(controller.resizeSidebar(300));
    EXPECT_EQ(layout.sidebar.widthDp, 300);
    EXPECT_FALSE(layout.sidebar.collapsed);
    EXPECT_TRUE(controller.setSidebarCollapsed(DockLocation::AuxiliaryBar, true));
    EXPECT_TRUE(layout.auxiliarySidebar.collapsed);
    EXPECT_TRUE(controller.resizeSidebar(DockLocation::AuxiliaryBar, 310));
    EXPECT_EQ(layout.auxiliarySidebar.widthDp, 310);
    EXPECT_FALSE(layout.auxiliarySidebar.collapsed);
    EXPECT_FALSE(controller.setSidebarCollapsed(DockLocation::Panel, true));
    EXPECT_FALSE(controller.resizeSidebar(DockLocation::Panel, 300));

    EXPECT_TRUE(controller.resizeOutlinerPanel(12));
    EXPECT_EQ(layout.outlinerHeightDp, kMainLayoutPanelMinHeightDp);
    EXPECT_TRUE(controller.resizeOutlinerPanel(10000));
    EXPECT_EQ(layout.outlinerHeightDp, kMainLayoutPanelMaxHeightDp);
    EXPECT_TRUE(controller.resizeOutlinerPanel(260));
    EXPECT_EQ(layout.outlinerHeightDp, 260);

    EXPECT_TRUE(controller.resizeBottomDock(12));
    EXPECT_EQ(layout.bottomDock.heightDp, kMainLayoutPanelMinHeightDp);
    EXPECT_TRUE(controller.resizeBottomDock(10000));
    EXPECT_EQ(layout.bottomDock.heightDp, kMainLayoutPanelMaxHeightDp);
    EXPECT_TRUE(controller.setBottomDockCollapsed(true));
    EXPECT_TRUE(layout.bottomDock.collapsed);
    EXPECT_TRUE(controller.resizeBottomDock(288));
    EXPECT_EQ(layout.bottomDock.heightDp, 288);
    EXPECT_FALSE(layout.bottomDock.collapsed);
    EXPECT_TRUE(controller.setBottomDockCollapsed(true));
    EXPECT_FALSE(controller.setBottomDockCollapsed(true));
}

TEST(MainLayoutControllerTests, TogglesFixedPanelsAndPreparesSidebarFlip) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    MainLayoutController controller(layout);

    EXPECT_TRUE(controller.setSidebarSide(MainLayoutSidebarSide::Left));
    EXPECT_EQ(layout.sidebarSide, MainLayoutSidebarSide::Left);
    EXPECT_FALSE(controller.setSidebarSide(MainLayoutSidebarSide::Left));

    EXPECT_FALSE(controller.panelDockLocation("Inspector"));
    EXPECT_TRUE(controller.movePanelToDockLocation(
            kMainLayoutPanelInspector,
            DockLocation::Sidebar,
            kMainLayoutPanelOutliner,
            DockPanelInsertPosition::Before));
    const std::vector<std::string_view> sidebarPanels = dockPanelsInLocation(layout.dockLayout, DockLocation::Sidebar);
    ASSERT_EQ(sidebarPanels.size(), 2u);
    EXPECT_EQ(sidebarPanels[0], kMainLayoutPanelInspector);
    EXPECT_EQ(sidebarPanels[1], kMainLayoutPanelOutliner);
    ASSERT_TRUE(controller.activeDockPanel(DockLocation::Sidebar));
    EXPECT_EQ(*controller.activeDockPanel(DockLocation::Sidebar), kMainLayoutPanelInspector);
    EXPECT_TRUE(controller.setActiveDockPanel(DockLocation::Sidebar, kMainLayoutPanelOutliner));
    ASSERT_TRUE(controller.activeDockPanel(DockLocation::Sidebar));
    EXPECT_EQ(*controller.activeDockPanel(DockLocation::Sidebar), kMainLayoutPanelOutliner);
    EXPECT_FALSE(controller.setActiveDockPanel(DockLocation::Panel, kMainLayoutPanelOutliner));
    EXPECT_FALSE(controller.movePanelToDockLocation(
            kMainLayoutPanelInspector,
            DockLocation::Sidebar,
            kMainLayoutPanelOutliner,
            DockPanelInsertPosition::Before));
    EXPECT_FALSE(controller.movePanelToDockLocation("Inspector", DockLocation::Panel));
    EXPECT_TRUE(controller.movePanelToDockLocation(kMainLayoutPanelInspector, DockLocation::Panel));
    ASSERT_TRUE(controller.panelDockLocation(kMainLayoutPanelInspector));
    EXPECT_EQ(*controller.panelDockLocation(kMainLayoutPanelInspector), DockLocation::Panel);
    ASSERT_TRUE(controller.activeDockPanel(DockLocation::Panel));
    EXPECT_EQ(*controller.activeDockPanel(DockLocation::Panel), kMainLayoutPanelInspector);
    EXPECT_FALSE(controller.movePanelToDockLocation(kMainLayoutPanelInspector, DockLocation::Panel));
    EXPECT_TRUE(controller.splitPanelWithDockPanel(
            kMainLayoutPanelOutliner,
            DockLocation::Panel,
            kMainLayoutPanelInspector,
            DockPanelInsertPosition::Before));
    const std::vector<std::string_view> splitPanelBodies =
            dockSplitPanelsInLocation(layout.dockLayout, DockLocation::Panel);
    ASSERT_EQ(splitPanelBodies.size(), 2u);
    EXPECT_EQ(splitPanelBodies[0], kMainLayoutPanelOutliner);
    EXPECT_EQ(splitPanelBodies[1], kMainLayoutPanelInspector);
    const std::vector<std::string_view> bottomPanelTabs =
            dockTabPanelsInLocation(layout.dockLayout, DockLocation::Panel);
    ASSERT_EQ(bottomPanelTabs.size(), 1u);
    EXPECT_EQ(bottomPanelTabs[0], kMainLayoutPanelInspector);
    ASSERT_TRUE(controller.activeDockPanel(DockLocation::Panel));
    EXPECT_EQ(*controller.activeDockPanel(DockLocation::Panel), kMainLayoutPanelInspector);
    EXPECT_FALSE(controller.setActiveDockPanel(DockLocation::Panel, kMainLayoutPanelOutliner));
    EXPECT_TRUE(controller.resizeBottomSplitPanel(kMainLayoutPanelOutliner, 24));
    ASSERT_TRUE(dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner));
    EXPECT_EQ(
            *dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner),
            kMainLayoutBottomSplitPanelMinWidthDp);
    EXPECT_TRUE(controller.resizeBottomSplitPanel(kMainLayoutPanelOutliner, 332));
    ASSERT_TRUE(dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner));
    EXPECT_EQ(*dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner), 332);
    EXPECT_FALSE(controller.resizeBottomSplitPanel(kMainLayoutPanelInspector, 240));
    EXPECT_FALSE(controller.resizeBottomSplitPanel(kMainLayoutPanelViewport, 240));
    EXPECT_TRUE(controller.movePanelToDockLocation(kMainLayoutPanelInspector, DockLocation::AuxiliaryBar));
    ASSERT_TRUE(controller.panelDockLocation(kMainLayoutPanelInspector));
    EXPECT_EQ(*controller.panelDockLocation(kMainLayoutPanelInspector), DockLocation::AuxiliaryBar);
    EXPECT_TRUE(layout.dockLayout.splitPanels.empty());
    ASSERT_TRUE(controller.activeDockPanel(DockLocation::Panel));
    EXPECT_EQ(*controller.activeDockPanel(DockLocation::Panel), kMainLayoutPanelOutliner);
    EXPECT_FALSE(controller.movePanelToDockLocation(kMainLayoutPanelViewport, DockLocation::Panel));
    EXPECT_FALSE(controller.movePanelToDockLocation("Console", DockLocation::Panel));
    EXPECT_FALSE(controller.panelDockLocation(kMainLayoutPanelViewport));
    EXPECT_FALSE(controller.panelDockLocation("Console"));

    EXPECT_TRUE(controller.setPanelVisible(kMainLayoutPanelOutliner, false));
    EXPECT_FALSE(controller.isPanelVisible(kMainLayoutPanelOutliner));
    EXPECT_TRUE(controller.setPanelVisible(kMainLayoutPanelInspector, false));
    EXPECT_FALSE(controller.isPanelVisible(kMainLayoutPanelInspector));
    EXPECT_FALSE(controller.setPanelVisible("Console", true));
}

TEST(MainLayoutSashRegistryTests, BottomSplitUsesTrackWidthWhenMeasuredPaneIsTiny) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelOutliner, DockLocation::Panel));
    ASSERT_TRUE(dockSplitPanelWithPanel(
            layout.dockLayout,
            kMainLayoutPanelInspector,
            DockLocation::Panel,
            kMainLayoutPanelOutliner,
            DockPanelInsertPosition::After));
    ASSERT_FALSE(dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner));

    MainLayoutSashRegistry registry;
    registry.setMeasuredLayout({
            .mainWidthDp = 900,
            .mainHeightDp = 620,
            .sideDockHeightDp = 480,
    });
    registry.addBottomPanelSplitSash(
            layout,
            MainLayoutBottomSplitPaneMeasurement{
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .widthDp = 1,
                    .availableWidthDp = 1,
                    .trackWidthDp = 640,
                    .splitIndex = 0,
                    .splitCount = 2,
            },
            UIPixelRect{.left = 320, .top = 200, .width = 12, .height = 220});

    const std::string handleId =
            MainLayoutSashRegistry::bottomPanelSplitHandleId(kMainLayoutPanelOutliner);
    const std::optional<ResizableSplitHandle> handle = registry.handle(handleId);
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(handle->id, handleId);
    EXPECT_EQ(handle->axis, ResizableSplitAxis::Horizontal);
    EXPECT_EQ(handle->growth, ResizableSplitGrowthDirection::Positive);
    EXPECT_EQ(handle->sizeDp, 320);
    EXPECT_EQ(handle->containerSizeDp, 640);
    ASSERT_TRUE(handle->oppositeMinSize.has_value());
    EXPECT_EQ(handle->oppositeMinSize->unit, ResizableSplitSizeUnit::Dp);
    EXPECT_EQ(handle->oppositeMinSize->value, static_cast<float>(kMainLayoutBottomSplitPanelMinWidthDp));

    const std::vector<ResizableSplitHitRegion> hitRegions = registry.hitRegions();
    ASSERT_EQ(hitRegions.size(), 1u);
    EXPECT_EQ(hitRegions[0].id, handleId);
    EXPECT_EQ(hitRegions[0].axis, ResizableSplitAxis::Horizontal);

    MainLayoutController controller(layout);
    EXPECT_TRUE(registry.applyResize(
            layout,
            controller,
            ResizableSplitResizeResult{
                    .id = handleId,
                    .axis = ResizableSplitAxis::Horizontal,
                    .sizeDp = 360,
                    .collapsed = false,
            }));
    ASSERT_TRUE(dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner));
    EXPECT_EQ(*dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner), 360);
}

TEST(MainLayoutSashRegistryTests, BottomSplitCanGrowWhenRememberedPaneAndContentMeasureAtMinimum) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelOutliner, DockLocation::Panel));
    ASSERT_TRUE(dockSplitPanelWithPanel(
            layout.dockLayout,
            kMainLayoutPanelInspector,
            DockLocation::Panel,
            kMainLayoutPanelOutliner,
            DockPanelInsertPosition::After));
    MainLayoutController controller(layout);
    ASSERT_TRUE(controller.resizeBottomSplitPanel(
            kMainLayoutPanelOutliner,
            kMainLayoutBottomSplitPanelMinWidthDp));

    MainLayoutSashRegistry registry;
    registry.setMeasuredLayout({
            .mainWidthDp = 1400,
            .mainHeightDp = 620,
            .sideDockHeightDp = 480,
            .bottomDockTrackWidthDp = 1400,
    });
    registry.addBottomPanelSplitSash(
            layout,
            MainLayoutBottomSplitPaneMeasurement{
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .widthDp = kMainLayoutBottomSplitPanelMinWidthDp,
                    .availableWidthDp = kMainLayoutBottomSplitPanelMinWidthDp,
                    .trackWidthDp = 1400,
                    .splitIndex = 0,
                    .splitCount = 2,
            },
            UIPixelRect{.left = 120, .top = 200, .width = 12, .height = 220});

    const std::string handleId =
            MainLayoutSashRegistry::bottomPanelSplitHandleId(kMainLayoutPanelOutliner);
    const std::optional<ResizableSplitHandle> handle = registry.handle(handleId);
    ASSERT_TRUE(handle.has_value());
    EXPECT_EQ(handle->sizeDp, kMainLayoutBottomSplitPanelMinWidthDp);
    EXPECT_EQ(handle->containerSizeDp, 1400);

    ResizableSplitController splitController;
    splitController.beginDrag(*handle, 120.0f, 0.0f, 1.0f);
    const std::optional<ResizableSplitResizeResult> resize =
            splitController.updateDrag(520.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 520);
    EXPECT_TRUE(registry.applyResize(layout, controller, *resize));
    ASSERT_TRUE(dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner));
    EXPECT_EQ(*dockSplitPanelSizeDp(layout.dockLayout, DockLocation::Panel, kMainLayoutPanelOutliner), 520);
}

TEST(MainLayoutSashRegistryTests, SidebarsAndBottomDockSnapCollapseThroughMainLayoutSashes) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    MainLayoutController controller(layout);

    MainLayoutSashRegistry registry;
    registry.setMeasuredLayout({
            .mainWidthDp = 1000,
            .mainHeightDp = 700,
            .sideDockHeightDp = 700,
            .bottomDockTrackWidthDp = 760,
    });
    registry.addSideDockWidthSash(
            layout,
            DockLocation::Sidebar,
            MainLayoutSidebarSide::Right,
            UIPixelRect{.left = 760, .top = 0, .width = 12, .height = 700});

    const std::optional<ResizableSplitHandle> sidebarHandle =
            registry.handle(MainLayoutSashRegistry::sideDockWidthHandleId(DockLocation::Sidebar));
    ASSERT_TRUE(sidebarHandle.has_value());
    ResizableSplitController splitController;
    splitController.beginDrag(*sidebarHandle, 0.0f, 0.0f, 1.0f);
    std::optional<ResizableSplitResizeResult> resize = splitController.updateDrag(100.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_FALSE(resize->collapsed);
    EXPECT_EQ(resize->sizeDp, kMainLayoutSidebarMinWidthDp);
    EXPECT_TRUE(registry.applyResize(layout, controller, *resize));
    EXPECT_FALSE(layout.sidebar.collapsed);

    resize = splitController.updateDrag(190.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_TRUE(resize->collapsed);
    EXPECT_TRUE(registry.applyResize(layout, controller, *resize));
    EXPECT_TRUE(layout.sidebar.collapsed);

    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelInspector, DockLocation::Panel));
    MainLayoutSashRegistry bottomRegistry;
    bottomRegistry.setMeasuredLayout({
            .mainWidthDp = 1000,
            .mainHeightDp = 700,
            .sideDockHeightDp = 700,
            .bottomDockTrackWidthDp = 760,
    });
    bottomRegistry.addBottomDockHeightSash(
            layout,
            UIPixelRect{.left = 0, .top = 460, .width = 760, .height = 12});

    const std::optional<ResizableSplitHandle> bottomDockHandle =
            bottomRegistry.handle(MainLayoutSashRegistry::bottomDockHeightHandleId());
    ASSERT_TRUE(bottomDockHandle.has_value());
    splitController.beginDrag(*bottomDockHandle, 0.0f, 0.0f, 1.0f);
    resize = splitController.updateDrag(0.0f, 140.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_FALSE(resize->collapsed);
    EXPECT_EQ(resize->sizeDp, kMainLayoutPanelMinHeightDp);
    EXPECT_TRUE(bottomRegistry.applyResize(layout, controller, *resize));
    EXPECT_FALSE(layout.bottomDock.collapsed);

    resize = splitController.updateDrag(0.0f, 190.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_TRUE(resize->collapsed);
    EXPECT_TRUE(bottomRegistry.applyResize(layout, controller, *resize));
    EXPECT_TRUE(layout.bottomDock.collapsed);
}

TEST(UIDragDropControllerTests, PromotesCandidateAfterThresholdAndDropsWholeRectTarget) {
    UIDragDropController controller;
    controller.setSources({
            {
                    .sourceId = "outliner-source",
                    .item = {
                            .kind = UIDragDropItemKind::DockPanel,
                            .stableId = "outliner",
                            .itemIds = {"outliner"},
                    },
                    .rect = {.left = 10, .top = 10, .width = 80, .height = 30},
            },
    });
    controller.setDropZones({
            {
                    .id = {.value = 7},
                    .kind = UIDropZoneKind::DockSidebar,
                    .rect = {.left = 120, .top = 0, .width = 60, .height = 120},
                    .accepts = {UIDragDropItemKind::DockPanel},
                    .rejectSameItem = false,
                    .feedbackKind = UIDropFeedbackKind::Edge,
            },
    });

    EXPECT_FALSE(controller.beginCandidate({.x = 0.0f, .y = 0.0f}));
    ASSERT_TRUE(controller.beginCandidate({.x = 20.0f, .y = 20.0f}));
    EXPECT_EQ(controller.phase(), UIDragDropPhase::Candidate);
    EXPECT_TRUE(controller.active());
    EXPECT_FALSE(controller.dragging());
    ASSERT_TRUE(controller.activeItem().has_value());
    EXPECT_EQ(controller.activeItem()->stableId, "outliner");
    EXPECT_FALSE(controller.update({.x = 22.0f, .y = 20.0f}).has_value());
    EXPECT_EQ(controller.phase(), UIDragDropPhase::Candidate);

    const std::optional<UIDragDropTarget> target = controller.update({.x = 130.0f, .y = 20.0f});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->zoneId.value, 7u);
    EXPECT_EQ(target->kind, UIDropZoneKind::DockSidebar);
    EXPECT_EQ(target->placement, UIDropPlacement::On);
    EXPECT_EQ(target->feedbackKind, UIDropFeedbackKind::Edge);
    EXPECT_TRUE(controller.dragging());

    const std::optional<UIDragDropResult> drop = controller.end({.x = 130.0f, .y = 20.0f});
    ASSERT_TRUE(drop.has_value());
    EXPECT_EQ(drop->item.stableId, "outliner");
    EXPECT_EQ(drop->target.zoneId.value, 7u);
    EXPECT_EQ(controller.phase(), UIDragDropPhase::Idle);
    EXPECT_FALSE(controller.active());
    EXPECT_FALSE(controller.target().has_value());
}

TEST(UIDragDropControllerTests, UsesLatestZonesDuringActiveDrag) {
    UIDragDropController controller;
    controller.setSources({
            {
                    .sourceId = "asset-source",
                    .item = {
                            .kind = UIDragDropItemKind::Asset,
                            .stableId = "mat-a",
                            .itemIds = {"mat-a"},
                    },
                    .rect = {.left = 0, .top = 0, .width = 40, .height = 40},
            },
    });

    ASSERT_TRUE(controller.beginCandidate({.x = 10.0f, .y = 10.0f}));
    EXPECT_FALSE(controller.update({.x = 100.0f, .y = 10.0f}).has_value());
    EXPECT_TRUE(controller.dragging());

    controller.setDropZones({
            {
                    .id = {.value = 2},
                    .kind = UIDropZoneKind::ViewportFrame,
                    .rect = {.left = 80, .top = 0, .width = 120, .height = 120},
                    .accepts = {UIDragDropItemKind::Asset},
                    .rejectSameItem = false,
            },
    });

    const std::optional<UIDragDropTarget> target = controller.update({.x = 100.0f, .y = 10.0f});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->zoneId.value, 2u);
    EXPECT_EQ(target->kind, UIDropZoneKind::ViewportFrame);
}

TEST(UIDragDropControllerTests, EnforcesAcceptanceSelfRejectionAndBeforeAfterPlacement) {
    UIDragDropController controller;
    controller.setSources({
            {
                    .sourceId = "entity-a-source",
                    .item = {
                            .kind = UIDragDropItemKind::OutlinerEntity,
                            .stableId = "entity-a",
                            .itemIds = {"entity-a"},
                    },
                    .rect = {.left = 0, .top = 0, .width = 40, .height = 40},
            },
    });
    controller.setDropZones({
            {
                    .id = {.value = 1},
                    .kind = UIDropZoneKind::OutlinerRow,
                    .rect = {.left = 100, .top = 0, .width = 100, .height = 100},
                    .placementPolicy = UIDropPlacementPolicy::VerticalBeforeAfter,
                    .accepts = {UIDragDropItemKind::Asset},
                    .priority = 30,
            },
            {
                    .id = {.value = 2},
                    .kind = UIDropZoneKind::OutlinerRow,
                    .rect = {.left = 100, .top = 0, .width = 100, .height = 100},
                    .placementPolicy = UIDropPlacementPolicy::VerticalBeforeAfter,
                    .accepts = {UIDragDropItemKind::OutlinerEntity},
                    .representedItem = UIDragDropItem{
                            .kind = UIDragDropItemKind::OutlinerEntity,
                            .stableId = "entity-a",
                            .itemIds = {"entity-a"},
                    },
                    .rejectSameItem = true,
                    .priority = 20,
            },
            {
                    .id = {.value = 3},
                    .kind = UIDropZoneKind::OutlinerRow,
                    .rect = {.left = 100, .top = 0, .width = 100, .height = 100},
                    .placementPolicy = UIDropPlacementPolicy::VerticalBeforeAfter,
                    .accepts = {UIDragDropItemKind::OutlinerEntity},
                    .representedItem = UIDragDropItem{
                            .kind = UIDragDropItemKind::OutlinerEntity,
                            .stableId = "entity-b",
                            .itemIds = {"entity-b"},
                    },
                    .priority = 10,
            },
    });

    ASSERT_TRUE(controller.beginCandidate({.x = 10.0f, .y = 10.0f}));
    std::optional<UIDragDropTarget> target = controller.update({.x = 120.0f, .y = 20.0f});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->zoneId.value, 3u);
    EXPECT_EQ(target->placement, UIDropPlacement::Before);

    target = controller.update({.x = 120.0f, .y = 80.0f});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->zoneId.value, 3u);
    EXPECT_EQ(target->placement, UIDropPlacement::After);
}

TEST(UIDragDropControllerTests, OverlappingZonesUsePriorityThenRegistrationOrder) {
    UIDragDropController controller;
    controller.setSources({
            {
                    .sourceId = "asset-source",
                    .item = {
                            .kind = UIDragDropItemKind::Asset,
                            .stableId = "mat-a",
                            .itemIds = {"mat-a"},
                    },
                    .rect = {.left = 0, .top = 0, .width = 40, .height = 40},
            },
    });
    controller.setDropZones({
            {
                    .id = {.value = 1},
                    .kind = UIDropZoneKind::AssetFolder,
                    .rect = {.left = 100, .top = 0, .width = 100, .height = 100},
                    .accepts = {UIDragDropItemKind::Asset},
                    .priority = 5,
            },
            {
                    .id = {.value = 2},
                    .kind = UIDropZoneKind::AssetFolder,
                    .rect = {.left = 100, .top = 0, .width = 100, .height = 100},
                    .accepts = {UIDragDropItemKind::Asset},
                    .priority = 10,
            },
            {
                    .id = {.value = 3},
                    .kind = UIDropZoneKind::AssetFolder,
                    .rect = {.left = 100, .top = 0, .width = 100, .height = 100},
                    .accepts = {UIDragDropItemKind::Asset},
                    .priority = 10,
            },
    });

    ASSERT_TRUE(controller.beginCandidate({.x = 10.0f, .y = 10.0f}));
    const std::optional<UIDragDropTarget> target = controller.update({.x = 120.0f, .y = 20.0f});

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->zoneId.value, 2u);
}

TEST(UIDragDropControllerTests, CancelClearsActiveItemAndTarget) {
    UIDragDropController controller;
    controller.setSources({
            {
                    .sourceId = "asset-source",
                    .item = {
                            .kind = UIDragDropItemKind::Asset,
                            .stableId = "mat-a",
                            .itemIds = {"mat-a"},
                    },
                    .rect = {.left = 0, .top = 0, .width = 40, .height = 40},
            },
    });
    controller.setDropZones({
            {
                    .id = {.value = 1},
                    .kind = UIDropZoneKind::ViewportFrame,
                    .rect = {.left = 80, .top = 0, .width = 120, .height = 120},
                    .accepts = {UIDragDropItemKind::Asset},
                    .rejectSameItem = false,
            },
    });

    ASSERT_TRUE(controller.beginCandidate({.x = 10.0f, .y = 10.0f}));
    ASSERT_TRUE(controller.update({.x = 100.0f, .y = 20.0f}).has_value());
    controller.cancel();

    EXPECT_EQ(controller.phase(), UIDragDropPhase::Idle);
    EXPECT_FALSE(controller.activeItem().has_value());
    EXPECT_FALSE(controller.target().has_value());
    EXPECT_FALSE(controller.update({.x = 100.0f, .y = 20.0f}).has_value());
}

TEST(MainLayoutDockDragControllerTests, DragsPanelHeaderToOppositeSidebarEdge) {
    MainLayoutDockDragController controller;
    controller.setHitRegions({
            {
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 32},
            },
    });
    controller.setDockDropZones({
            {
                    .location = DockLocation::AuxiliaryBar,
                    .rect = {.left = 0, .top = 0, .width = 64, .height = 600},
            },
    });

    EXPECT_FALSE(controller.beginDragCandidate(20.0f, 20.0f));
    ASSERT_TRUE(controller.beginDragCandidate(720.0f, 52.0f));
    EXPECT_TRUE(controller.active());
    EXPECT_FALSE(controller.dragging());
    EXPECT_EQ(controller.activePanelId(), kMainLayoutPanelOutliner);
    EXPECT_FALSE(controller.updateDrag(722.0f, 52.0f).has_value());
    EXPECT_FALSE(controller.dragging());

    const std::optional<MainLayoutDockDropTarget> target = controller.updateDrag(20.0f, 52.0f);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->location, DockLocation::AuxiliaryBar);
    EXPECT_TRUE(target->targetPanelId.empty());
    EXPECT_EQ(target->action, MainLayoutDockDropAction::MoveToDock);
    EXPECT_EQ(target->feedbackKind, UIDropFeedbackKind::Edge);
    EXPECT_TRUE(controller.dragging());
    ASSERT_TRUE(controller.target().has_value());
    EXPECT_EQ(controller.target()->location, DockLocation::AuxiliaryBar);
    EXPECT_TRUE(controller.target()->targetPanelId.empty());

    const std::optional<MainLayoutDockDropResult> drop = controller.endDrag(20.0f, 52.0f);
    ASSERT_TRUE(drop.has_value());
    EXPECT_EQ(drop->panelId, kMainLayoutPanelOutliner);
    EXPECT_EQ(drop->target.location, DockLocation::AuxiliaryBar);
    EXPECT_TRUE(drop->target.targetPanelId.empty());
    EXPECT_FALSE(controller.active());
    EXPECT_FALSE(controller.dragging());
    EXPECT_FALSE(controller.target().has_value());
}

TEST(MainLayoutDockDragControllerTests, DropsPanelBeforeOrAfterVisiblePanelTarget) {
    MainLayoutDockDragController controller;
    controller.setHitRegions({
            {
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 32},
            },
    });
    controller.setPanelDropZones({
            {
                    .location = DockLocation::Sidebar,
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 240},
            },
            {
                    .location = DockLocation::Sidebar,
                    .panelId = std::string(kMainLayoutPanelInspector),
                    .rect = {.left = 700, .top = 280, .width = 220, .height = 260},
            },
    });

    ASSERT_TRUE(controller.beginDragCandidate(720.0f, 52.0f));
    EXPECT_FALSE(controller.updateDrag(720.0f, 120.0f).has_value());

    std::optional<MainLayoutDockDropTarget> target = controller.updateDrag(720.0f, 300.0f);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->location, DockLocation::Sidebar);
    EXPECT_EQ(target->targetPanelId, kMainLayoutPanelInspector);
    EXPECT_EQ(target->position, DockPanelInsertPosition::Before);
    EXPECT_EQ(target->action, MainLayoutDockDropAction::InsertPanel);
    EXPECT_EQ(target->feedbackKind, UIDropFeedbackKind::Target);

    target = controller.updateDrag(720.0f, 500.0f);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->location, DockLocation::Sidebar);
    EXPECT_EQ(target->targetPanelId, kMainLayoutPanelInspector);
    EXPECT_EQ(target->position, DockPanelInsertPosition::After);

    const std::optional<MainLayoutDockDropResult> drop = controller.endDrag(720.0f, 500.0f);
    ASSERT_TRUE(drop.has_value());
    EXPECT_EQ(drop->panelId, kMainLayoutPanelOutliner);
    EXPECT_EQ(drop->target.location, DockLocation::Sidebar);
    EXPECT_EQ(drop->target.targetPanelId, kMainLayoutPanelInspector);
    EXPECT_EQ(drop->target.position, DockPanelInsertPosition::After);
    EXPECT_EQ(drop->target.action, MainLayoutDockDropAction::InsertPanel);
}

TEST(MainLayoutDockDragControllerTests, PanelDropZoneBeatsOverlappingSidebarDropZone) {
    MainLayoutDockDragController controller;
    controller.setHitRegions({
            {
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 32},
            },
    });
    controller.setDockDropZones({
            {
                    .location = DockLocation::Sidebar,
                    .rect = {.left = 680, .top = 0, .width = 280, .height = 600},
            },
    });
    controller.setPanelDropZones({
            {
                    .location = DockLocation::Sidebar,
                    .panelId = std::string(kMainLayoutPanelInspector),
                    .rect = {.left = 700, .top = 280, .width = 220, .height = 260},
            },
    });

    ASSERT_TRUE(controller.beginDragCandidate(720.0f, 52.0f));
    const std::optional<MainLayoutDockDropTarget> target = controller.updateDrag(720.0f, 300.0f);

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->location, DockLocation::Sidebar);
    EXPECT_EQ(target->targetPanelId, kMainLayoutPanelInspector);
    EXPECT_EQ(target->position, DockPanelInsertPosition::Before);
}

TEST(MainLayoutDockDragControllerTests, DraggingPanelOverItselfYieldsNoTarget) {
    MainLayoutDockDragController controller;
    controller.setHitRegions({
            {
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 32},
            },
    });
    controller.setPanelDropZones({
            {
                    .location = DockLocation::Sidebar,
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 240},
            },
    });

    ASSERT_TRUE(controller.beginDragCandidate(720.0f, 52.0f));
    EXPECT_FALSE(controller.updateDrag(720.0f, 120.0f).has_value());
    EXPECT_FALSE(controller.target().has_value());
}

TEST(MainLayoutDockDragControllerTests, DragsPanelHeaderToBottomDockEdge) {
    MainLayoutDockDragController controller;
    controller.setHitRegions({
            {
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 32},
            },
    });
    controller.setDockDropZones({
            {
                    .location = DockLocation::Panel,
                    .rect = {.left = 0, .top = 540, .width = 920, .height = 60},
            },
    });

    ASSERT_TRUE(controller.beginDragCandidate(720.0f, 52.0f));
    const std::optional<MainLayoutDockDropTarget> target = controller.updateDrag(460.0f, 570.0f);

    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->location, DockLocation::Panel);
    EXPECT_TRUE(target->targetPanelId.empty());
    EXPECT_EQ(target->action, MainLayoutDockDropAction::MoveToDock);
    EXPECT_EQ(target->feedbackKind, UIDropFeedbackKind::Edge);

    const std::optional<MainLayoutDockDropResult> drop = controller.endDrag(460.0f, 570.0f);
    ASSERT_TRUE(drop.has_value());
    EXPECT_EQ(drop->panelId, kMainLayoutPanelOutliner);
    EXPECT_EQ(drop->target.location, DockLocation::Panel);
    EXPECT_TRUE(drop->target.targetPanelId.empty());
}

TEST(MainLayoutDockDragControllerTests, DropsPanelBeforeOrAfterBottomTabTarget) {
    MainLayoutDockDragController controller;
    controller.setHitRegions({
            {
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 32},
            },
    });
    controller.setPanelDropZones({
            {
                    .location = DockLocation::Panel,
                    .panelId = std::string(kMainLayoutPanelInspector),
                    .rect = {.left = 280, .top = 360, .width = 180, .height = 32},
                    .action = MainLayoutDockDropAction::InsertPanel,
                    .placementPolicy = UIDropPlacementPolicy::HorizontalBeforeAfter,
                    .feedbackKind = UIDropFeedbackKind::Insertion,
            },
    });

    ASSERT_TRUE(controller.beginDragCandidate(720.0f, 52.0f));
    std::optional<MainLayoutDockDropTarget> target = controller.updateDrag(300.0f, 376.0f);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->location, DockLocation::Panel);
    EXPECT_EQ(target->targetPanelId, kMainLayoutPanelInspector);
    EXPECT_EQ(target->position, DockPanelInsertPosition::Before);
    EXPECT_EQ(target->action, MainLayoutDockDropAction::InsertPanel);
    EXPECT_EQ(target->feedbackKind, UIDropFeedbackKind::Insertion);

    target = controller.updateDrag(440.0f, 376.0f);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->location, DockLocation::Panel);
    EXPECT_EQ(target->targetPanelId, kMainLayoutPanelInspector);
    EXPECT_EQ(target->position, DockPanelInsertPosition::After);
}

TEST(MainLayoutDockDragControllerTests, DropsPanelBeforeOrAfterBottomBodySplitTarget) {
    MainLayoutDockDragController controller;
    controller.setHitRegions({
            {
                    .panelId = std::string(kMainLayoutPanelOutliner),
                    .rect = {.left = 700, .top = 40, .width = 220, .height = 32},
            },
    });
    controller.setPanelDropZones({
            {
                    .location = DockLocation::Panel,
                    .panelId = std::string(kMainLayoutPanelInspector),
                    .rect = {.left = 280, .top = 392, .width = 320, .height = 160},
                    .action = MainLayoutDockDropAction::SplitPanel,
                    .placementPolicy = UIDropPlacementPolicy::HorizontalBeforeAfter,
                    .feedbackKind = UIDropFeedbackKind::Target,
            },
    });

    ASSERT_TRUE(controller.beginDragCandidate(720.0f, 52.0f));
    std::optional<MainLayoutDockDropTarget> target = controller.updateDrag(300.0f, 420.0f);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->location, DockLocation::Panel);
    EXPECT_EQ(target->targetPanelId, kMainLayoutPanelInspector);
    EXPECT_EQ(target->position, DockPanelInsertPosition::Before);
    EXPECT_EQ(target->action, MainLayoutDockDropAction::SplitPanel);
    EXPECT_EQ(target->feedbackKind, UIDropFeedbackKind::Target);

    target = controller.updateDrag(580.0f, 420.0f);
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(target->position, DockPanelInsertPosition::After);
}

TEST(ResizableSplitControllerTests, HitTestsInflatedRegionsByAxis) {
    ResizableSplitController controller;
    controller.setHitRegions({
            {
                    .id = "sidebar",
                    .axis = ResizableSplitAxis::Horizontal,
                    .rect = ResizableSplitController::inflatedHitRect(
                            UIPixelRect{.left = 100, .top = 0, .width = 1, .height = 500},
                            ResizableSplitAxis::Horizontal,
                            12),
            },
            {
                    .id = "panels",
                    .axis = ResizableSplitAxis::Vertical,
                    .rect = ResizableSplitController::inflatedHitRect(
                            UIPixelRect{.left = 0, .top = 200, .width = 240, .height = 1},
                            ResizableSplitAxis::Vertical,
                            12),
            },
    });

    const std::optional<ResizableSplitHitRegion> sidebar = controller.hitTest(96.0f, 20.0f);
    ASSERT_TRUE(sidebar.has_value());
    EXPECT_EQ(sidebar->id, "sidebar");
    EXPECT_EQ(sidebar->axis, ResizableSplitAxis::Horizontal);

    const std::optional<ResizableSplitHitRegion> panels = controller.hitTest(20.0f, 196.0f);
    ASSERT_TRUE(panels.has_value());
    EXPECT_EQ(panels->id, "panels");
    EXPECT_EQ(panels->axis, ResizableSplitAxis::Vertical);

    EXPECT_FALSE(controller.hitTest(20.0f, 20.0f).has_value());
}

TEST(ResizableSplitControllerTests, ResizesWithDirectionDpScaleAndClamps) {
    ResizableSplitController controller;
    controller.beginDrag(
            {
                    .id = "right-sidebar",
                    .axis = ResizableSplitAxis::Horizontal,
                    .growth = ResizableSplitGrowthDirection::Negative,
                    .sizeDp = 240,
                    .minSize = ResizableSplitSizeRule::dp(160),
                    .maxSize = ResizableSplitSizeRule::dp(420),
            },
            400.0f,
            0.0f,
            2.0f);

    std::optional<ResizableSplitResizeResult> resize = controller.updateDrag(360.0f, 0.0f, 2.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->id, "right-sidebar");
    EXPECT_EQ(resize->sizeDp, 260);

    resize = controller.updateDrag(1200.0f, 0.0f, 2.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 160);

    controller.endDrag();
    EXPECT_FALSE(controller.updateDrag(360.0f, 0.0f, 2.0f).has_value());

    controller.beginDrag(
            {
                    .id = "top-panel",
                    .axis = ResizableSplitAxis::Vertical,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = 320,
                    .minSize = ResizableSplitSizeRule::dp(120),
                    .maxSize = ResizableSplitSizeRule::dp(900),
            },
            0.0f,
            100.0f,
            1.0f);
    resize = controller.updateDrag(0.0f, 40.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 260);
}

TEST(ResizableSplitControllerTests, ClampsAgainstOppositeMinSizeInsideContainer) {
    ResizableSplitController controller;
    controller.beginDrag(
            {
                    .id = "sidebar-panels",
                    .axis = ResizableSplitAxis::Vertical,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = 320,
                    .minSize = ResizableSplitSizeRule::dp(120),
                    .maxSize = ResizableSplitSizeRule::dp(900),
                    .oppositeMinSize = ResizableSplitSizeRule::dp(120),
                    .containerSizeDp = 480,
            },
            0.0f,
            100.0f,
            1.0f);

    const std::optional<ResizableSplitResizeResult> resize = controller.updateDrag(0.0f, 1000.0f, 1.0f);

    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 360);
    EXPECT_FALSE(resize->collapsed);
}

TEST(ResizableSplitControllerTests, UsesContainerAndOppositeMinAsEffectiveMaxWhenUnbounded) {
    ResizableSplitController controller;
    controller.beginDrag(
            {
                    .id = "sidebar",
                    .axis = ResizableSplitAxis::Horizontal,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = 240,
                    .minSize = ResizableSplitSizeRule::dp(160),
                    .oppositeMinSize = ResizableSplitSizeRule::dp(160),
                    .containerSizeDp = 720,
            },
            100.0f,
            0.0f,
            1.0f);

    const std::optional<ResizableSplitResizeResult> resize = controller.updateDrag(1000.0f, 0.0f, 1.0f);

    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 560);
    EXPECT_FALSE(resize->collapsed);
}

TEST(ResizableSplitControllerTests, RebasesDragAfterClampToAvoidStickyEdges) {
    ResizableSplitController controller;
    controller.beginDrag(
            {
                    .id = "bottom-split",
                    .axis = ResizableSplitAxis::Horizontal,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = 200,
                    .minSize = ResizableSplitSizeRule::dp(120),
                    .maxSize = ResizableSplitSizeRule::dp(220),
                    .containerSizeDp = 480,
            },
            0.0f,
            0.0f,
            1.0f);

    std::optional<ResizableSplitResizeResult> resize = controller.updateDrag(1000.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 220);

    resize = controller.updateDrag(999.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 219);
    EXPECT_FALSE(resize->collapsed);
}

TEST(ResizableSplitControllerTests, SupportsPercentBasedSizeRules) {
    ResizableSplitController controller;
    controller.beginDrag(
            {
                    .id = "percent",
                    .axis = ResizableSplitAxis::Horizontal,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = 250,
                    .minSize = ResizableSplitSizeRule::percent(20.0f),
                    .maxSize = ResizableSplitSizeRule::percent(80.0f),
                    .containerSizeDp = 500,
            },
            100.0f,
            0.0f,
            1.0f);

    std::optional<ResizableSplitResizeResult> resize = controller.updateDrag(-1000.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 100);

    resize = controller.updateDrag(1000.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_EQ(resize->sizeDp, 400);
}

TEST(ResizableSplitControllerTests, SupportsOptionalSnapCollapse) {
    ResizableSplitController controller;
    controller.beginDrag(
            {
                    .id = "sidebar",
                    .axis = ResizableSplitAxis::Horizontal,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = 160,
                    .minSize = ResizableSplitSizeRule::dp(160),
                    .maxSize = ResizableSplitSizeRule::dp(420),
                    .collapse = {
                            .enabled = true,
                            .collapsedSizeDp = 0,
                    },
            },
            160.0f,
            0.0f,
            1.0f);

    std::optional<ResizableSplitResizeResult> resize = controller.updateDrag(100.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_FALSE(resize->collapsed);
    EXPECT_EQ(resize->sizeDp, 160);

    resize = controller.updateDrag(50.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_TRUE(resize->collapsed);
    EXPECT_EQ(resize->sizeDp, 0);

    resize = controller.updateDrag(100.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_FALSE(resize->collapsed);
    EXPECT_EQ(resize->sizeDp, 160);

    controller.endDrag();
    controller.beginDrag(
            {
                    .id = "sidebar",
                    .axis = ResizableSplitAxis::Horizontal,
                    .growth = ResizableSplitGrowthDirection::Positive,
                    .sizeDp = 0,
                    .minSize = ResizableSplitSizeRule::dp(160),
                    .maxSize = ResizableSplitSizeRule::dp(420),
                    .collapse = {
                            .enabled = true,
                            .collapsedSizeDp = 0,
                    },
            },
            0.0f,
            0.0f,
            1.0f);

    resize = controller.updateDrag(70.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_TRUE(resize->collapsed);

    resize = controller.updateDrag(100.0f, 0.0f, 1.0f);
    ASSERT_TRUE(resize.has_value());
    EXPECT_FALSE(resize->collapsed);
    EXPECT_EQ(resize->sizeDp, 160);
}

TEST(MainLayoutRmlBuilderTests, GeneratesCleanViewportRightSidebarAndResizers) {
    const MainLayoutModel layout = MainLayoutModel::defaults();
    const std::string rml = buildMainLayoutRml(layout, {.viewport = "Viewport"});

    EXPECT_NE(rml.find("class=\"main-layout bg-gray-950 main-layout-sidebar-right\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-editor-column\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"modeling-mode-shelf-slot\" class=\"modeling-mode-shelf-slot\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"viewport-host\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-viewport\""), std::string::npos);
    EXPECT_NE(rml.find("src=\"quader://viewport/main\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-sidebar\" class=\"main-layout-sidebar main-layout-primary-sidebar main-layout-side-dock-right min-w-40 bg-gray-950 border-gray-600\""),
            std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-dock-location=\"Sidebar\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel min-h-30 bg-gray-950"), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-panel-outliner-header\""), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-drag-panel=\"outliner\""), std::string::npos);
    EXPECT_NE(rml.find("class=\"main-layout-panel-title text-sm text-gray-200\">Outliner</span>"), std::string::npos);
    EXPECT_EQ(rml.find("Rebind"), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-resizer=\"sidebar\""), std::string::npos);
    EXPECT_EQ(rml.find("data-main-layout-resizer=\"auxiliary-sidebar\""), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-resizer=\"panels\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-resizer-drag-line-vertical"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-resizer-drag-line-horizontal"), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-resizer-active"), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-panel-tabbar"), std::string::npos);
    EXPECT_EQ(rml.find("viewport-label"), std::string::npos);
    EXPECT_EQ(rml.find("<rml>"), std::string::npos);
    EXPECT_LT(rml.find("id=\"main-layout-editor-column\""), rml.find("id=\"modeling-mode-shelf-slot\""));
    EXPECT_LT(rml.find("id=\"modeling-mode-shelf-slot\""), rml.find("id=\"viewport-host\""));
}

TEST(MainLayoutRmlBuilderTests, MarksActiveResizerDuringDrag) {
    const MainLayoutModel layout = MainLayoutModel::defaults();

    const std::string sidebarRml = buildMainLayoutRml(
            layout,
            {},
            {.activeResizer = MainLayoutActiveResizer::Sidebar});
    EXPECT_NE(sidebarRml.find("main-layout-sidebar-resizer\" class=\"main-layout-sidebar-resizer main-layout-resizer-active"),
            std::string::npos);

    const std::string panelsRml = buildMainLayoutRml(
            layout,
            {},
            {.activeResizer = MainLayoutActiveResizer::Panels});
    EXPECT_NE(panelsRml.find("main-layout-panel-resizer\" class=\"main-layout-panel-resizer main-layout-resizer-active"),
            std::string::npos);

    MainLayoutModel bottomLayout = MainLayoutModel::defaults();
    ASSERT_TRUE(dockMovePanelToLocation(
            bottomLayout.dockLayout,
            kMainLayoutPanelInspector,
            DockLocation::Panel));
    const std::string bottomRml = buildMainLayoutRml(
            bottomLayout,
            {},
            {.activeResizer = MainLayoutActiveResizer::BottomDock});
    EXPECT_NE(bottomRml.find("main-layout-bottom-dock-resizer\" class=\"main-layout-bottom-dock-resizer main-layout-resizer-active"),
            std::string::npos);

    MainLayoutModel auxiliaryLayout = MainLayoutModel::defaults();
    ASSERT_TRUE(dockMovePanelToLocation(
            auxiliaryLayout.dockLayout,
            kMainLayoutPanelInspector,
            DockLocation::AuxiliaryBar));
    const std::string auxiliaryRml = buildMainLayoutRml(
            auxiliaryLayout,
            {},
            {.activeResizer = MainLayoutActiveResizer::AuxiliarySidebar});
    EXPECT_NE(auxiliaryRml.find("main-layout-auxiliary-sidebar-resizer\" class=\"main-layout-sidebar-resizer main-layout-resizer-active"),
            std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, MarksSidebarDropTargetDuringPanelHeaderDrag) {
    const MainLayoutModel layout = MainLayoutModel::defaults();
    const std::string rml = buildMainLayoutRml(
            layout,
            {},
            {
                    .dockDropLocation = DockLocation::AuxiliaryBar,
                    .dockDrag = MainLayoutDockDragPresentation{
                            .panelId = std::string(kMainLayoutPanelOutliner),
                            .previewLeftDp = 48,
                            .previewTopDp = 64,
                            .previewWidthDp = 220,
                            .previewHeightDp = 32,
                            .dragging = true,
                    },
            });

    EXPECT_NE(rml.find("id=\"main-layout-sidebar-drop-indicator\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-sidebar-drop-indicator-left bg-purple-500"), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-sidebar-drop-indicator-right bg-purple-500"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-drag-source"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-header-drag-source"), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-dock-drag-preview\""), std::string::npos);
    EXPECT_NE(rml.find("left: 48dp; top: 64dp; width: 220dp; height: 32dp;"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-dock-drag-preview main-layout-panel-header bg-gray-900 border-gray-600"),
            std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-title main-layout-dock-drag-preview-title text-sm text-gray-200\">Outliner</span>"),
            std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, MarksPanelInsertionTargetDuringPanelHeaderDrag) {
    const MainLayoutModel layout = MainLayoutModel::defaults();
    const std::string rml = buildMainLayoutRml(
            layout,
            {},
            {
                    .panelDrop = MainLayoutPanelDropPresentation{
                            .panelId = std::string(kMainLayoutPanelInspector),
                            .position = DockPanelInsertPosition::Before,
                    },
                    .dockDrag = MainLayoutDockDragPresentation{
                            .panelId = std::string(kMainLayoutPanelOutliner),
                            .previewLeftDp = 48,
                            .previewTopDp = 64,
                            .dragging = true,
                    },
            });

    EXPECT_EQ(rml.find("id=\"main-layout-sidebar-drop-indicator\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-drop-target"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-drop-overlay main-layout-panel-drop-overlay-vertical main-layout-panel-drop-overlay-vertical-before bg-purple-500"),
            std::string::npos);
    EXPECT_EQ(rml.find("main-layout-panel-drop-overlay-vertical-after bg-purple-500"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-drag-source"), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-dock-drag-preview\""), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, MarksBottomDockDropTargetDuringPanelHeaderDrag) {
    const MainLayoutModel layout = MainLayoutModel::defaults();
    const std::string rml = buildMainLayoutRml(
            layout,
            {},
            {
                    .dockDropLocation = DockLocation::Panel,
                    .dockDrag = MainLayoutDockDragPresentation{
                            .panelId = std::string(kMainLayoutPanelOutliner),
                            .previewLeftDp = 48,
                            .previewTopDp = 64,
                            .dragging = true,
                    },
            });

    EXPECT_EQ(rml.find("id=\"main-layout-sidebar-drop-indicator\""), std::string::npos);
    const std::size_t editorColumn = rml.find("id=\"main-layout-editor-column\"");
    const std::size_t bottomIndicator = rml.find("id=\"main-layout-bottom-dock-drop-indicator\"");
    ASSERT_NE(editorColumn, std::string::npos);
    ASSERT_NE(bottomIndicator, std::string::npos);
    EXPECT_LT(editorColumn, bottomIndicator);
    EXPECT_NE(rml.find("main-layout-bottom-dock-drop-indicator bg-purple-500"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-drag-source"), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-dock-drag-preview\""), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, MarksBottomTabInsertionTargetDuringPanelHeaderDrag) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelOutliner, DockLocation::Panel));
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelInspector, DockLocation::Panel));

    const std::string rml = buildMainLayoutRml(
            layout,
            {},
            {
                    .panelDrop = MainLayoutPanelDropPresentation{
                            .location = DockLocation::Panel,
                            .panelId = std::string(kMainLayoutPanelInspector),
                            .position = DockPanelInsertPosition::After,
                            .feedbackKind = UIDropFeedbackKind::Insertion,
                    },
                    .dockDrag = MainLayoutDockDragPresentation{
                            .panelId = std::string(kMainLayoutPanelOutliner),
                            .previewLeftDp = 48,
                            .previewTopDp = 64,
                            .dragging = true,
                    },
            });

    EXPECT_NE(rml.find("id=\"main-layout-bottom-dock-tab-strip\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-panel-inspector-tab\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-tab-drop-target"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-tab-drop-insertion-line main-layout-tab-drop-insertion-line-after bg-purple-500"),
            std::string::npos);
    EXPECT_EQ(rml.find("main-layout-tab-drop-overlay"), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-panel-drop-overlay-horizontal-after bg-purple-500"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-tab-drag-source"), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, MarksBottomPanelSplitTargetDuringPanelHeaderDrag) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelOutliner, DockLocation::Panel));
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelInspector, DockLocation::Panel));

    const std::string rml = buildMainLayoutRml(
            layout,
            {},
            {
                    .panelDrop = MainLayoutPanelDropPresentation{
                            .location = DockLocation::Panel,
                            .panelId = std::string(kMainLayoutPanelInspector),
                            .position = DockPanelInsertPosition::Before,
                            .feedbackKind = UIDropFeedbackKind::Target,
                    },
                    .dockDrag = MainLayoutDockDragPresentation{
                            .panelId = std::string(kMainLayoutPanelOutliner),
                            .previewLeftDp = 48,
                            .previewTopDp = 64,
                            .dragging = true,
                    },
            });

    EXPECT_NE(rml.find("id=\"main-layout-bottom-dock-content\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-drop-target"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-drop-overlay main-layout-panel-drop-overlay-horizontal main-layout-panel-drop-overlay-horizontal-before bg-purple-500"),
            std::string::npos);
    EXPECT_EQ(rml.find("main-layout-tab-drop-insertion-line"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-tab-drag-source"), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, CanProjectLeftSidebarAndHiddenPanels) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    layout.sidebarSide = MainLayoutSidebarSide::Left;
    layout.outlinerVisible = false;

    const std::string rml = buildMainLayoutRml(layout, {.viewport = "Viewport"});

    EXPECT_NE(rml.find("class=\"main-layout bg-gray-950 main-layout-sidebar-left\""), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-panel-outliner"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-inspector"), std::string::npos);
    EXPECT_EQ(rml.find("data-main-layout-resizer=\"panels\""), std::string::npos);
    EXPECT_LT(rml.find("main-layout-sidebar"), rml.find("viewport-host"));
}

TEST(MainLayoutRmlBuilderTests, ProjectsAuxiliaryBarOnOppositeSideOfPrimarySidebar) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    layout.auxiliarySidebar.widthDp = 312;
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelInspector, DockLocation::AuxiliaryBar));

    const std::string rml = buildMainLayoutRml(layout, {.viewport = "Viewport"});

    const std::size_t auxiliary = rml.find("id=\"main-layout-auxiliary-sidebar\"");
    const std::size_t viewport = rml.find("id=\"viewport-host\"");
    const std::size_t primary = rml.find("id=\"main-layout-sidebar\"");
    const std::size_t inspector = rml.find("id=\"main-layout-panel-inspector\"");
    const std::size_t outliner = rml.find("id=\"main-layout-panel-outliner\"");
    ASSERT_NE(auxiliary, std::string::npos);
    ASSERT_NE(viewport, std::string::npos);
    ASSERT_NE(primary, std::string::npos);
    ASSERT_NE(inspector, std::string::npos);
    ASSERT_NE(outliner, std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-dock-location=\"AuxiliaryBar\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-auxiliary-sidebar main-layout-side-dock-left"), std::string::npos);
    EXPECT_NE(rml.find("width: 312dp"), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-resizer=\"auxiliary-sidebar\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-auxiliary-sidebar-resizer\""), std::string::npos);
    EXPECT_LT(auxiliary, viewport);
    EXPECT_LT(viewport, primary);
    EXPECT_LT(inspector, viewport);
    EXPECT_LT(viewport, outliner);
    EXPECT_EQ(rml.find("data-main-layout-resizer=\"panels\""), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, ProjectsPanelDockLocationIntoBottomDock) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    layout.bottomDock.heightDp = 260;
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelInspector, DockLocation::Panel));

    const std::string rml = buildMainLayoutRml(layout, {.viewport = "Viewport"});

    const std::size_t editorColumn = rml.find("id=\"main-layout-editor-column\"");
    const std::size_t viewport = rml.find("id=\"viewport-host\"");
    const std::size_t bottomDock = rml.find("id=\"main-layout-bottom-dock\"");
    const std::size_t outliner = rml.find("id=\"main-layout-panel-outliner\"");
    const std::size_t inspector = rml.find("id=\"main-layout-panel-inspector\"");
    const std::size_t inspectorTab = rml.find("id=\"main-layout-panel-inspector-tab\"");
    ASSERT_NE(editorColumn, std::string::npos);
    ASSERT_NE(viewport, std::string::npos);
    ASSERT_NE(bottomDock, std::string::npos);
    ASSERT_NE(outliner, std::string::npos);
    ASSERT_NE(inspector, std::string::npos);
    ASSERT_NE(inspectorTab, std::string::npos);
    EXPECT_NE(rml.find("class=\"main-layout-bottom-dock bg-gray-950 border-gray-600\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-bottom-dock-tab-strip\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-bottom-dock-content\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-tab-active bg-gray-950"), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-tab-panel=\"inspector\""), std::string::npos);
    EXPECT_NE(rml.find("height: 260dp"), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-bottom-dock-resizer\""), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-resizer=\"bottom-dock\""), std::string::npos);
    EXPECT_LT(editorColumn, viewport);
    EXPECT_LT(viewport, bottomDock);
    EXPECT_LT(bottomDock, outliner);
    EXPECT_LT(bottomDock, inspectorTab);
    EXPECT_LT(inspectorTab, inspector);
    EXPECT_EQ(countOccurrences(rml, "id=\"main-layout-panel-inspector\""), 1u);
    EXPECT_EQ(rml.find("data-main-layout-resizer=\"panels\""), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, ProjectsMultiplePanelsInBottomDockOrder) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelOutliner, DockLocation::Panel));
    ASSERT_TRUE(dockMovePanelToLocationAtPanel(
            layout.dockLayout,
            kMainLayoutPanelInspector,
            DockLocation::Panel,
            kMainLayoutPanelOutliner,
            DockPanelInsertPosition::Before));

    const std::string rml = buildMainLayoutRml(layout, {.viewport = "Viewport"});

    const std::size_t bottomDock = rml.find("id=\"main-layout-bottom-dock\"");
    const std::size_t tabStrip = rml.find("id=\"main-layout-bottom-dock-tab-strip\"");
    const std::size_t inspectorTab = rml.find("id=\"main-layout-panel-inspector-tab\"");
    const std::size_t outlinerTab = rml.find("id=\"main-layout-panel-outliner-tab\"");
    const std::size_t inspector = rml.find("id=\"main-layout-panel-inspector\"");
    ASSERT_NE(bottomDock, std::string::npos);
    ASSERT_NE(tabStrip, std::string::npos);
    ASSERT_NE(inspectorTab, std::string::npos);
    ASSERT_NE(outlinerTab, std::string::npos);
    ASSERT_NE(inspector, std::string::npos);
    EXPECT_EQ(rml.find("id=\"main-layout-panel-outliner\""), std::string::npos);
    EXPECT_LT(bottomDock, tabStrip);
    EXPECT_LT(tabStrip, inspectorTab);
    EXPECT_LT(inspectorTab, outlinerTab);
    EXPECT_LT(outlinerTab, inspector);
    EXPECT_NE(rml.find("main-layout-tab-active bg-gray-950"), std::string::npos);
    EXPECT_EQ(countOccurrences(rml, "data-main-layout-tab-panel=\""), 2u);
    EXPECT_EQ(countOccurrences(rml, "main-layout-bottom-panel-pane"), 1u);
    EXPECT_EQ(rml.find("data-main-layout-resizer=\"panels\""), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, ProjectsBottomSplitPanelsHorizontally) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelOutliner, DockLocation::Panel));
    ASSERT_TRUE(dockSplitPanelWithPanel(
            layout.dockLayout,
            kMainLayoutPanelInspector,
            DockLocation::Panel,
            kMainLayoutPanelOutliner,
            DockPanelInsertPosition::After));
    ASSERT_TRUE(dockSetSplitPanelSizeDp(
            layout.dockLayout,
            DockLocation::Panel,
            kMainLayoutPanelOutliner,
            288));

    const std::string rml = buildMainLayoutRml(
            layout,
            {.viewport = "Viewport"},
            {
                    .activeResizer = MainLayoutActiveResizer::BottomSplitPanel,
                    .activeBottomSplitPanelId = std::string(kMainLayoutPanelOutliner),
            });

    const std::size_t content = rml.find("id=\"main-layout-bottom-dock-content\"");
    const std::size_t outliner = rml.find("id=\"main-layout-panel-outliner\"");
    const std::size_t resizer = rml.find("id=\"main-layout-bottom-split-resizer-outliner\"");
    const std::size_t inspector = rml.find("id=\"main-layout-panel-inspector\"");
    ASSERT_NE(content, std::string::npos);
    ASSERT_NE(outliner, std::string::npos);
    ASSERT_NE(resizer, std::string::npos);
    ASSERT_NE(inspector, std::string::npos);
    EXPECT_LT(content, outliner);
    EXPECT_LT(outliner, resizer);
    EXPECT_LT(resizer, inspector);
    EXPECT_LT(outliner, inspector);
    EXPECT_NE(rml.find("id=\"main-layout-panel-outliner-tab\""), std::string::npos);
    EXPECT_EQ(rml.find("id=\"main-layout-panel-inspector-tab\""), std::string::npos);
    EXPECT_EQ(countOccurrences(rml, "data-main-layout-tab-panel=\""), 1u);
    EXPECT_EQ(countOccurrences(rml, "main-layout-bottom-panel-pane"), 2u);
    EXPECT_EQ(countOccurrences(rml, "data-main-layout-resizer=\"bottom-split:"), 1u);
    EXPECT_NE(rml.find("data-main-layout-resizer=\"bottom-split:outliner\""), std::string::npos);
    EXPECT_EQ(rml.find("data-main-layout-resizer=\"bottom-split:inspector\""), std::string::npos);
    EXPECT_NE(
            rml.find("id=\"main-layout-panel-outliner\" class=\"main-layout-panel main-layout-panel-fixed"),
            std::string::npos);
    EXPECT_EQ(
            rml.find("id=\"main-layout-panel-outliner\" class=\"main-layout-panel main-layout-panel-flex"),
            std::string::npos);
    EXPECT_NE(rml.find("width: 288dp; flex-basis: 288dp; flex-grow: 0; flex-shrink: 0;"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-bottom-split-resizer main-layout-resizer-active"), std::string::npos);
    EXPECT_NE(rml.find("main-layout-bottom-panel-active"), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, CollapsedSidebarKeepsEdgeResizerWithoutSidebarSurface) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    layout.sidebar.collapsed = true;

    const std::string rml = buildMainLayoutRml(layout, {.viewport = "Viewport"});

    EXPECT_NE(rml.find("id=\"viewport-host\""), std::string::npos);
    EXPECT_EQ(rml.find("id=\"main-layout-sidebar\""), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-panel-outliner"), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-panel-inspector"), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-resizer=\"sidebar\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-sidebar-resizer-collapsed-right"), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, CollapsedBottomDockKeepsEdgeResizerWithoutDockSurface) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    layout.bottomDock.collapsed = true;
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelInspector, DockLocation::Panel));

    const std::string rml = buildMainLayoutRml(layout, {.viewport = "Viewport"});

    EXPECT_NE(rml.find("id=\"viewport-host\""), std::string::npos);
    EXPECT_EQ(rml.find("id=\"main-layout-bottom-dock\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-outliner"), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-panel-inspector"), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-resizer=\"bottom-dock\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-bottom-dock-resizer-collapsed"), std::string::npos);
}

TEST(MainLayoutRmlBuilderTests, CollapsedAuxiliarySidebarKeepsEdgeResizerWithoutSideDockSurface) {
    MainLayoutModel layout = MainLayoutModel::defaults();
    layout.auxiliarySidebar.collapsed = true;
    ASSERT_TRUE(dockMovePanelToLocation(layout.dockLayout, kMainLayoutPanelInspector, DockLocation::AuxiliaryBar));

    const std::string rml = buildMainLayoutRml(layout, {.viewport = "Viewport"});

    EXPECT_NE(rml.find("id=\"viewport-host\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"main-layout-sidebar\""), std::string::npos);
    EXPECT_EQ(rml.find("id=\"main-layout-auxiliary-sidebar\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-panel-outliner"), std::string::npos);
    EXPECT_EQ(rml.find("main-layout-panel-inspector"), std::string::npos);
    EXPECT_NE(rml.find("data-main-layout-resizer=\"auxiliary-sidebar\""), std::string::npos);
    EXPECT_NE(rml.find("main-layout-sidebar-resizer-collapsed-left"), std::string::npos);
}

TEST(UIUtilityTokensTests, AppUITokenConfigPathUsesRuntimeUiTokenDirectory) {
    const std::filesystem::path executableDir = "runtime";

    EXPECT_EQ(
            appUITokenConfigPath(executableDir),
            executableDir / "ui" / "tokens" / "app-ui.tokens.json");
}

TEST(UIUtilityTokensTests, LoadsTailwindStyleTokenConfigInAuthorOrder) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_token_config.json";
    {
        std::ofstream file(path);
        ASSERT_TRUE(file.is_open()) << path.string();
        file << R"JSON({
  "theme": {
    "colors": {
      "gray": {
        "50": "#F7F7F7",
        "100": "#D8D8D8"
      },
      "purple": {
        "300": "#B78BE9"
      }
    },
    "fontFamily": {
      "ui": "Inter"
    },
    "fontWeight": {
      "medium": 500
    },
    "fontSize": {
      "sm": "14dp"
    },
    "spacing": {
      "3": "12dp"
    },
    "borderRadius": {
      "6": "6dp"
    },
    "extend": {
      "colors": {
        "accent": {
          "500": "#ABCDEF"
        }
      },
      "spacing": {
        "4": "16dp"
      }
    }
  }
})JSON";
    }

    const UITokenConfig tokens = loadUITokenConfig(path);

    ASSERT_EQ(tokens.colors.size(), 4u);
    EXPECT_EQ(tokens.colors[0].name, "gray.50");
    EXPECT_EQ(tokens.colors[0].hex, "#F7F7F7");
    EXPECT_EQ(tokens.colors[1].name, "gray.100");
    EXPECT_EQ(tokens.colors[1].hex, "#D8D8D8");
    EXPECT_EQ(tokens.colors[2].name, "purple.300");
    EXPECT_EQ(tokens.colors[2].hex, "#B78BE9");
    EXPECT_EQ(tokens.colors[3].name, "accent.500");
    EXPECT_EQ(tokens.colors[3].hex, "#ABCDEF");
    ASSERT_EQ(tokens.utilities.size(), 8u);
    EXPECT_EQ(tokens.utilities[0].name, "font-ui");
    EXPECT_EQ(tokens.utilities[0].value, "Inter");
    EXPECT_EQ(tokens.utilities[1].name, "font-medium");
    EXPECT_EQ(tokens.utilities[1].value, "500");
    EXPECT_EQ(tokens.utilities[2].name, "text-sm");
    EXPECT_EQ(tokens.utilities[2].value, "14dp");
    EXPECT_EQ(tokens.utilities[3].name, "space-3");
    EXPECT_EQ(tokens.utilities[3].value, "12dp");
    EXPECT_EQ(tokens.utilities[4].name, "size-3");
    EXPECT_EQ(tokens.utilities[4].value, "12dp");
    EXPECT_EQ(tokens.utilities[5].name, "rounded-6");
    EXPECT_EQ(tokens.utilities[5].value, "6dp");
    EXPECT_EQ(tokens.utilities[6].name, "space-4");
    EXPECT_EQ(tokens.utilities[6].value, "16dp");
    EXPECT_EQ(tokens.utilities[7].name, "size-4");
    EXPECT_EQ(tokens.utilities[7].value, "16dp");

    std::filesystem::remove(path);
}

TEST(UIClassListTests, MaintainsOrderedDuplicateFreeClasses) {
    UIClassList classes;
    classes.add("chrome-icon-button").add("bg-gray-800").add("chrome-icon-button").addIf(false, "hidden");

    EXPECT_TRUE(classes.contains("chrome-icon-button"));
    EXPECT_FALSE(classes.contains("hidden"));
    EXPECT_EQ(classes.toString(), "chrome-icon-button bg-gray-800");
}

TEST(RmlMarkupWriterTests, EscapesAttributesTextAndStyles) {
    UIElement element = UIElement::div()
            .id("root&panel")
            .addClass("panel")
            .attr("data-title", "A \"quoted\" title")
            .style("height", "10dp")
            .child(UIElement::span().text("A < B & C"));

    EXPECT_EQ(
            writeRmlMarkup(element),
            "<div id=\"root&amp;panel\" class=\"panel\" data-title=\"A &quot;quoted&quot; title\" "
            "style=\"height: 10dp;\"><span>A &lt; B &amp; C</span></div>");
}

TEST(UIIconRegistryTests, RendersSemanticIconsThroughCurrentResourceProvider) {
    const UIIconRegistry& registry = UIIconRegistry::instance();
    const UIIconDefinition* closeIcon = registry.find(UIIconId::ChromeClose);
    const UIIconDefinition* meshIcon = registry.find(UIIconId::Mesh);

    ASSERT_NE(closeIcon, nullptr);
    ASSERT_NE(meshIcon, nullptr);
    EXPECT_EQ(closeIcon->name, "chrome-close");
    EXPECT_EQ(closeIcon->className, "ui-icon-chrome-close");
    EXPECT_EQ(closeIcon->provider, UIIconProvider::ImageResource);
    EXPECT_EQ(closeIcon->resourcePath, "quader://icons/close_icon.png");
    EXPECT_EQ(meshIcon->name, "mesh");
    EXPECT_EQ(meshIcon->className, "ui-icon-mesh");
    EXPECT_EQ(meshIcon->resourcePath, "quader://icons/mesh_icon.png");
    EXPECT_EQ(uiIconProviderClass(closeIcon->provider), "ui-icon-resource-image");

    EXPECT_EQ(
            writeRmlMarkup(registry.renderIcon(UIIconId::ChromeClose, "toolbar-icon")),
            "<span class=\"ui-icon ui-icon-resource-image ui-icon-chrome-close toolbar-icon\" "
            "data-ui-icon=\"chrome-close\"></span>");
}

TEST(UIButtonComponentTests, RendersStatefulLabeledIconButtonsThroughBaseClass) {
    /// Test-only button variant for exercising base button rendering.
    class TestButton final : public UIButtonComponent {
    public:
        TestButton()
            : UIButtonComponent("test-button") {
            setIcon(UIIconId::ChromeClose, UIButtonIconPlacement::Left);
            setLabel("Run");
            setHovered(true);
            setToggleButton(true);
            setToggled(true);
        }

    protected:
        void appendButtonClasses(UIElement& button) const override {
            button.addClass("test-button");
        }

        void appendButtonAttributes(UIElement& button) const override {
            UIButtonComponent::appendButtonAttributes(button);
            button.data("test-action", "run");
        }
    };

    const TestButton button;

    EXPECT_EQ(
            writeRmlMarkup(button.render()),
            "<button id=\"test-button\" class=\"test-button\" data-ui-hover=\"true\" aria-pressed=\"true\" "
            "data-ui-toggle=\"on\" data-test-action=\"run\"><span class=\"ui-icon ui-icon-resource-image "
            "ui-icon-chrome-close ui-button-icon\" data-ui-icon=\"chrome-close\"></span><span "
            "class=\"ui-button-label\">Run</span></button>");
}

TEST(UIToolbarButtonComponentTests, RendersIconLabelAndSecondaryText) {
    const UIToolbarButtonComponent button(UIToolbarButtonModel{
            .elementId = "toolbar-vertices",
            .icon = UIIconId::Mesh,
            .label = "Vertices",
            .secondaryText = "Shift+Z",
    });

    const std::string rml = writeRmlMarkup(button.render());

    EXPECT_NE(rml.find("id=\"toolbar-vertices\" class=\"toolbar-button bg-gray-950 border-1 border-gray-700 rounded-6 min-w-8 h-8\""),
            std::string::npos);
    EXPECT_NE(rml.find("class=\"ui-icon ui-icon-resource-image ui-icon-mesh toolbar-button-icon\" data-ui-icon=\"mesh\""),
            std::string::npos);
    EXPECT_NE(rml.find("class=\"toolbar-button-label text-sm text-gray-100\">Vertices</span>"),
            std::string::npos);
    EXPECT_NE(rml.find("class=\"toolbar-button-secondary font-alt text-xxs text-gray-300\">SHIFT+Z</span>"),
            std::string::npos);

    const UIToolbarButtonComponent iconButton(UIToolbarButtonModel{
            .elementId = "toolbar-icon",
            .icon = UIIconId::Mesh,
    });
    EXPECT_NE(writeRmlMarkup(iconButton.render()).find("toolbar-button-icon-only"), std::string::npos);
}

TEST(UIChromeButtonComponentTests, BuildsCurrentWindowChromeButtonsFromComponents) {
    EXPECT_EQ(chromeButtonElementId(UIChromeButtonKind::Minimize), "chrome-minimize-button");
    EXPECT_EQ(chromeButtonActionName(UIChromeButtonKind::Restore), "restore");
    EXPECT_EQ(chromeButtonIconId(UIChromeButtonKind::Close), UIIconId::ChromeClose);

    const std::string rml = buildWindowChromeControlsRml();
    const std::string innerRml = buildWindowChromeControlsInnerRml();

    EXPECT_NE(rml.find("<div id=\"chrome-window-controls\" class=\"chrome-window-controls\">"), std::string::npos);
    EXPECT_NE(rml.find("id=\"chrome-minimize-button\" class=\"chrome-icon-button bg-gray-800 hover-bg-gray-600 active-bg-gray-400\" data-chrome-window-action=\"minimize\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"chrome-restore-button\" class=\"chrome-icon-button bg-gray-800 hover-bg-gray-600 active-bg-gray-400\" data-chrome-window-action=\"restore\""), std::string::npos);
    EXPECT_NE(rml.find("id=\"chrome-close-button\" class=\"chrome-icon-button bg-gray-800 hover-bg-red-500 active-bg-red-600\" data-chrome-window-action=\"close\""), std::string::npos);
    EXPECT_NE(rml.find("class=\"ui-icon ui-icon-resource-image ui-icon-chrome-minimize ui-button-icon\" data-ui-icon=\"chrome-minimize\""), std::string::npos);
    EXPECT_NE(rml.find("class=\"ui-icon ui-icon-resource-image ui-icon-chrome-restore ui-button-icon\" data-ui-icon=\"chrome-restore\""), std::string::npos);
    EXPECT_NE(rml.find("class=\"ui-icon ui-icon-resource-image ui-icon-chrome-close ui-button-icon\" data-ui-icon=\"chrome-close\""), std::string::npos);
    EXPECT_EQ(countOccurrences(rml, "data-chrome-window-action"), 3u);
    EXPECT_EQ(countOccurrences(rml, "data-ui-icon"), 3u);
    EXPECT_EQ(innerRml.find("id=\"chrome-window-controls\""), std::string::npos);
    EXPECT_EQ(countOccurrences(innerRml, "data-chrome-window-action"), 3u);
}

TEST(ModelingModeShelfComponentTests, BuildsTabbedShelfShellOnly) {
    const std::vector<ModelingModeShelfControl> controls = {
            {
                    .id = "object",
                    .label = "Object",
                    .icon = UIIconId::Mesh,
                    .labelVisibility = ModelingModeShelfControlLabelVisibility::Active,
                    .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
            },
            {
                    .id = "vertex",
                    .label = "Vertices",
                    .icon = UIIconId::Mesh,
                    .labelVisibility = ModelingModeShelfControlLabelVisibility::Active,
                    .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
            },
            {
                    .id = "edge",
                    .label = "Edges",
                    .icon = UIIconId::Mesh,
                    .labelVisibility = ModelingModeShelfControlLabelVisibility::Active,
                    .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
            },
            {
                    .id = "face",
                    .label = "Faces",
                    .icon = UIIconId::Mesh,
                    .labelVisibility = ModelingModeShelfControlLabelVisibility::Active,
                    .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
            },
    };
    const std::vector<ModelingModeShelfCommand> commands = {
            {.id = "bridge", .label = "Bridge"},
            {.id = "collapse", .label = "Collapse", .secondaryText = "Shift+Z"},
            {.id = "thicken", .label = "Thicken", .secondaryText = "T"},
            {.id = "flip-normal", .label = "Flip Normal", .secondaryText = "F"},
    };

    for (const ModelingModeShelfControl& control : controls) {
        EXPECT_TRUE(modelingModeShelfControlOpensTabContent(control));
    }
    EXPECT_EQ(modelingModeShelfControlButtonElementId("face"), "modeling-mode-shelf-control-face");
    EXPECT_EQ(modelingModeShelfControlMeasureElementId("face"), "modeling-mode-shelf-measure-control-face");
    EXPECT_EQ(modelingModeShelfOverflowToggleControlId(), "__modeling-mode-shelf-overflow-toggle");
    EXPECT_EQ(modelingModeShelfOverflowButtonElementId(), "modeling-mode-shelf-overflow-button");

    const std::string toolbarRml = writeRmlMarkup(ModelingModeShelfToolbarComponent(ModelingModeShelfState{
            .controls = controls,
            .activeTabControlId = std::string("face"),
    }).render());
    EXPECT_EQ(countOccurrences(toolbarRml, "data-modeling-mode-shelf-control="), 4u);
    EXPECT_EQ(countOccurrences(toolbarRml, "data-modeling-mode-shelf-control-behavior=\"opens-tab-content\""), 4u);
    EXPECT_EQ(countOccurrences(toolbarRml, "data-modeling-mode-shelf-control-behavior=\"action\""), 0u);
    EXPECT_EQ(countOccurrences(toolbarRml, "data-ui-icon=\"mesh\""), 8u);
    EXPECT_EQ(toolbarRml.find(">Object</span>"), std::string::npos);
    EXPECT_NE(toolbarRml.find(">Faces</span>"), std::string::npos);

    const std::string rml = buildModelingModeShelfRml(ModelingModeShelfState{
            .controls = controls,
            .activeStripCommands = commands,
            .activeTabControlId = std::string("face"),
    });

    EXPECT_NE(rml.find("<div id=\"modeling-mode-shelf\" class=\"modeling-mode-shelf bg-gray-950\">"),
            std::string::npos);
    EXPECT_NE(rml.find("id=\"modeling-mode-shelf-toolbar\" class=\"modeling-mode-shelf-toolbar h-13\""),
            std::string::npos);
    EXPECT_NE(rml.find("id=\"modeling-mode-shelf-measure-strip\" class=\"modeling-mode-shelf-measure-strip h-13\""),
            std::string::npos);
    EXPECT_NE(rml.find("id=\"modeling-mode-shelf-measure-control-face\""),
            std::string::npos);
    EXPECT_NE(rml.find("id=\"modeling-mode-shelf-content\" class=\"modeling-mode-shelf-content h-11 bg-gray-800\" "
                       "data-modeling-mode-shelf-active-control=\"face\""),
            std::string::npos);
    EXPECT_EQ(countOccurrences(rml, "data-modeling-mode-shelf-command="), 4u);
    EXPECT_NE(rml.find("id=\"modeling-mode-shelf-command-bridge\""), std::string::npos);
    EXPECT_NE(rml.find("class=\"toolbar-button-secondary font-alt text-xxs text-gray-300\">SHIFT+Z</span>"),
            std::string::npos);
    EXPECT_EQ(countOccurrences(rml, "data-modeling-mode-shelf-control="), 4u);
    EXPECT_EQ(countOccurrences(rml, "aria-pressed=\"true\""), 1u);
    const std::size_t activeFace = rml.find("id=\"modeling-mode-shelf-control-face\"");
    ASSERT_NE(activeFace, std::string::npos);
    EXPECT_NE(rml.find("modeling-mode-shelf-tab-button-active", activeFace), std::string::npos);
    EXPECT_EQ(rml.find(">Object</span>"), std::string::npos);
    EXPECT_EQ(countOccurrences(rml, ">Faces</span>"), 2u);

    const std::string closedRml = buildModelingModeShelfRml(ModelingModeShelfState{
            .controls = controls,
            .activeStripCommands = commands,
            .activeTabControlId = std::nullopt,
    });
    EXPECT_EQ(closedRml.find("modeling-mode-shelf-content"), std::string::npos);
    EXPECT_EQ(closedRml.find("data-modeling-mode-shelf-command="), std::string::npos);
    EXPECT_EQ(countOccurrences(closedRml, "data-modeling-mode-shelf-control="), 4u);
}

TEST(ModelingModeShelfComponentTests, RendersOverflowToggleAndMenuFromState) {
    const std::vector<ModelingModeShelfControl> controls = {
            {
                    .id = "object",
                    .label = "Object",
                    .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
            },
            {
                    .id = "vertex",
                    .label = "Vertices",
                    .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
            },
            {
                    .id = "edge",
                    .label = "Edges",
                    .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
            },
            {
                    .id = "face",
                    .label = "Faces",
                    .behavior = ModelingModeShelfControlBehavior::OpensTabContent,
            },
    };

    const ModelingModeShelfState closedState{
            .controls = controls,
            .overflowControlIds = {"edge", "face"},
            .activeTabControlId = std::string("face"),
            .overflowButtonLabel = "More",
            .overflowMenuOpen = false,
    };
    const std::string closedRml = buildModelingModeShelfRml(closedState);

    EXPECT_NE(closedRml.find("id=\"modeling-mode-shelf-overflow-button\""),
            std::string::npos);
    EXPECT_NE(closedRml.find("data-modeling-mode-shelf-overflow-toggle=\"true\""),
            std::string::npos);
    EXPECT_NE(closedRml.find("aria-expanded=\"false\""),
            std::string::npos);
    EXPECT_EQ(closedRml.find("id=\"modeling-mode-shelf-control-edge\""),
            std::string::npos);
    EXPECT_EQ(closedRml.find("id=\"modeling-mode-shelf-overflow-menu\""),
            std::string::npos);
    EXPECT_EQ(countOccurrences(closedRml, "data-modeling-mode-shelf-control="), 2u);

    ModelingModeShelfState openState = closedState;
    openState.overflowMenuOpen = true;
    const std::string openRml = buildModelingModeShelfRml(openState);

    EXPECT_NE(openRml.find("id=\"modeling-mode-shelf-overflow-menu\" class=\"modeling-mode-shelf-overflow-menu bg-gray-950\""),
            std::string::npos);
    EXPECT_NE(openRml.find("aria-expanded=\"true\""),
            std::string::npos);
    EXPECT_NE(openRml.find("id=\"modeling-mode-shelf-control-edge\""),
            std::string::npos);
    EXPECT_NE(openRml.find("id=\"modeling-mode-shelf-control-face\""),
            std::string::npos);
    EXPECT_EQ(countOccurrences(openRml, "data-modeling-mode-shelf-control="), 4u);
    EXPECT_EQ(countOccurrences(openRml, "data-modeling-mode-shelf-overflow-toggle=\"true\""), 1u);
}

TEST(DebugPaletteViewTests, BuildsCopyablePaletteSwatchRml) {
    const std::vector<DebugPaletteColorToken> colors = {
            {.name = "gray.50", .hex = "#F7F7F7"},
            {.name = "purple.950", .hex = "#120F18"},
    };

    const std::string rml = buildDebugPalettePageRml(colors);

    EXPECT_NE(rml.find("id=\"debug-palette-page\""), std::string::npos);
    EXPECT_EQ(rml.find("debug-keybind"), std::string::npos);
    EXPECT_NE(rml.find("debug-palette-swatch"), std::string::npos);
    EXPECT_NE(rml.find("font-ui bg-purple-950 text-gray-50 p-6"), std::string::npos);
    EXPECT_NE(rml.find("bg-gray-50"), std::string::npos);
    EXPECT_NE(rml.find("bg-purple-950"), std::string::npos);
    EXPECT_NE(rml.find("gray.50"), std::string::npos);
    EXPECT_NE(rml.find("purple.950"), std::string::npos);
    EXPECT_NE(rml.find("data-copy-hex=\"#F7F7F7\""), std::string::npos);
    EXPECT_NE(rml.find(">#120F18</button>"), std::string::npos);
    EXPECT_EQ(rml.find("style=\"background-color:"), std::string::npos);
}

TEST(UIUtilityTokensTests, BuildsTailwindStyleRcssFromUtilityAndPaletteTokens) {
    const std::vector<UIUtilityToken> utilityTokens = {
            {.name = "font-ui", .value = "Inter"},
            {.name = "font-alt", .value = "NovaMono"},
            {.name = "font-medium", .value = "500"},
            {.name = "text-sm", .value = "14dp"},
            {.name = "leading-sm", .value = "18dp"},
            {.name = "space-3", .value = "12dp"},
            {.name = "size-8", .value = "32dp"},
            {.name = "size-60", .value = "240dp"},
            {.name = "border-1", .value = "1dp"},
            {.name = "rounded-6", .value = "6dp"},
    };
    const std::vector<DebugPaletteColorToken> paletteColors = {
            {.name = "gray.50", .hex = "#F7F7F7"},
            {.name = "gray.500", .hex = "#3C3C3C"},
    };

    const std::string rcss = buildUIUtilityRcss(utilityTokens, paletteColors);

    EXPECT_NE(rcss.find(".font-ui { font-family: Inter; }"), std::string::npos);
    EXPECT_NE(rcss.find(".font-alt { font-family: NovaMono; }"), std::string::npos);
    EXPECT_NE(rcss.find(".font-medium { font-weight: 500; }"), std::string::npos);
    EXPECT_NE(rcss.find(".text-sm { font-size: 14dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".leading-sm { line-height: 18dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".px-3 { padding-left: 12dp; padding-right: 12dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".mr-3 { margin-right: 12dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".size-8 { width: 32dp; height: 32dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".w-8 { width: 32dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".h-8 { height: 32dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".min-w-60 { min-width: 240dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".max-h-60 { max-height: 240dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".border-1 { border-width: 1dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".rounded-6 { border-radius: 6dp; }"), std::string::npos);
    EXPECT_NE(rcss.find(".bg-gray-50 { background-color: #F7F7F7; }"), std::string::npos);
    EXPECT_NE(rcss.find(".hover-bg-gray-50:hover { background-color: #F7F7F7; }"), std::string::npos);
    EXPECT_NE(rcss.find(".active-bg-gray-50:active { background-color: #F7F7F7; }"), std::string::npos);
    EXPECT_NE(rcss.find(".text-gray-50 { color: #F7F7F7; }"), std::string::npos);
    EXPECT_NE(rcss.find(".border-gray-500 { border-color: #3C3C3C; }"), std::string::npos);

    const std::size_t hoverGray500 = rcss.find(".hover-bg-gray-500:hover { background-color: #3C3C3C; }");
    const std::size_t activeGray50 = rcss.find(".active-bg-gray-50:active { background-color: #F7F7F7; }");
    ASSERT_NE(hoverGray500, std::string::npos);
    ASSERT_NE(activeGray50, std::string::npos);
    EXPECT_LT(hoverGray500, activeGray50);
}

TEST(UIUtilityTokensTests, AppUITokensStayTailwindLikeAndDpBased) {
    const std::string tokenConfig = readSourceFile("resources/ui/tokens/app-ui.tokens.json");

    EXPECT_NE(tokenConfig.find("\"theme\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"colors\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"gray\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"purple\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"950\": \"#120F18\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"fontFamily\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"fontWeight\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"fontSize\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"lineHeight\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"spacing\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"borderRadius\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"sm\": \"14dp\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"xxs\": \"10dp\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"3\": \"12dp\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"11\": \"45dp\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"13\": \"50dp\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"px\": \"1dp\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"60\": \"240dp\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"6\": \"6dp\""), std::string::npos);
    EXPECT_NE(tokenConfig.find("\"medium\": 500"), std::string::npos);
    EXPECT_EQ(tokenConfig.find("\"w-24\""), std::string::npos);
    EXPECT_EQ(tokenConfig.find("\"h-6\""), std::string::npos);
    EXPECT_EQ(tokenConfig.find("\"typography\""), std::string::npos);
    EXPECT_EQ(tokenConfig.find("$type"), std::string::npos);
    EXPECT_EQ(tokenConfig.find("$value"), std::string::npos);
}

TEST(PlatformWindowChromeTests, HitTestSupportsResizeEdgesToolbarDragAndWindowControls) {
    PlatformWindowChromeHitTest hitTest{
            .windowWidth = 800,
            .windowHeight = 600,
            .resizeBorder = 6,
            .titleBarHeight = 48,
            .rightControlsWidth = 120,
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
    EXPECT_EQ(hit(400, 24), SDL_HITTEST_DRAGGABLE);
    EXPECT_EQ(hit(700, 24), SDL_HITTEST_NORMAL);
    EXPECT_EQ(hit(400, 60), SDL_HITTEST_NORMAL);

    hitTest.maximized = true;
    EXPECT_EQ(hit(0, 0), SDL_HITTEST_DRAGGABLE);
    EXPECT_EQ(hit(799, 599), SDL_HITTEST_NORMAL);
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

TEST(UISystemTests, RmlShellOwnsMainLayoutAndMeasuredViewportPanel) {
    const std::string uiSystemSource = readSourceFile("src/ui/UISystem.cpp");
    const std::string uiSystemHeader = readSourceFile("src/ui/UISystem.h");
    const std::string rmlHostSource = readSourceFile("src/ui/backends/rml/RmlUiContextHost.cpp");
    const std::string rmlHostHeader = readSourceFile("src/ui/backends/rml/RmlUiContextHost.h");
    const std::string viewportHostSource = readSourceFile("src/ui/controllers/ViewportHost.cpp");
    const std::string viewportComponentHeader = readSourceFile("src/ui/components/organisms/ViewportComponent.h");
    const std::string dockLayoutModelHeader = readSourceFile("src/ui/components/common/DockLayoutModel.h");
    const std::string mainLayoutModelHeader = readSourceFile("src/ui/components/organisms/MainLayoutModel.h");
    const std::string mainLayoutControllerSource = readSourceFile("src/ui/controllers/MainLayoutController.cpp");
    const std::string mainLayoutDockDragControllerHeader =
            readSourceFile("src/ui/controllers/MainLayoutDockDragController.h");
    const std::string mainLayoutDockDragControllerSource =
            readSourceFile("src/ui/controllers/MainLayoutDockDragController.cpp");
    const std::string mainLayoutSashRegistryHeader =
            readSourceFile("src/ui/controllers/MainLayoutSashRegistry.h");
    const std::string mainLayoutSashRegistrySource =
            readSourceFile("src/ui/controllers/MainLayoutSashRegistry.cpp");
    const std::string uiGeometryHeader = readSourceFile("src/ui/components/common/UIGeometry.h");
    const std::string uiDragDropHeader = readSourceFile("src/ui/components/common/UIDragDrop.h");
    const std::string uiTabStripModelHeader = readSourceFile("src/ui/components/common/UITabStripModel.h");
    const std::string uiDragDropControllerHeader = readSourceFile("src/ui/controllers/UIDragDropController.h");
    const std::string uiDragDropControllerSource = readSourceFile("src/ui/controllers/UIDragDropController.cpp");
    const std::string resizableSplitHeader = readSourceFile("src/ui/components/common/ResizableSplit.h");
    const std::string resizableSplitControllerSource = readSourceFile("src/ui/controllers/ResizableSplitController.cpp");
    const std::string uiButtonHeader = readSourceFile("src/ui/components/atoms/UIButtonComponent.h");
    const std::string uiChromeButtonHeader = readSourceFile("src/ui/components/atoms/UIChromeButtonComponent.h");
    const std::string uiToolbarButtonHeader = readSourceFile("src/ui/components/atoms/UIToolbarButtonComponent.h");
    const std::string windowChromeControlsHeader =
            readSourceFile("src/ui/components/organisms/WindowChromeControlsComponent.h");
    const std::string modelingModeShelfHeader =
            readSourceFile("src/ui/components/organisms/ModelingModeShelfComponent.h");
    const std::string mainLayoutBuilderSource = readSourceFile("src/ui/backends/rml/MainLayoutRmlBuilder.cpp");
    const std::string shellRml = readSourceFile("resources/ui/rml/viewport-shell.rml");
    const std::string shellRcss = readSourceFile("resources/ui/rcss/app-shell.rcss");
    const std::string cmakeSource = readSourceFile("CMakeLists.txt");

    EXPECT_NE(uiSystemHeader.find("RmlUiContextHost rmlHost_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("MainLayoutModel mainLayout_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("MainLayoutController mainLayoutController_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("ResizableSplitController mainLayoutResizeController_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("MainLayoutDockDragController mainLayoutDockDragController_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("MainLayoutSashRegistry mainLayoutSashRegistry_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("DropForbidden"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("mainLayoutLeftPx_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("mainLayoutTopPx_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("mainLayoutDropForbiddenCursor_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("mainLayoutWidthDp_"), std::string::npos);
    EXPECT_EQ(uiSystemHeader.find("mainLayoutSidebarHeightDp_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("ViewportHost viewportHost_"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("rmlHost_.initialize"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("buildWindowChromeControlsInnerRml()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("buildModelingModeShelfRml"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("updateModelingModeShelfOverflowFromRml"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modelingModeShelfControlMeasureElementId"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modelingModeShelfOverflowToggleControlId()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modelingModeShelfSelectionModeForControlId"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("pendingModelingModeShelfSelectionMode_"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("editor.selection().setMode(mode)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("editor.modeling().selectionSummary().mode"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("projectedModelingModeShelfSelectionMode_"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("modelingModeShelfTabs_"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("modelingModeShelfContentOpen_"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("kModelingModeShelfButtonControlId"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("ui.modeling_shelf.button"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("modelingModeShelfTabControlIds"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("buildMainLayoutRml(mainLayout_"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("measureElementById(\"main-layout-slot\")"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutEditorColumnElementId()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutSideDockElementId(location)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutSideDockResizerElementId(location)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("addSideDockWidthSash"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("kMainLayoutAuxiliarySidebarResizer"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("mainLayoutOtherVisibleSideDockWidthDp"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutSideDockVisibleOnSide"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("!mainLayoutSideDockVisibleOnSide(mainLayout_, MainLayoutSidebarSide::Left)"),
            std::string::npos);
    EXPECT_NE(uiSystemSource.find("!mainLayoutSideDockVisibleOnSide(mainLayout_, MainLayoutSidebarSide::Right)"),
            std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutPanelHeaderElementId(panelId)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutPanelElementId(panelId)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("setPanelDropZones"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("beginMainLayoutDockDragCandidate"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("drop->target.location"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("setBottomDockCollapsed(false)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("movePanelToDockLocation(drop->panelId"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("drop->target.targetPanelId"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("updateMainLayoutDockDragPreviewPosition"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("kMainLayoutDockDragPreviewHotspotOffsetPx"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mouseX - static_cast<float>(mainLayoutLeftPx_)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mouseY - static_cast<float>(mainLayoutTopPx_)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("target ? MainLayoutCursorKind::Default : MainLayoutCursorKind::DropForbidden"),
            std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutSashRegistry_.applyResize"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("setSidebarCollapsed(registration->location, true)"),
            std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportHost_.handleFrame"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("<rml>"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("LoadDocumentFromMemory"), std::string::npos);
    EXPECT_EQ(uiSystemHeader.find("RmlFilamentRenderer"), std::string::npos);
    EXPECT_EQ(uiSystemHeader.find("ViewportRenderer"), std::string::npos);

    EXPECT_NE(rmlHostSource.find("Rml::SetRenderInterface(&renderer_)"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("Rml::SetFileInterface(fileInterface_.get())"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("context_->LoadDocument(documentPath.string())"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("debugPaletteSlot->SetInnerRML(debugPaletteRml)"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("NovaMono.ttf"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("data-modeling-mode-shelf-control"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("data-modeling-mode-shelf-overflow-toggle"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("quader://ui/rcss/app-utility.generated.rcss"), std::string::npos);
    EXPECT_NE(rmlHostHeader.find("measureElementById"), std::string::npos);
    EXPECT_NE(rmlHostHeader.find("setElementInnerRml"), std::string::npos);
    EXPECT_NE(rmlHostHeader.find("takeModelingModeShelfControlSelection"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("eventWindowId(const SDL_Event& event)"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("SDL_GetWindowID(window_)"), std::string::npos);
    EXPECT_EQ(rmlHostHeader.find("RmlMainLayoutEvent"), std::string::npos);
    EXPECT_EQ(rmlHostSource.find("MainLayoutEventListener"), std::string::npos);

    EXPECT_EQ(viewportHostSource.find("workspace.setLayout(ViewportLayoutId::Single)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("renderer_.resize(mainViewportFrame_.width, mainViewportFrame_.height)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("mousePixel.x - static_cast<float>(mainViewportFrame_.left)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("renderer_.setSceneVisible(true)"), std::string::npos);
    EXPECT_NE(viewportComponentHeader.find("kMainViewportTextureSource = \"quader://viewport/main\""), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("enum class DockLocation"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("enum class DockPanelInsertPosition"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("struct DockLayoutModel"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("struct DockLocationActivePanel"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("struct DockLocationSplitPanels"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockMovePanelToLocation"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockMovePanelToLocationAtPanel"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockReorderPanels"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockActivePanelId"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockSetActivePanel"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockTabStripForLocation"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockSplitPanelsInLocation"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockSplitPanelWithPanel"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("panelSizesDp"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockSplitPanelSizeDp"), std::string::npos);
    EXPECT_NE(dockLayoutModelHeader.find("dockSetSplitPanelSizeDp"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("enum class MainLayoutSidebarSide"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("mainLayoutOppositeSidebarSide"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("mainLayoutSidebarSideForDockLocation"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("mainLayoutDockLocationForSidebarSide"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("mainLayoutSideDockWidthDp"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("kMainLayoutPanelOutliner = \"outliner\""), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("mainLayoutCanonicalPanelId"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("MainLayoutBottomDockModel bottomDock"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("mainLayoutBottomDockCollapsed"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("kMainLayoutBottomSplitPanelMinWidthDp"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("mainLayoutClampedBottomSplitPanelWidthDp"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("MainLayoutSideDockModel"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("MainLayoutSideDockModel sidebar"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("MainLayoutSideDockModel auxiliarySidebar"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("DockLayoutModel dockLayout"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("mainLayoutSideDockCollapsed"), std::string::npos);
    EXPECT_EQ(mainLayoutModelHeader.find("sidebarWidthDp"), std::string::npos);
    EXPECT_EQ(mainLayoutModelHeader.find("auxiliarySidebarWidthDp"), std::string::npos);
    EXPECT_EQ(mainLayoutModelHeader.find("sidebarCollapsed"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("outlinerVisible"), std::string::npos);
    EXPECT_NE(mainLayoutModelHeader.find("inspectorVisible"), std::string::npos);
    EXPECT_NE(mainLayoutControllerSource.find("resizeSidebar"), std::string::npos);
    EXPECT_NE(mainLayoutControllerSource.find("resizeOutlinerPanel"), std::string::npos);
    EXPECT_NE(mainLayoutControllerSource.find("setSidebarCollapsed"), std::string::npos);
    EXPECT_NE(mainLayoutControllerSource.find("setPanelVisible"), std::string::npos);
    EXPECT_NE(mainLayoutControllerSource.find("movePanelToDockLocation"), std::string::npos);
    EXPECT_NE(mainLayoutControllerSource.find("setActiveDockPanel"), std::string::npos);
    EXPECT_NE(mainLayoutControllerSource.find("splitPanelWithDockPanel"), std::string::npos);
    EXPECT_NE(mainLayoutControllerSource.find("resizeBottomSplitPanel"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("struct MainLayoutDockDragHitRegion"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("struct MainLayoutDockDropZone"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("struct MainLayoutPanelDropZone"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("enum class MainLayoutDockDropAction"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("MainLayoutDockDropAction action"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("UIDropPlacementPolicy placementPolicy"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("UIDropFeedbackKind feedbackKind"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("struct MainLayoutDockDropTarget"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("struct MainLayoutDockDropResult"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("UIDragDropController dragDrop_"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerSource.find("MainLayoutDockDragController::beginDragCandidate"),
            std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerSource.find("UIDropZoneKind::DockPanel"), std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerHeader.find("UIDropPlacementPolicy::VerticalBeforeAfter"),
            std::string::npos);
    EXPECT_NE(mainLayoutDockDragControllerSource.find("MainLayoutDockDragController::targetForDropTarget"),
            std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/controllers/MainLayoutDockDragController.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/controllers/MainLayoutSashRegistry.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/controllers/UIDragDropController.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/components/atoms/UIButtonComponent.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/components/atoms/UIChromeButtonComponent.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/components/atoms/UIToolbarButtonComponent.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/components/common/UITabStripModel.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/components/organisms/ModelingModeShelfComponent.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/components/organisms/WindowChromeControlsComponent.cpp"), std::string::npos);
    EXPECT_NE(cmakeSource.find("src/ui/core/UIIconRegistry.cpp"), std::string::npos);
    EXPECT_NE(uiGeometryHeader.find("struct UIPixelPoint"), std::string::npos);
    EXPECT_NE(uiGeometryHeader.find("struct UIPixelRect"), std::string::npos);
    EXPECT_NE(uiTabStripModelHeader.find("class UITabStripModel"), std::string::npos);
    EXPECT_NE(uiTabStripModelHeader.find("activateItem"), std::string::npos);
    EXPECT_NE(uiDragDropHeader.find("enum class UIDragDropItemKind"), std::string::npos);
    EXPECT_NE(uiDragDropHeader.find("enum class UIDropZoneKind"), std::string::npos);
    EXPECT_NE(uiDragDropHeader.find("DockSurface"), std::string::npos);
    EXPECT_NE(uiDragDropHeader.find("enum class UIDropPlacementPolicy"), std::string::npos);
    EXPECT_NE(uiDragDropHeader.find("struct UIDropZoneRegion"), std::string::npos);
    EXPECT_NE(uiDragDropControllerHeader.find("class UIDragDropController"), std::string::npos);
    EXPECT_NE(uiDragDropControllerSource.find("UIDragDropController::beginCandidate"), std::string::npos);
    EXPECT_NE(uiDragDropControllerSource.find("UIDragDropController::zoneAt"), std::string::npos);
    EXPECT_NE(uiDragDropControllerSource.find("UIDragDropController::sameItem"), std::string::npos);
    EXPECT_NE(uiDragDropControllerSource.find("UIDragDropController::placementForZone"), std::string::npos);
    EXPECT_EQ(uiDragDropHeader.find("MainLayout"), std::string::npos);
    EXPECT_EQ(uiDragDropControllerSource.find("MainLayout"), std::string::npos);
    EXPECT_NE(resizableSplitHeader.find("#include \"ui/components/common/UIGeometry.h\""), std::string::npos);
    EXPECT_NE(resizableSplitHeader.find("enum class ResizableSplitAxis"), std::string::npos);
    EXPECT_NE(resizableSplitHeader.find("enum class ResizableSplitSizeUnit"), std::string::npos);
    EXPECT_NE(resizableSplitHeader.find("struct ResizableSplitHandle"), std::string::npos);
    EXPECT_NE(resizableSplitHeader.find("struct ResizableSplitCollapseOptions"), std::string::npos);
    EXPECT_NE(resizableSplitHeader.find("oppositeMinSize"), std::string::npos);
    EXPECT_NE(resizableSplitHeader.find("containerSizeDp"), std::string::npos);
    EXPECT_NE(resizableSplitControllerSource.find("ResizableSplitController::inflatedHitRect"), std::string::npos);
    EXPECT_NE(resizableSplitControllerSource.find("ResizableSplitGrowthDirection::Positive"), std::string::npos);
    EXPECT_NE(resizableSplitControllerSource.find("effectiveMaxSizeDp"), std::string::npos);
    EXPECT_NE(uiButtonHeader.find("class UIButtonComponent : public UIComponent"), std::string::npos);
    EXPECT_NE(uiButtonHeader.find("UIButtonIconPlacement"), std::string::npos);
    EXPECT_NE(uiChromeButtonHeader.find("class UIChromeButtonComponent final : public UIButtonComponent"),
            std::string::npos);
    EXPECT_NE(uiToolbarButtonHeader.find("class UIToolbarButtonComponent : public UIButtonComponent"),
            std::string::npos);
    EXPECT_NE(uiToolbarButtonHeader.find("struct UIToolbarButtonModel"), std::string::npos);
    EXPECT_NE(modelingModeShelfHeader.find("class ModelingModeShelfComponent final : public UIComponent"),
            std::string::npos);
    EXPECT_NE(modelingModeShelfHeader.find("class ModelingModeShelfToolbarComponent final : public UIComponent"),
            std::string::npos);
    EXPECT_NE(modelingModeShelfHeader.find("struct ModelingModeShelfControl"), std::string::npos);
    EXPECT_NE(modelingModeShelfHeader.find("struct ModelingModeShelfCommand"), std::string::npos);
    EXPECT_NE(modelingModeShelfHeader.find("std::optional<UIIconId> icon"), std::string::npos);
    EXPECT_NE(modelingModeShelfHeader.find("ModelingModeShelfControlLabelVisibility"), std::string::npos);
    EXPECT_NE(modelingModeShelfHeader.find("overflowControlIds"), std::string::npos);
    EXPECT_NE(modelingModeShelfHeader.find("modelingModeShelfOverflowButtonElementId"), std::string::npos);
    EXPECT_NE(windowChromeControlsHeader.find("class WindowChromeControlsComponent final : public UIComponent"),
            std::string::npos);
    EXPECT_NE(mainLayoutSashRegistryHeader.find("class MainLayoutSashRegistry"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistryHeader.find("MainLayoutSashMeasuredLayout"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistryHeader.find("bottomDockTrackWidthDp"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistryHeader.find("MainLayoutBottomSplitPaneMeasurement"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("bottomSplitEffectiveAvailableWidthDp"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("bottomSplitCurrentLeadingWidthDp"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("addBottomPanelSplitSash"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("applyResize"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("normalizeMeasuredBounds"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("ResizableSplitGrowthDirection::Positive"), std::string::npos);
    EXPECT_NE(resizableSplitControllerSource.find("std::clamp"), std::string::npos);
    EXPECT_EQ(uiSystemHeader.find("struct MainLayoutResizeDrag"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("horizontalHitRect"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("verticalHitRect"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("buildMainLayoutRml"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("mainLayoutEditorColumnElementId"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("mainLayoutModelingModeShelfSlotElementId"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-editor-column"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("DockLocation::Panel"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("DockLocation::AuxiliaryBar"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-bottom-dock"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-bottom-dock-tab-strip"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-bottom-dock-content"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-bottom-dock-resizer"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-bottom-split-resizer"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("mainLayoutBottomSplitResizerElementId"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("bottom-split:"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-bottom-dock-drop-indicator"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-auxiliary-sidebar"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("mainLayoutSideDockElementId"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("mainLayoutSideDockResizerElementId"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("appendPanelDragHandle"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("appendPanelDropOverlay"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("appendBottomDockTab"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("appendBottomPanelBody"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("appendBottomSplitResizer"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("UITabStripModel::fromItems"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("dockSplitPanelsInLocation"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("dockSplitPanelSizeDp"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("appendDockDragPreview"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("MainLayoutDockDragPresentation"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("data-main-layout-drag-panel"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-sidebar-drop-indicator"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-panel-drop-overlay"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("main-layout-tab-drop-insertion-line"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("data-main-layout-tab-panel"), std::string::npos);
    EXPECT_EQ(mainLayoutBuilderSource.find("main-layout-panel-tabbar"), std::string::npos);
    EXPECT_EQ(mainLayoutBuilderSource.find("viewport-label"), std::string::npos);

    EXPECT_EQ(shellRml.find("id=\"topbar\""), std::string::npos);
    EXPECT_EQ(shellRml.find("topbar-label"), std::string::npos);
    EXPECT_NE(shellRml.find("id=\"toolbar\""), std::string::npos);
    EXPECT_EQ(shellRml.find("id=\"modeling-mode-shelf-slot\""), std::string::npos);
    EXPECT_NE(shellRml.find("id=\"main-layout-slot\""), std::string::npos);
    EXPECT_NE(shellRml.find("id=\"statusbar\""), std::string::npos);
    EXPECT_EQ(shellRml.find("shell-topbar"), std::string::npos);
    EXPECT_NE(shellRml.find("shell-toolbar h-12 bg-gray-800 border-gray-600"), std::string::npos);
    EXPECT_NE(shellRml.find("id=\"toolbar-content\" class=\"shell-toolbar-content pl-3\""), std::string::npos);
    EXPECT_NE(shellRml.find("id=\"chrome-window-controls\" class=\"chrome-window-controls\""), std::string::npos);
    EXPECT_EQ(shellRml.find("chrome-minimize-button"), std::string::npos);
    EXPECT_EQ(shellRml.find("chrome-restore-button"), std::string::npos);
    EXPECT_EQ(shellRml.find("chrome-close-button"), std::string::npos);
    EXPECT_EQ(shellRml.find("chrome-icon-button-icon"), std::string::npos);
    EXPECT_NE(shellRml.find("shell-statusbar h-10 px-3 bg-gray-900 border-gray-600"), std::string::npos);
    EXPECT_EQ(shellRml.find("id=\"dock-interaction-slot\""), std::string::npos);
    EXPECT_NE(shellRml.find("quader://ui/rcss/app-utility.generated.rcss"), std::string::npos);
    EXPECT_NE(shellRcss.find(".shell-toolbar-content"), std::string::npos);
    EXPECT_NE(shellRcss.find(".chrome-window-controls"), std::string::npos);
    EXPECT_NE(shellRcss.find(".chrome-icon-button"), std::string::npos);
    EXPECT_NE(shellRcss.find("width: 40dp"), std::string::npos);
    EXPECT_NE(shellRcss.find(".ui-icon"), std::string::npos);
    EXPECT_NE(shellRcss.find(".ui-icon-chrome-minimize"), std::string::npos);
    EXPECT_NE(shellRcss.find(".ui-icon-chrome-restore"), std::string::npos);
    EXPECT_NE(shellRcss.find(".ui-icon-chrome-close"), std::string::npos);
    EXPECT_NE(shellRcss.find("image-color: #5B5B5B"), std::string::npos);
    EXPECT_NE(shellRcss.find(".chrome-icon-button:hover .ui-icon"), std::string::npos);
    EXPECT_NE(shellRcss.find(".chrome-icon-button:active .ui-icon"), std::string::npos);
    EXPECT_NE(shellRcss.find(".toolbar-button"), std::string::npos);
    EXPECT_NE(shellRcss.find(".toolbar-button-secondary"), std::string::npos);
    EXPECT_NE(shellRcss.find("line-height: 30dp"), std::string::npos);
    EXPECT_NE(shellRcss.find(".ui-icon-mesh"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-slot"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-toolbar"), std::string::npos);
    EXPECT_EQ(shellRcss.find("height: 50dp"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-tab-button"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-tab-button-active"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-measure-strip"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-overflow-button"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-overflow-menu"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-content"), std::string::npos);
    EXPECT_EQ(shellRcss.find("height: 45dp"), std::string::npos);
    EXPECT_NE(shellRcss.find(".modeling-mode-shelf-command-button"), std::string::npos);
    EXPECT_NE(shellRcss.find("image-color: #D8D8D8"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-main-row"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-editor-column"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-bottom-dock"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-tab-strip"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-tab-active"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-tab-drop-insertion-line"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-bottom-dock-content"), std::string::npos);
    EXPECT_NE(shellRcss.find("width: 100%"), std::string::npos);
    EXPECT_NE(shellRcss.find("flex-direction: row"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-bottom-panel-pane"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-bottom-dock-resizer"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-bottom-split-resizer"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-bottom-dock-drop-indicator"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-sidebar"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-side-dock-left"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-side-dock-right"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-panel-header"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-panel-drag-source"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-panel-drop-overlay"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-dock-drag-preview"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-sidebar-drop-indicator"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-panel-resizer"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-resizer-drag-line"), std::string::npos);
    EXPECT_NE(shellRcss.find(".viewport-surface"), std::string::npos);
    EXPECT_EQ(shellRcss.find("#2E2E2E"), std::string::npos);
    EXPECT_EQ(shellRcss.find("#0F0F0F"), std::string::npos);
    EXPECT_EQ(shellRcss.find("#9A64D8"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("bg-gray-600"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("bg-purple-500"), std::string::npos);
    EXPECT_NE(shellRcss.find("transition: opacity 0.12s sine-out"), std::string::npos);
    EXPECT_NE(shellRcss.find("width: 3dp"), std::string::npos);
    EXPECT_NE(shellRcss.find("height: 3dp"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-sidebar-resizer-collapsed"), std::string::npos);
    EXPECT_NE(shellRcss.find(".main-layout-resizer-active .main-layout-resizer-drag-line"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("MainLayoutActiveResizer::Sidebar"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("MainLayoutActiveResizer::AuxiliarySidebar"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("MainLayoutActiveResizer::Panels"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("MainLayoutActiveResizer::BottomDock"), std::string::npos);
    EXPECT_NE(mainLayoutBuilderSource.find("MainLayoutActiveResizer::BottomSplitPanel"), std::string::npos);
    EXPECT_EQ(shellRcss.find(".dock-slot-preview"), std::string::npos);
    EXPECT_EQ(shellRcss.find(".dock-drag-panel"), std::string::npos);
    EXPECT_NE(shellRcss.find("#debug-palette-page"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("updateDockDragFeedback"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("std::vector<MainLayoutDockDropZone> dockDropZones"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("setDockDropZones(std::move(dockDropZones))"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutPanelTabElementId(panelId)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("setActiveDockPanel(DockLocation::Panel"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("MainLayoutDockDropAction::SplitPanel"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("splitPanelWithDockPanel"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("UIDropPlacementPolicy::HorizontalBeforeAfter"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("UIDropFeedbackKind::Insertion"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("trailingTargetPanelId"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistryHeader.find("MainLayoutBottomSplitPaneMeasurement"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("kBottomPanelSplitHandlePrefix"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutBottomDockContentElementId"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayoutBottomDockElementId"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("bottomDockTrackWidthDp"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("bottomSplitResizerHitRectFromPane"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("measureElementById(mainLayoutBottomSplitResizerElementId"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("bottomPanelBodySplitDropZoneAdded"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("mainLayoutBottomSplitPaneMeasurements_"), std::string::npos);
    EXPECT_NE(mainLayoutSashRegistrySource.find("resizeBottomSplitPanel"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("UIDropZoneKind"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("UIDropZoneRegion"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("UIDragDropController"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("dock-drop"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_SYSTEM_CURSOR_EW_RESIZE"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_SYSTEM_CURSOR_NS_RESIZE"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_SYSTEM_CURSOR_NOT_ALLOWED"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_CaptureMouse(true)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_EVENT_WINDOW_FOCUS_LOST"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("rmlConsumedEventBlocksViewport"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("setElementDisplay(\"viewport-panel\", debugPaletteVisible_ ? \"none\" : \"flex\")"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("Only the viewport panel is available in the RmlUi shell."), std::string::npos);
    EXPECT_NE(uiSystemSource.find("QuaderPreferencesRuntimeHooks"), std::string::npos);
}

TEST(UISystemTests, WindowedViewportComponentsAreBoundedAndNonExperimental) {
    const std::string appSource = readSourceFile("src/app/App.cpp");
    const std::string uiSystemSource = readSourceFile("src/ui/UISystem.cpp");
    const std::string uiSystemHeader = readSourceFile("src/ui/UISystem.h");
    const std::string viewportHostSource = readSourceFile("src/ui/controllers/ViewportHost.cpp");
    const std::string viewportHostHeader = readSourceFile("src/ui/controllers/ViewportHost.h");
    const std::string viewportComponentHeader = readSourceFile("src/ui/components/organisms/ViewportComponent.h");
    const std::string renderSystemHeader = readSourceFile("src/render/RenderSystem.h");
    const std::string renderSystemSource = readSourceFile("src/render/RenderSystem.cpp");

    EXPECT_NE(viewportComponentHeader.find("kMaxViewportComponents = 4u"), std::string::npos);
    EXPECT_NE(viewportComponentHeader.find("enum class ViewportComponentSurface"), std::string::npos);
    EXPECT_NE(viewportComponentHeader.find("windowedViewportComponent(std::uint8_t layerIndex)"), std::string::npos);
    EXPECT_NE(viewportHostHeader.find("struct ViewportWindowInstance"), std::string::npos);
    EXPECT_NE(viewportHostHeader.find("std::vector<std::unique_ptr<ViewportWindowInstance>> windowedViewports_"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("spawnWindowedViewport()"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("totalViewportCount = 1u + windowedViewports_.size()"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("totalViewportCount >= kMaxViewportComponents"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("nextWindowedViewportLayerIndex()"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("renderSystem_->createAdditionalSwapChain"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("SwapChainPresentationColorSpace::Srgb"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("ViewportRenderSurface::Swapchain"), std::string::npos);
    EXPECT_NE(renderSystemHeader.find("enum class SwapChainPresentationColorSpace"), std::string::npos);
    EXPECT_NE(renderSystemSource.find("filament::SwapChain::CONFIG_SRGB_COLORSPACE"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("renderWindowedViewports(RenderSystem& renderSystem)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportHost_.renderWindowedViewports(renderSystem)"), std::string::npos);
    EXPECT_NE(appSource.find("ui_.renderWindowedViewports(renderSystem_)"), std::string::npos);

    EXPECT_EQ(viewportHostSource.find("Experimental"), std::string::npos);
    EXPECT_EQ(viewportHostHeader.find("Experimental"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("renderDetachedViewports"), std::string::npos);
    EXPECT_EQ(uiSystemHeader.find("renderDetachedViewports"), std::string::npos);
}

TEST(UISystemTests, DebugPalettePageIsShiftF1ToggledAndCopyable) {
    const std::string uiSystemSource = readSourceFile("src/ui/UISystem.cpp");
    const std::string uiSystemHeader = readSourceFile("src/ui/UISystem.h");
    const std::string rmlHostSource = readSourceFile("src/ui/backends/rml/RmlUiContextHost.cpp");
    const std::string debugPaletteSource = readSourceFile("src/ui/DebugPaletteView.cpp");

    EXPECT_NE(uiSystemSource.find("SDLK_F1"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_KMOD_SHIFT"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toggleDebugPalette"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("rmlHost_.setElementDisplay(\"debug-palette-page\""), std::string::npos);
    EXPECT_EQ(debugPaletteSource.find("debug-keybind"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("loadUITokenConfig(appUITokenConfigPath(executableDir))"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("buildUIUtilityRcss(uiTokens.utilities, uiTokens.colors)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_GetWindowDisplayScale(window_)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("rmlHost_.setDensityIndependentPixelRatio(rmlDpRatio_)"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("SDL_SetClipboardText"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("Rml::LoadFontFace"), std::string::npos);
    EXPECT_NE(debugPaletteSource.find("debug-palette-swatch"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("debugPaletteVisible_"), std::string::npos);
    EXPECT_NE(uiSystemHeader.find("rmlDpRatio_"), std::string::npos);

}

TEST(ViewportSettingsTests, DefaultsMatchNativeViewportSettingsState) {
    const ViewportSettings settings = ViewportSettings::defaults();

    ASSERT_EQ(viewportSettingsSections().size(), 7u);
    EXPECT_EQ(viewportSettingsSections()[0].label, "Scene");
    EXPECT_TRUE(viewportSettingsSections()[0].defaultExpanded);
    EXPECT_EQ(viewportSettingsSections()[1].label, "Grid");
    EXPECT_EQ(viewportSettingsSections()[2].label, "Selection");
    EXPECT_EQ(viewportSettingsSections()[3].label, "Overlay");
    EXPECT_EQ(viewportSettingsSections()[4].label, "Vertices");
    EXPECT_EQ(viewportSettingsSections()[5].label, "Picking");
    EXPECT_EQ(viewportSettingsSections()[6].label, "Gizmo");

    expectColorNear(settings.color(ViewportColorSettingId::Background), {0.0509804f, 0.0509804f, 0.0509804f, 1.0f});
    expectColorNear(settings.color(ViewportColorSettingId::SelectionWire), overlayPolicySettingColor(kOverlaySelectionWireColor));
    expectColorNear(settings.color(ViewportColorSettingId::SourceWire), overlayPolicySettingColor(kOverlaySourceWireColor));
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::GridMinorLineSize), 0.32f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::SelectionEdgeLineSize), 2.5f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::PickVertexRadius), 0.3f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::GizmoSize), 1.0f);
    EXPECT_TRUE(viewportBoolSettings().empty());
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
    EXPECT_TRUE(settings.setColor(ViewportColorSettingId::Background, {0.25f, 0.5f, 0.75f, 0.6f}));
    EXPECT_TRUE(settings.setFloatValue(ViewportFloatSettingId::GizmoSize, 10.0f));
    ASSERT_TRUE(settings.save(path));

    const ViewportSettings loaded = ViewportSettings::load(path);
    expectColorNear(loaded.color(ViewportColorSettingId::Background), {0.25098f, 0.501961f, 0.74902f, 0.6f});
    EXPECT_FLOAT_EQ(loaded.floatValue(ViewportFloatSettingId::GizmoSize), 2.0f);

    {
        std::ifstream sourceFile(path);
        ASSERT_TRUE(sourceFile.is_open()) << path.string();
        const std::string source{
                std::istreambuf_iterator<char>(sourceFile),
                std::istreambuf_iterator<char>()};
        EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.BackgroundColor"), std::string::npos);
        EXPECT_NE(source.find("QVertexWorks.Quader.Viewport.GizmoSize"), std::string::npos);
        EXPECT_EQ(source.find("QVertexWorks.Quader.Viewport.ShowSpecialTextures"), std::string::npos);
    }

    std::filesystem::remove(path);
}

TEST(GroundGridMaterialTests, UsesUserWorldAnchoring) {
    const std::filesystem::path materialPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "materials" / "ground_grid.mat";
    std::ifstream materialFile(materialPath);
    ASSERT_TRUE(materialFile.is_open()) << materialPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(materialFile),
            std::istreambuf_iterator<char>()};

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
}

TEST(QuaderMeshMaterialTests, UsesOverlaySelectionInsteadOfStandaloneTint) {
    const std::filesystem::path materialPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "materials" / "quader_mesh.mat";
    std::ifstream materialFile(materialPath);
    ASSERT_TRUE(materialFile.is_open()) << materialPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(materialFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(source.find("selectionTint"), std::string::npos);
    EXPECT_EQ(source.find("getColor()"), std::string::npos);
    EXPECT_EQ(source.find("vertexColor"), std::string::npos);
    EXPECT_EQ(source.find("color,"), std::string::npos);
}

TEST(QuaderMeshMaterialTests, UsesUnlitTextureTransformTimesColor) {
    const std::filesystem::path materialPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "materials" / "quader_mesh.mat";
    std::ifstream materialFile(materialPath);
    ASSERT_TRUE(materialFile.is_open()) << materialPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(materialFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("shadingModel : unlit"), std::string::npos);
    EXPECT_EQ(source.find("doubleSided : true"), std::string::npos);
    EXPECT_NE(source.find("vec2 uv = vec2(getUV0().x, 1.0 - getUV0().y)"), std::string::npos);
    EXPECT_NE(source.find("texture(materialParams_baseColorTexture, uv)"), std::string::npos);
    EXPECT_NE(source.find("baseSample.rgb * materialParams.baseColorFactor.rgb"), std::string::npos);
    EXPECT_NE(source.find("material.baseColor = vec4(baseColor, baseSample.a * materialParams.baseColorFactor.a)"), std::string::npos);
    EXPECT_EQ(source.find("roughnessValue"), std::string::npos);
    EXPECT_EQ(source.find("mix(1.0, roughnessValue"), std::string::npos);
}

TEST(OverlayMaterialTests, PremultipliesVertexAlphaForTransparentFaceFill) {
    const std::filesystem::path materialPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "materials" / "vertex_color_overlay.mat";
    std::ifstream materialFile(materialPath);
    ASSERT_TRUE(materialFile.is_open()) << materialPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(materialFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("blending : transparent"), std::string::npos);
    EXPECT_NE(source.find("depthWrite : false"), std::string::npos);
    EXPECT_NE(source.find("vec4(vertexColor.rgb * vertexColor.a, vertexColor.a)"), std::string::npos);
}

TEST(OverlayMaterialTests, LineAndPointMaterialsUseScreenSpaceOverlayExpansion) {
    const std::filesystem::path lineMaterialPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "materials" / "quader_overlay_line.mat";
    std::ifstream lineMaterialFile(lineMaterialPath);
    ASSERT_TRUE(lineMaterialFile.is_open()) << lineMaterialPath.string();
    const std::string lineSource{
            std::istreambuf_iterator<char>(lineMaterialFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(lineSource.find("vertexDomain : device"), std::string::npos);
    EXPECT_NE(lineSource.find("requires : [\n        color,\n        custom0"), std::string::npos);
    EXPECT_NE(lineSource.find("material.lineDistancePixels = getCustom0()"), std::string::npos);
    EXPECT_EQ(lineSource.find("material.worldPosition"), std::string::npos);
    EXPECT_EQ(lineSource.find("getClipFromViewMatrix()"), std::string::npos);
    EXPECT_EQ(lineSource.find("name : nearPlane"), std::string::npos);
    EXPECT_NE(lineSource.find("variable_lineDistancePixels.x"), std::string::npos);

    const std::filesystem::path pointMaterialPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "materials" / "quader_overlay_point.mat";
    std::ifstream pointMaterialFile(pointMaterialPath);
    ASSERT_TRUE(pointMaterialFile.is_open()) << pointMaterialPath.string();
    const std::string pointSource{
            std::istreambuf_iterator<char>(pointMaterialFile),
            std::istreambuf_iterator<char>()};

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

TEST(ViewportOverlayPolicyTests, MatchesNativeLayerDrawOrder) {
    const std::array<ViewportOverlayLayer, kViewportOverlayLayerCount> expected{{
            ViewportOverlayLayer::SelectedVisibleFace,
            ViewportOverlayLayer::SelectedFaceMask,
            ViewportOverlayLayer::WireframeBase,
            ViewportOverlayLayer::SourceWire,
            ViewportOverlayLayer::SelectedXrayFace,
            ViewportOverlayLayer::HoverFace,
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
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::SourceWire, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::OpenEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::HoverFaceEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::HoverEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::UnselectedVertex, ViewportOverlayGeometryKind::Points));

    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::SelectedFaceEdge, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::SelectedEdge, ViewportOverlayGeometryKind::Lines),
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
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SourceWire, kOverlaySelectedVertexColor), kOverlaySelectionWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SourceWire, rgba8(255, 255, 255)), kOverlaySourceWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::HoverFace, kOverlayRemoveWireColor), kOverlayRemoveFaceColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::HoverEdge, kOverlayRemoveFaceColor), kOverlayRemoveWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::GroupBounds, rgba8(255, 255, 255)), kOverlayGroupBoundsColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolPlane, rgba8(255, 255, 255)), kOverlayToolPlaneColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolPlaneLine, rgba8(255, 255, 255)), kOverlayToolPlaneLineColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolHandle, rgba8(255, 255, 255)), kOverlayToolHandleColor);

    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::WireframeBase, 0.0f), 1.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::SourceWire, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::SelectedEdge, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::HoverEdge, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::OpenEdge, 2.0f), 2.0f);
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

TEST(ModelingSessionServiceOverlayTests, MapsRuntimeOverlayLayersExplicitly) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "projection" / "SelectionOverlayProjection.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(source.find("static_cast<ViewportOverlayLayer>(static_cast<int>(layer))"), std::string::npos);
    EXPECT_NE(source.find("case quader_editor_core::SelectionOverlayLayer::source_wire:"), std::string::npos);
    EXPECT_NE(source.find("case quader_editor_core::SelectionOverlayLayer::snap_target_vertex:"), std::string::npos);
    EXPECT_NE(source.find("case quader_editor_core::SelectionOverlayLayer::diagnostic_edge:"), std::string::npos);
    EXPECT_NE(source.find("lineStyleRole"), std::string::npos);
    EXPECT_NE(source.find("ViewportOverlayStyleRole::SelectedSourceWire"), std::string::npos);
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
    const std::filesystem::path bridgePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "SceneRenderBridge.cpp";
    const std::filesystem::path builderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayRenderableBuilder.cpp";
    const std::filesystem::path proxyFactoryPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayProxyFactory.cpp";
    const std::filesystem::path toolPreviewCachePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "ToolPreviewRenderableCache.cpp";
    std::ifstream bridgeFile(bridgePath);
    std::ifstream builderFile(builderPath);
    std::ifstream proxyFactoryFile(proxyFactoryPath);
    std::ifstream toolPreviewCacheFile(toolPreviewCachePath);
    ASSERT_TRUE(bridgeFile.is_open()) << bridgePath.string();
    ASSERT_TRUE(builderFile.is_open()) << builderPath.string();
    ASSERT_TRUE(proxyFactoryFile.is_open()) << proxyFactoryPath.string();
    ASSERT_TRUE(toolPreviewCacheFile.is_open()) << toolPreviewCachePath.string();

    const std::string bridgeSource{
            std::istreambuf_iterator<char>(bridgeFile),
            std::istreambuf_iterator<char>()};
    const std::string builderSource{
            std::istreambuf_iterator<char>(builderFile),
            std::istreambuf_iterator<char>()};
    const std::string proxyFactorySource{
            std::istreambuf_iterator<char>(proxyFactoryFile),
            std::istreambuf_iterator<char>()};
    const std::string toolPreviewCacheSource{
            std::istreambuf_iterator<char>(toolPreviewCacheFile),
            std::istreambuf_iterator<char>()};

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
    const std::filesystem::path proxyFactoryPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayProxyFactory.cpp";
    const std::filesystem::path lineBuilderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayLineMeshBuilder.cpp";
    const std::filesystem::path pointBuilderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayPointMeshBuilder.cpp";
    const std::filesystem::path renderableBuilderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayRenderableBuilder.h";
    const std::filesystem::path projectorPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayCameraProjector.cpp";
    std::ifstream proxyFactoryFile(proxyFactoryPath);
    std::ifstream lineBuilderFile(lineBuilderPath);
    std::ifstream pointBuilderFile(pointBuilderPath);
    std::ifstream renderableBuilderFile(renderableBuilderPath);
    std::ifstream projectorFile(projectorPath);
    ASSERT_TRUE(proxyFactoryFile.is_open()) << proxyFactoryPath.string();
    ASSERT_TRUE(lineBuilderFile.is_open()) << lineBuilderPath.string();
    ASSERT_TRUE(pointBuilderFile.is_open()) << pointBuilderPath.string();
    ASSERT_TRUE(renderableBuilderFile.is_open()) << renderableBuilderPath.string();
    ASSERT_TRUE(projectorFile.is_open()) << projectorPath.string();

    const std::string proxyFactorySource{
            std::istreambuf_iterator<char>(proxyFactoryFile),
            std::istreambuf_iterator<char>()};
    const std::string lineBuilderSource{
            std::istreambuf_iterator<char>(lineBuilderFile),
            std::istreambuf_iterator<char>()};
    const std::string pointBuilderSource{
            std::istreambuf_iterator<char>(pointBuilderFile),
            std::istreambuf_iterator<char>()};
    const std::string renderableBuilderSource{
            std::istreambuf_iterator<char>(renderableBuilderFile),
            std::istreambuf_iterator<char>()};
    const std::string projectorSource{
            std::istreambuf_iterator<char>(projectorFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(lineBuilderSource.find("appendLineQuad"), std::string::npos);
    EXPECT_NE(pointBuilderSource.find("appendPointQuad"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("overlayLineMaterial_->createInstance()"), std::string::npos);
    EXPECT_NE(proxyFactorySource.find("overlayPointMaterial_->createInstance()"), std::string::npos);
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
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "MeshRenderableFactory.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("tangentFrameFromNormal(vertex.normal)"), std::string::npos);
    EXPECT_NE(source.find("filament::VertexAttribute::TANGENTS"), std::string::npos);
    EXPECT_NE(source.find("renderPrimitiveSpansFor"), std::string::npos);
    EXPECT_NE(source.find("primitive.indexOffset"), std::string::npos);
    EXPECT_NE(source.find("primitive.indexCount"), std::string::npos);
    EXPECT_NE(source.find("primitive.materialSlot"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesAssetRevisionsForIncrementalMeshSync) {
    const std::filesystem::path syncSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "SceneRenderSyncService.cpp";
    const std::filesystem::path cacheSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "MeshRenderableCache.cpp";
    std::ifstream syncSourceFile(syncSourcePath);
    std::ifstream cacheSourceFile(cacheSourcePath);
    ASSERT_TRUE(syncSourceFile.is_open()) << syncSourcePath.string();
    ASSERT_TRUE(cacheSourceFile.is_open()) << cacheSourcePath.string();

    const std::string syncSource{
            std::istreambuf_iterator<char>(syncSourceFile),
            std::istreambuf_iterator<char>()};
    const std::string cacheSource{
            std::istreambuf_iterator<char>(cacheSourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(syncSource.find("syncedAssetRevision_"), std::string::npos);
    EXPECT_NE(syncSource.find("renderAssetRevisionsChanged"), std::string::npos);
    EXPECT_NE(syncSource.find("meshCache_->needsRebuild"), std::string::npos);
    EXPECT_NE(syncSource.find("materialBinding_->materialRevision"), std::string::npos);
    EXPECT_NE(cacheSource.find("existing->second.meshRevision != mesh.revision.value"), std::string::npos);
    EXPECT_NE(cacheSource.find("existing->second.materialRevision != materialRevision"), std::string::npos);
    EXPECT_NE(cacheSource.find("factory_->updateTransform(existing->second, worldTransform)"), std::string::npos);
    const std::size_t rebuildStart = syncSource.find("void SceneRenderSyncService::rebuild(");
    ASSERT_NE(rebuildStart, std::string::npos);
    const std::string rebuildSource = syncSource.substr(rebuildStart);
    EXPECT_NE(rebuildSource.find("meshCache_->removeExcept(liveMeshEntities)"), std::string::npos);
    EXPECT_EQ(rebuildSource.find("meshProxies_.clear()"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesAnisotropicSamplingForMeshTextures) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "MaterialBindingService.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("kMeshTextureAnisotropy"), std::string::npos);
    EXPECT_NE(source.find("sampler.setAnisotropy(kMeshTextureAnisotropy)"), std::string::npos);
    EXPECT_NE(source.find("filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR"), std::string::npos);
}

TEST(RenderSystemTests, ShadedViewportUsesClearColorWithoutSkybox) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "RenderSystem.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("renderer_->setClearOptions(clear)"), std::string::npos);
    EXPECT_NE(source.find("clear.clearColor = {environment.clearColor.x"), std::string::npos);
    EXPECT_EQ(source.find("scene_->setSkybox(skybox_)"), std::string::npos);
    EXPECT_EQ(source.find("Skybox::Builder()"), std::string::npos);
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
    const std::filesystem::path appOverlayPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "services" / "ModelingSessionServices.cpp";
    const std::filesystem::path runtimeOverlayPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "runtime" /
            "session_selection_overlay.cpp";
    std::ifstream appOverlayFile(appOverlayPath);
    std::ifstream runtimeOverlayFile(runtimeOverlayPath);
    ASSERT_TRUE(appOverlayFile.is_open()) << appOverlayPath.string();
    ASSERT_TRUE(runtimeOverlayFile.is_open()) << runtimeOverlayPath.string();

    const std::string appOverlay{
            std::istreambuf_iterator<char>(appOverlayFile),
            std::istreambuf_iterator<char>()};
    const std::string runtimeOverlay{
            std::istreambuf_iterator<char>(runtimeOverlayFile),
            std::istreambuf_iterator<char>()};

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
    const std::filesystem::path textureSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "TextureBindingService.cpp";
    const std::filesystem::path materialSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "MaterialBindingService.cpp";
    const std::filesystem::path factorySourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "MeshRenderableFactory.cpp";
    std::ifstream textureSourceFile(textureSourcePath);
    std::ifstream materialSourceFile(materialSourcePath);
    std::ifstream factorySourceFile(factorySourcePath);
    ASSERT_TRUE(textureSourceFile.is_open()) << textureSourcePath.string();
    ASSERT_TRUE(materialSourceFile.is_open()) << materialSourcePath.string();
    ASSERT_TRUE(factorySourceFile.is_open()) << factorySourcePath.string();

    const std::string textureSource{
            std::istreambuf_iterator<char>(textureSourceFile),
            std::istreambuf_iterator<char>()};
    const std::string materialSource{
            std::istreambuf_iterator<char>(materialSourceFile),
            std::istreambuf_iterator<char>()};
    const std::string factorySource{
            std::istreambuf_iterator<char>(factorySourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(textureSource.find("createSolidTexture(255, 255, 255, 255, TextureColorSpace::Srgb)"), std::string::npos);
    EXPECT_NE(textureSource.find("createSolidTexture(255, 255, 255, 255, TextureColorSpace::Linear)"), std::string::npos);
    EXPECT_NE(textureSource.find("texture.colorSpace == TextureColorSpace::Srgb ? filament::Texture::InternalFormat::SRGB8_A8 : filament::Texture::InternalFormat::RGBA8"), std::string::npos);
    EXPECT_NE(materialSource.find("MinFilter::LINEAR_MIPMAP_LINEAR"), std::string::npos);
    EXPECT_NE(textureSource.find("mipLevelCount(width, height)"), std::string::npos);
    EXPECT_NE(textureSource.find("Texture::Usage::GEN_MIPMAPPABLE"), std::string::npos);
    EXPECT_NE(textureSource.find("generateMipmaps(*engine_)"), std::string::npos);
    EXPECT_NE(materialSource.find("setCullingMode(doubleSided"), std::string::npos);
    EXPECT_NE(materialSource.find("filament::MaterialInstance::CullingMode::NONE"), std::string::npos);
    EXPECT_NE(materialSource.find(": filament::MaterialInstance::CullingMode::BACK"), std::string::npos);
    EXPECT_EQ(materialSource.find("setCullingMode(filament::MaterialInstance::CullingMode::BACK);"), std::string::npos);
    EXPECT_NE(factorySource.find("createMeshMaterialInstance(assets, material, doubleSided)"), std::string::npos);
    EXPECT_EQ(factorySource.find(".culling(!doubleSided)"), std::string::npos);
    EXPECT_NE(textureSource.find("TextureCacheKey cacheKey"), std::string::npos);
    EXPECT_NE(textureSource.find(".colorSpace = texture->colorSpace"), std::string::npos);
    EXPECT_EQ(textureSource.find("bool srgb"), std::string::npos);
    EXPECT_EQ(materialSource.find("bool srgb"), std::string::npos);
}

TEST(SceneRenderBridgeTests, SkipsOverlayProxyRebuildWhenOverlayPayloadIsUnchanged) {
    const std::filesystem::path cacheSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayRenderableCache.cpp";
    const std::filesystem::path cacheHeaderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayRenderableCache.h";
    const std::filesystem::path builderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayRenderableBuilder.cpp";
    std::ifstream cacheSourceFile(cacheSourcePath);
    std::ifstream cacheHeaderFile(cacheHeaderPath);
    std::ifstream builderFile(builderPath);
    ASSERT_TRUE(cacheSourceFile.is_open()) << cacheSourcePath.string();
    ASSERT_TRUE(cacheHeaderFile.is_open()) << cacheHeaderPath.string();
    ASSERT_TRUE(builderFile.is_open()) << builderPath.string();

    const std::string cacheSource{
            std::istreambuf_iterator<char>(cacheSourceFile),
            std::istreambuf_iterator<char>()};
    const std::string cacheHeader{
            std::istreambuf_iterator<char>(cacheHeaderFile),
            std::istreambuf_iterator<char>()};
    const std::string builderSource{
            std::istreambuf_iterator<char>(builderFile),
            std::istreambuf_iterator<char>()};

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
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "services" / "ModelingSessionService.cpp";
    const std::filesystem::path splitSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "services" / "ModelingSessionServices.cpp";
    const std::filesystem::path overlayHeaderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "services" / "ModelingOverlayService.h";
    const std::filesystem::path selectionHeaderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "modeling" / "services" / "ModelingSelectionService.h";
    std::ifstream sourceFile(sourcePath);
    std::ifstream splitSourceFile(splitSourcePath);
    std::ifstream overlayHeaderFile(overlayHeaderPath);
    std::ifstream selectionHeaderFile(selectionHeaderPath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(splitSourceFile.is_open()) << splitSourcePath.string();
    ASSERT_TRUE(overlayHeaderFile.is_open()) << overlayHeaderPath.string();
    ASSERT_TRUE(selectionHeaderFile.is_open()) << selectionHeaderPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string splitSource{
            std::istreambuf_iterator<char>(splitSourceFile),
            std::istreambuf_iterator<char>()};
    const std::string overlayHeader{
            std::istreambuf_iterator<char>(overlayHeaderFile),
            std::istreambuf_iterator<char>()};
    const std::string selectionHeader{
            std::istreambuf_iterator<char>(selectionHeaderFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(overlayHeader.find("SelectionOverlayCache selectionOverlayCache_"), std::string::npos);
    EXPECT_NE(splitSource.find("selectionOverlayCache_.matches("), std::string::npos);
    EXPECT_NE(source.find("contentRevision,\n                    selectionRevision,\n                    sceneRevision"), std::string::npos);
    EXPECT_NE(source.find("selectionMode,\n                    overlaySignature,\n                    hover"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayToolSignature()"), std::string::npos);
    EXPECT_NE(source.find("runtime_.cut_state()"), std::string::npos);
    EXPECT_NE(source.find("runtime_.mirror_state()"), std::string::npos);
    EXPECT_NE(splitSource.find("selectionOverlayCache_.frame()"), std::string::npos);
    EXPECT_NE(source.find("SelectionOverlayProjection::projectSelectionOverlayFrame("), std::string::npos);
    EXPECT_NE(splitSource.find("selectionOverlayCache_.store("), std::string::npos);
    EXPECT_NE(splitSource.find("selectionOverlayBaseFrame(session)"), std::string::npos);
    EXPECT_NE(overlayHeader.find("overlayBaseCache_"), std::string::npos);
    EXPECT_NE(splitSource.find("build_selection_hover_overlay_frame"), std::string::npos);
    EXPECT_NE(splitSource.find("appendSelectedHoverFilteredBaseFrame"), std::string::npos);
    EXPECT_NE(source.find("cachedPickObjectBounds()"), std::string::npos);
    EXPECT_NE(source.find("cachedPickFaceGeometry(pickOptions)"), std::string::npos);
    EXPECT_NE(splitSource.find("pickingCache_.faceGeometryEntries"), std::string::npos);
    EXPECT_NE(splitSource.find("PickingCacheProjection::objectFaceGeometrySignature(objects[index], pickOptions)"), std::string::npos);
    const std::filesystem::path pickingProjectionPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "projection" / "PickingCacheProjection.cpp";
    std::ifstream pickingProjectionFile(pickingProjectionPath);
    ASSERT_TRUE(pickingProjectionFile.is_open()) << pickingProjectionPath.string();
    const std::string pickingProjection{
            std::istreambuf_iterator<char>(pickingProjectionFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(pickingProjection.find("pickOptions.vertex_radius"), std::string::npos);
    EXPECT_NE(pickingProjection.find("pickOptions.edge_radius"), std::string::npos);
    EXPECT_NE(pickingProjection.find("pickOptions.has_hidden_material_slot"), std::string::npos);
    EXPECT_EQ(splitSource.find("(void)pickOptions;"), std::string::npos);
}

TEST(SceneRenderBridgeTests, ConvertsSceneOverlayColorsToLinearBeforeViewportEncode) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayFaceFillBuilder.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("rgba8SrgbToLinear(triangle.color)"), std::string::npos);
    EXPECT_NE(source.find("linearColor"), std::string::npos);
}

TEST(ViewportRendererTests, PresentsLinearViewportTextureThroughUiEncoder) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "ViewportRenderTarget.cpp";
    const std::filesystem::path gridSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "ViewportGridRenderer.cpp";
    std::ifstream sourceFile(sourcePath);
    std::ifstream gridSourceFile(gridSourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(gridSourceFile.is_open()) << gridSourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string gridSource{
            std::istreambuf_iterator<char>(gridSourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find(".format(Texture::InternalFormat::RGBA8)"), std::string::npos);
    EXPECT_NE(source.find("TextureSampleColorSpace::LinearSrgb"), std::string::npos);
    EXPECT_NE(gridSource.find("settings.color(ViewportColorSettingId::GridMinor)"), std::string::npos);
    EXPECT_NE(gridSource.find("settings.floatValue(ViewportFloatSettingId::GridMinorLineSize)"), std::string::npos);
    EXPECT_EQ(source.find(".format(Texture::InternalFormat::SRGB8_A8)"), std::string::npos);
}

TEST(ViewportRendererTests, MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame) {
    const std::filesystem::path rendererPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "ViewportRenderer.cpp";
    std::ifstream rendererFile(rendererPath);
    ASSERT_TRUE(rendererFile.is_open()) << rendererPath.string();
    const std::string rendererSource{
            std::istreambuf_iterator<char>(rendererFile),
            std::istreambuf_iterator<char>()};
    const std::filesystem::path gridRendererPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "ViewportGridRenderer.cpp";
    const std::filesystem::path coordinatorPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "ViewportRenderPassCoordinator.cpp";
    std::ifstream gridRendererFile(gridRendererPath);
    std::ifstream coordinatorFile(coordinatorPath);
    ASSERT_TRUE(gridRendererFile.is_open()) << gridRendererPath.string();
    ASSERT_TRUE(coordinatorFile.is_open()) << coordinatorPath.string();
    const std::string gridRendererSource{
            std::istreambuf_iterator<char>(gridRendererFile),
            std::istreambuf_iterator<char>()};
    const std::string coordinatorSource{
            std::istreambuf_iterator<char>(coordinatorFile),
            std::istreambuf_iterator<char>()};

    const std::filesystem::path appPath = std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "app" / "App.cpp";
    std::ifstream appFile(appPath);
    ASSERT_TRUE(appFile.is_open()) << appPath.string();
    const std::string appSource{std::istreambuf_iterator<char>(appFile), std::istreambuf_iterator<char>()};

    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UISystem.cpp";
    const std::filesystem::path viewportHostPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "controllers" / "ViewportHost.cpp";
    std::ifstream uiSystemFile(uiSystemPath);
    std::ifstream viewportHostFile(viewportHostPath);
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();
    ASSERT_TRUE(viewportHostFile.is_open()) << viewportHostPath.string();
    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};
    const std::string viewportHostSource{
            std::istreambuf_iterator<char>(viewportHostFile),
            std::istreambuf_iterator<char>()};

    const std::filesystem::path overlayCachePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayRenderableCache.cpp";
    const std::filesystem::path overlayProxyFactoryPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "OverlayProxyFactory.cpp";
    std::ifstream overlayCacheFile(overlayCachePath);
    std::ifstream overlayProxyFactoryFile(overlayProxyFactoryPath);
    ASSERT_TRUE(overlayCacheFile.is_open()) << overlayCachePath.string();
    ASSERT_TRUE(overlayProxyFactoryFile.is_open()) << overlayProxyFactoryPath.string();
    const std::string overlayCacheSource{std::istreambuf_iterator<char>(overlayCacheFile), std::istreambuf_iterator<char>()};
    const std::string overlayProxyFactorySource{std::istreambuf_iterator<char>(overlayProxyFactoryFile), std::istreambuf_iterator<char>()};

    const std::size_t renderStart = rendererSource.find("void ViewportRenderer::render");
    const std::size_t renderEnd = rendererSource.find("void ViewportRenderer::updateVisibleLayers");
    ASSERT_NE(renderStart, std::string::npos);
    ASSERT_NE(renderEnd, std::string::npos);
    const std::string renderFunction = rendererSource.substr(renderStart, renderEnd - renderStart);
    EXPECT_EQ(renderFunction.find("scene_->addEntity"), std::string::npos);
    EXPECT_EQ(renderFunction.find("scene_->remove"), std::string::npos);
    EXPECT_NE(rendererSource.find("gridRenderer_.update(controller, width(), height(), gridWorldSpacing, settings, visible)"), std::string::npos);
    EXPECT_NE(gridRendererSource.find(".layerMask(kViewportAllLayerMask, gridLayerMask_)"), std::string::npos);
    EXPECT_NE(rendererSource.find("renderPassCoordinator_.applyVisibleLayers(view_)"), std::string::npos);
    EXPECT_NE(rendererSource.find("renderPassCoordinator_.render(renderer, view_, renderTarget_.renderTarget())"), std::string::npos);
    EXPECT_NE(coordinatorSource.find("view->setVisibleLayers(kViewportAllLayerMask, visibleLayerMask())"), std::string::npos);

    const std::size_t prepareViewports =
            appSource.find("ui_.prepareViewports(*editor_, renderBridge_, projectSession_.scene(), projectSession_.assets())");
    const std::size_t prepareLayout =
            appSource.find("ui_.prepareLayout(window_.widthPixels(), window_.heightPixels())");
    const std::size_t prepareRenderData =
            appSource.find("ui_.prepareRenderData(window_.widthPixels(), window_.heightPixels())");
    const std::size_t beginFrame = appSource.find("renderSystem_.beginFrame()");
    ASSERT_NE(prepareViewports, std::string::npos);
    ASSERT_NE(prepareLayout, std::string::npos);
    ASSERT_NE(prepareRenderData, std::string::npos);
    ASSERT_NE(beginFrame, std::string::npos);
    EXPECT_LT(prepareLayout, prepareViewports);
    EXPECT_LT(prepareViewports, prepareRenderData);
    EXPECT_LT(prepareLayout, beginFrame);
    EXPECT_LT(prepareViewports, beginFrame);
    EXPECT_LT(prepareRenderData, beginFrame);

    EXPECT_NE(uiSystemSource.find("void UISystem::prepareLayout("), std::string::npos);
    EXPECT_NE(uiSystemSource.find("void UISystem::prepareViewports("), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportHost_.prepareViewports(editor, renderBridge, document, assets)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("ViewportWorkspaceService& workspace"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("const SceneDocument& document"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("const AssetRegistry& assets"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("renderer_.resize(mainViewportFrame_.width, mainViewportFrame_.height)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("renderer_.setSceneVisible(true)"), std::string::npos);
    EXPECT_NE(
            viewportHostSource.find("renderer_.setOverlayVisible(overlaySubmission.ok && overlaySubmission.value.visible)"),
            std::string::npos);
    EXPECT_EQ(viewportHostSource.find("renderBridge.setOverlayFrame("), std::string::npos);
    EXPECT_NE(viewportHostSource.find("editor.render().syncToolPreviews(renderBridge)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("editor.render().syncSelectionOverlay("), std::string::npos);
    EXPECT_NE(viewportHostSource.find("viewportGridLayerMask(mainViewport_.layerIndex)"), std::string::npos);
    EXPECT_NE(overlayProxyFactorySource.find(".layerMask(kViewportAllLayerMask, layerMask)"), std::string::npos);
    EXPECT_NE(overlayCacheSource.find("void OverlayRenderableCache::rebuildSlot(OverlaySlot& slot)"), std::string::npos);
}

TEST(RmlFilamentRendererTests, EncodesLinearViewportTexturesOnlyWhenRequested) {
    const std::filesystem::path rendererPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "RmlFilamentRenderer.cpp";
    std::ifstream rendererFile(rendererPath);
    ASSERT_TRUE(rendererFile.is_open()) << rendererPath.string();

    const std::string rendererSource{
            std::istreambuf_iterator<char>(rendererFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(rendererSource.find("textureSources_.find(source)"), std::string::npos);
    EXPECT_NE(rendererSource.find("RmlFilamentRenderer::registerTextureSource"), std::string::npos);
    EXPECT_NE(rendererSource.find("RmlFilamentRenderer::clearTextureSources"), std::string::npos);
    EXPECT_NE(rendererSource.find("isRegisteredTextureSourceHandle(handle)"), std::string::npos);
    EXPECT_EQ(rendererSource.find("kViewportTextureSource"), std::string::npos);
    EXPECT_NE(rendererSource.find("sampleColorSpace = textures_->sampleColorSpace(renderTextureHandle)"), std::string::npos);
    EXPECT_NE(rendererSource.find("sampleColorSpace == TextureSampleColorSpace::LinearSrgb"), std::string::npos);
    EXPECT_NE(rendererSource.find("setParameter(\"encodeToSrgb\", encodeToSrgb)"), std::string::npos);
    EXPECT_NE(rendererSource.find("GenerateTexture("), std::string::npos);
    EXPECT_NE(rendererSource.find("LoadTexture(Rml::Vector2i& textureDimensions"), std::string::npos);
    EXPECT_NE(rendererSource.find("Rml::GetFileInterface()"), std::string::npos);
    EXPECT_NE(rendererSource.find("fileInterface->LoadFile(source, encodedTexture)"), std::string::npos);
    EXPECT_NE(rendererSource.find("lodepng::decode("), std::string::npos);
    EXPECT_NE(rendererSource.find("premultiplyRgbaPixels(pixels)"), std::string::npos);
    EXPECT_NE(rendererSource.find("createOwnedTextureFromRgba("), std::string::npos);

    const std::filesystem::path materialPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "materials" / "rmlui.mat";
    std::ifstream materialFile(materialPath);
    ASSERT_TRUE(materialFile.is_open()) << materialPath.string();

    const std::string materialSource{
            std::istreambuf_iterator<char>(materialFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(materialSource.find("name : encodeToSrgb"), std::string::npos);
    EXPECT_NE(materialSource.find("vec3 linearToSrgb(vec3 value)"), std::string::npos);
    EXPECT_NE(materialSource.find("mix(sampled.rgb, linearToSrgb(sampled.rgb), clamp(materialParams.encodeToSrgb"), std::string::npos);
}

TEST(RmlFilamentRendererTests, PreservesRmlPremultipliedTextureAndVertexColorConventions) {
    const std::string rendererSource = readSourceFile("src/render/RmlFilamentRenderer.cpp");
    const std::string materialSource = readSourceFile("resources/materials/rmlui.mat");

    EXPECT_NE(materialSource.find("flipUV : false"), std::string::npos);
    EXPECT_NE(materialSource.find("material.baseColor = color;"), std::string::npos);
    EXPECT_EQ(materialSource.find("vec4(color.rgb * color.a, color.a)"), std::string::npos);
    EXPECT_EQ(rendererSource.find("translated.uv.y = 1.0f - translated.uv.y"), std::string::npos);
}

TEST(TextureRegistryTests, TracksPerTexturePresentationColorSpace) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "TextureRegistry.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string headerSource{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(headerSource.find("enum class TextureSampleColorSpace"), std::string::npos);
    EXPECT_NE(headerSource.find("DisplaySrgb"), std::string::npos);
    EXPECT_NE(headerSource.find("LinearSrgb"), std::string::npos);
    EXPECT_NE(headerSource.find("using RenderTextureHandle = std::uintptr_t"), std::string::npos);
    EXPECT_NE(headerSource.find("TextureSampleColorSpace sampleColorSpace(RenderTextureHandle id) const"), std::string::npos);
    EXPECT_NE(headerSource.find("void updateTexture(RenderTextureHandle id"), std::string::npos);

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "TextureRegistry.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find(".sampleColorSpace = sampleColorSpace"), std::string::npos);
    EXPECT_NE(source.find("TextureSampleColorSpace::DisplaySrgb : it->second.sampleColorSpace"), std::string::npos);
}

TEST(UISystemTests, RmlViewportShellCapturesFlyNavigationInput) {
    const std::string appSource = readSourceFile("src/app/App.cpp");
    const std::string uiSystemSource = readSourceFile("src/ui/UISystem.cpp");
    const std::string viewportHostSource = readSourceFile("src/ui/controllers/ViewportHost.cpp");
    const std::string rmlHostSource = readSourceFile("src/ui/backends/rml/RmlUiContextHost.cpp");

    const std::size_t prepareLayout = appSource.find("ui_.prepareLayout(window_.widthPixels(), window_.heightPixels())");
    const std::size_t draw = appSource.find("ui_.draw(*editor_");
    ASSERT_NE(prepareLayout, std::string::npos);
    ASSERT_NE(draw, std::string::npos);
    EXPECT_LT(prepareLayout, draw);
    EXPECT_NE(viewportHostSource.find("SDL_SetWindowRelativeMouseMode(window_, captured)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("SDL_GetRelativeMouseState(&deltaX, &deltaY)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("SDL_GetRelativeMouseState(&discardX, &discardY)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("ViewportInputFrame input = buildViewportInputFrame()"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("resolveKeybindingsForFrame(editor, inputRouter_.state(), input)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("inputRouter_.handleFrame(input, camera)"), std::string::npos);
    const std::size_t activeInputResolution =
            viewportHostSource.find("for (const QuaderInputChord& chord : input.activeInputChords)");
    const std::size_t pressedInputResolution =
            viewportHostSource.find("for (const QuaderInputChord& chord : input.pressedInputChords)");
    ASSERT_NE(activeInputResolution, std::string::npos);
    ASSERT_NE(pressedInputResolution, std::string::npos);
    EXPECT_LT(activeInputResolution, pressedInputResolution);
    EXPECT_NE(viewportHostSource.find("setCursorCaptured(result.flyCursorCaptureActive)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("mouseDragChordsDown_[*mouseButton] = dragChord"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("appendActiveMouseDragChords(mouseDragChordsDown_, input.activeInputChords)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("mainViewportContainsWindowPoint(event.button.x, event.button.y)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("middlePressed_ = middlePressed_ || startedInMainViewport"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("rightPressed_ = rightPressed_ || startedInMainViewport"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("context_->SetDimensions"), std::string::npos);
    EXPECT_NE(rmlHostSource.find("context_->Render()"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("renderer_.resize(mainViewportFrame_.width, mainViewportFrame_.height)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("input.mouseLocal = mouseLocal"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("rmlHost_.registerTextureSource("), std::string::npos);
}

TEST(UISystemTests, RmlViewportShellUsesWorkspaceSinglePanePolicyForV1) {
    const std::string uiSystemSource = readSourceFile("src/ui/UISystem.cpp");
    const std::string viewportHostSource = readSourceFile("src/ui/controllers/ViewportHost.cpp");

    EXPECT_EQ(viewportHostSource.find("workspace.setLayout(ViewportLayoutId::Single)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("workspace.focusPane(mainViewport_.pane)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("workspace.clearPaneVisibility()"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("workspace.setPaneVisible(mainViewport_.pane, true)"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("mainMenuBar_.draw"), std::string::npos);
}

TEST(UISystemTests, RmlViewportShellLoadsViewportAndMainLayoutSettings) {
    const std::string uiSystemSource = readSourceFile("src/ui/UISystem.cpp");
    const std::string viewportHostSource = readSourceFile("src/ui/controllers/ViewportHost.cpp");
    const std::string uiSettingsSource = readSourceFile("src/ui/UISettings.cpp");

    EXPECT_NE(uiSystemSource.find("settingsPaths_ = UISettings::resolvePaths(executableDir)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("uiSettings_ = UISettings::load(settingsPaths_.settingsFile)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainLayout_ = uiSettings_.mainLayout()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportSettings_ = ViewportSettings::load(settingsPaths_.viewportSettingsFile)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("saveViewportSettings()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("saveUISettings()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("uiSettings_.setMainLayout(mainLayout_)"), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("SidebarSide="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("SidebarWidthDp="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("AuxiliarySidebarWidthDp="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("SidebarCollapsed="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("AuxiliarySidebarCollapsed="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("OutlinerHeightDp="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("BottomDockHeightDp="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("OutlinerVisible="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("InspectorVisible="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("OutlinerDockLocation="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("InspectorDockLocation="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("DockPanelOrder="), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("mainLayoutSidebarSideFromName(value)"), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("dockLocationFromName(value)"), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("dockReorderPanels"), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("mainLayoutClampedSidebarWidthDp"), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("mainLayoutClampedOutlinerHeightDp"), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("mainLayoutClampedBottomDockHeightDp"), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("normalizedMainLayoutModel(std::move(layout))"), std::string::npos);
    EXPECT_EQ(uiSettingsSource.find("DockLayoutJson"), std::string::npos);
    EXPECT_EQ(viewportHostSource.find("renderBridge.setOverlayFrame("), std::string::npos);
    EXPECT_NE(viewportHostSource.find("editor.render().syncToolPreviews(renderBridge)"), std::string::npos);
    EXPECT_NE(viewportHostSource.find("editor.render().syncSelectionOverlay("), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("settingsPanelOpen_"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("toolSettingsPanelOpen_"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("viewportSettingsPanel_.draw"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("toolSettingsPanel_.draw"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("toolSettingsPanelOpenRequested"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("requestToolSettingsPanel"), std::string::npos);
}

TEST(AppStartupTests, DoesNotCreateDefaultModelingMesh) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "app" / "App.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(source.find("createBoxFromBounds({"), std::string::npos);
    EXPECT_EQ(source.find("Startup scene created modeling box"), std::string::npos);
    EXPECT_EQ(source.find("startup box"), std::string::npos);
    EXPECT_EQ(source.find("Fake Directional Light"), std::string::npos);
    EXPECT_EQ(source.find("document_.setLight"), std::string::npos);
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
    EXPECT_TRUE(scene.exists(sphere));
    EXPECT_TRUE(scene.enttEntity(sphere) != entt::null);
    EXPECT_EQ(scene.entityId(scene.enttEntity(sphere)), sphere);
    ASSERT_NE(scene.name(sphere), nullptr);
    EXPECT_EQ(scene.name(sphere)->name, "Box");
    ASSERT_NE(scene.mesh(sphere), nullptr);
    EXPECT_EQ(scene.mesh(sphere)->mesh, 11u);
    EXPECT_TRUE(scene.mesh(sphere)->doubleSided);
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

TEST(ViewportWorkspaceServiceTests, PaneStateDoesNotStoreDuplicateView) {
    const std::string header = readSourceFile("src/viewport/ViewportWorkspaceService.h");
    const std::string source = readSourceFile("src/viewport/ViewportWorkspaceService.cpp");

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

    EXPECT_NEAR(camera.flySpeed(), 15.0f, 0.001f);
    camera.adjustFlySpeed(1.0f);
    EXPECT_NEAR(camera.flySpeed(), 16.0f, 0.001f);

    camera.flyMove({0.0f, 0.0f, 1.0f}, 0.25f);
    const ViewportCameraPose movedPose = camera.pose();
    const QVec3 movedDelta = movedPose.eye - lookPose.eye;

    EXPECT_GT(dot(movedDelta, lookForward), 3.7f);
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

    input.pressedKeys = {ViewportKey::B};
    auto result = router.handleFrame(input, camera);
    EXPECT_EQ(result.shortcutHandled, viewportToolHasNativeController(ViewportTool::Box));
    EXPECT_EQ(router.state().activeTool,
            viewportToolHasNativeController(ViewportTool::Box) ? ViewportTool::Box : ViewportTool::Select);

    input.pressedKeys = {ViewportKey::Q};
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Select);

    input.pressedKeys = {ViewportKey::W};
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Move);

    input.pressedKeys = {ViewportKey::R};
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Rotate);

    input.pressedKeys = {ViewportKey::S};
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Scale);
}

TEST(ViewportInputRouterTests, ResolvedNavigationBindingsDriveMouseNavigation) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.shortcutResolverActive = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {200.0f, 150.0f};
    input.middlePressed = true;
    input.middleDown = true;

    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.navigationActive);
    EXPECT_FALSE(result.consumed);

    input.middlePressed = false;
    input.middleDown = false;
    input.navigationBindings.orbitPressed = true;
    input.navigationBindings.orbitDown = true;
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.navigationActive);
    EXPECT_TRUE(result.consumed);

    input.navigationBindings = {};
    input.navigationBindings.orbitDown = true;
    input.mouseScreen = {220.0f, 165.0f};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_TRUE(result.navigationActive);

    input.navigationBindings = {};
    input.navigationBindings.orbitReleased = true;
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.navigationActive);
    EXPECT_TRUE(result.consumed);
}

TEST(ViewportInputRouterTests, ResolvedWheelBindingsDriveViewportZoom) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.shortcutResolverActive = true;
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
    input.shortcutResolverActive = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    input.navigationBindings.flyPressed = true;
    input.navigationBindings.flyDown = true;

    ViewportInputResult result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    input.navigationBindings = {};
    input.navigationBindings.flyDown = true;
    input.navigationBindings.orbitPressed = true;
    input.navigationBindings.orbitDown = true;
    input.navigationBindings.resetFlySpeedPressed = true;
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flySpeedChanged);
    EXPECT_TRUE(result.flySpeedReset);
    EXPECT_NEAR(camera.flySpeed(), 15.0f, 0.001f);
    EXPECT_TRUE(camera.flySpeedIsDefault());
}

TEST(ViewportInputRouterTests, ResolvedFlyNavigationKeepsWasdMovementAheadOfToolShortcuts) {
    ViewportInputRouter router;
    ViewportCameraController camera;

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.shortcutResolverActive = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    input.navigationBindings.flyPressed = true;
    input.navigationBindings.flyDown = true;

    ViewportInputResult result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    input.navigationBindings = {};
    input.navigationBindings.flyDown = true;
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
    input.shortcutResolverActive = true;
    input.viewportSize = {800.0f, 600.0f};
    input.mouseScreen = {300.0f, 240.0f};
    input.navigationBindings.flyPressed = true;
    input.navigationBindings.flyDown = true;

    ViewportInputResult result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    input.navigationBindings = {};
    input.navigationBindings.flyDown = true;
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

    input.modifiers.shift = true;
    input.pressedKeys = {ViewportKey::R};
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::InsertEdgeLoop);

    input.pressedKeys = {ViewportKey::C};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Cut);

    input.modifiers.shift = false;
    input.pressedKeys = {ViewportKey::C};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Knife);

    input.pressedKeys = {ViewportKey::P};
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
        ViewportKey key;
        ViewportModifiers modifiers;
        ViewportShortcutKind kind;
        ViewportShortcutContext context;
        ViewportMirrorAxis mirrorAxis = ViewportMirrorAxis::X;
        bool checkMirrorAxis = false;
    };

    const ViewportModifiers primary{.primary = true};
    const ViewportModifiers primaryShift{.primary = true, .shift = true};
    const ViewportModifiers shift{.shift = true};
    const std::array<ShortcutCase, 21> shortcuts{{
            {"Ctrl+A select all", ViewportKey::A, primary, ViewportShortcutKind::SelectAll, ViewportShortcutContext::Global},
            {"Ctrl+I invert selection", ViewportKey::I, primary, ViewportShortcutKind::InvertSelection, ViewportShortcutContext::Global},
            {"Delete delete selection", ViewportKey::Delete, {}, ViewportShortcutKind::DeleteSelection, ViewportShortcutContext::Global},
            {"Backspace delete selection", ViewportKey::Backspace, {}, ViewportShortcutKind::DeleteSelection, ViewportShortcutContext::Global},
            {"Ctrl+D duplicate", ViewportKey::D, primary, ViewportShortcutKind::Duplicate, ViewportShortcutContext::Global},
            {"Ctrl+C copy", ViewportKey::C, primary, ViewportShortcutKind::Copy, ViewportShortcutContext::Global},
            {"Ctrl+V paste", ViewportKey::V, primary, ViewportShortcutKind::Paste, ViewportShortcutContext::Global},
            {"Ctrl+Z undo", ViewportKey::Z, primary, ViewportShortcutKind::Undo, ViewportShortcutContext::Global},
            {"Ctrl+Y redo", ViewportKey::Y, primary, ViewportShortcutKind::Redo, ViewportShortcutContext::Global},
            {"Ctrl+Shift+Z redo", ViewportKey::Z, primaryShift, ViewportShortcutKind::Redo, ViewportShortcutContext::Global},
            {"Ctrl+R repeat", ViewportKey::R, primary, ViewportShortcutKind::RepeatLastAction, ViewportShortcutContext::Global},
            {"M merge active", ViewportKey::M, {}, ViewportShortcutKind::MergeActive, ViewportShortcutContext::Viewport},
            {"Shift+M merge center", ViewportKey::M, shift, ViewportShortcutKind::MergeCenter, ViewportShortcutContext::Viewport},
            {"X delete popup", ViewportKey::X, {}, ViewportShortcutKind::DeletePopup, ViewportShortcutContext::Viewport},
            {"Shift+X mirror X", ViewportKey::X, shift, ViewportShortcutKind::MirrorAxis, ViewportShortcutContext::Viewport, ViewportMirrorAxis::X, true},
            {"Shift+Y mirror Y", ViewportKey::Y, shift, ViewportShortcutKind::MirrorAxis, ViewportShortcutContext::Viewport, ViewportMirrorAxis::Y, true},
            {"Shift+Z mirror Z", ViewportKey::Z, shift, ViewportShortcutKind::MirrorAxis, ViewportShortcutContext::Viewport, ViewportMirrorAxis::Z, true},
            {"F flip mesh normals", ViewportKey::F, {}, ViewportShortcutKind::FlipMeshNormals, ViewportShortcutContext::Viewport},
            {"Shift+H flip horizontal", ViewportKey::H, shift, ViewportShortcutKind::FlipHorizontal, ViewportShortcutContext::Viewport},
            {"Shift+V flip vertical", ViewportKey::V, shift, ViewportShortcutKind::FlipVertical, ViewportShortcutContext::Viewport},
            {"Shift+T apply material", ViewportKey::T, shift, ViewportShortcutKind::ApplyMaterial, ViewportShortcutContext::Viewport},
    }};

    for (const ShortcutCase& shortcutCase : shortcuts) {
        SCOPED_TRACE(shortcutCase.label);
        ViewportInputRouter router;
        ViewportCameraController camera;
        ViewportInputFrame input;
        input.viewportFocused = true;
        input.viewportSize = {800.0f, 600.0f};
        input.modifiers = shortcutCase.modifiers;
        input.pressedKeys = {shortcutCase.key};

        const auto result = router.handleFrame(input, camera);

        EXPECT_TRUE(result.shortcutHandled);
        EXPECT_TRUE(result.consumed);
        EXPECT_EQ(result.shortcut.kind, shortcutCase.kind);
        EXPECT_EQ(result.shortcut.context, shortcutCase.context);
        if (shortcutCase.checkMirrorAxis) {
            EXPECT_EQ(result.shortcut.mirrorAxis, shortcutCase.mirrorAxis);
        }
    }
}

TEST(ViewportInputRouterTests, FlipShortcutsOnlyApplyInObjectSelectionMode) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.modifiers.shift = true;

    router.setSelectionMode(ViewportSelectionMode::Face);
    input.modifiers.shift = false;
    input.pressedKeys = {ViewportKey::F};
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.consumed);

    input.modifiers.shift = true;
    input.pressedKeys = {ViewportKey::H};
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.consumed);

    router.setSelectionMode(ViewportSelectionMode::Edge);
    input.pressedKeys = {ViewportKey::V};
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.consumed);

    router.setSelectionMode(ViewportSelectionMode::Object);
    input.modifiers.shift = false;
    input.pressedKeys = {ViewportKey::F};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FlipMeshNormals);

    input.modifiers.shift = true;
    input.pressedKeys = {ViewportKey::H};
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

    input.pressedKeys = {ViewportKey::Number1};
    router.handleFrame(input, camera);
    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Vertex);

    input.pressedKeys = {ViewportKey::Number2};
    router.handleFrame(input, camera);
    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Edge);

    input.pressedKeys = {ViewportKey::Number3};
    router.handleFrame(input, camera);
    EXPECT_EQ(router.state().selectionMode, ViewportSelectionMode::Face);

    input.pressedKeys = {ViewportKey::Number4};
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

    input.pressedKeys = {ViewportKey::F7};
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_TRUE(result.gridPresetChanged);
    EXPECT_EQ(router.state().gridPreset, 7);

    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_FALSE(result.gridPresetChanged);
    EXPECT_EQ(router.state().gridPreset, 7);
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
    input.pressedKeys = {ViewportKey::F};
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.focusDocumentRequested);

    input.pressedKeys = {ViewportKey::Space};
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.maximizedToggleRequested);

    router.setActiveTool(ViewportTool::Mirror);
    input.pressedKeys = {ViewportKey::Space};
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
    input.pressedKeys = {ViewportKey::Space};

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
    input.pressedKeys = {ViewportKey::B};

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
    input.pressedKeys = {ViewportKey::Escape};

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
    input.pressedKeys = {ViewportKey::Home};

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_TRUE(result.focusDocumentRequested);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FocusDocument);

    input.pressedKeys = {ViewportKey::Home};
    input.modifiers.shift = true;
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
    input.viewportSize = {800.0f, 600.0f};
    input.middlePressed = true;
    input.middleDown = true;
    input.mouseScreen = {100.0f, 100.0f};

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.middlePressed = false;
    input.mouseScreen = {160.0f, 120.0f};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.cameraChanged);

    input.middleDown = false;
    input.middleReleased = true;
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
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.middlePressed = true;
    input.middleDown = true;
    input.modifiers.alt = true;
    input.mouseScreen = {100.0f, 100.0f};

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    const ViewportCameraPose before = camera.pose();
    input.middlePressed = false;
    input.mouseScreen = {120.0f, 110.0f};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_LT(dot(normalize(camera.pose().target - camera.pose().eye), normalize(before.target - before.eye)), 0.9999f);

    input.middleDown = false;
    input.middleReleased = true;
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
    input.middlePressed = true;
    input.middleDown = true;
    input.mouseScreen = {100.0f, 100.0f};

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    ASSERT_TRUE(router.navigating());
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);

    input.middlePressed = false;
    input.modifiers.alt = true;
    input.mouseScreen = {100.0f, 100.0f};
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
    input.middlePressed = true;
    input.middleDown = true;
    input.modifiers.alt = true;
    input.mouseScreen = {100.0f, 100.0f};

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    input.middlePressed = false;
    result = router.handleFrame(input, camera);
    ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    ASSERT_FALSE(camera.orthographicViewLocked());

    input.middleDown = false;
    input.middleReleased = true;
    result = router.handleFrame(input, camera);
    ASSERT_FALSE(router.navigating());

    input = {};
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.middlePressed = true;
    input.middleDown = true;
    input.mouseScreen = {100.0f, 100.0f};
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
    input.middlePressed = true;
    input.middleDown = true;
    input.modifiers.alt = true;
    input.mouseScreen = {100.0f, 100.0f};

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    input.middlePressed = false;
    result = router.handleFrame(input, camera);
    ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    ASSERT_FALSE(camera.orthographicViewLocked());

    input.middleDown = false;
    input.middleReleased = true;
    result = router.handleFrame(input, camera);
    ASSERT_FALSE(router.navigating());

    input = {};
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.rightPressed = true;
    input.rightDown = true;
    input.mouseScreen = {300.0f, 240.0f};
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
    input.middlePressed = true;
    input.middleDown = true;
    input.mouseScreen = {100.0f, 100.0f};

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.middlePressed = false;
    input.mouseScreen = {180.0f, 140.0f};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(result.cameraChanged);
    EXPECT_NEAR(camera.state().target.x, before.target.x, 0.001f);
    EXPECT_NEAR(camera.state().target.y, before.target.y, 0.001f);
    EXPECT_NEAR(camera.state().target.z, before.target.z, 0.001f);
    EXPECT_NEAR(camera.state().yawRadians, before.yawRadians, 0.001f);
    EXPECT_NEAR(camera.state().pitchRadians, before.pitchRadians, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);

    input.middleDown = false;
    input.middleReleased = true;
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
    input.middlePressed = true;
    input.middleDown = true;
    input.modifiers.shift = true;
    input.mouseScreen = {100.0f, 100.0f};

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.middlePressed = false;
    input.mouseScreen = {180.0f, 140.0f};
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
    input.rightPressed = true;
    input.rightDown = true;
    input.mouseScreen = {240.0f, 180.0f};

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());

    input.rightPressed = false;
    input.mouseScreen = {310.0f, 210.0f};
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    input.deltaSeconds = 0.25f;
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_GT(length(camera.state().target - before.target), 0.001f);
    EXPECT_NEAR(camera.state().yawRadians, before.yawRadians, 0.001f);
    EXPECT_NEAR(camera.state().pitchRadians, before.pitchRadians, 0.001f);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Orthographic);

    input.rightDown = false;
    input.rightReleased = true;
    input.flyLocalDirection = {};
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
    input.rightPressed = true;
    input.rightDown = true;
    input.mouseScreen = {300.0f, 240.0f};

    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(router.navigating());
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flyCursorCaptureActive);
    EXPECT_FALSE(result.flyHudActive);

    const ViewportCameraPose beforeMove = camera.pose();
    input.rightPressed = false;
    input.mouseDelta = {40.0f, 0.0f};
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    input.deltaSeconds = 0.25f;
    input.pressedKeys = {ViewportKey::W};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.cameraChanged);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flyCursorCaptureActive);
    EXPECT_TRUE(result.flyHudActive);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Select);
    EXPECT_GT(length(camera.pose().eye - beforeMove.eye), 1.0f);

    input.flyLocalDirection = {};
    input.pressedKeys = {};
    input.mouseDelta = {};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flyCursorCaptureActive);
    EXPECT_TRUE(result.flyHudActive);

    input.rightDown = false;
    input.rightReleased = true;
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
    input.rightPressed = true;
    input.rightDown = true;
    input.mouseScreen = {300.0f, 240.0f};
    input.mouseDelta = {250.0f, -180.0f};
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    input.deltaSeconds = 0.25f;

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
    input.rightPressed = true;
    input.rightDown = true;
    input.mouseScreen = {300.0f, 240.0f};

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.consumed);
    ASSERT_TRUE(result.flyNavigationActive);

    const ViewportCameraPose before = camera.pose();
    input.rightPressed = false;
    input.mouseDelta = {0.0f, -2.0f};
    input.deltaSeconds = 0.016f;
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
    input.rightPressed = true;
    input.rightDown = true;
    input.mouseScreen = {300.0f, 240.0f};

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    const float distanceBeforeWheel = camera.state().distance;
    const float speedBeforeWheel = camera.flySpeed();

    input.rightPressed = false;
    input.mouseWheel = 1.0f;
    input.mouseDelta = {};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flySpeedChanged);
    EXPECT_NEAR(result.flySpeed, speedBeforeWheel + 1.0f, 0.001f);
    EXPECT_NEAR(camera.flySpeed(), speedBeforeWheel + 1.0f, 0.001f);
    EXPECT_NEAR(camera.state().distance, distanceBeforeWheel, 0.001f);

    input.mouseWheel = -2.0f;
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.flySpeedChanged);
    EXPECT_NEAR(camera.flySpeed(), speedBeforeWheel - 1.0f, 0.001f);
}

TEST(ViewportInputRouterTests, MiddleMouseResetsFlyNavigationSpeed) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    camera.adjustFlySpeed(5.0f);
    ASSERT_NEAR(camera.flySpeed(), 20.0f, 0.001f);
    ASSERT_FALSE(camera.flySpeedIsDefault());

    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.rightPressed = true;
    input.rightDown = true;
    input.mouseScreen = {300.0f, 240.0f};

    auto result = router.handleFrame(input, camera);
    ASSERT_TRUE(result.flyNavigationActive);

    input.rightPressed = false;
    input.middlePressed = true;
    input.mouseDelta = {};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.flySpeedChanged);
    EXPECT_TRUE(result.flySpeedReset);
    EXPECT_NEAR(result.flySpeed, 15.0f, 0.001f);
    EXPECT_NEAR(camera.flySpeed(), 15.0f, 0.001f);
    EXPECT_TRUE(camera.flySpeedIsDefault());
}

TEST(ViewportInputRouterTests, ShiftRightMouseDoesNotStartFlyNavigation) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportHovered = true;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};
    input.rightPressed = true;
    input.rightDown = true;
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
    const ProjectObjectRecord* record = document.objectRecord(object);
    ASSERT_NE(record, nullptr);
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
    const std::vector<ProjectObjectRecord> records = document.objectRecords();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(records[0].id, 42u);
    EXPECT_EQ(records[1].id, 43u);

    document.rename("Renamed Project");
    EXPECT_EQ(document.name(), "Renamed Project");
    EXPECT_EQ(document.revision(), 4u);
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
    ASSERT_NE(meshObject, InvalidEditorObjectId);
    ASSERT_NE(groupObject, InvalidEditorObjectId);
    EXPECT_NE(meshObject, groupObject);

    const std::vector<ProjectObjectRecord> records = document.objectRecords();
    ASSERT_EQ(records.size(), 2u);
    EXPECT_EQ(document.objectIdForEntity(mesh), meshObject);
    EXPECT_EQ(document.objectIdForEntity(group), groupObject);
    EXPECT_EQ(document.entityForObjectId(meshObject), mesh);
    EXPECT_EQ(document.entityForObjectId(groupObject), group);
    ASSERT_NE(document.objectRecord(meshObject), nullptr);
    ASSERT_NE(document.objectRecord(groupObject), nullptr);
    EXPECT_EQ(document.objectRecord(groupObject)->modelingObject, InvalidModelingObjectId);

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
    EXPECT_EQ(document.scene().editorObjectId(first), 1u);

    const EditorObjectId detached = document.createObjectRecord(ProjectObjectRecord{
            .authoredName = "Detached Object Record",
    });
    EXPECT_EQ(detached, 2u);

    const EntityId second = document.scene().createEntity("Second", EntityKind::Mesh);
    ASSERT_NE(second, InvalidEntityId);
    EXPECT_EQ(document.scene().editorObjectId(second), 3u);

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
    EXPECT_NE(opened->document().objectRecord(object), nullptr);
    EXPECT_EQ(opened->document().entityForObjectId(object), authoredEntity);
}

TEST(AssetProjectArchitectureTests, SourceExposesNamedAssetAndProjectArchitectureTypes) {
    const std::filesystem::path assetDocumentPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "assets" / "AssetDocument.h";
    const std::filesystem::path typedStorePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "assets" / "TypedAssetStore.h";
    const std::filesystem::path registryPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "assets" / "AssetRegistry.h";
    const std::filesystem::path projectSessionPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "scene" / "document" / "ProjectSession.h";

    std::ifstream assetDocumentFile(assetDocumentPath);
    std::ifstream typedStoreFile(typedStorePath);
    std::ifstream registryFile(registryPath);
    std::ifstream projectSessionFile(projectSessionPath);
    ASSERT_TRUE(assetDocumentFile.is_open()) << assetDocumentPath.string();
    ASSERT_TRUE(typedStoreFile.is_open()) << typedStorePath.string();
    ASSERT_TRUE(registryFile.is_open()) << registryPath.string();
    ASSERT_TRUE(projectSessionFile.is_open()) << projectSessionPath.string();

    const std::string assetDocumentSource{
            std::istreambuf_iterator<char>(assetDocumentFile),
            std::istreambuf_iterator<char>()};
    const std::string typedStoreSource{
            std::istreambuf_iterator<char>(typedStoreFile),
            std::istreambuf_iterator<char>()};
    const std::string registrySource{
            std::istreambuf_iterator<char>(registryFile),
            std::istreambuf_iterator<char>()};
    const std::string projectSessionSource{
            std::istreambuf_iterator<char>(projectSessionFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(assetDocumentSource.find("struct AssetDocumentBase"), std::string::npos);
    EXPECT_NE(typedStoreSource.find("class TypedAssetStore"), std::string::npos);
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
    QuaderEditorApi editor(session);
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
    EXPECT_NE(duplicateObject, InvalidEditorObjectId);
    EXPECT_NE(duplicateLeafObject, InvalidEditorObjectId);
    EXPECT_NE(duplicateObject, sourceObject);
    EXPECT_NE(duplicateLeafObject, leafObject);

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FALSE(scene.exists(duplicateRoot));
    ASSERT_TRUE(undoRedo.redo().ok);

    const EntityId redoneRoot = duplicateCommand->duplicateEntity();
    ASSERT_TRUE(scene.exists(redoneRoot));
    ASSERT_EQ(scene.children(redoneRoot).size(), 1u);
    const EntityId redoneLeaf = scene.children(redoneRoot).front();
    EXPECT_EQ(scene.editorObjectId(redoneRoot), duplicateObject);
    EXPECT_EQ(scene.editorObjectId(redoneLeaf), duplicateLeafObject);
}

TEST(DocumentCommandTests, DestroyUndoRestoresEditorObjectIdsForSubtrees) {
    SceneDocument scene;
    UndoRedoManager undoRedo;

    const EntityId source = scene.createEntity("Source", EntityKind::Mesh);
    const EntityId leaf = scene.createEntity("Leaf");
    ASSERT_TRUE(scene.setParent(leaf, source, false));
    const EditorObjectId sourceObject = scene.editorObjectId(source);
    const EditorObjectId leafObject = scene.editorObjectId(leaf);

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
    const ProjectObjectRecord* record = session.document().objectRecord(groupObject);
    ASSERT_NE(record, nullptr);
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

    EXPECT_EQ(box.command, ModelingCommandId::ActivateBoxTool);
    EXPECT_EQ(box.id, "box");
    EXPECT_EQ(box.label, "Box");
    EXPECT_EQ(box.shortcut, "B");
    EXPECT_TRUE(box.viewportScoped);
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
    EXPECT_FALSE(combine->disabledReason.empty());

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
    EXPECT_FALSE(fillHole->disabledReason.empty());

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
    auto meshPositionFingerprint = [](const MeshAsset& mesh) {
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
    };

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

TEST(ModelingSessionServiceTests, RmbMeshPickInFaceModeSelectsTargetAndAllowsContextMenu) {
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

    const ModelingSelectionSummary rmbOnAlreadySelected = service.pickSelect(ModelingPickSelectRequest{
            .ray = {.origin = {0.0f, 0.0f, 5.0f}, .direction = {0.0f, 0.0f, -1.0f}},
            .clickCount = 1,
            .preserveSelectionOnMiss = true,
            .preserveSelectionOnSelectedHit = true,
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
    EXPECT_TRUE(service.toolStatus().insertEdgeLoopActive);
    EXPECT_FALSE(service.toolStatus().insertEdgeLoopDragging);
    EXPECT_FALSE(service.toolStatus().insertEdgeLoopHasPreview);

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
    std::optional<SceneBounds> hoverWireBounds;
    for (const ViewportOverlayLine& line : hoverFrame.lines) {
        if (line.layer != ViewportOverlayLayer::SourceWire) {
            continue;
        }
        if (!hoverWireBounds) {
            hoverWireBounds = SceneBounds{.min = line.start, .max = line.start};
        } else {
            includePoint(*hoverWireBounds, line.start);
        }
        includePoint(*hoverWireBounds, line.end);
    }
    ASSERT_TRUE(hoverWireBounds.has_value());
    EXPECT_NEAR(boundsCenter(*hoverWireBounds).x, 4.0f, 0.001f);

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

    const ModelingCommandResult deleted = service.executeInstantOperation(ModelingInstantOperationId::DeleteSelection);
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

    const MeshComponent* meshComponent = scene.mesh(box.object.entity);
    ASSERT_NE(meshComponent, nullptr);
    const MeshAsset* sourceMesh = assets.mesh(meshComponent->mesh);
    ASSERT_NE(sourceMesh, nullptr);
    const std::size_t sourceIndexCount = sourceMesh->indices.size();

    ASSERT_TRUE(service.beginTransformPreview(false).ok);
    ASSERT_TRUE(service.updateTransformExtrudePreview({0.0f, 0.0f, 1.0f}, 1.0f).ok);
    const MeshAsset* extrudedMesh = assets.mesh(meshComponent->mesh);
    ASSERT_NE(extrudedMesh, nullptr);
    ASSERT_GT(extrudedMesh->indices.size(), sourceIndexCount);

    ASSERT_TRUE(service.updateTransformExtrudePreview({0.0f, 0.0f, 0.0f}, 1.0f).ok);
    const MeshAsset* restoredMesh = assets.mesh(meshComponent->mesh);
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
    QuaderEditorApi editor(session);

    const QuaderApiResult<QuaderModelingObjectRef> box = editor.modeling().createBox({
            .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
            .name = "History Object Drag Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(session.modeling().selectionMode(), ModelingSelectionMode::Object);

    QuaderApiResult<ModelingTransformDragPreview> begin =
            editor.modeling().beginTransformDrag(testXMoveDragBeginRequest());
    ASSERT_TRUE(begin.ok) << begin.message;
    EXPECT_TRUE(begin.value.active);
    EXPECT_TRUE(begin.value.sceneTransform);

    QuaderApiResult<ModelingTransformDragPreview> update =
            editor.modeling().updateTransformDrag(testXMoveDragUpdateRequest(2.0f));
    ASSERT_TRUE(update.ok) << update.message;
    EXPECT_TRUE(update.value.hasScenePreviewTransform);
    ASSERT_NE(session.scene().transform(box.value.entity), nullptr);
    EXPECT_NEAR(session.scene().transform(box.value.entity)->position.x, 2.0f, 0.001f);

    QuaderApiResult<ModelingTransformDragPreview> commit = editor.modeling().commitTransformDrag();
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

TEST(ModelingSessionServiceTests, SceneObjectSyncDoesNotResetComponentSelectionMode) {
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

    EXPECT_EQ(service.selectionMode(), ModelingSelectionMode::Vertex);
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Vertex);
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
        return value.layer == ViewportOverlayLayer::SourceWire;
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
