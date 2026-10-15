#pragma once

#include "math/Math.h"
#include <bgfx/bgfx.h>
#include <array>
#include <filesystem>
#include <memory>
#include <span>
#include <vector>

namespace crimson {
namespace ibl {

// CPU preprocessing is also used by the focused checks, without a bgfx context.
inline constexpr uint32_t PreprocessVersion = 3; // Solid-angle filtered source pyramid, K=2 overlap.
inline constexpr uint32_t BrdfVersion = 1; // Correlated Smith, alpha=r*r, r>=.045.
struct Settings {
    uint32_t diffuseSize{16}, specularSize{128}, lutSize{128};
    uint32_t diffuseSamples{512}, specularSamples{256}, lutSamples{1024};
    bool operator==(const Settings&) const = default;
};
struct Image {
    uint32_t width{}, height{};
    std::vector<Vec3> pixels; // Linear HDR, equirectangular, +Y at the top.
    Vec3 sample(const Vec3& direction) const;
};
// A linear-radiance pyramid, reduced with spherical-area weights rather than
// treating equirectangular pixels near the poles as equal-area pixels.
struct SourcePyramid {
    explicit SourcePyramid(const Image& source);
    std::vector<Image> levels;
    Vec3 sample(const Vec3& direction, float solidAngle) const;

private:
    std::vector<std::vector<float>> rowSolidAngles_;
};
struct CubeLevel {
    uint32_t size{};
    std::vector<float> rgba; // +X,-X,+Y,-Y,+Z,-Z; top-to-bottom face rows.
};
struct Bake {
    CubeLevel diffuse; // Irradiance/pi: a constant source remains constant.
    std::vector<CubeLevel> specular; // Perceptual roughness = .045 + .955*mip/(mipCount-1).
    std::vector<float> brdf; // RG: scale,bias; x=NoV, y=perceptual roughness.
};

Vec3 cubeDirection(uint8_t face, float u, float v);
float equirectTexelSolidAngle(uint32_t width, uint32_t height, uint32_t row);
float cubeTexelSolidAngle(uint32_t size, uint32_t x, uint32_t y);
float ggxSampleSolidAngle(float roughness, float noH, uint32_t samples);
Image decode(std::span<const uint8_t> source); // HDR remains linear; LDR is decoded from sRGB.
std::array<float, 2> integrateBrdf(float noV, float roughness, uint32_t samples);
Vec3 integrateDiffuse(const Image& image, const Vec3& normal, uint32_t samples);
Vec3 prefilterSpecular(const Image& image, const Vec3& normal, float roughness, uint32_t samples);
Vec3 prefilterSpecular(const SourcePyramid& source, const Vec3& normal, float roughness,
                      uint32_t samples, float outputSolidAngle = 0);
Bake bake(const Image& image, const Settings& settings = {});
uint64_t cacheKey(std::span<const uint8_t> source, const Settings& settings = {},
                  uint32_t preprocessingVersion = PreprocessVersion, uint32_t brdfVersion = BrdfVersion);
bool readCache(const std::filesystem::path& path, uint64_t key, const Settings& settings, Bake& result);
bool writeCache(const std::filesystem::path& path, uint64_t key, const Settings& settings, const Bake& result);

} // namespace ibl

struct PreparedEnvironment {
    ibl::Bake data;
    ibl::Settings settings;
    std::uint64_t key{};
    std::string status;
};
// Worker-safe: source IO, decoding, preprocessing and derived-cache writes only.
std::shared_ptr<const PreparedEnvironment> prepareEnvironmentCpu(const std::filesystem::path&,const std::string&);

// Renderer-owned; prepare at initialization or on assignment, never inside render.
// Construction performs no GPU work. Destroy before bgfx::shutdown().
class EnvironmentLighting {
public:
    EnvironmentLighting() = default;
    ~EnvironmentLighting();
    EnvironmentLighting(const EnvironmentLighting&) = delete;
    EnvironmentLighting& operator=(const EnvironmentLighting&) = delete;
    bool prepare(const std::filesystem::path& projectRoot, const std::string& sourceId);
    bool publish(const PreparedEnvironment&);
    bgfx::TextureHandle diffuse() const { return diffuse_; }
    bgfx::TextureHandle specular() const { return specular_; }
    bgfx::TextureHandle brdf() const { return brdf_; }
    std::span<const float> brdfCpu() const { return brdfCpu_; }
    float maxSpecularLod() const { return maxSpecularLod_; }
    std::size_t residentBytes() const { return residentBytes_; }
    const std::string& status() const { return status_; }

private:
    bgfx::TextureHandle diffuse_{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle specular_{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle brdf_{BGFX_INVALID_HANDLE};
    std::vector<float> brdfCpu_;
    uint64_t key_{};
    float maxSpecularLod_{};
    std::size_t residentBytes_{};
    std::string status_;
};

} // namespace crimson
