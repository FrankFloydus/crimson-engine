#pragma once

#include "RenderTypes.h"
#include "assets/ModelAsset.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

struct SDL_Window;
namespace bgfx { struct CallbackI; }

namespace crimson {

struct MaterialTextures {
    bgfx::TextureHandle albedo{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle roughness{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle metallic{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle normal{BGFX_INVALID_HANDLE};
};

// Base color, packed metallic/roughness, normal, occlusion, emissive.
// Textures are borrowed from AssetLibrary; sampler overrides belong to each slot.
struct ModelMaterialTextures {
    std::array<bgfx::TextureHandle, 5> handles{{BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE,
        BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE}};
    std::array<std::uint32_t, 5> flags{{UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}};
};

inline constexpr std::size_t MaxDirectionalLights = 4;
inline constexpr std::size_t MaxPointLights = 16;

struct RenderModelInstance {
    std::uint64_t entityId{};
    ModelInstance model;
    const std::vector<ModelMaterialTextures>* textures{nullptr};
};

struct PickRequest {
    std::uint64_t generation{}, revision{}, serial{};
    std::uint32_t x{}, y{}; // Top-left viewport texture pixel, independent of desktop DPI.
};
struct PickResult {
    std::uint64_t generation{}, revision{}, serial{}, entityId{}; // Zero means background.
};

struct RenderScene {
    const SceneSettings& settings;
    MaterialTextures sphereTextures;
    const ModelInstance* model{nullptr};
    const std::vector<ModelMaterialTextures>* modelTextures{nullptr};
    std::span<const RenderModelInstance> instances;
    std::span<const DirectionalLight> directionalLights;
    std::span<const PointLight> pointLights;
    bool sceneMode{false}; // A scene with no instances is empty; legacy mode previews a sphere.
};

// Owns the bgfx lifetime. Destroy ImGui's GPU resources before this object.
// The application calls bgfx::frame exactly once after render and the tools UI.
class Renderer final {
public:
    explicit Renderer(SDL_Window* window, bgfx::CallbackI* callback = nullptr);
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    void resize(std::uint32_t width, std::uint32_t height);
    void resizeViewport(std::uint32_t width, std::uint32_t height);
    // Called at startup or on source assignment, outside ordinary frame submission.
    bool prepareEnvironment(const std::filesystem::path& projectRoot, const std::string& sourceId);
    const std::string& environmentStatus() const;
    // Transactional GPU preparation outside frame submission. Failed preparation
    // preserves cached assets. Instances with the same ID share one GPU bundle.
    bool prepareModel(const ModelAsset& model);
    void clearModel();
    const std::string& modelStatus() const;
    // ID picking is rendered only on demand, with the same transforms and alpha
    // cutouts as the visible model pass. Results are asynchronous and carry the
    // caller's scene/interaction tags; the caller must reject stale tags too.
    bool requestPick(const PickRequest& request);
    std::optional<PickResult> pollPick(std::uint32_t bgfxFrame);
    void cancelPicking();
    const std::string& pickingStatus() const;
    void render(const RenderScene& scene);
    void render(const SceneSettings& settings, const MaterialTextures& textures = {});
    bgfx::TextureHandle viewportTexture() const noexcept;
    std::uint32_t width() const noexcept;
    std::uint32_t height() const noexcept;
    bool drawable() const noexcept;
    const char* backendName() const noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace crimson
