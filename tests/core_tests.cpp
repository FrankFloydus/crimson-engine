#include "quader/assets/AssetRegistry.h"
#include "quader/assets/BakeService.h"
#include "quader/assets/MaterialDocument.h"
#include "quader/core/Logger.h"
#include "quader/core/UndoRedoManager.h"
#include "quader/document/DocumentCommandService.h"
#include "quader/document/ProjectDocument.h"
#include "quader/geometry/UVSphere.h"
#include "quader/modeling/ModelingSessionService.h"
#include "quader/render/ViewportOverlayPolicy.h"
#include "quader/scene/SceneBounds.h"
#include "quader/scene/SceneDocument.h"
#include "quader/scene/SceneEntityNaming.h"
#include "quader/scene/TransformPivot.h"
#include "quader/ui/ConsoleModel.h"
#include "quader/ui/UISettings.h"
#include "quader/ui/ViewportContextMenuPolicy.h"
#include "quader/viewport/HostViewportContractAdapter.h"
#include "quader/viewport/ViewportCameraController.h"
#include "quader/viewport/ViewportInputRouter.h"
#include "quader/viewport/ViewportLayout.h"
#include "quader/viewport/ViewportSettings.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <string>
#include <string_view>
#include <vector>

using namespace quader;

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

void expectColorNear(ViewportSettingColor actual, ViewportSettingColor expected) {
    EXPECT_NEAR(actual.r, expected.r, 0.001f);
    EXPECT_NEAR(actual.g, expected.g, 0.001f);
    EXPECT_NEAR(actual.b, expected.b, 0.001f);
    EXPECT_NEAR(actual.a, expected.a, 0.001f);
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
        Vec3 pivot = {},
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "platform" / "FatalErrorHandler.cpp";
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
    EXPECT_NE(handlerSource.find("A stack trace was written to this log file"), std::string::npos);
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
}

TEST(UISettingsTests, RoundTripsPanelVisibility) {
    const std::filesystem::path path =
            std::filesystem::temp_directory_path() / "quader_ui_settings_round_trip.ini";
    std::filesystem::remove(path);

    UISettings settings = UISettings::defaults();
    settings.setPanelVisible(UIPanelId::Outliner, false);
    settings.setPanelVisible(UIPanelId::Inspector, true);
    settings.setPanelVisible(UIPanelId::Console, true);

    ASSERT_TRUE(settings.save(path));
    const UISettings loaded = UISettings::load(path);

    EXPECT_FALSE(loaded.isPanelVisible(UIPanelId::Outliner));
    EXPECT_TRUE(loaded.isPanelVisible(UIPanelId::Inspector));
    EXPECT_TRUE(loaded.isPanelVisible(UIPanelId::Console));

    std::filesystem::remove(path);
}

TEST(ViewportSettingsTests, DefaultsMirrorUnityViewportSettingsState) {
    const ViewportSettings settings = ViewportSettings::defaults();

    ASSERT_EQ(viewportSettingsSections().size(), 8u);
    EXPECT_EQ(viewportSettingsSections()[0].label, "Scene");
    EXPECT_TRUE(viewportSettingsSections()[0].defaultExpanded);
    EXPECT_EQ(viewportSettingsSections()[1].label, "Grid");
    EXPECT_EQ(viewportSettingsSections()[2].label, "Selection");
    EXPECT_EQ(viewportSettingsSections()[3].label, "Overlay");
    EXPECT_EQ(viewportSettingsSections()[4].label, "Vertices");
    EXPECT_EQ(viewportSettingsSections()[5].label, "Measurements");
    EXPECT_EQ(viewportSettingsSections()[6].label, "Picking");
    EXPECT_EQ(viewportSettingsSections()[7].label, "Gizmo");

    expectColorNear(settings.color(ViewportColorSettingId::Background), {0.0509804f, 0.0509804f, 0.0509804f, 1.0f});
    expectColorNear(settings.color(ViewportColorSettingId::SelectionWire), {1.0f, 0.827451f, 0.121569f, 1.0f});
    expectColorNear(settings.color(ViewportColorSettingId::SourceWire), {0.360784f, 0.878431f, 1.0f, 0.921569f});
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::GridMinorLineSize), 0.32f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::SelectionEdgeLineSize), 2.5f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::PickVertexRadius), 0.3f);
    EXPECT_FLOAT_EQ(settings.floatValue(ViewportFloatSettingId::GizmoSize), 1.0f);
    EXPECT_TRUE(viewportBoolSettings().empty());
}

TEST(ViewportSettingsTests, SavesLoadsAndClampsUnityPreferenceKeys) {
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

TEST(GroundGridMaterialTests, UsesUnityStyleUserWorldAnchoring) {
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

TEST(QuaderMeshMaterialTests, MatchesUnitySceneUnlitTextureTransformTimesColor) {
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

TEST(OverlayMaterialTests, LineAndPointMaterialsMirrorUnityScreenSpaceOverlayExpansion) {
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
}

TEST(ViewportOverlayPolicyTests, MatchesUnityLayerDrawOrder) {
    const std::array<ViewportOverlayLayer, kViewportOverlayLayerCount> expected{{
            ViewportOverlayLayer::SelectedVisibleFace,
            ViewportOverlayLayer::SelectedFaceMask,
            ViewportOverlayLayer::SourceWire,
            ViewportOverlayLayer::SelectedXrayFace,
            ViewportOverlayLayer::HoverFace,
            ViewportOverlayLayer::OpenEdge,
            ViewportOverlayLayer::DiagnosticEdge,
            ViewportOverlayLayer::SelectedFaceEdge,
            ViewportOverlayLayer::SelectedEdge,
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

TEST(ViewportOverlayPolicyTests, MatchesUnityDepthAndHoverSuppressionContract) {
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::SourceWire, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::OpenEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::HoverFaceEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::HoverEdge, ViewportOverlayGeometryKind::Lines));
    EXPECT_TRUE(viewportOverlayDepthTested(ViewportOverlayLayer::UnselectedVertex, ViewportOverlayGeometryKind::Points));

    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::SelectedFaceEdge, ViewportOverlayGeometryKind::Lines),
            ViewportOverlayDepthMode::DrawOnTop);
    EXPECT_EQ(viewportOverlayDepthMode(ViewportOverlayLayer::SelectedEdge, ViewportOverlayGeometryKind::Lines),
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

TEST(ViewportOverlayPolicyTests, MatchesUnityColorsAndSdkPointSizes) {
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SelectedVisibleFace, rgba8(255, 255, 255)), kOverlaySelectionFaceColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SelectedFaceEdge, rgba8(255, 255, 255)), kOverlaySelectionWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SourceWire, kOverlaySelectedVertexColor), kOverlaySelectionWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SourceWire, rgba8(255, 255, 255)), kOverlaySourceWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::HoverFace, kOverlayRemoveWireColor), kOverlayRemoveFaceColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::HoverEdge, kOverlayRemoveFaceColor), kOverlayRemoveWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolPlane, rgba8(255, 255, 255)), kOverlayToolPlaneColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolPlaneLine, rgba8(255, 255, 255)), kOverlayToolPlaneLineColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::ToolHandle, rgba8(255, 255, 255)), kOverlayToolHandleColor);

    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::SourceWire, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::SelectedEdge, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::HoverEdge, 3.0f), 3.0f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::OpenEdge, 2.0f), 2.0f);
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

TEST(SceneRenderBridgeTests, UsesUnityDepthStampedFaceFillForOverlayTriangles) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("viewportOverlayLayerUsesDepthStampedFaceFill"), std::string::npos);
    EXPECT_NE(source.find("setColorWrite(state.colorWrite)"), std::string::npos);
    EXPECT_NE(source.find("setDepthWrite(state.depthWrite)"), std::string::npos);
    EXPECT_NE(source.find("setDepthFunc(state.depthFunc)"), std::string::npos);
    EXPECT_NE(source.find("setTransparencyMode(state.transparencyMode)"), std::string::npos);
    EXPECT_NE(source.find("setPolygonOffset(state.polygonOffsetScale, state.polygonOffsetConstant)"), std::string::npos);
    EXPECT_NE(source.find("DepthFunc::GE"), std::string::npos);
    EXPECT_NE(source.find("DepthFunc::E"), std::string::npos);
    EXPECT_NE(source.find("CullingMode::BACK"), std::string::npos);
    EXPECT_NE(source.find("TransparencyMode::TWO_PASSES_ONE_SIDE"), std::string::npos);
    EXPECT_NE(source.find("viewportOverlayTriangleIsTwoSided(layer)"), std::string::npos);
    EXPECT_NE(source.find("CullingMode::NONE"), std::string::npos);
    EXPECT_NE(source.find("TransparencyMode::TWO_PASSES_TWO_SIDES"), std::string::npos);
    EXPECT_NE(source.find("setToolPreviewMeshes(std::vector<PreviewMesh> meshes"), std::string::npos);
    EXPECT_NE(source.find("toolPreviewMeshProxies_"), std::string::npos);
    EXPECT_NE(source.find("destroyToolPreviewMeshes()"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesUnityStyleQuadMaterialsForSelectionLineAndPointOverlay) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("appendOverlayLineQuad"), std::string::npos);
    EXPECT_NE(source.find("appendOverlayPointQuad"), std::string::npos);
    EXPECT_NE(source.find("overlayLineMaterial_->createInstance()"), std::string::npos);
    EXPECT_NE(source.find("overlayPointMaterial_->createInstance()"), std::string::npos);
    EXPECT_NE(source.find("filament::VertexAttribute::CUSTOM0"), std::string::npos);
    EXPECT_NE(source.find("AttributeType::FLOAT4, offsetof(OverlayLineVertex, position)"), std::string::npos);
    EXPECT_NE(source.find("AttributeType::FLOAT4, offsetof(OverlayPointVertex, position)"), std::string::npos);
    EXPECT_NE(source.find("projectOverlayViewPoint"), std::string::npos);
    EXPECT_NE(source.find("nearPlaneDistance(a, camera)"), std::string::npos);
    EXPECT_NE(source.find("setParameter(\"lineWidth\", widthPixels)"), std::string::npos);
    EXPECT_EQ(source.find("setParameter(\"pointSize\", pointSizePixels)"), std::string::npos);
    EXPECT_EQ(source.find("setParameter(\"nearPlane\""), std::string::npos);
    EXPECT_NE(source.find("LineBatchKey"), std::string::npos);
    EXPECT_EQ(source.find("PrimitiveType::LINES"), std::string::npos);
    EXPECT_EQ(source.find("PrimitiveType::POINTS"), std::string::npos);
}

TEST(SceneRenderBridgeTests, BindsNormalsAndBuildsPrimitiveSpans) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
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
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("syncedAssetRevision_"), std::string::npos);
    EXPECT_NE(source.find("renderAssetRevisionsChanged"), std::string::npos);
    EXPECT_NE(source.find("existing->second.meshRevision != mesh->revision.value"), std::string::npos);
    EXPECT_NE(source.find("existing->second.materialRevision != materialRevision"), std::string::npos);
    EXPECT_NE(source.find("updateMeshProxyTransform(existing->second, worldTransform)"), std::string::npos);
    const std::size_t rebuildStart = source.find("void SceneRenderBridge::rebuild(");
    const std::size_t overlayStart = source.find("void SceneRenderBridge::rebuildOverlayProxies", rebuildStart);
    ASSERT_NE(rebuildStart, std::string::npos);
    ASSERT_NE(overlayStart, std::string::npos);
    const std::string rebuildSource = source.substr(rebuildStart, overlayStart - rebuildStart);
    EXPECT_EQ(rebuildSource.find("meshProxies_.clear()"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesAnisotropicSamplingForMeshTextures) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "RenderSystem.cpp";
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("refreshCachedSelectionOverlayFrame(document, assets, modeling)"), std::string::npos);
    EXPECT_NE(source.find("appendHierarchySelectionWires(cachedSelectionOverlayFrame_, document, assets)"), std::string::npos);
    EXPECT_NE(source.find("cachedSelectionOverlayAssetRevision_ != assets.revision()"), std::string::npos);
    EXPECT_NE(source.find("renderBridge.setOverlayFrame(\n                viewportPaneIndex(paneId)"), std::string::npos);
    EXPECT_NE(source.find("makeViewportOverlayCamera(paneState.camera, {paneState.renderSize.x, paneState.renderSize.y})"), std::string::npos);
    EXPECT_EQ(source.find("renderBridge.setOverlayFrame(cachedSelectionOverlayFrame_, makeViewportOverlayCamera(camera, itemSize));"), std::string::npos);
    EXPECT_EQ(source.find("drawSelectionOverlay2d"), std::string::npos);
    EXPECT_EQ(source.find("drawVisibilityClippedOverlayLine"), std::string::npos);
    EXPECT_EQ(source.find("pointVisibleFromCamera"), std::string::npos);
    EXPECT_EQ(source.find("rayIntersectsTriangle(Vec3 origin"), std::string::npos);
    EXPECT_EQ(source.find("renderBridge.setOverlayFrame(selectionOverlay, camera"), std::string::npos);
    EXPECT_EQ(source.find("shouldDrawOverlayLayer2d"), std::string::npos);
    EXPECT_EQ(source.find("layerNeedsSceneVisibility2d"), std::string::npos);
}

