#pragma once

#include "RenderTypes.h"

#include <bgfx/bgfx.h>

#include <cstdint>
#include <filesystem>
#include <memory>

struct SDL_Window;
namespace bgfx { struct CallbackI; }

namespace crimson {

struct MaterialTextures {
    bgfx::TextureHandle albedo{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle roughness{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle metallic{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle normal{BGFX_INVALID_HANDLE};
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
