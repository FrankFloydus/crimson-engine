////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/AssetRegistry.h"
#include "assets/MeshAsset.h"
#include "assets/TextureAsset.h"
#include "core/ColorSpace.h"
#include "render/LightRenderableCache.h"
#include "render/MaterialBindingService.h"
#include "render/MeshRenderableFactory.h"
#include "render/RenderSignatureBuilder.h"
#include "render/RenderLayers.h"
#include "render/ViewportRenderPassCoordinator.h"
#include "render/ViewportOverlayFrame.h"
#include "render/ViewportOverlayPolicy.h"
#include "render/overlay/OverlayCameraProjector.h"
#include "render/overlay/OverlayProxyFactory.h"
#include "render/overlay/OverlayRenderableCache.h"
#include "render/overlay/OverlayRenderableBuilder.h"
#include "render/overlay/ViewportSceneOverlayBuilder.h"
#include "scene/SceneDocument.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

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

MeshAsset makeOverlayTriangleMesh() {
    MeshAsset mesh;
    mesh.name = "Overlay Triangle";
    mesh.vertices = {
            MeshVertex{.position = {-0.5f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.0f, 0.0f}},
            MeshVertex{.position = {0.5f, 0.0f, 0.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {1.0f, 0.0f}},
            MeshVertex{.position = {0.0f, 0.0f, 1.0f}, .normal = {0.0f, 1.0f, 0.0f}, .uv = {0.5f, 1.0f}},
    };
    mesh.indices = {0u, 1u, 2u};
    mesh.primitives = {MeshPrimitiveSpan{.indexOffset = 0u, .indexCount = 3u, .materialSlot = 0u}};
    return mesh;
}

std::size_t lineCountFor(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer) {
    return static_cast<std::size_t>(std::count_if(frame.lines.begin(), frame.lines.end(), [layer](const ViewportOverlayLine& line) {
        return line.layer == layer;
    }));
}

std::size_t lineSourceCountFor(const ViewportOverlayFrame& frame, ViewportOverlaySource source) {
    return static_cast<std::size_t>(std::count_if(frame.lines.begin(), frame.lines.end(), [source](const ViewportOverlayLine& line) {
        return line.source == source;
    }));
}

std::size_t triangleCountFor(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer) {
    return static_cast<std::size_t>(std::count_if(
            frame.triangles.begin(),
            frame.triangles.end(),
            [layer](const ViewportOverlayTriangle& triangle) {
                return triangle.layer == layer;
            }));
}

std::string readSourceFile(std::initializer_list<std::string_view> parts) {
    std::filesystem::path path = QUADER_SOURCE_DIR;
    for (std::string_view part : parts) {
        path /= std::string(part);
    }
    std::ifstream file(path);
    EXPECT_TRUE(file.is_open()) << path.string();
    return {
            std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
}

} // namespace

TEST(RenderSignatureBuilderTests, CentralizesMaterialMeshAndOverlaySignatures) {
    AssetRegistry assets;
    TextureAsset baseColor;
    baseColor.name = "Base Color";
    const AssetId baseColorTexture = assets.addTexture(baseColor);
    TextureAsset roughness;
    roughness.name = "Roughness";
    const AssetId roughnessTexture = assets.addTexture(roughness);

    MaterialDocument material;
    material.revision.value = 7u;
    material.textureSlots.emplace(MaterialTextureSlot::Roughness, roughnessTexture);
    material.textureSlots.emplace(MaterialTextureSlot::BaseColor, baseColorTexture);
    MaterialDocument reorderedMaterial = material;
    reorderedMaterial.textureSlots.clear();
    reorderedMaterial.textureSlots.emplace(MaterialTextureSlot::BaseColor, baseColorTexture);
    reorderedMaterial.textureSlots.emplace(MaterialTextureSlot::Roughness, roughnessTexture);

    MaterialBindingService materialBinding;
    EXPECT_EQ(materialBinding.materialRevision(assets, &material),
            RenderSignatureBuilder::materialSignature(assets, &material));
    EXPECT_EQ(RenderSignatureBuilder::materialSignature(assets, &material),
            RenderSignatureBuilder::materialSignature(assets, &reorderedMaterial));

    MeshAsset mesh = makeOverlayTriangleMesh();
    RenderSignatureBuilder meshSeed;
    meshSeed.addValue(42u).addVec3({1.0f, 2.0f, 3.0f});
    EXPECT_EQ(MeshRenderableFactory::meshAssetSignature(meshSeed.value(), mesh),
            RenderSignatureBuilder::meshAssetSignature(meshSeed.value(), mesh));

    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.0f, -1.0f},
            .end = {0.5f, 0.0f, -1.0f},
            .color = rgba8(255, 128, 64, 255),
            .widthPixels = 2.0f,
    });
    EXPECT_EQ(OverlayRenderableBuilder::frameSignature(frame),
            RenderSignatureBuilder::overlayFrameSignature(frame));
    EXPECT_EQ(OverlayRenderableBuilder::cameraSignature(testOverlayCamera()),
            RenderSignatureBuilder::overlayCameraSignature(testOverlayCamera()));
    EXPECT_EQ(OverlayRenderableBuilder::layerSignature(frame, ViewportOverlayLayer::SourceWire),
            RenderSignatureBuilder::overlayLayerSignature(frame, ViewportOverlayLayer::SourceWire));
}

