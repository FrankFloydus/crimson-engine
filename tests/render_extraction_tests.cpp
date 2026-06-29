////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "assets/asset_registry.hpp"
#include "assets/mesh_asset.hpp"
#include "assets/texture_asset.hpp"
#include "core/color_space.hpp"
#include "render/light_renderable_cache.hpp"
#include "render/material_binding_service.hpp"
#include "render/mesh_renderable_factory.hpp"
#include "render/render_signature_builder.hpp"
#include "render/scene_render_extraction.hpp"
#include "render/render_layers.hpp"
#include "render/viewport_render_pass_coordinator.hpp"
#include "render/viewport_overlay_frame.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/overlay/overlay_camera_projector.hpp"
#include "render/overlay/overlay_proxy_factory.hpp"
#include "render/overlay/overlay_renderable_cache.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/viewport_scene_overlay_builder.hpp"
#include "scene/scene_document.hpp"
#include "support/guardrail_file_scan.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

using namespace quader;

namespace {

using quader::test::guardrail::readSourceFile;

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

ViewportOverlayCamera testPerspectiveOverlayCamera() {
    return {
            .eye = {0.0f, 0.0f, 0.0f},
            .forward = {0.0f, 0.0f, -1.0f},
            .right = {1.0f, 0.0f, 0.0f},
            .up = {0.0f, 1.0f, 0.0f},
            .viewportSize = {200.0f, 100.0f},
            .projection = ViewportOverlayProjection::Perspective,
            .fovDegrees = 60.0f,
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

void appendSourceWireDepthStampTriangle(
        ViewportOverlayFrame& frame,
        int objectId,
        QVec3 a,
        QVec3 b,
        QVec3 c,
        bool insideOut) {
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = a,
            .b = insideOut ? c : b,
            .c = insideOut ? b : c,
            .color = rgba8(255, 224, 92, 235),
            .element = {.objectId = objectId},
    });
}

void appendSourceWireDepthStampQuad(
        ViewportOverlayFrame& frame,
        int objectId,
        QVec3 a,
        QVec3 b,
        QVec3 c,
        QVec3 d,
        bool insideOut) {
    appendSourceWireDepthStampTriangle(frame, objectId, a, b, c, insideOut);
    appendSourceWireDepthStampTriangle(frame, objectId, a, c, d, insideOut);
}

void appendSourceWireDepthStampBox(ViewportOverlayFrame& frame, int objectId, bool insideOut) {
    const QVec3 frontBottomLeft{-1.0f, -1.0f, -1.0f};
    const QVec3 frontBottomRight{1.0f, -1.0f, -1.0f};
    const QVec3 frontTopRight{1.0f, 1.0f, -1.0f};
    const QVec3 frontTopLeft{-1.0f, 1.0f, -1.0f};
    const QVec3 backBottomLeft{-1.0f, -1.0f, -3.0f};
    const QVec3 backBottomRight{1.0f, -1.0f, -3.0f};
    const QVec3 backTopRight{1.0f, 1.0f, -3.0f};
    const QVec3 backTopLeft{-1.0f, 1.0f, -3.0f};

    appendSourceWireDepthStampQuad(frame, objectId, frontBottomLeft, frontBottomRight, frontTopRight, frontTopLeft, insideOut);
    appendSourceWireDepthStampQuad(frame, objectId, backBottomLeft, backTopLeft, backTopRight, backBottomRight, insideOut);
    appendSourceWireDepthStampQuad(frame, objectId, frontBottomRight, backBottomRight, backTopRight, frontTopRight, insideOut);
    appendSourceWireDepthStampQuad(frame, objectId, frontBottomLeft, frontTopLeft, backTopLeft, backBottomLeft, insideOut);
    appendSourceWireDepthStampQuad(frame, objectId, frontTopLeft, frontTopRight, backTopRight, backTopLeft, insideOut);
    appendSourceWireDepthStampQuad(frame, objectId, frontBottomLeft, backBottomLeft, backBottomRight, frontBottomRight, insideOut);
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
    material.textureSlots.emplace(MaterialTextureSlot::Roughness, roughnessTexture);
    material.textureSlots.emplace(MaterialTextureSlot::BaseColor, baseColorTexture);
    const AssetId materialId = assets.addMaterial(std::move(material));
    ASSERT_NE(materialId, InvalidAssetId);

    MaterialBindingService materialBinding;
    const MaterialComponent materialComponent{.material = materialId};
    const MaterialComponent orderedSlotComponent{
            .material = materialId,
            .materialSlots = {{1u, materialId}, {2u, materialId}},
    };
    const MaterialComponent reorderedSlotComponent{
            .material = materialId,
            .materialSlots = {{2u, materialId}, {1u, materialId}},
    };
    EXPECT_EQ(materialBinding.materialRevision(assets, &materialComponent),
            RenderSignatureBuilder::materialSignature(assets, &materialComponent));
    EXPECT_EQ(RenderSignatureBuilder::materialSignature(assets, &orderedSlotComponent),
            RenderSignatureBuilder::materialSignature(assets, &reorderedSlotComponent));

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
            "src/render/material_binding_service.cpp",
            "src/render/mesh_renderable_factory.cpp",
            "src/render/tool_preview_renderable_cache.cpp",
            "src/render/overlay/overlay_renderable_builder.cpp",
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

TEST(SceneRenderExtractionTests, BuildsSharedVisibleMeshAndLightPlan) {
    SceneDocument document;
    AssetRegistry assets;
    MaterialBindingService materialBinding;

    const AssetId mesh = assets.addMesh(makeOverlayTriangleMesh());
    const AssetId material = assets.addMaterial(makeDefaultQuaderMaterial("Render Material"));

    const EntityId renderable = document.createEntity("Renderable", EntityKind::Mesh);
    document.setMesh(renderable, MeshComponent{.mesh = mesh, .castShadows = false, .doubleSided = true});
    document.setMaterial(renderable, MaterialComponent{.material = material});

    const EntityId missingMesh = document.createEntity("Missing Mesh", EntityKind::Mesh);
    document.setMesh(missingMesh, MeshComponent{.mesh = mesh + 1000u});

    const EntityId hidden = document.createEntity("Hidden Mesh", EntityKind::Mesh);
    document.setMesh(hidden, MeshComponent{.mesh = mesh});
    document.setVisible(hidden, false);

    const EntityId light = document.createEntity("Light", EntityKind::Light);
    document.setLight(light, LightComponent{});

    const SceneRenderExtractionPlan plan = buildSceneRenderExtractionPlan(document, assets, materialBinding);

    ASSERT_EQ(plan.meshes.size(), 2u);
    const auto renderableEntry = std::find_if(
            plan.meshes.begin(),
            plan.meshes.end(),
            [renderable](const SceneRenderMeshEntry& entry) {
                return entry.entityId == renderable;
            });
    ASSERT_NE(renderableEntry, plan.meshes.end());
    EXPECT_TRUE(renderableEntry->renderable());
    EXPECT_EQ(renderableEntry->meshId, mesh);
    EXPECT_EQ(renderableEntry->materialId, material);
    EXPECT_FALSE(renderableEntry->castShadows());
    EXPECT_TRUE(renderableEntry->doubleSided());
    EXPECT_EQ(renderableEntry->materialRevision,
            materialBinding.materialRevision(assets, renderableEntry->materialComponent));
    EXPECT_TRUE(plan.liveMeshEntities.contains(renderable));

    const auto missingEntry = std::find_if(
            plan.meshes.begin(),
            plan.meshes.end(),
            [missingMesh](const SceneRenderMeshEntry& entry) {
                return entry.entityId == missingMesh;
            });
    ASSERT_NE(missingEntry, plan.meshes.end());
    EXPECT_FALSE(missingEntry->renderable());
    EXPECT_FALSE(plan.liveMeshEntities.contains(missingMesh));
    EXPECT_FALSE(plan.liveMeshEntities.contains(hidden));

    ASSERT_EQ(plan.lights.size(), 1u);
    EXPECT_EQ(plan.lights.front().entityId, light);
    EXPECT_TRUE(plan.liveLightEntities.contains(light));
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
    const std::string header = readSourceFile({"src", "render", "light_renderable_cache.hpp"});
    const std::string source = readSourceFile({"src", "render", "light_renderable_cache.cpp"});
    const std::string syncSource = readSourceFile({"src", "render", "scene_render_sync_service.cpp"});

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
            .layer = ViewportOverlayLayer::OpenEdge,
            .start = {-0.5f, 0.0f, -0.05f},
            .end = {0.5f, 0.0f, -1.0f},
            .color = rgba8(255, 128, 64, 255),
            .widthPixels = 2.0f,
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::OpenEdge,
            .start = {-0.25f, 0.25f, -1.0f},
            .end = {0.25f, 0.25f, -1.0f},
            .color = rgba8(255, 128, 64, 255),
            .widthPixels = 2.0f,
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::OpenEdge);

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
    const OverlayLineBatch& batch = plan.lineBatches.front();
    EXPECT_FLOAT_EQ(batch.depthBias, 0.0f);
    EXPECT_EQ(batch.state.depthFunc, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depthCulling);
    const OverlayLineMesh& mesh = batch.mesh;
    ASSERT_EQ(mesh.vertices.size(), 4u);
    EXPECT_GT(std::fabs(mesh.vertices[0].position.x - mesh.vertices[3].position.x), 0.0001f);
    EXPECT_GT(std::fabs(mesh.vertices[0].position.y - mesh.vertices[3].position.y), 0.0001f);
}

TEST(OverlayLineMeshBuilderTests, SourceWireDepthStampsCullOutwardOccludedHandles) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.0f, -1.0f},
            .end = {0.5f, 0.0f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .widthPixels = 2.0f,
            .element = {.objectId = 1},
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.0f, -2.0f},
            .end = {0.5f, 0.0f, -2.0f},
            .color = rgba8(255, 224, 92, 235),
            .widthPixels = 2.0f,
            .element = {.objectId = 1},
    });
    appendSourceWireDepthStampBox(frame, 1, false);

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.lineBatches.size(), 1u);
    const OverlayLineBatch& batch = plan.lineBatches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_EQ(batch.mesh.indices.size(), 6u);
    EXPECT_FLOAT_EQ(batch.depthBias, 0.0f);
    EXPECT_EQ(batch.state.depthFunc, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depthCulling);
    EXPECT_FALSE(batch.state.depthWrite);
    for (const OverlayLineVertex& vertex : batch.mesh.vertices) {
        EXPECT_EQ(vertex.color, rgba8SrgbToLinear(rgba8(255, 224, 92, 235)));
    }
}

