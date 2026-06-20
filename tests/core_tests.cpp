#include "quader/assets/AssetRegistry.h"
#include "quader/assets/BakeService.h"
#include "quader/assets/MaterialDocument.h"
#include "quader/core/Logger.h"
#include "quader/geometry/UVSphere.h"
#include "quader/modeling/ModelingSessionService.h"
#include "quader/render/ViewportOverlayPolicy.h"
#include "quader/scene/SceneBounds.h"
#include "quader/scene/SceneDocument.h"
#include "quader/ui/ConsoleModel.h"
#include "quader/viewport/ViewportCameraController.h"
#include "quader/viewport/ViewportInputRouter.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <utility>
#include <string>
#include <vector>

using namespace quader;

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

TEST(LoggerTests, RoutesMessagesToMemorySink) {
    Logger::initialize(LoggerConfig{.name = "test", .filePath = "test.log", .maxEntries = 16});
    Logger::clearMemory();

    LOG_INFO("hello {}", "quader");

    const auto entries = Logger::entriesSnapshot();
    ASSERT_FALSE(entries.empty());
    EXPECT_NE(entries.back().message.find("hello quader"), std::string::npos);
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

TEST(ViewportOverlayPolicyTests, MatchesUnityLayerDrawOrder) {
    const std::array<ViewportOverlayLayer, 15> expected{{
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

TEST(ViewportOverlayPolicyTests, MatchesUnityColorsAndSizes) {
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SelectedVisibleFace, rgba8(255, 255, 255)), kOverlaySelectionFaceColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SelectedFaceEdge, rgba8(255, 255, 255)), kOverlaySelectionWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SourceWire, kOverlaySelectedVertexColor), kOverlaySelectionWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::SourceWire, rgba8(255, 255, 255)), kOverlaySourceWireColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::HoverFace, kOverlayRemoveWireColor), kOverlayRemoveFaceColor);
    EXPECT_EQ(viewportOverlayColorFor(ViewportOverlayLayer::HoverEdge, kOverlayRemoveFaceColor), kOverlayRemoveWireColor);

    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::SourceWire, 3.0f), 1.5f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::SelectedEdge, 3.0f), 2.5f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::HoverEdge, 3.0f), 2.5f);
    EXPECT_FLOAT_EQ(viewportOverlayLineWidthPixels(ViewportOverlayLayer::OpenEdge, 2.0f), 1.0f);
    EXPECT_FLOAT_EQ(viewportOverlayPointSizePixels(ViewportOverlayLayer::UnselectedVertex, 8.0f), 7.0f);
    EXPECT_FLOAT_EQ(viewportOverlayPointSizePixels(ViewportOverlayLayer::SelectedVertex, 8.0f), 7.5f);
    EXPECT_TRUE(viewportOverlayPointHasOutline(ViewportOverlayLayer::SelectedVertex));
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
    EXPECT_NE(source.find("DepthFunc::LE"), std::string::npos);
    EXPECT_NE(source.find("DepthFunc::E"), std::string::npos);
    EXPECT_NE(source.find("CullingMode::BACK"), std::string::npos);
    EXPECT_NE(source.find("TransparencyMode::TWO_PASSES_ONE_SIDE"), std::string::npos);
}

TEST(SceneRenderBridgeTests, DoesNotUseRawLineOrPointFallbackForSelectionOverlay) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_EQ(source.find("appendOverlayLineQuad"), std::string::npos);
    EXPECT_EQ(source.find("appendOverlayPoint"), std::string::npos);
    EXPECT_EQ(source.find("PrimitiveType::LINES"), std::string::npos);
    EXPECT_EQ(source.find("PrimitiveType::POINTS"), std::string::npos);
}

