#include "core/ColorSpace.h"
#include "render/RenderLayers.h"
#include "render/ViewportRenderPassCoordinator.h"
#include "render/ViewportOverlayFrame.h"
#include "render/ViewportOverlayPolicy.h"
#include "render/overlay/OverlayCameraProjector.h"
#include "render/overlay/OverlayProxyFactory.h"
#include "render/overlay/OverlayRenderableCache.h"
#include "render/overlay/OverlayRenderableBuilder.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

using namespace quader;

namespace {

ViewportOverlayCamera testOverlayCamera() {
    return {
            .eye = {0.0f, 0.0f, 0.0f},
            .forward = {0.0f, 0.0f, -1.0f},
            .right = {1.0f, 0.0f, 0.0f},
            .up = {0.0f, 1.0f, 0.0f},
            .viewportSize = {200.0f, 100.0f},
            .projection = ViewportOverlayProjection::Orthographic,
            .orthographicSize = 2.0f,
            .nearPlane = 0.1f,
            .farPlane = 10.0f,
    };
}

} // namespace

TEST(OverlayLineMeshBuilderTests, ClipsAndExpandsLinesToDeviceSpaceQuads) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.0f, -0.05f},
            .end = {0.5f, 0.0f, -1.0f},
            .color = rgba8(255, 128, 64, 255),
            .widthPixels = 2.0f,
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.25f, 0.25f, -1.0f},
            .end = {0.25f, 0.25f, -1.0f},
            .color = rgba8(255, 128, 64, 255),
            .widthPixels = 2.0f,
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.lineBatches.size(), 1u);
    const OverlayLineBatch& batch = plan.lineBatches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 8u);
    EXPECT_EQ(batch.mesh.indices.size(), 12u);
    EXPECT_FLOAT_EQ(batch.widthPixels, 2.0f);
    EXPECT_FLOAT_EQ(batch.depthBias, kOverlayLineDepthBias);
    EXPECT_EQ(batch.state.depthFunc, OverlayDepthFunc::GreaterOrEqual);
    EXPECT_TRUE(batch.state.depthCulling);
    EXPECT_FALSE(batch.state.depthWrite);
    for (const OverlayLineVertex& vertex : batch.mesh.vertices) {
        EXPECT_FLOAT_EQ(vertex.position.w, 1.0f);
        EXPECT_EQ(vertex.color, rgba8SrgbToLinear(rgba8(255, 128, 64, 255)));
    }
}

TEST(OverlayLineMeshBuilderTests, ExpandsDiagonalLinesPerpendicularToScreenDirection) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, -0.5f, -1.0f},
            .end = {0.5f, 0.5f, -1.0f},
            .color = rgba8(255, 128, 64, 255),
            .widthPixels = 2.0f,
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.lineBatches.size(), 1u);
    const OverlayLineMesh& mesh = plan.lineBatches.front().mesh;
    ASSERT_EQ(mesh.vertices.size(), 4u);
    EXPECT_GT(std::fabs(mesh.vertices[0].position.x - mesh.vertices[3].position.x), 0.0001f);
    EXPECT_GT(std::fabs(mesh.vertices[0].position.y - mesh.vertices[3].position.y), 0.0001f);
}

TEST(OverlayFaceFillBuilderTests, CreatesDepthStampedFaceFillPasses) {
    ViewportOverlayFrame frame;
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SelectedVisibleFace,
            .a = {-1.0f, 0.0f, -1.0f},
            .b = {1.0f, 0.0f, -1.0f},
            .c = {0.0f, 1.0f, -1.0f},
            .color = rgba8(255, 172, 31, 8),
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SelectedVisibleFace);

    ASSERT_EQ(plan.faceFillBatches.size(), 2u);
    EXPECT_EQ(plan.faceFillBatches[0].mesh.vertices.size(), 3u);
    EXPECT_EQ(plan.faceFillBatches[0].mesh.indices.size(), 3u);
    EXPECT_TRUE(plan.faceFillBatches[0].state.depthWrite);
    EXPECT_FALSE(plan.faceFillBatches[0].state.colorWrite);
    EXPECT_EQ(plan.faceFillBatches[0].state.depthFunc, OverlayDepthFunc::GreaterOrEqual);
    EXPECT_FALSE(plan.faceFillBatches[1].state.depthWrite);
    EXPECT_TRUE(plan.faceFillBatches[1].state.colorWrite);
    EXPECT_EQ(plan.faceFillBatches[1].state.depthFunc, OverlayDepthFunc::Equal);
    EXPECT_EQ(plan.faceFillBatches[1].mesh.vertices[0].color, rgba8SrgbToLinear(rgba8(255, 172, 31, 8)));
}

