#include "quader/render/SceneRenderBridge.h"

#include "quader/core/Logger.h"
#include "quader/render/FilamentUtils.h"
#include "quader/render/ViewportOverlayPolicy.h"

#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>
#include <backend/PixelBufferDescriptor.h>
#include <filament/LightManager.h>
#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <lodepng.h>
#include <math/mat4.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

struct RenderVertex {
    filament::math::float3 position;
    filament::math::float3 normal;
    filament::math::float2 uv;
    std::uint32_t color;
};

struct OverlayRenderState {
    bool depthWrite = false;
    bool colorWrite = true;
    bool depthCulling = false;
    filament::MaterialInstance::DepthFunc depthFunc = filament::MaterialInstance::DepthFunc::A;
    filament::MaterialInstance::CullingMode cullingMode = filament::MaterialInstance::CullingMode::NONE;
    filament::MaterialInstance::TransparencyMode transparencyMode = filament::MaterialInstance::TransparencyMode::DEFAULT;
    float polygonOffsetScale = 0.0f;
    float polygonOffsetConstant = 0.0f;
    std::uint16_t blendOrder = 0;
};

filament::math::float3 directionFromTransform(const TransformComponent& transform) {
    const float yaw = transform.rotationEuler.y;
    const float pitch = transform.rotationEuler.x;
    const float cp = std::cos(pitch);
    return toFilament(normalize({
            std::sin(yaw) * cp,
            -std::cos(pitch),
            std::cos(yaw) * cp,
    }));
}

filament::math::mat4f makeTransform(const TransformComponent& transform) {
    const auto translation = filament::math::mat4f::translation(toFilament(transform.position));
    const auto rotationX = filament::math::mat4f::rotation(transform.rotationEuler.x, filament::math::float3{1.0f, 0.0f, 0.0f});
    const auto rotationY = filament::math::mat4f::rotation(transform.rotationEuler.y, filament::math::float3{0.0f, 1.0f, 0.0f});
    const auto rotationZ = filament::math::mat4f::rotation(transform.rotationEuler.z, filament::math::float3{0.0f, 0.0f, 1.0f});
    const auto scale = filament::math::mat4f::scaling(toFilament(transform.scale));
    return translation * rotationZ * rotationY * rotationX * scale;
}

template <typename T>
filament::backend::BufferDescriptor makeBufferDescriptor(const std::vector<T>& values) {
    const std::size_t byteCount = sizeof(T) * values.size();
    auto* bytes = new std::byte[byteCount];
    std::memcpy(bytes, values.data(), byteCount);
    return {bytes, byteCount, [](void* buffer, std::size_t, void*) {
                delete[] static_cast<std::byte*>(buffer);
            }};
}

filament::backend::PixelBufferDescriptor makePixelBufferDescriptor(const unsigned char* pixels, std::size_t byteCount) {
    auto* bytes = new std::byte[byteCount];
    std::memcpy(bytes, pixels, byteCount);
    return {bytes, byteCount,
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE,
            [](void* buffer, std::size_t, void*) {
                delete[] static_cast<std::byte*>(buffer);
            }};
}

filament::LightManager::Type toFilamentLightType(LightType type) {
    switch (type) {
    case LightType::Directional:
        return filament::LightManager::Type::DIRECTIONAL;
    case LightType::Point:
        return filament::LightManager::Type::POINT;
    case LightType::Spot:
        return filament::LightManager::Type::SPOT;
    }
    return filament::LightManager::Type::DIRECTIONAL;
}

filament::Box computeOverlayBounds(const std::vector<RenderVertex>& vertices) {
    if (vertices.empty()) {
        return {{0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}};
    }

    filament::math::float3 minValue{
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
            std::numeric_limits<float>::max(),
    };
    filament::math::float3 maxValue{
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
            std::numeric_limits<float>::lowest(),
    };

    for (const RenderVertex& vertex : vertices) {
        minValue.x = std::min(minValue.x, vertex.position.x);
        minValue.y = std::min(minValue.y, vertex.position.y);
        minValue.z = std::min(minValue.z, vertex.position.z);
        maxValue.x = std::max(maxValue.x, vertex.position.x);
        maxValue.y = std::max(maxValue.y, vertex.position.y);
        maxValue.z = std::max(maxValue.z, vertex.position.z);
    }

    filament::Box box;
    box.set(minValue, maxValue);
    return box;
}

