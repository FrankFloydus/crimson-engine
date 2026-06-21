#include "quader/render/SceneRenderBridge.h"

#include "quader/core/Logger.h"
#include "quader/render/FilamentUtils.h"
#include "quader/render/RenderLayers.h"
#include "quader/render/ViewportOverlayPolicy.h"

#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>
#include <backend/PixelBufferDescriptor.h>
#include <filament/LightManager.h>
#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <filament/TransformManager.h>
#include <lodepng.h>
#include <math/mat3.h>
#include <math/mat4.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <string>
#include <unordered_set>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

constexpr std::uint64_t kFnvOffset = 1469598103934665603ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr float kPi = 3.14159265358979323846f;
constexpr float kMeshTextureAnisotropy = 8.0f;

struct RenderVertex {
    filament::math::float3 position;
    filament::math::float4 tangentFrame;
    filament::math::float2 uv;
    std::uint32_t color;
};

struct OverlayLineVertex {
    filament::math::float4 position;
    filament::math::float4 lineDistancePixels;
    std::uint32_t color;
};

struct OverlayPointVertex {
    filament::math::float4 position;
    filament::math::float2 uv;
    std::uint32_t color;
};

struct OverlayViewPoint {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

struct OverlayProjectedPoint {
    filament::math::float4 device;
    Vec2 pixel;
    bool valid = false;
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

std::uint64_t overlayFrameSignature(const ViewportOverlayFrame& frame) {
    std::uint64_t hash = hashCombine(kFnvOffset, frame.triangles.size());
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        hash = hashCombine(hash, static_cast<std::uint32_t>(triangle.layer));
        hash = hashCombine(hash, triangle.color);
        hash = hashVec3(hash, triangle.a);
        hash = hashVec3(hash, triangle.b);
        hash = hashVec3(hash, triangle.c);
    }
    hash = hashCombine(hash, frame.lines.size());
    for (const ViewportOverlayLine& line : frame.lines) {
        hash = hashCombine(hash, static_cast<std::uint32_t>(line.layer));
        hash = hashCombine(hash, line.color);
        hash = hashFloat(hash, line.widthPixels);
        hash = hashVec3(hash, line.start);
        hash = hashVec3(hash, line.end);
    }
    hash = hashCombine(hash, frame.points.size());
    for (const ViewportOverlayPoint& point : frame.points) {
        hash = hashCombine(hash, static_cast<std::uint32_t>(point.layer));
        hash = hashCombine(hash, point.color);
        hash = hashFloat(hash, point.sizePixels);
        hash = hashVec3(hash, point.position);
    }
    return hash;
}

std::uint64_t overlayCameraSignature(const ViewportOverlayCamera& camera) {
    std::uint64_t hash = hashCombine(kFnvOffset, static_cast<std::uint32_t>(camera.projection));
    hash = hashVec3(hash, camera.eye);
    hash = hashVec3(hash, camera.forward);
    hash = hashVec3(hash, camera.right);
    hash = hashVec3(hash, camera.up);
    hash = hashFloat(hash, camera.viewportSize.x);
    hash = hashFloat(hash, camera.viewportSize.y);
    hash = hashFloat(hash, camera.orthographicSize);
    hash = hashFloat(hash, camera.fovDegrees);
    hash = hashFloat(hash, camera.nearPlane);
    hash = hashFloat(hash, camera.farPlane);
    return hash;
}

std::uint64_t overlayLayerSignature(const ViewportOverlayFrame& frame, ViewportOverlayLayer layer) {
    std::uint64_t hash = hashCombine(kFnvOffset, static_cast<std::uint32_t>(layer));
    for (const ViewportOverlayTriangle& triangle : frame.triangles) {
        if (triangle.layer != layer) {
            continue;
        }
        hash = hashCombine(hash, static_cast<std::uint32_t>(triangle.layer));
        hash = hashCombine(hash, triangle.color);
        hash = hashVec3(hash, triangle.a);
        hash = hashVec3(hash, triangle.b);
        hash = hashVec3(hash, triangle.c);
    }
    for (const ViewportOverlayLine& line : frame.lines) {
        if (line.layer != layer) {
            continue;
        }
        hash = hashCombine(hash, static_cast<std::uint32_t>(line.layer));
        hash = hashCombine(hash, line.color);
        hash = hashFloat(hash, line.widthPixels);
        hash = hashVec3(hash, line.start);
        hash = hashVec3(hash, line.end);
    }
    for (const ViewportOverlayPoint& point : frame.points) {
        if (point.layer != layer) {
            continue;
        }
        hash = hashCombine(hash, static_cast<std::uint32_t>(point.layer));
        hash = hashCombine(hash, point.color);
        hash = hashFloat(hash, point.sizePixels);
        hash = hashVec3(hash, point.position);
    }
    return hash;
}

filament::math::float3 directionFromMatrix(const Mat4& transform) {
    return toFilament(normalize(transformVector(transform, {0.0f, -1.0f, 1.0f})));
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

filament::Box overlayMeshBounds() {
    filament::Box box;
    const filament::math::float3 halfExtent{500000.0f, 500000.0f, 500000.0f};
    box.set(-halfExtent, halfExtent);
    return box;
}

void appendOverlayTriangle(std::vector<RenderVertex>& vertices, const ViewportOverlayTriangle& triangle) {
    const std::uint32_t linearColor = rgba8SrgbToLinear(triangle.color);
    vertices.push_back({toFilament(triangle.a), {}, {}, linearColor});
    vertices.push_back({toFilament(triangle.b), {}, {}, linearColor});
    vertices.push_back({toFilament(triangle.c), {}, {}, linearColor});
}

ViewportOverlayCamera sanitizedOverlayCamera(ViewportOverlayCamera camera) {
    camera.forward = normalize(camera.forward);
    if (length(camera.forward) <= 0.000001f) {
        camera.forward = {0.0f, 0.0f, -1.0f};
    }
    camera.right = normalize(camera.right);
    if (length(camera.right) <= 0.000001f || std::abs(dot(camera.right, camera.forward)) > 0.999f) {
        const Vec3 reference = std::abs(camera.forward.y) < 0.999f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
        camera.right = normalize(cross(camera.forward, reference));
    }
    camera.up = normalize(cross(camera.right, camera.forward));
    if (length(camera.up) <= 0.000001f) {
        camera.up = {0.0f, 1.0f, 0.0f};
    }
    camera.viewportSize.x = std::max(camera.viewportSize.x, 1.0f);
    camera.viewportSize.y = std::max(camera.viewportSize.y, 1.0f);
    camera.orthographicSize = std::max(camera.orthographicSize, 0.0001f);
    camera.fovDegrees = std::clamp(camera.fovDegrees, 1.0f, 170.0f);
    camera.nearPlane = std::max(camera.nearPlane, 0.0001f);
    camera.farPlane = std::max(camera.farPlane, camera.nearPlane + 0.001f);
    return camera;
}

OverlayViewPoint viewPointFor(const ViewportOverlayCamera& camera, Vec3 world) {
    const Vec3 relative = world - camera.eye;
    const float depth = dot(relative, camera.forward);
    return {
            .x = dot(relative, camera.right),
            .y = dot(relative, camera.up),
            .z = -depth,
    };
}

OverlayViewPoint lerpViewPoint(OverlayViewPoint a, OverlayViewPoint b, float t) {
    return {
            .x = a.x + (b.x - a.x) * t,
            .y = a.y + (b.y - a.y) * t,
            .z = a.z + (b.z - a.z) * t,
    };
}

void applyOverlayDepthBias(OverlayViewPoint& point, float depthBias) {
    if (depthBias > 0.0f && point.z < -0.0001f) {
        point.z += std::min(depthBias, -point.z * 0.5f);
    }
}

float nearPlaneDistance(const OverlayViewPoint& point, const ViewportOverlayCamera& camera) {
    return -point.z - camera.nearPlane;
}

OverlayProjectedPoint projectOverlayViewPoint(const ViewportOverlayCamera& camera, OverlayViewPoint point) {
    OverlayProjectedPoint projected;

    if (camera.projection == ViewportOverlayProjection::Perspective) {
        const float depth = -point.z;
        if (depth <= 0.000001f) {
            return projected;
        }
        const float aspect = camera.viewportSize.x / camera.viewportSize.y;
        const float tanHalfFov = std::tan((camera.fovDegrees * kPi / 180.0f) * 0.5f);
        projected.device = {
                point.x / (depth * tanHalfFov * aspect),
                point.y / (depth * tanHalfFov),
                1.0f - (2.0f * camera.nearPlane / depth),
                1.0f,
        };
    } else {
        const float aspect = camera.viewportSize.x / camera.viewportSize.y;
        const float halfHeight = camera.orthographicSize * 0.5f;
        const float halfWidth = halfHeight * aspect;
        projected.device = {
                point.x / halfWidth,
                point.y / halfHeight,
                (-2.0f / (camera.farPlane - camera.nearPlane)) * point.z -
                        ((camera.farPlane + camera.nearPlane) / (camera.farPlane - camera.nearPlane)),
                1.0f,
        };
    }

    const Vec2 ndc{projected.device.x, projected.device.y};
    projected.pixel = {
            (ndc.x * 0.5f + 0.5f) * camera.viewportSize.x,
            (ndc.y * 0.5f + 0.5f) * camera.viewportSize.y,
    };
    projected.valid = true;
    return projected;
}

filament::math::float4 withPixelOffset(filament::math::float4 device, Vec2 pixelOffset, const ViewportOverlayCamera& camera) {
    device.x += pixelOffset.x * 2.0f / camera.viewportSize.x;
    device.y += pixelOffset.y * 2.0f / camera.viewportSize.y;
    return device;
}

bool appendOverlayLineQuad(
        std::vector<OverlayLineVertex>& vertices,
        std::vector<std::uint32_t>& indices,
        const ViewportOverlayLine& line,
        const ViewportOverlayCamera& camera,
        float widthPixels,
        float depthBias) {
    const std::uint32_t linearColor = rgba8SrgbToLinear(line.color);
    OverlayViewPoint a = viewPointFor(camera, line.start);
    OverlayViewPoint b = viewPointFor(camera, line.end);
    applyOverlayDepthBias(a, depthBias);
    applyOverlayDepthBias(b, depthBias);

    const float aNear = nearPlaneDistance(a, camera);
    const float bNear = nearPlaneDistance(b, camera);
    if (aNear < 0.0f && bNear < 0.0f) {
        return false;
    }
    if (aNear < 0.0f || bNear < 0.0f) {
        const float t = std::clamp(aNear / (aNear - bNear), 0.0f, 1.0f);
        const OverlayViewPoint clipped = lerpViewPoint(a, b, t);
        if (aNear < 0.0f) {
            a = clipped;
        } else {
            b = clipped;
        }
    }

    const OverlayProjectedPoint projectedA = projectOverlayViewPoint(camera, a);
    const OverlayProjectedPoint projectedB = projectOverlayViewPoint(camera, b);
    if (!projectedA.valid || !projectedB.valid) {
        return false;
    }

    Vec2 pixelDirection = projectedB.pixel - projectedA.pixel;
    if ((pixelDirection.x * pixelDirection.x + pixelDirection.y * pixelDirection.y) <= 0.000001f) {
        pixelDirection = {1.0f, 0.0f};
    }
    const Vec2 pixelNormal = std::abs(pixelDirection.x) > std::abs(pixelDirection.y)
            ? Vec2{0.0f, 1.0f}
            : Vec2{1.0f, 0.0f};
    const float sideDistance = (std::max(widthPixels, 1.0f) + kOverlayLineFeatherPixels) * 0.5f;

    const std::uint32_t offset = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({withPixelOffset(projectedA.device, pixelNormal * -sideDistance, camera), {-sideDistance, 0.0f, 0.0f, 0.0f}, linearColor});
    vertices.push_back({withPixelOffset(projectedB.device, pixelNormal * -sideDistance, camera), {-sideDistance, 0.0f, 0.0f, 0.0f}, linearColor});
    vertices.push_back({withPixelOffset(projectedB.device, pixelNormal * sideDistance, camera), {sideDistance, 0.0f, 0.0f, 0.0f}, linearColor});
    vertices.push_back({withPixelOffset(projectedA.device, pixelNormal * sideDistance, camera), {sideDistance, 0.0f, 0.0f, 0.0f}, linearColor});
    indices.push_back(offset);
    indices.push_back(offset + 1u);
    indices.push_back(offset + 2u);
    indices.push_back(offset);
    indices.push_back(offset + 2u);
    indices.push_back(offset + 3u);
    return true;
}

bool appendOverlayPointQuad(
        std::vector<OverlayPointVertex>& vertices,
        std::vector<std::uint32_t>& indices,
        Vec3 position,
        std::uint32_t color,
        const ViewportOverlayCamera& camera,
        float sizePixels,
        float depthBias) {
    const std::uint32_t linearColor = rgba8SrgbToLinear(color);
    OverlayViewPoint center = viewPointFor(camera, position);
    applyOverlayDepthBias(center, depthBias);
    if (nearPlaneDistance(center, camera) < 0.0f) {
        return false;
    }

    const OverlayProjectedPoint projected = projectOverlayViewPoint(camera, center);
    if (!projected.valid) {
        return false;
    }

    const float halfSize = std::max(sizePixels, 1.0f) * 0.5f;
    const std::uint32_t offset = static_cast<std::uint32_t>(vertices.size());
    vertices.push_back({withPixelOffset(projected.device, {-halfSize, -halfSize}, camera), {0.0f, 0.0f}, linearColor});
    vertices.push_back({withPixelOffset(projected.device, {halfSize, -halfSize}, camera), {1.0f, 0.0f}, linearColor});
    vertices.push_back({withPixelOffset(projected.device, {halfSize, halfSize}, camera), {1.0f, 1.0f}, linearColor});
    vertices.push_back({withPixelOffset(projected.device, {-halfSize, halfSize}, camera), {0.0f, 1.0f}, linearColor});
    indices.push_back(offset);
    indices.push_back(offset + 1u);
    indices.push_back(offset + 2u);
    indices.push_back(offset);
    indices.push_back(offset + 2u);
    indices.push_back(offset + 3u);
    return true;
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

std::uint64_t renderMaterialRevision(const AssetRegistry& assets, const MaterialDocument* material) {
    if (!material) {
        return 0;
    }

    std::uint64_t hash = hashCombine(kFnvOffset, material->revision.value);
    std::vector<std::string> textureSlots;
    textureSlots.reserve(material->textureSlots.size());
    for (const auto& [slot, texture] : material->textureSlots) {
        textureSlots.push_back(slot);
    }
    std::sort(textureSlots.begin(), textureSlots.end());
    for (const std::string& slot : textureSlots) {
        const AssetId textureId = material->textureSlots.at(slot);
        hash = hashCombine(hash, std::hash<std::string>{}(slot));
        hash = hashCombine(hash, textureId);
        hash = hashCombine(hash, assets.textureRevision(textureId).value);
    }
    return hash;
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

std::uint64_t meshAssetSignature(std::uint64_t hash, const MeshAsset& mesh) {
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

std::uint64_t previewMeshSignature(
        const SceneRenderBridge::PreviewMesh& previewMesh,
        const AssetRegistry& assets) {
    const MaterialDocument* material = assets.material(previewMesh.material);
    std::uint64_t hash = hashCombine(kFnvOffset, previewMesh.sourceEntity);
    hash = hashCombine(hash, previewMesh.material);
    hash = hashCombine(hash, renderMaterialRevision(assets, material));
    for (const float value : previewMesh.transform.values) {
        hash = hashFloat(hash, value);
    }
    return meshAssetSignature(hash, previewMesh.mesh);
}

std::uint64_t previewMeshesSignature(
        const std::vector<SceneRenderBridge::PreviewMesh>& previewMeshes,
        const AssetRegistry& assets) {
    std::uint64_t hash = hashCombine(kFnvOffset, previewMeshes.size());
    for (const SceneRenderBridge::PreviewMesh& previewMesh : previewMeshes) {
        hash = hashCombine(hash, previewMeshSignature(previewMesh, assets));
    }
    return hash;
}

} // namespace

SceneRenderBridge::~SceneRenderBridge() {
    shutdown();
}

void SceneRenderBridge::initialize(filament::Engine& engine,
        filament::Scene& scene,
        filament::Material& meshMaterial,
        filament::Material& overlayMaterial,
        filament::Material& overlayLineMaterial,
        filament::Material& overlayPointMaterial,
        std::filesystem::path resourceRoot) {
    engine_ = &engine;
    scene_ = &scene;
    meshMaterial_ = &meshMaterial;
    overlayMaterial_ = &overlayMaterial;
    overlayLineMaterial_ = &overlayLineMaterial;
    overlayPointMaterial_ = &overlayPointMaterial;
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
    destroyToolPreviewMeshes();

    for (OverlaySlot& slot : overlaySlots_) {
        destroyOverlaySlot(slot);
    }

    for (auto& [id, proxy] : lightProxies_) {
        destroyLightProxy(proxy);
    }
    lightProxies_.clear();
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
    overlayLineMaterial_ = nullptr;
    overlayPointMaterial_ = nullptr;
    resourceRoot_.clear();
    syncedRevision_ = 0;
    syncedAssetRevision_ = 0;
    hasSynced_ = false;
}

void SceneRenderBridge::sync(const SceneDocument& document, const AssetRegistry& assets) {
    if (!engine_ || !scene_ || !meshMaterial_ || !overlayMaterial_ || !overlayLineMaterial_ || !overlayPointMaterial_) {
        return;
    }

    const bool documentUnchanged = hasSynced_ && syncedRevision_ == document.revision();
    const bool assetsUnchanged = hasSynced_ && syncedAssetRevision_ == assets.revision();
    if (documentUnchanged && assetsUnchanged) {
        return;
    }
    if (documentUnchanged && !renderAssetRevisionsChanged(document, assets)) {
        syncedAssetRevision_ = assets.revision();
        return;
    }

    rebuild(document, assets);
    syncedRevision_ = document.revision();
    syncedAssetRevision_ = assets.revision();
    hasSynced_ = true;
}

void SceneRenderBridge::setToolPreviewMeshes(std::vector<PreviewMesh> meshes, const AssetRegistry& assets) {
    if (!engine_ || !scene_ || !meshMaterial_) {
        return;
    }

    const std::uint64_t signature = previewMeshesSignature(meshes, assets);
    if (signature == toolPreviewMeshSignature_) {
        return;
    }

    destroyToolPreviewMeshes();
    toolPreviewMeshSignature_ = signature;
    toolPreviewMeshProxies_.reserve(meshes.size());
    for (const PreviewMesh& previewMesh : meshes) {
        if (previewMesh.mesh.vertices.empty() || previewMesh.mesh.indices.empty()) {
            continue;
        }
        const MaterialDocument* material = assets.material(previewMesh.material);
        toolPreviewMeshProxies_.push_back(createMeshProxy(
                previewMesh.sourceEntity,
                InvalidAssetId,
                previewMesh.mesh,
                previewMesh.material,
                material,
                assets,
                previewMesh.transform));
    }
}

void SceneRenderBridge::setOverlayFrame(ViewportOverlayFrame frame, ViewportOverlayCamera camera) {
    setOverlayFrame(0u, kViewportOverlayLayerMask, std::move(frame), camera);
}

void SceneRenderBridge::setOverlayFrame(
        std::size_t viewportIndex,
        std::uint8_t layerMask,
        ViewportOverlayFrame frame,
        ViewportOverlayCamera camera) {
    if (viewportIndex >= overlaySlots_.size()) {
        return;
    }

    if (layerMask == 0u) {
        layerMask = kViewportOverlayLayerMask;
    }

    OverlaySlot& slot = overlaySlots_[viewportIndex];
    const std::uint64_t signature = overlayFrameSignature(frame);
    camera = sanitizedOverlayCamera(camera);
    const std::uint64_t cameraSignature = overlayCameraSignature(camera);
    const bool cameraChanged = cameraSignature != slot.cameraSignature;
    const bool layerChanged = layerMask != slot.layerMask;
    if (signature == slot.frameSignature && !cameraChanged && !layerChanged) {
        slot.frame = std::move(frame);
        return;
    }

    slot.frame = std::move(frame);
    slot.frameSignature = signature;
    if (cameraChanged || layerChanged) {
        slot.camera = camera;
        slot.cameraSignature = cameraSignature;
        slot.layerMask = layerMask;
        for (OverlayProxyGroup& group : slot.proxyGroups) {
            group.signature = 0;
        }
    }
    if (engine_ && scene_ && overlayMaterial_ && overlayLineMaterial_ && overlayPointMaterial_) {
        rebuildOverlayProxies(slot);
    }
}

void SceneRenderBridge::rebuild(const SceneDocument& document, const AssetRegistry& assets) {
    std::unordered_set<EntityId> liveMeshEntities;
    std::unordered_set<EntityId> liveLightEntities;
    auto& entityManager = utils::EntityManager::get();

    for (const EntityId entityId : document.entities()) {
        if (!document.isEffectivelyVisible(entityId)) {
            continue;
        }

        const Mat4 worldTransform = document.worldTransformMatrix(entityId);

        if (const auto* meshComponent = document.mesh(entityId)) {
            const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
            if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
                if (auto existing = meshProxies_.find(entityId); existing != meshProxies_.end()) {
                    destroyMeshProxy(existing->second);
                    meshProxies_.erase(existing);
                }
                LOG_WARN("Skipping render entity {} because its mesh asset is missing or empty", entityId);
            } else {
                liveMeshEntities.insert(entityId);
                const MaterialComponent* materialComponent = document.material(entityId);
                const AssetId materialId = materialComponent ? materialComponent->material : InvalidAssetId;
                const MaterialDocument* material = materialComponent ? assets.material(materialComponent->material) : nullptr;
                const std::uint64_t materialRevision = renderMaterialRevision(assets, material);

                auto existing = meshProxies_.find(entityId);
                const bool rebuildProxy =
                        existing == meshProxies_.end() ||
                        existing->second.mesh != meshComponent->mesh ||
                        existing->second.meshRevision != mesh->revision.value ||
                        existing->second.material != materialId ||
                        existing->second.materialRevision != materialRevision;
                if (rebuildProxy) {
                    if (existing != meshProxies_.end()) {
                        destroyMeshProxy(existing->second);
                        meshProxies_.erase(existing);
                    }
                    MeshProxy proxy = createMeshProxy(entityId, meshComponent->mesh, *mesh, materialId, material, assets, worldTransform);
                    meshProxies_.emplace(entityId, std::move(proxy));
                } else {
                    updateMeshProxyTransform(existing->second, worldTransform);
                }
            }
        }

        if (const auto* light = document.light(entityId)) {
            liveLightEntities.insert(entityId);
            if (auto existing = lightProxies_.find(entityId); existing != lightProxies_.end()) {
                destroyLightProxy(existing->second);
                lightProxies_.erase(existing);
            }

            LightProxy proxy;
            proxy.entity = entityManager.create();
            proxy.sourceEntity = entityId;

            filament::LightManager::Builder(toFilamentLightType(light->type))
                    .color({light->color.x, light->color.y, light->color.z})
                    .intensity(light->intensity)
                    .direction(directionFromMatrix(worldTransform))
                    .castShadows(light->castsShadows)
                    .build(*engine_, proxy.entity);
            scene_->addEntity(proxy.entity);
            lightProxies_.emplace(entityId, proxy);
        }
    }

    for (auto it = meshProxies_.begin(); it != meshProxies_.end();) {
        if (!liveMeshEntities.contains(it->first)) {
            destroyMeshProxy(it->second);
            it = meshProxies_.erase(it);
        } else {
            ++it;
        }
    }

    for (auto it = lightProxies_.begin(); it != lightProxies_.end();) {
        if (!liveLightEntities.contains(it->first)) {
            destroyLightProxy(it->second);
            it = lightProxies_.erase(it);
        } else {
            ++it;
        }
    }
}

bool SceneRenderBridge::renderAssetRevisionsChanged(const SceneDocument& document, const AssetRegistry& assets) const {
    for (const EntityId entityId : document.entities()) {
        if (!document.isEffectivelyVisible(entityId)) {
            continue;
        }
        const MeshComponent* meshComponent = document.mesh(entityId);
        if (!meshComponent) {
            continue;
        }
        const MeshAsset* mesh = assets.mesh(meshComponent->mesh);
        if (!mesh || mesh->vertices.empty() || mesh->indices.empty()) {
            if (meshProxies_.contains(entityId)) {
                return true;
            }
            continue;
        }

        const auto existing = meshProxies_.find(entityId);
        if (existing == meshProxies_.end()) {
            return true;
        }
        const MaterialComponent* materialComponent = document.material(entityId);
        const AssetId materialId = materialComponent ? materialComponent->material : InvalidAssetId;
        const MaterialDocument* material = materialComponent ? assets.material(materialComponent->material) : nullptr;
        if (existing->second.mesh != meshComponent->mesh ||
                existing->second.meshRevision != mesh->revision.value ||
                existing->second.material != materialId ||
                existing->second.materialRevision != renderMaterialRevision(assets, material)) {
            return true;
        }
    }
    return false;
}

SceneRenderBridge::MeshProxy SceneRenderBridge::createMeshProxy(EntityId entityId,
        AssetId meshId,
        const MeshAsset& mesh,
        AssetId materialId,
        const MaterialDocument* material,
        const AssetRegistry& assets,
        const Mat4& worldTransform) {
    MeshProxy proxy;
    proxy.entity = utils::EntityManager::get().create();
    proxy.sourceEntity = entityId;
    proxy.mesh = meshId;
    proxy.meshRevision = mesh.revision.value;
    proxy.material = materialId;
    proxy.materialRevision = renderMaterialRevision(assets, material);

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
    proxy.vertexBuffer->setBufferAt(*engine_, 0, makeBufferDescriptor(vertices));

    proxy.indexBuffer = filament::IndexBuffer::Builder()
            .indexCount(static_cast<std::uint32_t>(mesh.indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::UINT)
            .build(*engine_);
    proxy.indexBuffer->setBuffer(*engine_, makeBufferDescriptor(mesh.indices));

    const std::vector<MeshPrimitiveSpan> primitiveSpans = renderPrimitiveSpansFor(mesh);
    proxy.materialSlots.reserve(primitiveSpans.size());
    proxy.materialInstances.reserve(primitiveSpans.size());
    filament::RenderableManager::Builder builder(primitiveSpans.size());
    for (std::size_t primitiveIndex = 0; primitiveIndex < primitiveSpans.size(); ++primitiveIndex) {
        const MeshPrimitiveSpan& primitive = primitiveSpans[primitiveIndex];
        filament::MaterialInstance* materialInstance = createMeshMaterialInstance(assets, material);
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
            .castShadows(false)
            .receiveShadows(false)
            .build(*engine_, proxy.entity);

    engine_->getTransformManager().create(proxy.entity);
    updateMeshProxyTransform(proxy, worldTransform);
    scene_->addEntity(proxy.entity);
    return proxy;
}

void SceneRenderBridge::updateMeshProxyTransform(const MeshProxy& proxy, const Mat4& worldTransform) {
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

filament::MaterialInstance* SceneRenderBridge::createMeshMaterialInstance(const AssetRegistry& assets, const MaterialDocument* material) {
    filament::TextureSampler sampler(
            filament::TextureSampler::MinFilter::LINEAR_MIPMAP_LINEAR,
            filament::TextureSampler::MagFilter::LINEAR,
            filament::TextureSampler::WrapMode::REPEAT);
    sampler.setAnisotropy(kMeshTextureAnisotropy);

    filament::MaterialInstance* materialInstance = meshMaterial_->createInstance();
    materialInstance->setCullingMode(filament::MaterialInstance::CullingMode::BACK);
    materialInstance->setParameter(MaterialTextureSlot::BaseColor,
            resolveTexture(assets, material, MaterialTextureSlot::BaseColor, fallbackAlbedoTexture_),
            sampler);
    materialInstance->setParameter(MaterialTextureSlot::Roughness,
            resolveTexture(assets, material, MaterialTextureSlot::Roughness, fallbackRoughnessTexture_),
            sampler);
    const Vec3 baseColor = material ? material->baseColor : Vec3{1.0f, 1.0f, 1.0f};
    materialInstance->setParameter("baseColorFactor", filament::math::float4{baseColor.x, baseColor.y, baseColor.z, 1.0f});
    materialInstance->setParameter("metallicFactor", material ? material->metallic : 0.0f);
    materialInstance->setParameter("roughnessFactor", material ? material->roughness : 1.0f);
    return materialInstance;
}

void SceneRenderBridge::rebuildOverlayProxies(OverlaySlot& slot) {
    if (!engine_ || !scene_ || !overlayMaterial_ || !overlayLineMaterial_ || !overlayPointMaterial_) {
        return;
    }

    auto configureOverlayMaterial = [](filament::MaterialInstance& material, const OverlayRenderState& state) {
        material.setColorWrite(state.colorWrite);
        material.setDepthWrite(state.depthWrite);
        material.setDepthCulling(state.depthCulling);
        material.setDepthFunc(state.depthFunc);
        material.setCullingMode(state.cullingMode);
        material.setTransparencyMode(state.transparencyMode);
        material.setPolygonOffset(state.polygonOffsetScale, state.polygonOffsetConstant);
    };

    auto makeSequentialIndices = [](std::size_t vertexCount) {
        std::vector<std::uint32_t> indices;
        indices.reserve(vertexCount);
        for (std::uint32_t index = 0; index < vertexCount; ++index) {
            indices.push_back(index);
        }
        return indices;
    };

    auto buildTriangleOverlay = [this, &slot, &configureOverlayMaterial, &makeSequentialIndices](
                                const std::vector<RenderVertex>& vertices,
                                const OverlayRenderState& state,
                                std::vector<OverlayProxy>& targetProxies) {
        if (vertices.empty()) {
            return;
        }

        std::vector<std::uint32_t> indices = makeSequentialIndices(vertices.size());

        OverlayProxy proxy;
        proxy.entity = utils::EntityManager::get().create();
        proxy.materialInstance = overlayMaterial_->createInstance();
        configureOverlayMaterial(*proxy.materialInstance, state);
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
                .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, proxy.vertexBuffer, proxy.indexBuffer)
                .material(0, proxy.materialInstance)
                .blendOrder(0, state.blendOrder)
                .globalBlendOrderEnabled(0, true)
                .layerMask(kViewportAllLayerMask, slot.layerMask)
                .boundingBox(overlayMeshBounds())
                .culling(false)
                .castShadows(false)
                .receiveShadows(false)
                .build(*engine_, proxy.entity);
        scene_->addEntity(proxy.entity);
        targetProxies.push_back(proxy);
    };

    auto buildLineOverlay = [this, &slot, &configureOverlayMaterial](
                                    const std::vector<OverlayLineVertex>& vertices,
                                    const std::vector<std::uint32_t>& indices,
                                    const OverlayRenderState& state,
                                    float widthPixels,
                                    float depthBias,
                                    std::vector<OverlayProxy>& targetProxies) {
        if (vertices.empty() || indices.empty()) {
            return;
        }

        OverlayProxy proxy;
        proxy.entity = utils::EntityManager::get().create();
        proxy.materialInstance = overlayLineMaterial_->createInstance();
        configureOverlayMaterial(*proxy.materialInstance, state);
        proxy.materialInstance->setParameter("lineWidth", widthPixels);
        proxy.materialInstance->setParameter("featherPixels", kOverlayLineFeatherPixels);
        (void)depthBias;
        proxy.vertexBuffer = filament::VertexBuffer::Builder()
                .vertexCount(static_cast<std::uint32_t>(vertices.size()))
                .bufferCount(1)
                .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT4, offsetof(OverlayLineVertex, position), sizeof(OverlayLineVertex))
                .attribute(filament::VertexAttribute::CUSTOM0, 0, AttributeType::FLOAT4, offsetof(OverlayLineVertex, lineDistancePixels), sizeof(OverlayLineVertex))
                .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayLineVertex, color), sizeof(OverlayLineVertex))
                .normalized(filament::VertexAttribute::COLOR)
                .build(*engine_);
        proxy.vertexBuffer->setBufferAt(*engine_, 0, makeBufferDescriptor(vertices));
        proxy.indexBuffer = filament::IndexBuffer::Builder()
                .indexCount(static_cast<std::uint32_t>(indices.size()))
                .bufferType(filament::IndexBuffer::IndexType::UINT)
                .build(*engine_);
        proxy.indexBuffer->setBuffer(*engine_, makeBufferDescriptor(indices));

        filament::RenderableManager::Builder(1)
                .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, proxy.vertexBuffer, proxy.indexBuffer)
                .material(0, proxy.materialInstance)
                .blendOrder(0, state.blendOrder)
                .globalBlendOrderEnabled(0, true)
                .layerMask(kViewportAllLayerMask, slot.layerMask)
                .boundingBox(overlayMeshBounds())
                .culling(false)
                .castShadows(false)
                .receiveShadows(false)
                .build(*engine_, proxy.entity);
        scene_->addEntity(proxy.entity);
        targetProxies.push_back(proxy);
    };

    auto buildPointOverlay = [this, &slot, &configureOverlayMaterial](
                                     const std::vector<OverlayPointVertex>& vertices,
                                     const std::vector<std::uint32_t>& indices,
                                     const OverlayRenderState& state,
                                     float pointSizePixels,
                                     float innerRadius,
                                     float depthBias,
                                     std::vector<OverlayProxy>& targetProxies) {
        if (vertices.empty() || indices.empty()) {
            return;
        }

        OverlayProxy proxy;
        proxy.entity = utils::EntityManager::get().create();
        proxy.materialInstance = overlayPointMaterial_->createInstance();
        configureOverlayMaterial(*proxy.materialInstance, state);
        proxy.materialInstance->setParameter("innerRadius", innerRadius);
        (void)pointSizePixels;
        (void)depthBias;
        proxy.vertexBuffer = filament::VertexBuffer::Builder()
                .vertexCount(static_cast<std::uint32_t>(vertices.size()))
                .bufferCount(1)
                .attribute(filament::VertexAttribute::POSITION, 0, AttributeType::FLOAT4, offsetof(OverlayPointVertex, position), sizeof(OverlayPointVertex))
                .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2, offsetof(OverlayPointVertex, uv), sizeof(OverlayPointVertex))
                .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4, offsetof(OverlayPointVertex, color), sizeof(OverlayPointVertex))
                .normalized(filament::VertexAttribute::COLOR)
                .build(*engine_);
        proxy.vertexBuffer->setBufferAt(*engine_, 0, makeBufferDescriptor(vertices));
        proxy.indexBuffer = filament::IndexBuffer::Builder()
                .indexCount(static_cast<std::uint32_t>(indices.size()))
                .bufferType(filament::IndexBuffer::IndexType::UINT)
                .build(*engine_);
        proxy.indexBuffer->setBuffer(*engine_, makeBufferDescriptor(indices));