TEST(OverlayLineMeshBuilderTests, SourceWireDepthStampsKeepInsideOutTopologyVisible) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.0f, -2.0f},
            .end = {0.5f, 0.0f, -2.0f},
            .color = rgba8(255, 224, 92, 235),
            .widthPixels = 2.0f,
            .element = {.objectId = 1},
    });
    appendSourceWireDepthStampBox(frame, 1, true);

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.lineBatches.size(), 1u);
    const OverlayLineBatch& batch = plan.lineBatches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_EQ(batch.mesh.indices.size(), 6u);
    EXPECT_FLOAT_EQ(batch.depthBias, 0.0f);
    EXPECT_EQ(batch.state.depthFunc, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depthCulling);
}

TEST(OverlayLineMeshBuilderTests, SourceWireDrawsInvertedInternalEdges) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.0f, -1.0f},
            .end = {0.5f, 0.0f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .widthPixels = 2.0f,
            .element = {.objectId = 1},
    });
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = {-0.5f, 0.0f, -1.0f},
            .b = {0.5f, 0.0f, -1.0f},
            .c = {0.0f, 0.5f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .element = {.objectId = 1},
    });
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = {0.5f, 0.0f, -1.0f},
            .b = {-0.5f, 0.0f, -1.0f},
            .c = {0.0f, -0.5f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .element = {.objectId = 1},
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.lineBatches.size(), 1u);
    const OverlayLineBatch& batch = plan.lineBatches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(batch.depthBias, 0.0f);
    EXPECT_EQ(batch.state.depthFunc, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depthCulling);
}