void appendOverlayTriangle(std::vector<RenderVertex>& vertices, const ViewportOverlayTriangle& triangle) {
    const std::uint32_t linearColor = rgba8SrgbToLinear(triangle.color);
    vertices.push_back({toFilament(triangle.a), {}, {}, linearColor});
    vertices.push_back({toFilament(triangle.b), {}, {}, linearColor});
    vertices.push_back({toFilament(triangle.c), {}, {}, linearColor});
}

std::uint8_t mipLevelCount(unsigned width, unsigned height) {
    unsigned dimension = std::max(width, height);
    std::uint8_t levels = 1;
    while (dimension > 1u) {
        dimension /= 2u;
        ++levels;
    }
    return levels;
}

} // namespace

SceneRenderBridge::~SceneRenderBridge() {
    shutdown();
}

void SceneRenderBridge::initialize(filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& meshMaterial,
        filament::Material& overlayMaterial,
        std::filesystem::path resourceRoot) {
    engine_ = &engine;
    scene_ = &scene;
    meshMaterial_ = &meshMaterial;
    overlayMaterial_ = &overlayMaterial;
    resourceRoot_ = std::move(resourceRoot);
    fallbackAlbedoTexture_ = createSolidTexture(255, 255, 255, 255, TextureColorSpace::Srgb);
    fallbackRoughnessTexture_ = createSolidTexture(255, 255, 255, 255, TextureColorSpace::Linear);
}

void SceneRenderBridge::shutdown() {
    if (!engine_) {
        return;
    }

    for (auto& [id, proxy] : meshProxies_) {
        destroyMeshProxy(proxy);
    }
    meshProxies_.clear();

    for (OverlayProxy& proxy : overlayProxies_) {
        destroyOverlayProxy(proxy);
    }
    overlayProxies_.clear();

    for (auto& [id, proxy] : lightProxies_) {
        destroyLightProxy(proxy);
    }
    lightProxies_.clear();
    selectedOverlayFrame_ = {};

    for (auto& [id, texture] : textureCache_) {
        destroyTexture(texture);
    }
    textureCache_.clear();

    destroyTexture(fallbackAlbedoTexture_);
    destroyTexture(fallbackRoughnessTexture_);

    engine_ = nullptr;
    scene_ = nullptr;
    meshMaterial_ = nullptr;
    overlayMaterial_ = nullptr;
    resourceRoot_.clear();
    syncedRevision_ = 0;
    hasSynced_ = false;
}

void SceneRenderBridge::sync(const SceneDocument& document, const AssetRegistry& assets) {
    if (!engine_ || !scene_ || !meshMaterial_ || !overlayMaterial_) {
        return;
    }

    if (hasSynced_ && syncedRevision_ == document.revision()) {
        return;
    }

    rebuild(document, assets);
    syncedRevision_ = document.revision();
    hasSynced_ = true;
}

void SceneRenderBridge::setOverlayFrame(ViewportOverlayFrame frame) {
    externalOverlayFrame_ = std::move(frame);
    if (engine_ && scene_ && overlayMaterial_) {
        rebuildOverlayProxies();
    }
}