        filament::RenderableManager::Builder(1)
                .geometry(0, filament::RenderableManager::PrimitiveType::TRIANGLES, proxy.vertexBuffer, proxy.indexBuffer)
                .material(0, proxy.materialInstance)
                .blendOrder(0, state.blendOrder)
                .globalBlendOrderEnabled(0, true)
                .layerMask(kViewportAllLayerMask, slot.layerMask)
                .boundingBox(overlayMeshBounds())
                .culling(false)
                .castShadows(false)
                .receiveShadows(false)
                .build(*engine_, proxy.entity);
        scene_->addEntity(proxy.entity);
        targetProxies.push_back(proxy);
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

    auto appendLinesForLayer = [](std::vector<const ViewportOverlayLine*>& lines,
                                        const ViewportOverlayFrame& frame,
                                        ViewportOverlayLayer layer) {
        for (const ViewportOverlayLine& line : frame.lines) {
            if (line.layer == layer) {
                lines.push_back(&line);
            }
        }
    };

    auto appendPointsForLayer = [](std::vector<const ViewportOverlayPoint*>& points,
                                         const ViewportOverlayFrame& frame,
                                         ViewportOverlayLayer layer) {
        for (const ViewportOverlayPoint& point : frame.points) {
            if (point.layer == layer) {
                points.push_back(&point);
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
        state.depthFunc = depthStamp ? filament::MaterialInstance::DepthFunc::GE :
                faceColorPass ? filament::MaterialInstance::DepthFunc::E :
                drawOnTop ? filament::MaterialInstance::DepthFunc::A :
                filament::MaterialInstance::DepthFunc::GE;
        const bool twoSidedTriangle = kind == ViewportOverlayGeometryKind::Triangles &&
                viewportOverlayTriangleIsTwoSided(layer);
        state.cullingMode = kind == ViewportOverlayGeometryKind::Triangles && !twoSidedTriangle
                ? filament::MaterialInstance::CullingMode::BACK
                : filament::MaterialInstance::CullingMode::NONE;
        state.transparencyMode = kind == ViewportOverlayGeometryKind::Triangles && twoSidedTriangle
                ? filament::MaterialInstance::TransparencyMode::TWO_PASSES_TWO_SIDES
                : kind == ViewportOverlayGeometryKind::Triangles
                ? filament::MaterialInstance::TransparencyMode::TWO_PASSES_ONE_SIDE
                : filament::MaterialInstance::TransparencyMode::DEFAULT;
        if (kind == ViewportOverlayGeometryKind::Triangles) {
            state.polygonOffsetScale = -1.0f;
            state.polygonOffsetConstant = -1.0f;
        }
        state.blendOrder = blendOrder;
        return state;
    };

    for (const ViewportOverlayLayer layer : kViewportOverlayDrawOrder) {
        const std::uint64_t layerSignature = overlayLayerSignature(slot.frame, layer);
        OverlayProxyGroup& group = slot.proxyGroups[static_cast<std::size_t>(layer)];
        if (group.signature == layerSignature) {
            continue;
        }

        for (OverlayProxy& proxy : group.proxies) {
            destroyOverlayProxy(proxy);
        }
        group.proxies.clear();
        group.signature = layerSignature;

        std::uint16_t blendOrder = static_cast<std::uint16_t>(viewportOverlayDrawOrder(layer) * 64);
        std::vector<RenderVertex> triangleVertices;
        triangleVertices.reserve(slot.frame.triangles.size() * 3u);
        appendTrianglesForLayer(triangleVertices, slot.frame, layer);
        if (!triangleVertices.empty()) {
            if (viewportOverlayLayerUsesDepthStampedFaceFill(layer)) {
                buildTriangleOverlay(triangleVertices,
                        makeOverlayState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++, true, false),
                        group.proxies);
                buildTriangleOverlay(triangleVertices,
                        makeOverlayState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++, false, true),
                        group.proxies);
            } else {
                buildTriangleOverlay(triangleVertices,
                        makeOverlayState(layer, ViewportOverlayGeometryKind::Triangles, blendOrder++),
                        group.proxies);
            }
        }

        struct LineBatchKey {
            std::uint32_t color = 0;
            float widthPixels = 0.0f;
        };

        std::vector<const ViewportOverlayLine*> lines;
        lines.reserve(slot.frame.lines.size());
        appendLinesForLayer(lines, slot.frame, layer);
        std::vector<LineBatchKey> lineKeys;
        for (const ViewportOverlayLine* line : lines) {
            const LineBatchKey key{
                    .color = line->color,
                    .widthPixels = viewportOverlayLineWidthPixels(layer, line->widthPixels),
            };
            const auto existing = std::find_if(lineKeys.begin(), lineKeys.end(), [&](const LineBatchKey& candidate) {
                return candidate.color == key.color && candidate.widthPixels == key.widthPixels;
            });
            if (existing == lineKeys.end()) {
                lineKeys.push_back(key);
            }
        }
        for (const LineBatchKey& key : lineKeys) {
            std::vector<OverlayLineVertex> lineVertices;
            std::vector<std::uint32_t> lineIndices;
            lineVertices.reserve(lines.size() * 4u);
            lineIndices.reserve(lines.size() * 6u);
            const float depthBias = viewportOverlayDepthBias(layer, ViewportOverlayGeometryKind::Lines);
            for (const ViewportOverlayLine* line : lines) {
                if (line->color == key.color && viewportOverlayLineWidthPixels(layer, line->widthPixels) == key.widthPixels) {
                    (void)appendOverlayLineQuad(lineVertices, lineIndices, *line, slot.camera, key.widthPixels, depthBias);
                }
            }
            buildLineOverlay(lineVertices,
                    lineIndices,
                    makeOverlayState(layer, ViewportOverlayGeometryKind::Lines, blendOrder++),
                    key.widthPixels,
                    depthBias,
                    group.proxies);
        }

        std::vector<const ViewportOverlayPoint*> points;
        points.reserve(slot.frame.points.size());
        appendPointsForLayer(points, slot.frame, layer);
        auto buildPointPass = [&](bool outline) {
            std::vector<float> pointSizes;
            for (const ViewportOverlayPoint* point : points) {
                const float baseSize = viewportOverlayPointSizePixels(layer, point->sizePixels);
                const bool outlineUsesNativeColor = layer == ViewportOverlayLayer::SnapTargetVertex;
                const float sizePixels = outline && !outlineUsesNativeColor
                        ? baseSize + viewportOverlayPointOutlineGrowthPixels(layer)
                        : baseSize;
                if (std::find(pointSizes.begin(), pointSizes.end(), sizePixels) == pointSizes.end()) {
                    pointSizes.push_back(sizePixels);
                }
            }
            for (const float sizePixels : pointSizes) {
                std::vector<OverlayPointVertex> pointVertices;
                std::vector<std::uint32_t> pointIndices;
                pointVertices.reserve(points.size() * 4u);
                pointIndices.reserve(points.size() * 6u);
                const float depthBias = viewportOverlayDepthBias(layer, ViewportOverlayGeometryKind::Points);
                for (const ViewportOverlayPoint* point : points) {
                    const float baseSize = viewportOverlayPointSizePixels(layer, point->sizePixels);
                    const bool outlineUsesNativeColor = layer == ViewportOverlayLayer::SnapTargetVertex;
                    const float pointSize = outline && !outlineUsesNativeColor
                            ? baseSize + viewportOverlayPointOutlineGrowthPixels(layer)
                            : baseSize;
                    if (pointSize != sizePixels) {
                        continue;
                    }
                    const std::uint32_t color = outline && !outlineUsesNativeColor
                            ? kOverlayVertexOutlineColor
                            : point->color;
                    (void)appendOverlayPointQuad(pointVertices, pointIndices, point->position, color, slot.camera, sizePixels, depthBias);
                }
                buildPointOverlay(pointVertices,
                        pointIndices,
                        makeOverlayState(layer, ViewportOverlayGeometryKind::Points, blendOrder++),
                        sizePixels,
                        outline && viewportOverlayPointIsOutlineOnly(layer) ? viewportOverlayPointInnerRadius(layer) : 0.0f,
                        depthBias,
                        group.proxies);
            }
        };
        if (viewportOverlayPointHasOutline(layer)) {
            buildPointPass(true);
        }
        if (!viewportOverlayPointIsOutlineOnly(layer)) {
            buildPointPass(false);
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

void SceneRenderBridge::destroyToolPreviewMeshes() {
    for (MeshProxy& proxy : toolPreviewMeshProxies_) {
        destroyMeshProxy(proxy);
    }
    toolPreviewMeshProxies_.clear();
    toolPreviewMeshSignature_ = 0;
}

void SceneRenderBridge::destroyOverlaySlot(OverlaySlot& slot) {
    for (OverlayProxyGroup& group : slot.proxyGroups) {
        for (OverlayProxy& proxy : group.proxies) {
            destroyOverlayProxy(proxy);
        }
        group.proxies.clear();
        group.signature = 0;
    }
    slot.frame = {};
    slot.frameSignature = overlayFrameSignature(slot.frame);
    slot.camera = {};
    slot.cameraSignature = 0;
    slot.layerMask = 0;
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
            .revision = texture->revision.value,
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
