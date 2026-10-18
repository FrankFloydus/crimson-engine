#include "EditorResources.h"
#include "crimson-renderer/ShadowLightSelection.h"
#include "PreparedMaterialSignature.h"
#include "assets/AssetLibrary.h"
#include "assets/ModelAnimation.h"
#include "assets/MaterialGraphReferences.h"
#include "resources/StreamingModel.h"
#include "resources/CacheIO.h"
#include "scene/SceneExtraction.h"
#include "crimson-renderer/PointShadowMath.h"
#include "crimson-renderer/DirectionalShadowMath.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

namespace crimson {
namespace {
using streaming::detail::hash;
bool coherent(const AssetLibrary& assets,std::span<const ModelMaterial> materials,std::span<const ModelMaterialTextures> bindings) {
    if (materials.size()!=bindings.size()) return false;
    std::unordered_map<std::string,uint64_t> versions;
    for (size_t m=0;m<materials.size();++m) {
        if(const auto graph=bindings[m].graph) {
            if(!preparedGraphSourcesCoherent(*graph,versions))return false;
            continue;
        }
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
uint32_t textureDemand(const ModelAsset& asset,std::span<const ModelMaterial> materials,const Mat4& world,const Camera& camera,float fov,uint32_t height,float shadowTexelWorld,MaterialEvaluationContext context,const ModelPose* pose) {
    const auto bounds=math::transformBounds(pose?pose->bounds:asset.bounds,world);
    if (!bounds.valid || !height || !std::isfinite(fov) || fov<=0) return 64;
    const auto center=(bounds.minimum+bounds.maximum)*.5f;
    const auto radius=math::length(bounds.maximum-bounds.minimum)*.5;
    const double distance=math::length(camera.eye()-center)-radius;
    if (shadowTexelWorld<=0 && distance<=.001) return 16384;
    double span=std::numeric_limits<double>::infinity();
    const auto mappings=[context](const ModelMaterial& material,const auto& visit) {
        if(material.graph) {
            const auto& graph=*material.graph;const auto& output=materialOutputNode(graph).output;
            for(const auto id:materialGraphOrder(graph,output.shading,output.blend)) {
                const auto* node=findMaterialNode(graph,id);if(node->kind!=MaterialNodeKind::Grunge&&(node->kind!=MaterialNodeKind::Texture2D||node->texture.image.empty()))continue;
                MaterialUvMapping mapping;
                mapping=materialTextureMapping(graph,id,context);
                if(node->kind==MaterialNodeKind::Grunge) {
                    mapping.scale=mapping.scale*static_cast<float>(node->grunge.tiling);
                    mapping.offset=mapping.offset*static_cast<float>(node->grunge.tiling);
                }
                visit(mapping);
            }
        } else {
            for(const auto* slot:{&material.baseColorTexture,&material.metallicRoughnessTexture,&material.normalTexture,&material.occlusionTexture,&material.emissiveTexture}) {
                if(slot->image.empty())continue;
                MaterialUvMapping mapping;mapping.scale=material.uvScale;
                mapping.mode=material.triplanar?(material.triplanarWorld?MaterialUvMode::WorldTriplanar:MaterialUvMode::LocalTriplanar):
                    slot->texCoord==1?MaterialUvMode::UV1:MaterialUvMode::UV0;visit(mapping);
            }
        }
    };
    const auto scaleSpan=[](const MaterialUvMapping& mapping) {
        float scale=std::min(std::abs(mapping.scale[0]),std::abs(mapping.scale[1]));
        if(mapping.mode==MaterialUvMode::LocalTriplanar || mapping.mode==MaterialUvMode::WorldTriplanar)scale=std::min(scale,std::abs(mapping.scale[2]));
        return std::max(1e-6f,scale);
    };
    const auto worldMappingScale=[](const Mat4& transform) {
        // A lower bound on the smallest world-space stretch. The inverse's
        // Frobenius norm is conservative for rotations, reflections and shear;
        // underestimating the span requests more detail, never blurrier mips.
        const auto inverse=math::inverse(transform);
        if(!inverse)return 1e-6;
        double squared{};
        for(size_t row=0;row<3;++row)for(size_t column=0;column<3;++column) {
            const double value=inverse->at(row,column);squared+=value*value;
        }
        return squared>0 && std::isfinite(squared)?std::max(1e-6,1/std::sqrt(squared)):1e-6;
    };
    if (asset.stream) for (std::size_t drawIndex=0;drawIndex<asset.draws.size();++drawIndex) {
        const auto& draw=asset.draws[drawIndex];
        const auto& drawTransform=pose&&drawIndex<pose->drawTransforms.size()?pose->drawTransforms[drawIndex]:draw.transform;
        if(draw.primitive>=asset.stream->primitives.size())continue;
        const auto& primitive=asset.stream->primitives[draw.primitive];
        if (primitive.material>=materials.size()) continue;
        const auto& material=materials[primitive.material];
        mappings(material,[&](const MaterialUvMapping& mapping) {
            const double transformScale=mapping.mode==MaterialUvMode::WorldTriplanar?worldMappingScale(world*drawTransform):1;
            const auto& uv=primitive.uvSpan[mapping.mode==MaterialUvMode::UV1?1:0];
            double used=std::max<double>(uv[0],uv[1]);
            if(mapping.mode==MaterialUvMode::LocalTriplanar || mapping.mode==MaterialUvMode::WorldTriplanar) {
                const auto extent=primitive.bounds.maximum-primitive.bounds.minimum;
                used=std::numeric_limits<double>::infinity();
                for(const float axis:extent)if(axis>1e-6f)used=std::min(used,static_cast<double>(axis));
                if(!std::isfinite(used))used=1;
            }
            used*=scaleSpan(mapping)*transformScale;
            if(used>0)span=std::min(span,std::max(1e-6,used));
        });
    }
    else for(const auto& material:materials)mappings(material,[&](const MaterialUvMapping& mapping) {
        double transformScale=1;
        if(mapping.mode==MaterialUvMode::WorldTriplanar) {
            transformScale=worldMappingScale(world);
            for(std::size_t drawIndex=0;drawIndex<asset.draws.size();++drawIndex) {
                const auto& transform=pose&&drawIndex<pose->drawTransforms.size()?pose->drawTransforms[drawIndex]:asset.draws[drawIndex].transform;
                transformScale=std::min(transformScale,worldMappingScale(world*transform));
            }
        }
        span=std::min(span,scaleSpan(mapping)*transformScale);
    });
    if (!std::isfinite(span)) span=1;
    const double diameter=shadowTexelWorld>0?2*radius/shadowTexelWorld:radius*height/(distance*std::tan(fov*.5f));
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
    for(auto& [id,pose]:poses_)pose.wanted=false;
    framePins_.clear(); externalModels_.clear(); status_.clear();
    for (auto& [id,entry]:entries_) { (void)id; entry.wanted=false; }
}
void EditorResources::endFrame() {
    std::erase_if(poses_,[](const auto& item){return !item.second.wanted;});
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
    // The retention target controls evictable caches. Fixed render targets
    // (including shadows) must not evict the model/texture working set.
    const auto reserved=renderer_.thumbnailResidentBytes();
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
                auto bytes=data->geometry.vertices.size()*sizeof(ModelVertex)+data->geometry.indices.size()*sizeof(uint32_t)+data->indices16.size()*sizeof(uint16_t)
                    +data->geometry.skinWeights.size()*sizeof(ModelSkinWeights)+data->geometry.morphTargets.size()*sizeof(ModelMorphTarget);
                for(const auto& target:data->geometry.morphTargets)bytes+=(target.positions.size()+target.normals.size()+target.tangents.size())*sizeof(Vec3);
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
    uint32_t height,bool selected,ResourcePriority priority,float aspect,std::span<const AssetRef<MaterialAsset>> materialOverrides,float shadowTexelWorld,const ExtractedModel* presentation) {
    // Geometry identity remains shared. Immutable binding packets are keyed by
    // the complete slot assignment so instances cannot overwrite each other.
    // Canonicalize trailing inherited slots; embedded separators retain slot order.
    while(!materialOverrides.empty()&&materialOverrides.back().empty())materialOverrides=materialOverrides.first(materialOverrides.size()-1);
    auto key=id+std::string(1,'\0');
    for(const auto& material:materialOverrides){key+=material.id;key+='\0';}
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
        const auto presentedPose=presentation?pose(*presentation,asset):std::shared_ptr<const ModelPose>{};
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
                const auto level=selected || !asset->animations.empty() ? 0 : shadowTexelWorld>0?directional_shadow::chooseLod(primitive.levels,world*draw.transform,shadowTexelWorld):
                    streaming::chooseModelLod(primitive.levels,world*draw.transform,primitive.bounds,camera.eye(),fov,height,1.0f);
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
        auto materials=assets_.resolvedModelMaterials(*asset,materialOverrides);
        if (!assets_.modelMaterialsReady(*asset,materialOverrides)) {
            const auto error=assets_.modelMaterialsDiagnostic(*asset,materialOverrides);
            if (!error.empty()) { entry.diagnostic=error; entry.failed=true; }
            return retain(entry.current);
        }
        if(const auto diagnostic=assets_.modelMaterialsDiagnostic(*asset,materialOverrides);!diagnostic.empty())entry.diagnostic=diagnostic;
        // Cached browser cards render at time zero; they do not inherit the live
        // world's animated mapping footprint when sharing this resource owner.
        const auto context=priority==ResourcePriority::Preview?MaterialEvaluationContext{}:materialContext_;
        assets_.setTextureDemand(textureDemand(*asset,materials,world,camera,fov,height,shadowTexelWorld,context,presentedPose.get()),selected);
        auto textures=assets_.modelBindings(*asset,materials);
        for(const auto& binding:textures)if(binding.graphPending) {
            entry.diagnostic=binding.diagnostic;refined=false;
            if(entry.current && std::none_of(entry.current->textures.begin(),entry.current->textures.end(),
                [](const auto& previous){return previous.graphPending;}))return retain(entry.current);
        }
        if (!assets_.modelBindingsReady(textures) || !coherent(assets_,materials,textures)) return retain(entry.current);
        if (std::any_of(textures.begin(),textures.end(),[](const auto& binding){return !binding.sourceReady;})) return retain(entry.current);
        refined&=std::all_of(textures.begin(),textures.end(),[](const auto& binding){return binding.complete;});
        const auto value=preparedModelSignature(*asset,revision,materials,textures);
        if (entry.current && entry.current->signature==value && entry.current->refined==refined) return retain(entry.current);
        auto candidate=std::make_shared<PreparedEditorModel>(); candidate->asset=asset; candidate->revision=revision;
        candidate->materials=std::move(materials); candidate->textures=std::move(textures); candidate->signature=value; candidate->ready=true; candidate->refined=refined;
        auto pins=std::make_shared<Pins>(); pins->asset=asset;
        for (const auto& binding:candidate->textures) for (const auto& texture:binding.leases) if (texture) pins->textures.push_back(texture);
        candidate->pinned=std::move(pins); entry.current=std::move(candidate); return retain(entry.current);
    } catch (const std::exception& error) { entry.diagnostic=error.what(); entry.failed=true; return retain(entry.current); }
}
std::shared_ptr<const ModelPose> EditorResources::pose(const ExtractedModel& model,const std::shared_ptr<const ModelAsset>& asset) {
    if(!asset || (asset->skins.empty()&&asset->animations.empty()&&std::none_of(asset->primitives.begin(),asset->primitives.end(),
        [](const auto& primitive){return !primitive.morphTargets.empty();})))return {};
    auto& entry=poses_[model.id];entry.wanted=true;
    const auto clip=model.animation.clip;
    if(entry.value&&entry.source.lock()==asset&&entry.clip==clip&&entry.seconds==model.animationSeconds&&entry.loop==model.animation.loop)return entry.value;
    const auto selected=clip>=0&&static_cast<std::size_t>(clip)<asset->animations.size()?std::optional<std::size_t>{static_cast<std::size_t>(clip)}:std::nullopt;
    if(clip>=0&&!selected)status_+="Animation clip is unavailable for "+asset->name+".\n";
    const auto sampleTime=model.animationSeconds+(selected?asset->animations[*selected].start:0.);
    auto value=std::make_shared<const ModelPose>(evaluateModelPose(*asset,selected,sampleTime,model.animation.loop));
    if(entry.value&&entry.source.lock()==asset&&value->signature==entry.value->signature&&value->drawTransforms==entry.value->drawTransforms&&
        value->skinPalettes==entry.value->skinPalettes&&value->nodeWeights==entry.value->nodeWeights&&
        value->localTransforms==entry.value->localTransforms&&value->worldTransforms==entry.value->worldTransforms)value=entry.value;
    entry={asset,value,clip,model.animationSeconds,model.animation.loop,true};
    return value;
}
Aabb EditorResources::modelBounds(const ExtractedModel& model,const std::shared_ptr<const ModelAsset>& asset) {
    if(!asset)return {};
    const auto value=pose(model,asset);return value?value->bounds:asset->bounds;
}
std::shared_ptr<const PreparedEditorShadows> EditorResources::prepareShadows(const ExtractedScene& scene,std::uint16_t resolution,
    ResourcePriority priority,std::span<const ShadowCamera> cameras,std::span<const std::uint64_t> focusEntities,std::optional<Vec3> priorityOrigin,std::span<const Aabb> extraReceiverBounds) {
    auto result=std::make_shared<PreparedEditorShadows>();
    const auto shadowSelection=selectPointShadowLights(scene.points,priorityOrigin);
    const bool directional=std::any_of(scene.directional.begin(),scene.directional.end(),[](const auto& light){return light.enabled&&light.castShadows;});
    if(!directional && std::none_of(scene.points.begin(),scene.points.end(),[](const auto& light){return light.enabled && light.castShadows;}))return result;
    const std::array<ShadowCamera,1> fallback{{{Camera{},1.f}}};if(cameras.empty())cameras=fallback;
    const auto focused=[&](std::uint64_t id){return std::find(focusEntities.begin(),focusEntities.end(),id)!=focusEntities.end();};
    std::vector<Aabb> allBounds,worldCasterBounds;
    if(directional){allBounds.reserve(scene.models.size());worldCasterBounds.reserve(scene.models.size());result->receiverBounds.reserve(scene.models.size()+extraReceiverBounds.size());
        result->receiverBounds.insert(result->receiverBounds.end(),extraReceiverBounds.begin(),extraReceiverBounds.end());}
    if(directional)for(const auto& model:scene.models) {
        if(!model.visible)continue;
        const auto asset=assets_.findModel(model.assetId);
        const auto bounds=asset?math::transformBounds(modelBounds(model,asset),model.world):Aabb{};
        // Freeze receiver metadata before residency/camera preparation so both
        // resource demand and execution use exactly the same projection fit.
        if(model.receiveShadows)result->receiverBounds.push_back(bounds);
        if(model.castShadows){allBounds.push_back(bounds);if(!focused(model.id))worldCasterBounds.push_back(bounds);}
    }
    std::vector<directional_shadow::Cascades> fits;
    if(directional)for(const auto& light:scene.directional)if(light.enabled&&light.castShadows)
        for(const auto& camera:cameras) {
            if(camera.gameplay) {
                auto fit=directional_shadow::buildGame(light,camera,resolution,worldCasterBounds,camera.focusBounds,result->receiverBounds);
                if(fit.characterProjection>=fit.projectionCount)fit=directional_shadow::buildGame(light,camera,resolution,allBounds,{},result->receiverBounds);
                fits.push_back(std::move(fit));
            } else fits.push_back(directional_shadow::build(light,camera,resolution,allBounds));
        }
    std::vector<Aabb> footprintBounds;
    for(const auto& model:scene.models) {
        if(!model.visible || !model.castShadows)continue;
        const auto asset=assets_.findModel(model.assetId);
        const auto bounds=asset?math::transformBounds(modelBounds(model,asset),model.world):Aabb{};
        footprintBounds.clear();footprintBounds.push_back(bounds);
        if(asset && asset->stream && !pose(model,asset)) {
            footprintBounds.reserve(asset->draws.size()+1);
            for(const auto& draw:asset->draws)if(draw.primitive<asset->stream->primitives.size())
                footprintBounds.push_back(math::transformBounds(asset->stream->primitives[draw.primitive].bounds,model.world*draw.transform));
        }
        std::shared_ptr<const PreparedEditorModel> packet;
        float texel=std::numeric_limits<float>::max();
        // Unknown bounds still request the player's original detail, so an
        // offscreen pending model can become a caster without a visible draw.
        for(const auto& fit:fits)if(fit.valid)for(unsigned projection=0;projection<fit.projectionCount;++projection) {
            if(fit.characterProjection<fit.projectionCount && focused(model.id)!=(projection==fit.characterProjection))continue;
            if(directional_shadow::intersects(bounds,fit.values[projection]))texel=std::min(texel,fit.values[projection].texelWorld);
        }
        for(const auto source:shadowSelection.selectedSources()) {
            const auto& light=scene.points[source];
            if(!point_shadow::intersectsLight(bounds,light.position,light.radius))continue;
            // A parent's circumsphere need not contain its children's
            // circumspheres. Preserve each draw's original perspective error
            // bound as well as the model-wide texture-density bound.
            for(const auto& footprint:footprintBounds)
                texel=std::min(texel,point_shadow::casterTexelWorld(footprint,light.position,resolution));
        }
        if(texel<std::numeric_limits<float>::max()) {
            // Combine light demand before material/texture preparation. A room
            // lit by several points needs one coherent packet per caster, not
            // repeated material resolution and graph preparation for each light.
            auto prepared=prepare(model.assetId,model.world,cameras.front().camera,resolution+4,false,priority,cameras.front().aspect,model.materialOverrides,texel,&model);
            if(prepared){packet=prepared;result->models.push_back(prepared);}
            result->ready&=prepared&&prepared->ready;
            result->refined&=prepared&&prepared->ready&&prepared->refined;
        }
        if(packet) {
            RenderModelInstance instance{model.id,{packet->asset,model.world},&packet->textures,packet->revision,false,false,&packet->materials};
            instance.castShadows=model.castShadows;instance.receiveShadows=model.receiveShadows;
            instance.lightingScopes=model.lightingScopes;
            instance.lightingCategory=model.lightingCategory;instance.actorOwner=model.actorOwner;
            instance.pose=pose(model,packet->asset);
            result->instances.push_back(instance);
        }
    }
    return result;
}
void EditorResources::invalidate(const std::string& id) {
    for(auto& [key,entry]:entries_)if(entry.sourceId==id) {entry.geometry.clear();entry.geometryRevision=0;}
}
void EditorResources::clear() { entries_.clear();poses_.clear(); framePins_.clear(); externalModels_.clear(); status_.clear(); }
bool EditorResources::failed(const std::string& id) const {
    const auto found=entries_.find(id+std::string(1,'\0')); return found!=entries_.end() && found->second.failed;
}
} // namespace crimson
