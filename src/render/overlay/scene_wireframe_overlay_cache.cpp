////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/scene_wireframe_overlay_cache.hpp"

#include "assets/asset_registry.hpp"
#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "mesh/polygon/document.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"

#include <backend/DriverEnums.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr float kSceneWireframeLineNearPlane = 0.05f;

/**
 * Stores one shader-expanded persistent wireframe line vertex.
 */
struct SceneWireframeLineVertex {
    OverlayFloat4 position;
    OverlayFloat4 start;
    OverlayFloat4 end;
    OverlayFloat4 control;
    std::uint32_t color = 0u;
};

filament::math::mat4f makeTransform(const QMat4& transform) {
    const glm::mat4 glmTransform = toGlm(transform);
    filament::math::mat4f result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result[column][row] = glmTransform[column][row];
        }
    }
    return result;
}

filament::MaterialInstance::DepthFunc toFilamentDepthFunc(OverlayDepthFunc depthFunc) {
    switch (depthFunc) {
    case OverlayDepthFunc::Always:
        return filament::MaterialInstance::DepthFunc::A;
    case OverlayDepthFunc::Equal:
        return filament::MaterialInstance::DepthFunc::E;
    case OverlayDepthFunc::GreaterOrEqual:
        return filament::MaterialInstance::DepthFunc::GE;
    }
    return filament::MaterialInstance::DepthFunc::A;
}

filament::MaterialInstance::CullingMode toFilamentCullingMode(OverlayCullingMode cullingMode) {
    switch (cullingMode) {
    case OverlayCullingMode::None:
        return filament::MaterialInstance::CullingMode::NONE;
    case OverlayCullingMode::Back:
        return filament::MaterialInstance::CullingMode::BACK;
    }
    return filament::MaterialInstance::CullingMode::NONE;
}

filament::MaterialInstance::TransparencyMode toFilamentTransparencyMode(OverlayTransparencyMode transparencyMode) {
    switch (transparencyMode) {
    case OverlayTransparencyMode::Default:
        return filament::MaterialInstance::TransparencyMode::DEFAULT;
    case OverlayTransparencyMode::TwoPassesOneSide:
        return filament::MaterialInstance::TransparencyMode::TWO_PASSES_ONE_SIDE;
    case OverlayTransparencyMode::TwoPassesTwoSides:
        return filament::MaterialInstance::TransparencyMode::TWO_PASSES_TWO_SIDES;
    }
    return filament::MaterialInstance::TransparencyMode::DEFAULT;
}

void configureOverlayMaterial(filament::MaterialInstance& material, const OverlayRenderState& state) {
    material.setColorWrite(state.colorWrite);
    material.setDepthWrite(state.depthWrite);
    material.setDepthCulling(state.depthCulling);
    material.setDepthFunc(toFilamentDepthFunc(state.depthFunc));
    material.setCullingMode(toFilamentCullingMode(state.cullingMode));
    material.setTransparencyMode(toFilamentTransparencyMode(state.transparencyMode));
    material.setPolygonOffset(state.polygonOffsetScale, state.polygonOffsetConstant);
}

std::uint64_t edgeKey(std::uint32_t a, std::uint32_t b) {
    const std::uint32_t lo = std::min(a, b);
    const std::uint32_t hi = std::max(a, b);
    return (static_cast<std::uint64_t>(lo) << 32u) | static_cast<std::uint64_t>(hi);
}

void appendWireframeLineQuad(
        std::vector<SceneWireframeLineVertex>& vertices,
        std::vector<std::uint32_t>& indices,
        QVec3 start,
        QVec3 end,
        std::uint32_t colorRgba) {
    const std::uint32_t color = rgba8SrgbToLinear(colorRgba);
    const std::uint32_t offset = static_cast<std::uint32_t>(vertices.size());
    const OverlayFloat4 startValue{start.x, start.y, start.z, 1.0f};
    const OverlayFloat4 endValue{end.x, end.y, end.z, 1.0f};
    const OverlayFloat4 deviceOrigin{0.0f, 0.0f, 0.0f, 1.0f};
    vertices.push_back({deviceOrigin, startValue, endValue, {-1.0f, -1.0f, 0.0f, 0.0f}, color});
    vertices.push_back({deviceOrigin, startValue, endValue, {-1.0f, 1.0f, 1.0f, 0.0f}, color});
    vertices.push_back({deviceOrigin, startValue, endValue, {1.0f, 1.0f, 1.0f, 0.0f}, color});
    vertices.push_back({deviceOrigin, startValue, endValue, {1.0f, -1.0f, 0.0f, 0.0f}, color});
    indices.push_back(offset);
    indices.push_back(offset + 1u);
    indices.push_back(offset + 2u);
    indices.push_back(offset);
    indices.push_back(offset + 2u);
    indices.push_back(offset + 3u);
}