TEST(OverlayLineMeshBuilderTests, SourceWireDrawsBackFacingBoundaryEdges) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, -0.5f, -1.0f},
            .end = {0.5f, -0.5f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .widthPixels = 2.0f,
            .element = {.objectId = 1},
    });
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = {-0.5f, -0.5f, -1.0f},
            .b = {0.5f, -0.5f, -1.0f},
            .c = {0.0f, -1.0f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .element = {.objectId = 1},
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.lineBatches.size(), 1u);
    const OverlayLineBatch& batch = plan.lineBatches.front();
    EXPECT_EQ(batch.mesh.vertices.size(), 4u);
    EXPECT_FLOAT_EQ(batch.depthBias, 0.0f);
    EXPECT_EQ(batch.state.depthFunc, OverlayDepthFunc::Always);
    EXPECT_FALSE(batch.state.depthCulling);
}

TEST(OverlayLineMeshBuilderTests, SourceWireUsesOneOnTopBatchBesideDepthStamps) {
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.0f, -1.0f},
            .end = {0.5f, 0.0f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .widthPixels = 2.0f,
            .element = {.objectId = 1},
    });
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SourceWire,
            .start = {-0.5f, 0.5f, -1.0f},
            .end = {0.5f, 0.5f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .widthPixels = 2.0f,
            .element = {.objectId = 2},
    });
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = {-0.5f, 0.0f, -1.0f},
            .b = {0.5f, 0.0f, -1.0f},
            .c = {0.0f, 0.25f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
            .element = {.objectId = 1},
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SourceWire);

    ASSERT_EQ(plan.lineBatches.size(), 1u);
    EXPECT_EQ(plan.lineBatches[0].mesh.vertices.size(), 8u);
    EXPECT_FLOAT_EQ(plan.lineBatches[0].depthBias, 0.0f);
    EXPECT_EQ(plan.lineBatches[0].state.depthFunc, OverlayDepthFunc::Always);
    EXPECT_FALSE(plan.lineBatches[0].state.depthCulling);
}