TEST(ViewportPanelTests, DrawsPassiveDecorationsForNonInputPanes) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("void ViewportPanel::drawPaneDecorations("), std::string::npos);
    EXPECT_NE(source.find("drawViewCube(paneState.camera, itemMin, itemSize, false)"), std::string::npos);
    EXPECT_NE(source.find("drawTransformGizmo(\n            document,\n            assets,\n            undoRedo,\n            documentCommands,\n            modeling,\n            paneState.camera"), std::string::npos);
    EXPECT_NE(source.find("true,\n            false);"), std::string::npos);
    EXPECT_NE(source.find("if (!interactive || !mouseInsideViewport || !ImGui::IsMouseClicked(ImGuiMouseButton_Left))"), std::string::npos);
    EXPECT_NE(source.find("if (!interactive) {\n        return false;\n    }\n\n    auto fallbackGizmoHit = [&]()"), std::string::npos);
    EXPECT_NE(source.find("bool consumed = gizmoDragging_;"), std::string::npos);
}

TEST(ViewportPanelTests, GizmoSizeUsesSdkScaleWithoutReplacingActiveGrid) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("input.grid_size = gridSize"), std::string::npos);
    EXPECT_NE(source.find("input.gizmo_size_pixels = 68.0;"), std::string::npos);
    EXPECT_NE(source.find("input.gizmo_scale = std::max(gizmoSizeScale, 0.1f);"), std::string::npos);
    EXPECT_NE(source.find("build_transform_gizmo_frame(sdkInput)"), std::string::npos);
    EXPECT_NE(source.find("const bool precision = io.KeyCtrl || io.KeySuper;"), std::string::npos);
    EXPECT_EQ(source.find("visualSdkInput"), std::string::npos);
    EXPECT_EQ(source.find("kGizmoFrameGridFloor"), std::string::npos);
    EXPECT_EQ(source.find("input.gizmo_size_pixels = 68.0 * std::max(gizmoSizeScale"), std::string::npos);
}

TEST(TransformGizmoTests, ScreenSizedMoveScaleFrameDoesNotUseGridAsVisualFloor) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) /
            "modeling" / "sdk" / "packages" / "editor-core" / "src" / "transform_gizmo.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const double axis_length = pixel_world * screen_size;"), std::string::npos);
    EXPECT_NE(source.find("const double plane_offset = pixel_world * (screen_size * 0.36);"), std::string::npos);
    EXPECT_NE(source.find("const double plane_half_size = pixel_world * (8.0 * visual_scale);"), std::string::npos);
    EXPECT_EQ(source.find("safe_grid"), std::string::npos);
    EXPECT_EQ(source.find("safe_grid *"), std::string::npos);
}

TEST(ViewportPanelTests, SpaceMaximizeTargetsHoveredPane) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("result.shortcut.kind == ViewportShortcutKind::ToggleMaximized"), std::string::npos);
    EXPECT_NE(source.find("focusedPaneId_ = paneState.id"), std::string::npos);
}

TEST(ViewportPanelTests, HoverUpdatesFocusedPaneWithoutClick) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("if (drawPane.focusHovered && !splitConsumed)"), std::string::npos);
    EXPECT_NE(source.find("focusedPaneId_ = drawPane.pane->id"), std::string::npos);
}

TEST(ViewportPanelTests, GizmoDragKeepsInputBoundToOwnerPane) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("bool gizmoDragPaneActive_"), std::string::npos);
    EXPECT_NE(header.find("ViewportPaneId gizmoDragPaneId_"), std::string::npos);
    EXPECT_NE(header.find("ViewportPaneId paneId"), std::string::npos);

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const bool gizmoDragLocksInput = gizmoDragging_ && gizmoDragPaneActive_"), std::string::npos);
    EXPECT_NE(source.find("if (!gizmoDragLocksInput)"), std::string::npos);
    EXPECT_NE(source.find("drawPane.pane->id == gizmoDragPaneId_"), std::string::npos);
    EXPECT_NE(source.find("inputOwnsActiveGizmoDrag"), std::string::npos);
    EXPECT_NE(source.find("gizmoDragPaneActive_ = true"), std::string::npos);
    EXPECT_NE(source.find("gizmoDragPaneId_ = paneId"), std::string::npos);
}

TEST(ViewportPanelTests, HoverWorkKeyIncludesPaneAndCameraSignature) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("ViewportPaneId paneId"), std::string::npos);
    EXPECT_NE(header.find("Vec3 cameraEye"), std::string::npos);
    EXPECT_NE(header.find("Vec3 cameraForward"), std::string::npos);
    EXPECT_NE(header.find("Vec3 cameraRight"), std::string::npos);
    EXPECT_NE(header.find("Vec3 cameraUp"), std::string::npos);
    EXPECT_NE(header.find("ViewportOverlayProjection cameraProjection"), std::string::npos);
    EXPECT_NE(header.find("paneId == other.paneId"), std::string::npos);
    EXPECT_NE(header.find("vec3Matches(cameraEye, other.cameraEye)"), std::string::npos);
    EXPECT_NE(header.find("cameraProjection == other.cameraProjection"), std::string::npos);

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const ViewportOverlayCamera overlayCamera = makeViewportOverlayCamera(camera, itemSize)"), std::string::npos);
    EXPECT_NE(source.find(".paneId = paneState.id"), std::string::npos);
    EXPECT_NE(source.find(".cameraEye = overlayCamera.eye"), std::string::npos);
    EXPECT_NE(source.find(".cameraForward = overlayCamera.forward"), std::string::npos);
}

TEST(ViewportPanelTests, RmbContextMenuUsesContextActionsAndDisabledItems) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("ViewportOperationsContext"), std::string::npos);
    EXPECT_NE(source.find("modeling.contextActions(query)"), std::string::npos);
    EXPECT_NE(source.find("ImGui::MenuItem(action.label.c_str(), nullptr, false, enabled)"), std::string::npos);
    EXPECT_NE(source.find("modeling.executeContextAction(action.actionId)"), std::string::npos);
    EXPECT_NE(source.find("action.previewKind != ModelingActionPreviewKind::None ?"), std::string::npos);
    EXPECT_EQ(source.find("action.operationId == \"bridge\""), std::string::npos);
    EXPECT_EQ(source.find("uiContext.requestToolSettingsPanel()"), std::string::npos);
    EXPECT_NE(source.find("action.disabledReason"), std::string::npos);
    EXPECT_NE(source.find("modalToolOwnsViewport()"), std::string::npos);
    EXPECT_NE(source.find(".modalToolOwnsViewport = modalToolOwnsViewport()"), std::string::npos);
    EXPECT_NE(source.find("modeling.pickSelect(ModelingPickSelectRequest"), std::string::npos);
    EXPECT_NE(source.find("pendingContextMenuRequest.targetHit = summary.hasSelection"), std::string::npos);
    EXPECT_NE(source.find(".accumulatedDragDelta = contextMenuRightDragDelta_"), std::string::npos);
    EXPECT_NE(source.find("pendingContextMenuClick"), std::string::npos);
    EXPECT_NE(source.find("selectFromViewport(document, assets, modeling, camera, pendingContextMenuLocalMouse, {}, 1, true, true)"), std::string::npos);
    EXPECT_NE(source.find(".preserveSelectionOnMiss = preserveSelectionOnMiss"), std::string::npos);
    EXPECT_NE(source.find(".preserveSelectionOnSelectedHit = preserveSelectionOnSelectedHit"), std::string::npos);
    EXPECT_NE(source.find("preserveContextMenuHover"), std::string::npos);
    EXPECT_NE(source.find("shouldPreserveViewportContextMenuHover"), std::string::npos);
    EXPECT_NE(source.find("flyCursorCaptureScreen_"), std::string::npos);
    EXPECT_NE(source.find("flyCursorCaptureScreen_.x - mainViewport->Pos.x"), std::string::npos);
    EXPECT_EQ(source.find("contextMenuRightNavigationMoved"), std::string::npos);
    EXPECT_EQ(source.find("Button(operation.label"), std::string::npos);
}

TEST(ViewportPanelTests, InsertEdgeLoopOwnsMouseReleaseAndCommitsActiveDrag) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("bool insertEdgeLoopDragActive_ = false"), std::string::npos);
    EXPECT_NE(source.find("const bool insertEdgeLoopDragRelease"), std::string::npos);
    EXPECT_NE(source.find("!insertEdgeLoopDragRelease"), std::string::npos);
    EXPECT_NE(source.find("modeling.beginInsertEdgeLoopDrag(makeModelingToolEvent(true))"), std::string::npos);
    EXPECT_NE(source.find("insertEdgeLoopDragActive_ = command.ok && modeling.toolStatus().insertEdgeLoopDragging"), std::string::npos);
    EXPECT_NE(source.find("const bool insertEdgeLoopDragging"), std::string::npos);
    EXPECT_NE(source.find("!leftReleased"), std::string::npos);
    EXPECT_NE(source.find("!insertEdgeLoopDragging"), std::string::npos);
    EXPECT_NE(source.find("leftDown &&"), std::string::npos);
    EXPECT_NE(source.find("leftReleased && insertEdgeLoopDragging"), std::string::npos);
    EXPECT_NE(source.find("return modeling.commitInsertEdgeLoopDrag()"), std::string::npos);
}

TEST(ViewportPanelTests, DrawnPlaneToolsUseClickDownPositionForFirstPoint) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const Vec2 leftClickLocal"), std::string::npos);
    EXPECT_NE(source.find("makeModelingToolEventAt(leftClickLocal)"), std::string::npos);
    EXPECT_NE(source.find("makeModelingToolEventAt(leftClickLocal, true)"), std::string::npos);
    EXPECT_NE(source.find("modeling.updateCutHover(makeModelingToolEvent(true))"), std::string::npos);
    EXPECT_NE(source.find("modeling.updateMirrorHover(makeModelingToolEvent(true))"), std::string::npos);
    EXPECT_EQ(source.find("afterClick.cutDraggingNewPoint"), std::string::npos);
    EXPECT_EQ(source.find("afterClick.mirrorDraggingNewPoint"), std::string::npos);
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const bool topologyModifier = componentSelectionMode && io.KeyShift"), std::string::npos);
    EXPECT_NE(source.find("topologyModifier && tool == ViewportTool::Move ? ModelingTransformOperation::Extrude"), std::string::npos);
    EXPECT_NE(source.find("topologyModifier && tool == ViewportTool::Scale ? ModelingTransformOperation::Inset"), std::string::npos);
    EXPECT_NE(source.find(".operation = operation"), std::string::npos);
    EXPECT_NE(source.find("modeling.beginTransformDrag({"), std::string::npos);
    EXPECT_NE(source.find("modeling.updateTransformDrag({"), std::string::npos);
    EXPECT_EQ(source.find("sdkTransformPreviewExtrude_"), std::string::npos);
    EXPECT_EQ(source.find("sdkTransformPreviewInset_"), std::string::npos);
}