bool appendAuthoredPolygonWireframeLines(
        const MeshAsset& mesh,
        std::uint32_t color,
        std::vector<SceneWireframeLineVertex>& vertices,
        std::vector<std::uint32_t>& indices) {
    if (!mesh.hasAuthoredPolygonDocument || !mesh.authoredPolygonDocument) {
        return false;
    }

    const quader_poly::Document& document = *mesh.authoredPolygonDocument;
    std::unordered_map<quader_poly::ElementId, QVec3> positions;
    positions.reserve(document.vertices.size());
    for (const quader_poly::Vertex& vertex : document.vertices) {
        positions.emplace(vertex.id, vertex.position);
    }

    std::unordered_set<std::uint64_t> emittedEdges;
    for (const quader_poly::Edge edge : quader_poly::document_edges(document)) {
        const auto a = positions.find(edge.a);
        const auto b = positions.find(edge.b);
        if (a == positions.end() || b == positions.end()) {
            continue;
        }
        const std::uint64_t key = edgeKey(
                static_cast<std::uint32_t>(edge.a),
                static_cast<std::uint32_t>(edge.b));
        if (!emittedEdges.insert(key).second) {
            continue;
        }
        appendWireframeLineQuad(vertices, indices, a->second, b->second, color);
    }
    return !emittedEdges.empty();
}

void appendTriangleMeshWireframeLines(
        const MeshAsset& mesh,
        std::uint32_t color,
        std::vector<SceneWireframeLineVertex>& vertices,
        std::vector<std::uint32_t>& indices) {
    std::unordered_set<std::uint64_t> emittedEdges;
    emittedEdges.reserve(mesh.indices.size());
    auto appendEdge = [&](std::uint32_t a, std::uint32_t b) {
        if (a >= mesh.vertices.size() || b >= mesh.vertices.size() || a == b) {
            return;
        }
        if (!emittedEdges.insert(edgeKey(a, b)).second) {
            return;
        }
        appendWireframeLineQuad(vertices, indices, mesh.vertices[a].position, mesh.vertices[b].position, color);
    };

    for (std::size_t index = 0; index + 2u < mesh.indices.size(); index += 3u) {
        appendEdge(mesh.indices[index], mesh.indices[index + 1u]);
        appendEdge(mesh.indices[index + 1u], mesh.indices[index + 2u]);
        appendEdge(mesh.indices[index + 2u], mesh.indices[index]);
    }
}

