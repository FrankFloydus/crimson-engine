////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_proxy_factory.hpp"

#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/render_layers.hpp"
#include "render/viewport_overlay_policy.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/VertexBuffer.h>
#include <utils/Entity.h>
#include <utils/EntityManager.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

std::uint32_t toOverlayEntityId(utils::Entity entity) {
    return static_cast<std::uint32_t>(utils::Entity::smuggle(entity));
}

utils::Entity toFilamentEntity(std::uint32_t entityId) {
    return utils::Entity::import(static_cast<std::int32_t>(entityId));
}

filament::Box overlayMeshBounds() {
    filament::Box box;
    const filament::math::float3 halfExtent{500000.0f, 500000.0f, 500000.0f};
    box.set(-halfExtent, halfExtent);
    return box;
}

void destroyOverlayProxyResources(
        filament::Engine& engine,
        filament::Scene* scene,
        OverlayRenderableProxy& proxy,
        bool removeFromScene);

/**
 * Cleans up partially constructed overlay resources until ownership is handed to the proxy.
 */
class OverlayProxyCreationGuard {
public:
    OverlayProxyCreationGuard(filament::Engine& engine, filament::Scene& scene, OverlayRenderableProxy& proxy)
        : engine_(engine)
        , scene_(scene)
        , proxy_(proxy) {}

    OverlayProxyCreationGuard(const OverlayProxyCreationGuard&) = delete;
    OverlayProxyCreationGuard& operator=(const OverlayProxyCreationGuard&) = delete;
    OverlayProxyCreationGuard(OverlayProxyCreationGuard&&) = delete;
    OverlayProxyCreationGuard& operator=(OverlayProxyCreationGuard&&) = delete;

    ~OverlayProxyCreationGuard() {
        if (active_) {
            destroyOverlayProxyResources(engine_, &scene_, proxy_, sceneAdded_);
        }
    }

    void markSceneAdded() {
        sceneAdded_ = true;
    }

    void release() {
        active_ = false;
    }

private:
    filament::Engine& engine_;
    filament::Scene& scene_;
    OverlayRenderableProxy& proxy_;
    bool sceneAdded_ = false;
    bool active_ = true;
};

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

void destroyOverlayProxyResources(
        filament::Engine& engine,
        filament::Scene* scene,
        OverlayRenderableProxy& proxy,
        bool removeFromScene) {
    if (proxy.entityId != 0u) {
        const utils::Entity entity = toFilamentEntity(proxy.entityId);
        if (removeFromScene && scene != nullptr) {
            scene->remove(entity);
        }
        engine.getRenderableManager().destroy(entity);
    }
    if (proxy.materialInstance) {
        engine.destroy(proxy.materialInstance);
        proxy.materialInstance = nullptr;
    }
    if (proxy.vertexBuffer) {
        engine.destroy(proxy.vertexBuffer);
        proxy.vertexBuffer = nullptr;
    }
    if (proxy.indexBuffer) {
        engine.destroy(proxy.indexBuffer);
        proxy.indexBuffer = nullptr;
    }
    if (proxy.entityId != 0u) {
        utils::EntityManager::get().destroy(toFilamentEntity(proxy.entityId));
        proxy.entityId = 0u;
    }
}