TEST(ViewportPanelTests, TransformPreviewCapturesShiftDuplicateOperationAtDragBegin) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
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
            source.find("for (const GizmoStartTransform& start : gizmoStartTransforms_)", updatePreview);
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
    EXPECT_EQ(source.find("sdkTransformPreviewDuplicateSelected_"), std::string::npos);
}

TEST(ViewportPanelTests, TransformDragRuntimeStateIsOwnedByModelingService) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
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
    EXPECT_EQ(source.find("quader_editor_core::preview_rotate_from_screen_position"), std::string::npos);
    EXPECT_EQ(source.find("quader_editor_core::preview_scale_from_screen_position"), std::string::npos);
}

TEST(ViewportPanelTests, TransformGizmoAppliesRotationPreviewInWorldSpace) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("applyDocumentWorldRotationDeltaAroundPivot(\n                    document,\n                    start.entity,\n                    next,\n                    previewRotationDelta"), std::string::npos);
    EXPECT_NE(source.find("applyDocumentWorldRotationDeltaAroundPivot(\n                    document,\n                    start.entity,\n                    next,\n                    worldRotationDelta"), std::string::npos);
    EXPECT_EQ(source.find("next.rotationEuler = next.rotationEuler + previewRotationDelta"), std::string::npos);
}

TEST(ViewportPanelTests, SceneOnlySelectionsUseDocumentGizmoDrag) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("bool sceneOnlyGizmoDrag_"), std::string::npos);

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("selectedHasSceneOnlyTransform"), std::string::npos);
    EXPECT_NE(source.find("selectedHasHierarchyTransform"), std::string::npos);
    EXPECT_NE(source.find("componentSelectionMode || !selectedHasHierarchyTransform"), std::string::npos);
    EXPECT_NE(source.find("activeGizmoAxis_ = fallbackGizmoHit()"), std::string::npos);
    EXPECT_NE(source.find("parentLocalVectorForWorldVector(document, start.entity"), std::string::npos);
    EXPECT_NE(source.find("documentLocalPivotForWorldPoint(document, entity, gizmoPivot)"), std::string::npos);
    EXPECT_NE(source.find("sceneOnlyGizmoDrag_ = true"), std::string::npos);
    EXPECT_NE(source.find("if (wasDragging && !sceneOnlyGizmoDrag_)"), std::string::npos);
    EXPECT_NE(source.find("if (!sceneOnlyGizmoDrag_)"), std::string::npos);
    EXPECT_NE(source.find("transformPoint(document.worldTransformMatrix(selected), Vec3{})"), std::string::npos);
}

TEST(ViewportPanelTests, RemovesViewportPaddingAndUsesUnityBoxPreviewOutline) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("ImGuiStyleVar_WindowPadding"), std::string::npos);
    EXPECT_NE(source.find("ImGuiStyleVar_WindowBorderSize"), std::string::npos);
    EXPECT_NE(source.find("ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse"), std::string::npos);
    EXPECT_NE(source.find("IM_COL32(255, 235, 41, 209)"), std::string::npos);
    EXPECT_EQ(source.find("preview.footprint[0], preview.footprint[2]"), std::string::npos);
    EXPECT_EQ(source.find("preview.footprint[1], preview.footprint[3]"), std::string::npos);
}

TEST(ViewportPanelTests, SuppressesHoverPickingDuringCameraNavigation) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("const bool suppressHoverWork = result.navigationActive || result.cameraChanged"), std::string::npos);
    EXPECT_NE(source.find("drawTransformGizmo(document, assets, undoRedo, documentCommands, modeling, camera, paneState.id, itemMin, itemSize, suppressHoverWork)"), std::string::npos);
    EXPECT_NE(source.find("drawTransformGizmo(\n            document,\n            assets,\n            undoRedo,\n            documentCommands,\n            modeling,\n            paneState.camera"), std::string::npos);
    EXPECT_NE(source.find("if (interactive) {\n        modeling.syncObjectSelectionFromScene();\n    }"), std::string::npos);
    EXPECT_NE(source.find("mouseInsideViewport && !suppressHoverWork"), std::string::npos);
    EXPECT_NE(source.find("lastHoverWorkKey_"), std::string::npos);
    EXPECT_NE(source.find("claimHoverWork(hoverWorkKey)"), std::string::npos);
    EXPECT_NE(source.find("result.gridPresetChanged"), std::string::npos);
    EXPECT_NE(source.find("const ModelingCommandResult hoverUpdate = modeling.updateSelectionHover"), std::string::npos);
    EXPECT_NE(source.find("if (hoverUpdate.changed)"), std::string::npos);
    EXPECT_NE(source.find("const ModelingCommandResult clearHover = modeling.clearSelectionHover()"), std::string::npos);
    EXPECT_NE(source.find("lastHoverWorkKey_ = makeHoverWorkKey(gizmoConsumed)"), std::string::npos);
    EXPECT_NE(source.find("!suppressHoverWork &&\n                !gizmoConsumed"), std::string::npos);
    EXPECT_NE(source.find("selectionHoverClearIssued_"), std::string::npos);
    EXPECT_NE(source.find("cachedSelectionOverlayFrameDirty_"), std::string::npos);
    EXPECT_EQ(source.find("|| suppressHoverWork) &&"), std::string::npos);
}

TEST(SceneRenderBridgeTests, UsesExplicitTextureColorSpaceForMaterialUploads) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("createSolidTexture(255, 255, 255, 255, TextureColorSpace::Srgb)"), std::string::npos);
    EXPECT_NE(source.find("createSolidTexture(255, 255, 255, 255, TextureColorSpace::Linear)"), std::string::npos);
    EXPECT_NE(source.find("texture.colorSpace == TextureColorSpace::Srgb ? filament::Texture::InternalFormat::SRGB8_A8 : filament::Texture::InternalFormat::RGBA8"), std::string::npos);
    EXPECT_NE(source.find("MinFilter::LINEAR_MIPMAP_LINEAR"), std::string::npos);
    EXPECT_NE(source.find("mipLevelCount(width, height)"), std::string::npos);
    EXPECT_NE(source.find("Texture::Usage::GEN_MIPMAPPABLE"), std::string::npos);
    EXPECT_NE(source.find("generateMipmaps(*engine_)"), std::string::npos);
    EXPECT_NE(source.find("setCullingMode(filament::MaterialInstance::CullingMode::BACK)"), std::string::npos);
    EXPECT_NE(source.find("TextureCacheKey cacheKey"), std::string::npos);
    EXPECT_NE(source.find(".colorSpace = texture->colorSpace"), std::string::npos);
    EXPECT_EQ(source.find("bool srgb"), std::string::npos);
}

TEST(SceneRenderBridgeTests, SkipsOverlayProxyRebuildWhenOverlayPayloadIsUnchanged) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("overlayFrameSignature"), std::string::npos);
    EXPECT_NE(source.find("overlayLayerSignature"), std::string::npos);
    EXPECT_NE(source.find("overlaySlots_"), std::string::npos);
    EXPECT_NE(source.find("slot.proxyGroups"), std::string::npos);
    EXPECT_NE(source.find("frame.lines.size()"), std::string::npos);
    EXPECT_NE(source.find("frame.points.size()"), std::string::npos);
    EXPECT_NE(source.find("hashFloat(hash, line.widthPixels)"), std::string::npos);
    EXPECT_NE(source.find("hashFloat(hash, point.sizePixels)"), std::string::npos);
    EXPECT_NE(source.find("slot.frameSignature"), std::string::npos);
    EXPECT_NE(source.find("overlayCameraSignature"), std::string::npos);
    EXPECT_NE(source.find("const bool cameraChanged = cameraSignature != slot.cameraSignature"), std::string::npos);
    EXPECT_NE(source.find("const bool layerChanged = layerMask != slot.layerMask"), std::string::npos);
    EXPECT_NE(source.find("if (signature == slot.frameSignature && !cameraChanged && !layerChanged)"), std::string::npos);
    EXPECT_NE(source.find("slot.frame = std::move(frame);\n        return;"), std::string::npos);
    EXPECT_NE(source.find("group.signature = 0;"), std::string::npos);
    EXPECT_NE(source.find("rebuildOverlayProxies(slot);"), std::string::npos);
    EXPECT_NE(source.find("if (group.signature == layerSignature)"), std::string::npos);
    EXPECT_NE(source.find("viewportOverlayDrawOrder(layer) * 64"), std::string::npos);
}

TEST(ModelingSessionServiceTests, CachesSelectionOverlayFrameByRevisionAndHoverState) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "modeling" / "ModelingSessionService.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("selectionOverlayCacheValid_"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCacheContentRevision_ == contentRevision"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCacheSelectionRevision_ == selectionRevision"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCacheSceneRevision_ == sceneRevision"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCacheToolSignature_ == toolSignature"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayToolSignature()"), std::string::npos);
    EXPECT_NE(source.find("runtime_.cut_state()"), std::string::npos);
    EXPECT_NE(source.find("runtime_.mirror_state()"), std::string::npos);
    EXPECT_NE(source.find("hoverMatches(selectionOverlayCacheHover_, hover)"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCacheFrame_ = fromSdk(selectionOverlayCurrentFrame(hover)"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayBaseFrame()"), std::string::npos);
    EXPECT_NE(source.find("overlayBaseCache_"), std::string::npos);
    EXPECT_NE(source.find("build_selection_hover_overlay_frame"), std::string::npos);
    EXPECT_NE(source.find("appendSelectedHoverFilteredBaseFrame"), std::string::npos);
    EXPECT_NE(source.find("cachedPickObjectBounds()"), std::string::npos);
    EXPECT_NE(source.find("cachedPickFaceGeometry(pickOptions)"), std::string::npos);
    EXPECT_NE(source.find("objectPickFaceGeometrySignature(objects[index], pickOptions)"), std::string::npos);
}

TEST(ViewportPanelTests, SelectionOverlayCacheKeyIncludesSelectionMode) {
    const std::filesystem::path headerPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("ModelingSelectionMode cachedSelectionOverlayMode_"), std::string::npos);

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("cachedSelectionOverlayMode_ != overlaySummary.mode"), std::string::npos);
    EXPECT_NE(source.find("cachedSelectionOverlayMode_ = overlaySummary.mode"), std::string::npos);
    EXPECT_NE(source.find("toolOverlaySignature(modeling)"), std::string::npos);
    EXPECT_NE(source.find("cachedSelectionOverlayToolSignature_"), std::string::npos);
}

TEST(SceneRenderBridgeTests, ConvertsSceneOverlayColorsToLinearBeforeViewportEncode) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "ViewportRenderer.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find(".format(Texture::InternalFormat::RGBA8)"), std::string::npos);
    EXPECT_NE(source.find("TextureSampleColorSpace::LinearSrgb"), std::string::npos);
    EXPECT_NE(source.find("settings.color(ViewportColorSettingId::GridMinor)"), std::string::npos);
    EXPECT_NE(source.find("settings.floatValue(ViewportFloatSettingId::GridMinorLineSize)"), std::string::npos);
    EXPECT_EQ(source.find(".format(Texture::InternalFormat::SRGB8_A8)"), std::string::npos);
}

