////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/selected_object_overlay_cache.hpp"

#include "assets/asset_registry.hpp"
#include "core/color_space.hpp"
#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "mesh/polygon/document.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/render_layers.hpp"
#include "render/overlay/overlay_mesh_types.hpp"
#include "render/overlay/overlay_renderable_builder.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "scene/scene_document.hpp"

#include <backend/DriverEnums.h>
#include <filament/Engine.h>
#include <filament/Box.h>
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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr float kSelectedObjectLineNearPlane = 0.05f;

/**
 * Stores one shader-expanded selected object line vertex.
 */
struct SelectedObjectLineVertex {
    OverlayFloat4 position;
    OverlayFloat4 start;
    OverlayFloat4 end;
    OverlayFloat4 control;
    std::uint32_t color = 0u;
};

/**
 * Stores one fallback triangle edge collected from a mesh asset.
 */
struct TriangleEdgeRecord {
    std::uint32_t a = 0u;
    std::uint32_t b = 0u;
    std::uint32_t count = 0u;
    QVec3 firstNormal;
    bool coplanarWithFirst = true;
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

void appendSelectedLineQuad(
        std::vector<SelectedObjectLineVertex>& vertices,
        std::vector<std::uint32_t>& indices,
        QVec3 start,
        QVec3 end,
        std::uint32_t colorRgba = kOverlaySelectionWireColor) {
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

filament::Box boundsForSelectedLineVertices(const std::vector<SelectedObjectLineVertex>& vertices) {
    if (vertices.empty()) {
        return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    }

    QVec3 minValue{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
    };
    QVec3 maxValue{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
    };
    auto include = [&](const OverlayFloat4& point) {
        minValue.x = std::min(minValue.x, point.x);
        minValue.y = std::min(minValue.y, point.y);
        minValue.z = std::min(minValue.z, point.z);
        maxValue.x = std::max(maxValue.x, point.x);
        maxValue.y = std::max(maxValue.y, point.y);
        maxValue.z = std::max(maxValue.z, point.z);
    };

    for (const SelectedObjectLineVertex& vertex : vertices) {
        include(vertex.start);
        include(vertex.end);
    }
    if (minValue.x == maxValue.x) {
        minValue.x -= 0.5f;
        maxValue.x += 0.5f;
    }
    if (minValue.y == maxValue.y) {
        minValue.y -= 0.5f;
        maxValue.y += 0.5f;
    }
    if (minValue.z == maxValue.z) {
        minValue.z -= 0.5f;
        maxValue.z += 0.5f;
    }

    filament::Box box;
    box.set(toFilament(minValue), toFilament(maxValue));
    return box;
}

std::vector<OverlayFaceFillVertex> selectedFaceVertices(const MeshAsset& mesh) {
    std::vector<OverlayFaceFillVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    const std::uint32_t color = rgba8SrgbToLinear(kOverlaySelectionFaceColor);
    for (const MeshVertex& vertex : mesh.vertices) {
        vertices.push_back({{vertex.position.x, vertex.position.y, vertex.position.z}, {}, color});
    }
    return vertices;
}

std::vector<std::uint32_t> validTriangleIndices(const MeshAsset& mesh) {
    std::vector<std::uint32_t> indices;
    indices.reserve(mesh.indices.size());
    for (std::size_t index = 0u; index + 2u < mesh.indices.size(); index += 3u) {
        const std::uint32_t a = mesh.indices[index];
        const std::uint32_t b = mesh.indices[index + 1u];
        const std::uint32_t c = mesh.indices[index + 2u];
        if (a < mesh.vertices.size() && b < mesh.vertices.size() && c < mesh.vertices.size()) {
            indices.push_back(a);
            indices.push_back(b);
            indices.push_back(c);
        }
    }
    return indices;
}

template <typename Vertex, typename ConfigureAttributes, typename ConfigureMaterial>
SelectedObjectOverlayCache::Proxy createProxy(
        filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& material,
        filament::TransformManager::Instance parentTransform,
        const std::vector<Vertex>& vertices,
        const std::vector<std::uint32_t>& indices,
        const filament::Box& bounds,
        const OverlayRenderState& state,
        std::uint8_t layerMask,
        ConfigureAttributes configureAttributes,
        ConfigureMaterial configureMaterial) {
    SelectedObjectOverlayCache::Proxy proxy;
    if (vertices.empty() || indices.empty()) {
        return proxy;
    }

    proxy.entity = utils::EntityManager::get().create();
    proxy.vertexCount = static_cast<std::uint32_t>(vertices.size());
    proxy.indexCount = static_cast<std::uint32_t>(indices.size());
    proxy.materialInstance = material.createInstance();
    configureMaterial(*proxy.materialInstance);

    filament::VertexBuffer::Builder vertexBuilder;
    vertexBuilder
            .vertexCount(proxy.vertexCount)
            .bufferCount(1);
    configureAttributes(vertexBuilder);
    proxy.vertexBuffer = vertexBuilder.build(engine);
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
            .boundingBox(bounds)
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .build(engine, proxy.entity);
    engine.getTransformManager().create(proxy.entity, parentTransform);
    scene.addEntity(proxy.entity);
    return proxy;
}

} // namespace

bool selectedObjectFaceFillsEnabledForSelectionCount(std::size_t selectedMeshCount) {
    return selectedMeshCount > 0u && selectedMeshCount <= kSelectedObjectFaceFillSelectionLimit;
}

SelectedObjectOverlayCache::Proxy::Proxy(Proxy&& other) noexcept
    : entity(other.entity)
    , vertexBuffer(std::exchange(other.vertexBuffer, nullptr))
    , indexBuffer(std::exchange(other.indexBuffer, nullptr))
    , materialInstance(std::exchange(other.materialInstance, nullptr))
    , vertexCount(std::exchange(other.vertexCount, 0u))
    , indexCount(std::exchange(other.indexCount, 0u)) {
    other.entity.clear();
}

SelectedObjectOverlayCache::Proxy& SelectedObjectOverlayCache::Proxy::operator=(Proxy&& other) noexcept {
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

SelectedObjectOverlayCache::~SelectedObjectOverlayCache() {
    shutdown();
}

void SelectedObjectOverlayCache::initialize(
        filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& faceFillMaterial,
        filament::Material& lineMaterial) {
    engine_ = &engine;
    scene_ = &scene;
    faceFillMaterial_ = &faceFillMaterial;
    lineMaterial_ = &lineMaterial;
}

void SelectedObjectOverlayCache::shutdown() {
    if (!engine_) {
        return;
    }

    destroyProxy(lineProxy_, true);
    destroyLineBatchRoot();
    destroyFaceEntries();
    lineEntries_.clear();
    syncedStates_.clear();
    syncedSceneRevision_ = 0;
    syncedAssetRevision_ = 0;
    syncedSelectionRevision_ = 0;
    syncedVisibilityRevision_ = 0;
    syncedTransformRevision_ = 0;
    syncedMeshBindingRevision_ = 0;
    syncedFaceFillsEnabled_ = false;
    hasSynced_ = false;
    engine_ = nullptr;
    scene_ = nullptr;
    faceFillMaterial_ = nullptr;
    lineMaterial_ = nullptr;
    visible_ = true;
}

void SelectedObjectOverlayCache::sync(
        const SceneDocument& document,
        const AssetRegistry& assets,
        bool visible) {
    QDR_PROFILE_FUNCTION();
    if (!initialized()) {
        return;
    }

    if (!visible) {
        setVisible(false);
        return;
    }
    setVisible(true);

    const std::uint64_t sceneRevision = document.revision();
    const std::uint64_t assetRevision = assets.revision();
    const std::uint64_t selectionRevision = document.selectionRevision();
    const std::uint64_t visibilityRevision = document.visibilityRevision();
    const std::uint64_t transformRevision = document.transformRevision();
    const std::uint64_t meshBindingRevision = document.meshBindingRevision();
    if (hasSynced_ &&
            syncedAssetRevision_ == assetRevision &&
            syncedSelectionRevision_ == selectionRevision &&
            syncedVisibilityRevision_ == visibilityRevision &&
            syncedMeshBindingRevision_ == meshBindingRevision) {
        if (syncedTransformRevision_ != transformRevision) {
            updateLineProxy(syncedStates_, document);
            if (syncedFaceFillsEnabled_) {
                updateEntryTransforms(document);
            }
            syncedTransformRevision_ = transformRevision;
        }
        syncedSceneRevision_ = sceneRevision;
        return;
    }

    const std::vector<SelectedEntityState> states = selectedEntityStates(document, assets);
    const bool faceFillsEnabled = selectedObjectFaceFillsEnabledForSelectionCount(states.size());
    if (hasSynced_ && syncedFaceFillsEnabled_ == faceFillsEnabled && states == syncedStates_) {
        if (syncedTransformRevision_ != transformRevision) {
            updateLineProxy(syncedStates_, document);
            if (syncedFaceFillsEnabled_) {
                updateEntryTransforms(document);
            }
        }
        syncedSceneRevision_ = sceneRevision;
        syncedAssetRevision_ = assetRevision;
        syncedSelectionRevision_ = selectionRevision;
        syncedVisibilityRevision_ = visibilityRevision;
        syncedTransformRevision_ = transformRevision;
        syncedMeshBindingRevision_ = meshBindingRevision;
        return;
    }

    syncLineEntries(states, assets);
    updateLineProxy(states, document);

    if (faceFillsEnabled) {
        std::unordered_set<EntityId> liveEntities;
        liveEntities.reserve(states.size());
        for (const SelectedEntityState& state : states) {
            QDR_PROFILE_SCOPE("Selected object overlay sync entity");
            liveEntities.insert(state.entity);
            auto existing = entries_.find(state.entity);
            const bool rebuild = existing == entries_.end() ||
                    existing->second.meshId != state.meshId ||
                    existing->second.meshRevision != state.meshRevision;
            if (rebuild) {
                if (existing != entries_.end()) {
                    destroyEntry(existing->second);
                    entries_.erase(existing);
                }
                Entry entry = createEntry(state.entity, state.meshId, document, assets);
                existing = entries_.emplace(state.entity, std::move(entry)).first;
            }
            updateEntryTransform(existing->second, document, state.entity);
        }
        removeExcept(liveEntities);
    } else {
        destroyFaceEntries();
    }

    syncedStates_ = states;
    syncedSceneRevision_ = sceneRevision;
    syncedAssetRevision_ = assetRevision;
    syncedSelectionRevision_ = selectionRevision;
    syncedVisibilityRevision_ = visibilityRevision;
    syncedTransformRevision_ = transformRevision;
    syncedMeshBindingRevision_ = meshBindingRevision;
    syncedFaceFillsEnabled_ = faceFillsEnabled;
    hasSynced_ = true;
}

std::vector<SelectedObjectOverlayCache::SelectedEntityState> SelectedObjectOverlayCache::selectedEntityStates(
        const SceneDocument& document,
        const AssetRegistry& assets) const {
    QDR_PROFILE_FUNCTION();
    std::vector<SelectedEntityState> states;
    const std::vector<EntityId> selected = document.selectedEntities();
    states.reserve(selected.size());
    for (const EntityId entityId : selected) {
        if (!document.isEffectivelyVisible(entityId)) {
            continue;
        }
        const MeshComponent* meshComponent = document.mesh(entityId);
        if (meshComponent == nullptr || meshComponent->mesh == InvalidAssetId) {
            continue;
        }
        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) {
            continue;
        }
        states.push_back({
                .entity = entityId,
                .meshId = meshComponent->mesh,
                .meshRevision = mesh->revision.value,
        });
    }
    return states;
}

SelectedObjectOverlayCache::LineEntry SelectedObjectOverlayCache::createLineEntry(
        AssetId meshId,
        const AssetRegistry& assets) const {
    QDR_PROFILE_FUNCTION();
    LineEntry entry;
    entry.meshId = meshId;
    const MeshAsset* mesh = assets.mesh(meshId);
    if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) {
        return entry;
    }
    entry.meshRevision = mesh->revision.value;

