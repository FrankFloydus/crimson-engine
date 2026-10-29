#include "ResourceRuntime.h"
#include "ShaderRuntimePaths.h"
#include "AssetSourceSnapshot.h"
#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/MaterialGraphGpu.h"
#include "resources/FileWatcher.h"
#include "resources/ShaderReloadSession.h"
#include "resources/Paths.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <algorithm>
#include <unordered_map>

namespace crimson {
namespace {
bool inside(const std::filesystem::path& file,const std::filesystem::path& folder) {
    const auto value=resourcePathKey(file),root=resourcePathKey(folder);
    return value==root || (value.size()>root.size() && value.compare(0,root.size(),root)==0 && value[root.size()]=='/');
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
    AssetCatalogSnapshot catalog;
    std::unordered_map<std::string,std::uint64_t> ownFingerprints;
    std::unordered_map<std::string,std::filesystem::path> locations;
    std::uint64_t storageRevision{};
    std::uint64_t catalogRevision{};
    std::string error;
    bool stopped{},storageBlocked{};
    Impl(const std::filesystem::path& project,ResourceManager& manager,AssetLibrary& library,Renderer& graphics,ShaderCompileConfig config,std::span<const ShaderSourceBaselineEntry> baseline)
        :root(std::filesystem::absolute(project).lexically_normal()),resources(manager),assets(library),renderer(graphics),
        shaderCompile(std::move(config)),watcher(shaderRuntimeWatchedRoots(root,{shaderCompile.sourceDirectory,shaderCompile.includeDirectory})),
        shaders(manager,graphics,shaderCompile,baseline),materialPrograms(manager,shaderCompile) {
        CRIMSON_PROFILE_ZONE("Resource runtime catalog registration");
        assets.setMaterialGraphPrograms(&materialPrograms);
        // Every cold preview descriptor needs this shared shader-family list.
        // Discover it once during resource initialization, before browser/UI
        // frames, rather than making the first visible card scan the family.
        {CRIMSON_PROFILE_ZONE("Material preview dependency preparation");(void)materialPrograms.sourceDependencies();}
        (void)catalog.update(root,assets.catalog());
        catalogRevision=assets.catalogRevision();
        storageRevision=contentStorageRevision(root);
        for(const auto& entry:storedAssetPaths(root))locations.emplace(entry.id,entry.file);
    }
    bool managedFile(const std::filesystem::path& file) const {
        if(contentStorageReady(root))return inside(file,root/"Content") || inside(file,root/"Engine") || inside(file,root/".crimson");
        return inside(file,root/"assets") && !inside(file,root/"assets"/"branding");
    }
    std::vector<std::filesystem::path> observeLocations() {
        const auto revision=contentStorageRevision(root);
        if(revision==storageRevision)return {};
        storageRevision=revision;
        std::unordered_map<std::string,std::filesystem::path> next;
        std::vector<std::filesystem::path> changed;
        for(const auto& entry:storedAssetPaths(root)) {
            next.emplace(entry.id,entry.file);
            const auto previous=locations.find(entry.id);
            if(previous==locations.end() || resourcePathKey(previous->second)==resourcePathKey(entry.file))continue;
            resources.invalidatePath(previous->second);resources.invalidate(entry.id);
            assets.onFileChanged(entry.file);changed.push_back(entry.file);
            acknowledgeFile(entry.file);
        }
        locations=std::move(next);
        return changed;
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
    void acknowledgeAsset(const AssetCatalogSource& entry) {
        const auto& file=entry.file;
        if(file.empty())return;
        if(!managedFile(file))return;
        acknowledgeFile(file);
        if(entry.kind==AssetKind::Model && std::filesystem::is_directory(file.parent_path()))
            for(const auto& dependency:std::filesystem::recursive_directory_iterator(file.parent_path()))
                if(dependency.is_regular_file())acknowledgeFile(dependency.path());
    }
    // Source creation through imports/save/promotion updates the catalog before
    // native notification processing. External creation enters through the watcher.
    void observeCatalog(bool ownChanges) {
        if(catalogRevision==assets.catalogRevision())return;
        const auto changes=catalog.update(root,assets.catalog());
        if(ownChanges)for(const auto& added:changes.added)acknowledgeAsset(added);
        for(const auto& removed:changes.removed) {
            renderer.evictThumbnail(removed.id);if(removed.kind==AssetKind::Model)renderer.evictModel(removed.id);
            if(ownChanges && !removed.file.empty())acknowledgeFile(removed.file);
        }
        catalogRevision=assets.catalogRevision();
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
ResourceRuntime::ResourceRuntime(const std::filesystem::path& root,ResourceManager& manager,AssetLibrary& assets,Renderer& renderer,ShaderCompileConfig config,std::span<const ShaderSourceBaselineEntry> baseline)
    :impl_(std::make_unique<Impl>(root,manager,assets,renderer,std::move(config),baseline)) {}
ResourceRuntime::~ResourceRuntime(){stop();impl_->assets.setMaterialGraphPrograms(nullptr);}
MaterialGraphPrograms& ResourceRuntime::materialPrograms(){return impl_->materialPrograms;}
std::vector<std::filesystem::path> ResourceRuntime::preFrame(std::uint32_t frame) {
    CRIMSON_PROFILE_ZONE("Resource runtime pre-frame");
    if(impl_->stopped)return {};
    // Native notifications are the storage refresh boundary. Steady frames use
    // the validated in-memory location snapshot without filesystem polling.
    auto changed=[&] {
        CRIMSON_PROFILE_ZONE("Resource file notification polling");
        auto paths=impl_->watcher.poll();
        std::erase_if(paths,[&](const auto& file){return impl_->ownChange(file);});
        return paths;
    }();
    bool refreshStorage=false;
    for(const auto& file:changed) {
        if(file==impl_->root/".crimson/asset-locations.json") {refreshStorage=true;break;}
        if(!inside(file,impl_->root/"Content")&&!inside(file,impl_->root/"Engine"))continue;
        std::error_code error;
        const auto state=std::filesystem::symlink_status(file,error);
        if(error || !std::filesystem::is_regular_file(state)) {
            refreshStorage=std::any_of(impl_->locations.begin(),impl_->locations.end(),[&](const auto& location){return inside(location.second,file);});
            if(refreshStorage)break;
        }
    }
    if(refreshStorage) {
        CRIMSON_PROFILE_ZONE("Content storage refresh");
        try {refreshContentStorage(impl_->root);impl_->storageBlocked=false;}
        catch(const std::exception& error) {impl_->error=error.what();impl_->storageBlocked=true;return {};}
    }
    if(impl_->storageBlocked)return {};
    auto relocated=[&]{CRIMSON_PROFILE_ZONE("Resource location observation");return impl_->observeLocations();}();
    impl_->resources.pump();
    // This coordinator observes current snapshots directly. Consume notification
    // metadata each frame so long editor sessions cannot accumulate an event log.
    (void)impl_->resources.takePublications();
    {CRIMSON_PROFILE_ZONE("Own asset write acknowledgement");acknowledge(impl_->assets.takeOwnWrites());}
    {CRIMSON_PROFILE_ZONE("Asset catalog observation");impl_->observeCatalog(true);}
    {
    CRIMSON_PROFILE_ZONE("External resource change publication");
    std::vector<std::filesystem::path> assetChanges;
    for(const auto& file:changed)if(impl_->managedFile(file)) {
        impl_->assets.onFileChanged(file);assetChanges.push_back(file);
    }
    // Catalog entries discovered by external notifications are not own writes.
    impl_->observeCatalog(false);
    relocated.insert(relocated.end(),assetChanges.begin(),assetChanges.end());
    impl_->shaders.changed(changed);
    impl_->materialPrograms.changed(changed);
    for(const auto& file:changed)if(!impl_->managedFile(file))impl_->resources.invalidatePath(file);
    }
    {CRIMSON_PROFILE_ZONE("Material shader retention");impl_->materialPrograms.trim();}
    {CRIMSON_PROFILE_ZONE("Import publication");impl_->assets.processImports();}
    impl_->assets.updateResources(frame);
    {CRIMSON_PROFILE_ZONE("Asset catalog observation");impl_->observeCatalog(true);}
    {CRIMSON_PROFILE_ZONE("Fixed shader publication");impl_->shaders.pump();}
    return relocated;
}
void ResourceRuntime::acknowledge(std::span<const std::filesystem::path> paths) {for(const auto& path:paths)impl_->acknowledgeFile(path);}
void ResourceRuntime::stop() {
    if(!impl_ || impl_->stopped)return;
    impl_->stopped=true;impl_->watcher.stop();
}
std::string ResourceRuntime::status() const {
    std::string result=impl_->error;const auto watcher=impl_->watcher.diagnostic();
    const auto add=[&](const std::string& message){if(!message.empty()){if(!result.empty())result+='\n';result+=message;}};
    add(watcher);
    if(impl_->shaders.status()!="Shader programs reloaded.")add(impl_->shaders.status());
    return result;
}
}