SceneWireframeOverlayCache::Proxy createProxy(
        filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& material,
        filament::TransformManager::Instance parentTransform,
        ViewportOverlayLayer layer,
        const std::vector<SceneWireframeLineVertex>& vertices,
        const std::vector<std::uint32_t>& indices,
        const MeshAsset& boundsMesh,
        std::uint8_t layerMask) {
    SceneWireframeOverlayCache::Proxy proxy;
    if (vertices.empty() || indices.empty()) {
        return proxy;
    }

    const std::uint16_t blendOrder =
            static_cast<std::uint16_t>(viewportOverlayDrawOrder(layer) * 64);
    const OverlayRenderState state = OverlayRenderableBuilder::makeState(
            layer,
            ViewportOverlayGeometryKind::Lines,
            blendOrder);

    proxy.entity = utils::EntityManager::get().create();
    proxy.vertexCount = static_cast<std::uint32_t>(vertices.size());
    proxy.indexCount = static_cast<std::uint32_t>(indices.size());
    proxy.materialInstance = material.createInstance();
    configureOverlayMaterial(*proxy.materialInstance, state);
    proxy.materialInstance->setParameter("lineWidth", viewportOverlayLineWidthPixels(layer, 0.0f));
    proxy.materialInstance->setParameter("featherPixels", kOverlayLineFeatherPixels);
    proxy.materialInstance->setParameter("nearPlane", kSceneWireframeLineNearPlane);

    proxy.vertexBuffer = filament::VertexBuffer::Builder()
            .vertexCount(proxy.vertexCount)
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT4, offsetof(SceneWireframeLineVertex, position), sizeof(SceneWireframeLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM0, 0, AttributeType::FLOAT4, offsetof(SceneWireframeLineVertex, start), sizeof(SceneWireframeLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM1, 0, AttributeType::FLOAT4, offsetof(SceneWireframeLineVertex, end), sizeof(SceneWireframeLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM2, 0, AttributeType::FLOAT4, offsetof(SceneWireframeLineVertex, control), sizeof(SceneWireframeLineVertex))
            .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(SceneWireframeLineVertex, color), sizeof(SceneWireframeLineVertex))
            .normalized(filament::VertexAttribute::COLOR)
            .build(engine);
    proxy.vertexBuffer->setBufferAt(engine, 0, FilamentBufferDescriptorFactory::vector(vertices));

    proxy.indexBuffer = filament::IndexBuffer::Builder()
            .indexCount(proxy.indexCount)
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(engine);
    proxy.indexBuffer->setBuffer(engine, FilamentBufferDescriptorFactory::vector(indices));

    filament::RenderableManager::Builder(1)
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, proxy.vertexBuffer, proxy.indexBuffer)
            .material(0, proxy.materialInstance)
            .blendOrder(0, state.blendOrder)
            .globalBlendOrderEnabled(0, true)
            .layerMask(kViewportAllLayerMask, layerMask)
            .boundingBox(computeBounds(boundsMesh))
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .build(engine, proxy.entity);
    engine.getTransformManager().create(proxy.entity, parentTransform);
    scene.addEntity(proxy.entity);
    return proxy;
}

} // namespace

SceneWireframeOverlayCache::Proxy::Proxy(Proxy&& other) noexcept
    : entity(other.entity)
    , vertexBuffer(std::exchange(other.vertexBuffer, nullptr))
    , indexBuffer(std::exchange(other.indexBuffer, nullptr))
    , materialInstance(std::exchange(other.materialInstance, nullptr))
    , vertexCount(std::exchange(other.vertexCount, 0u))
    , indexCount(std::exchange(other.indexCount, 0u)) {
    other.entity.clear();
}

SceneWireframeOverlayCache::Proxy& SceneWireframeOverlayCache::Proxy::operator=(Proxy&& other) noexcept {
    if (this == &other) {
        return *this;
    }
    entity = other.entity;
    other.entity.clear();
    vertexBuffer = std::exchange(other.vertexBuffer, nullptr);
    indexBuffer = std::exchange(other.indexBuffer, nullptr);
    materialInstance = std::exchange(other.materialInstance, nullptr);
    vertexCount = std::exchange(other.vertexCount, 0u);
    indexCount = std::exchange(other.indexCount, 0u);
    return *this;
}

SceneWireframeOverlayCache::~SceneWireframeOverlayCache() {
    shutdown();
}

void SceneWireframeOverlayCache::initialize(
        filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& lineMaterial) {
    engine_ = &engine;
    scene_ = &scene;
    lineMaterial_ = &lineMaterial;
}

void SceneWireframeOverlayCache::shutdown() {
    if (!engine_) {
        return;
    }

    for (auto& [entityId, entry] : entries_) {
        (void)entityId;
        destroyEntry(entry);
    }
    entries_.clear();
    syncedStates_.clear();
    syncedSceneRevision_ = 0;
    syncedAssetRevision_ = 0;
    layerMask_ = 0u;
    hasSynced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    lineMaterial_ = nullptr;
}

