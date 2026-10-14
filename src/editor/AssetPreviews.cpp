#include "AssetPreviews.h"
#include "EditorResources.h"
#include "assets/AssetLibrary.h"
#include "assets/MaterialPreviewGeometry.h"
#include "assets/AtomicFile.h"
#include "crimson-renderer/Environment.h"
#include "resources/ImageDecoder.h"
#include "resources/CacheIO.h"
#include "resources/Paths.h"
#include "scene/SceneExtraction.h"
#include "scene/SceneDocument.h"
#include "math/Json.h"
#include <lodepng.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <string_view>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
namespace {
constexpr uint32_t PreviewVersion=4,PreviewSize=512;
using streaming::detail::hash;
uint64_t textHash(std::string_view text) { auto result=streaming::detail::HashSeed; hash(result,text.data(),text.size()); return result; }
void materialHash(uint64_t& value,const ModelMaterial& material) {
    hash(value,material.baseColor); hash(value,material.metallic); hash(value,material.roughness); hash(value,material.normalScale);
    hash(value,material.occlusionStrength); hash(value,material.emissive); hash(value,material.alphaMask); hash(value,material.alphaCutoff); hash(value,material.doubleSided);
    for (const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
        hash(value,texture->image.id.data(),texture->image.id.size()); hash(value,texture->texCoord); hash(value,texture->wrapU); hash(value,texture->wrapV);
        hash(value,texture->minFilter); hash(value,texture->magFilter);
    }
}
std::vector<std::filesystem::path> uniqueFiles(std::vector<std::filesystem::path> files) {
    std::sort(files.begin(),files.end(),[](const auto& a,const auto& b){return resourcePathKey(a)<resourcePathKey(b);});
    files.erase(std::unique(files.begin(),files.end(),[](const auto& a,const auto& b){return resourcePathKey(a)==resourcePathKey(b);}),files.end()); return files;
}
struct PreviewPins {
    std::shared_ptr<const SceneSnapshot> scene;
    std::shared_ptr<const MaterialAsset> material;
    std::vector<std::shared_ptr<const PreparedEditorModel>> models;
    std::shared_ptr<const PreparedEnvironment> environment;
    std::shared_ptr<const PreparedParticleMaterial> particle;
};
bool texturesMatchSources(const AssetLibrary& assets,const std::filesystem::path& root,const PreviewDependencies& expected,
    std::span<const ModelMaterial> materials,std::span<const ModelMaterialTextures> bindings,ResourceManager& resources,
    std::unordered_map<std::string,uint64_t>& refreshedSources) {
    if (materials.size()!=bindings.size()) return false;
    for (size_t m=0;m<materials.size();++m) {
        const auto& material=materials[m]; size_t slot{};
        for (const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
            if (!texture->image.empty()) {
                const auto target=assets.references().resolveTexture(texture->image.id,texture==&material.normalTexture);
                if (!target.empty() && !target.starts_with("builtin:")) {
                    const auto file=root/pathFromUtf8(target);
                    if (!expected.sourceMatches(file,bindings[m].sourceFingerprints[slot])) {
                        // Source hashing can beat the native watcher's debounce.
                        // Request this exact changed source once, preserving the
                        // last GPU version while the new role payloads converge.
                        if (const auto fingerprint=expected.sourceFingerprint(file);fingerprint &&
                            refreshedSources[resourcePathKey(file)]!=*fingerprint) {
                            refreshedSources[resourcePathKey(file)]=*fingerprint; resources.invalidatePath(file);
                        }
                        return false;
                    }
                }
            }
            ++slot;
        }
    }
    return true;
}
}
struct AssetPreviews::Impl {
    struct Entry {
        AssetKind kind{AssetKind::Model};
        ResourceHandle<SceneSnapshot> scene;
        ResourceHandle<PreviewDependencies> dependencies;
        ResourceHandle<DecodedImage> cached,writer;
        std::shared_ptr<const DecodedImage> writePin;
        uint64_t sourceKey{},signature{},cacheChecked{},published{},rendering{};
        bool wanted{},forceScene{},saveRequested{},failed{};
        std::string error;
        std::chrono::steady_clock::time_point used{std::chrono::steady_clock::now()};
    };
    struct EnvironmentEntry {
        ResourceHandle<PreparedEnvironment> handle;
        std::chrono::steady_clock::time_point used{std::chrono::steady_clock::now()};
    };
    std::filesystem::path root;
    AssetLibrary& assets;
    ResourceManager& resources;
    Renderer& renderer;
    EditorResources& prepared;
    std::unordered_map<std::string,Entry> entries;
    std::unordered_map<std::string,EnvironmentEntry> environments;
    std::unordered_map<std::string,uint64_t> refreshedSources;
    uint64_t thumbnailEnvironment{};
    std::string status;
    Impl(std::filesystem::path directory,AssetLibrary& library,ResourceManager& manager,Renderer& gpu,EditorResources& models)
        :root(std::move(directory)),assets(library),resources(manager),renderer(gpu),prepared(models) {}
    std::filesystem::path cache(const std::string& id,uint64_t signature) const {
        return root/"assets"/"cache"/"previews"/(streaming::detail::hex(textHash(id))+"-"+streaming::detail::hex(signature)+".png");
    }
    void writePreview(const ThumbnailResult& result,Entry& entry) {
        const auto file=cache(result.id,result.signature); const auto metadata=std::filesystem::path(file.wstring()+L".json");
        const auto id=result.id; const auto signature=result.signature;
        entry.writePin=std::make_shared<DecodedImage>(result.image);
        const std::weak_ptr<const DecodedImage> weak=entry.writePin;
        ResourceRequestOptions options; options.variant="preview-write:"+std::to_string(signature); options.priority=ResourcePriority::Background;
        entry.writer=resources.request<DecodedImage>(AssetRef<DecodedImage>{id},[weak,file,metadata,id,signature](std::stop_token token) {
            const auto pixels=weak.lock(); if (!pixels || token.stop_requested()) throw std::runtime_error("Preview write canceled.");
            std::vector<unsigned char> encoded;
            const auto error=lodepng::encode(encoded,pixels->rgba,pixels->width,pixels->height);
            if (error) throw std::runtime_error(std::string("Cannot encode preview: ")+lodepng_error_text(error));
            if (token.stop_requested()) throw std::runtime_error("Preview write canceled.");
            writeFileAtomically(file,std::string(reinterpret_cast<const char*>(encoded.data()),encoded.size()));
            const nlohmann::json record{{"version",PreviewVersion},{"id",id},{"signature",signature},{"width",pixels->width},{"height",pixels->height}};
            writeFileAtomically(metadata,record.dump(2)+"\n");
            return ResourceLoadResult<DecodedImage>{pixels,pixels->rgba.size(),signature,{}};
        },std::move(options));
    }
    ResourceSnapshot<SceneSnapshot> scene(const std::string& id,Entry& entry) {
        if (!entry.scene || entry.forceScene) {
            ResourceRequestOptions options; options.variant="preview"; options.priority=ResourcePriority::Preview;
            options.dependencies={root/pathFromUtf8(id)}; options.forceReload=entry.forceScene;
            const auto project=root;
            entry.scene=resources.request<SceneSnapshot>(AssetRef<SceneSnapshot>{id},[project,id](std::stop_token token) {
                if (token.stop_requested()) throw std::runtime_error("Scene preview canceled.");
                auto snapshot=std::make_shared<SceneSnapshot>(readSceneSnapshot(project,id));
                return ResourceLoadResult<SceneSnapshot>{snapshot,sizeof(SceneSnapshot)+snapshot->records.size()*sizeof(EntityRecord),snapshot->fingerprint,{}};
            },std::move(options)); entry.forceScene=false;
        }
        if (entry.scene.state()==ResourceState::Failed) { entry.error=entry.scene.diagnostic(); return {}; }
        return entry.scene.state()==ResourceState::Ready ? entry.scene.snapshot() : ResourceSnapshot<SceneSnapshot>{};
    }
    std::shared_ptr<const PreparedEnvironment> environment(const std::string& source,Entry& entry) {
        const auto id=source.empty() ? std::string(StudioEnvironmentId) : source;
        auto& entryEnvironment=environments[id]; entryEnvironment.used=std::chrono::steady_clock::now();
        auto& handle=entryEnvironment.handle;
        if (!handle) {
            ResourceRequestOptions options; options.variant="ibl"; options.priority=ResourcePriority::Preview;
            if (!source.empty()) options.dependencies={root/pathFromUtf8(source)};
            const auto project=root;
            handle=resources.request<PreparedEnvironment>(AssetRef<PreparedEnvironment>{id},[project,source](std::stop_token token) {
                if (token.stop_requested()) throw std::runtime_error("Environment preview canceled.");
                auto bake=prepareEnvironmentCpu(project,source); size_t bytes=bake->data.diffuse.rgba.size()*sizeof(float)+bake->data.brdf.size()*sizeof(float);
                for (const auto& level:bake->data.specular) bytes+=level.rgba.size()*sizeof(float);
                return ResourceLoadResult<PreparedEnvironment>{bake,bytes,bake->key,{}};
            },std::move(options));
        }
        if (handle.state()==ResourceState::Failed) {
            entry.error=handle.diagnostic();
            return source.empty() ? nullptr : environment({},entry);
        }
        return handle.state()==ResourceState::Ready ? handle.snapshot().shared() : nullptr;
    }
    bool warmCache(const std::string& id,Entry& entry,uint64_t signature) {
        if (bgfx::isValid(renderer.thumbnailTexture(id,signature))) { entry.published=signature; return true; }
        if (entry.cacheChecked!=signature) {
            entry.cacheChecked=signature; entry.cached={};
            const auto file=cache(id,signature),metadata=std::filesystem::path(file.wstring()+L".json");
            if (std::filesystem::is_regular_file(file) && std::filesystem::is_regular_file(metadata)) {
                ResourceRequestOptions options; options.variant="preview-png:"+std::to_string(signature); options.priority=ResourcePriority::Preview;
                options.dependencies={file,metadata};
                entry.cached=resources.request<DecodedImage>(AssetRef<DecodedImage>{id},[file,metadata,id,signature](std::stop_token token) {
                    if (token.stop_requested()) throw std::runtime_error("Cached preview read canceled.");
                    std::ifstream input(metadata); const auto record=nlohmann::json::parse(input);
                    if (record.at("version")!=PreviewVersion || record.at("id")!=id || record.at("signature")!=signature)
                        throw std::runtime_error("Cached preview metadata is stale.");
                    auto image=std::make_shared<DecodedImage>(decodeManagedImage(file));
                    if (image->width!=PreviewSize || image->height!=PreviewSize) throw std::runtime_error("Cached preview dimensions are invalid.");
                    return ResourceLoadResult<DecodedImage>{image,image->rgba.size(),signature,{}};
                },std::move(options));
            }
        }
        if (entry.cached) {
            if (entry.cached.state()==ResourceState::Ready) {
                if (const auto pixels=entry.cached.snapshot()) {
                    if (renderer.setThumbnail(id,signature,*pixels)) { entry.published=signature; entry.error.clear(); entry.cached={}; return true; }
                }
            } else if (entry.cached.state()!=ResourceState::Failed) return true; // Wait; do not decode model geometry for a warm PNG.
            else { entry.error=entry.cached.diagnostic(); entry.cached={}; }
        }
        return false;
    }
    void request(const std::string& id,Entry& entry) {
        if (assets.references().deleted(id) || entry.failed) return;
        SceneSettings settings; settings.directional.enabled=false; for (auto& light:settings.points) light.enabled=false;
        std::vector<ExtractedModel> models; std::vector<DirectionalLight> directions; std::vector<PointLight> points;
        std::shared_ptr<const SceneSnapshot> sceneSnapshot; Aabb bounds;
        std::shared_ptr<const MaterialAsset> materialAsset;
        std::shared_ptr<const ModelAsset> materialGeometry;
        bool spriteMaterial=false;
        std::vector<std::filesystem::path> files;
        uint64_t sourceKey=streaming::detail::HashSeed; hash(sourceKey,PreviewVersion); hash(sourceKey,id.data(),id.size()); hash(sourceKey,renderer.shaderSignature());
        if (entry.kind==AssetKind::Scene) {
            const auto snapshot=scene(id,entry); if (!snapshot) return;
            sceneSnapshot=snapshot.shared(); hash(sourceKey,snapshot->fingerprint);
            Scene candidate; candidate.apply(snapshot->records,snapshot->properties); auto extracted=extractScene(candidate);
            models=std::move(extracted.models); directions=std::move(extracted.directional); points=std::move(extracted.points);
            settings.environment=extracted.environment.environment; settings.exposure=extracted.environment.exposure;
            if (snapshot->previewCamera) settings.camera=*snapshot->previewCamera;
        } else if (entry.kind==AssetKind::Material) {
            materialAsset=assets.findMaterial(id);
            if (!materialAsset) { entry.error="Material is unavailable."; return; }
            spriteMaterial=particleMaterialCompatible(*materialAsset);
            if (!spriteMaterial) {
                ModelAsset probe; probe.materialIds={id};
                if (!assets.modelMaterialsReady(probe)) {
                    const auto diagnostic=assets.modelMaterialsDiagnostic(probe);
                    if (!diagnostic.empty()) entry.error=diagnostic;
                    return;
                }
                materialGeometry=materialPreviewGeometry();
            }
            const uint32_t materialPreviewVersion=1; hash(sourceKey,materialPreviewVersion);
            hash(sourceKey,materialAsset->shading);hash(sourceKey,materialAsset->blend);
            materialHash(sourceKey,materialAsset->surface);
            if (!id.starts_with("builtin:")) files.push_back(root/pathFromUtf8(id));
            const auto& material=materialAsset->surface;
            for (const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
                if (texture->image.empty()) continue;
                const auto target=assets.references().resolveTexture(texture->image.id,texture==&material.normalTexture);
                hash(sourceKey,target.data(),target.size());
                if (!target.empty() && !target.starts_with("builtin:")) files.push_back(root/pathFromUtf8(target));
            }
            settings.camera.position={0,0,3}; settings.camera.target={0,0,0}; settings.camera.up={0,1,0};
            settings.camera.fov=45; settings.camera.nearPlane=.1f; settings.camera.farPlane=10;
            directions={DirectionalLight{{-1,-1,-1},{1,1,1},1,true},DirectionalLight{{0,1,0},{.7f,.7f,.7f},1,true}};
        } else {
            models.push_back({0,id,Mat4{}}); settings.camera.position={4,3,5}; settings.camera.target={0,0,0};
        }
        bool loaded=true;
        for (const auto& model:models) {
            const auto asset=assets.findModel(model.assetId);
            if (!asset) {
                if (assets.modelLoading(model.assetId)) loaded=false;
                else entry.error=assets.modelDiagnostic(model.assetId).empty() ? "Missing model: "+model.assetId : assets.modelDiagnostic(model.assetId);
                continue;
            }
            const auto fingerprint=assets.modelFingerprint(model.assetId); hash(sourceKey,fingerprint); hash(sourceKey,model.world);
            const auto materials=assets.resolvedModelMaterials(*asset);
            if (!assets.modelMaterialsReady(*asset)) {
                loaded=false;
                const auto diagnostic=assets.modelMaterialsDiagnostic(*asset);
                if (!diagnostic.empty()) entry.error=diagnostic;
                continue;
            }
            for (const auto& material:materials) {
                materialHash(sourceKey,material);
                for (const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
                    if (texture->image.empty()) continue;
                    const auto target=assets.references().resolveTexture(texture->image.id,texture==&material.normalTexture);
                    hash(sourceKey,target.data(),target.size());
                    if (!target.empty() && !target.starts_with("builtin:")) files.push_back(root/pathFromUtf8(target));
                }
            }
            math::include(bounds,math::transformBounds(asset->bounds,model.world));
        }
        if (!loaded) return;
        if (entry.kind==AssetKind::Model && !bounds.valid) {
            entry.error="Model bounds are unavailable."; entry.failed=true; return;
        }
        const auto source=assets.references().resolveEnvironment(settings.environment.map);
        auto bake=environment(source,entry); if (!bake) return;
        hash(sourceKey,bake->key); hash(sourceKey,settings.exposure); hash(sourceKey,settings.environment.intensity); hash(sourceKey,settings.environment.rotation); hash(sourceKey,settings.environment.enabled);
        if (entry.kind==AssetKind::Model || (entry.kind==AssetKind::Scene && !sceneSnapshot->previewCamera)) {
            if (bounds.valid) settings.camera.frameBounds(bounds.minimum,bounds.maximum,1);
        }
        hash(sourceKey,settings.camera.position); hash(sourceKey,settings.camera.target); hash(sourceKey,settings.camera.up); hash(sourceKey,settings.camera.fov);
        hash(sourceKey,settings.camera.nearPlane); hash(sourceKey,settings.camera.farPlane);
        if (entry.sourceKey!=sourceKey || !entry.dependencies) {
            entry.sourceKey=sourceKey; entry.signature=0; entry.cacheChecked=0;
            files=uniqueFiles(std::move(files)); ResourceRequestOptions options; options.variant="preview-sources:"+std::to_string(sourceKey);
            options.priority=ResourcePriority::Preview; options.dependencies=files; options.assetDependencies={id};
            entry.dependencies=resources.request<PreviewDependencies>(AssetRef<PreviewDependencies>{id},[files](std::stop_token token) {
                if (token.stop_requested()) throw std::runtime_error("Preview fingerprint canceled.");
                auto data=std::make_shared<PreviewDependencies>(); data->files=files; data->fingerprint=files.empty() ? streaming::detail::HashSeed : fingerprintFiles(files);
                data->sourceFingerprints.reserve(files.size());
                for (const auto& file:files) {
                    if (token.stop_requested()) throw std::runtime_error("Preview fingerprint canceled.");
                    data->sourceFingerprints.push_back(fingerprintFiles({file}));
                }
                return ResourceLoadResult<PreviewDependencies>{data,sizeof(PreviewDependencies)+files.size()*(sizeof(std::filesystem::path)+sizeof(uint64_t)),data->fingerprint,files};
            },std::move(options));
        }
        if (entry.dependencies.state()==ResourceState::Failed) { entry.error=entry.dependencies.diagnostic(); return; }
        if (entry.dependencies.state()!=ResourceState::Ready) return;
        const auto dependencies=entry.dependencies.snapshot(); if (!dependencies) return;
        auto signature=sourceKey; hash(signature,dependencies->fingerprint); entry.signature=signature;
        if (warmCache(id,entry,signature)) { if (entry.published==signature) entry.saveRequested=false; return; }
        if (renderer.thumbnailBusy() || entry.rendering==signature) return;
        auto pins=std::make_shared<PreviewPins>(); pins->scene=sceneSnapshot; pins->material=materialAsset; pins->environment=bake;
        std::vector<RenderModelInstance> instances;
        std::vector<RenderParticle> particles;
        std::vector<RenderParticleMaterial> particleMaterials;
        if (spriteMaterial) {
            assets.setTextureDemand(PreviewSize);
            const auto material=assets.preparedParticleMaterial(id);
            if(!material || !material->ready || !material->current) { entry.error=material?material->diagnostic:"Sprite material is unavailable.";return; }
            const auto& image=materialAsset->surface.baseColorTexture.image;
            if(!image.empty()) {
                const auto target=assets.references().resolveTexture(image.id,false);
                if(!target.empty() && !target.starts_with("builtin:")) {
                    const auto file=root/pathFromUtf8(target);
                    if(!dependencies->sourceMatches(file,material->sourceFingerprint)) {
                        if(const auto fingerprint=dependencies->sourceFingerprint(file);fingerprint && refreshedSources[resourcePathKey(file)]!=*fingerprint) {
                            refreshedSources[resourcePathKey(file)]=*fingerprint;resources.invalidatePath(file);
                        }
                        return;
                    }
                }
            }
            pins->particle=material;particleMaterials.push_back(material->render);
            particles.push_back({{0,0,0},2,{1,1,1,1},0});
            if(!material->diagnostic.empty())entry.error=material->diagnostic;
        }
        if (materialAsset && !spriteMaterial) {
            assets.setTextureDemand(PreviewSize);
            auto candidate=std::make_shared<PreparedEditorModel>(); candidate->asset=materialGeometry;
            candidate->materials={materialAsset->surface}; candidate->textures=assets.modelBindings(*materialGeometry,candidate->materials);
            if (!assets.modelBindingsReady(candidate->textures) || std::any_of(candidate->textures.begin(),candidate->textures.end(),[](const auto& binding) {
                return !binding.complete || !binding.sourceReady;
            })) return;
            if (!texturesMatchSources(assets,root,*dependencies,candidate->materials,candidate->textures,resources,refreshedSources)) return;
            std::unordered_map<std::string,uint64_t> versions;
            const auto& material=materialAsset->surface; size_t slot{};
            for (const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
                if (!texture->image.empty()) {
                    const auto target=assets.references().resolveTexture(texture->image.id,texture==&material.normalTexture);
                    const auto fingerprint=candidate->textures.front().sourceFingerprints[slot];
                    const auto [found,inserted]=versions.emplace(target,fingerprint);
                    if (!inserted && found->second!=fingerprint) return;
                }
                ++slot;
            }
            prepared.retainExternalModelForFrame(materialGeometry->id);
            renderer.configureModelStream(materialGeometry->id,1,*materialGeometry->stream);
            if (!renderer.modelStreamReady(materialGeometry->id,1)) {
                if (!renderer.queueModelLod(materialGeometry->id,1,materialPreviewGeometryLevel())) entry.error=renderer.modelStatus();
                return;
            }
            candidate->revision=1; candidate->ready=candidate->refined=true; pins->models.push_back(candidate);
            instances.push_back({0,{materialGeometry,Mat4{}},&candidate->textures,1,true,false,&candidate->materials});
        }
        for (const auto& model:models) {
            if (!assets.findModel(model.assetId) && !assets.modelLoading(model.assetId)) continue;
            const auto ready=prepared.prepare(model.assetId,model.world,settings.camera,PreviewSize,false,ResourcePriority::Preview);
            if (!ready || !ready->ready || !ready->refined) return;
            if (!texturesMatchSources(assets,root,*dependencies,ready->materials,ready->textures,resources,refreshedSources)) return;
            pins->models.push_back(ready);
            instances.push_back({0,{ready->asset,model.world},&ready->textures,ready->revision,false,false,&ready->materials});
        }
        if (thumbnailEnvironment!=bake->key) {
            if (!renderer.publishThumbnailEnvironment(*bake)) { entry.error=renderer.thumbnailStatus(); return; }
            thumbnailEnvironment=bake->key;
        }
        if (renderer.renderThumbnail(id,signature,RenderScene{settings,{},nullptr,nullptr,instances,directions,points,true,pins,particles,particleMaterials},PreviewSize,pins)) entry.rendering=signature;
        else { entry.error=renderer.thumbnailStatus().empty() ? "This renderer cannot create asset previews." : renderer.thumbnailStatus(); entry.failed=true; }
    }
};
AssetPreviews::AssetPreviews(std::filesystem::path root,AssetLibrary& assets,ResourceManager& resources,Renderer& renderer,EditorResources& models)
    :impl_(std::make_unique<Impl>(std::move(root),assets,resources,renderer,models)) {}