TEST(RenderSignatureBuilderTests, KeepsRenderHashPolicyInSingleOwner) {
    const std::array<std::string_view, 4u> cacheSources = {
            "src/render/MaterialBindingService.cpp",
            "src/render/MeshRenderableFactory.cpp",
            "src/render/ToolPreviewRenderableCache.cpp",
            "src/render/overlay/OverlayRenderableBuilder.cpp",
    };
    for (const std::string_view source : cacheSources) {
        const std::string text = readSourceFile({source});
        EXPECT_EQ(text.find("kFnv"), std::string::npos);
        EXPECT_EQ(text.find("hashCombine"), std::string::npos);
        EXPECT_EQ(text.find("hashFloat"), std::string::npos);
        EXPECT_EQ(text.find("hashVec3"), std::string::npos);
        EXPECT_NE(text.find("RenderSignatureBuilder"), std::string::npos);
    }
}

TEST(LightRenderableCacheTests, SeparatesRebuildAndTransformSignatures) {
    LightComponent light;
    QMat4 transform;
    const std::uint64_t rebuildSignature = LightRenderableCache::lightSignature(light);
    const std::uint64_t transformSignature = LightRenderableCache::lightTransformSignature(transform);

    QMat4 movedTransform = transform;
    movedTransform.values[12] = 5.0f;
    EXPECT_EQ(LightRenderableCache::lightSignature(light), rebuildSignature);
    EXPECT_NE(LightRenderableCache::lightTransformSignature(movedTransform), transformSignature);

    LightComponent recoloredLight = light;
    recoloredLight.color.x = 0.25f;
    EXPECT_NE(LightRenderableCache::lightSignature(recoloredLight), rebuildSignature);
}

TEST(LightRenderableCacheTests, OwnsIncrementalLightSyncPolicy) {
    const std::string header = readSourceFile({"src", "render", "LightRenderableCache.h"});
    const std::string source = readSourceFile({"src", "render", "LightRenderableCache.cpp"});
    const std::string syncSource = readSourceFile({"src", "render", "SceneRenderSyncService.cpp"});

    EXPECT_NE(header.find("rebuildSignature"), std::string::npos);
    EXPECT_NE(header.find("transformSignature"), std::string::npos);
    EXPECT_NE(source.find("needsRebuild(entityId, light)"), std::string::npos);
    EXPECT_NE(source.find("updateLightTransform(existing->second, worldTransform)"), std::string::npos);
    EXPECT_NE(syncSource.find("lightCache_.syncLight"), std::string::npos);
    EXPECT_EQ(syncSource.find("lightSignature"), std::string::npos);
    EXPECT_EQ(syncSource.find("updateLightTransform"), std::string::npos);
}