    if (mesh->hasAuthoredPolygonDocument && mesh->authoredPolygonDocument) {
        std::unordered_map<quader_poly::ElementId, QVec3> positions;
        positions.reserve(mesh->authoredPolygonDocument->vertices.size());
        for (const quader_poly::Vertex& vertex : mesh->authoredPolygonDocument->vertices) {
            positions.emplace(vertex.id, vertex.position);
        }

        for (const quader_poly::Edge edge : quader_poly::document_edges(*mesh->authoredPolygonDocument)) {
            const auto a = positions.find(edge.a);
            const auto b = positions.find(edge.b);
            if (a == positions.end() || b == positions.end()) {
                continue;
            }
            entry.segments.push_back({.start = a->second, .end = b->second});
        }
    }

    if (!entry.segments.empty()) {
        return entry;
    }

    std::unordered_map<std::uint64_t, TriangleEdgeRecord> edges;
    edges.reserve(mesh->indices.size());

    auto appendEdge = [&](std::uint32_t a, std::uint32_t b, QVec3 normal) {
        if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || a == b) {
            return;
        }
        TriangleEdgeRecord& record = edges[edgeKey(a, b)];
        if (record.count == 0u) {
            record.a = a;
            record.b = b;
            record.firstNormal = normal;
        } else if (length(record.firstNormal) > 0.000001f && length(normal) > 0.000001f) {
            record.coplanarWithFirst =
                    record.coplanarWithFirst &&
                    std::abs(dot(normalize(record.firstNormal), normalize(normal))) > 0.999f;
        }
        ++record.count;
    };

    for (std::size_t index = 0u; index + 2u < mesh->indices.size(); index += 3u) {
        const std::uint32_t a = mesh->indices[index];
        const std::uint32_t b = mesh->indices[index + 1u];
        const std::uint32_t c = mesh->indices[index + 2u];
        if (a >= mesh->vertices.size() || b >= mesh->vertices.size() || c >= mesh->vertices.size()) {
            continue;
        }
        QVec3 normal = cross(
                mesh->vertices[b].position - mesh->vertices[a].position,
                mesh->vertices[c].position - mesh->vertices[a].position);
        if (length(normal) > 0.000001f) {
            normal = normalize(normal);
        }
        appendEdge(a, b, normal);
        appendEdge(b, c, normal);
        appendEdge(c, a, normal);
    }

    entry.segments.reserve(edges.size());
    for (const auto& [key, edge] : edges) {
        (void)key;
        if (edge.count == 2u && edge.coplanarWithFirst) {
            continue;
        }
        entry.segments.push_back({
                .start = mesh->vertices[edge.a].position,
                .end = mesh->vertices[edge.b].position,
        });
    }
    return entry;
}