template <typename Vertex, typename ConfigureAttributes, typename ConfigureMaterial>
void appendOverlayProxy(
        filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& material,
        const std::vector<Vertex>& vertices,
        const std::vector<std::uint32_t>& indices,
        OverlayRenderableProxyKind kind,
        const OverlayRenderState& state,
        std::uint8_t layerMask,
        std::vector<OverlayRenderableProxy>& proxies,
        ConfigureAttributes configureAttributes,
        ConfigureMaterial configureMaterial) {
    if (vertices.empty() || indices.empty()) {
        return;
    }

    OverlayRenderableProxy proxy;
    proxy.kind = kind;
    proxy.vertexCount = static_cast<std::uint32_t>(vertices.size());
    proxy.indexCount = static_cast<std::uint32_t>(indices.size());
    const utils::Entity entity = utils::EntityManager::get().create();
    proxy.entityId = toOverlayEntityId(entity);
    OverlayProxyCreationGuard creationGuard(engine, scene, proxy);
    proxy.materialInstance = material.createInstance();
    configureMaterial(*proxy.materialInstance);

    filament::VertexBuffer::Builder vertexBuilder;
    vertexBuilder
            .vertexCount(static_cast<std::uint32_t>(vertices.size()))
            .bufferCount(1);
    configureAttributes(vertexBuilder);
    proxy.vertexBuffer = vertexBuilder.build(engine);
    proxy.vertexBuffer->setBufferAt(engine, 0, FilamentBufferDescriptorFactory::vector(vertices));

    proxy.indexBuffer = filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(engine);
    proxy.indexBuffer->setBuffer(engine, FilamentBufferDescriptorFactory::vector(indices));

    filament::RenderableManager::Builder(1)
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, proxy.vertexBuffer, proxy.indexBuffer)
            .material(0, proxy.materialInstance)
            .blendOrder(0, state.blendOrder)
            .globalBlendOrderEnabled(0, true)
            .layerMask(kViewportAllLayerMask, layerMask)
            .boundingBox(overlayMeshBounds())
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .build(engine, entity);
    scene.addEntity(entity);
    creationGuard.markSceneAdded();
    proxies.push_back(std::move(proxy));
    creationGuard.release();
}

template <typename Vertex>
bool proxyCanUpdateMesh(
        const OverlayRenderableProxy& proxy,
        OverlayRenderableProxyKind kind,
        const OverlayMesh<Vertex>& mesh) {
    return !mesh.empty() &&
            proxy.kind == kind &&
            proxy.vertexBuffer != nullptr &&
            proxy.indexBuffer != nullptr &&
            proxy.materialInstance != nullptr &&
            proxy.vertexCount == mesh.vertices.size() &&
            proxy.indexCount == mesh.indices.size();
}

void updateProxyLayerMask(
        filament::Engine& engine,
        OverlayRenderableProxy& proxy,
        std::uint8_t layerMask) {
    if (proxy.entityId == 0u) {
        return;
    }
    filament::RenderableManager& renderables = engine.getRenderableManager();
    const filament::RenderableManager::Instance instance =
            renderables.getInstance(toFilamentEntity(proxy.entityId));
    if (!instance) {
        return;
    }
    renderables.setLayerMask(instance, kViewportAllLayerMask, layerMask);
}

template <typename Vertex>
void updateProxyBuffers(
        filament::Engine& engine,
        OverlayRenderableProxy& proxy,
        const OverlayMesh<Vertex>& mesh,
        std::uint8_t layerMask) {
    proxy.vertexBuffer->setBufferAt(engine, 0, FilamentBufferDescriptorFactory::vector(mesh.vertices));
    proxy.indexBuffer->setBuffer(engine, FilamentBufferDescriptorFactory::vector(mesh.indices));
    updateProxyLayerMask(engine, proxy, layerMask);
}

} // namespace

void OverlayProxyFactory::initialize(filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& overlayMaterial,
        filament::Material& overlayLineMaterial,
        filament::Material& overlayPointMaterial) {
    engine_ = &engine;
    scene_ = &scene;
    overlayMaterial_ = &overlayMaterial;
    overlayLineMaterial_ = &overlayLineMaterial;
    overlayPointMaterial_ = &overlayPointMaterial;
}

void OverlayProxyFactory::shutdown() {
    engine_ = nullptr;
    scene_ = nullptr;
    overlayMaterial_ = nullptr;
    overlayLineMaterial_ = nullptr;
    overlayPointMaterial_ = nullptr;
}

std::vector<OverlayRenderableProxy> OverlayProxyFactory::createLayerProxies(
        const OverlayLayerBuildPlan& plan,
        std::uint8_t layerMask) {
    std::vector<OverlayRenderableProxy> proxies;
    if (!initialized()) {
        return proxies;
    }

    proxies.reserve(plannedProxyCount(plan));
    for (const OverlayFaceFillBatch& batch : plan.faceFillBatches) {
        createTriangleProxy(batch, layerMask, proxies);
    }
    for (const OverlayLineBatch& batch : plan.lineBatches) {
        createLineProxy(batch, layerMask, proxies);
    }
    for (const OverlayPointBatch& batch : plan.pointBatches) {
        createPointProxy(batch, layerMask, proxies);
    }
    return proxies;
}