TEST(ViewportRendererTests, MultiViewportRenderDoesNotMutateSceneInsideFilamentFrame) {
    const std::filesystem::path rendererPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "ViewportRenderer.cpp";
    std::ifstream rendererFile(rendererPath);
    ASSERT_TRUE(rendererFile.is_open()) << rendererPath.string();
    const std::string rendererSource{
            std::istreambuf_iterator<char>(rendererFile),
            std::istreambuf_iterator<char>()};

    const std::filesystem::path appPath = std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "app" / "App.cpp";
    std::ifstream appFile(appPath);
    ASSERT_TRUE(appFile.is_open()) << appPath.string();
    const std::string appSource{std::istreambuf_iterator<char>(appFile), std::istreambuf_iterator<char>()};

    const std::filesystem::path viewportPanelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream viewportPanelFile(viewportPanelPath);
    ASSERT_TRUE(viewportPanelFile.is_open()) << viewportPanelPath.string();
    const std::string viewportPanelSource{
            std::istreambuf_iterator<char>(viewportPanelFile),
            std::istreambuf_iterator<char>()};

    const std::filesystem::path bridgePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
    std::ifstream bridgeFile(bridgePath);
    ASSERT_TRUE(bridgeFile.is_open()) << bridgePath.string();
    const std::string bridgeSource{std::istreambuf_iterator<char>(bridgeFile), std::istreambuf_iterator<char>()};

    const std::size_t renderStart = rendererSource.find("void ViewportRenderer::render");
    const std::size_t renderEnd = rendererSource.find("void ViewportRenderer::createGroundGrid");
    ASSERT_NE(renderStart, std::string::npos);
    ASSERT_NE(renderEnd, std::string::npos);
    const std::string renderFunction = rendererSource.substr(renderStart, renderEnd - renderStart);
    EXPECT_EQ(renderFunction.find("scene_->addEntity"), std::string::npos);
    EXPECT_EQ(renderFunction.find("scene_->remove"), std::string::npos);
    EXPECT_NE(rendererSource.find(".layerMask(kViewportAllLayerMask, gridLayerMask_)"), std::string::npos);
    EXPECT_NE(rendererSource.find("view_->setVisibleLayers(kViewportAllLayerMask, visibleLayers)"), std::string::npos);

    const std::size_t prepareViewports = appSource.find("ui_.prepareViewports(renderBridge_)");
    const std::size_t beginFrame = appSource.find("renderSystem_.beginFrame()");
    ASSERT_NE(prepareViewports, std::string::npos);
    ASSERT_NE(beginFrame, std::string::npos);
    EXPECT_LT(prepareViewports, beginFrame);

    EXPECT_NE(viewportPanelSource.find("void ViewportPanel::prepareViewports(SceneRenderBridge& renderBridge)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("paneState.renderer.setOverlayVisible(true)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("renderBridge.setOverlayFrame(\n                viewportPaneIndex(paneId)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("viewportGridLayerMask(viewportPaneIndex(paneId))"), std::string::npos);
    EXPECT_NE(bridgeSource.find(".layerMask(kViewportAllLayerMask, slot.layerMask)"), std::string::npos);
    EXPECT_NE(bridgeSource.find("void SceneRenderBridge::rebuildOverlayProxies(OverlaySlot& slot)"), std::string::npos);
}

TEST(ImGuiFilamentRendererTests, EncodesLinearViewportTexturesOnlyWhenRequested) {
    const std::filesystem::path rendererPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "ImGuiFilamentRenderer.cpp";
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "TextureRegistry.h";
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "TextureRegistry.cpp";
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UIStyleTokens.h";
    std::ifstream headerFile(headerPath);
    ASSERT_TRUE(headerFile.is_open()) << headerPath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};

    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UIStyleTokens.cpp";
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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui";

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
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "InspectorPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("kMinAxisInputWidth"), std::string::npos);
    EXPECT_EQ(source.find("availableWidth < minimumInlineWidth"), std::string::npos);
    EXPECT_NE(source.find("std::max((availableWidth - kTransformLabelColumnWidth) / 3.0f"), std::string::npos);
    EXPECT_NE(source.find("setNextWindowMinWidth(context.style.sidebarMinWidth)"), std::string::npos);
    EXPECT_NE(source.find("ImGuiTableFlags_SizingFixedFit"), std::string::npos);
    EXPECT_NE(source.find("ImGuiTableFlags_NoSavedSettings"), std::string::npos);
    EXPECT_NE(source.find("applyWorldRotationDeltaAroundPivot(next, delta"), std::string::npos);
    EXPECT_EQ(source.find("next.rotationEuler = next.rotationEuler + delta"), std::string::npos);
}

TEST(OutlinerPanelTests, UsesTreeIconsContextMenuAndGroupingShortcuts) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("entityRecordsTreeOrder()"), std::string::npos);
    EXPECT_NE(source.find("ImGui::TreeNodeEx"), std::string::npos);
    EXPECT_NE(source.find("drawList->AddImage"), std::string::npos);
    EXPECT_NE(source.find("ImGui::OpenPopupOnItemClick(\"EntityContext\""), std::string::npos);
    EXPECT_NE(source.find("ImGui::BeginPopup(\"EntityContext\")"), std::string::npos);
    EXPECT_NE(source.find("MenuItem(\"Delete\")"), std::string::npos);
    EXPECT_NE(source.find("MenuItem(\"Duplicate\")"), std::string::npos);
    EXPECT_NE(source.find("MenuItem(\"Group\")"), std::string::npos);
    EXPECT_NE(source.find("MenuItem(\"Ungroup\""), std::string::npos);
    EXPECT_NE(source.find("ImGuiKey_G"), std::string::npos);
    EXPECT_NE(source.find("createEmpty"), std::string::npos);
}

TEST(OutlinerPanelTests, UsesSingleSelectedTreeRowFill) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("ImGuiTreeNodeFlags_Selected"), std::string::npos);
    EXPECT_NE(source.find("drawList->AddText(textPos"), std::string::npos);
    EXPECT_EQ(source.find("ImGui::Selectable(record.name.c_str(), record.selected"), std::string::npos);
    EXPECT_EQ(source.find("ImGuiSelectableFlags_AllowOverlap"), std::string::npos);
}

TEST(OutlinerPanelTests, KeepsRootRowsTightlyInset) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "OutlinerPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("constexpr float kOutlinerRowLeftPadding = 3.0f"), std::string::npos);
    EXPECT_NE(source.find("ImGuiStyleVar_IndentSpacing"), std::string::npos);
    EXPECT_NE(source.find("ImGuiStyleVar_FramePadding"), std::string::npos);
    EXPECT_NE(source.find("record.hasChildren ? ImGui::GetTreeNodeToLabelSpacing() : kOutlinerRowLeftPadding"), std::string::npos);
    EXPECT_NE(source.find("drawList->AddImage(icon"), std::string::npos);
    EXPECT_EQ(source.find("ImGui::SetCursorScreenPos"), std::string::npos);
}

