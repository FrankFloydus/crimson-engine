#include "MeshPaintResources.h"
#include "VertexPaintSubdivisionOverlay.h"
#include "assets/AssetLibrary.h"
#include "assets/MaterialGraph.h"
#include "assets/MaterialAsset.h"
#include "resources/StreamingModel.h"
#include "scene/SceneExtraction.h"
#include "decals/DecalPaintSurface.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <algorithm>
#include <atomic>
#include <bit>
#include <sstream>
#include <unordered_set>

namespace crimson {
namespace {
std::atomic_uint64_t nextSubdivisionOverlayOwner{1};
constexpr std::size_t MaximumSubdivisionOverlayBytes=128ull*1024*1024,MaximumSubdivisionOverlayEntries=256;
struct ModelData final:ModelAsset {
    struct Draw {std::uint32_t sourcePrimitive{};std::shared_ptr<const MeshPaintGeometry> geometry;std::shared_ptr<const MeshPaintPayload> base;std::shared_ptr<const decals::SurfaceBoundaryLineage> boundary,decalBoundary;};
    std::vector<Draw> paintedDraws;
    std::optional<SceneMeshPaint> resampled;
    std::shared_ptr<const ModelAsset> source;
    std::shared_ptr<const ModelAsset> decalAsset;
    std::uint64_t revision{},signature{};
};
struct SurfaceData final:ModelAsset {
    std::shared_ptr<const ModelAsset> source;
    std::vector<std::shared_ptr<const ShadowRayMesh>> meshes;
};
void cancelled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Mesh paint preparation cancelled.");}
void mix(std::uint64_t& hash,std::uint64_t value){hash^=value+0x9e3779b97f4a7c15ull+(hash<<6)+(hash>>2);}
bool rigidSource(const ModelAsset& model){return model.skins.empty()&&model.animations.empty()&&std::none_of(model.primitives.begin(),model.primitives.end(),[](const auto& primitive){return primitive.hasSkinning||!primitive.morphTargets.empty();});}
std::uint64_t paintKey(const std::optional<SceneMeshPaint>& paint){std::uint64_t hash=1469598103934665603ull;if(paint)for(const auto& draw:paint->draws){mix(hash,draw.draw);mix(hash,draw.primitive);mix(hash,draw.level);mix(hash,draw.sourceSignature);mix(hash,draw.weights?draw.weights->fingerprint:0);}return hash;}
std::size_t bytes(const ModelData& model){std::size_t total=sizeof(ModelData);std::unordered_set<const MeshPaintGeometry*> geometries;
    for(const auto& p:model.primitives)total+=sizeof(ModelPrimitive)+p.vertices.size()*sizeof(ModelVertex)+p.indices.size()*sizeof(std::uint32_t);
    for(const auto& d:model.paintedDraws){if(d.boundary)total+=sizeof(decals::SurfaceBoundaryLineage)+d.boundary->triangles.capacity()*sizeof(decals::SurfaceBoundaryTriangle);if(d.decalBoundary&&d.decalBoundary!=d.boundary)total+=sizeof(decals::SurfaceBoundaryLineage)+d.decalBoundary->triangles.capacity()*sizeof(decals::SurfaceBoundaryTriangle);if(geometries.insert(d.geometry.get()).second){total+=sizeof(MeshPaintGeometry)+(d.geometry->vertices.size()+d.geometry->shadingVertices.size())*sizeof(ModelVertex)+d.geometry->indices.size()*sizeof(std::uint32_t)+d.geometry->cells.size()*sizeof(MeshPaintCell)+d.geometry->boundaryEdges.size()*sizeof(std::array<std::uint32_t,2>);
        for(const auto& list:d.geometry->vertexTriangles)total+=sizeof(list)+list.size()*sizeof(std::uint32_t);for(const auto& list:d.geometry->seamVertices)total+=sizeof(list)+list.size()*sizeof(std::uint32_t);}}
    if(model.decalAsset){total+=sizeof(ModelAsset);for(const auto& p:model.decalAsset->primitives)total+=sizeof(ModelPrimitive)+p.vertices.size()*sizeof(ModelVertex)+p.indices.size()*sizeof(std::uint32_t);}
    if(model.resampled)for(const auto& d:model.resampled->draws)if(d.weights)for(const auto& page:d.weights->pages)if(page)total+=sizeof(MeshPaintPage);return total;
}
std::shared_ptr<ModelData> build(const std::shared_ptr<const ModelAsset>& source,EntityId entity,std::uint32_t level,std::uint64_t revision,std::optional<SceneMeshPaint> paint,bool resample,std::stop_token stop){
    CRIMSON_PROFILE_ZONE("Vertex paint subdivision worker");
    cancelled(stop);if(!rigidSource(*source))throw std::runtime_error("Vertex painting supports rigid static meshes.");
    auto result=std::make_shared<ModelData>();static_cast<ModelAsset&>(*result)=*source;result->source=source;result->revision=revision;result->stream.reset();result->primitives.clear();result->paintedDraws.clear();result->signature=revision;mix(result->signature,level);
    std::vector<std::shared_ptr<const ModelPrimitive>> originals(source->primitives.size());std::vector<std::shared_ptr<const MeshPaintGeometry>> geometries(source->primitives.size());
    std::shared_ptr<ModelAsset> decalAsset;
    std::uint64_t vertexTotal{},triangleTotal{};
    for(std::uint32_t drawIndex=0;drawIndex<source->draws.size();++drawIndex){cancelled(stop);const auto& draw=source->draws[drawIndex];if(draw.primitive>=source->primitives.size())throw std::runtime_error("Paint draw has an invalid primitive.");auto& original=originals[draw.primitive];
        if(!original){const auto& primitive=source->primitives[draw.primitive];if(!primitive.vertices.empty())original={source,&primitive};else {if(!source->stream||draw.primitive>=source->stream->primitives.size()||source->stream->primitives[draw.primitive].levels.empty())throw std::runtime_error("Original mesh geometry is unavailable.");
            try{const auto lod=streaming::loadModelLod(source->stream->primitives[draw.primitive].levels[0],draw.primitive,0,false,stop);original={lod,&lod->geometry};}
            catch(const std::exception& error){cancelled(stop);throw std::runtime_error(std::string("Original paint geometry cache: ")+error.what());}}}
        auto& geometry=geometries[draw.primitive];if(!geometry){CRIMSON_PROFILE_ZONE("Vertex paint corner quad refinement");geometry=buildMeshPaintGeometry(*original,level,stop);}
        vertexTotal+=geometry->vertices.size();triangleTotal+=geometry->indices.size()/3;if(vertexTotal>MaximumMeshPaintVertices||triangleTotal>MaximumMeshPaintTriangles)throw std::runtime_error("Subdivision exceeds this entity's 262144 vertex / 524288 triangle budget.");
        const auto primitive=static_cast<std::uint32_t>(result->primitives.size());result->primitives.push_back(meshPaintRenderPrimitive(*original,*geometry));result->draws[drawIndex].primitive=primitive;const auto boundary=decals::meshPaintBoundaryLineage(*original,*geometry,stop);result->paintedDraws.push_back({draw.primitive,geometry,makeMeshPaintPayload(static_cast<std::uint32_t>(geometry->vertices.size())),boundary,boundary});mix(result->signature,geometry->signature);
        {CRIMSON_PROFILE_ZONE("Decal paint-only refinement removal");
        std::shared_ptr<const decals::SurfaceBoundaryLineage> retainedBoundary;
        if(auto simplified=decals::simplifyDecalPaintPrimitive(*original,*geometry,stop,boundary.get(),&retainedBoundary)){
            result->paintedDraws.back().decalBoundary=std::move(retainedBoundary);
            if(!decalAsset){decalAsset=std::make_shared<ModelAsset>();*decalAsset=*result;decalAsset->primitives.back()=std::move(*simplified);}
            else decalAsset->primitives.push_back(std::move(*simplified));
        }else if(decalAsset)decalAsset->primitives.push_back(result->primitives.back());}
    }
    result->id="mesh-paint:"+source->id+":"+std::to_string(entity)+":"+std::to_string(result->signature);
    if(result->materialIds.size()<result->materials.size())result->materialIds.resize(result->materials.size());
    for(std::size_t i=0;i<result->materials.size();++i)if(result->materialIds[i].empty())result->materialIds[i]=importedMaterialId(source->id,i);
    // Keep model-node metadata coherent with expanded occurrence primitives.
    for(std::size_t mesh=0;mesh<result->meshes.size();++mesh){auto& p=result->meshes[mesh].primitives;p.clear();for(std::uint32_t i=0;i<source->draws.size();++i){const auto node=source->draws[i].node;if(node>=0&&static_cast<std::size_t>(node)<source->nodes.size()&&source->nodes[node].mesh==static_cast<std::int32_t>(mesh))p.push_back(result->draws[i].primitive);}}
    if(decalAsset){decalAsset->id=result->id;decalAsset->draws=result->draws;decalAsset->meshes=result->meshes;decalAsset->materialIds=result->materialIds;result->decalAsset=std::move(decalAsset);}
    if(resample){result->resampled=paint.value_or(SceneMeshPaint{});auto& replacement=*result->resampled;replacement.subdivisionLevel=level;replacement.recipeVersion=MeshPaintRecipeVersion;
        for(auto& stored:replacement.draws){cancelled(stop);if(stored.draw>=result->paintedDraws.size()||result->paintedDraws[stored.draw].sourcePrimitive!=stored.primitive)throw std::runtime_error("Reset incompatible paint before changing subdivision.");const auto& current=result->paintedDraws[stored.draw];
            if(stored.sourceSignature!=current.geometry->sourceSignature||!stored.weights)throw std::runtime_error("Reset incompatible paint before changing subdivision.");
            if(stored.level!=level){CRIMSON_PROFILE_ZONE("Vertex paint resample worker");const auto before=buildMeshPaintGeometry(*originals[stored.primitive],stored.level,stop);stored.weights=resampleMeshPaintPayload(*before,*stored.weights,*current.geometry,stop);stored.level=level;}
            else if(stored.weights->vertexCount!=current.geometry->vertices.size())throw std::runtime_error("Stored paint vertex count no longer matches its source.");}
    }
    return result;
}
}
MeshPaintResources::MeshPaintResources(AssetLibrary& assets,ResourceManager& resources):assets_(assets),resources_(resources),subdivisionOverlayOwner_(nextSubdivisionOverlayOwner.fetch_add(1)){}
ResourceHandle<ModelAsset> MeshPaintResources::request(const EntityRecord& record,std::uint32_t level,ResourcePriority priority,bool resample,std::string& diagnostic){
    CRIMSON_PROFILE_ZONE("Vertex paint subdivision request");
    if(!record.model){diagnostic="Select a rigid mesh.";return {};}
    if(level>MaximumMeshPaintLevel){diagnostic="Subdivision Level must be from 0 to 5.";return {};}
    const auto source=assets_.findModel(record.model->id,record.primitive);if(!source){diagnostic=assets_.modelDiagnostic(record.model->id);if(diagnostic.empty())diagnostic="Preparing original mesh geometry.";return {};}
    if(!record.modelAnimation.asset.empty()||!record.modelRagdoll.asset.empty()||!rigidSource(*source)){diagnostic="Vertex painting supports rigid static meshes.";return {};}
    const auto revision=std::max<std::uint64_t>(1,assets_.modelRevision(record.model->id));std::string key=source->id+":"+std::to_string(revision)+":"+std::to_string(record.id)+":"+std::to_string(level);
    if(resample)key+=":"+std::to_string(paintKey(record.meshPaint));
    auto& entry=(resample?resampling_:geometry_)[key];entry.used=std::chrono::steady_clock::now();entry.entity=record.id;
    if(!entry.resource){ResourceRequestOptions options;options.priority=priority;options.retainLoader=false;options.cancelWhenUnused=true;options.assetDependencies={record.model->id};if(source->stream)for(const auto& primitive:source->stream->primitives)if(!primitive.levels.empty())options.dependencies.push_back(primitive.levels[0].file);
        entry.resource=resources_.requestDerived<ModelAsset>((resample?"mesh-paint-resample:":"mesh-paint-geometry:")+key,[source,entity=record.id,level,revision,paint=resample?record.meshPaint:std::optional<SceneMeshPaint>{},resample](std::stop_token stop){const auto data=build(source,entity,level,revision,paint,resample,stop);return ResourceLoadResult<ModelAsset>{data,bytes(*data),data->signature,{}};},std::move(options));}
    diagnostic=entry.resource.diagnostic();
    if(entry.resource.state()==ResourceState::Failed&&diagnostic.starts_with("Original paint geometry cache:")) {
        if(!entry.repairRequested){entry.repairRequested=true;assets_.rebuildModelCache(record.model->id);diagnostic="Preparing original mesh geometry.";return {};}
        if(assets_.modelLoading(record.model->id)){diagnostic="Preparing original mesh geometry.";return {};}
    }
    if(diagnostic.empty()&&entry.resource.state()!=ResourceState::Ready)diagnostic="Preparing subdivision geometry.";return entry.resource;
}
PreparedMeshPaintModel MeshPaintResources::prepare(const EntityRecord& record,ResourcePriority priority){CRIMSON_PROFILE_ZONE("Vertex paint prepared geometry and weights");PreparedMeshPaintModel out;std::string diagnostic;const auto handle=request(record,record.meshPaint?record.meshPaint->subdivisionLevel:0,priority,false,diagnostic);out.diagnostic=diagnostic;out.failed=handle&&handle.state()==ResourceState::Failed;
    const auto snapshot=handle.snapshot();if(!snapshot||handle.state()!=ResourceState::Ready){statuses_[record.id]=out.diagnostic;return out;}
    const auto data=std::static_pointer_cast<const ModelData>(snapshot.shared());out.asset=data;out.revision=data->revision;out.signature=data->signature;out.ready=true;
    // Generated smooth normals depend on triangle density. Retain their exact
    // render mesh; only explicit/flat normal modes use the proven proxy.
    out.decalAsset=record.meshShading!=MeshShading::Smooth&&data->decalAsset?data->decalAsset:out.asset;
    out.decalSignature=out.signature;if(out.decalAsset!=out.asset)mix(out.decalSignature,0x646563616c707278ull);mix(out.decalSignature,0x626f756e64617279ull);
    for(std::uint32_t i=0;i<data->paintedDraws.size();++i){const auto& d=data->paintedDraws[i];MeshPaintPreparedDraw result;result.draw=i;result.sourcePrimitive=d.sourcePrimitive;result.primitive=data->draws[i].primitive;result.geometry=d.geometry;result.decalBoundary=out.decalAsset==out.asset?d.boundary:d.decalBoundary;
        const SceneMeshPaintDraw* stored=nullptr;if(record.meshPaint)for(const auto& candidate:record.meshPaint->draws)if(candidate.draw==i&&candidate.primitive==d.sourcePrimitive){stored=&candidate;break;}
        if(stored){result.compatible=stored->sourceSignature==d.geometry->sourceSignature&&stored->level==d.geometry->level&&stored->weights&&stored->weights->vertexCount==d.geometry->vertices.size();if(result.compatible)result.weights=stored->weights;else out.diagnostic="Vertex paint source changed. Reset Paint to start again.";}
        if(!result.weights)result.weights=d.base;out.bindings.push_back({result.draw,result.primitive,result.geometry,result.weights});out.draws.push_back(std::move(result));}
    if(record.meshPaint)for(const auto& stored:record.meshPaint->draws)if(stored.draw>=data->paintedDraws.size()||data->paintedDraws[stored.draw].sourcePrimitive!=stored.primitive)out.diagnostic="Vertex paint source changed. Reset Paint to start again.";
    statuses_[record.id]=out.diagnostic;return out;
}
PreparedMeshPaintModel MeshPaintResources::prepare(const ExtractedModel& model,ResourcePriority priority){EntityRecord record;record.id=model.id;record.model=AssetRef<ModelAsset>{model.assetId};record.primitive=model.primitive;record.modelMaterials=model.materialOverrides;record.blendSettings=model.blendSettings;record.modelAnimation=model.animation;record.modelRagdoll=model.ragdoll;record.meshPaint=model.meshPaint;record.meshShading=model.meshShading;return prepare(record,priority);}
VertexPaintTargets MeshPaintResources::targets(const SceneDocument& document,EntityId entity){CRIMSON_PROFILE_ZONE("Vertex paint selected draw preparation");VertexPaintTargets out;const auto record=document.scene().find(entity);const auto world=document.scene().worldTransform(entity);if(!record||!world||!document.scene().isVisible(entity)){out.diagnostic="Select a visible rigid mesh.";return out;}
    const auto prepared=prepare(*record,ResourcePriority::Selected);out.diagnostic=prepared.diagnostic;if(!prepared.ready||!out.diagnostic.empty())return out;
    try{const auto materials=assets_.resolvedModelMaterials(*prepared.asset,record->modelMaterials,record->blendSettings);for(const auto& draw:prepared.draws){const auto& p=prepared.asset->primitives[draw.primitive];if(p.material>=materials.size()||!materials[p.material].graph)continue;const auto& output=materialOutputNode(*materials[p.material].graph).output;
            if(output.shading!=MaterialShading::BlendPbr||output.blend!=MaterialBlend::Opaque||output.tessellation.enabled)continue;
            VertexPaintTarget target;target.entity=entity;target.draw=draw.draw;target.primitive=draw.primitive;target.sourcePrimitive=draw.sourcePrimitive;target.resourceRevision=prepared.revision;target.materialSlot=p.material;
            target.materialId=p.material<record->modelMaterials.size()&&!record->modelMaterials[p.material].empty()?record->modelMaterials[p.material].id:
                p.material<prepared.asset->materialIds.size()?prepared.asset->materialIds[p.material]:importedMaterialId(record->model->id,p.material);
            target.sourceNode=prepared.asset->draws[draw.draw].node;target.world=*world*prepared.asset->draws[draw.draw].transform;target.geometry=draw.geometry;target.weights=draw.weights;out.draws.push_back(std::move(target));}
        if(out.draws.empty())out.diagnostic="Assign an opaque Blend material to this mesh.";
    }catch(const std::exception& error){out.draws.clear();out.diagnostic=error.what();}return out;
}
std::vector<RenderCollisionOverlay> MeshPaintResources::subdivisionOverlays(const SceneDocument& document,std::span<const EntityId> selection) {
    CRIMSON_PROFILE_ZONE("Vertex paint selected subdivision overlays");
    std::vector<RenderCollisionOverlay> result;const auto now=std::chrono::steady_clock::now();
    for(const auto entity:selection) {
        const auto record=document.scene().find(entity);const auto world=document.scene().worldTransform(entity);
        if(!record||!record->model||!world||!document.scene().isVisible(entity))continue;
        const auto prepared=prepare(*record,ResourcePriority::Selected);if(!prepared.ready)continue;
        for(const auto& draw:prepared.draws) {
            if(!draw.geometry||draw.draw>=prepared.asset->draws.size())continue;
            const auto* key=draw.geometry.get();auto found=subdivisionOverlays_.find(key);
            if(found!=subdivisionOverlays_.end()&&found->second.source.lock()!=draw.geometry) {
                subdivisionOverlayBytes_-=found->second.reservedBytes;subdivisionOverlays_.erase(found);found=subdivisionOverlays_.end();
            }
            if(found==subdivisionOverlays_.end()) {
                // Reserve a conservative output bound before launching work.
                // Changing selection cannot retain an unbounded set of jobs.
                const auto estimate=sizeof(CollisionOverlayGeometry)+draw.geometry->vertices.size()*sizeof(Vec3)+
                    draw.geometry->cells.size()*4*sizeof(CollisionOverlayEdge);
                if(estimate>MaximumSubdivisionOverlayBytes)continue;
                while(!subdivisionOverlays_.empty()&&(subdivisionOverlayBytes_+estimate>MaximumSubdivisionOverlayBytes||subdivisionOverlays_.size()>=MaximumSubdivisionOverlayEntries)) {
                    const auto oldest=std::min_element(subdivisionOverlays_.begin(),subdivisionOverlays_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
                    if(oldest->second.used==now)break;
                    subdivisionOverlayBytes_-=oldest->second.reservedBytes;subdivisionOverlays_.erase(oldest);
                }
                if(subdivisionOverlayBytes_+estimate>MaximumSubdivisionOverlayBytes||subdivisionOverlays_.size()>=MaximumSubdivisionOverlayEntries)continue;
                ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;options.cancelWhenUnused=true;
                SubdivisionOverlayEntry entry;entry.source=draw.geometry;entry.used=now;entry.reservedBytes=estimate;
                entry.resource=resources_.requestDerived<CollisionOverlayGeometry>(
                    "vertex-paint-subdivision-overlay:"+std::to_string(subdivisionOverlayOwner_)+":"+std::to_string(++subdivisionOverlayJob_),
                    [geometry=draw.geometry](std::stop_token stop) {
                        const auto overlay=buildVertexPaintSubdivisionOverlay(*geometry,stop);
                        const auto bytes=sizeof(CollisionOverlayGeometry)+overlay->vertices.capacity()*sizeof(Vec3)+overlay->edges.capacity()*sizeof(CollisionOverlayEdge);
                        return ResourceLoadResult<CollisionOverlayGeometry>{overlay,bytes,geometry->signature,{}};
                    },std::move(options));
                found=subdivisionOverlays_.emplace(key,std::move(entry)).first;subdivisionOverlayBytes_+=estimate;
            }
            auto& entry=found->second;entry.used=now;
            const auto snapshot=entry.resource.snapshot();if(!snapshot||entry.resource.state()!=ResourceState::Ready)continue;
            const auto transform=*world*prepared.asset->draws[draw.draw].transform;
            result.push_back({snapshot.shared(),transform,{.1f,.85f,1.f,.8f},0.f});
        }
    }
    return result;
}
VertexPaintSurfaceSnapshot MeshPaintResources::surfaces(const SceneDocument& document,const SurfaceBoundsQuery& surfaceBounds) {
    CRIMSON_PROFILE_ZONE("Vertex paint surface snapshot preparation");
    std::vector<VertexPaintSurfaceMesh> surfaces;std::uint64_t signature=1469598103934665603ull;
    std::uint64_t readiness=1469598103934665603ull;
    const auto valueHash=[&](const auto& value){const auto* bytes=reinterpret_cast<const unsigned char*>(&value);for(std::size_t i=0;i<sizeof(value);++i){signature^=bytes[i];signature*=1099511628211ull;}};
    try {
    for(const auto& occurrence:extractScene(document.scene()).models) {
        if(!occurrence.visible)continue;
        auto model=assets_.findModel(occurrence.assetId,occurrence.primitive);
        if(!model)continue;
        const auto revision=std::max<std::uint64_t>(1,assets_.modelRevision(occurrence.assetId));
        auto materials=assets_.resolvedModelMaterials(*model,occurrence.materialOverrides,occurrence.blendSettings);
        const bool rigid=occurrence.animation.asset.empty()&&occurrence.ragdoll.asset.empty()&&rigidSource(*model);
        if(!rigid) {
            const auto inverse=math::inverse(occurrence.world);if(!inverse)continue;
            const auto state=surfaceBounds?surfaceBounds(occurrence,model):SurfaceBounds{};const auto bounds=state.bounds;
            if(!bounds.valid)return {0,false,"Preparing animated viewport surfaces.",{}};
            valueHash(occurrence.id);valueHash(revision);valueHash(occurrence.world);valueHash(bounds.minimum);valueHash(bounds.maximum);valueHash(bounds.valid);valueHash(rigid);valueHash(state.signature);
            surfaces.push_back({{},occurrence.world,*inverse,bounds,occurrence.id,UINT32_MAX,UINT32_MAX,revision,true,false});
            continue;
        }
        bool painted=rigid&&occurrence.meshPaint.has_value();
        if(rigid&&!painted)for(const auto& material:materials)painted|=material.graph&&materialOutputNode(*material.graph).output.shading==MaterialShading::BlendPbr;
        bool geometryReady=true;
        if(painted) {
            const auto prepared=prepare(occurrence,ResourcePriority::Selected);
            if(prepared.ready)model=prepared.asset;else geometryReady=false;
        }
        const auto key=model->id+":"+std::to_string(revision);
        auto& entry=surfaceModels_[key];entry.used=std::chrono::steady_clock::now();
        if(rigid&&geometryReady&&!entry.resource) {
            ResourceRequestOptions options;options.priority=ResourcePriority::Selected;options.retainLoader=false;options.cancelWhenUnused=true;options.assetDependencies={occurrence.assetId};
            if(model->stream)for(const auto& p:model->stream->primitives)if(!p.levels.empty())options.dependencies.push_back(p.levels[0].file);
            entry.resource=resources_.requestDerived<ModelAsset>("vertex-paint-surface:"+key,[model](std::stop_token stop) {
                CRIMSON_PROFILE_ZONE("Vertex paint surface BVH worker");
                auto data=std::make_shared<SurfaceData>();data->source=model;data->meshes.resize(model->primitives.size());std::size_t bytes=sizeof(SurfaceData)+data->meshes.capacity()*sizeof(data->meshes.front());
                for(std::uint32_t primitive=0;primitive<model->primitives.size();++primitive) {
                    cancelled(stop);const auto* original=&model->primitives[primitive];std::shared_ptr<const ModelGeometryLevel> lod;
                    if(original->vertices.empty()&&model->stream&&primitive<model->stream->primitives.size()&&!model->stream->primitives[primitive].levels.empty()) {
                        lod=streaming::loadModelLod(model->stream->primitives[primitive].levels[0],primitive,0,false,stop);original=&lod->geometry;
                    }
                    {CRIMSON_PROFILE_ZONE("Vertex paint original LOD BVH build");data->meshes[primitive]=build_shadow_ray_mesh(*original,stop);}if(data->meshes[primitive])bytes+=data->meshes[primitive]->bytes();
                }
                return ResourceLoadResult<ModelAsset>{data,bytes,0,{}};
            },std::move(options));
        }
        const auto snapshot=entry.resource.snapshot();const auto data=snapshot&&entry.resource.state()==ResourceState::Ready?std::static_pointer_cast<const SurfaceData>(snapshot.shared()):nullptr;
        valueHash(occurrence.id);valueHash(revision);valueHash(occurrence.world);valueHash(rigid);valueHash(geometryReady);mix(readiness,bool(data));
        for(const auto c:model->id)valueHash(c);
        bool hashedCoverage=false;
        for(std::uint32_t drawIndex=0;drawIndex<model->draws.size();++drawIndex) {
            const auto& draw=model->draws[drawIndex];if(draw.primitive>=model->primitives.size())continue;
            const auto& primitive=model->primitives[draw.primitive];
            const auto world=occurrence.world*draw.transform;const auto inverse=math::inverse(world);if(!inverse)continue;
            bool exact=rigid&&geometryReady,doubleSided=false;float displacement{};
            if(primitive.material<materials.size()) {
                const auto& material=materials[primitive.material];doubleSided=material.doubleSided;
                if(material.graph) {const auto& output=materialOutputNode(*material.graph).output;exact&=output.blend==MaterialBlend::Opaque&&!output.tessellation.enabled;doubleSided=output.doubleSided;displacement=materialTessellationDisplacementExtent(output.tessellation);}
                else exact&=!material.alphaMask&&!material.alphaBlend;
            }else exact=false;
            if(!exact&&!hashedCoverage&&surfaceBounds) {valueHash(surfaceBounds(occurrence,model).signature);hashedCoverage=true;}
            const auto mesh=data&&draw.primitive<data->meshes.size()&&displacement==0?data->meshes[draw.primitive]:nullptr;
            auto bounds=mesh?mesh->bounds:primitive.bounds;
            if(displacement>0&&bounds.valid)for(unsigned axis=0;axis<3;++axis){bounds.minimum[axis]-=displacement;bounds.maximum[axis]+=displacement;}
            valueHash(draw.primitive);valueHash(draw.transform);valueHash(exact);valueHash(doubleSided);valueHash(displacement);
            surfaces.push_back({mesh,world,*inverse,bounds,occurrence.id,drawIndex,draw.primitive,revision,doubleSided,exact&&bool(data)});
        }
    }
    }catch(const std::exception& error){return {0,false,error.what(),{}};}
    mix(readiness,signature);
    if(surfaceSnapshot_.ready&&surfaceSnapshotKey_==readiness)return surfaceSnapshot_;
    surfaceSnapshotKey_=readiness;
    surfaceSnapshot_=makeVertexPaintSurfaceSnapshot(std::move(surfaces),signature);return surfaceSnapshot_;
}
bool MeshPaintResources::subdivision(const EntityRecord& record,std::uint32_t level,SceneMeshPaint& result,std::string& diagnostic){const auto handle=request(record,level,ResourcePriority::Selected,true,diagnostic);const auto snapshot=handle.snapshot();if(!snapshot||handle.state()!=ResourceState::Ready)return false;const auto data=std::static_pointer_cast<const ModelData>(snapshot.shared());if(!data->resampled)return false;result=*data->resampled;diagnostic.clear();return true;}
std::string MeshPaintResources::status(EntityId entity)const{const auto found=statuses_.find(entity);return found==statuses_.end()?std::string{}:found->second;}
void MeshPaintResources::collect(){const auto now=std::chrono::steady_clock::now();const auto expired=[&](const auto& entry){return now-entry.second.used>resources_.config().idleGrace;};std::erase_if(geometry_,expired);std::erase_if(resampling_,expired);std::erase_if(surfaceModels_,expired);
    for(auto it=subdivisionOverlays_.begin();it!=subdivisionOverlays_.end();)if(expired(*it)) {subdivisionOverlayBytes_-=it->second.reservedBytes;it=subdivisionOverlays_.erase(it);}else ++it;
    if(surfaceModels_.empty()){surfaceSnapshot_={};surfaceSnapshotKey_=0;}
    std::unordered_set<EntityId> live;for(const auto& [key,entry]:geometry_){(void)key;live.insert(entry.entity);}for(const auto& [key,entry]:resampling_){(void)key;live.insert(entry.entity);}std::erase_if(statuses_,[&](const auto& entry){return !live.contains(entry.first);});}
void MeshPaintResources::clear(){geometry_.clear();resampling_.clear();surfaceModels_.clear();subdivisionOverlays_.clear();subdivisionOverlayBytes_=0;surfaceSnapshot_={};surfaceSnapshotKey_=0;statuses_.clear();}
}
