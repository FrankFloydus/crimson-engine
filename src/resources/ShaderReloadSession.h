#pragma once
#include "ShaderCompiler.h"
#include "ResourceManager.h"
#include "ShaderSourceBaseline.h"
#include <span>
#include <unordered_map>

namespace crimson {
namespace shared { class Renderer; } using Renderer=shared::Renderer;
class ImGuiBgfx;
// Coordinates CPU compilation and atomic publication to the existing GPU owners.
// Call changed() and pump() before constructing ImGui/render frame snapshots.
class ShaderReloadSession final {
public:
    ShaderReloadSession(ResourceManager&,Renderer&,ImGuiBgfx&,ShaderCompileConfig,
        std::span<const ShaderSourceBaselineEntry> embeddedBaseline={});
    void changed(std::span<const std::filesystem::path> paths);
    void pump();
    const std::string& status() const { return status_; }
    bool available() const { return available_; }
    bool pending() const { return pending_; }
    uint64_t revision() const { return publishedRevision_; }
private:
    ResourceManager& resources_;
    Renderer& renderer_;
    ImGuiBgfx& tools_;
    ShaderCompileConfig config_;
    std::unordered_map<std::string,ResourceHandle<ShaderBinary>> stages_;
    std::unordered_map<std::string,std::vector<std::filesystem::path>> dependencies_;
    uint64_t epoch_{},publishedRevision_{};
    bool available_{},pending_{};
    std::string status_;
    void requestBatch();
};
} // namespace crimson