void SceneRenderBridge::rebuild(const SceneDocument& document, const AssetRegistry& assets) {
    for (auto& [id, proxy] : meshProxies_) {
        destroyMeshProxy(proxy);
    }
    meshProxies_.clear();

    for (auto& [id, proxy] : lightProxies_) {
        destroyLightProxy(proxy);
    }
    lightProxies_.clear();
    selectedOverlayFrame_ = {};

    auto& entityManager = utils::EntityManager::get();
    auto& transformManager = engine_->getTransformManager();
    const filament::TextureSampler sampler(
            filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
            filament::TextureSampler::MagFilter::LINEAR,
            filament::TextureSampler::WrapMode::REPEAT);

    for (const EntityId entityId : document.entities()) {
        const VisibilityComponent* visibility = document.visibility(entityId);
        if (visibility && !visibility->visible) {
            continue;
        }

        const TransformComponent transform = document.transform(entityId) ? *document.transform(entityId) : TransformComponent{};

        if (const auto* meshComponent = document.mesh(entityId)) {
            const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
            if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                LOG_WARN("Skipping render entity {} because its mesh asset is missing or empty", entityId);
                continue;
            }

            const MaterialComponent* materialComponent = document.material(entityId);
            const MaterialDocument* material = materialComponent ? assets.material(materialComponent->material) : nullptr;

            MeshProxy proxy;
            proxy.entity = entityManager.create();
            proxy.sourceEntity = entityId;
            proxy.mesh = meshComponent->mesh;
            proxy.material = materialComponent ? materialComponent->material : InvalidAssetId;
            proxy.materialInstance = meshMaterial_->createInstance();
            proxy.materialInstance->setCullingMode(filament::MaterialInstance::CullingMode::BACK);
            proxy.materialInstance->setParameter(MaterialTextureSlot::BaseColor,
                    resolveTexture(assets, material, MaterialTextureSlot::BaseColor, fallbackAlbedoTexture_),
                    sampler);
            proxy.materialInstance->setParameter(MaterialTextureSlot::Roughness,
                    resolveTexture(assets, material, MaterialTextureSlot::Roughness, fallbackRoughnessTexture_),
                    sampler);
            const Vec3 baseColor = material ? material->baseColor : Vec3{1.0f, 1.0f, 1.0f};
            proxy.materialInstance->setParameter("baseColorFactor", filament::math::float4{baseColor.x, baseColor.y, baseColor.z, 1.0f});
            proxy.materialInstance->setParameter("metallicFactor", material ? material->metallic : 0.0f);
            proxy.materialInstance->setParameter("roughnessFactor", material ? material->roughness : 1.0f);

            std::vector<RenderVertex> vertices;
            vertices.reserve(mesh->vertices.size());
            for (const MeshVertex& vertex : mesh->vertices) {
                vertices.push_back({
                        toFilament(vertex.position),
                        toFilament(vertex.normal),
                        {vertex.uv.x, vertex.uv.y},
                        vertex.rgba,
                });
            }

            proxy.vertexBuffer = filament::VertexBuffer::Builder()
                    .vertexCount(static_cast<std::uint32_t>(vertices.size()))
                    .bufferCount(1)
                    .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(RenderVertex, position), sizeof(RenderVertex))
                    .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(RenderVertex, uv), sizeof(RenderVertex))
                    .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(RenderVertex, color), sizeof(RenderVertex))
                    .normalized(filament::VertexAttribute::COLOR)
                    .build(*engine_);
            proxy.vertexBuffer->setBufferAt(*engine_, 0, makeBufferDescriptor(vertices));

            proxy.indexBuffer = filament::IndexBuffer::Builder()
                    .indexCount(static_cast<std::uint32_t>(mesh->indices.size()))
                    .bufferType(filament::IndexBuffer::IndexType::UINT)
                    .build(*engine_);
            proxy.indexBuffer->setBuffer(*engine_, makeBufferDescriptor(mesh->indices));

            filament::RenderableManager::Builder(1)
                    .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES,
                            proxy.vertexBuffer, proxy.indexBuffer)
                    .material(0, proxy.materialInstance)
                    .boundingBox(computeBounds(*mesh))
                    .castShadows(false)
                    .receiveShadows(false)
                    .build(*engine_, proxy.entity);

            transformManager.create(proxy.entity);
            transformManager.setTransform(transformManager.getInstance(proxy.entity), makeTransform(transform));
            scene_->addEntity(proxy.entity);
            meshProxies_.emplace(entityId, proxy);
        }

        if (const auto* light = document.light(entityId)) {
            LightProxy proxy;
            proxy.entity = entityManager.create();
            proxy.sourceEntity = entityId;

            filament::LightManager::Builder(toFilamentLightType(light->type))
                    .color({light->color.x, light->color.y, light->color.z})
                    .intensity(light->intensity)
                    .direction(directionFromTransform(transform))
                    .castShadows(light->castsShadows)
                    .build(*engine_, proxy.entity);
            scene_->addEntity(proxy.entity);
            lightProxies_.emplace(entityId, proxy);
        }
    }

    rebuildOverlayProxies();
}

