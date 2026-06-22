#include "render/MeshRenderableFactory.h"

#include "render/FilamentBufferDescriptorFactory.h"
#include "render/FilamentUtils.h"
#include "render/MaterialBindingService.h"

#include <backend/DriverEnums.h>
#include <filament/RenderableManager.h>
#include <filament/TransformManager.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr std::uint64_t kFnvPrime = 1099511628211ull;

struct RenderVertex {
    filament::math::float3 position;
    filament::math::float4 tangentFrame;
    filament::math::float2 uv;
    std::uint32_t color;
};

std::uint64_t hashCombine(std::uint64_t hash, std::uint64_t value) {
    hash ^= value;
    hash *= kFnvPrime;
    return hash;
}

std::uint64_t hashFloat(std::uint64_t hash, float value) {
    return hashCombine(hash, std::bit_cast<std::uint32_t>(value));
}

std::uint64_t hashVec3(std::uint64_t hash, Vec3 value) {
    hash = hashFloat(hash, value.x);
    hash = hashFloat(hash, value.y);
    return hashFloat(hash, value.z);
}

filament::math::mat4f makeTransform(const Mat4& transform) {
    const glm::mat4 glmTransform = toGlm(transform);
    filament::math::mat4f result;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            result[column][row] = glmTransform[column][row];
        }
    }
    return result;
}

filament::math::float4 tangentFrameFromNormal(Vec3 normal) {
    if (length(normal) <= 0.000001f) {
        normal = {0.0f, 1.0f, 0.0f};
    } else {
        normal = normalize(normal);
    }

    const Vec3 reference = std::fabs(normal.y) < 0.999f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
    Vec3 tangent = cross(reference, normal);
    if (length(tangent) <= 0.000001f) {
        tangent = {1.0f, 0.0f, 0.0f};
    } else {
        tangent = normalize(tangent);
    }
    const Vec3 bitangent = cross(normal, tangent);
    const filament::math::mat3f tangentFrame{
            toFilament(tangent),
            toFilament(bitangent),
            toFilament(normal),
    };
    const filament::math::quatf packed = filament::math::mat3f::packTangentFrame(tangentFrame, sizeof(float));
    return {packed.x, packed.y, packed.z, packed.w};
}

std::vector<MeshPrimitiveSpan> renderPrimitiveSpansFor(const MeshAsset& mesh) {
    std::vector<MeshPrimitiveSpan> spans;
    spans.reserve(mesh.primitives.empty() ? 1u : mesh.primitives.size());
    for (const MeshPrimitiveSpan& primitive : mesh.primitives) {
        const std::uint64_t offset = primitive.indexOffset;
        const std::uint64_t count = primitive.indexCount;
        if (count == 0u || offset >= mesh.indices.size() || offset + count > mesh.indices.size()) {
            continue;
        }
        spans.push_back(primitive);
    }
    if (spans.empty() && !mesh.indices.empty()) {
        spans.push_back(MeshPrimitiveSpan{
                .indexOffset = 0,
                .indexCount = static_cast<std::uint32_t>(mesh.indices.size()),
                .materialSlot = 0,
        });
    }
    return spans;
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
        AssetId materialId,
        const MaterialDocument* material,
        const AssetRegistry& assets,
        const Mat4& worldTransform,
        bool castShadows,
        bool doubleSided,
        std::uint64_t materialRevision) {
    MeshRenderableProxy proxy;
    proxy.entity = utils::EntityManager::get().create();
    proxy.sourceEntity = entityId;
    proxy.mesh = meshId;
    proxy.meshRevision = mesh.revision.value;
    proxy.material = materialId;
    proxy.materialRevision = materialRevision;
    proxy.castShadows = castShadows;
    proxy.doubleSided = doubleSided;

    std::vector<RenderVertex> vertices;
    vertices.reserve(mesh.vertices.size());
    for (const MeshVertex& vertex : mesh.vertices) {
        vertices.push_back({
                toFilament(vertex.position),
                tangentFrameFromNormal(vertex.normal),
                {vertex.uv.x, vertex.uv.y},
                vertex.rgba,
        });
    }

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

    const std::vector<MeshPrimitiveSpan> primitiveSpans = renderPrimitiveSpansFor(mesh);
    proxy.materialSlots.reserve(primitiveSpans.size());
    proxy.materialInstances.reserve(primitiveSpans.size());
    filament::RenderableManager::Builder builder(primitiveSpans.size());
    for (std::size_t primitiveIndex = 0; primitiveIndex < primitiveSpans.size(); ++primitiveIndex) {
        const MeshPrimitiveSpan& primitive = primitiveSpans[primitiveIndex];
        filament::MaterialInstance* materialInstance = materialBinding_->createMeshMaterialInstance(assets, material);
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
    builder.boundingBox(computeBounds(mesh))
            .culling(!doubleSided)
            .castShadows(castShadows)
            .receiveShadows(false)
            .build(*engine_, proxy.entity);

    engine_->getTransformManager().create(proxy.entity);
    updateTransform(proxy, worldTransform);
    scene_->addEntity(proxy.entity);
    return proxy;
}

void MeshRenderableFactory::updateTransform(const MeshRenderableProxy& proxy, const Mat4& worldTransform) {
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

void MeshRenderableFactory::destroy(MeshRenderableProxy& proxy) {
    if (!engine_) {
        return;
    }

    if (proxy.entity) {
        scene_->remove(proxy.entity);
        engine_->getRenderableManager().destroy(proxy.entity);
        engine_->getTransformManager().destroy(proxy.entity);
    }
    for (filament::MaterialInstance*& materialInstance : proxy.materialInstances) {
        if (materialInstance) {
            engine_->destroy(materialInstance);
            materialInstance = nullptr;
        }
    }
    proxy.materialInstances.clear();
    proxy.materialSlots.clear();
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
}

std::uint64_t MeshRenderableFactory::meshAssetSignature(std::uint64_t hash, const MeshAsset& mesh) {
    hash = hashCombine(hash, mesh.vertices.size());
    for (const MeshVertex& vertex : mesh.vertices) {
        hash = hashVec3(hash, vertex.position);
        hash = hashVec3(hash, vertex.normal);
        hash = hashFloat(hash, vertex.uv.x);
        hash = hashFloat(hash, vertex.uv.y);
        hash = hashCombine(hash, vertex.rgba);
    }
    hash = hashCombine(hash, mesh.indices.size());
    for (const std::uint32_t index : mesh.indices) {
        hash = hashCombine(hash, index);
    }
    hash = hashCombine(hash, mesh.primitives.size());
    for (const MeshPrimitiveSpan& primitive : mesh.primitives) {
        hash = hashCombine(hash, primitive.indexOffset);
        hash = hashCombine(hash, primitive.indexCount);
        hash = hashCombine(hash, primitive.materialSlot);
    }
    return hash;
}

} // namespace quader