void SelectedObjectOverlayCache::syncLineEntries(
        const std::vector<SelectedEntityState>& states,
        const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    std::unordered_set<EntityId> liveEntities;
    liveEntities.reserve(states.size());
    for (const SelectedEntityState& state : states) {
        QDR_PROFILE_SCOPE("Selected object overlay sync line entity");
        liveEntities.insert(state.entity);
        const auto existing = lineEntries_.find(state.entity);
        if (existing == lineEntries_.end() ||
                existing->second.meshId != state.meshId ||
                existing->second.meshRevision != state.meshRevision) {
            lineEntries_[state.entity] = createLineEntry(state.meshId, assets);
        }
    }

    for (auto it = lineEntries_.begin(); it != lineEntries_.end();) {
        if (!liveEntities.contains(it->first)) {
            it = lineEntries_.erase(it);
        } else {
            ++it;
        }
    }
}

void SelectedObjectOverlayCache::updateLineProxy(
        const std::vector<SelectedEntityState>& states,
        const SceneDocument& document) {
    QDR_PROFILE_FUNCTION();
    if (states.empty()) {
        destroyProxy(lineProxy_, true);
        destroyLineBatchRoot();
        return;
    }

    std::size_t segmentCount = 0u;
    for (const SelectedEntityState& state : states) {
        const auto existing = lineEntries_.find(state.entity);
        if (existing != lineEntries_.end()) {
            segmentCount += existing->second.segments.size();
        }
    }

    std::vector<SelectedObjectLineVertex> lineVertices;
    std::vector<std::uint32_t> lineIndices;
    lineVertices.reserve(segmentCount * 4u);
    lineIndices.reserve(segmentCount * 6u);
    for (const SelectedEntityState& state : states) {
        QDR_PROFILE_SCOPE("Selected object overlay transform batched wire entity");
        const auto existing = lineEntries_.find(state.entity);
        if (existing == lineEntries_.end()) {
            continue;
        }
        const QMat4 worldFromLocal = document.worldTransformMatrix(state.entity);
        for (const SelectedObjectLineSegment& segment : existing->second.segments) {
            appendSelectedLineQuad(
                    lineVertices,
                    lineIndices,
                    transformPoint(worldFromLocal, segment.start),
                    transformPoint(worldFromLocal, segment.end));
        }
    }
    if (lineVertices.empty() || lineIndices.empty()) {
        destroyProxy(lineProxy_, true);
        destroyLineBatchRoot();
        return;
    }

    if (!lineBatchTransformRoot_) {
        lineBatchTransformRoot_ = utils::EntityManager::get().create();
        engine_->getTransformManager().create(lineBatchTransformRoot_);
    }
    const filament::TransformManager::Instance parentTransform =
            engine_->getTransformManager().getInstance(lineBatchTransformRoot_);
    const std::uint16_t lineBlendOrder =
            static_cast<std::uint16_t>(viewportOverlayDrawOrder(ViewportOverlayLayer::SelectedEdge) * 64);
    const OverlayRenderState lineState = OverlayRenderableBuilder::makeState(
            ViewportOverlayLayer::SelectedEdge,
            ViewportOverlayGeometryKind::Lines,
            lineBlendOrder);
    const filament::Box bounds = boundsForSelectedLineVertices(lineVertices);
    const std::uint32_t vertexCount = static_cast<std::uint32_t>(lineVertices.size());
    const std::uint32_t indexCount = static_cast<std::uint32_t>(lineIndices.size());
    if (lineProxy_.entity &&
            lineProxy_.vertexBuffer != nullptr &&
            lineProxy_.indexBuffer != nullptr &&
            lineProxy_.vertexCount == vertexCount &&
            lineProxy_.indexCount == indexCount) {
        lineProxy_.vertexBuffer->setBufferAt(*engine_, 0, FilamentBufferDescriptorFactory::vector(lineVertices));
        filament::RenderableManager& renderables = engine_->getRenderableManager();
        const filament::RenderableManager::Instance instance = renderables.getInstance(lineProxy_.entity);
        if (instance) {
            renderables.setAxisAlignedBoundingBox(instance, bounds);
        }
        setProxyLayerMask(lineProxy_, visible_ ? kViewportOverlayLayerMask : 0u);
        return;
    }

    destroyProxy(lineProxy_, true);
    lineProxy_ = createProxy(
            *engine_,
            *scene_,
            *lineMaterial_,
            parentTransform,
            lineVertices,
            lineIndices,
            bounds,
            lineState,
            visible_ ? kViewportOverlayLayerMask : 0u,
            [](filament::VertexBuffer::Builder& builder) {
                builder
                        .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT4, offsetof(SelectedObjectLineVertex, position), sizeof(SelectedObjectLineVertex))
                        .attribute(filament::VertexAttribute::CUSTOM0, 0, AttributeType::FLOAT4, offsetof(SelectedObjectLineVertex, start), sizeof(SelectedObjectLineVertex))
                        .attribute(filament::VertexAttribute::CUSTOM1, 0, AttributeType::FLOAT4, offsetof(SelectedObjectLineVertex, end), sizeof(SelectedObjectLineVertex))
                        .attribute(filament::VertexAttribute::CUSTOM2, 0, AttributeType::FLOAT4, offsetof(SelectedObjectLineVertex, control), sizeof(SelectedObjectLineVertex))
                        .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(SelectedObjectLineVertex, color), sizeof(SelectedObjectLineVertex))
                        .normalized(filament::VertexAttribute::COLOR);
            },
            [&lineState](filament::MaterialInstance& material) {
                configureOverlayMaterial(material, lineState);
                material.setParameter("lineWidth", kOverlaySelectionWireLineWidthPixels);
                material.setParameter("featherPixels", kOverlayLineFeatherPixels);
                material.setParameter("nearPlane", kSelectedObjectLineNearPlane);
            });
}