TEST(ViewportPanelTests, ClipsOnlyPolicyDepthTestedWireOverlayInComponentModes) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("renderBridge.setOverlayFrame(selectionOverlay);"), std::string::npos);
    EXPECT_NE(source.find("drawSelectionOverlay2d"), std::string::npos);
    EXPECT_NE(source.find("kViewportOverlayDrawOrder"), std::string::npos);
    EXPECT_NE(source.find("viewportOverlayLineWidthPixels"), std::string::npos);
    EXPECT_NE(source.find("viewportOverlayPointSizePixels"), std::string::npos);
    EXPECT_NE(source.find("viewportOverlayDepthTested(layer, ViewportOverlayGeometryKind::Lines)"), std::string::npos);
    EXPECT_NE(source.find("viewportOverlayDepthTested(layer, ViewportOverlayGeometryKind::Points)"), std::string::npos);
    EXPECT_NE(source.find("inputRouter_.state().selectionMode != ViewportSelectionMode::Object"), std::string::npos);
    EXPECT_NE(source.find("drawVisibilityClippedOverlayLine"), std::string::npos);
    EXPECT_NE(source.find("pointVisibleFromCamera"), std::string::npos);
    EXPECT_EQ(source.find("renderBridge.setOverlayFrame(selectionOverlay, camera"), std::string::npos);
    EXPECT_EQ(source.find("shouldDrawOverlayLayer2d"), std::string::npos);
    EXPECT_EQ(source.find("layerNeedsSceneVisibility2d"), std::string::npos);
}

TEST(ViewportPanelTests, RmbContextMenuUsesSdkInstantOperationsAndDisabledItems) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("ViewportOperationsContext"), std::string::npos);
    EXPECT_NE(source.find("modeling.instantOperations()"), std::string::npos);
    EXPECT_NE(source.find("ImGui::MenuItem(operation.label.c_str(), nullptr, false, operation.enabled)"), std::string::npos);
    EXPECT_NE(source.find("modeling.executeInstantOperation(operation.operation)"), std::string::npos);
    EXPECT_EQ(source.find("Button(operation.label"), std::string::npos);
}

TEST(ViewportPanelTests, TransformPreviewCapturesShiftTopologyOperationAtDragBegin) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "ViewportPanel.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("sdkTransformPreviewExtrude_ = sdkPreviewDriven && topologyModifier && tool == ViewportTool::Move"), std::string::npos);
    EXPECT_NE(source.find("sdkTransformPreviewInset_ = sdkPreviewDriven && topologyModifier && tool == ViewportTool::Scale"), std::string::npos);
    EXPECT_NE(source.find("if (sdkTransformPreviewExtrude_)"), std::string::npos);
    EXPECT_NE(source.find("tool == ViewportTool::Scale && sdkTransformPreviewInset_"), std::string::npos);
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
    EXPECT_NE(source.find("drawTransformGizmo(document, assets, modeling, camera, itemMin, itemSize, suppressHoverWork)"), std::string::npos);
    EXPECT_NE(source.find("mouseInsideViewport && !suppressHoverWork"), std::string::npos);
    EXPECT_NE(source.find("hovered && !leftDown && !suppressHoverWork && boxPreview_.dragState"), std::string::npos);
    EXPECT_NE(source.find("!suppressHoverWork &&\n                !gizmoConsumed"), std::string::npos);
    EXPECT_NE(source.find("selectionHoverClearIssued_"), std::string::npos);
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

TEST(SceneRenderBridgeTests, SkipsOverlayProxyRebuildWhenTrianglePayloadIsUnchanged) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "render" / "SceneRenderBridge.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("overlayTriangleSignature"), std::string::npos);
    EXPECT_NE(source.find("externalOverlayTriangleSignature_"), std::string::npos);
    EXPECT_NE(source.find("if (triangleSignature == externalOverlayTriangleSignature_)"), std::string::npos);
    EXPECT_NE(source.find("externalOverlayFrame_ = std::move(frame);\n        return;"), std::string::npos);
    EXPECT_NE(source.find("rebuildOverlayProxies();"), std::string::npos);
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
    EXPECT_NE(source.find("hoverMatches(selectionOverlayCacheHover_, hover)"), std::string::npos);
    EXPECT_NE(source.find("selectionOverlayCacheFrame_ = fromSdk"), std::string::npos);
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
    EXPECT_NE(source.find("srgbToLinear(0.8f)"), std::string::npos);
    EXPECT_EQ(source.find(".format(Texture::InternalFormat::SRGB8_A8)"), std::string::npos);
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