TEST(UIStyleTokensTests, KeepsDockWindowMenuButtonAvailable) {
    const std::filesystem::path stylePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UIStyleTokens.cpp";
    std::ifstream styleFile(stylePath);
    ASSERT_TRUE(styleFile.is_open()) << stylePath.string();

    const std::string styleSource{
            std::istreambuf_iterator<char>(styleFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(styleSource.find("style.WindowMenuButtonPosition = ImGuiDir_Left"), std::string::npos);
    EXPECT_EQ(styleSource.find("style.WindowMenuButtonPosition = ImGuiDir_None"), std::string::npos);

    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UISystem.cpp";
    std::ifstream uiSystemFile(uiSystemPath);
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();

    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(uiSystemSource.find("ImGuiDockNodeFlags_NoWindowMenuButton"), std::string::npos);
}

TEST(UISystemTests, SidebarsUseMinimumWidthAndFlyNavigationCapturesCursor) {
    const std::filesystem::path uiPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui";

    std::ifstream styleFile(uiPath / "UIStyleTokens.h");
    ASSERT_TRUE(styleFile.is_open());
    const std::string styleSource{
            std::istreambuf_iterator<char>(styleFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(styleSource.find("sidebarMinWidth = 270.0f"), std::string::npos);

    std::ifstream outlinerFile(uiPath / "OutlinerPanel.cpp");
    ASSERT_TRUE(outlinerFile.is_open());
    const std::string outlinerSource{
            std::istreambuf_iterator<char>(outlinerFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(outlinerSource.find("setNextWindowMinWidth(context.style.sidebarMinWidth)"), std::string::npos);

    std::ifstream uiSystemFile(uiPath / "UISystem.cpp");
    ASSERT_TRUE(uiSystemFile.is_open());
    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(uiSystemSource.find("ImGui::Begin(\"Settings\", &settingsPanelOpen_, ImGuiWindowFlags_NoDocking)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGui::Begin(\"Tool Settings\", &toolSettingsPanelOpen_, ImGuiWindowFlags_NoDocking)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGui::SetNextWindowViewport(viewport->ID)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGui::SetNextWindowPos("), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_SetWindowRelativeMouseMode(window_, captured)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_GetRelativeMouseState(&deltaX, &deltaY)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_GetRelativeMouseState(&discardX, &discardY)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("SDL_WarpMouseInWindow(window_, point.x, point.y)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("enforceSidebarDockMinimums()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("FindWindowByName(windowName)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("clampDockLeafNodeWidth"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGuiStyleVar_WindowMinSize"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("sidebarMinimumRatio"), std::string::npos);

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
    EXPECT_NE(inspectorSource.find("context.requestPanelFocus(id())"), std::string::npos);

    std::ifstream consoleFile(uiPath / "ConsolePanel.cpp");
    ASSERT_TRUE(consoleFile.is_open());
    const std::string consoleSource{
            std::istreambuf_iterator<char>(consoleFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(consoleSource.find("context.requestPanelFocus(id())"), std::string::npos);

    std::ifstream viewportPanelFile(uiPath / "ViewportPanel.cpp");
    ASSERT_TRUE(viewportPanelFile.is_open());
    const std::string viewportPanelSource{
            std::istreambuf_iterator<char>(viewportPanelFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(viewportPanelSource.find("Middle Mouse to reset"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("result.flyHudActive && !camera.flySpeedIsDefault()"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("uiContext.requestPanelFocus(UIPanelId::Viewport)"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("globalEscCancelRequested"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("mouseClickedOutsideViewport"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("!io.WantCaptureMouse"), std::string::npos);
    EXPECT_EQ(viewportPanelSource.find("flySpeedIndicatorSeconds_"), std::string::npos);
}

TEST(UISystemTests, TopMenuExposesViewportLayoutOptions) {
    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UISystem.cpp";
    std::ifstream uiSystemFile(uiSystemPath);
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();

    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(uiSystemSource.find("ImGui::BeginMenu(\"Layout\")"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportLayoutOptions()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportPanel_->setViewportLayout(option.id)"), std::string::npos);
}

TEST(UISystemTests, TopMenuExposesLiveViewportSettingsPanel) {
    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UISystem.cpp";
    std::ifstream uiSystemFile(uiSystemPath);
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();

    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(uiSystemSource.find("ImGui::BeginMenu(\"Edit\")"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGui::MenuItem(\"Settings\")"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGui::MenuItem(\"Tool Settings\")"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("settingsPanelOpen_"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolSettingsPanelOpen_"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("drawSettingsPanel"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("drawToolSettingsPanel"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modeling.toolSettingsTarget()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modeling.toolPreviewMeshes()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("renderBridge.setToolPreviewMeshes(std::move(renderPreviewMeshes), assets)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("switch (target.kind)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modeling.setInteractionSettings(settings)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolSettingsPanelOpen_ = false;"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolSettingsPanelOpen_ = result.ok ? false : modeling.toolSettingsTarget().active"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGui::RadioButton(\"Discard Left\""), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGui::RadioButton(\"Discard Right\""), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGui::RadioButton(\"Keep Both\""), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modeling.commitActiveOperationPreview()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("modeling.cancelActiveOperationPreview()"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("No active tool settings."), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("ImGui::Combo(\"Keep Mode\""), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("ImGui::CollapsingHeader(\"Bridge\")"), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("ImGui::CollapsingHeader(\"Edge Bevel\""), std::string::npos);
    EXPECT_EQ(uiSystemSource.find("ImGui::CollapsingHeader(\"Merge by Distance\""), std::string::npos);
    EXPECT_NE(uiSystemSource.find("viewportSettingsSections()"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGuiColorEditFlags_AlphaBar"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("ImGuiColorEditFlags_NoInputs"), std::string::npos);

    const std::filesystem::path uiContextPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UIContext.h";
    std::ifstream uiContextFile(uiContextPath);
    ASSERT_TRUE(uiContextFile.is_open()) << uiContextPath.string();

    const std::string uiContextSource{
            std::istreambuf_iterator<char>(uiContextFile),
            std::istreambuf_iterator<char>()};
    EXPECT_EQ(uiContextSource.find("toolSettingsPanelOpenRequested"), std::string::npos);
    EXPECT_EQ(uiContextSource.find("requestToolSettingsPanel"), std::string::npos);
}

TEST(UISystemTests, TopMenuExposesViewportToolsMenu) {
    const std::filesystem::path uiSystemPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UISystem.cpp";
    std::ifstream uiSystemFile(uiSystemPath);
    ASSERT_TRUE(uiSystemFile.is_open()) << uiSystemPath.string();

    const std::string uiSystemSource{
            std::istreambuf_iterator<char>(uiSystemFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(uiSystemSource.find("ImGui::BeginMenu(\"Tools\")"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("activateToolFromMenu(tool, modeling)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolMenuItem(\"Insert Edge Loop\", \"Shift+R\", ViewportTool::InsertEdgeLoop)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolMenuItem(\"Knife\", \"C\", ViewportTool::Knife)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolMenuItem(\"Cut\", \"Shift+C\", ViewportTool::Cut)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolMenuItem(\"Poly\", \"P\", ViewportTool::Poly)"), std::string::npos);
    EXPECT_NE(uiSystemSource.find("toolMenuItem(\"Mirror\", nullptr, ViewportTool::Mirror)"), std::string::npos);

    const std::filesystem::path viewportPanelPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream viewportPanelFile(viewportPanelPath);
    ASSERT_TRUE(viewportPanelFile.is_open()) << viewportPanelPath.string();

    const std::string viewportPanelSource{
            std::istreambuf_iterator<char>(viewportPanelFile),
            std::istreambuf_iterator<char>()};
    EXPECT_NE(viewportPanelSource.find("ViewportPanel::activateToolFromMenu"), std::string::npos);
    EXPECT_NE(viewportPanelSource.find("modeling.activateViewportTool(toModelingViewportTool(tool))"), std::string::npos);
}

TEST(AppStartupTests, DoesNotCreateDefaultSdkMesh) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "app" / "App.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(source.find("createBoxFromBounds({"), std::string::npos);
    EXPECT_EQ(source.find("Startup scene created SDK box"), std::string::npos);
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

    const Vec3 worldPoint = transformPoint(scene.worldTransformMatrix(grandchild), {});
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

    const Vec3 before = transformPoint(scene.worldTransformMatrix(child), {});
    ASSERT_TRUE(scene.setParent(child, InvalidEntityId, true));
    const Vec3 after = transformPoint(scene.worldTransformMatrix(child), {});
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

    const Vec3 center = boundsCenter(*bounds);
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
    const Vec3 center = boundsCenter(*bounds);
    EXPECT_NEAR(center.x, 0.0f, 0.001f);
    EXPECT_NEAR(center.y, 1.0f, 0.001f);
    EXPECT_NEAR(center.z, 0.0f, 0.001f);
}

TEST(TransformPivotTests, KeepsNonOriginVisualPivotFixedDuringRotation) {
    TransformComponent transform;
    const Vec3 worldPivot{0.0f, 1.0f, 0.0f};
    const Vec3 localPivot = localPivotForWorldPoint(transform, worldPivot);

    TransformComponent rotated = transform;
    rotated.rotationEuler.z = 1.57079632679f;
    pinLocalPivotToWorldPoint(rotated, localPivot, worldPivot);

    const Vec3 pinnedWorldPivot = transformPoint(composeTransform(rotated.spatialTransform()), localPivot);
    EXPECT_NEAR(pinnedWorldPivot.x, worldPivot.x, 0.001f);
    EXPECT_NEAR(pinnedWorldPivot.y, worldPivot.y, 0.001f);
    EXPECT_NEAR(pinnedWorldPivot.z, worldPivot.z, 0.001f);
    EXPECT_NEAR(rotated.position.x, 1.0f, 0.001f);
    EXPECT_NEAR(rotated.position.y, 1.0f, 0.001f);
}

TEST(TransformPivotTests, KeepsNonOriginVisualPivotFixedDuringScale) {
    TransformComponent transform;
    const Vec3 worldPivot{0.0f, 1.0f, 0.0f};
    const Vec3 localPivot = localPivotForWorldPoint(transform, worldPivot);

    TransformComponent scaled = transform;
    scaled.scale = {2.0f, 3.0f, 4.0f};
    pinLocalPivotToWorldPoint(scaled, localPivot, worldPivot);

    const Vec3 pinnedWorldPivot = transformPoint(composeTransform(scaled.spatialTransform()), localPivot);
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
    const Mat4 expectedWorldRotation = fromGlm(delta * current);
    const Mat4 actualWorldRotation = composeTransform(rotated.spatialTransform());

    const std::array<Vec3, 3> basis = {{
            {1.0f, 0.0f, 0.0f},
            {0.0f, 1.0f, 0.0f},
            {0.0f, 0.0f, 1.0f},
    }};
    for (const Vec3 axis : basis) {
        const Vec3 expected = transformVector(expectedWorldRotation, axis);
        const Vec3 actual = transformVector(actualWorldRotation, axis);
        EXPECT_NEAR(actual.x, expected.x, 0.001f);
        EXPECT_NEAR(actual.y, expected.y, 0.001f);
        EXPECT_NEAR(actual.z, expected.z, 0.001f);
    }

    TransformComponent locallyAdded = transform;
    locallyAdded.rotationEuler = locallyAdded.rotationEuler + Vec3{HalfPi, 0.0f, 0.0f};
    const Vec3 worldSpaceY = transformVector(actualWorldRotation, {0.0f, 1.0f, 0.0f});
    const Vec3 localSpaceY = transformVector(composeTransform(locallyAdded.spatialTransform()), {0.0f, 1.0f, 0.0f});
    EXPECT_GT(length(worldSpaceY - localSpaceY), 0.1f);
}

TEST(MathTypesTests, ComposeTransformRoundTripsPoints) {
    const Transform transform{
            .position = {2.0f, 3.0f, 4.0f},
            .rotation = {},
            .scale = {2.0f, 3.0f, 4.0f},
    };

    const Mat4 localToWorld = composeTransform(transform);
    const Mat4 worldToLocal = inverse(localToWorld);

    const Vec3 worldPoint = transformPoint(localToWorld, {1.0f, 1.0f, 1.0f});
    EXPECT_NEAR(worldPoint.x, 4.0f, 0.001f);
    EXPECT_NEAR(worldPoint.y, 6.0f, 0.001f);
    EXPECT_NEAR(worldPoint.z, 8.0f, 0.001f);

    const Vec3 localPoint = transformPoint(worldToLocal, worldPoint);
    EXPECT_NEAR(localPoint.x, 1.0f, 0.001f);
    EXPECT_NEAR(localPoint.y, 1.0f, 0.001f);
    EXPECT_NEAR(localPoint.z, 1.0f, 0.001f);
}

TEST(UVSphereTests, GeneratesExpectedTopology) {
    const MeshAsset sphere = generateUVSphere({.segments = 8, .rings = 4, .radius = 2.0f});

    EXPECT_EQ(sphere.vertices.size(), 45u);
    EXPECT_EQ(sphere.indices.size(), 192u);
    EXPECT_NEAR(length(sphere.vertices[0].position), 2.0f, 0.001f);
    EXPECT_TRUE(sphere.hasStableCpuTopology);
    ASSERT_EQ(sphere.primitives.size(), 1u);
    EXPECT_EQ(sphere.primitives.front().indexOffset, 0u);
    EXPECT_EQ(sphere.primitives.front().indexCount, sphere.indices.size());
    EXPECT_EQ(sphere.primitives.front().materialSlot, 0u);
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

TEST(ViewportLayoutTests, OptionLabelsMatchUnityViewportLayoutModel) {
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

TEST(ViewportLayoutTests, BuildPlacementsMatchUnityNormalizedRects) {
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

TEST(ViewportLayoutTests, BuildSplitHandlesMatchUnityAxesAndSpans) {
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

TEST(ViewportLayoutTests, SplitStateDefaultsAndClampsLikeUnity) {
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

TEST(ViewportCameraTests, DefaultPerspectiveMatchesUnityViewportContract) {
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
    const Vec3 forward = normalize(pose.target - pose.eye);

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
    const Vec3 forward = normalize(pose.target - pose.eye);
    const Vec3 right = normalize(cross(forward, pose.up));
    const Vec3 up = normalize(cross(right, forward));
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

    const Vec3 targetBefore = camera.state().target;
    const float distanceBefore = camera.state().distance;
    camera.setViewDirection({-1.0f, 0.0f, 0.0f});

    const ViewportCameraPose pose = camera.pose();
    const Vec3 forward = normalize(pose.target - pose.eye);
    EXPECT_NEAR(camera.state().target.x, targetBefore.x, 0.001f);
    EXPECT_NEAR(camera.state().target.y, targetBefore.y, 0.001f);
    EXPECT_NEAR(camera.state().target.z, targetBefore.z, 0.001f);
    EXPECT_FLOAT_EQ(camera.state().distance, distanceBefore);
    EXPECT_NEAR(forward.x, -1.0f, 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
    EXPECT_NEAR(forward.z, 0.0f, 0.001f);
}

TEST(ViewportCameraTests, ViewAxisUsesSdkNavigationPolicy) {
    ViewportCameraController camera;
    camera.focusBounds({10.0f, -2.0f, -4.0f}, {14.0f, 2.0f, 4.0f});

    const Vec3 targetBefore = camera.state().target;
    const float distanceBefore = camera.state().distance;
    camera.viewAxis(ViewportViewAxis::Y, 1);

    const ViewportCameraPose pose = camera.pose();
    const Vec3 forward = normalize(pose.target - pose.eye);
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

TEST(ViewportCameraTests, OrthographicAxisViewBuildsParallelPickingRays) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.viewAxis(ViewportViewAxis::Z, 1);

    ASSERT_EQ(camera.state().projection, ViewportProjection::Orthographic);
    const ViewportRay centerRay = camera.rayFromViewportPoint({400.0f, 300.0f});
    const ViewportRay cornerRay = camera.rayFromViewportPoint({800.0f, 0.0f});
    const ViewportCameraPose pose = camera.pose();
    const Vec3 forward = normalize(pose.target - pose.eye);

    EXPECT_NEAR(dot(centerRay.direction, forward), 1.0f, 0.001f);
    EXPECT_NEAR(dot(cornerRay.direction, centerRay.direction), 1.0f, 0.001f);
    EXPECT_NE(centerRay.origin.x, cornerRay.origin.x);
    EXPECT_NE(centerRay.origin.y, cornerRay.origin.y);
}

TEST(ViewportCameraTests, FocusOriginResetsOrthographicSizeLikeUnity) {
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
    const Vec3 forward = normalize(pose.target - pose.eye);
    EXPECT_LT(forward.y, -0.99f);
    EXPECT_GT(length(pose.up), 0.99f);
}

TEST(ViewportCameraTests, OrbitSnapUsesSdkOrthogonalSnap) {
#if QUADER_HAS_MODELING_SDK
    ViewportCameraController camera;
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::OrbitSnap);
    camera.updateDrag({100.0f, 100.0f});
    camera.endDrag();

    const ViewportCameraPose pose = camera.pose();
    const Vec3 forward = normalize(pose.target - pose.eye);
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
#else
    GTEST_SKIP() << "SDK navigation is disabled.";
#endif
}

TEST(ViewportCameraTests, FlyLookKeepsEyeFixedAndFlyMoveAdvancesCamera) {
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);

    const ViewportCameraPose startPose = camera.pose();
    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Fly);
    camera.updateDrag({160.0f, 80.0f});
    const ViewportCameraPose lookPose = camera.pose();
    const Vec3 startForward = normalize(startPose.target - startPose.eye);
    const Vec3 lookForward = normalize(lookPose.target - lookPose.eye);

    EXPECT_NEAR(length(lookPose.eye - startPose.eye), 0.0f, 0.001f);
    EXPECT_LT(dot(lookForward, startForward), 0.999f);

    EXPECT_NEAR(camera.flySpeed(), 15.0f, 0.001f);
    camera.adjustFlySpeed(1.0f);
    EXPECT_NEAR(camera.flySpeed(), 16.0f, 0.001f);

    camera.flyMove({0.0f, 0.0f, 1.0f}, 0.25f);
    const ViewportCameraPose movedPose = camera.pose();
    const Vec3 movedDelta = movedPose.eye - lookPose.eye;

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
    Vec3 forward = normalize(pose.target - pose.eye);
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
#if QUADER_HAS_MODELING_SDK
    ViewportCameraController camera;
    camera.setViewportSize(800.0f, 600.0f);
    camera.focusOrigin(8.0f);
    camera.setViewDirection({0.02f, 0.0f, -1.0f});

    camera.beginDrag({100.0f, 100.0f}, ViewportDragMode::Orbit);
    camera.updateDrag({100.0f, 100.0f});
    camera.endDrag();

    const ViewportCameraPose pose = camera.pose();
    const Vec3 forward = normalize(pose.target - pose.eye);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_GT(std::abs(forward.x), 0.001f);
    EXPECT_NEAR(forward.y, 0.0f, 0.001f);
#else
    GTEST_SKIP() << "SDK navigation is disabled.";
#endif
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

TEST(ViewportInputRouterTests, OperationShortcutsMatchUnityViewportContract) {
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
    const std::array<ShortcutCase, 19> shortcuts{{
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
            {"Shift+X mirror X", ViewportKey::X, shift, ViewportShortcutKind::MirrorAxis, ViewportShortcutContext::Viewport, ViewportMirrorAxis::X, true},
            {"Shift+Y mirror Y", ViewportKey::Y, shift, ViewportShortcutKind::MirrorAxis, ViewportShortcutContext::Viewport, ViewportMirrorAxis::Y, true},
            {"Shift+Z mirror Z", ViewportKey::Z, shift, ViewportShortcutKind::MirrorAxis, ViewportShortcutContext::Viewport, ViewportMirrorAxis::Z, true},
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
    input.pressedKeys = {ViewportKey::H};
    ViewportInputResult result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.consumed);

    router.setSelectionMode(ViewportSelectionMode::Edge);
    input.pressedKeys = {ViewportKey::V};
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(result.consumed);

    router.setSelectionMode(ViewportSelectionMode::Object);
    input.pressedKeys = {ViewportKey::H};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(result.shortcut.kind, ViewportShortcutKind::FlipHorizontal);
}

TEST(ViewportInputRouterTests, SelectionModeHotkeysMatchUnityViewportShortcuts) {
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

TEST(ViewportInputRouterTests, GridPresetWorldSizesMatchUnityMapUnitContract) {
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
    EXPECT_FALSE(router.state().maximized);

    router.setActiveTool(ViewportTool::Mirror);
    input.pressedKeys = {ViewportKey::Space};
    result = router.handleFrame(input, camera);
    EXPECT_FALSE(result.shortcutHandled);
    EXPECT_FALSE(router.state().maximized);
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

TEST(ViewportInputRouterTests, FocusShortcutsMatchUnityViewportContract) {
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

    camera.scroll(1.0f);
    input.pressedKeys = {ViewportKey::F};
    result = router.handleFrame(input, camera);
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
#if QUADER_HAS_MODELING_SDK
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
#else
    GTEST_SKIP() << "SDK navigation is disabled.";
#endif
}

TEST(ViewportInputRouterTests, PlainMiddleOrbitAfterAltSnapDoesNotSnap) {
#if QUADER_HAS_MODELING_SDK
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
#else
    GTEST_SKIP() << "SDK navigation is disabled.";
#endif
}

TEST(ViewportInputRouterTests, RightMouseAfterAltSnapPansOrthographicViewLikeUnity) {
#if QUADER_HAS_MODELING_SDK
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
#else
    GTEST_SKIP() << "SDK navigation is disabled.";
#endif
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

    const Vec3 targetBefore = camera.state().target;

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
#if QUADER_HAS_MODELING_SDK
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
    const Vec3 afterForward = normalize(after.target - after.eye);
    EXPECT_TRUE(result.consumed);
    EXPECT_TRUE(result.flyNavigationActive);
    EXPECT_TRUE(result.cameraChanged);
    EXPECT_EQ(camera.state().projection, ViewportProjection::Perspective);
    EXPECT_FALSE(camera.orthographicViewLocked());
    EXPECT_NEAR(length(after.eye - before.eye), 0.0f, 0.001f);
    EXPECT_GT(std::abs(afterForward.x), 0.001f);
#else
    GTEST_SKIP() << "SDK navigation is disabled.";
#endif
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
    const AssetId mesh = registry.addMesh(generateUVSphere());
    const AssetId material = registry.addMaterial(makeDefaultQuaderMaterial());
    const AssetId texture = registry.addTexture(TextureAsset{
        .name = "Curvature",
        .usage = TextureUsage::Curvature,
        .colorSpace = TextureColorSpace::Linear,
        .width = 512,
        .height = 512,
        .generated = true,
    });

    ASSERT_NE(registry.mesh(mesh), nullptr);
    ASSERT_NE(registry.material(material), nullptr);
    ASSERT_NE(registry.texture(texture), nullptr);
    EXPECT_EQ(registry.texture(texture)->usage, TextureUsage::Curvature);
    EXPECT_EQ(registry.texture(texture)->colorSpace, TextureColorSpace::Linear);
}

TEST(AssetRegistryTests, TracksTypedRevisionsAndScopedUpdates) {
    AssetRegistry registry;
    MeshAsset meshAsset = generateUVSphere();
    meshAsset.name = "Source";
    const AssetId mesh = registry.addMesh(std::move(meshAsset));
    const AssetId material = registry.addMaterial(makeDefaultQuaderMaterial());
    const AssetId texture = registry.addTexture(TextureAsset{
        .name = "Mask",
        .usage = TextureUsage::Mask,
        .width = 16,
        .height = 16,
        .generated = true,
    });

    EXPECT_EQ(registry.meshRevision(mesh).value, 1u);
    EXPECT_EQ(registry.materialRevision(material).value, 1u);
    EXPECT_EQ(registry.textureRevision(texture).value, 1u);
    EXPECT_EQ(registry.revision(), 3u);

    MeshAsset replacement = generateUVSphere();
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

TEST(ProjectDocumentTests, OwnsSceneAssetsAndStableObjectRecords) {
    ProjectDocument document(ProjectDocumentRecord{
        .id = 17,
        .name = "Authored Project",
    });

    MeshAsset meshAsset = generateUVSphere();
    meshAsset.name = "Authored Mesh";
    const AssetId mesh = document.assets().addMesh(std::move(meshAsset));
    const EntityId entity = document.scene().createEntity("Authored Mesh");
    document.scene().setMesh(entity, MeshComponent{.mesh = mesh});

    const ProjectObjectId object = document.createObjectRecord(ProjectObjectRecord{
        .sceneEntity = entity,
        .mesh = mesh,
        .authoredName = "Authored Mesh",
    });

    ASSERT_NE(object, InvalidProjectObjectId);
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
    EXPECT_EQ(document.createObjectRecord(ProjectObjectRecord{}), 43u);
    const std::vector<ProjectObjectRecord> records = document.objectRecords();
    ASSERT_EQ(records.size(), 3u);
    EXPECT_EQ(records[0].id, object);
    EXPECT_EQ(records[1].id, 42u);
    EXPECT_EQ(records[2].id, 43u);

    document.rename("Renamed Project");
    EXPECT_EQ(document.name(), "Renamed Project");
    EXPECT_EQ(document.revision(), 4u);
}

TEST(DocumentCommandServiceTests, RecordsUndoRedoForNativeSceneEdits) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    UndoRedoManager undoRedo;
    DocumentCommandService commands(scene, undoRedo);

    DocumentCommandResult result = commands.renameEntity(entity, "Brush_1");
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");
    EXPECT_TRUE(undoRedo.canUndo());
    EXPECT_FALSE(undoRedo.canRedo());
    ASSERT_NE(undoRedo.undoLabel(), nullptr);
    EXPECT_EQ(*undoRedo.undoLabel(), "Rename Entity");

    result = undoRedo.undo();
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush");
    EXPECT_FALSE(undoRedo.canUndo());
    EXPECT_TRUE(undoRedo.canRedo());

    result = undoRedo.redo();
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_NE(scene.name(entity), nullptr);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");

    TransformComponent transform;
    transform.position = {3.0f, 2.0f, 1.0f};
    ASSERT_TRUE(commands.setTransform(entity, transform).ok);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 3.0f);
    EXPECT_TRUE(undoRedo.undo().ok);
    ASSERT_NE(scene.transform(entity), nullptr);
    EXPECT_FLOAT_EQ(scene.transform(entity)->position.x, 0.0f);
}

TEST(DocumentCommandServiceTests, BatchesVisibilityAndTransforms) {
    SceneDocument scene;
    const EntityId first = scene.createEntity("First");
    const EntityId second = scene.createEntity("Second");
    const EntityId locked = scene.createEntity("Locked");
    UndoRedoManager undoRedo;
    DocumentCommandService commands(scene, undoRedo);

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

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(scene.visibility(first)->visible);
    EXPECT_TRUE(scene.visibility(second)->visible);
    ASSERT_TRUE(undoRedo.redo().ok);
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

    ASSERT_TRUE(undoRedo.undo().ok);
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

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_FALSE(scene.lock(locked)->locked);
}

TEST(UndoRedoManagerTests, NoopCommandsDoNotClearRedoButNewCommandsDo) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    UndoRedoManager undoRedo;
    DocumentCommandService commands(scene, undoRedo);

    ASSERT_TRUE(commands.renameEntity(entity, "Brush_1").ok);
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(undoRedo.canRedo());

    DocumentCommandResult result = commands.renameEntity(entity, "Brush");
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(undoRedo.canRedo());

    result = commands.setVisible(entity, true);
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(undoRedo.canRedo());

    result = commands.renameEntity(entity, "Brush_2");
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    EXPECT_FALSE(undoRedo.canRedo());
}

TEST(UndoRedoManagerTests, ExternalCommandsShareUndoOrderWithDocumentCommands) {
    SceneDocument scene;
    const EntityId entity = scene.createEntity("Brush");
    UndoRedoManager undoRedo;
    DocumentCommandService commands(scene, undoRedo);
    int externalValue = 0;

    UndoRedoResult result = undoRedo.executeExternal(
            "External Edit",
            [&]() {
                externalValue = 1;
                return UndoRedoResult{.ok = true, .changed = true};
            },
            [&]() {
                externalValue = 0;
                return UndoRedoResult{.ok = true, .changed = true};
            },
            [&]() {
                externalValue = 1;
                return UndoRedoResult{.ok = true, .changed = true};
            });
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(commands.renameEntity(entity, "Brush_1").ok);

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_EQ(scene.name(entity)->name, "Brush");
    EXPECT_EQ(externalValue, 1);

    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_EQ(externalValue, 0);

    ASSERT_TRUE(undoRedo.redo().ok);
    EXPECT_EQ(externalValue, 1);

    ASSERT_TRUE(undoRedo.redo().ok);
    EXPECT_EQ(scene.name(entity)->name, "Brush_1");
}

TEST(UndoRedoManagerTests, FailedExecuteAndFailedRestoreDoNotCorruptStacks) {
    UndoRedoManager undoRedo;
    int value = 0;

    UndoRedoResult result = undoRedo.execute(UndoRedoCommand{
            .label = "Set Value",
            .undo = [&]() {
                value = 0;
                return UndoRedoResult{.ok = true, .changed = true};
            },
            .redo = [&]() {
                value = 1;
                return UndoRedoResult{.ok = true, .changed = true};
            },
    });
    ASSERT_TRUE(result.ok) << result.message;
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(undoRedo.canRedo());

    result = undoRedo.execute(UndoRedoCommand{
            .label = "Noop",
            .undo = []() {
                return UndoRedoResult{.ok = true, .changed = true};
            },
            .redo = []() {
                return UndoRedoResult{.ok = true, .changed = false};
            },
    });
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_FALSE(result.changed);
    EXPECT_TRUE(undoRedo.canRedo());

    result = undoRedo.execute(UndoRedoCommand{
            .label = "Failed",
            .undo = []() {
                return UndoRedoResult{.ok = true, .changed = true};
            },
            .redo = []() {
                return UndoRedoResult{.ok = false, .message = "failed"};
            },
    });
    EXPECT_FALSE(result.ok);
    EXPECT_TRUE(undoRedo.canRedo());

    result = undoRedo.redo();
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_EQ(value, 1);
    EXPECT_TRUE(undoRedo.canUndo());
    ASSERT_TRUE(undoRedo.undo().ok);
    EXPECT_TRUE(undoRedo.canRedo());

    bool failUndo = true;
    bool failRedo = false;
    result = undoRedo.execute(UndoRedoCommand{
            .label = "Fragile",
            .undo = [&]() {
                if (failUndo) {
                    return UndoRedoResult{.ok = false, .message = "undo failed"};
                }
                value = 1;
                return UndoRedoResult{.ok = true, .changed = true};
            },
            .redo = [&]() {
                if (failRedo) {
                    return UndoRedoResult{.ok = false, .message = "redo failed"};
                }
                value = 2;
                return UndoRedoResult{.ok = true, .changed = true};
            },
    });
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

#if QUADER_HAS_MODELING_SDK
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
    EXPECT_EQ(invert->label, "Invert Mesh");
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
#else
    EXPECT_TRUE(service.contextActionSet().panels.empty());
    EXPECT_TRUE(service.contextActions().empty());
    EXPECT_FALSE(service.executeContextAction("invert_mesh").ok);
#endif
}

TEST(ModelingSessionServiceTests, ContextActionsExposeEdgePanelAndDisableInsufficientCounts) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_TRUE(service.contextActionSet().panels.empty());
#endif
}

TEST(ModelingSessionServiceTests, PreviewActionsExposeOnlyActiveToolSettingsAndLiveUpdate) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    const std::optional<MeshAsset> previewBefore = service.compileObjectMesh(box.object.sdkObjectId);
    ASSERT_TRUE(previewBefore.has_value());
    auto meshPositionFingerprint = [](const MeshAsset& mesh) {
        double value = 0.0;
        for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
            const Vec3& position = mesh.vertices[index].position;
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

    const std::optional<MeshAsset> previewAfter = service.compileObjectMesh(box.object.sdkObjectId);
    ASSERT_TRUE(previewAfter.has_value());
    EXPECT_NE(meshPositionFingerprint(*previewAfter), meshPositionFingerprint(*previewBefore));

    const ModelingCommandResult cancel = service.cancelActiveOperationPreview();
    EXPECT_TRUE(cancel.ok) << cancel.message;
    EXPECT_FALSE(service.toolSettingsTarget().active);
#else
    EXPECT_FALSE(service.toolSettingsTarget().active);
    EXPECT_FALSE(service.cancelActiveOperationPreview().ok);
#endif
}

TEST(ModelingSessionServiceTests, ContextActionsExposeFacePanelAndKeepBridgeDisabledForOneFace) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_TRUE(service.contextActionSet().panels.empty());
#endif
}

TEST(ModelingSessionServiceTests, RmbMeshPickInFaceModeSelectsTargetAndAllowsContextMenu) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    GTEST_SKIP() << "SDK selection is disabled.";
#endif
}

TEST(ModelingSessionServiceTests, RmbObjectPickOnSelectedMultiSelectionPreservesSelection) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.sdkAvailable());
#endif
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

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_TRUE(service.contextActionSet().panels.empty());
    EXPECT_TRUE(service.contextActions(contextQuery).empty());
#endif
}

TEST(ModelingSessionServiceTests, CreatesBoxThroughSdkWhenAvailable) {
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

#if QUADER_HAS_MODELING_SDK
    ASSERT_TRUE(result.ok) << result.message;
    EXPECT_TRUE(result.changed);
    EXPECT_GT(result.object.sdkObjectId, 0);
    EXPECT_NE(result.object.entity, InvalidEntityId);
    EXPECT_NE(result.object.mesh, InvalidAssetId);
    EXPECT_EQ(service.entityForSdkObject(result.object.sdkObjectId), result.object.entity);
    EXPECT_EQ(service.sdkObjectForEntity(result.object.entity), result.object.sdkObjectId);

    const MeshAsset* storedMesh = assets.mesh(result.object.mesh);
    ASSERT_NE(storedMesh, nullptr);
    EXPECT_EQ(storedMesh->name, "Test Box");
    EXPECT_FALSE(storedMesh->vertices.empty());
    EXPECT_FALSE(storedMesh->indices.empty());
    EXPECT_TRUE(storedMesh->hasStableCpuTopology);
    const std::optional<SceneBounds> selectedBounds = selectedWorldBounds(scene, assets);
    ASSERT_TRUE(selectedBounds.has_value());
    const Vec3 selectedCenter = boundsCenter(*selectedBounds);
    EXPECT_NEAR(selectedCenter.x, 0.0f, 0.001f);
    EXPECT_NEAR(selectedCenter.y, 0.0f, 0.001f);
    EXPECT_NEAR(selectedCenter.z, 0.0f, 0.001f);
    ASSERT_NE(scene.mesh(result.object.entity), nullptr);
    EXPECT_EQ(scene.mesh(result.object.entity)->mesh, result.object.mesh);
    ASSERT_NE(scene.modelingObject(result.object.entity), nullptr);
    EXPECT_EQ(scene.modelingObject(result.object.entity)->objectId, result.object.sdkObjectId);
#else
    EXPECT_FALSE(service.sdkAvailable());
    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.changed);
    EXPECT_FALSE(result.message.empty());
    EXPECT_EQ(assets.meshCount(), 0u);
    EXPECT_TRUE(scene.entities().empty());
#endif
}

TEST(ModelingSessionServiceTests, InsertEdgeLoopDragCommitsMesh) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Edge Loop Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    const std::optional<MeshAsset> before = service.compileObjectMesh(box.object.sdkObjectId);
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

    const std::optional<MeshAsset> after = service.compileObjectMesh(box.object.sdkObjectId);
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(after->vertices.size(), before->vertices.size());
    EXPECT_GT(after->indices.size(), before->indices.size());
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::InsertEdgeLoop).ok);
#endif
}

TEST(ModelingSessionServiceTests, InsertEdgeLoopCommitSurvivesReleaseFrameHover) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Release Hover Edge Loop Source",
    });
    ASSERT_TRUE(box.ok) << box.message;

    std::optional<MeshAsset> before = service.compileObjectMesh(box.object.sdkObjectId);
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

    std::optional<MeshAsset> after = service.compileObjectMesh(box.object.sdkObjectId);
    ASSERT_TRUE(after.has_value());
    EXPECT_GT(after->vertices.size(), before->vertices.size());
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::InsertEdgeLoop).ok);
#endif
}

TEST(ModelingSessionServiceTests, InsertEdgeLoopUsesViewportPickRadius) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::InsertEdgeLoop).ok);
#endif
}

TEST(ModelingSessionServiceTests, CutAndMirrorEmitUnityDrawnPlaneOverlay) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::Cut).ok);
#endif
}

TEST(ModelingSessionServiceTests, CutToolDragEndAndFinalizeSplitMesh) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::Cut).ok);
#endif
}

TEST(ModelingSessionServiceTests, CutToolActivationPromotesSelectedSceneMeshToObjectMode) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::Cut).ok);
#endif
}

TEST(ModelingSessionServiceTests, CutAndMirrorActivationPromotesSdkSelectionWhenSceneSelectionIsStale) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::Mirror).ok);
#endif
}

TEST(ModelingSessionServiceTests, MirrorToolDrawnPlaneFinalizeDuplicatesSelection) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::Mirror).ok);
#endif
}

TEST(ModelingSessionServiceTests, MirrorToolActivationPromotesSelectedSceneMeshToObjectMode) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.activateViewportTool(ModelingViewportTool::Mirror).ok);
#endif
}

TEST(ModelingSessionServiceTests, SyncsSceneSelectionToSdkSelectionCenter) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    scene.clearSelection();
    scene.setSelected(first.object.entity, true);
    service.syncObjectSelectionFromScene();
    std::optional<Vec3> center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_FLOAT_EQ(center->x, 0.0f);
    EXPECT_FLOAT_EQ(center->y, 0.0f);
    EXPECT_FLOAT_EQ(center->z, 0.0f);
    const std::uint64_t firstSelectionRevision = service.selectionSummary().selectionRevision;
    service.syncObjectSelectionFromScene();
    EXPECT_EQ(service.selectionSummary().selectionRevision, firstSelectionRevision);

    scene.clearSelection();
    scene.setSelected(second.object.entity, true);
    service.syncObjectSelectionFromScene();
    center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_FLOAT_EQ(center->x, 11.0f);
    EXPECT_FLOAT_EQ(center->y, 4.0f);
    EXPECT_FLOAT_EQ(center->z, -1.0f);
    const std::uint64_t secondSelectionRevision = service.selectionSummary().selectionRevision;
    service.syncObjectSelectionFromScene();
    EXPECT_EQ(service.selectionSummary().selectionRevision, secondSelectionRevision);
#else
    service.syncObjectSelectionFromScene();
    EXPECT_FALSE(service.selectionCenter().has_value());
#endif
}

