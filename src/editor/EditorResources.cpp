#include "EditorResources.h"
#include "assets/AssetLibrary.h"
#include "resources/StreamingModel.h"
#include "resources/CacheIO.h"
#include "scene/SceneExtraction.h"
#include "crimson-renderer/PointShadowMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace crimson {
namespace {
using streaming::detail::hash;
uint64_t signature(const ModelAsset& asset,uint64_t revision,std::span<const ModelMaterial> materials,
                   std::span<const ModelMaterialTextures> textures) {
    auto result=streaming::detail::HashSeed; hash(result,revision); hash(result,asset.id.data(),asset.id.size());
    for (const auto& material:materials) {
        hash(result,material.baseColor); hash(result,material.metallic); hash(result,material.roughness);
        hash(result,material.normalScale); hash(result,material.occlusionStrength); hash(result,material.emissive);
        hash(result,material.alphaMask); hash(result,material.alphaCutoff); hash(result,material.doubleSided);
        for (const auto* slot:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
            hash(result,slot->image.id.data(),slot->image.id.size()); hash(result,slot->texCoord); hash(result,slot->wrapU); hash(result,slot->wrapV);
            hash(result,slot->minFilter); hash(result,slot->magFilter);
        }
    }
    for (const auto& binding:textures) for (size_t slot=0;slot<binding.handles.size();++slot) {
        hash(result,binding.handles[slot].idx); hash(result,binding.flags[slot]);
        hash(result,binding.signatures[slot]); hash(result,binding.sourceFingerprints[slot]);
    }
    return result;
}
bool coherent(const AssetLibrary& assets,std::span<const ModelMaterial> materials,std::span<const ModelMaterialTextures> bindings) {
    if (materials.size()!=bindings.size()) return false;
    std::unordered_map<std::string,uint64_t> versions;
    for (size_t m=0;m<materials.size();++m) {
        const auto& material=materials[m]; const std::array<const ModelTexture*,5> slots{
            &material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture};
        for (size_t slot=0;slot<slots.size();++slot) {
            if (slots[slot]->image.empty()) continue;
            const auto id=assets.references().resolveTexture(slots[slot]->image.id,slot==2);
            const auto fingerprint=bindings[m].sourceFingerprints[slot];
            if (!fingerprint) continue; // Legacy diagnostic textures are not streamed.
            const auto [found,inserted]=versions.emplace(id,fingerprint);
            if (!inserted && found->second!=fingerprint) return false;
        }
    }
    return true;
}
float verticalFov(const Camera& camera,const Renderer& renderer,ResourcePriority priority,float requestedAspect) {
    const auto aspect=std::isfinite(requestedAspect) && requestedAspect>0 ? requestedAspect :
        priority==ResourcePriority::Preview || !renderer.height() ? 1.0f :
        static_cast<float>(renderer.width())/static_cast<float>(renderer.height());
    return camera.verticalFov(aspect)*std::numbers::pi_v<float>/180;
}
uint32_t textureDemand(const ModelAsset& asset,std::span<const ModelMaterial> materials,const Mat4& world,const Camera& camera,float fov,uint32_t height) {
    const auto bounds=math::transformBounds(asset.bounds,world);
    if (!bounds.valid || !height || !std::isfinite(fov) || fov<=0) return 64;
    const auto center=(bounds.minimum+bounds.maximum)*.5f;
    const auto radius=math::length(bounds.maximum-bounds.minimum)*.5;
    const double distance=math::length(camera.eye()-center)-radius;
    if (distance<=.001) return 16384;
    double span=std::numeric_limits<double>::infinity();
    if (asset.stream) for (const auto& primitive:asset.stream->primitives) {
        if (primitive.material>=materials.size()) continue;
        const auto& material=materials[primitive.material];
        for (const auto* slot:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
            if (slot->image.empty() || slot->texCoord>1) continue;
            const auto& uv=primitive.uvSpan[slot->texCoord];
            const double used=std::max<double>(uv[0],uv[1]);
            if (used>1e-6) span=std::min(span,used);
        }
    }
    if (!std::isfinite(span)) span=1;
    const double diameter=radius*height/(distance*std::tan(fov*.5f));
    return static_cast<uint32_t>(std::clamp(std::ceil(diameter/span),64.0,16384.0));
}
struct Pins {
    std::shared_ptr<const ModelAsset> asset;
    std::vector<std::shared_ptr<const void>> textures;
};
}
EditorResources::EditorResources(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,std::filesystem::path root)
    :assets_(assets),renderer_(renderer),resources_(resources),root_(std::move(root)) {}