AssetPreviews::~AssetPreviews()=default;
void AssetPreviews::pump(uint32_t frame,std::span<const std::string> visible) {
    impl_->status.clear();
    if (auto result=impl_->renderer.pollThumbnail(frame)) {
        const auto found=impl_->entries.find(result->id);
        if (found!=impl_->entries.end()) {
            auto& entry=found->second; entry.rendering=0;
            if (!impl_->assets.references().deleted(result->id) && entry.signature==result->signature) {
                if (impl_->renderer.setThumbnail(result->id,result->signature,result->image)) {
                    entry.published=result->signature; entry.saveRequested=false; entry.error.clear();
                    impl_->writePreview(*result,entry);
                } else entry.error="Cannot publish thumbnail texture.";
            }
        }
    }
    for (auto& [id,entry]:impl_->entries) {
        (void)id; entry.wanted=entry.saveRequested;
        if (entry.writer && (entry.writer.state()==ResourceState::Ready || entry.writer.state()==ResourceState::Failed)) {
            if (entry.writer.state()==ResourceState::Failed) entry.error=entry.writer.diagnostic(); entry.writePin.reset(); entry.writer={};
        }
    }
    for (const auto& id:visible) {
        const auto asset=std::find_if(impl_->assets.catalog().begin(),impl_->assets.catalog().end(),[&](const auto& value){return value.id==id;});
        if (asset==impl_->assets.catalog().end() || (asset->kind!=AssetKind::Model && asset->kind!=AssetKind::Scene && asset->kind!=AssetKind::Material)) continue;
        auto& entry=impl_->entries[id]; entry.kind=asset->kind; entry.wanted=true;
    }
    for (auto& [id,entry]:impl_->entries) if (entry.wanted) {
        entry.used=std::chrono::steady_clock::now();
        try { impl_->request(id,entry); }
        catch (const std::exception& error) { entry.error=error.what(); }
        if (!entry.error.empty()) impl_->status+=impl_->assets.displayName(id)+": "+entry.error+"\n";
    }
    // Cached GPU/PNG previews survive leaving the browser. CPU interests are
    // temporary so hidden/deleted scene metadata and IBL bakes can be reclaimed.
    const auto now=std::chrono::steady_clock::now();
    const auto grace=impl_->resources.config().idleGrace;
    std::erase_if(impl_->entries,[&](const auto& item) {
        const auto& entry=item.second;
        return !entry.rendering && !entry.writer && (impl_->assets.references().deleted(item.first) ||
            (!entry.wanted && now-entry.used>=grace));
    });
    std::erase_if(impl_->environments,[&](const auto& item){return now-item.second.used>=grace;});
}
void AssetPreviews::requestSceneSave(const std::string& id) {
    if (id.empty()) return;
    auto& entry=impl_->entries[id]; entry.kind=AssetKind::Scene; entry.saveRequested=true; entry.forceScene=true; entry.failed=false;
}
void AssetPreviews::invalidate(const std::string& id) {
    if (const auto found=impl_->entries.find(id);found!=impl_->entries.end()) {
        found->second.failed=false; found->second.cacheChecked=0; found->second.sourceKey=0; found->second.error.clear();
        if (found->second.kind==AssetKind::Scene) found->second.forceScene=true;
    }
}
const std::string& AssetPreviews::status() const { return impl_->status; }
} // namespace crimson
