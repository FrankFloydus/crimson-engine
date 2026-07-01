////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/mesh_renderable_factory.hpp"

#include "diagnostics/profile.hpp"
#include "math/glm_bridge.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/filament_utils.hpp"
#include "render/material_binding_service.hpp"
#include "render/render_signature_builder.hpp"

#include <backend/DriverEnums.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

/**
 * Represents a Render Vertex value used by the render extraction and Filament resource layer.
 */
struct RenderVertex {
    filament::math::float3 position;
    filament::math::float4 tangentFrame;
    filament::math::float2 uv;
    std::uint32_t color;
};

void destroyMeshRenderableProxyResources(
        filament::Engine& engine,
        filament::Scene* scene,
        MeshRenderableProxy& proxy,
        bool removeFromScene);

/**
 * Cleans up partially constructed Filament mesh resources until ownership is handed to the proxy.
 */
class MeshRenderableCreationGuard {
public:
    MeshRenderableCreationGuard(filament::Engine& engine, filament::Scene& scene, MeshRenderableProxy& proxy)
        : engine_(engine)
        , scene_(scene)
        , proxy_(proxy) {}

    MeshRenderableCreationGuard(const MeshRenderableCreationGuard&) = delete;
    MeshRenderableCreationGuard& operator=(const MeshRenderableCreationGuard&) = delete;
    MeshRenderableCreationGuard(MeshRenderableCreationGuard&&) = delete;
    MeshRenderableCreationGuard& operator=(MeshRenderableCreationGuard&&) = delete;

