#include "ResourceRuntime.h"
#include "ShaderRuntimePaths.h"
#include "EditorSession.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "resources/FileWatcher.h"
#include "resources/ShaderReloadSession.h"
#include "resources/Paths.h"
#include "ShaderSourceBaseline.h"
#include <algorithm>
#include <unordered_map>

namespace crimson {
namespace {
bool inside(const std::filesystem::path& file,const std::filesystem::path& folder) {
    const auto value=resourcePathKey(file),root=resourcePathKey(folder);
    return value==root || (value.size()>root.size() && value.compare(0,root.size(),root)==0 && value[root.size()]=='/');
}
ShaderCompileConfig shaderConfig(const std::filesystem::path& root) {
    const ShaderRuntimePaths live{CC_SHADER_SOURCE_DIR,CC_SHADER_INCLUDE_DIR},staged{CC_STAGED_SHADER_DIR,CC_STAGED_SHADER_INCLUDE_DIR};
#ifdef NDEBUG
    constexpr bool development=false;
#else
    constexpr bool development=true;
#endif
    std::error_code error;
    const bool liveComplete=std::filesystem::is_directory(live.sources,error)&&std::filesystem::is_directory(live.includes,error);
    const auto selected=selectShaderRuntimePaths(development,live,staged,liveComplete);
    return {std::filesystem::path(CC_SHADERC_PATH),selected.sources,selected.includes,root/"assets"/"cache"/"shaders"};
}
std::uint64_t fingerprint(const std::filesystem::path& file) {
    std::error_code error;if(!std::filesystem::is_regular_file(file,error))return 0;
    return fingerprintFiles({file});
}
}
struct ResourceRuntime::Impl {
    std::filesystem::path root;
    ResourceManager& resources;
    AssetLibrary& assets;
    Renderer& renderer;
    ShaderCompileConfig shaderCompile;
    FileWatcher watcher;
    ShaderReloadSession shaders;
    MaterialGraphPrograms materialPrograms;
    std::unordered_map<std::string,AssetKind> catalog;
    std::unordered_map<std::string,std::uint64_t> ownFingerprints;
    std::string error;
    bool stopped{};
    Impl(const std::filesystem::path& project,ResourceManager& manager,AssetLibrary& library,Renderer& graphics,ImGuiBgfx& tools)
        :root(std::filesystem::absolute(project).lexically_normal()),resources(manager),assets(library),renderer(graphics),
        shaderCompile(shaderConfig(root)),watcher(shaderRuntimeWatchedRoots(root,{shaderCompile.sourceDirectory,shaderCompile.includeDirectory})),
        shaders(manager,graphics,tools,shaderCompile,EmbeddedShaderSourceBaseline),materialPrograms(manager,shaderCompile) {
        assets.setMaterialGraphPrograms(&materialPrograms);
        for(const auto& entry:assets.catalog())catalog.emplace(entry.id,entry.kind);
    }
    void acknowledgeFile(const std::filesystem::path& file) {
        try {
            // Large imported buffers/images already have unique managed identities;
            // native stamp acknowledgement avoids re-reading them on the UI thread.
            // Small authored metadata additionally suppresses identical-content writes.
            std::error_code ignored;
            const auto key=resourcePathKey(file);
            if(!std::filesystem::is_regular_file(file,ignored) || std::filesystem::file_size(file,ignored)<=64*1024)
                ownFingerprints[key]=fingerprint(file);
            else ownFingerprints.erase(key);
            watcher.acknowledge(file);
        }
        catch(const std::exception& exception) {error=exception.what();}
    }
    void acknowledgeAsset(const AssetEntry& entry) {
        const auto file=root/pathFromUtf8(entry.id);
        if(!inside(file,root/"assets"))return;
        acknowledgeFile(file);
        if(entry.kind==AssetKind::Model && std::filesystem::is_directory(file.parent_path()))
            for(const auto& dependency:std::filesystem::recursive_directory_iterator(file.parent_path()))
                if(dependency.is_regular_file())acknowledgeFile(dependency.path());
    }
    // Source creation through imports/save/promotion updates the catalog before
    // native notification processing. External creation enters through the watcher.
    void observeCatalog(bool ownChanges) {
        std::unordered_map<std::string,AssetKind> next;
        for(const auto& entry:assets.catalog()) {
            next.emplace(entry.id,entry.kind);
            if(ownChanges && !catalog.contains(entry.id))acknowledgeAsset(entry);
        }
        for(const auto& [id,kind]:catalog)if(!next.contains(id)) {
            renderer.evictThumbnail(id);if(kind==AssetKind::Model)renderer.evictModel(id);
            if(ownChanges)acknowledgeFile(root/pathFromUtf8(id));
        }
        catalog=std::move(next);
    }
    bool ownChange(const std::filesystem::path& file) {
        const auto found=ownFingerprints.find(resourcePathKey(file));
        if(found==ownFingerprints.end())return false;
        try {
            std::error_code ignored;
            if(std::filesystem::is_regular_file(file,ignored) && std::filesystem::file_size(file,ignored)>64*1024) {ownFingerprints.erase(found);return false;}
            if(found->second==fingerprint(file))return true;
            ownFingerprints.erase(found);
        } catch(const std::exception& exception) {error=exception.what();}
        return false;
    }
};
ResourceRuntime::ResourceRuntime(const std::filesystem::path& root,ResourceManager& manager,AssetLibrary& assets,Renderer& renderer,ImGuiBgfx& tools)
    :impl_(std::make_unique<Impl>(root,manager,assets,renderer,tools)) {}
ResourceRuntime::~ResourceRuntime(){stop();impl_->assets.setMaterialGraphPrograms(nullptr);}
void ResourceRuntime::preFrame(EditorSession* editor,std::uint32_t frame) {
    if(impl_->stopped)return;
    impl_->resources.pump();
    // This coordinator observes current snapshots directly. Consume notification
    // metadata each frame so long editor sessions cannot accumulate an event log.
    (void)impl_->resources.takePublications();
    acknowledge(impl_->assets.takeOwnWrites());
    impl_->observeCatalog(true);
    auto changed=impl_->watcher.poll();
    std::erase_if(changed,[&](const auto& file){return impl_->ownChange(file);});
    std::vector<std::filesystem::path> assetChanges;
    for(const auto& file:changed)if(inside(file,impl_->root/"assets") && !inside(file,impl_->root/"assets"/"branding")) {
        impl_->assets.onFileChanged(file);assetChanges.push_back(file);
    }
    // Catalog entries discovered by external notifications are not own writes.
    impl_->observeCatalog(false);
    if(editor && !assetChanges.empty())editor->filesChanged(assetChanges);
    impl_->shaders.changed(changed);
    impl_->materialPrograms.changed(changed);
    for(const auto& file:changed)if(!inside(file,impl_->root/"assets"))impl_->resources.invalidatePath(file);
    impl_->materialPrograms.trim();
    impl_->assets.processImports();
    impl_->assets.updateResources(frame);
    impl_->observeCatalog(true);
    impl_->shaders.pump();
    if(editor) { editor->setResourceStatus(status());editor->prepareFrame(impl_->assets,impl_->renderer,frame); }
}
void ResourceRuntime::acknowledge(std::span<const std::filesystem::path> paths) {for(const auto& path:paths)impl_->acknowledgeFile(path);}
void ResourceRuntime::stop() {
    if(!impl_ || impl_->stopped)return;
    impl_->stopped=true;impl_->watcher.stop();impl_->resources.stop();
}
std::string ResourceRuntime::status() const {
    std::string result=impl_->error;const auto watcher=impl_->watcher.diagnostic();
    const auto add=[&](const std::string& message){if(!message.empty()){if(!result.empty())result+='\n';result+=message;}};
    add(watcher);
    if(impl_->shaders.status()!="Shader programs reloaded.")add(impl_->shaders.status());
    return result;
}
}