void SceneWireframeOverlayCache::sync(
        const SceneDocument& document,
        const AssetRegistry& assets,
        std::uint8_t layerMask) {
    QDR_PROFILE_FUNCTION();
    if (!initialized()) {
        return;
    }

    setLayerMask(layerMask);
    if (layerMask == 0u) {
        return;
    }

    const std::uint64_t sceneRevision = document.revision();
    const std::uint64_t assetRevision = assets.revision();
    if (hasSynced_ &&
            syncedSceneRevision_ == sceneRevision &&
            syncedAssetRevision_ == assetRevision) {
        return;
    }

    const std::vector<WireframeEntityState> states = wireframeEntityStates(document, assets);
    if (hasSynced_ &&
            syncedAssetRevision_ == assetRevision &&
            states == syncedStates_) {
        updateEntryTransforms(document);
        syncedSceneRevision_ = sceneRevision;
        return;
    }

    std::unordered_set<EntityId> liveEntities;
    liveEntities.reserve(states.size());
    for (const WireframeEntityState& state : states) {
        QDR_PROFILE_SCOPE("Scene wireframe overlay sync entity");
        liveEntities.insert(state.entity);
        auto existing = entries_.find(state.entity);
        const bool rebuild = existing == entries_.end() ||
                existing->second.meshId != state.meshId ||
                existing->second.meshRevision != state.meshRevision ||
                existing->second.selected != state.selected;
        if (rebuild) {
            if (existing != entries_.end()) {
                destroyEntry(existing->second);
                entries_.erase(existing);
            }
            Entry entry = createEntry(state.entity, state.meshId, state.selected, document, assets);
            existing = entries_.emplace(state.entity, std::move(entry)).first;
        }
        updateEntryTransform(existing->second, document, state.entity);
    }

    removeExcept(liveEntities);
    syncedStates_ = states;
    syncedSceneRevision_ = sceneRevision;
    syncedAssetRevision_ = assetRevision;
    hasSynced_ = true;
}

std::vector<SceneWireframeOverlayCache::WireframeEntityState> SceneWireframeOverlayCache::wireframeEntityStates(
        const SceneDocument& document,
        const AssetRegistry& assets) const {
    QDR_PROFILE_FUNCTION();
    std::vector<WireframeEntityState> states;
    const std::vector<EntityId> entities = document.entities();
    states.reserve(entities.size());
    for (const EntityId entityId : entities) {
        if (!document.isEffectivelyVisible(entityId)) {
            continue;
        }
        const MeshComponent* meshComponent = document.mesh(entityId);
        if (meshComponent == nullptr || meshComponent->mesh == InvalidAssetId) {
            continue;
        }
        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3u) {
            continue;
        }
        states.push_back({
                .entity = entityId,
                .meshId = meshComponent->mesh,
                .meshRevision = mesh->revision.value,
                .selected = [&]() {
                    const SelectionComponent* selection = document.selection(entityId);
                    return selection != nullptr && selection->selected;
                }(),
        });
    }
    return states;
}

SceneWireframeOverlayCache::Entry SceneWireframeOverlayCache::createEntry(
        EntityId entityId,
        AssetId meshId,
        bool selected,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    Entry entry;
    entry.meshId = meshId;
    const MeshAsset* mesh = assets.mesh(meshId);
    if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.size() < 3u) {
        return entry;
    }
    entry.meshRevision = mesh->revision.value;
    entry.selected = selected;
    entry.transformRoot = utils::EntityManager::get().create();
    filament::TransformManager& transforms = engine_->getTransformManager();
    transforms.create(entry.transformRoot);
    const filament::TransformManager::Instance parentTransform = transforms.getInstance(entry.transformRoot);

    const ViewportOverlayLayer layer = selected ? ViewportOverlayLayer::SelectedEdge : ViewportOverlayLayer::WireframeXray;
    const std::uint32_t color = selected ? kOverlaySelectionWireColor : kOverlayWireframeXrayColor;
    std::vector<SceneWireframeLineVertex> lineVertices;
    std::vector<std::uint32_t> lineIndices;
    if (!appendAuthoredPolygonWireframeLines(*mesh, color, lineVertices, lineIndices)) {
        appendTriangleMeshWireframeLines(*mesh, color, lineVertices, lineIndices);
    }
    entry.lineProxy = createProxy(
            *engine_,
            *scene_,
            *lineMaterial_,
            parentTransform,
            layer,
            lineVertices,
            lineIndices,
            *mesh,
            layerMask_);
    updateEntryTransform(entry, document, entityId);
    return entry;
}