TEST(ModelingSessionServiceTests, SelectionCommandsStaySynchronizedWithScene) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    scene.setSelected(first.object.entity, true);
    service.syncObjectSelectionFromScene();
    const ModelingCommandResult invertOne = service.invertSelection();
    ASSERT_TRUE(invertOne.ok) << invertOne.message;
    EXPECT_EQ(service.selectionSummary().selectedObjectCount, 1u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(scene.selectedEntities().front(), second.object.entity);
#else
    EXPECT_FALSE(service.selectAll().ok);
    EXPECT_FALSE(service.invertSelection().ok);
#endif
}

TEST(ModelingSessionServiceTests, ElementSelectionCommandsWorkAcrossDifferentMeshes) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    const std::optional<Vec3> center = service.selectionCenter();
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
#else
    EXPECT_FALSE(service.selectAll().ok);
#endif
}

TEST(ModelingSessionServiceTests, SelectionCenterAndOverlayRespectSceneTransforms) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Moved Box",
    });
    ASSERT_TRUE(box.ok) << box.message;

    TransformComponent moved;
    moved.position = {7.0f, 2.0f, -3.0f};
    scene.setTransform(box.object.entity, moved);
    scene.clearSelection();
    scene.setSelected(box.object.entity, true);
    service.syncObjectSelectionFromScene();

    const std::optional<Vec3> center = service.selectionCenter();
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
    const Vec3 overlayCenter = boundsCenter(*overlayBounds);
    EXPECT_NEAR(overlayCenter.x, 7.0f, 0.001f);
    EXPECT_NEAR(overlayCenter.y, 2.0f, 0.001f);
    EXPECT_NEAR(overlayCenter.z, -3.0f, 0.001f);
