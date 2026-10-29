#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include "resources/ShaderCompiler.h"
#include "resources/ShaderSourceBaseline.h"

namespace crimson {
class ResourceManager;class AssetLibrary;class MaterialGraphPrograms;namespace shared { class Renderer; } using Renderer=shared::Renderer;
// Resource publication boundary borrowed by the single Qt LevelSession.
class ResourceRuntime final {
public:
    ResourceRuntime(const std::filesystem::path&,ResourceManager&,AssetLibrary&,Renderer&,ShaderCompileConfig,
        std::span<const ShaderSourceBaselineEntry> embeddedBaseline={});
    ~ResourceRuntime();
    std::vector<std::filesystem::path> preFrame(std::uint32_t bgfxFrame);
    MaterialGraphPrograms& materialPrograms();
    void acknowledge(std::span<const std::filesystem::path> ownWrites);
    void stop();
    std::string status() const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