void EditorResources::beginFrame() {
    framePins_.clear(); externalModels_.clear(); status_.clear();
    for (auto& [id,entry]:entries_) { (void)id; entry.wanted=false; }
}
void EditorResources::endFrame() {
    const auto now=std::chrono::steady_clock::now();
    auto live=externalModels_;
    for (auto& [id,entry]:entries_) {
        if (entry.wanted) {
            if (entry.current) live.push_back(entry.current->asset->id);
            if (!entry.geometryAssetId.empty()) live.push_back(entry.geometryAssetId);
            if (!entry.diagnostic.empty()) status_+=assets_.displayName(entry.sourceId)+": "+entry.diagnostic+"\n";
        }
    }
    std::erase_if(entries_,[&](const auto& pair){return !pair.second.wanted && now-pair.second.used>resources_.config().idleGrace;});
    const auto reserved=renderer_.shadowResidentBytes()+renderer_.thumbnailResidentBytes();
    renderer_.trimModelCache(reserved>=resources_.config().gpuRetentionBytes?0:resources_.config().gpuRetentionBytes-reserved,live);
}
void EditorResources::requestGeometry(Entry& entry,const ModelAsset& asset,uint64_t revision,uint32_t primitive,uint32_t level,ResourcePriority priority) {
    if (!asset.stream || primitive>=asset.stream->primitives.size() || level>=asset.stream->primitives[primitive].levels.size()) return;
    const auto key=std::to_string(primitive)+":"+std::to_string(level);
    if (renderer_.modelLodReady(asset.id,revision,primitive,level)) { entry.geometry.erase(key); return; }
    auto& request=entry.geometry[key];
    if (!request) {
        const auto descriptor=asset.stream->primitives[primitive].levels[level];
        ResourceRequestOptions options; options.priority=priority; options.dependencies={descriptor.file}; options.assetDependencies={asset.id};
        options.variant="lod:"+std::to_string(asset.stream->key)+":"+key;
        options.forceReload=entry.repairRevision!=0;
        request=resources_.request<ModelGeometryLevel>(AssetRef<ModelGeometryLevel>{asset.id},
            [descriptor,primitive,level](std::stop_token token) {
                if (token.stop_requested()) throw std::runtime_error("Model LOD read canceled.");
                auto data=streaming::loadModelLod(descriptor,primitive,level);
                if (token.stop_requested()) throw std::runtime_error("Model LOD read canceled.");
                const auto bytes=data->geometry.vertices.size()*sizeof(ModelVertex)+data->geometry.indices.size()*sizeof(uint32_t)+data->indices16.size()*sizeof(uint16_t);
                return ResourceLoadResult<ModelGeometryLevel>{data,bytes,descriptor.key,{descriptor.file}};
            },std::move(options));
    }
    if (request.state()==ResourceState::Failed) {
        entry.diagnostic=request.diagnostic();
        if (entry.repairRevision!=revision) { entry.repairRevision=revision; assets_.rebuildModelCache(asset.id); entry.failed=false; }
        else entry.failed=!assets_.modelLoading(asset.id);
        return;
    }
    if (request.state()==ResourceState::Ready) if (const auto data=request.snapshot())
        if (!renderer_.queueModelLod(asset.id,revision,data.shared())) { entry.diagnostic=renderer_.modelStatus(); entry.failed=true; }
}
std::shared_ptr<const PreparedEditorModel> EditorResources::prepare(const std::string& id,const Mat4& world,const Camera& camera,
    uint32_t height,bool selected,ResourcePriority priority,float aspect,const std::string& materialOverride) {
    // Geometry identity remains shared. Immutable binding packets are keyed by
    // instance material identity so two Boxes cannot overwrite each other's material.
    const auto key=id+std::string(1,'\0')+materialOverride;
    auto& entry=entries_[key];entry.sourceId=id;entry.wanted=true; entry.used=std::chrono::steady_clock::now();
    entry.failed=false;
    const auto retain=[&](std::shared_ptr<const PreparedEditorModel> model) {
        if (model) framePins_.push_back(model); return model;
    };
    if (assets_.references().resolve(id).empty()) { entry.current.reset(); entry.geometry.clear(); entry.failed=true; return {}; }
    const auto asset=assets_.findModel(id);
    entry.diagnostic=assets_.modelDiagnostic(id);
    if (!asset) { entry.failed=!assets_.modelLoading(id); if (entry.diagnostic.empty()) entry.diagnostic=assets_.modelLoading(id) ? "Loading model..." : "Model is unavailable."; return retain(entry.current); }
    if (!entry.diagnostic.empty() && !assets_.modelLoading(id)) entry.failed=true;
    const auto revision=std::max<uint64_t>(1,assets_.modelRevision(id));
    entry.geometryAssetId=asset->id;
    try {
        const auto fov=verticalFov(camera,renderer_,priority,aspect);
        bool refined=true;
        if (asset->stream) {
            if (entry.geometryRevision!=revision) { entry.geometry.clear(); entry.geometryRevision=revision; }
            renderer_.configureModelStream(asset->id,revision,*asset->stream);
            std::vector<uint32_t> desired(asset->stream->primitives.size());
            for (size_t primitive=0;primitive<desired.size();++primitive)
                desired[primitive]=static_cast<uint32_t>(asset->stream->primitives[primitive].levels.size()-1);
            for (const auto& draw:asset->draws) if (draw.primitive<desired.size()) {
                const auto& primitive=asset->stream->primitives[draw.primitive];
                const auto level=selected ? 0 : streaming::chooseModelLod(primitive.levels,world*draw.transform,primitive.bounds,camera.eye(),fov,height,1.0f);
                desired[draw.primitive]=std::min(desired[draw.primitive],level);
            }
            for (uint32_t primitive=0;primitive<desired.size();++primitive) {
                const auto coarse=static_cast<uint32_t>(asset->stream->primitives[primitive].levels.size()-1);
                requestGeometry(entry,*asset,revision,primitive,coarse,priority);
                if (desired[primitive]!=coarse) requestGeometry(entry,*asset,revision,primitive,desired[primitive],selected ? ResourcePriority::Selected : priority);
                refined&=renderer_.modelLodReady(asset->id,revision,primitive,desired[primitive]);
            }
            if (!renderer_.modelStreamReady(asset->id,revision)) return retain(entry.current);
        } else if (!renderer_.prepareModel(*asset)) { entry.diagnostic=renderer_.modelStatus(); entry.failed=true; return retain(entry.current); }
        auto materials=assets_.resolvedModelMaterials(*asset,materialOverride);
        if (!assets_.modelMaterialsReady(*asset,materialOverride)) {
            const auto error=assets_.modelMaterialsDiagnostic(*asset,materialOverride);
            if (!error.empty()) { entry.diagnostic=error; entry.failed=true; }
            return retain(entry.current);
        }
        assets_.setTextureDemand(textureDemand(*asset,materials,world,camera,fov,height),selected);
        auto textures=assets_.modelBindings(*asset,materials);
        if (!assets_.modelBindingsReady(textures) || !coherent(assets_,materials,textures)) return retain(entry.current);
        if (std::any_of(textures.begin(),textures.end(),[](const auto& binding){return !binding.sourceReady;})) return retain(entry.current);
        refined&=std::all_of(textures.begin(),textures.end(),[](const auto& binding){return binding.complete;});
        const auto value=signature(*asset,revision,materials,textures);
        if (entry.current && entry.current->signature==value && entry.current->refined==refined) return retain(entry.current);
        auto candidate=std::make_shared<PreparedEditorModel>(); candidate->asset=asset; candidate->revision=revision;
        candidate->materials=std::move(materials); candidate->textures=std::move(textures); candidate->signature=value; candidate->ready=true; candidate->refined=refined;
        auto pins=std::make_shared<Pins>(); pins->asset=asset;
        for (const auto& binding:candidate->textures) for (const auto& texture:binding.leases) if (texture) pins->textures.push_back(texture);
        candidate->pinned=std::move(pins); entry.current=std::move(candidate); return retain(entry.current);
    } catch (const std::exception& error) { entry.diagnostic=error.what(); entry.failed=true; return retain(entry.current); }
}
std::shared_ptr<const PreparedEditorShadows> EditorResources::prepareShadows(const ExtractedScene& scene,std::uint16_t resolution,
    ResourcePriority priority) {
    auto result=std::make_shared<PreparedEditorShadows>();
    if(std::none_of(scene.points.begin(),scene.points.end(),[](const auto& light){return light.enabled && light.castShadows;}))return result;
    for(const auto& model:scene.models) {
        if(!model.castShadows)continue;
        const auto asset=assets_.findModel(model.assetId);
        const auto bounds=asset?math::transformBounds(asset->bounds,model.world):Aabb{};
        std::shared_ptr<const PreparedEditorModel> packet;
        for(const auto& light:scene.points) {
            if(!light.enabled || !light.castShadows)continue;
            if(!point_shadow::intersectsLight(bounds,light.position,light.radius))continue;
            const auto camera=point_shadow::faceCamera(light.position,light.radius,0,resolution);
            // All six faces have the same footprint. Each light requests its
            // detail once; renderer selection remains independent per light.
            auto prepared=prepare(model.assetId,model.world,camera,resolution+4,false,priority,1,model.materialId);
            if(prepared) { packet=prepared;result->models.push_back(prepared); }
            result->refined&=prepared && prepared->ready && prepared->refined;
        }
        if(packet) {
            RenderModelInstance instance{model.id,{packet->asset,model.world},&packet->textures,packet->revision,false,false,&packet->materials};
            instance.castShadows=model.castShadows;instance.receiveShadows=model.receiveShadows;
            result->instances.push_back(instance);
        }
    }
    return result;
}
void EditorResources::invalidate(const std::string& id) {
    for(auto& [key,entry]:entries_)if(entry.sourceId==id) {entry.geometry.clear();entry.geometryRevision=0;}
}
void EditorResources::clear() { entries_.clear(); framePins_.clear(); externalModels_.clear(); status_.clear(); }
bool EditorResources::failed(const std::string& id) const {
    const auto found=entries_.find(id+std::string(1,'\0')); return found!=entries_.end() && found->second.failed;
}
} // namespace crimson