SelectedObjectOverlayCache::Entry SelectedObjectOverlayCache::createEntry(
        EntityId entityId,
        AssetId meshId,
        const SceneDocument& document,
        const AssetRegistry& assets) {
    QDR_PROFILE_FUNCTION();
    Entry entry;
    entry.meshId = meshId;
    const MeshAsset* mesh = assets.mesh(meshId);
    if (mesh == nullptr || mesh->vertices.empty() || mesh->indices.empty()) {
        return entry;
    }
    entry.meshRevision = mesh->revision.value;
    entry.transformRoot = utils::EntityManager::get().create();
    filament::TransformManager& transforms = engine_->getTransformManager();
    transforms.create(entry.transformRoot);
    const filament::TransformManager::Instance parentTransform = transforms.getInstance(entry.transformRoot);

    const std::uint8_t layerMask = visible_ ? kViewportOverlayLayerMask : 0u;
    std::vector<OverlayFaceFillVertex> faceVertices = selectedFaceVertices(*mesh);
    std::vector<std::uint32_t> faceIndices = validTriangleIndices(*mesh);
    if (!faceVertices.empty() && !faceIndices.empty()) {
        const filament::Box bounds = computeBounds(*mesh);
        for (const ViewportOverlayLayer layer : {
                     ViewportOverlayLayer::SelectedXrayFace,
                     ViewportOverlayLayer::SelectedFaceMask,
             }) {
            std::uint16_t blendOrder =
                    static_cast<std::uint16_t>(viewportOverlayDrawOrder(layer) * 64);
            const OverlayRenderState depthState = OverlayRenderableBuilder::makeState(
                    layer,
                    ViewportOverlayGeometryKind::Triangles,
                    blendOrder++,
                    true,
                    false);
            entry.faceProxies.push_back(createProxy(
                    *engine_,
                    *scene_,
                    *faceFillMaterial_,
                    parentTransform,
                    faceVertices,
                    faceIndices,
                    bounds,
                    depthState,
                    layerMask,
                    [](filament::VertexBuffer::Builder& builder) {
                        builder
                                .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(OverlayFaceFillVertex, position), sizeof(OverlayFaceFillVertex))
                                .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(OverlayFaceFillVertex, uv), sizeof(OverlayFaceFillVertex))
                                .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayFaceFillVertex, color), sizeof(OverlayFaceFillVertex))
                                .normalized(filament::VertexAttribute::COLOR);
                    },
                    [&depthState](filament::MaterialInstance& material) {
                        configureOverlayMaterial(material, depthState);
                    }));

            const OverlayRenderState colorState = OverlayRenderableBuilder::makeState(
                    layer,
                    ViewportOverlayGeometryKind::Triangles,
                    blendOrder++,
                    false,
                    true);
            entry.faceProxies.push_back(createProxy(
                    *engine_,
                    *scene_,
                    *faceFillMaterial_,
                    parentTransform,
                    faceVertices,
                    faceIndices,
                    bounds,
                    colorState,
                    layerMask,
                    [](filament::VertexBuffer::Builder& builder) {
                        builder
                                .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(OverlayFaceFillVertex, position), sizeof(OverlayFaceFillVertex))
                                .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(OverlayFaceFillVertex, uv), sizeof(OverlayFaceFillVertex))
                                .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayFaceFillVertex, color), sizeof(OverlayFaceFillVertex))
                                .normalized(filament::VertexAttribute::COLOR);
                    },
                    [&colorState](filament::MaterialInstance& material) {
                        configureOverlayMaterial(material, colorState);
                    }));
        }
    }

    updateEntryTransform(entry, document, entityId);
    return entry;
}

