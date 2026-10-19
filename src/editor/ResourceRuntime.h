#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>

namespace crimson {
class ResourceManager;class AssetLibrary;namespace shared { class Renderer; } using Renderer=shared::Renderer;class ImGuiBgfx;class EditorSession;
// Pre-frame publication/lifetime coordinator. App remains a frame-loop boundary.
class ResourceRuntime final {
public:
    ResourceRuntime(const std::filesystem::path&,ResourceManager&,AssetLibrary&,Renderer&,ImGuiBgfx&);
    ~ResourceRuntime();
    void preFrame(EditorSession*,std::uint32_t bgfxFrame);
    void acknowledge(std::span<const std::filesystem::path> ownWrites);
    void stop();
    std::string status() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