#else
    EXPECT_FALSE(service.selectionCenter().has_value());
#endif
}

TEST(ModelingSessionServiceTests, PickAndHoverRespectSceneTransformsForLocalMeshes) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
    EXPECT_EQ(picked.activeObjectId, right.object.sdkObjectId);
#else
    EXPECT_FALSE(service.sdkAvailable());
#endif
}

TEST(ModelingSessionServiceTests, VertexHoverCandidatePointsRespectSceneTransforms) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.sdkAvailable());
#endif
}

TEST(ModelingSessionServiceTests, SelectedComponentHoverKeepsHoverColor) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    EXPECT_GT(hoverFaceCount, 0);
    EXPECT_GT(hoverFaceEdgeCount, 0);
    EXPECT_GT(selectedFaceEdgeCount, 0);
#else
    EXPECT_FALSE(service.sdkAvailable());
#endif
}

TEST(ModelingSessionServiceTests, InstantOperationsRefreshSceneObjectsAndSelection) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Source Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    const AssetId defaultMaterial = assets.addMaterial(makeDefaultQuaderMaterial());
    scene.setMaterial(box.object.entity, MaterialComponent{.material = defaultMaterial});
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
    EXPECT_EQ(scene.name(duplicateEntity)->name.find("SDK Object"), std::string::npos);
    const MeshComponent* duplicateMesh = scene.mesh(duplicateEntity);
    ASSERT_NE(duplicateMesh, nullptr);
    const MeshAsset* duplicateMeshAsset = assets.mesh(duplicateMesh->mesh);
    ASSERT_NE(duplicateMeshAsset, nullptr);
    EXPECT_EQ(duplicateMeshAsset->name, "Mesh");
    const MaterialComponent* duplicateMaterial = scene.material(duplicateEntity);
    ASSERT_NE(duplicateMaterial, nullptr);
    EXPECT_EQ(duplicateMaterial->material, defaultMaterial);

    const ModelingCommandResult deleted = service.executeInstantOperation(ModelingInstantOperationId::DeleteSelection);
    ASSERT_TRUE(deleted.ok) << deleted.message;
    EXPECT_TRUE(deleted.changed);
    EXPECT_EQ(scene.entities().size(), 1u);
    EXPECT_EQ(service.objects().size(), 1u);
    EXPECT_TRUE(scene.selectedEntities().empty());
