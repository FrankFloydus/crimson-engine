////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/OverlayProxyFactory.h"

#include "render/FilamentBufferDescriptorFactory.h"
#include "render/RenderLayers.h"
#include "render/ViewportOverlayPolicy.h"

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

void OverlayProxyFactory::destroy(OverlayRenderableProxy& proxy) {
    if (!engine_) {
        return;
    }

    if (proxy.entityId != 0u) {
        const utils::Entity entity = toFilamentEntity(proxy.entityId);
        scene_->remove(entity);
        engine_->getRenderableManager().destroy(entity);
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
    if (proxy.entityId != 0u) {
        utils::EntityManager::get().destroy(toFilamentEntity(proxy.entityId));
        proxy.entityId = 0u;
    }
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

    OverlayRenderableProxy proxy;
    const utils::Entity entity = utils::EntityManager::get().create();
    proxy.entityId = toOverlayEntityId(entity);
    proxy.materialInstance = overlayMaterial_->createInstance();
    configureOverlayMaterial(*proxy.materialInstance, batch.state);
    proxy.vertexBuffer = filament::VertexBuffer::Builder()
            .vertexCount(static_cast<std::uint32_t>(batch.mesh.vertices.size()))
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(OverlayFaceFillVertex, position), sizeof(OverlayFaceFillVertex))
            .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(OverlayFaceFillVertex, uv), sizeof(OverlayFaceFillVertex))
            .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayFaceFillVertex, color), sizeof(OverlayFaceFillVertex))
            .normalized(filament::VertexAttribute::COLOR)
            .build(*engine_);
    proxy.vertexBuffer->setBufferAt(*engine_, 0, FilamentBufferDescriptorFactory::vector(batch.mesh.vertices));
    proxy.indexBuffer = filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(batch.mesh.indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine_);
    proxy.indexBuffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::vector(batch.mesh.indices));

    filament::RenderableManager::Builder(1)
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, proxy.vertexBuffer, proxy.indexBuffer)
            .material(0, proxy.materialInstance)
            .blendOrder(0, batch.state.blendOrder)
            .globalBlendOrderEnabled(0, true)
            .layerMask(kViewportAllLayerMask, layerMask)
            .boundingBox(overlayMeshBounds())
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .build(*engine_, entity);
    scene_->addEntity(entity);
    proxies.push_back(proxy);
}

void OverlayProxyFactory::createLineProxy(
        const OverlayLineBatch& batch,
        std::uint8_t layerMask,
        std::vector<OverlayRenderableProxy>& proxies) {
    if (batch.mesh.empty()) {
        return;
    }

    OverlayRenderableProxy proxy;
    const utils::Entity entity = utils::EntityManager::get().create();
    proxy.entityId = toOverlayEntityId(entity);
    proxy.materialInstance = overlayLineMaterial_->createInstance();
    configureOverlayMaterial(*proxy.materialInstance, batch.state);
    proxy.materialInstance->setParameter("lineWidth", batch.widthPixels);
    proxy.materialInstance->setParameter("featherPixels", kOverlayLineFeatherPixels);
    (void)batch.depthBias;
    proxy.vertexBuffer = filament::VertexBuffer::Builder()
            .vertexCount(static_cast<std::uint32_t>(batch.mesh.vertices.size()))
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT4, offsetof(OverlayLineVertex, position), sizeof(OverlayLineVertex))
            .attribute(filament::VertexAttribute::CUSTOM0, 0, AttributeType::FLOAT4, offsetof(OverlayLineVertex, lineDistancePixels), sizeof(OverlayLineVertex))
            .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayLineVertex, color), sizeof(OverlayLineVertex))
            .normalized(filament::VertexAttribute::COLOR)
            .build(*engine_);
    proxy.vertexBuffer->setBufferAt(*engine_, 0, FilamentBufferDescriptorFactory::vector(batch.mesh.vertices));
    proxy.indexBuffer = filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(batch.mesh.indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine_);
    proxy.indexBuffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::vector(batch.mesh.indices));

    filament::RenderableManager::Builder(1)
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, proxy.vertexBuffer, proxy.indexBuffer)
            .material(0, proxy.materialInstance)
            .blendOrder(0, batch.state.blendOrder)
            .globalBlendOrderEnabled(0, true)
            .layerMask(kViewportAllLayerMask, layerMask)
            .boundingBox(overlayMeshBounds())
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .build(*engine_, entity);
    scene_->addEntity(entity);
    proxies.push_back(proxy);
}

void OverlayProxyFactory::createPointProxy(
        const OverlayPointBatch& batch,
        std::uint8_t layerMask,
        std::vector<OverlayRenderableProxy>& proxies) {
    if (batch.mesh.empty()) {
        return;
    }

    OverlayRenderableProxy proxy;
    const utils::Entity entity = utils::EntityManager::get().create();
    proxy.entityId = toOverlayEntityId(entity);
    proxy.materialInstance = overlayPointMaterial_->createInstance();
    configureOverlayMaterial(*proxy.materialInstance, batch.state);
    proxy.materialInstance->setParameter("innerRadius", batch.innerRadius);
    (void)batch.sizePixels;
    (void)batch.depthBias;
    proxy.vertexBuffer = filament::VertexBuffer::Builder()
            .vertexCount(static_cast<std::uint32_t>(batch.mesh.vertices.size()))
            .bufferCount(1)
            .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT4, offsetof(OverlayPointVertex, position), sizeof(OverlayPointVertex))
            .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(OverlayPointVertex, uv), sizeof(OverlayPointVertex))
            .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayPointVertex, color), sizeof(OverlayPointVertex))
            .normalized(filament::VertexAttribute::COLOR)
            .build(*engine_);
    proxy.vertexBuffer->setBufferAt(*engine_, 0, FilamentBufferDescriptorFactory::vector(batch.mesh.vertices));
    proxy.indexBuffer = filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(batch.mesh.indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine_);
    proxy.indexBuffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::vector(batch.mesh.indices));

    filament::RenderableManager::Builder(1)
            .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, proxy.vertexBuffer, proxy.indexBuffer)
            .material(0, proxy.materialInstance)
            .blendOrder(0, batch.state.blendOrder)
            .globalBlendOrderEnabled(0, true)
            .layerMask(kViewportAllLayerMask, layerMask)
            .boundingBox(overlayMeshBounds())
            .culling(false)
            .castShadows(false)
            .receiveShadows(false)
            .build(*engine_, entity);
    scene_->addEntity(entity);
    proxies.push_back(proxy);
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
