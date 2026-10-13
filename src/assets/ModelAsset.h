#pragma once

#include "math/Math.h"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace crimson {

using ModelMatrix = Mat4;
inline constexpr ModelMatrix ModelIdentity{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
enum class ModelWrap { Repeat, Clamp, Mirror };
enum class ModelFilter { Linear, Nearest };

struct ModelTexture {
    std::string image; // Managed, project-relative image path; empty means no texture.
    uint32_t texCoord{};
    ModelWrap wrapU{ModelWrap::Repeat}, wrapV{ModelWrap::Repeat};
    ModelFilter minFilter{ModelFilter::Linear}, magFilter{ModelFilter::Linear};
};
struct ModelMaterial {
    std::string name;
    Vec4 baseColor{1,1,1,1}; // Linear factors, including alpha.
    float metallic{1}, roughness{1}, normalScale{1}, occlusionStrength{1};
    Vec3 emissive{0,0,0};
    bool alphaMask{false}, doubleSided{false};
    float alphaCutoff{0.5f};
    ModelTexture baseColorTexture, metallicRoughnessTexture, normalTexture, occlusionTexture, emissiveTexture;
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
    std::vector<std::string> images; // All managed package images, including unreferenced images.
    ModelBounds bounds;
    std::vector<std::string> warnings;
};
struct ModelInstance {
    std::shared_ptr<const ModelAsset> asset;
    ModelMatrix transform{ModelIdentity};
};

// CPU-only, transactional managed import. Throws a descriptive error on failure.
std::shared_ptr<ModelAsset> importModelPackage(const std::filesystem::path& source, const std::filesystem::path& projectRoot);
std::shared_ptr<ModelAsset> loadModelPackage(const std::filesystem::path& manifest, const std::filesystem::path& projectRoot);

} // namespace crimson
