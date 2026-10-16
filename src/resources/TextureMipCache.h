#pragma once
#include <cstdint>
#include <filesystem>
#include <span>
#include <vector>

namespace crimson {
struct DecodedImage { std::uint32_t width{}, height{}; std::vector<std::uint8_t> rgba; };
namespace streaming {
enum class TextureRole : std::uint32_t { Color, Data, Normal };
using TextureMip = DecodedImage;
struct MipInfo { std::uint32_t width{}, height{}; std::uint64_t offset{}, bytes{}, checksum{}; };
struct TextureMipCache {
    std::filesystem::path file;
    std::uint64_t key{};
    TextureRole role{TextureRole::Data};
    std::vector<MipInfo> levels;
    // Immutable in-memory source for generated images; no file/catalog entry.
    std::vector<TextureMip> memory;
};
inline constexpr std::uint32_t TextureCacheVersion = 1;
std::vector<TextureMip> generateTextureMips(std::uint32_t width, std::uint32_t height,
    std::span<const std::uint8_t> rgba, TextureRole);
TextureMipCache generatedTextureMipCache(std::uint64_t sourceKey,TextureMip image);
TextureMipCache generatedTextureThumbnail(const TextureMipCache& linear);
std::size_t textureMipCacheBytes(const TextureMipCache&);
TextureMipCache writeTextureMipCache(const std::filesystem::path&, std::uint64_t sourceKey,
    TextureRole, std::span<const TextureMip>);
TextureMipCache readTextureMipCache(const std::filesystem::path&, std::uint64_t sourceKey, TextureRole);
TextureMip readTextureMip(const TextureMipCache&, std::uint32_t level);
std::uint64_t textureContentKey(std::span<const std::uint8_t>, TextureRole);
std::uint32_t coarseTextureMip(std::span<const MipInfo>, std::uint32_t maxEdge = 64);
}}