bool OverlayProxyFactory::updateLayerProxies(
        std::vector<OverlayRenderableProxy>& proxies,
        const OverlayLayerBuildPlan& plan,
        std::uint8_t layerMask) {
    if (!initialized() || proxies.size() != plannedProxyCount(plan)) {
        return false;
    }

    std::size_t index = 0;
    for (const OverlayFaceFillBatch& batch : plan.faceFillBatches) {
        if (batch.mesh.empty()) {
            continue;
        }
        if (index >= proxies.size() ||
                !proxyCanUpdateMesh(proxies[index++], OverlayRenderableProxyKind::Triangle, batch.mesh)) {
            return false;
        }
    }
    for (const OverlayLineBatch& batch : plan.lineBatches) {
        if (batch.mesh.empty()) {
            continue;
        }
        if (index >= proxies.size() ||
                !proxyCanUpdateMesh(proxies[index++], OverlayRenderableProxyKind::Line, batch.mesh)) {
            return false;
        }
    }
    for (const OverlayPointBatch& batch : plan.pointBatches) {
        if (batch.mesh.empty()) {
            continue;
        }
        if (index >= proxies.size() ||
                !proxyCanUpdateMesh(proxies[index++], OverlayRenderableProxyKind::Point, batch.mesh)) {
            return false;
        }
    }

    index = 0;
    for (const OverlayFaceFillBatch& batch : plan.faceFillBatches) {
        if (!batch.mesh.empty()) {
            updateTriangleProxy(proxies[index++], batch, layerMask);
        }
    }
    for (const OverlayLineBatch& batch : plan.lineBatches) {
        if (!batch.mesh.empty()) {
            updateLineProxy(proxies[index++], batch, layerMask);
        }
    }
    for (const OverlayPointBatch& batch : plan.pointBatches) {
        if (!batch.mesh.empty()) {
            updatePointProxy(proxies[index++], batch, layerMask);
        }
    }
    return true;
}

void OverlayProxyFactory::destroy(OverlayRenderableProxy& proxy) {
    if (!engine_) {
        return;
    }

    destroyOverlayProxyResources(*engine_, scene_, proxy, true);
}

std::size_t OverlayProxyFactory::plannedProxyCount(const OverlayLayerBuildPlan& plan) {
    std::size_t count = 0;
    for (const OverlayFaceFillBatch& batch : plan.faceFillBatches) {
        count += batch.mesh.empty() ? 0u : 1u;
    }
    for (const OverlayLineBatch& batch : plan.lineBatches) {
        count += batch.mesh.empty() ? 0u : 1u;
    }
    for (const OverlayPointBatch& batch : plan.pointBatches) {
        count += batch.mesh.empty() ? 0u : 1u;
    }
    return count;
}

void OverlayProxyFactory::createTriangleProxy(
        const OverlayFaceFillBatch& batch,
        std::uint8_t layerMask,
        std::vector<OverlayRenderableProxy>& proxies) {
    if (batch.mesh.empty()) {
        return;
    }

    appendOverlayProxy(
            *engine_,
            *scene_,
            *overlayMaterial_,
            batch.mesh.vertices,
            batch.mesh.indices,
            OverlayRenderableProxyKind::Triangle,
            batch.state,
            layerMask,
            proxies,
            [](filament::VertexBuffer::Builder& builder) {
                builder
                        .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(OverlayFaceFillVertex, position), sizeof(OverlayFaceFillVertex))
                        .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(OverlayFaceFillVertex, uv), sizeof(OverlayFaceFillVertex))
                        .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayFaceFillVertex, color), sizeof(OverlayFaceFillVertex))
                        .normalized(filament::VertexAttribute::COLOR);
            },
            [this, &batch](filament::MaterialInstance& material) {
                configureOverlayMaterial(material, batch.state);
            });
}

