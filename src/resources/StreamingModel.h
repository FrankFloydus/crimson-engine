#pragma once
#include "assets/ModelAsset.h"
#include <filesystem>
#include <memory>
#include <span>

namespace crimson {
struct ModelGeometryLevel {
    std::uint32_t primitive{}, lodIndex{};
    ModelPrimitive geometry;
    float error{};
    std::vector<std::uint16_t> indices16; // Worker-prepared when every reference fits.
    bool wideIndices{false},validated{false};
};
namespace streaming {
inline constexpr std::uint32_t ModelCacheVersion = 4;
struct ModelLod {
    std::filesystem::path file;
    std::uint64_t key{};
    std::uint32_t vertexCount{}, indexCount{};
    float error{}; // Absolute object-space positional/attribute error.
};
struct PrimitiveLods {
    std::uint32_t material{};
    Aabb bounds;
    std::vector<ModelLod> levels; // Original first; smallest last.
    std::array<Vec2,2> uvSpan{{{1,1},{1,1}}};
};
struct ModelStream {
    std::string id;
    std::uint64_t key{};
    std::vector<PrimitiveLods> primitives;
};
// Worker-only CPU preprocessing. Each cache level can be read independently.
ModelStream buildModelCache(const ModelAsset&, const std::filesystem::path& projectRoot,bool writeMetadata = true);
// A valid warm cache yields hierarchy/material/primitive metadata without decoding geometry.
std::shared_ptr<ModelAsset> loadModelMetadataCache(const std::filesystem::path& manifest,const std::filesystem::path& projectRoot);
void writeModelMetadataCache(const ModelAsset&,const ModelStream&,const std::filesystem::path& projectRoot);
std::shared_ptr<const ModelGeometryLevel> loadModelLod(const ModelLod&, std::uint32_t primitive = 0, std::uint32_t level = 0);
std::uint32_t chooseModelLod(std::span<const ModelLod>, const Mat4& world,
    const Aabb& objectBounds, const Vec3& camera, float verticalFovRadians,
    std::uint32_t screenHeight, float allowedPixels = 1.0f);
std::uint64_t modelContentKey(const ModelAsset&);
}}
