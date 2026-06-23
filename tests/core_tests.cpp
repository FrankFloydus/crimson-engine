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
#include "api/QuaderEditorApi.h"
#include "ui/ConsoleModel.h"
#include "ui/UISettings.h"
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
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <memory>
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

TEST(UISettingsTests, DefaultsKeepConsoleHidden) {
    const UISettings settings = UISettings::defaults();

    EXPECT_TRUE(settings.isPanelVisible(UIPanelId::Outliner));
    EXPECT_TRUE(settings.isPanelVisible(UIPanelId::Inspector));
    EXPECT_FALSE(settings.isPanelVisible(UIPanelId::Console));
    EXPECT_TRUE(settings.isPanelVisible(UIPanelId::Viewport));
    EXPECT_TRUE(settings.navigationGizmoVisible());
    EXPECT_EQ(settings.locale(), "en-US");
}

TEST(UISettingsTests, RoundTripsPanelVisibility) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_round_trip.ini";
    std::filesystem::remove(path);

    UISettings settings = UISettings::defaults();
    settings.setPanelVisible(UIPanelId::Outliner, false);
    settings.setPanelVisible(UIPanelId::Inspector, true);
    settings.setPanelVisible(UIPanelId::Console, true);
    settings.setNavigationGizmoVisible(false);
    settings.setLocale("it-IT");

    ASSERT_TRUE(settings.save(path));
    const UISettings loaded = UISettings::load(path);

    EXPECT_FALSE(loaded.isPanelVisible(UIPanelId::Outliner));
    EXPECT_TRUE(loaded.isPanelVisible(UIPanelId::Inspector));
    EXPECT_TRUE(loaded.isPanelVisible(UIPanelId::Console));
    EXPECT_FALSE(loaded.navigationGizmoVisible());
    EXPECT_EQ(loaded.locale(), "it-IT");

    std::filesystem::remove(path);
}

TEST(UISystemTests, ShowMenuTogglesNavigationGizmo) {
    const std::filesystem::path mainMenuBarPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "MainMenuBar.cpp";
    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UISystem.cpp";
    const std::filesystem::path viewportPanelHeaderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.h";
    const std::filesystem::path uiSettingsSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UISettings.cpp";

    std::ifstream mainMenuBarFile(mainMenuBarPath);
    std::ifstream uiSystemFile(uiSystemPath);
    std::ifstream viewportPanelHeaderFile(viewportPanelHeaderPath);
    std::ifstream uiSettingsSourceFile(uiSettingsSourcePath);
    ASSERT_TRUE(mainMenuBarFile.is_open()) << mainMenuBarPath.string();
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();
    ASSERT_TRUE(viewportPanelHeaderFile.is_open()) << viewportPanelHeaderPath.string();
    ASSERT_TRUE(uiSettingsSourceFile.is_open()) << uiSettingsSourcePath.string();

    const std::string mainMenuBarSource{
            std::istreambuf_iterator<char>(mainMenuBarFile),
            std::istreambuf_iterator<char>()};
    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};
    const std::string viewportPanelHeader{
            std::istreambuf_iterator<char>(viewportPanelHeaderFile),
            std::istreambuf_iterator<char>()};
    const std::string uiSettingsSource{
            std::istreambuf_iterator<char>(uiSettingsSourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(mainMenuBarSource.find("void ShowMenu::draw"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ui.menu.show"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ui.menu.show.navigation_gizmo"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ImGui::MenuItem(gizmoLabel.c_str()"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("context.editor.preferences().setNavigationGizmoVisible(navigationGizmoVisible)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("QuaderPreferencesRuntimeHooks"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("result.settingsChanged = true"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportPanel_->setNavigationGizmoVisible(settings_.navigationGizmoVisible())"), std::string::npos);
    EXPECT_NE(viewportPanelHeader.find("bool navigationGizmoVisible() const"), std::string::npos);
    EXPECT_NE(viewportPanelHeader.find("NavigationGizmo navigationGizmo_"), std::string::npos);
    EXPECT_NE(uiSettingsSource.find("NavigationGizmo="), std::string::npos);
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
    expectColorNear(settings.color(ViewportColorSettingId::SelectionWire), {1.0f, 0.827451f, 0.121569f, 1.0f});
    expectColorNear(settings.color(ViewportColorSettingId::SourceWire), {0.360784f, 0.878431f, 1.0f, 0.921569f});
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::GridMinorLineSize), 0.32f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::SelectionEdgeLineSize), 2.5f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::PickVertexRadius), 0.3f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::GizmoSize), 1.0f);
    EXPECT_TRUE(viewportBoolSettings().empty());
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

TEST(ViewportPanelTests, RoutesSelectionOverlayThroughRenderBridgeWithoutCpuVisibilityClipping) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("refreshCachedSelectionOverlayFrame(document, assets, modeling)"), std::string::npos);
    EXPECT_NE(source.find("ViewportSceneOverlayBuilder::buildSelectionFrame("), std::string::npos);
    EXPECT_NE(source.find(".assetRevision = assets.revision()"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCache_.needsRefresh(key)"), std::string::npos);
    EXPECT_NE(source.find("renderBridge.setOverlayFrame(\n                viewportPaneIndex(paneId)"), std::string::npos);
    EXPECT_NE(source.find("ViewportCameraAdapter::overlayCamera(paneWorkspace.camera, paneWorkspace.renderSize)"), std::string::npos);
    EXPECT_EQ(source.find("appendHierarchySelectionWires"), std::string::npos);
    EXPECT_EQ(source.find("appendGroupSelectionCages"), std::string::npos);
    EXPECT_EQ(source.find("renderBridge.setOverlayFrame(cachedSelectionOverlayFrame_, ViewportCameraAdapter::overlayCamera(camera, itemSize));"), std::string::npos);
    EXPECT_EQ(source.find("drawSelectionOverlay2d"), std::string::npos);
    EXPECT_EQ(source.find("drawVisibilityClippedOverlayLine"), std::string::npos);
    EXPECT_EQ(source.find("pointVisibleFromCamera"), std::string::npos);
    EXPECT_EQ(source.find("rayIntersectsTriangle(QVec3 origin"), std::string::npos);
    EXPECT_EQ(source.find("renderBridge.setOverlayFrame(selectionOverlay, camera"), std::string::npos);
    EXPECT_EQ(source.find("shouldDrawOverlayLayer2d"), std::string::npos);
    EXPECT_EQ(source.find("layerNeedsSceneVisibility2d"), std::string::npos);
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

TEST(ViewportPanelTests, ShutdownIsIdempotentAndClearsBorrowedWorkspace) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("if (shutdownComplete_)"), std::string::npos);
    EXPECT_NE(source.find("shutdownComplete_ = true;"), std::string::npos);
    EXPECT_NE(source.find("workspace_->clearPaneVisibility();\n        workspace_ = nullptr;"), std::string::npos);
}

TEST(ViewportPanelTests, WorkspaceAccessRequiresInjectionWithoutStaticFallback) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(source.find("static ViewportWorkspaceService fallbackWorkspace"), std::string::npos);
    EXPECT_NE(source.find("throw std::logic_error(\"ViewportPanel requires an attached ViewportWorkspaceService.\")"),
            std::string::npos);
}

TEST(ViewportPanelTests, WireframeObjectSelectionUsesWhiteBaseAndShadedOverlayWires) {
    const std::filesystem::path panelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    const std::filesystem::path builderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "ViewportSceneOverlayBuilder.cpp";
    std::ifstream panelFile(panelPath);
    std::ifstream builderFile(builderPath);
    ASSERT_TRUE(panelFile.is_open()) << panelPath.string();
    ASSERT_TRUE(builderFile.is_open()) << builderPath.string();

    const std::string panel{
            std::istreambuf_iterator<char>(panelFile),
            std::istreambuf_iterator<char>()};
    const std::string builder{
            std::istreambuf_iterator<char>(builderFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(panel.find("ViewportSceneOverlayBuilder::buildWireframeFrame("), std::string::npos);
    EXPECT_NE(builder.find("stripWireframeObjectFaceFills(baseFrame, selectionMode)"), std::string::npos);
    EXPECT_NE(builder.find("wireframeUsesWireOnlyObjectSelection"), std::string::npos);
    EXPECT_NE(builder.find("rgba8(255, 255, 255)"), std::string::npos);
    EXPECT_NE(builder.find("ViewportOverlayLayer::WireframeBase"), std::string::npos);
    EXPECT_NE(builder.find("ViewportOverlayLayer::SelectedVisibleFace"), std::string::npos);
    EXPECT_NE(builder.find("ViewportOverlayLayer::HoverFace"), std::string::npos);
    EXPECT_EQ(builder.find("selectedWire ? kOverlaySelectionWireColor : kOverlaySourceWireColor"), std::string::npos);
    EXPECT_NE(builder.find("appendMeshSelectionWire(\n                frame,\n                document,\n                assets,\n                entity"),
            std::string::npos);
    EXPECT_EQ(panel.find("appendSceneWireframeOverlay"), std::string::npos);
    EXPECT_NE(panel.find("ViewportSceneOverlayBuilder::buildWireframeFrame(\n                    std::move(overlayFrame),"),
            std::string::npos);
    EXPECT_EQ(panel.find("ViewportOverlayStyleResolver::apply(overlayFrame, settings);\n            appendSceneWireframeOverlay"),
            std::string::npos);
}

TEST(ViewportPanelTests, GroupSelectionCageUsesDedicatedYellowLayerOutsideGroupMode) {
    const std::filesystem::path panelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    const std::filesystem::path builderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "overlay" / "ViewportSceneOverlayBuilder.cpp";
    std::ifstream panelFile(panelPath);
    std::ifstream builderFile(builderPath);
    ASSERT_TRUE(panelFile.is_open()) << panelPath.string();
    ASSERT_TRUE(builderFile.is_open()) << builderPath.string();

    const std::string panel{
            std::istreambuf_iterator<char>(panelFile),
            std::istreambuf_iterator<char>()};
    const std::string builder{
            std::istreambuf_iterator<char>(builderFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(builder.find(".layer = ViewportOverlayLayer::GroupBounds"), std::string::npos);
    EXPECT_NE(builder.find(".color = kOverlayGroupBoundsColor"), std::string::npos);
    EXPECT_NE(panel.find("ViewportSceneOverlayBuilder::buildSelectionFrame(\n            modeling.selectionOverlayFrame(),"),
            std::string::npos);
    EXPECT_EQ(panel.find("appendGroupSelectionCages"), std::string::npos);
    EXPECT_EQ(builder.find("if (overlaySummary.mode == ModelingSelectionMode::Group) {\n        appendGroupSelectionCages"),
            std::string::npos);
}

TEST(ViewportPanelTests, ToolActivationPreservesGroupModeSceneSelection) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const bool preserveGroupSelection"), std::string::npos);
    EXPECT_NE(source.find("selectedBeforeToolActivation"), std::string::npos);
    EXPECT_NE(source.find("activeBeforeToolActivation"), std::string::npos);
    EXPECT_NE(source.find("editor.selection().selectObjects(\n                        std::span<const EntityId>(selectedBeforeToolActivation.data()"), std::string::npos);
    EXPECT_NE(source.find("uiContext.editor.selection().selectObjects(\n                                std::span<const EntityId>(selectedBeforeToolActivation.data()"), std::string::npos);
    EXPECT_EQ(source.find("editor.scene().select(std::span<const EntityId>(selectedBeforeToolActivation.data()"), std::string::npos);
    EXPECT_EQ(source.find("uiContext.editor.scene().select(\n                                std::span<const EntityId>(selectedBeforeToolActivation.data()"), std::string::npos);
    EXPECT_NE(source.find("activeBeforeToolActivation)"), std::string::npos);
    EXPECT_NE(source.find("editor.selection().setMode(ModelingSelectionMode::Group)"), std::string::npos);
    EXPECT_NE(source.find("inputRouter_.setSelectionMode(ViewportSelectionMode::Group)"), std::string::npos);
    EXPECT_NE(source.find("bool ViewportPanel::activateToolFromMenu(\n        ViewportToolId tool,\n        QuaderEditorApi& editor,\n        SceneDocument& document,\n        ModelingSessionService& modeling"),
            std::string::npos);
    EXPECT_NE(source.find("Group selection restore after menu tool activation failed"), std::string::npos);
    EXPECT_EQ(source.find("inputRouter_.setSelectionMode(toViewportSelectionMode(modeling.selectionSummary().mode));\n    inputRouter_.clearModalToolState();"),
            std::string::npos);
}

TEST(ViewportPanelTests, SelectionModeHotkeysRouteThroughEditorSelectionEvenWhenRouterStateAlreadyMatches) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const bool selectionModeShortcutHandled"), std::string::npos);
    EXPECT_NE(source.find("result.shortcut.kind == ViewportShortcutKind::SetSelectionMode"), std::string::npos);
    EXPECT_NE(source.find("uiContext.editor.selection().setMode(viewportSelectionModeToApp(result.shortcut.selectionMode))"),
            std::string::npos);
    EXPECT_NE(source.find("inputRouter_.setSelectionMode(appSelectionModeToViewport(uiContext.editor.selection().summary().mode))"),
            std::string::npos);
    EXPECT_EQ(source.find("if (result.selectionModeChanged) {\n        const QuaderApiVoidResult command =\n                uiContext.editor.selection().setMode"),
            std::string::npos);
}

TEST(ViewportPanelTests, DeletePopupTreatsSelectedSceneGroupsAsObjectTargets) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("bool hasSelectedSceneGroupRoot(const SceneDocument& document)"), std::string::npos);
    EXPECT_NE(source.find("document.kind(selected) == EntityKind::Empty && document.hasChildren(selected)"),
            std::string::npos);
    EXPECT_NE(source.find("deleteSummary.mode == ModelingSelectionMode::Group ||\n                hasSelectedSceneGroupRoot(document)"),
            std::string::npos);
}