TEST(ViewportSceneOverlayBuilderTests, AddsHierarchySelectionWiresAndGroupBounds) {
    SceneDocument document;
    AssetRegistry assets;
    const AssetId mesh = assets.addMesh(makeOverlayTriangleMesh());
    const EntityId group = document.createEmpty("Selected Group");
    const EntityId child = document.createEntity("Child Mesh", EntityKind::Mesh);
    document.setMesh(child, MeshComponent{.mesh = mesh});
    ASSERT_TRUE(document.setParent(child, group));
    ASSERT_TRUE(document.setSelected(group, true));

    ViewportOverlayFrame baseFrame;
    baseFrame.lines.push_back({
            .layer = ViewportOverlayLayer::SelectedEdge,
            .start = {},
            .end = {1.0f, 0.0f, 0.0f},
            .source = ViewportOverlaySource::Selection,
    });

    const ViewportOverlayFrame frame =
            ViewportSceneOverlayBuilder::buildSelectionFrame(std::move(baseFrame), document, assets);

    EXPECT_EQ(lineCountFor(frame, ViewportOverlayLayer::SelectedEdge), 1u);
    EXPECT_EQ(lineSourceCountFor(frame, ViewportOverlaySource::HierarchySelection), 3u);
    EXPECT_EQ(lineCountFor(frame, ViewportOverlayLayer::GroupBounds), 12u);
    EXPECT_TRUE(std::all_of(frame.lines.begin(), frame.lines.end(), [](const ViewportOverlayLine& line) {
        return line.layer != ViewportOverlayLayer::GroupBounds ||
                (line.color == kOverlayGroupBoundsColor &&
                        line.styleRole == ViewportOverlayStyleRole::GroupBounds &&
                        line.source == ViewportOverlaySource::GroupBounds);
    }));
}

TEST(ViewportSceneOverlayBuilderTests, BuildsWireframeOverlayWithoutObjectFaceFills) {
    SceneDocument document;
    AssetRegistry assets;
    const AssetId mesh = assets.addMesh(makeOverlayTriangleMesh());
    const EntityId entity = document.createEntity("Wire Mesh", EntityKind::Mesh);
    document.setMesh(entity, MeshComponent{.mesh = mesh});

    ViewportOverlayFrame baseFrame;
    baseFrame.triangles.push_back({
            .layer = ViewportOverlayLayer::SelectedVisibleFace,
            .a = {},
            .b = {1.0f, 0.0f, 0.0f},
            .c = {0.0f, 1.0f, 0.0f},
    });
    baseFrame.triangles.push_back({
            .layer = ViewportOverlayLayer::HoverFace,
            .a = {},
            .b = {1.0f, 0.0f, 0.0f},
            .c = {0.0f, 0.0f, 1.0f},
    });
    baseFrame.triangles.push_back({
            .layer = ViewportOverlayLayer::ToolPreviewFill,
            .a = {},
            .b = {0.0f, 1.0f, 0.0f},
            .c = {0.0f, 0.0f, 1.0f},
            .source = ViewportOverlaySource::Tool,
    });

    const ViewportOverlayFrame objectFrame =
            ViewportSceneOverlayBuilder::buildWireframeFrame(baseFrame, ModelingSelectionMode::Object, document, assets);

    EXPECT_EQ(triangleCountFor(objectFrame, ViewportOverlayLayer::SelectedVisibleFace), 0u);
    EXPECT_EQ(triangleCountFor(objectFrame, ViewportOverlayLayer::HoverFace), 0u);
    EXPECT_EQ(triangleCountFor(objectFrame, ViewportOverlayLayer::ToolPreviewFill), 1u);
    EXPECT_EQ(lineCountFor(objectFrame, ViewportOverlayLayer::WireframeBase), 3u);
    EXPECT_TRUE(std::all_of(objectFrame.lines.begin(), objectFrame.lines.end(), [](const ViewportOverlayLine& line) {
        return line.layer != ViewportOverlayLayer::WireframeBase ||
                (line.color == rgba8(255, 255, 255) &&
                        line.styleRole == ViewportOverlayStyleRole::WireframeBase &&
                        line.source == ViewportOverlaySource::SceneWireframe);
    }));

    const ViewportOverlayFrame faceFrame =
            ViewportSceneOverlayBuilder::buildWireframeFrame(baseFrame, ModelingSelectionMode::Face, document, assets);

    EXPECT_EQ(triangleCountFor(faceFrame, ViewportOverlayLayer::SelectedVisibleFace), 1u);
    EXPECT_EQ(triangleCountFor(faceFrame, ViewportOverlayLayer::HoverFace), 1u);
    EXPECT_EQ(triangleCountFor(faceFrame, ViewportOverlayLayer::ToolPreviewFill), 1u);
    EXPECT_EQ(lineCountFor(faceFrame, ViewportOverlayLayer::WireframeBase), 3u);
}

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