#else
    EXPECT_TRUE(service.instantOperations().empty());
    EXPECT_FALSE(service.executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes).ok);
#endif
}

TEST(ModelingSessionServiceTests, RepeatedSelectionSyncDoesNotDirtySceneOrResyncMeshes) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Selection Sync Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    scene.clearSelection();
    scene.setSelected(box.object.entity, true);
    service.syncObjectSelectionFromScene();
    service.syncSceneSelectionFromSdk();

    const std::uint64_t sceneRevision = scene.revision();
    const ModelingSelectionSummary summary = service.selectionSummary();
    service.syncSceneSelectionFromSdk();

    EXPECT_EQ(scene.revision(), sceneRevision);
    EXPECT_EQ(service.selectionSummary().contentRevision, summary.contentRevision);
    EXPECT_EQ(service.selectionSummary().selectionRevision, summary.selectionRevision);
#else
    EXPECT_FALSE(service.sdkAvailable());
#endif
}

TEST(ModelingSessionServiceTests, TransformPreviewZeroDeltaRestoresSceneMesh) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.beginTransformPreview(false).ok);
#endif
}

TEST(ModelingSessionServiceTests, ComponentTransformPreviewRefreshesSelectionOverlayEveryUpdate) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.beginTransformPreview(false).ok);
#endif
}

TEST(ModelingSessionServiceTests, ComponentTransformPreviewMovesVertexHandlesEveryUpdate) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.beginTransformPreview(false).ok);
#endif
}

TEST(ModelingSessionServiceTests, ComponentTransformPreviewMovesEdgeHandlesEveryUpdate) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.beginTransformPreview(false).ok);
#endif
}

TEST(ModelingSessionServiceTests, ComponentTransformCommitKeepsHoverHandlesOnCommittedGeometry) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.beginTransformPreview(false).ok);
#endif
}

TEST(ModelingSessionServiceTests, ComponentTransformCancelRestoresMeshAndOverlayTogether) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.beginTransformPreview(false).ok);
#endif
}

TEST(ModelingSessionServiceTests, TransformDragComponentSelectionCommitsOverlayOnCommittedGeometry) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.beginTransformDrag(testXMoveDragBeginRequest()).ok);
#endif
}

TEST(ModelingSessionServiceTests, TransformDragObjectSelectionUsesSceneTransformPreview) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.beginTransformDrag(testXMoveDragBeginRequest()).ok);
#endif
}

TEST(ModelingSessionServiceTests, TransformDragComponentCancelRestoresMeshOverlayAndRuntime) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.cancelTransformDrag().ok);
#endif
}

TEST(ModelingSessionServiceTests, TransformPreviewDuplicateTranslateRefreshesMeshAndKeepsDuplicateSelectable) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Shift Duplicate Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    ASSERT_EQ(scene.entities().size(), 1u);

    const ModelingCommandResult begin = service.beginTransformPreview(true);
    ASSERT_TRUE(begin.ok) << begin.message;
    ASSERT_TRUE(begin.changed);
    ASSERT_EQ(scene.entities().size(), 2u);
    ASSERT_EQ(service.objects().size(), 2u);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);

    const EntityId duplicateEntity = scene.selectedEntities().front();
    EXPECT_NE(duplicateEntity, box.object.entity);
    const std::optional<int> duplicateSdkObjectId = service.sdkObjectForEntity(duplicateEntity);
    ASSERT_TRUE(duplicateSdkObjectId.has_value());
    EXPECT_NE(*duplicateSdkObjectId, box.object.sdkObjectId);

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
    EXPECT_EQ(picked.activeObjectId, *duplicateSdkObjectId);
    ASSERT_EQ(scene.selectedEntities().size(), 1u);
    EXPECT_EQ(service.sdkObjectForEntity(scene.selectedEntities().front()), duplicateSdkObjectId);
#else
    EXPECT_FALSE(service.beginTransformPreview(true).ok);
#endif
}

TEST(ModelingSessionServiceTests, ShortcutCommandWrappersRefreshSceneAndHistory) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    EXPECT_FALSE(service.canUndo());
    EXPECT_FALSE(service.canRedo());

    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {.min = {-1.0f, -1.0f, -1.0f}, .max = {1.0f, 1.0f, 1.0f}},
        .name = "Shortcut Source",
    });
    ASSERT_TRUE(box.ok) << box.message;
    EXPECT_EQ(scene.entities().size(), 1u);
    EXPECT_TRUE(service.canUndo());
    EXPECT_FALSE(service.canRedo());

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

    command = service.pasteSelection();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 4u);

    command = service.undo();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 3u);
    EXPECT_TRUE(service.canRedo());

    command = service.redo();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 4u);
    EXPECT_FALSE(service.canRedo());

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
#else
    EXPECT_FALSE(service.canUndo());
    EXPECT_FALSE(service.canRedo());
    EXPECT_FALSE(service.copySelection().ok);
    EXPECT_FALSE(service.pasteSelection().ok);
    EXPECT_FALSE(service.undo().ok);
    EXPECT_FALSE(service.redo().ok);
    EXPECT_FALSE(service.repeatLastAction().ok);
    EXPECT_FALSE(service.mirrorSelection(ModelingMirrorAxis::X).ok);
    EXPECT_FALSE(service.flipSelection(ModelingFlipOrientation::Horizontal).ok);
    EXPECT_FALSE(service.applyActiveMaterial(7u).ok);
#endif
}

TEST(ModelingSessionServiceTests, RepeatLastActionReplaysRecordedTransformPreview) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    std::optional<Vec3> center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_NEAR(center->x, 2.0f, 0.001f);

    command = service.repeatLastAction();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_NEAR(center->x, 4.0f, 0.001f);
#else
    EXPECT_FALSE(service.recordRepeatableTransform({.active = true}).ok);
#endif
}

TEST(ModelingSessionServiceTests, RepeatLastActionReplaysRecordedExtrudePreview) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    const std::optional<MeshAsset> before = service.compileObjectMesh(box.object.sdkObjectId);
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

    const std::optional<MeshAsset> afterFirst = service.compileObjectMesh(box.object.sdkObjectId);
    ASSERT_TRUE(afterFirst.has_value());
    ASSERT_GT(afterFirst->indices.size(), before->indices.size());

    command = service.repeatLastAction();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);

    const std::optional<MeshAsset> afterRepeat = service.compileObjectMesh(box.object.sdkObjectId);
    ASSERT_TRUE(afterRepeat.has_value());
    EXPECT_GT(afterRepeat->indices.size(), afterFirst->indices.size());
#else
    EXPECT_FALSE(service.recordRepeatableTransform({.active = true}).ok);
#endif
}

TEST(ModelingSessionServiceTests, SceneObjectSyncDoesNotResetComponentSelectionMode) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    scene.clearSelection();
    scene.setSelected(box.object.entity, true);
    service.syncObjectSelectionFromScene();

    EXPECT_EQ(service.selectionMode(), ModelingSelectionMode::Vertex);
    EXPECT_EQ(service.selectionSummary().mode, ModelingSelectionMode::Vertex);
#else
    service.syncObjectSelectionFromScene();
    EXPECT_EQ(service.selectionMode(), ModelingSelectionMode::Object);
#endif
}

TEST(ModelingSessionServiceTests, SelectionModeSwitchPreservesSceneOnlySelection) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    const EntityId empty = scene.createEmpty("Empty");
    scene.setSelected(empty, true);
    EXPECT_FALSE(service.setSelectionMode(ModelingSelectionMode::Object).ok);
    EXPECT_EQ(scene.primarySelection(), empty);
#endif
}

TEST(ModelingSessionServiceTests, VertexSelectionModeExposesUnityOverlayPoints) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_TRUE(service.selectionOverlayFrame().points.empty());
#endif
}

TEST(ModelingSessionServiceTests, SwitchingSelectedObjectToVertexModeShowsHandlesImmediately) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Mode Switch Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    scene.clearSelection();
    scene.setSelected(box.object.entity, true);
    service.syncObjectSelectionFromScene();

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
#else
    EXPECT_TRUE(service.selectionOverlayFrame().points.empty());
#endif
}

TEST(ModelingSessionServiceTests, SwitchingSelectedObjectToEdgeModeShowsHandlesImmediately) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Edge Mode Switch Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    scene.clearSelection();
    scene.setSelected(box.object.entity, true);
    service.syncObjectSelectionFromScene();

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
#else
    EXPECT_TRUE(service.selectionOverlayFrame().lines.empty());
#endif
}

TEST(ModelingSessionServiceTests, SwitchingSelectedObjectToFaceModeShowsHandlesImmediately) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Face Mode Switch Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    scene.clearSelection();
    scene.setSelected(box.object.entity, true);
    service.syncObjectSelectionFromScene();

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
#else
    EXPECT_TRUE(service.selectionOverlayFrame().triangles.empty());
#endif
}

TEST(ModelingSessionServiceTests, ObjectSelectionOverlayUsesUnitySelectionColors) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
    const ModelingCreateBoxResult box = service.createBoxFromBounds({
        .bounds = {
            .min = {-1.0f, -1.0f, -1.0f},
            .max = {1.0f, 1.0f, 1.0f},
        },
        .name = "Object Overlay Box",
    });
    ASSERT_TRUE(box.ok) << box.message;
    scene.clearSelection();
    scene.setSelected(box.object.entity, true);
    service.syncObjectSelectionFromScene();

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
#else
    EXPECT_TRUE(service.selectionOverlayFrame().triangles.empty());
#endif
}

TEST(ModelingSessionServiceTests, FacePickKeepsSceneEntitySelectedForInspectorAndGizmo) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.sdkAvailable());
#endif
}

TEST(ModelingSessionServiceTests, SupportsSdkBoxPreviewLifecycle) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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
#else
    EXPECT_FALSE(service.sdkAvailable());
    EXPECT_FALSE(service.updateBoxHover({}).ok);
    EXPECT_FALSE(service.beginBoxFootprint({}).ok);
    EXPECT_FALSE(service.commitBox("Preview Box").ok);
#endif
}

TEST(MaterialDocumentTests, DefaultQuaderMaterialUsesSdkTextureSlots) {
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