TEST(OverlayLineMeshBuilderTests, ExtendsLineCapsInScreenSpace) {
    const ViewportOverlayCamera camera = testOverlayCamera();
    const QVec3 lineStart{-0.25f, 0.0f, -1.0f};
    const QVec3 lineEnd{0.25f, 0.0f, -1.0f};
    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::SelectedEdge,
            .start = lineStart,
            .end = lineEnd,
            .color = rgba8(255, 211, 31, 255),
            .widthPixels = 2.0f,
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, camera, ViewportOverlayLayer::SelectedEdge);

    ASSERT_EQ(plan.lineBatches.size(), 1u);
    const OverlayLineMesh& mesh = plan.lineBatches.front().mesh;
    ASSERT_EQ(mesh.vertices.size(), 4u);

    const OverlayCameraProjector projector(camera);
    const OverlayProjectedPoint projectedStart = projector.project(projector.viewPointFor(lineStart));
    const OverlayProjectedPoint projectedEnd = projector.project(projector.viewPointFor(lineEnd));
    ASSERT_TRUE(projectedStart.valid);
    ASSERT_TRUE(projectedEnd.valid);

    const float capPixels = (2.0f + kOverlayLineFeatherPixels) * 0.5f;
    const float capDeviceX = capPixels * 2.0f / camera.viewportSize.x;
    const float startCapCenterX = (mesh.vertices[0].position.x + mesh.vertices[3].position.x) * 0.5f;
    const float endCapCenterX = (mesh.vertices[1].position.x + mesh.vertices[2].position.x) * 0.5f;
    EXPECT_NEAR(startCapCenterX, projectedStart.device.x - capDeviceX, 0.00001f);
    EXPECT_NEAR(endCapCenterX, projectedEnd.device.x + capDeviceX, 0.00001f);
}