void SelectedObjectOverlayCache::destroyEntry(Entry& entry) {
    for (Proxy& proxy : entry.faceProxies) {
        destroyProxy(proxy, true);
    }
    entry.faceProxies.clear();
    destroyTransformRoot(entry);
}

void SelectedObjectOverlayCache::destroyProxy(Proxy& proxy, bool removeFromScene) {
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

void SelectedObjectOverlayCache::destroyLineBatchRoot() {
    if (!engine_ || !lineBatchTransformRoot_) {
        return;
    }
    engine_->getTransformManager().destroy(lineBatchTransformRoot_);
    utils::EntityManager::get().destroy(lineBatchTransformRoot_);
    lineBatchTransformRoot_.clear();
}

void SelectedObjectOverlayCache::destroyFaceEntries() {
    for (auto& [entityId, entry] : entries_) {
        (void)entityId;
        destroyEntry(entry);
    }
    entries_.clear();
}

void SelectedObjectOverlayCache::destroyTransformRoot(Entry& entry) {
    if (!engine_ || !entry.transformRoot) {
        return;
    }
    engine_->getTransformManager().destroy(entry.transformRoot);
    utils::EntityManager::get().destroy(entry.transformRoot);
    entry.transformRoot.clear();
}

void SelectedObjectOverlayCache::setProxyLayerMask(const Proxy& proxy, std::uint8_t layerMask) const {
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

void SelectedObjectOverlayCache::updateEntryTransform(
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

void SelectedObjectOverlayCache::updateEntryTransforms(const SceneDocument& document) {
    QDR_PROFILE_FUNCTION();
    if (!engine_) {
        return;
    }
    filament::TransformManager& transforms = engine_->getTransformManager();
    transforms.openLocalTransformTransaction();
    for (const SelectedEntityState& state : syncedStates_) {
        const auto existing = entries_.find(state.entity);
        if (existing != entries_.end()) {
            updateEntryTransform(existing->second, document, state.entity);
        }
    }
    transforms.commitLocalTransformTransaction();
}

void SelectedObjectOverlayCache::setVisible(bool visible) {
    if (visible_ == visible) {
        return;
    }
    visible_ = visible;
    const std::uint8_t layerMask = visible_ ? kViewportOverlayLayerMask : 0u;
    setProxyLayerMask(lineProxy_, layerMask);
    for (auto& [entityId, entry] : entries_) {
        (void)entityId;
        for (const Proxy& proxy : entry.faceProxies) {
            setProxyLayerMask(proxy, layerMask);
        }
    }
}

void SelectedObjectOverlayCache::removeExcept(const std::unordered_set<EntityId>& liveEntities) {
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
