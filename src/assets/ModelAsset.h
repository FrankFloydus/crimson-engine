#pragma once

#include "math/Math.h"
#include "resources/AssetRef.h"
#include "ModelImportIdentity.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {
struct DecodedImage;
struct MaterialGraph;
namespace streaming { struct ModelStream; }

using ModelMatrix = Mat4;
inline constexpr ModelMatrix ModelIdentity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
enum class ModelWrap { Repeat, Clamp, Mirror };
enum class ModelFilter { Linear, Nearest, NearestMipmapNearest, LinearMipmapNearest, NearestMipmapLinear, LinearMipmapLinear };

struct ModelSampler {
    ModelWrap wrapU{ModelWrap::Repeat}, wrapV{ModelWrap::Repeat};
    ModelFilter minFilter{ModelFilter::Linear}, magFilter{ModelFilter::Linear};
    bool operator==(const ModelSampler&) const = default;
};
struct ModelTexture {
    AssetRef<DecodedImage> image; // Managed image identity; no loaded-data ownership.
    uint32_t texCoord{};
    ModelWrap wrapU{ModelWrap::Repeat}, wrapV{ModelWrap::Repeat};
    ModelFilter minFilter{ModelFilter::Linear}, magFilter{ModelFilter::Linear};
};
struct ModelMaterial {
    std::string name;
    Vec4 baseColor{1,1,1,1}; // Linear factors, including alpha.
    float metallic{1}, roughness{1}, normalScale{1}, occlusionStrength{1};
    float specular{.5f}; // Derived graph transport; imported materials default to dielectric F0 .04.
    Vec3 emissive{0,0,0};
    bool alphaMask{false}, doubleSided{false};
    float alphaCutoff{0.5f};
    ModelTexture baseColorTexture, metallicRoughnessTexture, normalTexture, occlusionTexture, emissiveTexture;
    Vec3 uvScale{1,1,1}, uvOffset{}, uvRotation{}; // Rotation in degrees; ordinary UVs use Z.
    bool triplanar{false}, uvScaleLinked{true};
    // Missing only for preserved imported/legacy per-slot sampler settings.
    std::optional<ModelSampler> sharedSampler;
    bool triplanarWorld{false}; // World-anchored projection; otherwise mesh-local coordinates.
    // Derived runtime transport only; imported model metadata remains unchanged.
    std::shared_ptr<const MaterialGraph> graph;
};
struct ModelVertex {
    Vec3 position{}, normal{0,1,0};
    Vec2 uv0{}, uv1{};
    Vec4 tangent{1,0,0,1}, color{1,1,1,1};
};
struct ModelPrimitive {
    std::vector<ModelVertex> vertices;
    std::vector<uint32_t> indices;
    uint32_t material{};
    uint8_t uvMask{}; // Authored UV availability; default-valued GPU fields do not imply a present set.
    uint8_t tangentUv{0xff}; // UV basis for authored/generated tangent data, or unknown.
    uint64_t sourceVertexCount{}, sourceIndexCount{};
};
struct ModelMesh { std::string name; std::vector<uint32_t> primitives; };
struct ModelNode {
    std::string name;
    ModelMatrix local{ModelIdentity};
    std::vector<uint32_t> children;
    int32_t mesh{-1};
};
struct ModelDraw { uint32_t primitive{}; ModelMatrix transform{ModelIdentity}; };
using ModelBounds = Aabb;
struct ModelAsset {
    std::string id, name, format;
    std::vector<ModelPrimitive> primitives;
    std::vector<ModelMesh> meshes;
    std::vector<ModelNode> nodes;
    std::vector<uint32_t> roots;
    std::vector<ModelDraw> draws;
    std::vector<ModelMaterial> materials;
    std::vector<std::string> materialIds; // Stable imported material asset identities, parallel to materials.
    std::vector<std::string> images; // All managed package images, including unreferenced images.
    ModelBounds bounds;
    std::vector<std::string> warnings;
    std::shared_ptr<const streaming::ModelStream> stream;
};
struct ModelInstance {
    std::shared_ptr<const ModelAsset> asset;
    ModelMatrix transform{ModelIdentity};
};

// CPU-only, transactional managed import. Throws a descriptive error on failure.
struct ModelImportStage {
    std::filesystem::path root,staging;
    mutable std::filesystem::path destination;
    std::string contentIdentity,sourceName;
    std::vector<ModelImportFileStamp> reuseFiles,inputFiles;
    std::shared_ptr<ModelAsset> asset;
    mutable bool published{},reused{};
    ModelImportStage()=default;
    ~ModelImportStage();
    ModelImportStage(const ModelImportStage&)=delete;
    ModelImportStage& operator=(const ModelImportStage&)=delete;
};
std::shared_ptr<ModelImportStage> stageModelImport(const std::filesystem::path& source,const std::filesystem::path& projectRoot);
std::shared_ptr<ModelAsset> publishModelImport(const std::shared_ptr<const ModelImportStage>& stage,
    const std::function<bool(const std::string&)>& reservedId = {});
std::shared_ptr<ModelAsset> importModelPackage(const std::filesystem::path& source, const std::filesystem::path& projectRoot);
struct ModelResolvers {
    // Empty means an intentionally deleted image using the engine's role-aware built-in fallback.
    std::function<std::string(const std::string&)> image;
    std::function<std::optional<ModelMaterial>(const std::string&)> material;
};
std::shared_ptr<ModelAsset> loadModelPackage(const std::filesystem::path& manifest, const std::filesystem::path& projectRoot,
                                           const ModelResolvers& resolvers = {});
std::shared_ptr<ModelAsset> loadModelMaterialMetadata(const std::filesystem::path& manifest,const std::filesystem::path& projectRoot,
                                                    const ModelResolvers& resolvers = {});

} // namespace crimson
