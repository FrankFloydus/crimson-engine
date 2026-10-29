#include "DecalResources.h"
#include "decals/DecalResolve.h"
#include "decals/DecalCache.h"
#include "assets/AssetLibrary.h"
#include "assets/MaterialGraph.h"
#include "resources/StreamingModel.h"
#include <crimson/renderer/core/Profiler.hpp>
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
std::vector<bool> eligibleSlots(AssetLibrary&assets,const ExtractedModel&m,const ModelAsset&model,std::string*diagnostic=nullptr){
    const auto materials=assets.resolvedModelMaterials(model,m.materialOverrides,m.blendSettings);std::vector<bool>result;result.reserve(materials.size());
    for(const auto&material:materials){std::string reason;const bool eligible=decals::receiverMaterialEligible(material,&reason);result.push_back(eligible);if(diagnostic&&!eligible&&diagnostic->empty())*diagnostic=std::move(reason);}return result;
}
bool eligibleDraw(const decals::ModelSurface&s,uint32_t draw,uint32_t primitive,const std::vector<bool>&slots){
    const auto it=std::find_if(s.draws.begin(),s.draws.end(),[&](const auto&d){return d.surface.id.draw==draw&&d.surface.id.primitive==primitive;});return it!=s.draws.end()&&it->material<slots.size()&&slots[it->material];
}
struct Candidate {std::shared_ptr<const decals::ModelSurface> source;Mat4 world;EntityId entity;uint64_t revision;std::vector<bool>slots;uint64_t sourceInstanceId{};};
std::shared_ptr<const DecalPatchData> renderPatch(const decals::DecalPatch&cpu,uint64_t signature,const std::map<std::pair<EntityId,uint64_t>,uint64_t>& revisions){
    auto result=std::make_shared<DecalPatchData>();result->fingerprint=signature;
    for(const auto&chunk:cpu.chunks){const uint32_t firstVertex=static_cast<uint32_t>(result->vertices.size()),firstIndex=static_cast<uint32_t>(result->indices.size());
        for(const auto&vertex:chunk.vertices){result->vertices.push_back({vertex.position,vertex.baseNormal,vertex.uv});math::include(result->bounds,vertex.position);}
        for(uint32_t i:chunk.indices)result->indices.push_back(firstVertex+i);
        result->receivers.push_back({chunk.receiver.entity,chunk.receiver.draw,chunk.receiver.primitive,firstIndex,static_cast<uint32_t>(chunk.indices.size()),revisions.at({chunk.receiver.entity,chunk.receiver.sourceInstanceId}),chunk.receiver.sourceInstanceId});
    }return result;
}
}
DecalResources::DecalResources(AssetLibrary&assets,ResourceManager&resources):assets_(assets),resources_(resources),meshPaint_(assets,resources){}
void DecalResources::appendExternalModels(ExtractedScene& scene)const {
    if(externalMeshes_){scene.models.reserve(scene.models.size()+externalMeshes_->meshes.size());for(const auto& source:externalMeshes_->meshes)scene.models.push_back(source.occurrence);}
}
std::vector<bool> DecalResources::eligibleMaterials(const ExtractedModel& occurrence,const ModelAsset& model,std::string* diagnostic) {
    const auto* external=externalSource(occurrence);
    if(!external||!external->materials)return eligibleSlots(assets_,occurrence,model,diagnostic);
    std::vector<bool> result;
    for(const auto& material:*external->materials){std::string reason;const bool eligible=decals::receiverMaterialEligible(material,&reason);result.push_back(eligible);if(diagnostic&&!eligible&&diagnostic->empty())*diagnostic=std::move(reason);}
    return result;
}
DecalSourceResult DecalResources::modelSurface(const ExtractedModel&m,ResourcePriority priority){
    DecalSourceResult result;const auto* external=externalSource(m);
    if(!external&&assets_.references().resolve(m.assetId).empty()){result.failed=true;result.diagnostic="Missing surface model.";return result;}
    auto model=external?external->model:assets_.findModel(m.assetId,m.primitive);
    result.diagnostic=external?std::string{}:assets_.modelDiagnostic(m.assetId);
    const bool loading=!external&&assets_.modelLoading(m.assetId);
    if(!model||loading||!result.diagnostic.empty()){result.failed=!loading&&(!model||!result.diagnostic.empty());if(result.diagnostic.empty())result.diagnostic=loading?"Preparing surface geometry.":"Missing surface model.";return result;}
    result.revision=std::max<uint64_t>(1,external?external->revision:assets_.modelRevision(m.assetId));
    result.fingerprint=external?external->fingerprint:model->stream?model->stream->key:assets_.modelFingerprint(m.assetId);Hash identity;identity.add(model->id);identity.add(result.fingerprint);
    if(external)identity.add(external->entityFromLocal);
    bool painted=!external&&m.meshPaint.has_value();
    try{if(!external&&!painted)for(const auto& material:assets_.resolvedModelMaterials(*model,m.materialOverrides,m.blendSettings))if(material.graph&&materialOutputNode(*material.graph).output.shading==MaterialShading::BlendPbr){painted=true;break;}}
    catch(const std::exception& error){result.failed=true;result.diagnostic=error.what();return result;}
    std::vector<std::shared_ptr<const decals::SurfaceBoundaryLineage>> boundaries;
    if(painted){const auto prepared=meshPaint_.prepare(m,priority);if(!prepared.ready){result.failed=prepared.failed;result.diagnostic=prepared.diagnostic;return result;}model=prepared.decalAsset;result.revision=prepared.revision;result.fingerprint=prepared.decalSignature;identity=Hash{};identity.add(model->id);identity.add(result.fingerprint);boundaries.resize(model->draws.size());for(const auto& draw:prepared.draws)if(draw.draw<boundaries.size())boundaries[draw.draw]=draw.decalBoundary;}
    result.model=model;
    // The stream key covers original primitive attributes. Draw occurrence
    // transforms are separate metadata and must also invalidate the source.
    for(const auto&draw:model->draws){identity.add(draw.primitive);identity.add(draw.transform);}result.fingerprint=identity.value;
    const auto mode=static_cast<decals::SourceNormals>(m.meshShading);
    const std::string key=model->id+":"+std::to_string(result.fingerprint)+":"+std::to_string(static_cast<unsigned>(mode));result.sourceKey=key;
    auto&entry=sources_[key];entry.used=std::chrono::steady_clock::now();entry.fingerprint=result.fingerprint;
    const bool retry=entry.resource&&entry.resource.state()==ResourceState::Failed&&entry.revision!=result.revision;
    if(!entry.resource||retry){ResourceRequestOptions options;options.priority=priority;options.retainLoader=false;options.cancelWhenUnused=true;if(!external)options.assetDependencies={m.assetId};options.forceReload=retry;
        if(!external&&model->stream)for(const auto&primitive:model->stream->primitives)if(!primitive.levels.empty())options.dependencies.push_back(primitive.levels[0].file);
        entry.revision=result.revision;
        const auto externalOriginal=external?external->originalLod:decals::OriginalPrimitiveLoader{};
        const auto entityFromLocal=external?external->entityFromLocal:Mat4{};
        entry.resource=resources_.requestDerived<decals::ModelSurface>("decal-surface:"+key,[model,mode,boundaries=std::move(boundaries),fingerprint=result.fingerprint,externalOriginal,entityFromLocal](std::stop_token stop){
            const auto original=[model,externalOriginal](uint32_t primitive,std::stop_token token)->std::shared_ptr<const ModelPrimitive>{
                if(externalOriginal)return externalOriginal(primitive,token);
                if(!model->stream||primitive>=model->stream->primitives.size()||model->stream->primitives[primitive].levels.empty())return {};
                auto lod=streaming::loadModelLod(model->stream->primitives[primitive].levels[0],primitive,0,false,token);return {lod,&lod->geometry};
            };
            ModelAsset metadata;const ModelAsset* input=model.get();
            if(externalOriginal){metadata.id=model->id;metadata.draws=model->draws;metadata.primitives.resize(model->primitives.size());for(std::size_t i=0;i<metadata.primitives.size();++i)metadata.primitives[i].material=model->primitives[i].material;for(auto& draw:metadata.draws)draw.transform=entityFromLocal*draw.transform;input=&metadata;}
            auto surface=std::make_shared<decals::ModelSurface>(decals::buildModelSurface(*input,fingerprint,mode,original,stop));for(auto& draw:surface->draws)if(draw.surface.id.draw<boundaries.size())draw.surface.boundary=boundaries[draw.surface.id.draw];return ResourceLoadResult<decals::ModelSurface>{surface,surface->bytes(),fingerprint,{}};
        },std::move(options));
    }
    if(const auto snapshot=entry.resource.snapshot()){result.surface=snapshot.shared();result.current=entry.resource.state()==ResourceState::Ready;}
    result.diagnostic=entry.resource.diagnostic();result.failed=entry.resource.state()==ResourceState::Failed;
    if(result.failed&&!external&&model->stream&&!entry.repairRequested){entry.repairRequested=true;assets_.rebuildModelCache(m.assetId);result.failed=false;result.diagnostic="Preparing surface geometry.";}
    if(result.current&&result.surface->draws.empty()){result.current=false;result.failed=true;result.diagnostic=result.surface->diagnostic;}
    if(!result.surface&&result.diagnostic.empty())result.diagnostic="Preparing surface geometry.";return result;
}
DecalPlacementResult DecalResources::resolvePlacement(const Scene&scene,uint64_t entity,uint32_t draw,uint32_t primitive,uint64_t revision,Vec3 point,uint64_t sourceInstanceId){
    DecalPlacementResult result;auto extraction=extractScene(scene);appendExternalModels(extraction);const auto found=std::find_if(extraction.models.begin(),extraction.models.end(),[&](const auto&m){return m.id==entity&&m.sourceInstanceId==sourceInstanceId;});
    const auto fail=[&](const char*message){result.failed=true;result.diagnostic=message;return result;};
    if(found==extraction.models.end())return fail("This visual has no supported mesh surface.");
    if(!found->visible||!found->receiveDecals)return fail("Receive Decals is disabled for this surface.");
    if(!found->animation.asset.empty())return fail("Animated decal receivers are unsupported.");
    const auto source=modelSurface(*found,ResourcePriority::Placement);if(revision&&source.revision&&revision!=source.revision)return fail("Surface changed; place the decal again.");
    if(!source.current){result.diagnostic=source.diagnostic;result.failed=source.failed;return result;}
    const auto model=source.model;if(!model)return fail("Missing surface.");
    try{std::string reason;if(!eligibleDraw(*source.surface,draw,primitive,eligibleMaterials(*found,*model,&reason))){result.failed=true;result.diagnostic=reason.empty()?"Unsupported receiver material.":std::move(reason);return result;}}catch(const std::exception&e){result.failed=true;result.diagnostic=e.what();return result;}
    const auto hit=decals::resolveModelSurfaceHit(*source.surface,entity,found->world,draw,primitive,point);
    if(!hit.hit)return fail(hit.status==decals::SurfaceHitStatus::Ambiguous?"Ambiguous surface.":"Visible surface no longer matches its source.");
    const auto normalMatrix=math::normalMatrix(found->world);if(!normalMatrix)return fail("Invalid receiver transform.");
    result.seed.receiver=entity;result.seed.modelId=found->assetId;result.seed.fingerprint=source.fingerprint;
    result.seed.sourceInstanceId=found->sourceInstanceId;
    if(const auto* external=externalSource(*found))result.seed.nativeSource=external->nativeSource;
    result.seed.draw=draw;result.seed.primitive=primitive;result.seed.triangle=hit.hit->seed.triangle;result.seed.barycentric=hit.hit->seed.barycentrics;result.seed.localPoint=hit.hit->localPoint;result.seed.localNormal=hit.hit->localNormal;
    result.frame=decals::surfacePlacementFrame(math::transformPoint(found->world,hit.hit->localPoint),math::transformDirection(*normalMatrix,hit.hit->localNormal));result.ready=true;return result;
}
uint64_t DecalResources::spatialSceneSignature(const ExtractedScene& extracted){
    CRIMSON_PROFILE_ZONE("Decal scene metadata signature");
    // Observe immutable inputs/revisions once per scene preparation. This does
    // not resolve material graphs, query receiver triangles or project seeds.
    Hash key;key.add(assets_.catalogRevision());key.add(assets_.references().revision());if(externalMeshes_)key.add(externalMeshes_->revision);
    for(const auto& model:extracted.models){
        key.add(model.id);key.add(model.visible);key.add(model.receiveDecals);
        if(!model.visible||!model.receiveDecals)continue;
        key.add(model.assetId);key.add(model.world);key.add(model.meshShading);key.add(model.animation.asset.id);
        key.add(model.primitive.has_value());
        if(model.primitive){const auto& p=*model.primitive;key.add(p.size);key.add(p.radius);key.add(p.height);key.add(p.sides);key.add(p.rings);}
        key.add(model.meshPaint.has_value());
        if(model.meshPaint){key.add(model.meshPaint->subdivisionLevel);key.add(model.meshPaint->recipeVersion);}
        const auto* external=externalSource(model);if(external){key.add(model.sourceInstanceId);key.add(external->fingerprint);key.add(external->entityFromLocal);key.add(external->revision);continue;}
        const auto asset=assets_.findModel(model.assetId,model.primitive);
        key.add(assets_.modelRevision(model.assetId));key.add(assets_.modelLoading(model.assetId));key.add(assets_.modelDiagnostic(model.assetId));
        key.add(bool(asset));
        // Imported metadata is retained by the library; primitives are weakly
        // cached and their geometry identity comes from explicit settings.
        if(asset&&asset->format!="BUILTIN")key.add(reinterpret_cast<uintptr_t>(asset.get()));
        const auto slots=std::max(model.materialOverrides.size(),asset?asset->materials.size():size_t{});
        for(size_t slot=0;slot<slots;++slot){
            const auto id=slot<model.materialOverrides.size()&&!model.materialOverrides[slot].empty()?model.materialOverrides[slot].id:
                asset&&slot<asset->materialIds.size()?asset->materialIds[slot]:asset?importedMaterialId(asset->id,slot):std::string{};
            key.add(id);key.add(assets_.materialRevision(id));
        }
    }
    return key.value;
}
bool DecalResources::spatialSourcesCurrent(const SpatialEntry& spatial){
    const auto now=std::chrono::steady_clock::now();
    for(const auto& [key,surface]:spatial.sources){
        const auto found=sources_.find(key);
        if(found==sources_.end()||found->second.resource.state()!=ResourceState::Ready)return false;
        const auto current=found->second.resource.snapshot();
        if(current.revision()!=surface.revision()||current.shared()!=surface.shared())return false;
        found->second.used=now;
    }
    return true;
}
void DecalResources::append(DecalPreparedScene&out,const Scene&scene,const ExtractedScene&extracted,const ExtractedDecal&d,ResourcePriority priority,uint64_t sceneSignature){
    if(!d.visible||d.decal.opacity<=0){if(const auto existing=patches_.find(d.id);existing!=patches_.end())existing->second.current=false;return;}
    auto&entry=patches_[d.id];entry.current=false;entry.used=std::chrono::steady_clock::now();
    // A pending frame submits no packet, while retaining a compatible CPU
    // publication so texture preparation cannot trigger a chart rebuild.
    const auto pending=[&](std::string message,bool failed=false){
        entry.status=std::move(message);out.ready=false;out.refined=false;
        // A mixed scene may have both loading and terminal decal results. Its
        // retry state must explain the terminal result which caused backoff.
        if(out.status.empty()||(failed&&!out.failed))out.status=entry.status;
        out.failed|=failed;
    };
    if(d.decal.projection==SceneDecalProjection::Box) {
        entry.resource={};entry.render.reset();entry.spatial.reset();entry.surfaceFrame.reset();
        if(!math::inverse(d.world)){pending("Invalid decal box transform.",true);return;}
        const auto material=assets_.preparedDecalMaterial(d.decal.asset.id,1024,priority);
        if(!material||!material->ready||!material->current||!material->render){pending(material&&!material->diagnostic.empty()?material->diagnostic:"Preparing decal material.");return;}
        RenderDecal packet;packet.entityId=d.id;packet.stableOrder=d.id;packet.layer=d.decal.priority;
        packet.material=material->render;packet.tint=d.decal.tint;packet.opacity=d.decal.opacity;packet.edgeFeather=d.decal.edgeFeather;
        packet.frame=d.world;packet.projection=DecalProjection::Box;
        out.decals.push_back(std::move(packet));entry.status.clear();entry.current=true;return;
    }
    Hash inputs;inputs.add(sceneSignature);inputs.add(d.world);
    inputs.add(d.decal.seed.receiver);inputs.add(d.decal.seed.sourceInstanceId);inputs.add(d.decal.seed.fingerprint);inputs.add(d.decal.seed.draw);inputs.add(d.decal.seed.primitive);
    inputs.add(d.decal.seed.localPoint);
    const bool reuse=entry.spatial&&entry.spatial->inputs==inputs.value&&spatialSourcesCurrent(*entry.spatial);
    if(!reuse){
    CRIMSON_PROFILE_ZONE("Decal spatial preparation");
    ++stats_.spatialPreparations;
    entry.spatial.reset();
    const auto center=math::transformPoint(d.world,{}),x=math::transformDirection(d.world,{1,0,0}),y=math::transformDirection(d.world,{0,1,0});
    const double radius=.625*std::max(math::length(x+y),math::length(x-y))+.5*std::min(math::length(x),math::length(y));
    if(!std::isfinite(radius)||radius<=0){pending("Invalid footprint.",true);return;}
    const float projectionDistance=static_cast<float>(std::max(.01,.5*std::max(math::length(x),math::length(y))+.025));
    const Vec3 searchExtent{radius+projectionDistance,radius+projectionDistance,radius+projectionDistance};
    const Aabb region{center-searchExtent,center+searchExtent,true};
    std::vector<Candidate>candidates;SpatialEntry spatial;spatial.inputs=inputs.value;Hash bindings;Hash signature;
    signature.add(decals::DecalChartCacheVersion);signature.add(d.id);signature.add(d.world);
    std::optional<decals::ModelSurfaceHit> seed;const Candidate* seedCandidate=nullptr;double closest=INFINITY;std::string unsupported,preparing;
    for(const auto&m:extracted.models){
        if(!m.visible||!m.receiveDecals)continue;
        // An unrelated missing/unloaded visual must not suppress a valid local
        // receiver. Ready models enter the geometric signature when published.
        const auto* external=externalSource(m);const auto model=external?external->model:assets_.findModel(m.assetId,m.primitive);if(!model){if(assets_.modelLoading(m.assetId)){preparing=assets_.modelDiagnostic(m.assetId);if(preparing.empty())preparing="Preparing receiver models.";}else{unsupported=assets_.modelDiagnostic(m.assetId);if(unsupported.empty())unsupported="Missing receiver model.";}continue;}
        // Pin immutable metadata/primitive variants with this bounded spatial
        // entry. A weak primitive cache must not rebuild geometry on retries.
        spatial.models.push_back(model);
        if(!overlaps(math::transformBounds(model->bounds,external?m.world*external->entityFromLocal:m.world),region))continue;
        if(!m.animation.asset.empty()){unsupported="Animated decal receivers are unsupported.";continue;}
        std::vector<bool>slots;try{slots=eligibleMaterials(m,*model,&unsupported);}catch(const std::exception&e){unsupported=e.what();continue;}
        if(std::none_of(slots.begin(),slots.end(),[](bool value){return value;}))continue;
        const auto source=modelSurface(m,priority);if(!source.current){if(source.failed){unsupported=source.diagnostic;continue;}pending(source.diagnostic);return;}
        if(source.model)try{slots=eligibleMaterials(m,*source.model,&unsupported);}catch(const std::exception& e){unsupported=e.what();continue;}
        signature.add(m.id);signature.add(m.sourceInstanceId);signature.add(m.world);signature.add(source.fingerprint);signature.add(m.meshShading);for(bool eligible:slots)signature.add(eligible);
        spatial.revisions.emplace(std::pair{m.id,m.sourceInstanceId},source.revision);bindings.add(m.id);bindings.add(m.sourceInstanceId);bindings.add(source.revision);
        spatial.sources.emplace_back(source.sourceKey,sources_.at(source.sourceKey).resource.snapshot());
        candidates.push_back({source.surface,m.world,m.id,source.revision,std::move(slots),m.sourceInstanceId});spatial.receivers.push_back(m.id);out.receivers.push_back(m.id);
    }
    // Unmoved native seeds retain their exact member through hierarchy copies.
    // Moving the decal still projects its authored footprint onto other surfaces.
    const auto receiverWorld=scene.worldTransform(d.decal.seed.receiver);
    const bool retainedNative=d.decal.seed.nativeSource&&receiverWorld&&
        math::length(math::transformPoint(*receiverWorld,d.decal.seed.localPoint)-center)<1e-5;
    for(const auto&candidate:candidates){
        if(retainedNative&&(candidate.entity!=d.decal.seed.receiver||candidate.sourceInstanceId!=d.decal.seed.sourceInstanceId))continue;
        decals::SurfaceHitResult hit;
        if(candidate.entity==d.decal.seed.receiver&&candidate.sourceInstanceId==d.decal.seed.sourceInstanceId&&candidate.source->fingerprint==d.decal.seed.fingerprint&&
           math::length(math::transformPoint(candidate.world,d.decal.seed.localPoint)-center)<1e-5) {
            ++stats_.seedQueries;hit=decals::resolveModelSurfaceHit(*candidate.source,candidate.entity,candidate.world,d.decal.seed.draw,d.decal.seed.primitive,center);
            if(hit.hit&&!eligibleDraw(*candidate.source,hit.hit->seed.receiver.draw,hit.hit->seed.receiver.primitive,candidate.slots))hit={};
        }
        if(!hit.hit){++stats_.seedQueries;hit=decals::projectModelSurfaceHit(*candidate.source,candidate.entity,candidate.world,center,projectionDistance,
            [&](uint32_t material){return material<candidate.slots.size()&&candidate.slots[material];});}
        if(!hit.hit)continue;
        const double distance=math::length(math::transformPoint(candidate.world,hit.hit->localPoint)-center);
        if(!seed||distance<closest-1e-5||(std::abs(distance-closest)<=1e-5&&(candidate.entity==d.decal.seed.receiver&&candidate.sourceInstanceId==d.decal.seed.sourceInstanceId))) {
            seed=hit.hit;seed->seed.receiver.sourceInstanceId=candidate.sourceInstanceId;seedCandidate=&candidate;closest=distance;
        }
    }
    if(!seed||!seedCandidate){spatial.diagnostic=!unsupported.empty()?unsupported:!preparing.empty()?preparing:"No receiving surface within the decal footprint.";spatial.failed=preparing.empty();entry.spatial=std::move(spatial);pending(entry.spatial->diagnostic,entry.spatial->failed);return;}
    const auto normalMatrix=math::normalMatrix(seedCandidate->world);
    const auto effectiveFrame=normalMatrix?decals::conformDecalFrame(d.world,math::transformPoint(seedCandidate->world,seed->localPoint),math::transformDirection(*normalMatrix,seed->localNormal)):std::nullopt;
    if(!effectiveFrame){pending("Invalid footprint.",true);return;}
    spatial.frame=*effectiveFrame;
    signature.add(*effectiveFrame);signature.add(seed->seed.receiver.entity);signature.add(seed->seed.receiver.sourceInstanceId);signature.add(seed->seed.receiver.draw);signature.add(seed->seed.receiver.primitive);signature.add(seed->seed.triangle);signature.add(seed->seed.barycentrics);
    const auto key=signature.value;
    spatial.signature=key;spatial.bindings=bindings.value;
    if(!entry.resource||entry.signature!=key){entry.signature=key;entry.publishedRevision=0;entry.render.reset();ResourceRequestOptions options;options.priority=priority;options.retainLoader=false;options.cancelWhenUnused=true;
        for(const auto&m:extracted.models)if(!externalSource(m)&&std::any_of(candidates.begin(),candidates.end(),[&](const auto&c){return c.entity==m.id;}))options.assetDependencies.push_back(m.assetId);
        decals::DecalBuildRequest request;request.seed=seed->seed;request.frame=*effectiveFrame;
        const auto cacheFile=decals::decalCachePath(assets_.projectRoot(),key);
        entry.resource=resources_.requestDerived<decals::DecalPatch>("decal-patch:"+std::to_string(key),[seedSource=seedCandidate->source,seedWorld=seedCandidate->world,candidates=std::move(candidates),request,key,cacheFile](std::stop_token stop) mutable{
            const auto sourceQuery=decals::decalSourceQuery(*seedSource,seedWorld,request);
            decals::DecalSurfaceSnapshot snapshot;size_t triangles=0;
            for(const auto&candidate:candidates)for(const auto&draw:candidate.source->draws){if(stop.stop_requested())throw std::runtime_error("Decal preparation cancelled.");if(draw.material>=candidate.slots.size()||!candidate.slots[draw.material])continue;
                const auto indices=decals::querySurfaceTriangles(draw,candidate.world,sourceQuery,stop);if(indices.empty())continue;
                if((triangles+=indices.size())>request.limits.maximumInputTriangles)throw std::runtime_error("Decal receiver neighborhood exceeds its triangle budget.");
                decals::SurfaceReceiver receiver;receiver.id=draw.surface.id;receiver.id.entity=candidate.entity;receiver.id.sourceInstanceId=candidate.sourceInstanceId;receiver.generation=candidate.revision;receiver.localToWorld=candidate.world*draw.surface.localToWorld;receiver.boundary=draw.surface.boundary;receiver.triangles.reserve(indices.size());for(uint32_t i:indices)receiver.triangles.push_back(draw.surface.triangles[i]);snapshot.receivers.push_back(std::move(receiver));
            }
            // Keep the final upload bounded while allowing genuinely dense
            // receiver fields to survive a large image. Flat paint-only detail
            // was already removed; retained curved/normal fields are not lossy.
            request.limits.maximumTriangles=static_cast<uint32_t>(std::clamp<size_t>(triangles*2,16384,65536));
            auto cached=decals::readDecalCache(cacheFile,key,snapshot,request,stop);
            auto patch=std::make_shared<decals::DecalPatch>(cached?std::move(*cached):decals::buildDecalPatch(snapshot,request,stop));
            if(!cached&&patch->usable())decals::writeDecalCache(cacheFile,key,*patch,request.limits,stop);
            size_t bytes=sizeof(*patch);for(const auto&chunk:patch->chunks)bytes+=sizeof(chunk)+chunk.vertices.size()*sizeof(decals::DecalPatchVertex)+chunk.indices.size()*sizeof(uint32_t);return ResourceLoadResult<decals::DecalPatch>{patch,bytes,key,{}};
        },std::move(options));
    }
    entry.spatial=std::move(spatial);
    }else ++stats_.spatialCacheHits;
    const auto& spatial=*entry.spatial;const auto key=spatial.signature;
    if(reuse)out.receivers.insert(out.receivers.end(),spatial.receivers.begin(),spatial.receivers.end());
    if(!spatial.diagnostic.empty()){pending(spatial.diagnostic,spatial.failed);return;}
    entry.surfaceFrame=spatial.frame;
    const auto patch=entry.resource.snapshot();if(!patch){const bool failed=entry.resource.state()==ResourceState::Failed;pending(failed?entry.resource.diagnostic():"Preparing decal surface.",failed);return;}
    if(!patch->usable()){const auto state=entry.resource.state();const bool failed=state==ResourceState::Ready||state==ResourceState::Failed;pending(failed?patch->message:"Preparing decal surface.",failed);return;}
    if(entry.publishedRevision!=patch.revision()||entry.publishedBindings!=spatial.bindings){Hash published;published.add(key);published.add(spatial.bindings);entry.render=renderPatch(*patch,published.value,spatial.revisions);entry.publishedRevision=patch.revision();entry.publishedBindings=spatial.bindings;}
    entry.current=true;
    const auto material=assets_.preparedDecalMaterial(d.decal.asset.id,1024,priority);if(!material||!material->ready||!material->current||!material->render){pending(material&&!material->diagnostic.empty()?material->diagnostic:"Preparing decal material.");return;}
    entry.status=patch->message;out.decals.push_back({d.id,d.id,d.decal.priority,entry.render,material->render,d.decal.tint,d.decal.opacity,d.decal.edgeFeather});
    out.decals.back().frame=spatial.frame;
    if(patch->status==decals::DecalPatchStatus::Incomplete&&!out.status.size())out.status=patch->message;
}
DecalPreparedScene DecalResources::prepare(const Scene&scene,const ExtractedScene&source,ResourcePriority priority){auto extracted=source;appendExternalModels(extracted);DecalPreparedScene result;for(auto&[id,entry]:patches_){(void)id;entry.current=false;}const auto surface=std::any_of(extracted.decals.begin(),extracted.decals.end(),[](const auto& d){return d.visible&&d.decal.projection==SceneDecalProjection::Surface;});const auto signature=surface?spatialSceneSignature(extracted):0;for(const auto&d:extracted.decals)append(result,scene,extracted,d,priority,signature);std::sort(result.receivers.begin(),result.receivers.end());result.receivers.erase(std::unique(result.receivers.begin(),result.receivers.end()),result.receivers.end());return result;}
DecalPreparedScene DecalResources::prepareTransient(const Scene&scene,const ExtractedScene&source,const SceneDecal&decal,const Mat4&frame,uint64_t id,ResourcePriority priority){auto extracted=source;if(decal.projection==SceneDecalProjection::Surface)appendExternalModels(extracted);DecalPreparedScene result;append(result,scene,extracted,{id,frame,decal,true},priority,decal.projection==SceneDecalProjection::Surface?spatialSceneSignature(extracted):0);return result;}
std::string DecalResources::status(EntityId id)const{const auto it=patches_.find(id);return it==patches_.end()?std::string{}:it->second.status;}
std::shared_ptr<const DecalPatchData> DecalResources::patch(EntityId id)const{const auto it=patches_.find(id);return it==patches_.end()||!it->second.current?nullptr:it->second.render;}
std::optional<Mat4> DecalResources::surfaceFrame(EntityId id)const{const auto it=patches_.find(id);return it==patches_.end()?std::nullopt:it->second.surfaceFrame;}
void DecalResources::collect(){
    {CRIMSON_PROFILE_ZONE("Decal paint resource cleanup");meshPaint_.collect();}
    const auto now=std::chrono::steady_clock::now();const auto grace=resources_.config().idleGrace;
    {CRIMSON_PROFILE_ZONE("Decal source resource cleanup");std::erase_if(sources_,[&](const auto&entry){return now-entry.second.used>grace;});}
    {CRIMSON_PROFILE_ZONE("Decal patch resource cleanup");std::erase_if(patches_,[&](const auto&entry){return now-entry.second.used>grace;});}
}
void DecalResources::clear(){patches_.clear();sources_.clear();meshPaint_.clear();externalMeshes_.reset();}
}
