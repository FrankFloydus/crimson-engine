#include "ShaderReloadSession.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <algorithm>
#include <array>
#include <stdexcept>

namespace crimson {
namespace {
bool inside(const std::filesystem::path& path,const std::filesystem::path& directory) {
    if (directory.empty()) return false;
    const auto file=resourcePathKey(path),root=resourcePathKey(directory);
    return file==root || (file.size()>root.size() && file.compare(0,root.size(),root)==0 &&
        (file[root.size()]=='/' || file[root.size()]=='\\'));
}
}
ShaderReloadSession::ShaderReloadSession(ResourceManager& resources,Renderer& renderer,ImGuiBgfx& tools,ShaderCompileConfig config)
    : resources_(resources),renderer_(renderer),tools_(tools),config_(std::move(config)) {
    available_=std::filesystem::is_regular_file(config_.compiler) && std::filesystem::is_directory(config_.sourceDirectory) &&
        std::filesystem::is_directory(config_.includeDirectory);
    if (available_) requestBatch();
    else status_="Shader hot reload is unavailable; embedded programs remain active.";
}
void ShaderReloadSession::requestBatch() {
    try {
        auto pendingDependencies=dependencies_;
        const auto names=engineShaderNames();
        for (const auto& name:names) pendingDependencies[name]=shaderDependencies(config_,name);
        const auto epoch=++epoch_;
        for (const auto& name:names) {
            ResourceRequestOptions options; options.priority=ResourcePriority::Background; options.forceReload=true;
            options.dependencies=pendingDependencies.at(name);
            const auto config=config_;
            stages_[name]=resources_.request<ShaderBinary>(AssetRef<ShaderBinary>{"engine-shader:"+name},
                [config,name,epoch](std::stop_token token) {
                    auto binary=std::make_shared<ShaderBinary>(compileShader(config,name,token)); binary->reloadEpoch=epoch;
                    return ResourceLoadResult<ShaderBinary>{binary,binary->bytes.size(),binary->fingerprint,binary->dependencies};
                },std::move(options));
        }
        dependencies_=std::move(pendingDependencies); pending_=true; status_="Compiling shader programs...";
    } catch (const std::exception& error) { pending_=false; status_="Shader reload: "+std::string(error.what()); }
}
void ShaderReloadSession::changed(std::span<const std::filesystem::path> paths) {
    if (!available_) return;
    const bool affected=std::any_of(paths.begin(),paths.end(),[&](const auto& path) {
        if (inside(path,config_.sourceDirectory) || inside(path,config_.includeDirectory) || resourcePathKey(path)==resourcePathKey(config_.compiler)) return true;
        return std::any_of(dependencies_.begin(),dependencies_.end(),[&](const auto& stage) {
            return std::any_of(stage.second.begin(),stage.second.end(),[&](const auto& dependency){return resourcePathKey(path)==resourcePathKey(dependency);});
        });
    });
    if (affected) requestBatch();
}
void ShaderReloadSession::pump() {
    if (!pending_) return;
    std::unordered_map<std::string,ResourceSnapshot<ShaderBinary>> ready;
    for (const auto& name:engineShaderNames()) {
        const auto found=stages_.find(name);
        if (found==stages_.end() || !found->second) { pending_=false; status_="Shader request could not be queued."; return; }
        const auto state=found->second.state();
        if (state==ResourceState::Failed) { pending_=false; status_=name+": "+found->second.diagnostic(); return; }
        const auto snapshot=found->second.snapshot();
        if (state!=ResourceState::Ready || !snapshot || snapshot->reloadEpoch!=epoch_) return;
        ready.emplace(name,snapshot);
    }
    struct Pair { RendererProgram program; const char* vertex; const char* fragment; };
    constexpr std::array<Pair,11> definitions{{
        {RendererProgram::Sphere,"vs_pbr","fs_pbr"},{RendererProgram::Model,"vs_model","fs_model"},
        {RendererProgram::Picking,"vs_pick","fs_pick"},{RendererProgram::Present,"vs_present","fs_present"},
        {RendererProgram::Grid,"vs_grid","fs_grid"},{RendererProgram::Encode,"vs_present","fs_encode"},
        {RendererProgram::Particle,"vs_particle","fs_particle"},{RendererProgram::Sky,"vs_present","fs_sky"},
        {RendererProgram::PointShadow,"vs_point_shadow","fs_point_shadow"},
        {RendererProgram::GlowExtract,"vs_present","fs_glow_extract"},{RendererProgram::GlowBlur,"vs_present","fs_glow_blur"}}};
    std::vector<RendererShaderPair> pairs; pairs.reserve(definitions.size());
    for (const auto& pair:definitions) pairs.push_back({pair.program,ready.at(pair.vertex)->bytes,ready.at(pair.fragment)->bytes});
    std::string diagnostic;
    const auto ui=tools_.prepareProgramReload(ready.at("vs_imgui")->bytes,ready.at("fs_imgui")->bytes,diagnostic);
    if (!bgfx::isValid(ui)) { pending_=false; status_="ImGui shader reload: "+diagnostic; return; }
    if (!renderer_.reloadPrograms(pairs,ready.at("cs_procedural_filter")->bytes)) {
        bgfx::destroy(ui); pending_=false; status_="Shader reload: "+renderer_.shaderStatus(); return;
    }
    // The renderer accepted its whole candidate group; this commit cannot fail.
    tools_.publishProgramReload(ui);
    for (const auto& [name,binary]:ready) dependencies_[name]=binary->dependencies;
    ++publishedRevision_; pending_=false; status_="Shader programs reloaded.";
}
} // namespace crimson