    ~MeshRenderableCreationGuard() {
        if (active_) {
            destroyMeshRenderableProxyResources(engine_, &scene_, proxy_, sceneAdded_);
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
    MeshRenderableProxy& proxy_;
    bool sceneAdded_ = false;
    bool active_ = true;
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

filament::math::float4 tangentFrameFromNormal(QVec3 normal) {
    if (length(normal) <= 0.000001f) {
        normal = {0.0f, 1.0f, 0.0f};
    } else {
        normal = normalize(normal);
    }

    const QVec3 reference = std::fabs(normal.y) < 0.999f ? QVec3{0.0f, 1.0f, 0.0f} : QVec3{1.0f, 0.0f, 0.0f};
    QVec3 tangent = cross(reference, normal);
    if (length(tangent) <= 0.000001f) {
        tangent = {1.0f, 0.0f, 0.0f};
    } else {
        tangent = normalize(tangent);
    }
    const QVec3 bitangent = cross(normal, tangent);
    const filament::math::mat3f tangentFrame{
            toFilament(tangent),
            toFilament(bitangent),
            toFilament(normal),
    };
    const filament::math::quatf packed = filament::math::mat3f::packTangentFrame(tangentFrame, sizeof(float));
    return {packed.x, packed.y, packed.z, packed.w};
}

void destroyMeshRenderableProxyResources(
        filament::Engine& engine,
        filament::Scene* scene,
        MeshRenderableProxy& proxy,
        bool removeFromScene) {
    if (proxy.entity) {
        if (removeFromScene && scene != nullptr) {
            scene->remove(proxy.entity);
        }
        engine.getRenderableManager().destroy(proxy.entity);
        engine.getTransformManager().destroy(proxy.entity);
    }
    for (filament::MaterialInstance*& materialInstance : proxy.materialInstances) {
        if (materialInstance) {
            engine.destroy(materialInstance);
            materialInstance = nullptr;
        }
    }
    proxy.materialInstances.clear();
    proxy.materialSlots.clear();
    if (proxy.vertexBuffer) {
        engine.destroy(proxy.vertexBuffer);
        proxy.vertexBuffer = nullptr;
    }
    if (proxy.indexBuffer) {
        engine.destroy(proxy.indexBuffer);
        proxy.indexBuffer = nullptr;
    }
    if (proxy.entity) {
        utils::EntityManager::get().destroy(proxy.entity);
        proxy.entity.clear();
    }
}

} // namespace

void MeshRenderableFactory::initialize(
        filament::Engine& engine,
        filament::Scene& scene,
        MaterialBindingService& materialBinding) {
    engine_ = &engine;
    scene_ = &scene;
    materialBinding_ = &materialBinding;
}

void MeshRenderableFactory::shutdown() {
    engine_ = nullptr;
    scene_ = nullptr;
    materialBinding_ = nullptr;
}

MeshRenderableProxy MeshRenderableFactory::create(EntityId entityId,
        AssetId meshId,
        const MeshAsset& mesh,
        const MaterialComponent* material,
        const AssetRegistry& assets,
        const QMat4& worldTransform,
        bool castShadows,
        bool doubleSided,
        std::uint64_t materialRevision,
        std::uint8_t layerMask,
        bool editorUnlit,
        bool depthOnly,
        std::uint8_t channel) {
    QDR_PROFILE_FUNCTION();
    MeshRenderableProxy proxy;
    MeshRenderableCreationGuard creationGuard(*engine_, *scene_, proxy);
    proxy.entity = utils::EntityManager::get().create();
    proxy.sourceEntity = entityId;
    proxy.mesh = meshId;
    proxy.meshRevision = mesh.revision.value;
    proxy.material = material ? material->material : InvalidAssetId;
    proxy.materialRevision = materialRevision;
    proxy.castShadows = castShadows;
    proxy.doubleSided = doubleSided;

    std::vector<RenderVertex> vertices;
    {
        QDR_PROFILE_SCOPE("Mesh renderable build vertices");
        vertices.reserve(mesh.vertices.size());
        for (const MeshVertex& vertex : mesh.vertices) {
            vertices.push_back({
                    toFilament(vertex.position),
                    tangentFrameFromNormal(vertex.normal),
                    {vertex.uv.x, vertex.uv.y},
                    vertex.rgba,
            });
        }
    }

    {
        QDR_PROFILE_SCOPE("Mesh renderable upload vertex/index buffers");
        proxy.vertexBuffer = filament::VertexBuffer::Builder()
                .vertexCount(static_cast<std::uint32_t>(vertices.size()))
                .bufferCount(1)
                .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(RenderVertex, position), sizeof(RenderVertex))
                .attribute(filament::VertexAttribute::TANGENTS, 0, AttributeType::FLOAT4, offsetof(RenderVertex, tangentFrame), sizeof(RenderVertex))
                .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(RenderVertex, uv), sizeof(RenderVertex))
                .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(RenderVertex, color), sizeof(RenderVertex))
                .normalized(filament::VertexAttribute::COLOR)
                .build(*engine_);
        proxy.vertexBuffer->setBufferAt(*engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));

        proxy.indexBuffer = filament::IndexBuffer::Builder()
                .indexCount(static_cast<std::uint32_t>(mesh.indices.size()))
                .bufferType(filament::IndexBuffer::IndexType::UINT)
                .build(*engine_);
        proxy.indexBuffer->setBuffer(*engine_, FilamentBufferDescriptorFactory::vector(mesh.indices));
    }

    const std::vector<MeshPrimitiveSpan> primitiveSpans = coalescedMeshPrimitiveSpans(mesh);
    proxy.materialSlots.reserve(primitiveSpans.size());
    proxy.materialInstances.reserve(primitiveSpans.size());
    filament::RenderableManager::Builder builder(primitiveSpans.size());
    {
        QDR_PROFILE_SCOPE("Mesh renderable build primitives");
        for (std::size_t primitiveIndex = 0; primitiveIndex < primitiveSpans.size(); ++primitiveIndex) {
            const MeshPrimitiveSpan& primitive = primitiveSpans[primitiveIndex];
            const AssetId primitiveMaterialId = material ? material->materialForSlot(primitive.materialSlot) : InvalidAssetId;
            const MaterialDocument* primitiveMaterial = assets.material(primitiveMaterialId);
            filament::MaterialInstance* materialInstance =
                    materialBinding_->createMeshMaterialInstance(assets, primitiveMaterial, doubleSided, editorUnlit);
            if (depthOnly && materialInstance) {
                materialInstance->setColorWrite(false);
                materialInstance->setDepthWrite(true);
                materialInstance->setDepthCulling(true);
            }
            proxy.materialSlots.push_back(primitive.materialSlot);
            proxy.materialInstances.push_back(materialInstance);
            builder.geometry(primitiveIndex,
                    filament::RenderableManager::PrimitiveType::TRIANGLES,
                    proxy.vertexBuffer,
                    proxy.indexBuffer,
                    primitive.indexOffset,
                    primitive.indexCount);
            builder.material(primitiveIndex, materialInstance);
        }
    }
    {
        QDR_PROFILE_SCOPE("Mesh renderable build Filament entity");
        builder.boundingBox(computeBounds(mesh))
                .layerMask(kViewportAllLayerMask, layerMask)
                .channel(channel)
                .castShadows(castShadows)
                .receiveShadows(!editorUnlit && !depthOnly)
                .build(*engine_, proxy.entity);
    }

    engine_->getTransformManager().create(proxy.entity);
    updateTransform(proxy, worldTransform);
    scene_->addEntity(proxy.entity);
    creationGuard.markSceneAdded();
    creationGuard.release();
    return proxy;
}

void MeshRenderableFactory::updateTransform(const MeshRenderableProxy& proxy, const QMat4& worldTransform) {
    QDR_PROFILE_FUNCTION();
    if (!engine_ || !proxy.entity) {
        return;
    }

    auto& transformManager = engine_->getTransformManager();
    filament::TransformManager::Instance instance = transformManager.getInstance(proxy.entity);
    if (!instance) {
        transformManager.create(proxy.entity);
        instance = transformManager.getInstance(proxy.entity);
    }
    transformManager.setTransform(instance, makeTransform(worldTransform));
}

void MeshRenderableFactory::removeFromScene(const MeshRenderableProxy& proxy) {
    if (!scene_ || !proxy.entity) {
        return;
    }

    scene_->remove(proxy.entity);
}

void MeshRenderableFactory::destroy(MeshRenderableProxy& proxy) {
    if (!engine_) {
        return;
    }

    destroyMeshRenderableProxyResources(*engine_, scene_, proxy, true);
}

void MeshRenderableFactory::destroyDetached(MeshRenderableProxy& proxy) {
    if (!engine_) {
        return;
    }

    destroyMeshRenderableProxyResources(*engine_, nullptr, proxy, false);
}

std::uint64_t MeshRenderableFactory::meshAssetSignature(std::uint64_t hash, const MeshAsset& mesh) {
    return RenderSignatureBuilder::meshAssetSignature(hash, mesh);
}

} // namespace quader