TEST(ViewportPanelTests, DrawsPassiveDecorationsForNonInputPanes) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    const std::filesystem::path transformGizmoPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    const std::filesystem::path navigationGizmoPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "NavigationGizmo.cpp";
    std::ifstream sourceFile(sourcePath);
    std::ifstream transformGizmoFile(transformGizmoPath);
    std::ifstream navigationGizmoFile(navigationGizmoPath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(transformGizmoFile.is_open()) << transformGizmoPath.string();
    ASSERT_TRUE(navigationGizmoFile.is_open()) << navigationGizmoPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string transformGizmoSource{
            std::istreambuf_iterator<char>(transformGizmoFile),
            std::istreambuf_iterator<char>()};
    const std::string navigationGizmoSource{
            std::istreambuf_iterator<char>(navigationGizmoFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("void ViewportPanel::drawPaneDecorations("), std::string::npos);
    EXPECT_NE(source.find("ViewportCameraController& camera = workspace().camera(paneState.id)"), std::string::npos);
    EXPECT_NE(source.find("navigationGizmo_.draw(camera, paneState.id, itemMin, itemSize, {}, nullptr, nullptr, false)"), std::string::npos);
    EXPECT_NE(source.find("const bool navigationGizmoConsumed = navigationGizmo_.draw("), std::string::npos);
    EXPECT_EQ(source.find("drawViewCube("), std::string::npos);
    EXPECT_EQ(source.find("navigationGizmoVisible_"), std::string::npos);
    EXPECT_NE(source.find("transformGizmoController_.draw({"), std::string::npos);
    EXPECT_NE(source.find(".interactive = false"), std::string::npos);
    EXPECT_EQ(source.find("drawTransformGizmo("), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("if (!interactive || !mouseInsideViewport || !ImGui::IsMouseClicked(ImGuiMouseButton_Left) || !hoverHit.hit)"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("input.widget_size = 70.0;"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("input.margin = {46.0f, 46.0f};"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("hoverHit.kind == quader_editor_core::ViewCubeHitKind::face"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("IM_COL32(255, 221, 111, 255)"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("drawList->Flags &= ~ImDrawListFlags_AntiAliasedFill"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("for (const quader_editor_core::ViewCubeEdgePrimitive& edge : primitives.edges)"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("neutralEdgeColor"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("for (const quader_editor_core::ViewCubeEdgePrimitive& edge : primitives.axis_edges)"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("coloredEdgeIndices"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("coloredBottomVertex"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("edgeTouchesColoredBottomVertex"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("point.y > coloredBottomVertex->y + 0.5f"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("edge.axis != marker.axis"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("distanceToSegmentSquared(\n                    markerCenter"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("staticXAxisEnd"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("staticYAxisEnd"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("staticZAxisEnd"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("toViewCubeScreenPoint(edge.screen_start)"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("constexpr float labelSize = 11.5f"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("IM_COL32(17, 22, 28, 255)"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("nearestCubeCornerTo"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("highlighted ? IM_COL32(255, 244, 184, 255)"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("point.x += 1.5f"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("markerCenter.x + 1.0f"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("marker.depth >= 0.0 ? 1.0f : 0.42f"), std::string::npos);
    EXPECT_EQ(navigationGizmoSource.find("marker.depth >= 0.0 ? 8.0f : 6.5f"), std::string::npos);
    EXPECT_NE(transformGizmoSource.find("if (!interactive) {\n        return false;\n    }\n\n    auto fallbackHit = [&]()"), std::string::npos);
    EXPECT_NE(transformGizmoSource.find("bool consumed = dragging_;"), std::string::npos);
}

TEST(ViewportPanelTests, GizmoSizeUsesRuntimeScaleWithoutReplacingActiveGrid) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("input.grid_size = gridSize"), std::string::npos);
    EXPECT_NE(source.find("input.gizmo_size_pixels = 68.0;"), std::string::npos);
    EXPECT_NE(source.find("input.gizmo_scale = std::max(gizmoSizeScale, 0.1f);"), std::string::npos);
    EXPECT_NE(source.find("build_transform_gizmo_frame(runtimeInput)"), std::string::npos);
    EXPECT_NE(source.find("const bool precision = io.KeyCtrl || io.KeySuper;"), std::string::npos);
    EXPECT_EQ(source.find("visualRuntimeInput"), std::string::npos);
    EXPECT_EQ(source.find("kGizmoFrameGridFloor"), std::string::npos);
    EXPECT_EQ(source.find("input.gizmo_size_pixels = 68.0 * std::max(gizmoSizeScale"), std::string::npos);
}

TEST(ViewportPanelTests, SceneOnlyGroupTransformsSnapWithActiveGrid) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    const std::filesystem::path servicePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "transform" / "SceneTransformDragService.cpp";
    const std::filesystem::path boundsPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "scene" / "SceneBounds.cpp";
    std::ifstream sourceFile(sourcePath);
    std::ifstream serviceFile(servicePath);
    std::ifstream boundsFile(boundsPath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(serviceFile.is_open()) << servicePath.string();
    ASSERT_TRUE(boundsFile.is_open()) << boundsPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string service{
            std::istreambuf_iterator<char>(serviceFile),
            std::istreambuf_iterator<char>()};
    const std::string bounds{
            std::istreambuf_iterator<char>(boundsFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("selectedTransformWorldBounds(document, request.assets)"), std::string::npos);
    EXPECT_NE(bounds.find("entityHierarchyWorldBounds(document, assets, entity)"), std::string::npos);
    EXPECT_NE(service.find("documentLocalPivotForWorldPoint(document, entity, worldPivot)"), std::string::npos);
    EXPECT_NE(source.find("snapWorldDeltaForHandle(worldMoveDelta, activeAxis_, startWorldPivot_, activeGridSize, snapEnabled)"),
            std::string::npos);
    EXPECT_NE(source.find("sceneOnlyRotateState_"), std::string::npos);
    EXPECT_NE(source.find("quader_editor_core::preview_rotate_from_screen_position"), std::string::npos);
    EXPECT_NE(source.find("snapEnabled ? 15.0 : 0.0"), std::string::npos);
    EXPECT_NE(source.find("quader_editor_core::scale_pixels_per_factor"), std::string::npos);
    EXPECT_NE(source.find("quader_editor_core::snapped_scale_factor"), std::string::npos);
    EXPECT_NE(source.find("selectionBounds,\n                        startWorldPivot_,\n                        snapEnabled,\n                        activeGridSize"),
            std::string::npos);
    EXPECT_EQ(source.find("applyAxis(next.scale.x, next.scale.y, next.scale.z, screenAxisDelta(activeAxis_) * 0.01f)"),
            std::string::npos);
}

TEST(TransformGizmoTests, ScreenSizedMoveScaleFrameDoesNotUseGridAsVisualFloor) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) /
            "src" / "editor" / "modeling" / "runtime" / "transform_gizmo.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const double axis_length = pixel_world * screen_size;"), std::string::npos);
    EXPECT_NE(source.find("const double plane_offset = pixel_world * (screen_size * 0.50);"), std::string::npos);
    EXPECT_NE(source.find("const double plane_half_size = pixel_world * (8.0 * visual_scale);"), std::string::npos);
    EXPECT_EQ(source.find("safe_grid"), std::string::npos);
    EXPECT_EQ(source.find("safe_grid *"), std::string::npos);
}

TEST(ViewportPanelTests, SpaceMaximizeTargetsHoveredPane) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("result.maximizedToggleRequested && !operationPreviewShortcutConsumed"), std::string::npos);
    EXPECT_NE(source.find("viewportWorkspace.focusPane(paneState.id)"), std::string::npos);
    EXPECT_NE(source.find("viewportWorkspace.toggleFocusedPaneMaximized()"), std::string::npos);
}

TEST(ViewportPanelTests, HoverUpdatesFocusedPaneWithoutClick) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("if (drawPane.focusHovered && !drawPane.chromeConsumesInput && !splitConsumed)"),
            std::string::npos);
    EXPECT_NE(source.find("viewportWorkspace.focusPane(drawPane.pane->id)"), std::string::npos);
}

TEST(ViewportPanelTests, GizmoDragKeepsInputBoundToOwnerPane) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.h";
    const std::filesystem::path transformGizmoHeaderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.h";
    const std::filesystem::path transformGizmoSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    const std::filesystem::path navigationGizmoHeaderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "NavigationGizmo.h";
    const std::filesystem::path navigationGizmoSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "NavigationGizmo.cpp";
    std::ifstream headerFile(headerPath);
    std::ifstream transformGizmoHeaderFile(transformGizmoHeaderPath);
    std::ifstream transformGizmoSourceFile(transformGizmoSourcePath);
    std::ifstream navigationGizmoHeaderFile(navigationGizmoHeaderPath);
    std::ifstream navigationGizmoSourceFile(navigationGizmoSourcePath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();
    ASSERT_TRUE(transformGizmoHeaderFile.is_open()) << transformGizmoHeaderPath.string();
    ASSERT_TRUE(transformGizmoSourceFile.is_open()) << transformGizmoSourcePath.string();
    ASSERT_TRUE(navigationGizmoHeaderFile.is_open()) << navigationGizmoHeaderPath.string();
    ASSERT_TRUE(navigationGizmoSourceFile.is_open()) << navigationGizmoSourcePath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};
    const std::string transformGizmoHeader{
            std::istreambuf_iterator<char>(transformGizmoHeaderFile),
            std::istreambuf_iterator<char>()};
    const std::string transformGizmoSource{
            std::istreambuf_iterator<char>(transformGizmoSourceFile),
            std::istreambuf_iterator<char>()};
    const std::string navigationGizmoHeader{
            std::istreambuf_iterator<char>(navigationGizmoHeaderFile),
            std::istreambuf_iterator<char>()};
    const std::string navigationGizmoSource{
            std::istreambuf_iterator<char>(navigationGizmoSourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(header.find("gizmoDragPaneActive_"), std::string::npos);
    EXPECT_EQ(header.find("gizmoDragPaneId_"), std::string::npos);
    EXPECT_NE(transformGizmoHeader.find("bool dragLocksInput() const"), std::string::npos);
    EXPECT_NE(transformGizmoHeader.find("ViewportPaneId dragPaneId() const"), std::string::npos);
    EXPECT_NE(transformGizmoHeader.find("bool dragPaneActive_"), std::string::npos);
    EXPECT_NE(transformGizmoHeader.find("ViewportPaneId dragPaneId_"), std::string::npos);
    EXPECT_NE(header.find("ViewportPaneId paneId"), std::string::npos);
    EXPECT_NE(header.find("NavigationGizmo navigationGizmo_"), std::string::npos);
    EXPECT_EQ(header.find("viewCubeDragPaneActive_"), std::string::npos);
    EXPECT_NE(navigationGizmoHeader.find("bool inputLocked() const"), std::string::npos);
    EXPECT_NE(navigationGizmoHeader.find("ViewportPaneId lockedPaneId() const"), std::string::npos);
    EXPECT_NE(navigationGizmoHeader.find("bool dragPaneActive_"), std::string::npos);
    EXPECT_NE(navigationGizmoHeader.find("ViewportPaneId dragPaneId_"), std::string::npos);

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const bool transformDragLocksInput = transformGizmoController_.dragLocksInput()"), std::string::npos);
    EXPECT_NE(source.find("const bool navigationGizmoDragLocksInput = navigationGizmo_.inputLocked()"), std::string::npos);
    EXPECT_NE(source.find("if (!transformDragLocksInput && !navigationGizmoDragLocksInput)"), std::string::npos);
    EXPECT_NE(source.find("drawPane.pane->id == transformGizmoController_.dragPaneId()"), std::string::npos);
    EXPECT_NE(source.find("drawPane.pane->id == navigationGizmo_.lockedPaneId()"), std::string::npos);
    EXPECT_NE(source.find("inputOwnsActiveGizmoDrag"), std::string::npos);
    EXPECT_NE(source.find("inputOwnsActiveNavigationGizmoDrag"), std::string::npos);
    EXPECT_NE(transformGizmoSource.find("dragPaneActive_ = true"), std::string::npos);
    EXPECT_NE(transformGizmoSource.find("dragPaneId_ = request.paneId"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("dragPaneActive_ = true"), std::string::npos);
    EXPECT_NE(navigationGizmoSource.find("dragPaneId_ = paneId"), std::string::npos);
}

TEST(ViewportPanelTests, HoverWorkKeyIncludesPaneAndCameraSignature) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportHoverController.h";
    const std::filesystem::path controllerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportHoverController.cpp";
    const std::filesystem::path panelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream headerFile(headerPath);
    std::ifstream controllerFile(controllerPath);
    std::ifstream panelFile(panelPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();
    ASSERT_TRUE(controllerFile.is_open()) << controllerPath.string();
    ASSERT_TRUE(panelFile.is_open()) << panelPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};
    const std::string controller{
            std::istreambuf_iterator<char>(controllerFile),
            std::istreambuf_iterator<char>()};
    const std::string panel{
            std::istreambuf_iterator<char>(panelFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("ViewportPaneId paneId"), std::string::npos);
    EXPECT_NE(header.find("QVec3 cameraEye"), std::string::npos);
    EXPECT_NE(header.find("QVec3 cameraForward"), std::string::npos);
    EXPECT_NE(header.find("QVec3 cameraRight"), std::string::npos);
    EXPECT_NE(header.find("QVec3 cameraUp"), std::string::npos);
    EXPECT_NE(header.find("ViewportOverlayProjection cameraProjection"), std::string::npos);
    EXPECT_NE(header.find("bool primaryModifier"), std::string::npos);
    EXPECT_NE(header.find("double vertexRadius"), std::string::npos);
    EXPECT_NE(header.find("double edgeRadius"), std::string::npos);
    EXPECT_NE(controller.find("paneId == other.paneId"), std::string::npos);
    EXPECT_NE(controller.find("vec3Matches(cameraEye, other.cameraEye)"), std::string::npos);
    EXPECT_NE(controller.find("cameraProjection == other.cameraProjection"), std::string::npos);
    EXPECT_NE(controller.find("primaryModifier == other.primaryModifier"), std::string::npos);
    EXPECT_NE(controller.find("vertexRadius == other.vertexRadius"), std::string::npos);
    EXPECT_NE(controller.find("edgeRadius == other.edgeRadius"), std::string::npos);
    EXPECT_NE(controller.find("const ViewportOverlayCamera overlayCamera = ViewportCameraAdapter::overlayCamera(camera, request.viewportSize)"), std::string::npos);
    EXPECT_NE(controller.find(".cameraEye = overlayCamera.eye"), std::string::npos);
    EXPECT_NE(controller.find(".cameraForward = overlayCamera.forward"), std::string::npos);
    EXPECT_NE(controller.find(".primaryModifier = request.modifiers.primary"), std::string::npos);
    EXPECT_NE(controller.find(".vertexRadius = request.vertexRadius"), std::string::npos);
    EXPECT_NE(controller.find(".edgeRadius = request.edgeRadius"), std::string::npos);
    EXPECT_NE(panel.find("hoverController_.makeKey(camera, ViewportHoverWorkRequest"), std::string::npos);
    EXPECT_NE(panel.find(".paneId = paneState.id"), std::string::npos);
    EXPECT_NE(panel.find(".modifiers = input.modifiers"), std::string::npos);
    EXPECT_NE(panel.find(".vertexRadius = settings.floatValue(ViewportFloatSettingId::PickVertexRadius)"), std::string::npos);
    EXPECT_NE(panel.find(".edgeRadius = settings.floatValue(ViewportFloatSettingId::PickEdgeRadius)"), std::string::npos);
}

TEST(ViewportPanelTests, RmbContextMenuUsesContextActionsAndDisabledItems) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    const std::filesystem::path selectionControllerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportSelectionController.cpp";
    const std::filesystem::path presenterPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportContextMenuPresenter.cpp";
    std::ifstream sourceFile(sourcePath);
    std::ifstream selectionControllerFile(selectionControllerPath);
    std::ifstream presenterFile(presenterPath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(selectionControllerFile.is_open()) << selectionControllerPath.string();
    ASSERT_TRUE(presenterFile.is_open()) << presenterPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string selectionController{
            std::istreambuf_iterator<char>(selectionControllerFile),
            std::istreambuf_iterator<char>()};
    const std::string presenter{
            std::istreambuf_iterator<char>(presenterFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("contextMenuPresenter_.drawOperationsPopup"), std::string::npos);
    EXPECT_NE(presenter.find("ViewportOperationsContext"), std::string::npos);
    EXPECT_NE(presenter.find("modeling.contextActions(query)"), std::string::npos);
    EXPECT_NE(presenter.find("labelKey.append(action.actionId)"), std::string::npos);
    EXPECT_NE(presenter.find("localization.imguiLabel(labelKey, stableId, action.label)"), std::string::npos);
    EXPECT_NE(presenter.find("ImGui::MenuItem(label.c_str(), nullptr, false, enabled)"), std::string::npos);
    EXPECT_NE(presenter.find("editor.commands().executeToolActivation(action.toolId)"), std::string::npos);
    EXPECT_NE(presenter.find("editor.commands().executeModelingAction(action.actionId)"), std::string::npos);
    EXPECT_EQ(presenter.find("contextMenuCommandId("), std::string::npos);
    EXPECT_EQ(presenter.find("editor.commands().execute(commandId)"), std::string::npos);
    EXPECT_NE(presenter.find("shouldCloseOperationsPopupForMouseClick()"), std::string::npos);
    EXPECT_NE(presenter.find("isGenericContextAction(action.actionId)"), std::string::npos);
    EXPECT_NE(presenter.find("ImGui::Separator()"), std::string::npos);
    EXPECT_NE(presenter.find("ImGui::CloseCurrentPopup()"), std::string::npos);
    EXPECT_EQ(presenter.find("modeling.executeContextAction(action.actionId)"), std::string::npos);
    EXPECT_EQ(presenter.find("executeTrackedCommandTool"), std::string::npos);
    EXPECT_EQ(presenter.find("action.operationId == \"bridge\""), std::string::npos);
    EXPECT_EQ(presenter.find("uiContext.requestToolSettingsPanel()"), std::string::npos);
    EXPECT_NE(presenter.find("action.disabledReason"), std::string::npos);
    EXPECT_NE(source.find("modalToolOwnsViewport()"), std::string::npos);
    EXPECT_NE(source.find(".modalToolOwnsViewport = modalToolOwnsViewport()"), std::string::npos);
    EXPECT_NE(selectionController.find("editor.selection().pickSelect(ModelingPickSelectRequest"), std::string::npos);
    EXPECT_NE(source.find("pendingContextMenuRequest.targetHit = summary.hasSelection"), std::string::npos);
    EXPECT_NE(source.find(".accumulatedDragDelta = contextMenuRightDragDelta_"), std::string::npos);
    EXPECT_NE(source.find("pendingContextMenuClick"), std::string::npos);
    EXPECT_NE(source.find("selectionController_.pickSelection("), std::string::npos);
    EXPECT_NE(source.find("pendingContextMenuLocalMouse"), std::string::npos);
    EXPECT_EQ(source.find("selectFromViewport"), std::string::npos);
    EXPECT_NE(selectionController.find(".preserveSelectionOnMiss = preserveSelectionOnMiss"), std::string::npos);
    EXPECT_NE(selectionController.find(".preserveSelectionOnSelectedHit = preserveSelectionOnSelectedHit"), std::string::npos);
    EXPECT_EQ(selectionController.find("if (groupMode)"), std::string::npos);
    EXPECT_EQ(selectionController.find("const EntityId hitMesh = pickMeshEntityByBounds(document, assets, worldRay)"), std::string::npos);
    EXPECT_EQ(selectionController.find("const EntityId targetGroup = nearestGroupAncestor(document, hitMesh)"), std::string::npos);
    EXPECT_EQ(selectionController.find("modeling.previewSelectionHover"), std::string::npos);
    EXPECT_NE(source.find("preserveContextMenuHover"), std::string::npos);
    EXPECT_NE(source.find("shouldPreserveViewportContextMenuHover"), std::string::npos);
    EXPECT_NE(source.find("flyCursorCaptureScreen_"), std::string::npos);
    EXPECT_NE(source.find("flyCursorCaptureScreen_.x - mainViewport->Pos.x"), std::string::npos);
    EXPECT_EQ(source.find("contextMenuRightNavigationMoved"), std::string::npos);
    EXPECT_EQ(source.find("Button(operation.label"), std::string::npos);
}

TEST(ViewportPanelTests, UsesSplitInputFocusAndContextMenuHelpers) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.h";
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream headerFile(headerPath);
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};
    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("ViewportInputFrameBuilder inputFrameBuilder_"), std::string::npos);
    EXPECT_NE(header.find("ViewportFocusController focusController_"), std::string::npos);
    EXPECT_NE(header.find("ViewportContextMenuPresenter contextMenuPresenter_"), std::string::npos);
    EXPECT_NE(header.find("ViewportShortcutController shortcutController_"), std::string::npos);
    EXPECT_NE(header.find("ViewportHoverController hoverController_"), std::string::npos);
    EXPECT_NE(header.find("ViewportPaneOverlayCache selectionOverlayCache_"), std::string::npos);
    EXPECT_NE(source.find("focusController_.resolve(focusHovered, uiContext, UIPanelId::Viewport)"), std::string::npos);
    EXPECT_NE(source.find("focusController_.inputActive()"), std::string::npos);
    EXPECT_NE(source.find("inputFrameBuilder_.build"), std::string::npos);
    EXPECT_NE(source.find("contextMenuPresenter_.drawOperationsPopup"), std::string::npos);
    EXPECT_NE(source.find("shortcutController_.handleOperationPreviewShortcut"), std::string::npos);
    EXPECT_NE(source.find("shortcutController_.handleModelingShortcut"), std::string::npos);
    EXPECT_NE(source.find("hoverController_.makeKey"), std::string::npos);
    EXPECT_NE(source.find("hoverController_.claim"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCache_.markDirty"), std::string::npos);
    EXPECT_EQ(source.find("ViewportInputFrame makeViewportInputFrame"), std::string::npos);
    EXPECT_EQ(source.find("bool viewportFocusInputActive()"), std::string::npos);
    EXPECT_EQ(source.find("void drawViewportOperationsPopup"), std::string::npos);
    EXPECT_EQ(source.find("executeViewportModelingShortcut(undoRedo, modeling, result.shortcut)"), std::string::npos);
    EXPECT_EQ(source.find("lastHoverWorkKey_"), std::string::npos);
    EXPECT_EQ(source.find("cachedSelectionOverlayFrameDirty_"), std::string::npos);
}

TEST(ViewportPanelTests, InsertEdgeLoopOwnsMouseReleaseAndCommitsActiveDrag) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.h";
    const std::filesystem::path toolStatePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "tools" / "ViewportTool.h";
    const std::filesystem::path dragToolPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "tools" / "base" / "DragCommitTool.cpp";
    std::ifstream headerFile(headerPath);
    std::ifstream toolStateFile(toolStatePath);
    std::ifstream dragToolFile(dragToolPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();
    ASSERT_TRUE(toolStateFile.is_open()) << toolStatePath.string();
    ASSERT_TRUE(dragToolFile.is_open()) << dragToolPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};
    const std::string toolState{
            std::istreambuf_iterator<char>(toolStateFile),
            std::istreambuf_iterator<char>()};
    const std::string dragTool{
            std::istreambuf_iterator<char>(dragToolFile),
            std::istreambuf_iterator<char>()};

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(header.find("insertEdgeLoopDragActive_"), std::string::npos);
    EXPECT_NE(toolState.find("bool insertEdgeLoopDragActive = false"), std::string::npos);
    EXPECT_NE(source.find("const bool insertEdgeLoopDragRelease"), std::string::npos);
    EXPECT_NE(source.find("!insertEdgeLoopDragRelease"), std::string::npos);
    EXPECT_NE(dragTool.find("context.modeling.beginInsertEdgeLoopDrag(pointContext.makeEventAt(pointer.mouseLocal, true))"), std::string::npos);
    EXPECT_NE(dragTool.find("context.modalState->insertEdgeLoopDragActive = command.ok && context.modeling.toolStatus().insertEdgeLoopDragging"), std::string::npos);
    EXPECT_NE(dragTool.find("const bool dragging"), std::string::npos);
    EXPECT_NE(dragTool.find("!pointer.leftReleased"), std::string::npos);
    EXPECT_NE(dragTool.find("!dragging"), std::string::npos);
    EXPECT_NE(dragTool.find("pointer.leftDown && dragging"), std::string::npos);
    EXPECT_NE(dragTool.find("pointer.leftReleased && dragging"), std::string::npos);
    EXPECT_NE(dragTool.find("context.modeling.commitInsertEdgeLoopDrag()"), std::string::npos);
}

TEST(ViewportPanelTests, DrawnPlaneToolsUseClickDownPositionForFirstPoint) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    const std::filesystem::path drawnPlaneToolPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "tools" / "base" / "DrawnPlaneTool.cpp";
    std::ifstream sourceFile(sourcePath);
    std::ifstream drawnPlaneToolFile(drawnPlaneToolPath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(drawnPlaneToolFile.is_open()) << drawnPlaneToolPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string drawnPlaneTool{
            std::istreambuf_iterator<char>(drawnPlaneToolFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const QVec2 leftClickLocal"), std::string::npos);
    EXPECT_NE(drawnPlaneTool.find("pointContext.makeEventAt(pointer.leftClickLocal, true)"), std::string::npos);
    EXPECT_NE(drawnPlaneTool.find("pointContext.makeEventAt(pointer.leftClickLocal, false)"), std::string::npos);
    EXPECT_NE(drawnPlaneTool.find("context.modeling.updateCutHover(pointContext.makeEventAt(pointer.mouseLocal, true))"), std::string::npos);
    EXPECT_NE(drawnPlaneTool.find("context.modeling.updateMirrorHover(pointContext.makeEventAt(pointer.mouseLocal, true))"), std::string::npos);
    EXPECT_EQ(drawnPlaneTool.find("afterClick.cutDraggingNewPoint"), std::string::npos);
    EXPECT_EQ(drawnPlaneTool.find("afterClick.mirrorDraggingNewPoint"), std::string::npos);
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

TEST(ViewportPanelTests, TransformPreviewCapturesShiftTopologyOperationAtDragBegin) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const bool topologyModifier = componentSelectionMode && io.KeyShift"), std::string::npos);
    EXPECT_NE(source.find("topologyModifier && tool == ViewportToolId::Move ? ModelingTransformOperation::Extrude"), std::string::npos);
    EXPECT_NE(source.find("topologyModifier && tool == ViewportToolId::Scale ? ModelingTransformOperation::Inset"), std::string::npos);
    EXPECT_NE(source.find(".operation = operation"), std::string::npos);
    EXPECT_NE(source.find("modeling.beginTransformDrag({"), std::string::npos);
    EXPECT_NE(source.find("modeling.updateTransformDrag({"), std::string::npos);
    EXPECT_EQ(source.find("runtimeTransformPreviewExtrude_"), std::string::npos);
    EXPECT_EQ(source.find("runtimeTransformPreviewInset_"), std::string::npos);
}

TEST(ViewportPanelTests, TransformPreviewCapturesShiftDuplicateOperationAtDragBegin) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    const std::size_t duplicateStart =
            source.find("const bool duplicateSelected = !componentSelectionMode && io.KeyShift");
    const std::size_t beginPreview = source.find("modeling.beginTransformDrag({");
    const std::size_t duplicateField = source.find(".duplicateSelected = duplicateSelected", beginPreview);
    const std::size_t updatePreview = source.find("modeling.updateTransformDrag({");
    const std::size_t localFallback =
            source.find("for (const SceneTransformDragTarget& start : sceneTransformDrag_.targets())", updatePreview);
    const std::size_t commitPreview = source.find("modeling.commitTransformDrag()");

    ASSERT_NE(duplicateStart, std::string::npos);
    ASSERT_NE(beginPreview, std::string::npos);
    ASSERT_NE(duplicateField, std::string::npos);
    ASSERT_NE(updatePreview, std::string::npos);
    ASSERT_NE(localFallback, std::string::npos);
    ASSERT_NE(commitPreview, std::string::npos);
    EXPECT_LT(duplicateStart, beginPreview);
    EXPECT_LT(beginPreview, duplicateField);
    EXPECT_LT(updatePreview, localFallback);
    EXPECT_EQ(source.find("modeling.beginTransformPreview(duplicateSelected)"), std::string::npos);
    EXPECT_EQ(source.find("modeling.recordRepeatableTransform"), std::string::npos);
    EXPECT_EQ(source.find("runtimeTransformPreviewDuplicateSelected_"), std::string::npos);
}

TEST(ViewportPanelTests, TransformDragRuntimeStateIsOwnedByModelingService) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("modeling.beginTransformDrag({"), std::string::npos);
    EXPECT_NE(source.find("modeling.updateTransformDrag({"), std::string::npos);
    EXPECT_NE(source.find("modeling.commitTransformDrag()"), std::string::npos);
    EXPECT_EQ(source.find("quader_editor_core::begin_move_drag"), std::string::npos);
    EXPECT_EQ(source.find("quader_editor_core::preview_move_from_viewport"), std::string::npos);
    EXPECT_NE(source.find("quader_editor_core::preview_rotate_from_screen_position"), std::string::npos);
    EXPECT_NE(source.find("*sceneOnlyRotateState_"), std::string::npos);
    EXPECT_EQ(source.find("quader_editor_core::preview_scale_from_screen_position"), std::string::npos);
}

TEST(ViewportPanelTests, TransformGizmoSupportsRegisteredRuntimeTransformTools) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::size_t supportGate = source.find("bool TransformGizmoController::supportsViewportTool");
    const std::size_t runtimeMapping = source.find("quader_editor_core::ViewportInteractionTool TransformGizmoController::runtimeToolFor");
    ASSERT_NE(supportGate, std::string::npos);
    ASSERT_NE(runtimeMapping, std::string::npos);

    EXPECT_NE(source.find("tool == ViewportToolId::Extend", supportGate), std::string::npos);
    EXPECT_NE(source.find("tool == ViewportToolId::Extrude", supportGate), std::string::npos);
    EXPECT_NE(source.find("case ViewportToolId::Extend:", runtimeMapping), std::string::npos);
    EXPECT_NE(source.find("return quader_editor_core::ViewportInteractionTool::extend;", runtimeMapping), std::string::npos);
    EXPECT_NE(source.find("case ViewportToolId::Extrude:", runtimeMapping), std::string::npos);
    EXPECT_NE(source.find("return quader_editor_core::ViewportInteractionTool::extrude;", runtimeMapping), std::string::npos);
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

TEST(ViewportPanelTests, TransformGizmoAppliesRotationPreviewInWorldSpace) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("localTransformAfterWorldQuaternionRotation("), std::string::npos);
    EXPECT_NE(source.find("delta * startWorldRotation"), std::string::npos);
    EXPECT_NE(source.find("glm::inverse(parentWorldRotation) * nextWorldRotation"), std::string::npos);
    EXPECT_NE(source.find("positiveEulerRadians(equivalentEulerRadiansClosestTo("), std::string::npos);
    EXPECT_NE(source.find("rotationEulerReference(start)"), std::string::npos);
    EXPECT_NE(source.find("start.worldTransform"), std::string::npos);
    EXPECT_EQ(source.find("worldRotationAroundPivotMatrix("), std::string::npos);
    EXPECT_EQ(source.find("localTransformAfterWorldDelta("), std::string::npos);
    EXPECT_EQ(source.find("applyDocumentWorldRotationDeltaAroundPivot("), std::string::npos);
    EXPECT_EQ(source.find("previewRotationDelta"), std::string::npos);
    EXPECT_EQ(source.find("next.rotationEuler = next.rotationEuler + previewRotationDelta"), std::string::npos);
}

TEST(ViewportPanelTests, SceneOnlySelectionsUseDocumentGizmoDrag) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("bool sceneOnlyDrag_"), std::string::npos);
    EXPECT_NE(header.find("std::optional<quader_editor_core::RotateToolState> sceneOnlyRotateState_"), std::string::npos);

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    const std::filesystem::path servicePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "transform" / "SceneTransformDragService.cpp";
    std::ifstream sourceFile(sourcePath);
    std::ifstream serviceFile(servicePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(serviceFile.is_open()) << servicePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string service{
            std::istreambuf_iterator<char>(serviceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("selectedHasSceneOnlyTransform"), std::string::npos);
    EXPECT_NE(source.find("selectedHasHierarchyTransform"), std::string::npos);
    EXPECT_NE(source.find("const bool objectDuplicateRequested = !componentSelectionMode && io.KeyShift"), std::string::npos);
    EXPECT_NE(source.find("componentSelectionMode || !selectedHasHierarchyTransform || objectDuplicateRequested"), std::string::npos);
    EXPECT_NE(source.find("activeAxis_ = fallbackHit()"), std::string::npos);
    EXPECT_NE(source.find("parentLocalVectorForWorldVector(document, start.entity"), std::string::npos);
    EXPECT_NE(service.find("documentLocalPivotForWorldPoint(document, entity, worldPivot)"), std::string::npos);
    EXPECT_NE(service.find("hasSelectedAncestor(document, entity)"), std::string::npos);
    EXPECT_NE(source.find("sceneTransformDrag_.begin(document, pivot"), std::string::npos);
    EXPECT_NE(source.find("sceneOnlyDrag_ = true"), std::string::npos);
    EXPECT_NE(source.find("quader_editor_core::begin_rotate_drag({"), std::string::npos);
    EXPECT_NE(source.find("if (wasDragging && !sceneOnlyDrag_)"), std::string::npos);
    EXPECT_NE(source.find("if (!sceneOnlyDrag_)"), std::string::npos);
    EXPECT_NE(source.find("transformPoint(document.worldTransformMatrix(selected), QVec3{})"), std::string::npos);
}

TEST(ViewportPanelTests, RemovesViewportPaddingAndUsesBoxPreviewOutline) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    const std::filesystem::path overlayRendererPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ModalToolOverlayRenderer.cpp";
    std::ifstream sourceFile(sourcePath);
    std::ifstream overlayRendererFile(overlayRendererPath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(overlayRendererFile.is_open()) << overlayRendererPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string overlayRenderer{
            std::istreambuf_iterator<char>(overlayRendererFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("ImGuiStyleVar_WindowPadding"), std::string::npos);
    EXPECT_NE(source.find("ImGuiStyleVar_WindowBorderSize"), std::string::npos);
    EXPECT_NE(source.find("ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse"), std::string::npos);
    EXPECT_NE(overlayRenderer.find("IM_COL32(255, 235, 41, 209)"), std::string::npos);
    EXPECT_EQ(overlayRenderer.find("preview.footprint[0], preview.footprint[2]"), std::string::npos);
    EXPECT_EQ(overlayRenderer.find("preview.footprint[1], preview.footprint[3]"), std::string::npos);
}

TEST(ViewportPanelTests, SuppressesHoverPickingDuringCameraNavigation) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    const std::filesystem::path transformGizmoPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "TransformGizmoController.cpp";
    std::ifstream sourceFile(sourcePath);
    std::ifstream transformGizmoFile(transformGizmoPath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();
    ASSERT_TRUE(transformGizmoFile.is_open()) << transformGizmoPath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};
    const std::string transformGizmoSource{
            std::istreambuf_iterator<char>(transformGizmoFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const bool suppressHoverWork = result.navigationActive || result.cameraChanged"), std::string::npos);
    EXPECT_NE(source.find("transformGizmoController_.draw({"), std::string::npos);
    EXPECT_NE(source.find(".suppressHoverWork = suppressHoverWork"), std::string::npos);
    EXPECT_NE(source.find(".suppressHoverWork = true"), std::string::npos);
    EXPECT_NE(transformGizmoSource.find("if (interactive && activeSelectionMode == ModelingSelectionMode::Object) {\n        const std::vector<EntityId> selectedEntities = document.selectedEntities();"), std::string::npos);
    EXPECT_EQ(transformGizmoSource.find("if (interactive && activeSelectionMode != ModelingSelectionMode::Group) {\n        const std::vector<EntityId> selectedEntities = document.selectedEntities();"), std::string::npos);
    EXPECT_NE(transformGizmoSource.find("request.selection.setObjectSelection("), std::string::npos);
    EXPECT_EQ(transformGizmoSource.find("syncObjectsFromScene()"), std::string::npos);
    EXPECT_NE(transformGizmoSource.find("mouseInsideViewport && !request.suppressHoverWork"), std::string::npos);
    EXPECT_NE(source.find("hoverController_.invalidate()"), std::string::npos);
    EXPECT_NE(source.find("hoverController_.claim(hoverWorkKey)"), std::string::npos);
    EXPECT_NE(source.find("result.gridPresetChanged"), std::string::npos);
    EXPECT_NE(source.find("uiContext.editor.selection().hover(makeSelectionHoverRequestAt(input.mouseLocal, input.modifiers.primary))"), std::string::npos);
    EXPECT_NE(source.find("if (hoverUpdate.changed)"), std::string::npos);
    EXPECT_NE(source.find("const QuaderApiVoidResult clearHover = uiContext.editor.selection().clearHover()"), std::string::npos);
    EXPECT_EQ(source.find("hoverController_.remember("), std::string::npos);
    EXPECT_NE(source.find("const ModelingSelectionSummary hoverSelectionSummary = uiContext.editor.selection().summary();\n    auto makeViewportHoverWorkKey"), std::string::npos);
    EXPECT_EQ(source.find("auto makeViewportHoverWorkKey = [&](bool consumedByGizmo) {\n        const ModelingSelectionSummary hoverSelectionSummary = uiContext.editor.selection().summary();"), std::string::npos);
    EXPECT_EQ(source.find("auto preserveSelectionHoverAfterSelection = [&]()"), std::string::npos);
    EXPECT_NE(source.find("auto suppressSelectionHoverAfterSelection = [&](QVec2 mouseLocal, ModelingHoverTarget hoveredTargetBeforeSelection = {})"), std::string::npos);
    EXPECT_NE(source.find("if (hoverTargetValid(hoveredTargetBeforeSelection)) {\n            selectionHoverSuppressedTarget_ = hoveredTargetBeforeSelection;"), std::string::npos);
    EXPECT_NE(source.find("uiContext.editor.selection().previewHover(makeSelectionHoverRequestAt(mouseLocal, input.modifiers.primary))"), std::string::npos);
    EXPECT_NE(source.find("selectionHoverSuppressedTarget_ = hover.target"), std::string::npos);
    EXPECT_NE(source.find("bool selectionHoverSuppressedThisFrame = false"), std::string::npos);
    EXPECT_NE(source.find("hoverTargetsMatch(*selectionHoverSuppressedTarget_, hover.target)"), std::string::npos);
    EXPECT_NE(source.find("if (!input.modifiers.primary) {\n                    const QuaderApiVoidResult clearHover = uiContext.editor.selection().clearHover();"), std::string::npos);
    EXPECT_NE(source.find("!selectionHoverSuppressedThisFrame"), std::string::npos);
    EXPECT_NE(source.find("const ModelingHoverTarget hoveredBeforeSelection = uiContext.editor.selection().hoverSummary().target;\n            const ModelingSelectionSummary summary = selectionController_.pickSelection"), std::string::npos);
    EXPECT_NE(source.find("paintSelectionActive_ = componentSelectionMode(summary.mode);\n            suppressSelectionHoverAfterSelection(input.mouseLocal, hoveredBeforeSelection);"), std::string::npos);
    EXPECT_NE(source.find("!suppressHoverWork &&\n                !gizmoConsumed"), std::string::npos);
    EXPECT_NE(source.find("selectionHoverClearIssued_"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCache_.markDirty()"), std::string::npos);
    EXPECT_EQ(source.find("|| suppressHoverWork) &&"), std::string::npos);
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
    EXPECT_NE(builderSource.find("frame.lines.size()"), std::string::npos);
    EXPECT_NE(builderSource.find("frame.points.size()"), std::string::npos);
    EXPECT_NE(builderSource.find("hashFloat(hash, line.widthPixels)"), std::string::npos);
    EXPECT_NE(builderSource.find("hashFloat(hash, point.sizePixels)"), std::string::npos);
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

TEST(ViewportPanelTests, SelectionOverlayCacheKeyIncludesSelectionMode) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPaneOverlayCache.h";
    const std::filesystem::path cachePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPaneOverlayCache.cpp";
    const std::filesystem::path panelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream headerFile(headerPath);
    std::ifstream cacheFile(cachePath);
    std::ifstream panelFile(panelPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();
    ASSERT_TRUE(cacheFile.is_open()) << cachePath.string();
    ASSERT_TRUE(panelFile.is_open()) << panelPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};
    const std::string cache{
            std::istreambuf_iterator<char>(cacheFile),
            std::istreambuf_iterator<char>()};
    const std::string panel{
            std::istreambuf_iterator<char>(panelFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("ModelingSelectionMode selectionMode"), std::string::npos);
    EXPECT_NE(header.find("std::uint64_t overlaySignature"), std::string::npos);
    EXPECT_NE(cache.find("key_.selectionMode != key.selectionMode"), std::string::npos);
    EXPECT_NE(cache.find("key_.overlaySignature != key.overlaySignature"), std::string::npos);
    EXPECT_NE(panel.find(".selectionMode = overlaySummary.mode"), std::string::npos);
    EXPECT_NE(panel.find(".overlaySignature = modeling.selectionOverlaySignature()"), std::string::npos);
    EXPECT_EQ(panel.find("toolOverlaySignature("), std::string::npos);
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

    const std::filesystem::path viewportPanelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream viewportPanelFile(viewportPanelPath);
    ASSERT_TRUE(viewportPanelFile.is_open()) << viewportPanelPath.string();
    const std::string viewportPanelSource{
            std::istreambuf_iterator<char>(viewportPanelFile),
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
    const std::size_t beginFrame = appSource.find("renderSystem_.beginFrame()");
    ASSERT_NE(prepareViewports, std::string::npos);
    ASSERT_NE(beginFrame, std::string::npos);
    EXPECT_LT(prepareViewports, beginFrame);

    EXPECT_NE(viewportPanelSource.find("void ViewportPanel::prepareViewports("), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ViewportWorkspaceService& workspace"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("const SceneDocument& document"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("const AssetRegistry& assets"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.setOverlayVisible(true)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("renderBridge.setOverlayFrame(\n                viewportPaneIndex(paneId)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("viewportGridLayerMask(viewportPaneIndex(paneId))"), std::string::npos);
    EXPECT_NE(overlayProxyFactorySource.find(".layerMask(kViewportAllLayerMask, layerMask)"), std::string::npos);
    EXPECT_NE(overlayCacheSource.find("void OverlayRenderableCache::rebuildSlot(OverlaySlot& slot)"), std::string::npos);
}

TEST(ImGuiFilamentRendererTests, EncodesLinearViewportTexturesOnlyWhenRequested) {
    const std::filesystem::path rendererPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "ImGuiFilamentRenderer.cpp";
    std::ifstream rendererFile(rendererPath);
    ASSERT_TRUE(rendererFile.is_open()) << rendererPath.string();

    const std::string rendererSource{
            std::istreambuf_iterator<char>(rendererFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(rendererSource.find(".sampleColorSpace = textures_->sampleColorSpace(command.GetTexID())"), std::string::npos);
    EXPECT_NE(rendererSource.find("primitive.sampleColorSpace == TextureSampleColorSpace::LinearSrgb"), std::string::npos);
    EXPECT_NE(rendererSource.find("setParameter(\"encodeToSrgb\", encodeToSrgb)"), std::string::npos);
    EXPECT_NE(rendererSource.find("fontTextureId_ = textures_->registerTexture(fontTexture_)"), std::string::npos);

    const std::filesystem::path materialPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "materials" / "imgui.mat";
    std::ifstream materialFile(materialPath);
    ASSERT_TRUE(materialFile.is_open()) << materialPath.string();

    const std::string materialSource{
            std::istreambuf_iterator<char>(materialFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(materialSource.find("name : encodeToSrgb"), std::string::npos);
    EXPECT_NE(materialSource.find("vec3 linearToSrgb(vec3 value)"), std::string::npos);
    EXPECT_NE(materialSource.find("mix(sampled.rgb, linearToSrgb(sampled.rgb), clamp(materialParams.encodeToSrgb"), std::string::npos);
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
    EXPECT_NE(headerSource.find("TextureSampleColorSpace sampleColorSpace(ImTextureID id) const"), std::string::npos);

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

TEST(UIStyleTokensTests, LoadsInterAtConfiguredEighteenPixelBaseSize) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UIStyleTokens.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UIStyleTokens.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("fontSize = 18.0f"), std::string::npos);
    EXPECT_NE(source.find("AddFontFromFileTTF(fontPath.string().c_str(), tokens.fontSize"), std::string::npos);
    EXPECT_EQ(source.find("AddFontFromFileTTF(fontPath.string().c_str(), 16.0f"), std::string::npos);
    EXPECT_EQ(source.find("AddFontFromFileTTF(fontPath.string().c_str(), 15.0f"), std::string::npos);
    EXPECT_NE(source.find("io.FontDefault = font"), std::string::npos);
}

TEST(UILayoutTests, AvoidsNegativeFillWidthsAndSavedStretchColumnsInPanels) {
    const std::filesystem::path uiPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui";

    for (const std::filesystem::directory_entry& entry : std::filesystem::recursive_directory_iterator(uiPath)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const std::filesystem::path path = entry.path();
        if (path.extension() != ".cpp" && path.extension() != ".h") {
            continue;
        }

        std::ifstream sourceFile(path);
        ASSERT_TRUE(sourceFile.is_open()) << path.string();
        const std::string source{
                std::istreambuf_iterator<char>(sourceFile),
                std::istreambuf_iterator<char>()};

        EXPECT_EQ(source.find("SetNextItemWidth(-1.0f)"), std::string::npos) << path.string();
        EXPECT_EQ(source.find("ImGuiTableFlags_SizingStretchProp"), std::string::npos) << path.string();
    }

    const std::filesystem::path layoutPath = uiPath / "UILayout.h";
    std::ifstream layoutFile(layoutPath);
    ASSERT_TRUE(layoutFile.is_open()) << layoutPath.string();
    const std::string layoutSource{
            std::istreambuf_iterator<char>(layoutFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(layoutSource.find("uiFillWidthAtLeast"), std::string::npos);
    EXPECT_NE(layoutSource.find("std::max(ImGui::GetContentRegionAvail().x, minimumWidth)"), std::string::npos);
    EXPECT_NE(layoutSource.find("setNextWindowMinWidth"), std::string::npos);
    EXPECT_NE(layoutSource.find("ImGui::SetNextWindowSizeConstraints"), std::string::npos);
}

TEST(InspectorPanelTests, TransformInputsUseResponsiveRowsAndWorldRotationDeltas) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "InspectorPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("kMinAxisInputWidth"), std::string::npos);
    EXPECT_EQ(source.find("availableWidth < minimumInlineWidth"), std::string::npos);
    EXPECT_NE(source.find("std::max((availableWidth - kTransformLabelColumnWidth) / 3.0f"), std::string::npos);
    EXPECT_NE(source.find("InspectorPanel::minimumWidth"), std::string::npos);
    EXPECT_NE(source.find("context.style.sidebarMinWidth"), std::string::npos);
    EXPECT_NE(source.find("ImGuiTableFlags_SizingFixedFit"), std::string::npos);
    EXPECT_NE(source.find("ImGuiTableFlags_NoSavedSettings"), std::string::npos);
    EXPECT_NE(source.find("inputTextWithLeftLabel(\"name\""), std::string::npos);
    EXPECT_NE(source.find("ImGui::CollapsingHeader(transformLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)"), std::string::npos);
    EXPECT_NE(source.find("context.editor.scene().setMeshComponent"), std::string::npos);
    EXPECT_NE(source.find("ui.inspector.double_sided"), std::string::npos);
    EXPECT_NE(source.find("ui.inspector.hidden"), std::string::npos);
    EXPECT_NE(source.find(".after = !hidden"), std::string::npos);
    EXPECT_NE(source.find("const QVec3 displayedRotation = positiveEulerRadians(transform->rotationEuler)"), std::string::npos);
    EXPECT_NE(source.find("const QVec3 previous = displayedRotation"), std::string::npos);
    EXPECT_NE(source.find("applyWorldRotationDeltaAroundPivot(next, delta"), std::string::npos);
    EXPECT_EQ(source.find("next.rotationEuler = next.rotationEuler + delta"), std::string::npos);
}

TEST(OutlinerPanelTests, UsesTreeIconsContextMenuAndGroupingShortcuts) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("entityRecordsTreeOrder()"), std::string::npos);
    EXPECT_NE(source.find("ImGui::TreeNodeEx"), std::string::npos);
    EXPECT_NE(source.find("drawList->AddImage"), std::string::npos);
    EXPECT_NE(source.find("drawHierarchyGuides"), std::string::npos);
    EXPECT_NE(source.find("context.icons.lock"), std::string::npos);
    EXPECT_NE(source.find("context.icons.hide"), std::string::npos);
    EXPECT_NE(source.find("drawOutlinerText"), std::string::npos);
    EXPECT_NE(source.find("EntityId activeSelection = record.id"), std::string::npos);
    EXPECT_NE(source.find("context.editor.selection().selectObjects"), std::string::npos);
    EXPECT_EQ(source.find("syncSelectionToModeling"), std::string::npos);
    EXPECT_EQ(source.find("previousMode == ModelingSelectionMode::Group"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.selection().setMode(ModelingSelectionMode::Group)"), std::string::npos);
    EXPECT_EQ(source.find("selectionTargetForMode"), std::string::npos);
    EXPECT_EQ(source.find("nearestGroupAncestor(context.document"), std::string::npos);
    EXPECT_NE(source.find("ImGui::OpenPopupOnItemClick(\"EntityContext\""), std::string::npos);
    EXPECT_NE(source.find("ImGui::BeginPopup(\"EntityContext\")"), std::string::npos);
    EXPECT_NE(source.find("ui.outliner.delete"), std::string::npos);
    EXPECT_NE(source.find("ui.outliner.duplicate"), std::string::npos);
    EXPECT_NE(source.find("ui.outliner.group"), std::string::npos);
    EXPECT_NE(source.find("ui.outliner.ungroup"), std::string::npos);
    EXPECT_NE(source.find("ImGui::MenuItem(deleteLabel.c_str())"), std::string::npos);
    EXPECT_NE(source.find("ImGui::MenuItem(duplicateLabel.c_str())"), std::string::npos);
    EXPECT_NE(source.find("ImGui::MenuItem(groupLabel.c_str())"), std::string::npos);
    EXPECT_NE(source.find("ImGui::MenuItem(ungroupLabel.c_str()"), std::string::npos);
    EXPECT_NE(source.find("ImGuiKey_G"), std::string::npos);
    EXPECT_NE(source.find("context.editor.commands().execute(QuaderCommand::GroupSelection)"), std::string::npos);
    EXPECT_NE(source.find("context.editor.commands().execute(QuaderCommand::UngroupSelection)"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().groupSelection"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().ungroupSelection"), std::string::npos);
}

TEST(OutlinerPanelTests, RoutesDocumentMutationsThroughPublicApi) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("context.editor.selection().selectObjects"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().select"), std::string::npos);
    EXPECT_NE(source.find("context.editor.scene().duplicateSelectedHierarchy"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().createEntity"), std::string::npos);
    EXPECT_NE(source.find("context.editor.commands().execute(QuaderCommand::DeleteSelection)"), std::string::npos);
    EXPECT_NE(source.find("context.editor.commands().execute(QuaderCommand::GroupSelection)"), std::string::npos);
    EXPECT_NE(source.find("context.editor.commands().execute(QuaderCommand::UngroupSelection)"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.commands().execute(QuaderCommand::DuplicateSelection)"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().reparent"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().groupSelection"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().ungroupSelection"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().rename"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().setTransform"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().setVisible"), std::string::npos);
    EXPECT_EQ(source.find("context.editor.scene().setLocked"), std::string::npos);
    EXPECT_EQ(source.find("cloneBySource"), std::string::npos);
    EXPECT_EQ(source.find("copyCommonEntityState"), std::string::npos);
    EXPECT_EQ(source.find("duplicateModelingEntities"), std::string::npos);

    EXPECT_EQ(source.find("document.clearSelection("), std::string::npos);
    EXPECT_EQ(source.find("document.setSelected("), std::string::npos);
    EXPECT_EQ(source.find("document.setActiveSelection("), std::string::npos);
    EXPECT_EQ(source.find("document.createEntity("), std::string::npos);
    EXPECT_EQ(source.find("document.createEmpty("), std::string::npos);
    EXPECT_EQ(source.find("document.destroyEntity("), std::string::npos);
    EXPECT_EQ(source.find("document.setParent("), std::string::npos);
    EXPECT_EQ(source.find("document.setTransform("), std::string::npos);
    EXPECT_EQ(source.find("document.setEntityKind("), std::string::npos);
    EXPECT_EQ(source.find("context.document.clearSelection("), std::string::npos);
    EXPECT_EQ(source.find("context.document.setSelected("), std::string::npos);
}

TEST(OutlinerPanelTests, DeleteSelectionUsesUnifiedDeleteCommandWithoutSplitHierarchyDelete) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    const std::size_t start = source.find("void deleteSelection(UIContext& context)");
    ASSERT_NE(start, std::string::npos);
    const std::size_t end = source.find("void duplicateSelection(UIContext& context)", start);
    ASSERT_NE(end, std::string::npos);
    const std::string body = source.substr(start, end - start);

    EXPECT_NE(body.find("context.editor.commands().execute(QuaderCommand::DeleteSelection)"), std::string::npos);
    EXPECT_EQ(body.find("selectedHierarchyEntities"), std::string::npos);
    EXPECT_EQ(body.find("context.editor.scene().destroy"), std::string::npos);
    EXPECT_EQ(body.find("selectOnly(context, modelingEntities"), std::string::npos);
    EXPECT_EQ(body.find("context.modeling.syncObjectSelectionFromScene()"), std::string::npos);
}

TEST(OutlinerPanelTests, DuplicateSelectionDelegatesHierarchyPolicyToSceneApi) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    const std::size_t start = source.find("void duplicateSelection(UIContext& context)");
    ASSERT_NE(start, std::string::npos);
    const std::size_t end = source.find("ImTextureID iconFor", start);
    ASSERT_NE(end, std::string::npos);
    const std::string body = source.substr(start, end - start);

    EXPECT_NE(body.find("context.editor.scene().duplicateSelectedHierarchy()"), std::string::npos);
    EXPECT_EQ(body.find("context.editor.commands().execute(QuaderCommand::DuplicateSelection)"), std::string::npos);
    EXPECT_EQ(body.find("context.editor.scene().createEntity"), std::string::npos);
    EXPECT_EQ(body.find("context.editor.scene().reparent"), std::string::npos);
    EXPECT_EQ(body.find("context.editor.scene().rename"), std::string::npos);
    EXPECT_EQ(body.find("context.editor.scene().setTransform"), std::string::npos);
    EXPECT_EQ(body.find("cloneBySource"), std::string::npos);
}

TEST(OutlinerPanelTests, UsesSingleSelectedTreeRowFill) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("ImGuiTreeNodeFlags_Selected"), std::string::npos);
    EXPECT_NE(source.find("drawOutlinerText("), std::string::npos);
    EXPECT_EQ(source.find("ImGui::Selectable(record.name.c_str(), record.selected"), std::string::npos);
    EXPECT_EQ(source.find("ImGuiSelectableFlags_AllowOverlap"), std::string::npos);
}

TEST(OutlinerPanelTests, KeepsRootRowsTightlyInset) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("constexpr float kOutlinerRowLeftPadding = 3.0f"), std::string::npos);
    EXPECT_NE(source.find("ImGuiStyleVar_IndentSpacing"), std::string::npos);
    EXPECT_NE(source.find("ImGuiStyleVar_FramePadding"), std::string::npos);
    EXPECT_NE(source.find("rowMin.x + ImGui::GetTreeNodeToLabelSpacing()"), std::string::npos);
    EXPECT_NE(source.find("drawList->AddImage(icon"), std::string::npos);
    EXPECT_EQ(source.find("ImGui::SetCursorScreenPos"), std::string::npos);
}

TEST(UIPanelLocalizationTests, DockedPanelsUseLocalizedTitlesWithStableWindowIds) {
    const std::filesystem::path uiPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui";
    std::ifstream dockPanelFile(uiPath / "UIDockPanel.h");
    std::ifstream viewportPanelFile(uiPath / "ViewportPanel.cpp");
    ASSERT_TRUE(dockPanelFile.is_open());
    ASSERT_TRUE(viewportPanelFile.is_open());

    const std::string dockPanelSource{
            std::istreambuf_iterator<char>(dockPanelFile),
            std::istreambuf_iterator<char>()};
    const std::string viewportPanelSource{
            std::istreambuf_iterator<char>(viewportPanelFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(dockPanelSource.find("context.localization.imguiLabel(uiPanelLocalizationKey(id()), uiPanelKey(id()), title())"),
            std::string::npos);
    EXPECT_NE(dockPanelSource.find("ImGui::Begin(windowTitle.c_str(), nullptr"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("uiContext.localization.imguiLabel(\"ui.panel.viewport\", uiPanelKey(UIPanelId::Viewport), \"Viewport\")"),
            std::string::npos);
    EXPECT_NE(viewportPanelSource.find("ImGui::Begin("), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("windowTitle.c_str()"), std::string::npos);
}

TEST(UIStyleTokensTests, KeepsDockWindowMenuButtonAvailable) {
    const std::filesystem::path stylePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UIStyleTokens.cpp";
    std::ifstream styleFile(stylePath);
    ASSERT_TRUE(styleFile.is_open()) << stylePath.string();

    const std::string styleSource{
            std::istreambuf_iterator<char>(styleFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(styleSource.find("style.WindowMenuButtonPosition = ImGuiDir_Left"), std::string::npos);
    EXPECT_EQ(styleSource.find("style.WindowMenuButtonPosition = ImGuiDir_None"), std::string::npos);

    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UISystem.cpp";
    std::ifstream uiSystemFile(uiSystemPath);
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();

    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(uiSystemSource.find("ImGuiDockNodeFlags_NoWindowMenuButton"), std::string::npos);
}

TEST(UISystemTests, SidebarsUseMinimumWidthAndFlyNavigationCapturesCursor) {
    const std::filesystem::path uiPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui";

    std::ifstream styleFile(uiPath / "UIStyleTokens.h");
    ASSERT_TRUE(styleFile.is_open());
    const std::string styleSource{
            std::istreambuf_iterator<char>(styleFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(styleSource.find("sidebarMinWidth = 340.0f"), std::string::npos);

    std::ifstream dockPanelFile(uiPath / "UIDockPanel.h");
    ASSERT_TRUE(dockPanelFile.is_open());
    const std::string dockPanelSource{
            std::istreambuf_iterator<char>(dockPanelFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(dockPanelSource.find("setNextWindowMinWidth(minWidth)"), std::string::npos);
    EXPECT_NE(dockPanelSource.find("context.requestPanelFocus(id())"), std::string::npos);

    std::ifstream outlinerFile(uiPath / "OutlinerPanel.cpp");
    ASSERT_TRUE(outlinerFile.is_open());
    const std::string outlinerSource{
            std::istreambuf_iterator<char>(outlinerFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(outlinerSource.find("OutlinerPanel::minimumWidth"), std::string::npos);
    EXPECT_NE(outlinerSource.find("context.style.sidebarMinWidth"), std::string::npos);

    std::ifstream uiSystemFile(uiPath / "UISystem.cpp");
    ASSERT_TRUE(uiSystemFile.is_open());
    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(uiSystemSource.find("viewportSettingsPanel_.draw(context, settingsPanelOpen_)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolSettingsPanel_.draw(context, toolSettingsPanelOpen_)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("icons_.lock = load(\"lock_icon.png\")"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("icons_.hide = load(\"hide_icon.png\")"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_SetWindowRelativeMouseMode(window_, captured)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_GetRelativeMouseState(&deltaX, &deltaY)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_GetRelativeMouseState(&discardX, &discardY)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_WarpMouseInWindow(window_, point.x, point.y)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("enforceSidebarDockMinimums()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("FindWindowByName(windowName)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("clampDockLeafNodeWidth"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGuiStyleVar_WindowMinSize"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("sidebarMinimumRatio"), std::string::npos);

    std::ifstream floatingPanelFile(uiPath / "UIFloatingPanel.h");
    ASSERT_TRUE(floatingPanelFile.is_open());
    const std::string floatingPanelSource{
            std::istreambuf_iterator<char>(floatingPanelFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(floatingPanelSource.find("ImGuiWindowFlags_NoDocking"), std::string::npos);
    EXPECT_NE(floatingPanelSource.find("context.localization.imguiLabel(titleKey(), titleStableId(), title())"), std::string::npos);
    EXPECT_NE(floatingPanelSource.find("ImGui::Begin(windowTitle.c_str(), &open"), std::string::npos);

    std::ifstream toolSettingsPanelFile(uiPath / "ToolSettingsPanel.cpp");
    ASSERT_TRUE(toolSettingsPanelFile.is_open());
    const std::string toolSettingsPanelSource{
            std::istreambuf_iterator<char>(toolSettingsPanelFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(toolSettingsPanelSource.find("ImGui::SetNextWindowViewport(viewport->ID)"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("ImGui::SetNextWindowPos("), std::string::npos);

    std::ifstream contextFile(uiPath / "UIContext.h");
    ASSERT_TRUE(contextFile.is_open());
    const std::string contextSource{
            std::istreambuf_iterator<char>(contextFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(contextSource.find("viewportRelativeMouseDelta"), std::string::npos);
    EXPECT_NE(contextSource.find("viewportCursorCaptureRequested"), std::string::npos);
    EXPECT_NE(contextSource.find("viewportCursorCapturePoint"), std::string::npos);
    EXPECT_NE(contextSource.find("focusedPanelStorage"), std::string::npos);
    EXPECT_NE(contextSource.find("requestPanelFocus"), std::string::npos);

    std::ifstream layoutFile(uiPath / "UILayout.h");
    ASSERT_TRUE(layoutFile.is_open());
    const std::string layoutSource{
            std::istreambuf_iterator<char>(layoutFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(layoutSource.find("panelFocusInputActive"), std::string::npos);

    std::ifstream inspectorFile(uiPath / "InspectorPanel.cpp");
    ASSERT_TRUE(inspectorFile.is_open());
    const std::string inspectorSource{
            std::istreambuf_iterator<char>(inspectorFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(inspectorSource.find("InspectorPanel::minimumWidth"), std::string::npos);

    std::ifstream consoleFile(uiPath / "ConsolePanel.cpp");
    ASSERT_TRUE(consoleFile.is_open());
    const std::string consoleSource{
            std::istreambuf_iterator<char>(consoleFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(consoleSource.find("ConsolePanel::drawContent"), std::string::npos);

    std::ifstream viewportPanelFile(uiPath / "ViewportPanel.cpp");
    std::ifstream viewportFocusFile(uiPath / "ViewportFocusController.cpp");
    std::ifstream modalOverlayFile(uiPath / "ModalToolOverlayRenderer.cpp");
    ASSERT_TRUE(viewportPanelFile.is_open());
    ASSERT_TRUE(viewportFocusFile.is_open());
    ASSERT_TRUE(modalOverlayFile.is_open());
    const std::string viewportPanelSource{
            std::istreambuf_iterator<char>(viewportPanelFile),
            std::istreambuf_iterator<char>()};
    const std::string viewportFocusSource{
            std::istreambuf_iterator<char>(viewportFocusFile),
            std::istreambuf_iterator<char>()};
    const std::string modalOverlaySource{
            std::istreambuf_iterator<char>(modalOverlayFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(modalOverlaySource.find("Middle Mouse to reset"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("result.flyHudActive && !camera.flySpeedIsDefault()"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("focusController_.resolve(focusHovered, uiContext, UIPanelId::Viewport)"), std::string::npos);
    EXPECT_NE(viewportFocusSource.find("uiContext.requestPanelFocus(panelId)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("globalEscCancelRequested"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("mouseClickedOutsideViewport"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("!io.WantCaptureMouse"), std::string::npos);
    EXPECT_EQ(viewportPanelSource.find("flySpeedIndicatorSeconds_"), std::string::npos);
}

TEST(UISystemTests, TopMenuExposesViewportLayoutOptions) {
    const std::filesystem::path mainMenuBarPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "MainMenuBar.cpp";
    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UISystem.cpp";
    std::ifstream mainMenuBarFile(mainMenuBarPath);
    std::ifstream uiSystemFile(uiSystemPath);
    ASSERT_TRUE(mainMenuBarFile.is_open()) << mainMenuBarPath.string();
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();

    const std::string mainMenuBarSource{
            std::istreambuf_iterator<char>(mainMenuBarFile),
            std::istreambuf_iterator<char>()};
    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(mainMenuBarSource.find("void LayoutMenu::draw"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ui.menu.layout"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("layoutLabel(context.localization, option)"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("viewportLayoutStableId(option.id)"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("viewportLayoutOptions()"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("context.editor.viewports().setLayout(option.id)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("mainMenuBar_.draw"), std::string::npos);
}

TEST(UISystemTests, TopMenuExposesLiveViewportSettingsPanel) {
    const std::filesystem::path mainMenuBarPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "MainMenuBar.cpp";
    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UISystem.cpp";
    const std::filesystem::path toolSettingsPanelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ToolSettingsPanel.cpp";
    const std::filesystem::path modelingPreviewCommandServicePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "history" / "ModelingPreviewCommandService.h";
    const std::filesystem::path viewportSettingsPanelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportSettingsPanel.cpp";
    std::ifstream mainMenuBarFile(mainMenuBarPath);
    std::ifstream uiSystemFile(uiSystemPath);
    std::ifstream toolSettingsPanelFile(toolSettingsPanelPath);
    std::ifstream modelingPreviewCommandServiceFile(modelingPreviewCommandServicePath);
    std::ifstream viewportSettingsPanelFile(viewportSettingsPanelPath);
    ASSERT_TRUE(mainMenuBarFile.is_open()) << mainMenuBarPath.string();
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();
    ASSERT_TRUE(toolSettingsPanelFile.is_open()) << toolSettingsPanelPath.string();
    ASSERT_TRUE(modelingPreviewCommandServiceFile.is_open()) << modelingPreviewCommandServicePath.string();
    ASSERT_TRUE(viewportSettingsPanelFile.is_open()) << viewportSettingsPanelPath.string();

    const std::string mainMenuBarSource{
            std::istreambuf_iterator<char>(mainMenuBarFile),
            std::istreambuf_iterator<char>()};
    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};
    const std::string toolSettingsPanelSource{
            std::istreambuf_iterator<char>(toolSettingsPanelFile),
            std::istreambuf_iterator<char>()};
    const std::string modelingPreviewCommandServiceSource{
            std::istreambuf_iterator<char>(modelingPreviewCommandServiceFile),
            std::istreambuf_iterator<char>()};
    const std::string viewportSettingsPanelSource{
            std::istreambuf_iterator<char>(viewportSettingsPanelFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(mainMenuBarSource.find("void EditMenu::draw"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ui.menu.edit"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ui.menu.edit.settings"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ui.menu.edit.tool_settings"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ImGui::MenuItem(settingsLabel.c_str())"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ImGui::MenuItem(toolSettingsLabel.c_str())"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("context.viewportSettingsPanelOpen = true"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("context.toolSettingsPanelOpen = true"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("settingsPanelOpen_"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolSettingsPanelOpen_"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportSettingsPanel_.draw(context, settingsPanelOpen_)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolSettingsPanel_.draw(context, toolSettingsPanelOpen_)"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("context.modeling.toolSettingsTarget()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modeling.toolPreviewMeshes()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("previewMesh.material != InvalidAssetId ? previewMesh.material : defaultMaterial"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("renderBridge.setToolPreviewMeshes(std::move(renderPreviewMeshes), assets)"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("switch (target.kind)"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("context.modeling.setInteractionSettings(settings)"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("open = false;"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("open = result.ok ? false : context.modeling.toolSettingsTarget().active"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("tool_settings.cut.discard_left"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("tool_settings.cut.discard_right"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("tool_settings.cut.keep_both"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("ImGui::RadioButton(discardLeft.c_str()"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("ImGui::RadioButton(discardRight.c_str()"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("ImGui::RadioButton(keepBoth.c_str()"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("context.editor.modeling().commitPreview(historyLabel)"), std::string::npos);
    EXPECT_NE(toolSettingsPanelSource.find("context.editor.modeling().cancelPreview()"), std::string::npos);
    EXPECT_NE(modelingPreviewCommandServiceSource.find("commitActiveOperationPreview()"), std::string::npos);
    EXPECT_NE(modelingPreviewCommandServiceSource.find("cancelActiveOperationPreview()"), std::string::npos);
    EXPECT_EQ(toolSettingsPanelSource.find("No active tool settings."), std::string::npos);
    EXPECT_EQ(toolSettingsPanelSource.find("ImGui::Combo(\"Keep Mode\""), std::string::npos);
    EXPECT_EQ(toolSettingsPanelSource.find("ImGui::CollapsingHeader(\"Bridge\")"), std::string::npos);
    EXPECT_EQ(toolSettingsPanelSource.find("ImGui::CollapsingHeader(\"Edge Bevel\""), std::string::npos);
    EXPECT_EQ(toolSettingsPanelSource.find("ImGui::CollapsingHeader(\"Merge by Distance\""), std::string::npos);
    EXPECT_NE(viewportSettingsPanelSource.find("viewportSettingsSections()"), std::string::npos);
    EXPECT_NE(viewportSettingsPanelSource.find("ImGuiColorEditFlags_AlphaBar"), std::string::npos);
    EXPECT_NE(viewportSettingsPanelSource.find("ImGuiColorEditFlags_NoInputs"), std::string::npos);

    const std::filesystem::path uiContextPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "UIContext.h";
    std::ifstream uiContextFile(uiContextPath);
    ASSERT_TRUE(uiContextFile.is_open()) << uiContextPath.string();

    const std::string uiContextSource{
            std::istreambuf_iterator<char>(uiContextFile),
            std::istreambuf_iterator<char>()};
    EXPECT_EQ(uiContextSource.find("toolSettingsPanelOpenRequested"), std::string::npos);
    EXPECT_EQ(uiContextSource.find("requestToolSettingsPanel"), std::string::npos);
}

TEST(UISystemTests, TopMenuExposesViewportToolsMenu) {
    const std::filesystem::path mainMenuBarPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "MainMenuBar.cpp";
    std::ifstream mainMenuBarFile(mainMenuBarPath);
    ASSERT_TRUE(mainMenuBarFile.is_open()) << mainMenuBarPath.string();

    const std::string mainMenuBarSource{
            std::istreambuf_iterator<char>(mainMenuBarFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(mainMenuBarSource.find("void ToolsMenu::draw"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("ui.menu.tools"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("toolLabel(context.localization, *tool)"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("viewport.tool."), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("viewportToolCatalog()"), std::string::npos);
    EXPECT_NE(mainMenuBarSource.find("activateToolFromMenu(\n                    tool->viewportTool(),\n                    context.editor,\n                    context.document,\n                    context.modeling"),
            std::string::npos);

    const std::filesystem::path toolCatalogPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "editor" / "tools" / "base" / "ViewportToolCatalog.cpp";
    std::ifstream toolCatalogFile(toolCatalogPath);
    ASSERT_TRUE(toolCatalogFile.is_open()) << toolCatalogPath.string();

    const std::string toolCatalogSource{
            std::istreambuf_iterator<char>(toolCatalogFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(toolCatalogSource.find("InsertEdgeLoopTool"), std::string::npos);
    EXPECT_NE(toolCatalogSource.find("KnifeTool"), std::string::npos);
    EXPECT_NE(toolCatalogSource.find("CutTool"), std::string::npos);
    EXPECT_NE(toolCatalogSource.find("PolyTool"), std::string::npos);
    EXPECT_NE(toolCatalogSource.find("MirrorTool"), std::string::npos);

    const std::filesystem::path viewportPanelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "ui" / "ViewportPanel.cpp";
    std::ifstream viewportPanelFile(viewportPanelPath);
    ASSERT_TRUE(viewportPanelFile.is_open()) << viewportPanelPath.string();

    const std::string viewportPanelSource{
            std::istreambuf_iterator<char>(viewportPanelFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(viewportPanelSource.find("ViewportPanel::activateToolFromMenu"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("editor.tools().activate(tool)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("uiContext.editor.tools().activate(inputRouter_.state().activeTool)"), std::string::npos);
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

TEST(ViewportWorkspaceServiceTests, MaximizedPaneStateOwnsActiveLayout) {
    ViewportWorkspaceService workspace;
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
    ViewportWorkspaceService workspace;
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

TEST(ViewportWorkspaceServiceTests, ViewReflectsCameraMutationsOutsideSetView) {
    ViewportWorkspaceService workspace;
    ASSERT_TRUE(workspace.setView(ViewportPaneId::Perspective, ViewportView::Top));
    ASSERT_EQ(workspace.view(ViewportPaneId::Perspective), ViewportView::Top);

    resetViewportCameraForView(workspace.camera(ViewportPaneId::Perspective), ViewportView::Front);

    EXPECT_EQ(workspace.view(ViewportPaneId::Perspective), ViewportView::Front);
    EXPECT_EQ(workspace.pane(ViewportPaneId::Perspective).view, ViewportView::Front);
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

    ModelingTransformDragPreview commit = service.commitTransformDrag();
    ASSERT_TRUE(commit.ok) << commit.message;
    EXPECT_FALSE(commit.previewDriven);
    const ModelingTransformDragPreview afterCommitUpdate = service.updateTransformDrag(testXMoveDragUpdateRequest(3.0f));
    EXPECT_TRUE(afterCommitUpdate.ok);
    EXPECT_FALSE(afterCommitUpdate.active);
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