TEST(OverlayLineMeshBuilderTests, PerspectiveDepthBiasDoesNotMoveLineOrPointScreenPosition) {
    const ViewportOverlayCamera camera = testPerspectiveOverlayCamera();
    const QVec3 lineStart{-0.5f, 0.2f, -1.0f};
    const QVec3 lineEnd{0.5f, 0.2f, -1.0f};
    const QVec3 pointPosition{0.35f, -0.25f, -1.0f};

    ViewportOverlayFrame frame;
    frame.lines.push_back({
            .layer = ViewportOverlayLayer::OpenEdge,
            .start = lineStart,
            .end = lineEnd,
            .color = rgba8(255, 128, 64, 255),
            .widthPixels = 2.0f,
    });
    frame.points.push_back({
            .layer = ViewportOverlayLayer::UnselectedVertex,
            .position = pointPosition,
            .color = rgba8(255, 211, 31, 255),
            .sizePixels = 6.0f,
    });

    const OverlayLayerBuildPlan linePlan =
            OverlayRenderableBuilder::buildLayer(frame, camera, ViewportOverlayLayer::OpenEdge);
    const OverlayLayerBuildPlan pointPlan =
            OverlayRenderableBuilder::buildLayer(frame, camera, ViewportOverlayLayer::UnselectedVertex);

    ASSERT_EQ(linePlan.lineBatches.size(), 1u);
    ASSERT_EQ(pointPlan.pointBatches.size(), 1u);
    const OverlayLineMesh& lineMesh = linePlan.lineBatches.front().mesh;
    const OverlayPointMesh& pointMesh = pointPlan.pointBatches.front().mesh;
    ASSERT_EQ(lineMesh.vertices.size(), 4u);
    ASSERT_EQ(pointMesh.vertices.size(), 4u);

    const OverlayCameraProjector projector(camera);
    const OverlayProjectedPoint projectedLineStart = projector.project(projector.viewPointFor(lineStart));
    const OverlayProjectedPoint projectedLineEnd = projector.project(projector.viewPointFor(lineEnd));
    const OverlayProjectedPoint projectedPoint = projector.project(projector.viewPointFor(pointPosition));
    ASSERT_TRUE(projectedLineStart.valid);
    ASSERT_TRUE(projectedLineEnd.valid);
    ASSERT_TRUE(projectedPoint.valid);

    const float expectedLineDepth =
            projectedLineStart.device.z + camera.nearPlane * kOverlayDeviceDepthBiasPerNearPlane * kOverlayLineDepthBias;
    const float lineCenterX = (lineMesh.vertices[0].position.x + lineMesh.vertices[1].position.x +
                               lineMesh.vertices[2].position.x + lineMesh.vertices[3].position.x) *
            0.25f;
    const float lineCenterY = (lineMesh.vertices[0].position.y + lineMesh.vertices[1].position.y +
                               lineMesh.vertices[2].position.y + lineMesh.vertices[3].position.y) *
            0.25f;
    EXPECT_NEAR(lineCenterX, (projectedLineStart.device.x + projectedLineEnd.device.x) * 0.5f, 0.00001f);
    EXPECT_NEAR(lineCenterY, (projectedLineStart.device.y + projectedLineEnd.device.y) * 0.5f, 0.00001f);
    EXPECT_NEAR(lineMesh.vertices[0].position.z, expectedLineDepth, 0.00001f);

    const float expectedPointDepth =
            projectedPoint.device.z + camera.nearPlane * kOverlayDeviceDepthBiasPerNearPlane * kOverlayPointDepthBias;
    const float pointCenterX = (pointMesh.vertices[0].position.x + pointMesh.vertices[1].position.x +
                                pointMesh.vertices[2].position.x + pointMesh.vertices[3].position.x) *
            0.25f;
    const float pointCenterY = (pointMesh.vertices[0].position.y + pointMesh.vertices[1].position.y +
                                pointMesh.vertices[2].position.y + pointMesh.vertices[3].position.y) *
            0.25f;
    EXPECT_NEAR(pointCenterX, projectedPoint.device.x, 0.00001f);
    EXPECT_NEAR(pointCenterY, projectedPoint.device.y, 0.00001f);
    EXPECT_NEAR(pointMesh.vertices[0].position.z, expectedPointDepth, 0.00001f);
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

TEST(OverlayFaceFillBuilderTests, SelectedAndHoverFaceFillsRenderTwoSided) {
    const std::array<ViewportOverlayLayer, 4u> layers{{
            ViewportOverlayLayer::SelectedVisibleFace,
            ViewportOverlayLayer::SelectedFaceMask,
            ViewportOverlayLayer::SelectedXrayFace,
            ViewportOverlayLayer::HoverFace,
    }};

    for (const ViewportOverlayLayer layer : layers) {
        SCOPED_TRACE(static_cast<int>(layer));
        ViewportOverlayFrame frame;
        frame.triangles.push_back({
                .layer = layer,
                .a = {-1.0f, 0.0f, -1.0f},
                .b = {0.0f, 1.0f, -1.0f},
                .c = {1.0f, 0.0f, -1.0f},
                .color = rgba8(31, 255, 102, 22),
        });

        const OverlayLayerBuildPlan plan =
                OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), layer);

        ASSERT_EQ(plan.faceFillBatches.size(), 2u);
        EXPECT_TRUE(plan.faceFillBatches[0].state.depthWrite);
        EXPECT_FALSE(plan.faceFillBatches[0].state.colorWrite);
        EXPECT_EQ(plan.faceFillBatches[0].state.depthFunc, OverlayDepthFunc::GreaterOrEqual);
        EXPECT_FALSE(plan.faceFillBatches[1].state.depthWrite);
        EXPECT_TRUE(plan.faceFillBatches[1].state.colorWrite);
        EXPECT_EQ(plan.faceFillBatches[1].state.depthFunc, OverlayDepthFunc::Equal);
        for (const OverlayFaceFillBatch& batch : plan.faceFillBatches) {
            EXPECT_EQ(batch.state.cullingMode, OverlayCullingMode::None);
            EXPECT_EQ(batch.state.transparencyMode, OverlayTransparencyMode::TwoPassesTwoSides);
        }
    }
}

TEST(OverlayFaceFillBuilderTests, SourceWireDepthStampDoesNotRenderDepthPass) {
    ViewportOverlayFrame frame;
    frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = {-1.0f, 0.0f, -1.0f},
            .b = {1.0f, 0.0f, -1.0f},
            .c = {0.0f, 1.0f, -1.0f},
            .color = rgba8(255, 224, 92, 235),
    });

    const OverlayLayerBuildPlan plan =
            OverlayRenderableBuilder::buildLayer(frame, testOverlayCamera(), ViewportOverlayLayer::SourceWireDepthStamp);

    EXPECT_TRUE(plan.faceFillBatches.empty());
    EXPECT_TRUE(plan.lineBatches.empty());
    EXPECT_TRUE(plan.pointBatches.empty());
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
    const std::string header = readSourceFile({"src", "render", "scene_render_bridge.hpp"});
    const std::string source = readSourceFile({"src", "render", "scene_render_bridge.cpp"});

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