void SceneWireframeOverlayCache::destroyEntry(Entry& entry) {
    destroyProxy(entry.lineProxy, true);
    destroyTransformRoot(entry);
}

void SceneWireframeOverlayCache::destroyProxy(Proxy& proxy, bool removeFromScene) {
    if (!engine_) {
        return;
    }

    if (proxy.entity) {
        if (removeFromScene && scene_ != nullptr) {
            scene_->remove(proxy.entity);
        }
        engine_->getRenderableManager().destroy(proxy.entity);
        engine_->getTransformManager().destroy(proxy.entity);
    }
    if (proxy.materialInstance) {
        engine_->destroy(proxy.materialInstance);
        proxy.materialInstance = nullptr;
    }
    if (proxy.vertexBuffer) {
        engine_->destroy(proxy.vertexBuffer);
        proxy.vertexBuffer = nullptr;
    }
    if (proxy.indexBuffer) {
        engine_->destroy(proxy.indexBuffer);
        proxy.indexBuffer = nullptr;
    }
    if (proxy.entity) {
        utils::EntityManager::get().destroy(proxy.entity);
        proxy.entity.clear();
    }
    proxy.vertexCount = 0u;
    proxy.indexCount = 0u;
}

void SceneWireframeOverlayCache::destroyTransformRoot(Entry& entry) {
    if (!engine_ || !entry.transformRoot) {
        return;
    }
    engine_->getTransformManager().destroy(entry.transformRoot);
    utils::EntityManager::get().destroy(entry.transformRoot);
    entry.transformRoot.clear();
}

void SceneWireframeOverlayCache::setProxyLayerMask(const Proxy& proxy, std::uint8_t layerMask) const {
    if (!engine_ || !proxy.entity) {
        return;
    }
    filament::RenderableManager& renderables = engine_->getRenderableManager();
    const filament::RenderableManager::Instance instance = renderables.getInstance(proxy.entity);
    if (!instance) {
        return;
    }
    renderables.setLayerMask(instance, kViewportAllLayerMask, layerMask);
}

void SceneWireframeOverlayCache::setLayerMask(std::uint8_t layerMask) {
    if (layerMask_ == layerMask) {
        return;
    }
    layerMask_ = layerMask;
    for (auto& [entityId, entry] : entries_) {
        (void)entityId;
        setProxyLayerMask(entry.lineProxy, layerMask_);
    }
}

void SceneWireframeOverlayCache::updateEntryTransform(
        const Entry& entry,
        const SceneDocument& document,
        EntityId entityId) const {
    if (!engine_ || !entry.transformRoot) {
        return;
    }
    filament::TransformManager& transforms = engine_->getTransformManager();
    filament::TransformManager::Instance instance = transforms.getInstance(entry.transformRoot);
    if (!instance) {
        transforms.create(entry.transformRoot);
        instance = transforms.getInstance(entry.transformRoot);
    }
    transforms.setTransform(instance, makeTransform(document.worldTransformMatrix(entityId)));
}

void SceneWireframeOverlayCache::updateEntryTransforms(const SceneDocument& document) {
    QDR_PROFILE_FUNCTION();
    if (!engine_) {
        return;
    }
    filament::TransformManager& transforms = engine_->getTransformManager();
    transforms.openLocalTransformTransaction();
    for (const WireframeEntityState& state : syncedStates_) {
        const auto existing = entries_.find(state.entity);
        if (existing != entries_.end()) {
            updateEntryTransform(existing->second, document, state.entity);
        }
    }
    transforms.commitLocalTransformTransaction();
}

void SceneWireframeOverlayCache::removeExcept(const std::unordered_set<EntityId>& liveEntities) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (!liveEntities.contains(it->first)) {
            destroyEntry(it->second);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace quader
