#pragma once
#include "ShaderCompiler.h"
#include "ResourceManager.h"
#include "ShaderSourceBaseline.h"
#include <crimson/renderer/forward/ForwardTypes.hpp>
#include <functional>
#include <span>
#include <unordered_map>

namespace crimson {
namespace shared { class Renderer; } using Renderer=shared::Renderer;
struct ShaderReloadPrograms {
    std::span<const RendererShaderPair> graphics;
    std::span<const std::uint8_t> procedural,cluster,deformation,ssgiTrace,ssgiTemporal,ssgiFilter,ssgiPublish,ssaoEvaluate,ssaoPublish,refractionDownsample;
};
struct ShaderReloadHooks {
    std::function<ShaderBinary(const ShaderCompileConfig&,const std::string&,std::stop_token)> compile;
    std::function<bool(const ShaderReloadPrograms&,std::string& diagnostic)> publish;
};
// Coordinates CPU compilation and atomic publication to the existing GPU owners.
// Call changed() and pump() before constructing immutable render snapshots.
class ShaderReloadSession final {
public:
    ShaderReloadSession(ResourceManager&,Renderer&,ShaderCompileConfig,
        std::span<const ShaderSourceBaselineEntry> embeddedBaseline={});
    ShaderReloadSession(ResourceManager&,ShaderCompileConfig,ShaderReloadHooks,
        std::span<const ShaderSourceBaselineEntry> embeddedBaseline={});
    void changed(std::span<const std::filesystem::path> paths);
    void pump();
    const std::string& status() const { return status_; }
    bool available() const { return available_; }
    bool pending() const { return pending_; }
    uint64_t revision() const { return publishedRevision_; }
private:
    ResourceManager& resources_;
    ShaderReloadHooks hooks_;
    ShaderCompileConfig config_;
    std::unordered_map<std::string,ResourceHandle<ShaderBinary>> stages_;
    std::unordered_map<std::string,std::vector<std::filesystem::path>> dependencies_;
    uint64_t epoch_{},publishedRevision_{};
    bool available_{},pending_{};
    std::string status_;
    void requestBatch();
};
} // namespace crimson