TEST(UiSystemTests, LoadsInterAtEighteenPixelBaseSize) {
    const std::filesystem::path sourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "quader" / "ui" / "UiSystem.cpp";
    std::ifstream sourceFile(sourcePath);
    ASSERT_TRUE(sourceFile.is_open()) << sourcePath.string();

    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(source.find("AddFontFromFileTTF(fontPath.string().c_str(), 18.0f"), std::string::npos);
    EXPECT_EQ(source.find("AddFontFromFileTTF(fontPath.string().c_str(), 16.0f"), std::string::npos);
    EXPECT_EQ(source.find("AddFontFromFileTTF(fontPath.string().c_str(), 15.0f"), std::string::npos);
    EXPECT_NE(source.find("io.FontDefault = font"), std::string::npos);
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

    camera.flyMove({0.0f, 0.0f, 1.0f}, 0.25f, false, false);
    const ViewportCameraPose movedPose = camera.pose();
    const Vec3 movedDelta = movedPose.eye - lookPose.eye;

    EXPECT_GT(dot(movedDelta, lookForward), 1.49f);
    EXPECT_NEAR(length((movedPose.target - lookPose.target) - movedDelta), 0.0f, 0.001f);
}

TEST(ViewportInputRouterTests, ToolHotkeysMatchUnityViewportShortcuts) {
    ViewportInputRouter router;
    ViewportCameraController camera;
    ViewportInputFrame input;
    input.viewportFocused = true;
    input.viewportSize = {800.0f, 600.0f};

    input.pressedKeys = {ViewportKey::B};
    auto result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.shortcutHandled);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Box);

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

    input.modifiers.shift = true;
    input.pressedKeys = {ViewportKey::R};
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::InsertEdgeLoop);

    input.pressedKeys = {ViewportKey::C};
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Cut);

    input.modifiers.shift = false;
    input.pressedKeys = {ViewportKey::C};
    result = router.handleFrame(input, camera);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Knife);

    input.pressedKeys = {ViewportKey::P};
    result = router.handleFrame(input, camera);
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
    EXPECT_NEAR(viewportGridWorldSizeForPreset(1), 1.0f / 32.0f, 0.0001f);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(6), 1.0f, 0.0001f);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(10), 16.0f, 0.0001f);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(-4), 1.0f / 32.0f, 0.0001f);
    EXPECT_NEAR(viewportGridWorldSizeForPreset(99), 16.0f, 0.0001f);
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

    const ViewportCameraPose beforeMove = camera.pose();
    input.rightPressed = false;
    input.mouseScreen = {340.0f, 240.0f};
    input.flyLocalDirection = {0.0f, 0.0f, 1.0f};
    input.deltaSeconds = 0.25f;
    input.pressedKeys = {ViewportKey::W};
    result = router.handleFrame(input, camera);

    EXPECT_TRUE(result.cameraChanged);
    EXPECT_EQ(router.state().activeTool, ViewportTool::Select);
    EXPECT_GT(length(camera.pose().eye - beforeMove.eye), 1.0f);

    input.rightDown = false;
    input.rightReleased = true;
    input.flyLocalDirection = {};
    input.pressedKeys = {};
    result = router.handleFrame(input, camera);
    EXPECT_TRUE(result.consumed);
    EXPECT_FALSE(router.navigating());
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

    scene.clearSelection();
    scene.setSelected(second.object.entity, true);
    service.syncObjectSelectionFromScene();
    center = service.selectionCenter();
    ASSERT_TRUE(center.has_value());
    EXPECT_FLOAT_EQ(center->x, 11.0f);
    EXPECT_FLOAT_EQ(center->y, 4.0f);
    EXPECT_FLOAT_EQ(center->z, -1.0f);
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
    EXPECT_NE(scene.selectedEntities().front(), box.object.entity);
    const MaterialComponent* duplicateMaterial = scene.material(scene.selectedEntities().front());
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

TEST(ModelingSessionServiceTests, ShortcutCommandWrappersRefreshSceneAndHistory) {
    SceneDocument scene;
    AssetRegistry assets;
    ModelingSessionService service(scene, assets);

#if QUADER_HAS_MODELING_SDK
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

    command = service.pasteSelection();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 4u);

    command = service.undo();
    ASSERT_TRUE(command.ok) << command.message;
    EXPECT_TRUE(command.changed);
    EXPECT_EQ(scene.entities().size(), 3u);

    command = service.redo();
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
#else
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