void OverlayProxyFactory::createLineProxy(
        const OverlayLineBatch& batch,
        std::uint8_t layerMask,
        std::vector<OverlayRenderableProxy>& proxies) {
    if (batch.mesh.empty()) {
        return;
    }

    (void)batch.depthBias;
    appendOverlayProxy(
            *engine_,
            *scene_,
            *overlayLineMaterial_,
            batch.mesh.vertices,
            batch.mesh.indices,
            OverlayRenderableProxyKind::Line,
            batch.state,
            layerMask,
            proxies,
            [](filament::VertexBuffer::Builder& builder) {
                builder
                        .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT4, offsetof(OverlayLineVertex, position), sizeof(OverlayLineVertex))
                        .attribute(filament::VertexAttribute::CUSTOM0, 0, AttributeType::FLOAT4, offsetof(OverlayLineVertex, lineDistancePixels), sizeof(OverlayLineVertex))
                        .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayLineVertex, color), sizeof(OverlayLineVertex))
                        .normalized(filament::VertexAttribute::COLOR);
            },
            [this, &batch](filament::MaterialInstance& material) {
                configureOverlayMaterial(material, batch.state);
                material.setParameter("lineWidth", batch.widthPixels);
                material.setParameter("featherPixels", kOverlayLineFeatherPixels);
            });
}

void OverlayProxyFactory::createPointProxy(
        const OverlayPointBatch& batch,
        std::uint8_t layerMask,
        std::vector<OverlayRenderableProxy>& proxies) {
    if (batch.mesh.empty()) {
        return;
    }

    (void)batch.sizePixels;
    (void)batch.depthBias;
    appendOverlayProxy(
            *engine_,
            *scene_,
            *overlayPointMaterial_,
            batch.mesh.vertices,
            batch.mesh.indices,
            OverlayRenderableProxyKind::Point,
            batch.state,
            layerMask,
            proxies,
            [](filament::VertexBuffer::Builder& builder) {
                builder
                        .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT4, offsetof(OverlayPointVertex, position), sizeof(OverlayPointVertex))
                        .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(OverlayPointVertex, uv), sizeof(OverlayPointVertex))
                        .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayPointVertex, color), sizeof(OverlayPointVertex))
                        .normalized(filament::VertexAttribute::COLOR);
            },
            [this, &batch](filament::MaterialInstance& material) {
                configureOverlayMaterial(material, batch.state);
                material.setParameter("innerRadius", batch.innerRadius);
            });
}

void OverlayProxyFactory::updateTriangleProxy(
        OverlayRenderableProxy& proxy,
        const OverlayFaceFillBatch& batch,
        std::uint8_t layerMask) {
    updateProxyBuffers(*engine_, proxy, batch.mesh, layerMask);
    configureOverlayMaterial(*proxy.materialInstance, batch.state);
}

void OverlayProxyFactory::updateLineProxy(
        OverlayRenderableProxy& proxy,
        const OverlayLineBatch& batch,
        std::uint8_t layerMask) {
    updateProxyBuffers(*engine_, proxy, batch.mesh, layerMask);
    configureOverlayMaterial(*proxy.materialInstance, batch.state);
    proxy.materialInstance->setParameter("lineWidth", batch.widthPixels);
    proxy.materialInstance->setParameter("featherPixels", kOverlayLineFeatherPixels);
}

void OverlayProxyFactory::updatePointProxy(
        OverlayRenderableProxy& proxy,
        const OverlayPointBatch& batch,
        std::uint8_t layerMask) {
    updateProxyBuffers(*engine_, proxy, batch.mesh, layerMask);
    configureOverlayMaterial(*proxy.materialInstance, batch.state);
    proxy.materialInstance->setParameter("innerRadius", batch.innerRadius);
}

void OverlayProxyFactory::configureOverlayMaterial(
        filament::MaterialInstance& material,
        const OverlayRenderState& state) const {
    material.setColorWrite(state.colorWrite);
    material.setDepthWrite(state.depthWrite);
    material.setDepthCulling(state.depthCulling);
    material.setDepthFunc(toFilamentDepthFunc(state.depthFunc));
    material.setCullingMode(toFilamentCullingMode(state.cullingMode));
    material.setTransparencyMode(toFilamentTransparencyMode(state.transparencyMode));
    material.setPolygonOffset(state.polygonOffsetScale, state.polygonOffsetConstant);
}

} // namespace quader
