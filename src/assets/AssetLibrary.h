#pragma once

#include "crimson-renderer/Renderer.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

struct SDL_Window;

namespace crimson {

enum class MaterialSlot { Albedo, Roughness, Metallic, Normal };

struct TextureAsset {
    std::string id; // Project-relative UTF-8 path, e.g. assets/textures/stone.png.
    std::string name;
    uint32_t width{}, height{};
    bool hdr{false}; // Radiance HDR images are environment sources, not material data maps.
    bgfx::TextureHandle thumbnail{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle colorTexture{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle dataTexture{BGFX_INVALID_HANDLE};
    bool colorAttempted{false}, dataAttempted{false};
};

class AssetLibrary {
public:
    explicit AssetLibrary(std::filesystem::path projectRoot);
    ~AssetLibrary();
    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;

    const std::vector<TextureAsset>& assets() const { return assets_; }
    const TextureAsset* find(const std::string& id) const;
    const TextureAsset* selected() const { return find(selectedId_); }
    void select(const std::string& id) { selectedId_ = id; }
    const std::string& status() const { return status_; }

    void requestImport(SDL_Window* window);
    void processImports();
    const TextureAsset* importTexture(const std::filesystem::path& source);
    bool assign(Material& material, MaterialSlot slot, const std::string& id);
    void clear(Material& material, MaterialSlot slot);
    MaterialTextures bindings(const Material& material);
    void loadMaterial(Material& material);
    void saveMaterial(const Material& material);
    bool assignEnvironment(EnvironmentSettings& environment, const std::string& id);
    void clearEnvironment(EnvironmentSettings& environment);
    void loadEnvironment(EnvironmentSettings& environment);
    void saveEnvironment(const EnvironmentSettings& environment);

private:
    struct ImportQueue;
    std::shared_ptr<ImportQueue> imports_;
    std::filesystem::path projectRoot_;
    std::vector<TextureAsset> assets_;
    std::string selectedId_, status_;
    TextureAsset* findMutable(const std::string& id);
    TextureAsset makeAsset(const std::filesystem::path& path);
    bgfx::TextureHandle fullTexture(TextureAsset& asset, bool srgb);
};

} // namespace crimson