TEST(OverlayPointMeshBuilderTests, BuildsOutlineAndFillPointPasses) {
    ViewportOverlayFrame frame;
    frame.points.push_back({
            .layer = ViewportOverlayLayer::SelectedVertex,
            .position = {0.0f, 0.0f, -1.0f},
            .color = rgba8(255, 211, 31, 255),
            .sizePixels = 6.0f,
            .outlineGrowthPixels = 3.0f,
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SelectedVertex);

    ASSERT_EQ(plan.pointBatches.size(), 2u);
    EXPECT_FLOAT_EQ(plan.pointBatches[0].sizePixels, 9.0f);
    EXPECT_EQ(plan.pointBatches[0].mesh.vertices[0].color, rgba8SrgbToLinear(kOverlayVertexOutlineColor));
    EXPECT_FLOAT_EQ(plan.pointBatches[1].sizePixels, 6.0f);
    EXPECT_EQ(plan.pointBatches[1].mesh.vertices[0].color, rgba8SrgbToLinear(rgba8(255, 211, 31, 255)));
    for (const OverlayPointBatch& batch : plan.pointBatches) {
        EXPECT_EQ(batch.mesh.vertices.size(), 4u);
        EXPECT_EQ(batch.mesh.indices.size(), 6u);
        EXPECT_EQ(batch.state.depthFunc, OverlayDepthFunc::Always);
        EXPECT_FALSE(batch.state.depthCulling);
    }
}

TEST(OverlayRenderableBuilderTests, SanitizesCameraBeforeSigning) {
    ViewportOverlayCamera camera = testOverlayCamera();
    camera.viewportSize = {0.0f, -3.0f};
    camera.forward = {};
    camera.right = {};
    camera.nearPlane = -1.0f;
    camera.farPlane = -0.5f;

    const ViewportOverlayCamera sanitized = OverlayCameraProjector::sanitizedCamera(camera);

    EXPECT_GE(sanitized.viewportSize.x, 1.0f);
    EXPECT_GE(sanitized.viewportSize.y, 1.0f);
    EXPECT_GT(sanitized.nearPlane, 0.0f);
    EXPECT_GT(sanitized.farPlane, sanitized.nearPlane);
    EXPECT_NE(OverlayRenderableBuilder::cameraSignature(sanitized), OverlayRenderableBuilder::cameraSignature(testOverlayCamera()));
}

TEST(OverlayProxyFactoryTests, CountsOnlyNonEmptyRenderableBatches) {
    OverlayLayerBuildPlan plan;
    plan.faceFillBatches.push_back({});
    plan.faceFillBatches.push_back({
            .mesh = {
                    .vertices = {
                            {{0.0f, 0.0f, -1.0f}, {}, rgba8(255, 255, 255, 255)},
                            {{1.0f, 0.0f, -1.0f}, {}, rgba8(255, 255, 255, 255)},
                            {{0.0f, 1.0f, -1.0f}, {}, rgba8(255, 255, 255, 255)},
                    },
                    .indices = {0u, 1u, 2u},
            },
    });
    plan.lineBatches.push_back({
            .mesh = {
                    .vertices = {
                            {{0.0f, 0.0f, 0.0f, 1.0f}, {}, rgba8(255, 255, 255, 255)},
                            {{1.0f, 0.0f, 0.0f, 1.0f}, {}, rgba8(255, 255, 255, 255)},
                            {{1.0f, 1.0f, 0.0f, 1.0f}, {}, rgba8(255, 255, 255, 255)},
                    },
                    .indices = {0u, 1u, 2u},
            },
    });
    plan.pointBatches.push_back({});

    EXPECT_EQ(OverlayProxyFactory::plannedProxyCount(plan), 2u);
}

TEST(OverlayRenderableCacheTests, TracksSlotSignaturesWithoutOwningFilamentProxies) {
    OverlayRenderableCache cache;
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.0f, -1.0f},
            .end = {0.5f, 0.0f, -1.0f},
            .color = rgba8(255, 128, 64, 255),
            .widthPixels = 2.0f,
    });

    const ViewportOverlayCamera camera = testOverlayCamera();
    EXPECT_TRUE(cache.setOverlayFrame(1u, 0u, frame, camera));

    const OverlayRenderableCacheStats stats = cache.stats(1u);
    EXPECT_EQ(stats.frameSignature, OverlayRenderableBuilder::frameSignature(frame));
    EXPECT_EQ(stats.cameraSignature,
            OverlayRenderableBuilder::cameraSignature(OverlayCameraProjector::sanitizedCamera(camera)));
    EXPECT_EQ(stats.layerMask, kViewportOverlayLayerMask);
    EXPECT_EQ(stats.proxyCount, 0u);

    EXPECT_FALSE(cache.setOverlayFrame(1u, 0u, frame, camera));

    frame.lines.front().widthPixels = 4.0f;
    EXPECT_TRUE(cache.setOverlayFrame(1u, 0u, frame, camera));
    EXPECT_EQ(cache.stats(1u).frameSignature, OverlayRenderableBuilder::frameSignature(frame));
}