void SceneRenderBridge::rebuildOverlayProxies() {
    for (OverlayProxy& proxy : overlayProxies_) {
        destroyOverlayProxy(proxy);
    }
    overlayProxies_.clear();

    if (!engine_ || !scene_ || !overlayMaterial_) {
        return;
    }

    auto buildOverlay = [this](const std::vector<RenderVertex>& vertices,
                                filament::RenderableManager::PrimitiveType primitiveType,
                                const OverlayRenderState& state) {
        if (vertices.empty()) {
            return;
        }

        std::vector<std::uint32_t> indices;
        indices.reserve(vertices.size());
        for (std::uint32_t index = 0; index < vertices.size(); ++index) {
            indices.push_back(index);
        }

        OverlayProxy proxy;
        proxy.entity = utils::EntityManager::get().create();
        proxy.materialInstance = overlayMaterial_->createInstance();
        proxy.materialInstance->setColorWrite(state.colorWrite);
        proxy.materialInstance->setDepthWrite(state.depthWrite);
        proxy.materialInstance->setDepthCulling(state.depthCulling);
        proxy.materialInstance->setDepthFunc(state.depthFunc);
        proxy.materialInstance->setCullingMode(state.cullingMode);
        proxy.materialInstance->setTransparencyMode(state.transparencyMode);
        proxy.materialInstance->setPolygonOffset(state.polygonOffsetScale, state.polygonOffsetConstant);
        proxy.vertexBuffer = filament::VertexBuffer::Builder()
                .vertexCount(static_cast<std::uint32_t>(vertices.size()))
                .bufferCount(1)
                .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT3, offsetof(RenderVertex, position), sizeof(RenderVertex))
                .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(RenderVertex, uv), sizeof(RenderVertex))
                .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(RenderVertex, color), sizeof(RenderVertex))
                .normalized(filament::VertexAttribute::COLOR)
                .build(*engine_);
        proxy.vertexBuffer->setBufferAt(*engine_, 0, makeBufferDescriptor(vertices));
        proxy.indexBuffer = filament::IndexBuffer::Builder()
                .indexCount(static_cast<std::uint32_t>(indices.size()))
                .bufferType(filament::IndexBuffer::IndexType::UINT)
                .build(*engine_);
        proxy.indexBuffer->setBuffer(*engine_, makeBufferDescriptor(indices));

        filament::RenderableManager::Builder(1)
                .geometry(0, primitiveType, proxy.vertexBuffer, proxy.indexBuffer)
                .material(0, proxy.materialInstance)
                .blendOrder(0, state.blendOrder)
                .globalBlendOrderEnabled(0, true)
                .boundingBox(computeOverlayBounds(vertices))
                .castShadows(false)
                .receiveShadows(false)
                .build(*engine_, proxy.entity);
        scene_->addEntity(proxy.entity);
        overlayProxies_.push_back(proxy);
    };

    auto appendTrianglesForLayer = [](std::vector<RenderVertex>& vertices,
                                            const ViewportOverlayFrame& frame,
                                            ViewportOverlayLayer layer) {
        for (const ViewportOverlayTriangle& triangle : frame.triangles) {
            if (triangle.layer == layer) {
                appendOverlayTriangle(vertices, triangle);
            }
        }
    };

    auto makeOverlayState = [](ViewportOverlayLayer layer,
                                      ViewportOverlayGeometryKind kind,
                                      std::uint16_t blendOrder,
                                      bool depthStamp = false,
                                      bool faceColorPass = false) {
        const bool drawOnTop = viewportOverlayDepthMode(layer, kind) == ViewportOverlayDepthMode::DrawOnTop;
        OverlayRenderState state;
        state.depthWrite = depthStamp;
        state.colorWrite = !depthStamp;
        state.depthCulling = depthStamp || faceColorPass || !drawOnTop;
        state.depthFunc = depthStamp ? filament::MaterialInstance::DepthFunc::LE :
                faceColorPass ? filament::MaterialInstance::DepthFunc::E :
                drawOnTop ? filament::MaterialInstance::DepthFunc::A :
                filament::MaterialInstance::DepthFunc::LE;
        state.cullingMode = kind == ViewportOverlayGeometryKind::Triangles
                ? filament::MaterialInstance::CullingMode::BACK
                : filament::MaterialInstance::CullingMode::NONE;
        state.transparencyMode = kind == ViewportOverlayGeometryKind::Triangles
                ? filament::MaterialInstance::TransparencyMode::TWO_PASSES_ONE_SIDE
                : filament::MaterialInstance::TransparencyMode::DEFAULT;
        if (kind == ViewportOverlayGeometryKind::Triangles) {
            state.polygonOffsetScale = -1.0f;
            state.polygonOffsetConstant = -1.0f;
        }
        state.blendOrder = blendOrder;
        return state;
    };

    std::uint16_t blendOrder = 0;
    for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
        std::vector<RenderVertex> triangleVertices;
        triangleVertices.reserve((selectedOverlayFrame_.triangles.size() + externalOverlayFrame_.triangles.size()) * 3u);
        appendTrianglesForLayer(triangleVertices, selectedOverlayFrame_, layer);
        appendTrianglesForLayer(triangleVertices, externalOverlayFrame_, layer);
        if (!triangleVertices.empty()) {
            if (viewportOverlayLayerUsesDepthStampedFaceFill(layer)) {
                buildOverlay(triangleVertices,
                        filament::RenderableManager::PrimitiveType::TRIANGLES,
                        makeOverlayState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++, true, false));
                buildOverlay(triangleVertices,
                        filament::RenderableManager::PrimitiveType::TRIANGLES,
                        makeOverlayState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++, false, true));
            } else {
                buildOverlay(triangleVertices,
                        filament::RenderableManager::PrimitiveType::TRIANGLES,
                        makeOverlayState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++));
            }
        }
    }
}

