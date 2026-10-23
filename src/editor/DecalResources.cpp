#include "DecalResources.h"
#include "decals/DecalResolve.h"
#include "decals/DecalCache.h"
#include "assets/AssetLibrary.h"
#include "resources/StreamingModel.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace crimson {
namespace {
struct Hash {
    uint64_t value{1469598103934665603ull};
    void bytes(const void*p,size_t n){const auto*b=static_cast<const unsigned char*>(p);for(size_t i=0;i<n;++i){value^=b[i];value*=1099511628211ull;}}
    template<class T>void add(const T&v){bytes(&v,sizeof(v));}
    void add(const std::string&s){bytes(s.data(),s.size());const unsigned char zero=0;bytes(&zero,1);}
};
bool overlaps(const Aabb&a,const Aabb&b){if(!a.valid||!b.valid)return true;for(size_t i=0;i<3;++i)if(a.minimum[i]>b.maximum[i]||b.minimum[i]>a.maximum[i])return false;return true;}
std::vector<bool> eligibleSlots(AssetLibrary&assets,const ExtractedModel&m,const ModelAsset&model){
    const auto materials=assets.resolvedModelMaterials(model,m.materialOverrides);std::vector<bool>result;result.reserve(materials.size());for(const auto&material:materials)result.push_back(decals::receiverMaterialEligible(material));return result;
}
bool eligibleDraw(const decals::ModelSurface&s,uint32_t draw,uint32_t primitive,const std::vector<bool>&slots){
    const auto it=std::find_if(s.draws.begin(),s.draws.end(),[&](const auto&d){return d.surface.id.draw==draw&&d.surface.id.primitive==primitive;});return it!=s.draws.end()&&it->material<slots.size()&&slots[it->material];
}
bool staticReceiver(const ExtractedModel&m){return m.visible&&!m.actorOwner&&m.animation.asset.empty();}
struct Candidate {std::shared_ptr<const decals::ModelSurface> source;Mat4 world;EntityId entity;uint64_t revision;std::vector<bool>slots;};
std::shared_ptr<const DecalPatchData> renderPatch(const decals::DecalPatch&cpu,uint64_t signature,const std::unordered_map<EntityId,uint64_t>& revisions){
    auto result=std::make_shared<DecalPatchData>();result->fingerprint=signature;
    for(const auto&chunk:cpu.chunks){const uint32_t firstVertex=static_cast<uint32_t>(result->vertices.size()),firstIndex=static_cast<uint32_t>(result->indices.size());
        for(const auto&vertex:chunk.vertices){result->vertices.push_back({vertex.position,vertex.baseNormal,vertex.uv});math::include(result->bounds,vertex.position);}
        for(uint32_t i:chunk.indices)result->indices.push_back(firstVertex+i);
        result->receivers.push_back({chunk.receiver.entity,chunk.receiver.draw,chunk.receiver.primitive,firstIndex,static_cast<uint32_t>(chunk.indices.size()),revisions.at(chunk.receiver.entity)});
    }return result;
}
}
DecalResources::DecalResources(AssetLibrary&assets,ResourceManager&resources):assets_(assets),resources_(resources){}
DecalSourceResult DecalResources::modelSurface(const ExtractedModel&m,ResourcePriority priority){
    DecalSourceResult result;if(assets_.references().resolve(m.assetId).empty()){result.failed=true;result.diagnostic="Missing surface model.";return result;}const auto model=assets_.findModel(m.assetId,m.primitive);
    result.diagnostic=assets_.modelDiagnostic(m.assetId);
    if(!model||assets_.modelLoading(m.assetId)||!result.diagnostic.empty()){result.failed=!result.diagnostic.empty();if(result.diagnostic.empty())result.diagnostic="Preparing surface geometry.";return result;}
    result.revision=std::max<uint64_t>(1,assets_.modelRevision(m.assetId));
    result.fingerprint=model->stream?model->stream->key:assets_.modelFingerprint(m.assetId);Hash identity;identity.add(model->id);identity.add(result.fingerprint);
    // The stream key covers original primitive attributes. Draw occurrence
    // transforms are separate metadata and must also invalidate the source.
    for(const auto&draw:model->draws){identity.add(draw.primitive);identity.add(draw.transform);}result.fingerprint=identity.value;
    const auto mode=static_cast<decals::SourceNormals>(m.meshShading);
    const std::string key=model->id+":"+std::to_string(result.fingerprint)+":"+std::to_string(static_cast<unsigned>(mode));
    auto&entry=sources_[key];entry.used=std::chrono::steady_clock::now();entry.fingerprint=result.fingerprint;
    const bool retry=entry.resource&&entry.resource.state()==ResourceState::Failed&&entry.revision!=result.revision;
    if(!entry.resource||retry){ResourceRequestOptions options;options.priority=priority;options.retainLoader=false;options.cancelWhenUnused=true;options.assetDependencies={m.assetId};options.forceReload=retry;
        if(model->stream)for(const auto&primitive:model->stream->primitives)if(!primitive.levels.empty())options.dependencies.push_back(primitive.levels[0].file);
        entry.revision=result.revision;
        entry.resource=resources_.requestDerived<decals::ModelSurface>("decal-surface:"+key,[model,mode,fingerprint=result.fingerprint](std::stop_token stop){
            const auto original=[model](uint32_t primitive,std::stop_token token)->std::shared_ptr<const ModelPrimitive>{
                if(!model->stream||primitive>=model->stream->primitives.size()||model->stream->primitives[primitive].levels.empty())return {};
                auto lod=streaming::loadModelLod(model->stream->primitives[primitive].levels[0],primitive,0,false,token);return {lod,&lod->geometry};
            };
            auto surface=std::make_shared<decals::ModelSurface>(decals::buildModelSurface(*model,fingerprint,mode,original,stop));return ResourceLoadResult<decals::ModelSurface>{surface,surface->bytes(),fingerprint,{}};
        },std::move(options));
    }
    if(const auto snapshot=entry.resource.snapshot()){result.surface=snapshot.shared();result.current=entry.resource.state()==ResourceState::Ready;}
    result.diagnostic=entry.resource.diagnostic();result.failed=entry.resource.state()==ResourceState::Failed;
    if(result.failed&&model->stream&&!entry.repairRequested){entry.repairRequested=true;assets_.rebuildModelCache(m.assetId);result.failed=false;result.diagnostic="Preparing surface geometry.";}
    if(result.current&&result.surface->draws.empty()){result.current=false;result.failed=true;result.diagnostic=result.surface->diagnostic;}
    if(!result.surface&&result.diagnostic.empty())result.diagnostic="Preparing surface geometry.";return result;
}
DecalPlacementResult DecalResources::resolvePlacement(const Scene&scene,uint64_t entity,uint32_t draw,uint32_t primitive,uint64_t revision,Vec3 point){
    DecalPlacementResult result;const auto extraction=extractScene(scene);const auto found=std::find_if(extraction.models.begin(),extraction.models.end(),[&](const auto&m){return m.id==entity;});
    const auto fail=[&](const char*message){result.failed=true;result.diagnostic=message;return result;};
    if(found==extraction.models.end()||!staticReceiver(*found))return fail("Unsupported receiver.");
    const auto source=modelSurface(*found,ResourcePriority::Placement);if(revision&&source.revision&&revision!=source.revision)return fail("Surface changed; place the decal again.");
    if(!source.current){result.diagnostic=source.diagnostic;result.failed=source.failed;return result;}
    const auto model=assets_.peekModel(found->assetId,found->primitive);if(!model)return fail("Missing surface.");
    try{if(!eligibleDraw(*source.surface,draw,primitive,eligibleSlots(assets_,*found,*model)))return fail("Unsupported receiver material.");}catch(const std::exception&e){result.failed=true;result.diagnostic=e.what();return result;}
    const auto hit=decals::resolveModelSurfaceHit(*source.surface,entity,found->world,draw,primitive,point);
    if(!hit.hit)return fail(hit.status==decals::SurfaceHitStatus::Ambiguous?"Ambiguous surface.":"Visible surface no longer matches its source.");
    const auto normalMatrix=math::normalMatrix(found->world);if(!normalMatrix)return fail("Invalid receiver transform.");
    result.seed.receiver=entity;result.seed.sourceKey=found->sourceKey;result.seed.modelId=found->assetId;result.seed.fingerprint=source.fingerprint;
    result.seed.draw=draw;result.seed.primitive=primitive;result.seed.triangle=hit.hit->seed.triangle;result.seed.barycentric=hit.hit->seed.barycentrics;result.seed.localPoint=hit.hit->localPoint;result.seed.localNormal=hit.hit->localNormal;
    result.frame=decals::surfacePlacementFrame(math::transformPoint(found->world,hit.hit->localPoint),math::transformDirection(*normalMatrix,hit.hit->localNormal));result.ready=true;return result;
}
void DecalResources::append(DecalPreparedScene&out,const Scene&scene,const ExtractedScene&extracted,const ExtractedDecal&d,ResourcePriority priority){
    (void)scene;
    if(!d.visible||d.decal.opacity<=0){if(const auto existing=patches_.find(d.id);existing!=patches_.end())existing->second.current=false;return;}
    auto&entry=patches_[d.id];entry.current=false;entry.used=std::chrono::steady_clock::now();
    // A pending frame submits no packet, while retaining a compatible CPU
    // publication so texture preparation cannot trigger a chart rebuild.
    const auto pending=[&](std::string message,bool ready=false){entry.status=std::move(message);out.ready&=ready;out.refined=false;if(out.status.empty())out.status=entry.status;};
    const auto seedModel=std::find_if(extracted.models.begin(),extracted.models.end(),[&](const auto&m){return m.id==d.decal.seed.receiver;});
    if(seedModel==extracted.models.end()){pending("Missing surface.");return;}
    if(!staticReceiver(*seedModel)){pending("Unsupported receiver.");return;}
    if(!d.decal.seed.sourceKey.empty()&&seedModel->sourceKey!=d.decal.seed.sourceKey){pending("Receiver occurrence changed.");return;}
    const auto source=modelSurface(*seedModel,priority);if(!source.current){pending(source.diagnostic);return;}
    const auto seed=decals::resolveAuthoredSeed(*source.surface,d.decal.seed,seedModel->world,d.world);
    if(!seed.hit){pending(seed.diagnostic);return;}
    const auto center=math::transformPoint(d.world,{}),x=math::transformDirection(d.world,{1,0,0}),y=math::transformDirection(d.world,{0,1,0});
    const double radius=.625*std::max(math::length(x+y),math::length(x-y))+.5*std::min(math::length(x),math::length(y));
    if(!std::isfinite(radius)||radius<=0){pending("Invalid footprint.");return;}
    const Vec3 extent{radius,radius,radius};const Aabb region{center-extent,center+extent,true};
    std::vector<Candidate>candidates;std::unordered_map<EntityId,uint64_t> revisions;Hash bindings;Hash signature;signature.add(decals::DecalChartCacheVersion);signature.add(d.id);signature.add(d.world);signature.add(seed.hit->seed.receiver.entity);signature.add(seed.hit->seed.receiver.draw);signature.add(seed.hit->seed.receiver.primitive);signature.add(seed.hit->seed.triangle);signature.add(seed.hit->seed.barycentrics);
    bool seedEligible=false;
    for(const auto&m:extracted.models){
        if(!staticReceiver(m)||!decals::receiverPolicyAllows(d.decal.receivers,seedModel->id,seedModel->quaderOwner,m.id,m.quaderOwner))continue;
        const auto model=assets_.findModel(m.assetId,m.primitive);if(!model){pending("Preparing receiver models.");return;}
        if(!overlaps(math::transformBounds(model->bounds,m.world),region))continue;
        const auto s=modelSurface(m,priority);if(!s.current){pending(s.diagnostic);return;}
        std::vector<bool>slots;try{slots=eligibleSlots(assets_,m,*model);}catch(const std::exception&e){pending(e.what());return;}
        if(m.id==seedModel->id)seedEligible=eligibleDraw(*s.surface,d.decal.seed.draw,d.decal.seed.primitive,slots);
        if(std::none_of(slots.begin(),slots.end(),[](bool v){return v;}))continue;
        signature.add(m.id);signature.add(m.world);signature.add(s.fingerprint);signature.add(m.meshShading);for(bool eligible:slots)signature.add(eligible);
        revisions.emplace(m.id,s.revision);bindings.add(m.id);bindings.add(s.revision);
        candidates.push_back({s.surface,m.world,m.id,s.revision,std::move(slots)});out.receivers.push_back(m.id);
    }
    if(!seedEligible){pending("Unsupported receiver material.");return;}
    const auto key=signature.value;
    if(!entry.resource||entry.signature!=key){entry.signature=key;entry.publishedRevision=0;entry.render.reset();ResourceRequestOptions options;options.priority=priority;options.retainLoader=false;options.cancelWhenUnused=true;
        for(const auto&m:extracted.models)if(std::any_of(candidates.begin(),candidates.end(),[&](const auto&c){return c.entity==m.id;}))options.assetDependencies.push_back(m.assetId);
        decals::DecalBuildRequest request;request.seed=seed.hit->seed;request.frame=d.world;
        const auto cacheFile=decals::decalCachePath(assets_.projectRoot(),key);
        entry.resource=resources_.requestDerived<decals::DecalPatch>("decal-patch:"+std::to_string(key),[candidates=std::move(candidates),region,request,key,cacheFile](std::stop_token stop){
            decals::DecalSurfaceSnapshot snapshot;size_t triangles=0;
            for(const auto&candidate:candidates)for(const auto&draw:candidate.source->draws){if(stop.stop_requested())throw std::runtime_error("Decal preparation cancelled.");if(draw.material>=candidate.slots.size()||!candidate.slots[draw.material])continue;
                const auto indices=decals::querySurfaceTriangles(draw,candidate.world,region);if(indices.empty())continue;
                if((triangles+=indices.size())>request.limits.maximumInputTriangles)throw std::runtime_error("Decal receiver neighborhood exceeds its triangle budget.");
                decals::SurfaceReceiver receiver;receiver.id=draw.surface.id;receiver.id.entity=candidate.entity;receiver.generation=candidate.revision;receiver.localToWorld=candidate.world*draw.surface.localToWorld;receiver.triangles.reserve(indices.size());for(uint32_t i:indices)receiver.triangles.push_back(draw.surface.triangles[i]);snapshot.receivers.push_back(std::move(receiver));
            }
            auto cached=decals::readDecalCache(cacheFile,key,snapshot,request,stop);
            auto patch=std::make_shared<decals::DecalPatch>(cached?std::move(*cached):decals::buildDecalPatch(snapshot,request,stop));
            if(!cached&&patch->usable())decals::writeDecalCache(cacheFile,key,*patch,request.limits,stop);
            size_t bytes=sizeof(*patch);for(const auto&chunk:patch->chunks)bytes+=sizeof(chunk)+chunk.vertices.size()*sizeof(decals::DecalPatchVertex)+chunk.indices.size()*sizeof(uint32_t);return ResourceLoadResult<decals::DecalPatch>{patch,bytes,key,{}};
        },std::move(options));
    }
    const auto patch=entry.resource.snapshot();if(!patch){pending(entry.resource.state()==ResourceState::Failed?entry.resource.diagnostic():"Preparing decal surface.");return;}
    if(!patch->usable()){pending(patch->message);return;}
    if(entry.publishedRevision!=patch.revision()||entry.publishedBindings!=bindings.value){Hash published;published.add(key);published.add(bindings.value);entry.render=renderPatch(*patch,published.value,revisions);entry.publishedRevision=patch.revision();entry.publishedBindings=bindings.value;}
    entry.current=true;
    const auto material=assets_.preparedDecalMaterial(d.decal.asset.id,1024,priority);if(!material||!material->ready||!material->current||!material->render){pending(material&&!material->diagnostic.empty()?material->diagnostic:"Preparing decal material.");return;}
    entry.status=patch->message;out.decals.push_back({d.id,d.id,d.decal.priority,entry.render,material->render,d.decal.tint,d.decal.opacity,d.decal.edgeFeather});
    if(patch->status==decals::DecalPatchStatus::Incomplete&&!out.status.size())out.status=patch->message;
}
DecalPreparedScene DecalResources::prepare(const Scene&scene,const ExtractedScene&extracted,ResourcePriority priority){DecalPreparedScene result;for(auto&[id,entry]:patches_){(void)id;entry.current=false;}for(const auto&d:extracted.decals)append(result,scene,extracted,d,priority);std::sort(result.receivers.begin(),result.receivers.end());result.receivers.erase(std::unique(result.receivers.begin(),result.receivers.end()),result.receivers.end());return result;}
DecalPreparedScene DecalResources::prepareTransient(const Scene&scene,const ExtractedScene&extracted,const SceneDecal&decal,const Mat4&frame,uint64_t id,ResourcePriority priority){DecalPreparedScene result;append(result,scene,extracted,{id,frame,decal,true},priority);return result;}
std::string DecalResources::status(EntityId id)const{const auto it=patches_.find(id);return it==patches_.end()?std::string{}:it->second.status;}
std::shared_ptr<const DecalPatchData> DecalResources::patch(EntityId id)const{const auto it=patches_.find(id);return it==patches_.end()||!it->second.current?nullptr:it->second.render;}
void DecalResources::collect(){const auto now=std::chrono::steady_clock::now();const auto grace=resources_.config().idleGrace;std::erase_if(sources_,[&](const auto&entry){return now-entry.second.used>grace;});std::erase_if(patches_,[&](const auto&entry){return now-entry.second.used>grace;});}
void DecalResources::clear(){patches_.clear();sources_.clear();}
}