TEST(SceneRenderBridgeExtractionTests, DelegatesOverlayAndPreviewProxyOwnership) {
    const std::filesystem::path bridgeHeaderPath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "SceneRenderBridge.h";
    const std::filesystem::path bridgeSourcePath =
            std::filesystem::path(QUADER_SOURCE_DIR) / "src" / "render" / "SceneRenderBridge.cpp";
    std::ifstream headerFile(bridgeHeaderPath);
    std::ifstream sourceFile(bridgeSourcePath);
    ASSERT_TRUE(headerFile.is_open()) << bridgeHeaderPath.string();
    ASSERT_TRUE(sourceFile.is_open()) << bridgeSourcePath.string();

    const std::string header{
            std::istreambuf_iterator<char>(headerFile),
            std::istreambuf_iterator<char>()};
    const std::string source{
            std::istreambuf_iterator<char>(sourceFile),
            std::istreambuf_iterator<char>()};

    EXPECT_NE(header.find("OverlayRenderableCache overlayCache_"), std::string::npos);
    EXPECT_NE(header.find("ToolPreviewRenderableCache toolPreviewCache_"), std::string::npos);
    EXPECT_NE(source.find("overlayCache_.setOverlayFrame"), std::string::npos);
    EXPECT_NE(source.find("toolPreviewCache_.setMeshes"), std::string::npos);

    EXPECT_EQ(header.find("struct OverlayProxy"), std::string::npos);
    EXPECT_EQ(header.find("struct OverlaySlot"), std::string::npos);
    EXPECT_EQ(header.find("struct OverlayProxyGroup"), std::string::npos);
    EXPECT_EQ(header.find("overlaySlots_"), std::string::npos);
    EXPECT_EQ(header.find("toolPreviewMeshProxies_"), std::string::npos);
    EXPECT_EQ(source.find("rebuildOverlayProxies"), std::string::npos);
    EXPECT_EQ(source.find("destroyOverlayProxy"), std::string::npos);
    EXPECT_EQ(source.find("destroyToolPreviewMeshes"), std::string::npos);
}

TEST(ViewportRenderPassCoordinatorTests, KeepsGridLayerVisibleForOverlayPasses) {
    ViewportRenderPassCoordinator coordinator;
    const std::uint8_t gridLayerMask = viewportGridLayerMask(2);
    coordinator.initialize(gridLayerMask);

    EXPECT_EQ(coordinator.visibleLayerMask(), static_cast<std::uint8_t>(kViewportSceneLayerMask | gridLayerMask));

    EXPECT_TRUE(coordinator.setGridVisible(false));
    EXPECT_EQ(coordinator.visibleLayerMask(), kViewportSceneLayerMask);

    EXPECT_TRUE(coordinator.setOverlayVisible(true));
    EXPECT_EQ(coordinator.visibleLayerMask(), static_cast<std::uint8_t>(kViewportSceneLayerMask | gridLayerMask));
}