void SceneRenderBridge::destroyMeshProxy(MeshProxy& proxy) {
    if (!engine_) {
        return;
    }

    if (proxy.entity) {
        scene_->remove(proxy.entity);
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
}

void SceneRenderBridge::destroyOverlayProxy(OverlayProxy& proxy) {
    if (!engine_) {
        return;
    }

    if (proxy.entity) {
        scene_->remove(proxy.entity);
        engine_->getRenderableManager().destroy(proxy.entity);
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
}

void SceneRenderBridge::destroyLightProxy(LightProxy& proxy) {
    if (!engine_) {
        return;
    }

    if (proxy.entity) {
        scene_->remove(proxy.entity);
        engine_->getLightManager().destroy(proxy.entity);
        utils::EntityManager::get().destroy(proxy.entity);
        proxy.entity.clear();
    }
}

filament::Texture* SceneRenderBridge::resolveTexture(const AssetRegistry& assets,
        const MaterialDocument* material,
        const char* slotName,
        filament::Texture* fallback) {
    if (!material) {
        return fallback;
    }
    auto it = material->textureSlots.find(slotName);
    if (it == material->textureSlots.end()) {
        std::string alternateSlot = slotName;
        if (!alternateSlot.ends_with("Texture")) {
            alternateSlot += "Texture";
        }
        it = material->textureSlots.find(alternateSlot);
        if (it == material->textureSlots.end()) {
            return fallback;
        }
    }
    const TextureAsset* texture = assets.texture(it->second);
    if (!texture) {
        return fallback;
    }
    const TextureCacheKey cacheKey{
            .id = it->second,
            .colorSpace = texture->colorSpace,
    };
    if (const auto cached = textureCache_.find(cacheKey); cached != textureCache_.end()) {
        return cached->second ? cached->second : fallback;
    }
    filament::Texture* loaded = loadTextureAsset(*texture);
    textureCache_[cacheKey] = loaded;
    return loaded ? loaded : fallback;
}

filament::Texture* SceneRenderBridge::loadTextureAsset(const TextureAsset& texture) {
    if (!engine_ || texture.sourcePath.empty()) {
        return nullptr;
    }

    std::filesystem::path path = texture.sourcePath;
    if (path.is_relative()) {
        path = resourceRoot_ / path;
    }

    std::vector<unsigned char> pixels;
    unsigned width = 0;
    unsigned height = 0;
    const unsigned error = lodepng::decode(pixels, width, height, path.string());
    if (error != 0u) {
        LOG_ERROR("Failed to decode material texture '{}': {}", path.string(), lodepng_error_text(error));
        return nullptr;
    }

    const std::uint8_t levels = mipLevelCount(width, height);
    filament::Texture* loaded = filament::Texture::Builder()
            .width(width)
            .height(height)
            .levels(levels)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(texture.colorSpace == TextureColorSpace::Srgb ? filament::Texture::InternalFormat::SRGB8_A8 : filament::Texture::InternalFormat::RGBA8)
            .usage(filament::Texture::Usage::UPLOADABLE |
                    filament::Texture::Usage::SAMPLEABLE |
                    filament::Texture::Usage::GEN_MIPMAPPABLE)
            .build(*engine_);
    loaded->setImage(*engine_, 0, makePixelBufferDescriptor(pixels.data(), pixels.size()));
    if (levels > 1u) {
        loaded->generateMipmaps(*engine_);
    }
    LOG_INFO("Loaded material texture '{}' ({}x{})", path.string(), width, height);
    return loaded;
}

filament::Texture* SceneRenderBridge::createSolidTexture(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a, TextureColorSpace colorSpace) {
    if (!engine_) {
        return nullptr;
    }
    filament::Texture* texture = filament::Texture::Builder()
            .width(1)
            .height(1)
            .levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(colorSpace == TextureColorSpace::Srgb ? filament::Texture::InternalFormat::SRGB8_A8 : filament::Texture::InternalFormat::RGBA8)
            .usage(filament::Texture::Usage::UPLOADABLE | filament::Texture::Usage::SAMPLEABLE)
            .build(*engine_);
    const std::array<unsigned char, 4> pixel{{r, g, b, a}};
    texture->setImage(*engine_, 0, makePixelBufferDescriptor(pixel.data(), pixel.size()));
    return texture;
}

void SceneRenderBridge::destroyTexture(filament::Texture*& texture) {
    if (engine_ && texture) {
        engine_->destroy(texture);
    }
    texture = nullptr;
}

} // namespace quader
