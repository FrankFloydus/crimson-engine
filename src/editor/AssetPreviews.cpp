#include "AssetPreviews.h"
#include "scene/BuiltinModels.h"
#include "AssetPreviewCache.h"
#include "AssetSourceSnapshot.h"
#include "crimson-renderer/EnvironmentPresentation.h"
#include "EditorResources.h"
#include "assets/AssetLibrary.h"
#include "assets/MaterialGraphReferences.h"
#include "assets/MaterialPreviewGeometry.h"
#include "assets/AtomicFile.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
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
constexpr uint32_t PreviewVersion=10;
using streaming::detail::hash;
uint64_t textHash(std::string_view text) { auto result=streaming::detail::HashSeed; hash(result,text.data(),text.size()); return result; }
void materialHash(uint64_t& value,const ModelMaterial& material) {
    if(material.graph){hash(value,materialGraphRenderHash(material));return;}
    hash(value,material.baseColor); hash(value,material.metallic); hash(value,material.roughness); hash(value,material.normalScale);
    hash(value,material.occlusionStrength); hash(value,material.emissive); hash(value,material.alphaMask); hash(value,material.alphaCutoff); hash(value,material.doubleSided); hash(value,material.receiveFog);
    hash(value,material.uvScale); hash(value,material.uvOffset); hash(value,material.uvRotation); hash(value,material.triplanar);
    hash(value,material.triplanarWorld);
    hash(value,material.sharedSampler.has_value());
    if(material.sharedSampler) {
        hash(value,material.sharedSampler->wrapU); hash(value,material.sharedSampler->wrapV);
        hash(value,material.sharedSampler->minFilter); hash(value,material.sharedSampler->magFilter);
    }
    for (const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
        hash(value,texture->image.id.data(),texture->image.id.size()); hash(value,texture->texCoord); hash(value,texture->wrapU); hash(value,texture->wrapV);
        hash(value,texture->minFilter); hash(value,texture->magFilter);
    }
}
uint64_t materialPreviewHash(const MaterialAsset& material) {
    auto key=streaming::detail::HashSeed;hash(key,material.shading);hash(key,material.blend);materialHash(key,material.surface);return key;
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
    std::shared_ptr<const PreparedEditorShadows> shadows;
};
bool texturesMatchSources(const AssetLibrary& assets,const std::filesystem::path& root,const PreviewDependencies& expected,
    std::span<const ModelMaterial> materials,std::span<const ModelMaterialTextures> bindings,ResourceManager& resources,
    std::unordered_map<std::string,uint64_t>& refreshedSources) {
    if (materials.size()!=bindings.size()) return false;
    for (size_t m=0;m<materials.size();++m) {
        if(const auto graph=bindings[m].graph) {
            for(const auto& source:graph->sources) {
                const auto& target=source.image.id;
                if(target.empty() || isEngineAssetId(target))continue;
                // A retained graph packet still pins its old image after the
                // deletion transaction has published a replacement policy.
                if(assets.references().resolveTexture(target,source.role==MaterialGraphTextureRole::Normal)!=target)return false;
                const auto file=resolveAssetPath(root,target);
                if(!expected.sourceMatches(file,graph->sourceFingerprints[source.slot])) {
                    if(const auto fingerprint=expected.sourceFingerprint(file);fingerprint && refreshedSources[resourcePathKey(file)]!=*fingerprint) {
                        refreshedSources[resourcePathKey(file)]=*fingerprint;resources.invalidatePath(file);
                    }
                    return false;
                }
            }
            continue;
        }
        const auto& material=materials[m]; size_t slot{};
        for (const auto* texture:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
            if (!texture->image.empty()) {
                const auto target=assets.references().resolveTexture(texture->image.id,texture==&material.normalTexture);
                if (!target.empty() && !isEngineAssetId(target)) {
                    const auto file=resolveAssetPath(root,target);
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
        ResourceHandle<PreviewDependencies> diskIndex;
        ResourceHandle<DecodedImage> cached,writer;
        std::shared_ptr<const DecodedImage> writePin;
        uint64_t sourceKey{},signature{},rendering{};
        PreviewCacheState cacheState;
        PreviewRetryState retry;
        PreviewValidationKey validated;
        PreviewValidationKey indexValidation;
        std::vector<std::filesystem::path> files;
        std::vector<PreviewMaterialFingerprint> materials;
        std::vector<PreviewSourceLocation> locations;
        bool clean{},indexChecked{};
        bool wanted{},forceScene{},saveRequested{};
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
    uint64_t storageRevision{};
    uint64_t policyRevision{},policyFingerprint{};
    uint32_t previewSize{128};
    PreviewValidationKey validation;
    AssetPreviewStats statistics;
    std::chrono::steady_clock::time_point validationDeadline;
    std::filesystem::path cacheFolder;
    std::unordered_map<std::string,AssetKind> catalogKinds;
    uint64_t catalogRevision{};
    std::string status;
    Impl(std::filesystem::path directory,AssetLibrary& library,ResourceManager& manager,Renderer& gpu,EditorResources& models)
        :root(std::move(directory)),assets(library),resources(manager),renderer(gpu),prepared(models),
         cacheFolder(resolveAssetPath(root,"assets/cache/previews")) {}
    std::filesystem::path cache(const std::string& id,uint64_t signature) const {
        return assetPreviewImagePath(cacheFolder,id,signature);
    }
    void writePreview(const ThumbnailResult& result,Entry& entry) {
        const auto id=result.id; const auto signature=result.signature;
        entry.writePin=std::make_shared<DecodedImage>(result.image);
        const std::weak_ptr<const DecodedImage> weak=entry.writePin;
        CachedAssetPreview record;record.signature=signature;record.edge=result.image.width;record.shaderSignature=renderer.shaderSignature();
        record.policyFingerprint=policyFingerprint;record.files=entry.files;record.materials=entry.materials;record.locations=entry.locations;
        const auto expected=entry.dependencies.snapshot();const auto folder=cacheFolder,project=root;
        if(expected && expected->files==record.files)record.sourceFingerprints=expected->sourceFingerprints;
        ResourceRequestOptions options; options.variant="preview-write:"+std::to_string(signature); options.priority=ResourcePriority::Background;
        options.retainLoader=false;options.cancelWhenUnused=true;options.forceReload=true;
        entry.writer=resources.request<DecodedImage>(AssetRef<DecodedImage>{id},[weak,folder,project,id,signature,record=std::move(record)](std::stop_token token) mutable {
            const auto pixels=weak.lock(); if (!pixels || token.stop_requested()) throw std::runtime_error("Preview write canceled.");
            // Unsaved material edits can have a useful session PNG, but must not
            // become a warm-start preview for the older saved asset definition.
            // Idle residency deliberately releases CPU dependency snapshots.
            // A PNG restored after that point can still be displayed, but it
            // cannot certify newly observed source bytes as its render version.
            bool saved=record.files.empty() || record.sourceFingerprints.size()==record.files.size();
            for(const auto& material:record.materials)if(!isEngineAssetId(material.id)) {
                try {if(materialPreviewHash(loadMaterialAsset(project,material.id))!=material.fingerprint)saved=false;}
                catch(const std::exception&){saved=false;}
            }
            record.image=pixels;writeAssetPreviewCache(project,folder,id,record,saved,token);
            return ResourceLoadResult<DecodedImage>{pixels,pixels->rgba.size(),signature,{}};
        },std::move(options));
    }
    bool indexedCache(const std::string& id,Entry& entry) {
        if(entry.saveRequested)return false;
        if(entry.diskIndex && entry.indexValidation!=validation) {entry.diskIndex={};entry.indexChecked=false;}
        if(!entry.indexChecked) {
            entry.indexChecked=true;entry.indexValidation=validation;
            const auto folder=cacheFolder,project=root;const auto key=validation;const auto policy=policyFingerprint;
            ResourceRequestOptions options;options.variant="preview-index:"+std::to_string(key.edge)+":"+std::to_string(key.rendererShader)+":"+std::to_string(policy);
            options.priority=ResourcePriority::Preview;options.forceReload=true;options.retainLoader=false;options.cancelWhenUnused=true;
            options.dependencyValidation=DependencyValidation::FileState;
            ++statistics.diskRequests;
            entry.diskIndex=resources.request<PreviewDependencies>(AssetRef<PreviewDependencies>{id},[project,folder,id,key,policy](std::stop_token token) {
                auto data=std::make_shared<PreviewDependencies>();
                try {
                    auto cached=std::make_shared<CachedAssetPreview>(readAssetPreviewCache(project,folder,id,key.edge,key.rendererShader,policy,token));
                    for(const auto& location:cached->locations)
                        if(resolveAssetPath(project,location.id).lexically_normal()!=location.file.lexically_normal())throw std::runtime_error("Preview source moved.");
                    data->files=cached->files;data->cachedPreview=std::move(cached);
                }catch(const std::exception&){if(token.stop_requested())throw;}
                const auto bytes=sizeof(PreviewDependencies)+(data->cachedPreview?data->cachedPreview->image->rgba.size():0);
                return ResourceLoadResult<PreviewDependencies>{data,bytes,data->cachedPreview?data->cachedPreview->signature:0,data->files};
            },std::move(options));
        }
        if(!entry.diskIndex)return false;
        if(entry.diskIndex.state()!=ResourceState::Ready && entry.diskIndex.state()!=ResourceState::Failed)return true;
        const auto data=entry.diskIndex.snapshot();
        if(data && data->cachedPreview) {
            const auto& cached=*data->cachedPreview;
            bool current=true;
            for(const auto& expected:cached.materials)if(const auto material=assets.peekMaterial(expected.id))
                if(materialPreviewHash(*material)!=expected.fingerprint){current=false;break;}
            if(current && renderer.setThumbnail(id,cached.signature,*cached.image)) {
                entry.signature=cached.signature;entry.cacheState.published=cached.signature;
                entry.files=cached.files;entry.materials=cached.materials;entry.locations=cached.locations;
                entry.clean=true;entry.validated=validation;entry.retry.reset();entry.error.clear();entry.diskIndex={};
                ++statistics.diskHits;return true;
            }
        }
        entry.diskIndex={};return false;
    }
    ResourceSnapshot<SceneSnapshot> scene(const std::string& id,Entry& entry) {
        if (!entry.scene || entry.forceScene) {
            ResourceRequestOptions options; options.variant="preview"; options.priority=ResourcePriority::Preview;
            options.dependencies={resolveAssetPath(root,id)}; options.forceReload=entry.forceScene;
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
            if (!source.empty()) options.dependencies={resolveAssetPath(root,source)};
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
        if (bgfx::isValid(renderer.thumbnailTexture(id,signature))) { ++statistics.residentHits;entry.cacheState.published=signature;entry.retry.reset();entry.error.clear();return true; }
        if (entry.cacheState.published==signature && entry.writer) return true;
        if (entry.cacheState.needsDiskRead(signature)) {
            entry.cached={};
            const auto file=cache(id,signature),metadata=std::filesystem::path(file.wstring()+L".json");
            const auto edge=previewSize;
            {
                ResourceRequestOptions options; options.variant="preview-png:"+std::to_string(signature); options.priority=ResourcePriority::Preview;
                // Cache files bypass the authored-source watcher. An earlier
                // failed read must not poison this image after it is repaired.
                options.forceReload=true;
                options.retainLoader=false;options.cancelWhenUnused=true;options.dependencyValidation=DependencyValidation::FileState;
                ++statistics.diskRequests;
                entry.cached=resources.request<DecodedImage>(AssetRef<DecodedImage>{id},[file,metadata,id,signature,edge](std::stop_token token) {
                    if (token.stop_requested()) throw std::runtime_error("Cached preview read canceled.");
                    try {
                        std::ifstream input(metadata); const auto record=nlohmann::json::parse(input);
                        if (record.at("version")!=PreviewVersion || record.at("id")!=id || record.at("signature")!=signature)
                            throw std::runtime_error("Cached preview metadata is stale.");
                        auto image=std::make_shared<DecodedImage>(decodeManagedImage(file));
                        if (image->width!=edge || image->height!=edge) throw std::runtime_error("Cached preview dimensions are invalid.");
                        return ResourceLoadResult<DecodedImage>{image,image->rgba.size(),signature,{file,metadata}};
                    }catch(const std::exception&){if(token.stop_requested())throw;return ResourceLoadResult<DecodedImage>{std::make_shared<DecodedImage>(),sizeof(DecodedImage),signature,{}};}
                },std::move(options));
            }
        }
        if (entry.cached) {
            if (entry.cached.state()==ResourceState::Ready) {
                if (const auto pixels=entry.cached.snapshot()) {
                    if(!pixels->width){entry.cached={};return false;}
                    if (renderer.setThumbnail(id,signature,*pixels)) {
                        ++statistics.diskHits;entry.cacheState.published=signature;entry.retry.reset();entry.error.clear();
                        writePreview(ThumbnailResult{id,signature,*pixels},entry);entry.cached={};return true;
                    }
                    entry.error="Cannot publish cached thumbnail texture.";entry.retry.failed(PreviewRetryState::Clock::now());return true;
                }
            } else if (entry.cached.state()!=ResourceState::Failed) return true; // Wait; do not decode model geometry for a warm PNG.
            else { entry.error=entry.cached.diagnostic(); entry.cached={}; }
        }
        return false;
    }
    void request(const std::string& id,Entry& entry) {
        ++statistics.requests;
        if (assets.references().deleted(id) || !entry.retry.ready(PreviewRetryState::Clock::now())) return;
        if(entry.clean && entry.validated==validation && !entry.forceScene && !entry.saveRequested)
            if(warmCache(id,entry,entry.signature))return;
        if(!entry.clean && indexedCache(id,entry))return;
        if(std::chrono::steady_clock::now()>=validationDeadline)return;
        ++statistics.validations;
        SceneSettings settings; settings.directional.enabled=false; for (auto& light:settings.points) light.enabled=false;
        std::vector<ExtractedModel> models; std::vector<DirectionalLight> directions; std::vector<PointLight> points;
        std::shared_ptr<const SceneSnapshot> sceneSnapshot; Aabb bounds;
        std::shared_ptr<const MaterialAsset> materialAsset;
        std::shared_ptr<const ModelAsset> materialGeometry;
        bool spriteMaterial=false;
        std::vector<std::filesystem::path> files;
        std::vector<std::filesystem::path> validationFiles;
        std::vector<PreviewMaterialFingerprint> materialVersions;
        std::vector<PreviewSourceLocation> locations;
        const auto sourceFile=[&](const std::string& source) {
            auto file=resolveAssetPath(root,source);locations.push_back({source,file});return file;
        };
        const auto includeMaterial=[&](const std::string& source) {
            if(source.empty())return;
            const auto target=assets.references().resolveMaterial(source);
            if(const auto material=assets.peekMaterial(target)) {
                materialVersions.push_back({target,materialPreviewHash(*material)});
                if(!isEngineAssetId(target))validationFiles.push_back(sourceFile(target));
            }
        };
        uint64_t sourceKey=streaming::detail::HashSeed; hash(sourceKey,PreviewVersion); hash(sourceKey,id.data(),id.size()); hash(sourceKey,renderer.shaderSignature());hash(sourceKey,assets.materialShaderRevision());
        hash(sourceKey,previewSize);
        if (entry.kind==AssetKind::Scene) {
            const auto snapshot=scene(id,entry); if (!snapshot) return;
            validationFiles.push_back(sourceFile(id));
            sceneSnapshot=snapshot.shared(); hash(sourceKey,snapshot->fingerprint);
            const auto profileLocations=storedAssetPaths(root);
            const NavigationAgentProfileQuery profileQuery=[&](const std::string& profileId) {
                if(auto managed=assets.findNavigationAgentProfile(profileId))return managed;
                if(std::any_of(profileLocations.begin(),profileLocations.end(),[&](const auto& entry){return entry.id==profileId;}))return std::shared_ptr<const NavigationAgentProfile>{};
                const auto pending=std::find_if(snapshot->stagedNavigationProfiles.begin(),snapshot->stagedNavigationProfiles.end(),
                    [&](const auto& profile){return profile&&profile->id==profileId;});
                return pending==snapshot->stagedNavigationProfiles.end()?std::shared_ptr<const NavigationAgentProfile>{}:*pending;
            };
            for(const auto& record:snapshot->records)if(record.controller)if(const auto profile=resolveNavigationAgentProfile(record.controller->profile.id,profileQuery)) {
                hash(sourceKey,profile->traversal.height);hash(sourceKey,profile->traversal.radius);
                hash(sourceKey,profile->traversal.maxSlope);hash(sourceKey,profile->traversal.maxStep);
                if(!isEngineAssetId(profile->id)&&std::any_of(profileLocations.begin(),profileLocations.end(),[&](const auto& entry){return entry.id==profile->id;}))validationFiles.push_back(sourceFile(profile->id));
            }
            Scene candidate; candidate.apply(snapshot->records,snapshot->properties); auto extracted=extractScene(candidate,profileQuery);
            models=std::move(extracted.models); directions=std::move(extracted.directional); points=std::move(extracted.points);
            std::erase_if(models,[](const auto& model){return !model.visible;});
            for(auto& model:models)model.animationSeconds=model.animation.startTime;
            settings.environment=extracted.environment.environment; settings.exposure=extracted.environment.exposure;
            settings.proceduralSun=extracted.proceduralSun;
            if (snapshot->previewCamera) settings.camera=*snapshot->previewCamera;
        } else if (entry.kind==AssetKind::Material) {
            materialAsset=assets.findMaterial(id);
            if (!materialAsset) { entry.error="Material is unavailable."; return; }
            includeMaterial(id);
            spriteMaterial=materialAsset->id==DefaultParticleMaterialId;
            if (!spriteMaterial) {
                ModelAsset probe; probe.materialIds={id};
                if (!assets.modelMaterialsReady(probe)) {
                    const auto diagnostic=assets.modelMaterialsDiagnostic(probe);
                    if (!diagnostic.empty()) entry.error=diagnostic;
                    return;
                }
                materialGeometry=materialPreviewGeometry();
            }
            const uint32_t materialPreviewVersion=2; hash(sourceKey,materialPreviewVersion);
            hash(sourceKey,materialAsset->shading);hash(sourceKey,materialAsset->blend);
            materialHash(sourceKey,materialAsset->surface);
            if (!isEngineAssetId(id) && materialAsset->graph.empty()) files.push_back(sourceFile(id));
            const auto& material=materialAsset->surface;
            visitMaterialTextures(material,true,[&](const auto& image,MaterialTextureRole role) {
                const auto target=assets.references().resolveTexture(image,role==MaterialTextureRole::Normal);
                hash(sourceKey,target.data(),target.size());
                if(!target.empty() && !isEngineAssetId(target))files.push_back(sourceFile(target));
            });
            settings.camera.position={0,0,3}; settings.camera.target={0,0,0}; settings.camera.up={0,1,0};
            settings.camera.fov=45; settings.camera.nearPlane=.1f; settings.camera.farPlane=10;
            directions={DirectionalLight{{-1,-1,-1},{1,1,1},1,true},DirectionalLight{{0,1,0},{.7f,.7f,.7f},1,true}};
        } else {
            models.push_back({0,id,Mat4{}}); settings.camera.position={4,3,5}; settings.camera.target={0,0,0};
        }
        // Generated shader inputs participate in the persistent thumbnail key;
        // a graph-only template edit need not change an ordinary engine program.
        const auto shaderFiles=assets.materialShaderDependencies();files.insert(files.end(),shaderFiles.begin(),shaderFiles.end());
        bool loaded=true;
        for (const auto& model:models) {
            const auto asset=assets.findModel(model.assetId);
            if (!asset) {
                if (assets.modelLoading(model.assetId)) loaded=false;
                else entry.error=assets.modelDiagnostic(model.assetId).empty() ? "Missing model: "+model.assetId : assets.modelDiagnostic(model.assetId);
                continue;
            }
            const auto fingerprint=assets.modelFingerprint(model.assetId); hash(sourceKey,fingerprint); hash(sourceKey,model.world);
            // Saved scene references retain their authored identity after a
            // deletion fallback. Only the resolved model owns current files;
            // the original bundled UUID may no longer have a registration.
            if(!isBuiltinModel(asset->id)) {
                validationFiles.push_back(sourceFile(asset->id));
                const auto dependencies=assets.modelDependencies(model.assetId);
                validationFiles.insert(validationFiles.end(),dependencies.begin(),dependencies.end());
            }
            const auto materials=assets.resolvedModelMaterials(*asset,model.materialOverrides);
            if (!assets.modelMaterialsReady(*asset,model.materialOverrides)) {
                loaded=false;
                const auto diagnostic=assets.modelMaterialsDiagnostic(*asset,model.materialOverrides);
                if (!diagnostic.empty()) entry.error=diagnostic;
                continue;
            }
            for(size_t slot=0;slot<materials.size();++slot) {
                const auto materialId=slot<model.materialOverrides.size() && !model.materialOverrides[slot].empty()?model.materialOverrides[slot].id:
                    slot<asset->materialIds.size()?asset->materialIds[slot]:importedMaterialId(asset->id,slot);
                includeMaterial(materialId);
            }
            for (const auto& material:materials) {
                materialHash(sourceKey,material);
                visitMaterialTextures(material,true,[&](const auto& image,MaterialTextureRole role) {
                    const auto target=assets.references().resolveTexture(image,role==MaterialTextureRole::Normal);
                    hash(sourceKey,target.data(),target.size());
                    if(!target.empty() && !isEngineAssetId(target))files.push_back(sourceFile(target));
                });
            }
            math::include(bounds,math::transformBounds(prepared.modelBounds(model,asset),model.world));
        }
        if (!loaded) return;
        if (entry.kind==AssetKind::Model && !bounds.valid) {
            entry.error="Model bounds are unavailable.";entry.retry.failed(PreviewRetryState::Clock::now());return;
        }
        // Browser thumbnails deliberately omit post-process halos. Their cache
        // stays stable when a scene's Glow is tuned in its full-size views.
        settings.environment.glow.enabled=false;
        settings.environment.ssgi.enabled=false;settings.environment.ssao.enabled=false;
        const auto source=settings.environment.source==EnvironmentSource::Hdri?
            assets.references().resolveEnvironment(settings.environment.map):std::string{};
        if(!source.empty())validationFiles.push_back(sourceFile(source));
        auto bake=environment(source,entry); if (!bake) return;
        hash(sourceKey,bake->key);hash(sourceKey,settings.exposure);
        hash(sourceKey,thumbnailEnvironmentKey(settings.environment,settings.proceduralSun));
        if (entry.kind==AssetKind::Model || (entry.kind==AssetKind::Scene && !sceneSnapshot->previewCamera)) {
            if (bounds.valid) settings.camera.frameBounds(bounds.minimum,bounds.maximum,1);
        }
        hash(sourceKey,settings.camera.position); hash(sourceKey,settings.camera.target); hash(sourceKey,settings.camera.up); hash(sourceKey,settings.camera.fov);
        hash(sourceKey,settings.camera.nearPlane); hash(sourceKey,settings.camera.farPlane);
        if (entry.sourceKey!=sourceKey || !entry.dependencies) {
            entry.sourceKey=sourceKey; entry.signature=0; entry.cacheState.checked=0;
            files.insert(files.end(),validationFiles.begin(),validationFiles.end());
            files=uniqueFiles(std::move(files)); ResourceRequestOptions options; options.variant="preview-sources:"+std::to_string(sourceKey);
            options.priority=ResourcePriority::Preview; options.dependencies=files; options.assetDependencies={id};
            options.dependencyValidation=DependencyValidation::FileState;
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
        validationFiles.insert(validationFiles.end(),dependencies->files.begin(),dependencies->files.end());
        entry.files=uniqueFiles(std::move(validationFiles));entry.materials=std::move(materialVersions);entry.locations=std::move(locations);
        entry.clean=true;entry.validated=validation;
        if (warmCache(id,entry,signature)) { if (entry.cacheState.published==signature) entry.saveRequested=false; return; }
        if (renderer.thumbnailBusy() || entry.rendering==signature) return;
        auto pins=std::make_shared<PreviewPins>(); pins->scene=sceneSnapshot; pins->material=materialAsset; pins->environment=bake;
        if(sceneSnapshot) {
            ExtractedScene shadowScene;shadowScene.models=models;shadowScene.points=points;shadowScene.directional=directions;
            std::erase_if(shadowScene.models,[&](const auto& model){return !assets.findModel(model.assetId) && !assets.modelLoading(model.assetId);});
            const std::array<ShadowCamera,1> shadowCameras{{{settings.camera,1.f}}};
            pins->shadows=prepared.prepareShadows(shadowScene,256,ResourcePriority::Preview,shadowCameras);
            if(!pins->shadows->refined)return;
            for(const auto& model:pins->shadows->models)
                if(!texturesMatchSources(assets,root,*dependencies,model->materials,model->textures,resources,refreshedSources))return;
        }
        std::vector<RenderModelInstance> instances;
        std::vector<RenderParticle> particles;
        std::vector<RenderParticleMaterial> particleMaterials;
        if (spriteMaterial) {
            assets.setTextureDemand(previewSize);
            const auto material=assets.preparedParticleMaterial(id);
            if(!material || !material->ready || !material->current) { entry.error=material?material->diagnostic:"Sprite material is unavailable.";return; }
            const auto& image=materialAsset->surface.baseColorTexture.image;
            if(!material->render.graph && !image.empty()) {
                const auto target=assets.references().resolveTexture(image.id,false);
                if(!target.empty() && !isEngineAssetId(target)) {
                    const auto file=resolveAssetPath(root,target);
                    if(!dependencies->sourceMatches(file,material->sourceFingerprint)) {
                        if(const auto fingerprint=dependencies->sourceFingerprint(file);fingerprint && refreshedSources[resourcePathKey(file)]!=*fingerprint) {
                            refreshedSources[resourcePathKey(file)]=*fingerprint;resources.invalidatePath(file);
                        }
                        return;
                    }
                }
            }
            if(material->render.graph) {
                const ModelMaterialTextures graphTextures{material->render.graph};
                const std::array<ModelMaterial,1> surfaces{materialAsset->surface};
                const std::array<ModelMaterialTextures,1> textures{graphTextures};
                if(!texturesMatchSources(assets,root,*dependencies,surfaces,textures,resources,refreshedSources))return;
            }
            pins->particle=material;particleMaterials.push_back(material->render);
            particles.push_back({{0,0,0},2,{1,1,1,1},0,0,ParticleBillboardMode::Enabled});
            if(!material->diagnostic.empty())entry.error=material->diagnostic;
        }
        if (materialAsset && !spriteMaterial) {
            assets.setTextureDemand(previewSize);
            auto candidate=std::make_shared<PreparedEditorModel>(); candidate->asset=materialGeometry;
            candidate->materials={materialAsset->surface}; candidate->textures=assets.modelBindings(*materialGeometry,candidate->materials);
            if (!assets.modelBindingsReady(candidate->textures) || std::any_of(candidate->textures.begin(),candidate->textures.end(),[](const auto& binding) {
                return binding.graphPending || !binding.complete || !binding.sourceReady;
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
            const auto ready=prepared.prepare(model.assetId,model.world,settings.camera,previewSize,false,ResourcePriority::Preview,0,model.materialOverrides,0,&model);
            if (!ready || !ready->ready || !ready->refined) return;
            if (!texturesMatchSources(assets,root,*dependencies,ready->materials,ready->textures,resources,refreshedSources)) return;
            pins->models.push_back(ready);
            instances.push_back({model.id,{ready->asset,model.world},&ready->textures,ready->revision,false,false,&ready->materials});
            instances.back().castShadows=model.castShadows;instances.back().receiveShadows=model.receiveShadows;
            instances.back().lightingScopes=model.lightingScopes;
            instances.back().lightingCategory=model.lightingCategory;
            instances.back().actorOwner=model.actorOwner;
            instances.back().pose=prepared.pose(model,ready->asset);
        }
        if (thumbnailEnvironment!=bake->key) {
            if (!renderer.publishThumbnailEnvironment(*bake)) { entry.error=renderer.thumbnailStatus(); return; }
            thumbnailEnvironment=bake->key;
        }
        RenderScene renderScene{settings,{},nullptr,nullptr,instances,directions,points,true,pins,particles,particleMaterials};
        if(pins->shadows) {
            renderScene.shadowInstances=pins->shadows->instances;
            renderScene.shadowWorldId=textHash(id);renderScene.shadowResolution=256;
        }
        if (renderer.renderThumbnail(id,signature,renderScene,static_cast<uint16_t>(previewSize),pins)) {entry.rendering=signature;++statistics.renderRequests;}
        else { entry.error=renderer.thumbnailStatus().empty() ? "This renderer cannot create asset previews." : renderer.thumbnailStatus();entry.retry.failed(PreviewRetryState::Clock::now()); }
    }
};
AssetPreviews::AssetPreviews(std::filesystem::path root,AssetLibrary& assets,ResourceManager& resources,Renderer& renderer,EditorResources& models)
    :impl_(std::make_unique<Impl>(std::move(root),assets,resources,renderer,models)) {}
AssetPreviews::~AssetPreviews()=default;
void AssetPreviews::pump(uint32_t frame,std::span<const std::string> visible,uint32_t pixelEdge) {
    const auto revision=contentStorageRevision(impl_->root);
    if(impl_->storageRevision!=revision) {
        impl_->storageRevision=revision;
        const AssetSourceSnapshot sources(impl_->root);
        for(auto& [id,entry]:impl_->entries) {
            (void)id;
            if(std::any_of(entry.locations.begin(),entry.locations.end(),[&](const auto& source){
                return !sources.matches(source.id,source.file);
            })) {
                entry.scene={};entry.dependencies={};entry.diskIndex={};entry.sourceKey=0;entry.signature=0;
                entry.clean=false;entry.indexChecked=true;entry.retry.reset();++impl_->statistics.invalidations;
            }
        }
    }
    impl_->status.clear();
    impl_->previewSize=std::clamp(pixelEdge,16u,512u);
    const auto policyRevision=impl_->assets.references().revision();
    if(impl_->policyRevision!=policyRevision) {
        impl_->policyRevision=policyRevision;impl_->policyFingerprint=textHash(impl_->assets.references().serialize());
    }
    impl_->validation={0,policyRevision,impl_->renderer.shaderSignature(),impl_->assets.materialShaderRevision(),impl_->previewSize};
    if(impl_->catalogRevision!=impl_->assets.catalogRevision()) {
        impl_->catalogRevision=impl_->assets.catalogRevision();impl_->catalogKinds.clear();
        for(const auto& asset:impl_->assets.catalog())impl_->catalogKinds.emplace(asset.id,asset.kind);
        // Catalog growth and model hydration do not change existing previews.
        // Authored material publication can change an unsaved surface without a
        // file event, so compare only each preview's actual material versions.
        for(auto& [id,entry]:impl_->entries)if(entry.clean) {
            (void)id;
            const auto changed=std::any_of(entry.materials.begin(),entry.materials.end(),[&](const auto& expected){
                const auto current=impl_->assets.peekMaterial(expected.id);return current && materialPreviewHash(*current)!=expected.fingerprint;
            });
            if(changed) {
                entry.clean=false;entry.indexChecked=true;entry.diskIndex={};entry.sourceKey=0;entry.signature=0;
                entry.retry.reset();++impl_->statistics.invalidations;
            }
        }
    }
    if (auto result=impl_->renderer.pollThumbnail(frame)) {
        const auto found=impl_->entries.find(result->id);
        if (found!=impl_->entries.end()) {
            auto& entry=found->second; entry.rendering=0;
            if (!impl_->assets.references().deleted(result->id) && entry.signature==result->signature) {
                if (impl_->renderer.setThumbnail(result->id,result->signature,result->image)) {
                    entry.cacheState.published=result->signature;entry.retry.reset();entry.saveRequested=false;entry.error.clear();
                    impl_->writePreview(*result,entry);
                } else { entry.error="Cannot publish thumbnail texture.";entry.retry.failed(PreviewRetryState::Clock::now()); }
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
        const auto asset=impl_->catalogKinds.find(id);
        if (asset==impl_->catalogKinds.end() || (asset->second!=AssetKind::Model && asset->second!=AssetKind::Scene && asset->second!=AssetKind::Material)) continue;
        auto& entry=impl_->entries[id]; entry.kind=asset->second; entry.wanted=true;
    }
    impl_->validationDeadline=std::chrono::steady_clock::now()+std::chrono::milliseconds{2};
    for (auto& [id,entry]:impl_->entries) if (entry.wanted) {
        entry.used=std::chrono::steady_clock::now();
        try { impl_->request(id,entry); }
        catch (const std::exception& error) { entry.error=error.what();entry.retry.failed(PreviewRetryState::Clock::now()); }
        if (!entry.error.empty()) impl_->status+=impl_->assets.displayName(id)+": "+entry.error+"\n";
    }
    // Keep bounded, lightweight validated signatures with the GPU working set.
    // Releasing heavyweight metadata interests must not make the next visit
    // reconstruct models/scenes before it can find an existing image.
    const auto now=std::chrono::steady_clock::now();
    const auto grace=impl_->resources.config().idleGrace;
    std::erase_if(impl_->entries,[&](const auto& item) {
        const auto& entry=item.second;
        return !entry.rendering && !entry.writer && impl_->assets.references().deleted(item.first);
    });
    for(auto& [id,entry]:impl_->entries)if(!entry.wanted && !entry.rendering && !entry.writer && now-entry.used>=grace) {
        (void)id;entry.scene={};entry.dependencies={};entry.cached={};
        if(entry.diskIndex){entry.diskIndex={};entry.indexChecked=false;}
    }
    constexpr size_t MaximumRetainedDescriptors=4096;
    if(impl_->entries.size()>MaximumRetainedDescriptors) {
        std::vector<std::pair<std::chrono::steady_clock::time_point,std::string>> idle;
        for(const auto& [id,entry]:impl_->entries)if(!entry.wanted && !entry.rendering && !entry.writer)idle.emplace_back(entry.used,id);
        std::sort(idle.begin(),idle.end());
        for(const auto& [used,id]:idle) { (void)used;if(impl_->entries.size()<=MaximumRetainedDescriptors)break;impl_->entries.erase(id); }
    }
    std::erase_if(impl_->environments,[&](const auto& item){return now-item.second.used>=grace;});
}
void AssetPreviews::requestSceneSave(const std::string& id) {
    if (id.empty()) return;
    auto& entry=impl_->entries[id];entry.kind=AssetKind::Scene;entry.saveRequested=true;entry.forceScene=true;entry.clean=false;entry.indexChecked=true;entry.retry.reset();
}
void AssetPreviews::invalidate(const std::string& id) {
    const auto source=AssetSourceSnapshot(impl_->root).find(id);
    if(source) {invalidateFile(*source);return;}
    // Deleted sources have no current path. Retained descriptors still identify
    // exactly which previews depend on them, including pending scene saves.
    for(auto& [assetId,entry]:impl_->entries) {
        const bool affected=assetId==id || std::any_of(entry.locations.begin(),entry.locations.end(),[&](const auto& location){return location.id==id;});
        if(!affected)continue;
        entry.retry.reset();entry.cacheState.checked=0;entry.sourceKey=0;entry.signature=0;entry.error.clear();
        entry.clean=false;entry.indexChecked=true;entry.diskIndex={};entry.cached={};++impl_->statistics.invalidations;
        if(entry.kind==AssetKind::Scene)entry.forceScene=true;
    }
}
void AssetPreviews::invalidateFile(const std::filesystem::path& file) {
    const auto changed=resourcePathKey(file),id=assetIdAtPath(impl_->root,file);
    for(auto& [assetId,entry]:impl_->entries) {
        bool affected=assetId==id;
        if(!affected)affected=std::any_of(entry.files.begin(),entry.files.end(),[&](const auto& file){return resourcePathKey(file)==changed;});
        if(!affected && !id.empty())affected=std::any_of(entry.locations.begin(),entry.locations.end(),[&](const auto& source){return source.id==id;});
        if(!affected)continue;
        entry.retry.reset();entry.cacheState.checked=0;entry.sourceKey=0;entry.signature=0;entry.error.clear();
        entry.clean=false;entry.indexChecked=true;entry.diskIndex={};entry.cached={};++impl_->statistics.invalidations;
        if(entry.kind==AssetKind::Scene)entry.forceScene=true;
    }
}
const std::string& AssetPreviews::status() const { return impl_->status; }
AssetPreviewStats AssetPreviews::stats() const { return impl_->statistics; }
} // namespace crimson
