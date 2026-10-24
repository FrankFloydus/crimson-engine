#include "MeshPaintResources.h"
#include "assets/AssetLibrary.h"
#include "assets/MaterialGraph.h"
#include "assets/MaterialAsset.h"
#include "resources/StreamingModel.h"
#include "scene/SceneExtraction.h"
#include <algorithm>
#include <sstream>
#include <unordered_set>

namespace crimson {
namespace {
struct ModelData final:ModelAsset {
    struct Draw {std::uint32_t sourcePrimitive{};std::shared_ptr<const MeshPaintGeometry> geometry;std::shared_ptr<const MeshPaintPayload> base;};
    std::vector<Draw> paintedDraws;
    std::optional<SceneMeshPaint> resampled;
    std::shared_ptr<const ModelAsset> source;
    std::uint64_t revision{},signature{};
};
void cancelled(std::stop_token stop){if(stop.stop_requested())throw std::runtime_error("Mesh paint preparation cancelled.");}
void mix(std::uint64_t& hash,std::uint64_t value){hash^=value+0x9e3779b97f4a7c15ull+(hash<<6)+(hash>>2);}
std::uint64_t paintKey(const std::optional<SceneMeshPaint>& paint){std::uint64_t hash=1469598103934665603ull;if(paint)for(const auto& draw:paint->draws){mix(hash,draw.draw);mix(hash,draw.primitive);mix(hash,draw.level);mix(hash,draw.sourceSignature);mix(hash,draw.weights?draw.weights->fingerprint:0);}return hash;}
std::size_t bytes(const ModelData& model){std::size_t total=sizeof(ModelData);std::unordered_set<const MeshPaintGeometry*> geometries;
    for(const auto& p:model.primitives)total+=sizeof(ModelPrimitive)+p.vertices.size()*sizeof(ModelVertex)+p.indices.size()*sizeof(std::uint32_t);
    for(const auto& d:model.paintedDraws)if(geometries.insert(d.geometry.get()).second){total+=sizeof(MeshPaintGeometry)+d.geometry->vertices.size()*sizeof(ModelVertex)+d.geometry->indices.size()*sizeof(std::uint32_t)+d.geometry->cells.size()*sizeof(MeshPaintCell)+d.geometry->boundaryEdges.size()*sizeof(std::array<std::uint32_t,2>);
        for(const auto& list:d.geometry->vertexTriangles)total+=sizeof(list)+list.size()*sizeof(std::uint32_t);for(const auto& list:d.geometry->seamVertices)total+=sizeof(list)+list.size()*sizeof(std::uint32_t);}
    if(model.resampled)for(const auto& d:model.resampled->draws)if(d.weights)for(const auto& page:d.weights->pages)if(page)total+=sizeof(MeshPaintPage);return total;
}
std::shared_ptr<ModelData> build(const std::shared_ptr<const ModelAsset>& source,EntityId entity,std::uint32_t level,std::uint64_t revision,std::optional<SceneMeshPaint> paint,bool resample,std::stop_token stop){
    cancelled(stop);if(!source->skins.empty()||!source->animations.empty())throw std::runtime_error("Vertex painting supports rigid static meshes.");
    auto result=std::make_shared<ModelData>();static_cast<ModelAsset&>(*result)=*source;result->source=source;result->revision=revision;result->stream.reset();result->primitives.clear();result->paintedDraws.clear();result->signature=revision;mix(result->signature,level);
    std::vector<std::shared_ptr<const ModelPrimitive>> originals(source->primitives.size());std::vector<std::shared_ptr<const MeshPaintGeometry>> geometries(source->primitives.size());
    std::uint64_t vertexTotal{},triangleTotal{};
    for(std::uint32_t drawIndex=0;drawIndex<source->draws.size();++drawIndex){cancelled(stop);const auto& draw=source->draws[drawIndex];if(draw.primitive>=source->primitives.size())throw std::runtime_error("Paint draw has an invalid primitive.");auto& original=originals[draw.primitive];
        if(!original){const auto& primitive=source->primitives[draw.primitive];if(!primitive.vertices.empty())original={source,&primitive};else {if(!source->stream||draw.primitive>=source->stream->primitives.size()||source->stream->primitives[draw.primitive].levels.empty())throw std::runtime_error("Original mesh geometry is unavailable.");
            try{const auto lod=streaming::loadModelLod(source->stream->primitives[draw.primitive].levels[0],draw.primitive,0,false,stop);original={lod,&lod->geometry};}
            catch(const std::exception& error){cancelled(stop);throw std::runtime_error(std::string("Original paint geometry cache: ")+error.what());}}}
        auto& geometry=geometries[draw.primitive];if(!geometry)geometry=buildMeshPaintGeometry(*original,level,stop);
        vertexTotal+=geometry->vertices.size();triangleTotal+=geometry->indices.size()/3;if(vertexTotal>MaximumMeshPaintVertices||triangleTotal>MaximumMeshPaintTriangles)throw std::runtime_error("Subdivision exceeds this entity's 262144 vertex / 524288 triangle budget.");
        const auto primitive=static_cast<std::uint32_t>(result->primitives.size());result->primitives.push_back(meshPaintRenderPrimitive(*original,*geometry));result->draws[drawIndex].primitive=primitive;result->paintedDraws.push_back({draw.primitive,geometry,makeMeshPaintPayload(static_cast<std::uint32_t>(geometry->vertices.size()))});mix(result->signature,geometry->signature);
    }
    result->id="mesh-paint:"+source->id+":"+std::to_string(entity)+":"+std::to_string(result->signature);
    if(result->materialIds.size()<result->materials.size())result->materialIds.resize(result->materials.size());
    for(std::size_t i=0;i<result->materials.size();++i)if(result->materialIds[i].empty())result->materialIds[i]=importedMaterialId(source->id,i);
    // Keep model-node metadata coherent with expanded occurrence primitives.
    for(std::size_t mesh=0;mesh<result->meshes.size();++mesh){auto& p=result->meshes[mesh].primitives;p.clear();for(std::uint32_t i=0;i<source->draws.size();++i){const auto node=source->draws[i].node;if(node>=0&&static_cast<std::size_t>(node)<source->nodes.size()&&source->nodes[node].mesh==static_cast<std::int32_t>(mesh))p.push_back(result->draws[i].primitive);}}
    if(resample){result->resampled=paint.value_or(SceneMeshPaint{});auto& replacement=*result->resampled;replacement.subdivisionLevel=level;replacement.recipeVersion=MeshPaintRecipeVersion;
        for(auto& stored:replacement.draws){cancelled(stop);if(stored.draw>=result->paintedDraws.size()||result->paintedDraws[stored.draw].sourcePrimitive!=stored.primitive)throw std::runtime_error("Reset incompatible paint before changing subdivision.");const auto& current=result->paintedDraws[stored.draw];
            if(stored.sourceSignature!=current.geometry->sourceSignature||!stored.weights)throw std::runtime_error("Reset incompatible paint before changing subdivision.");
            if(stored.level!=level){const auto before=buildMeshPaintGeometry(*originals[stored.primitive],stored.level,stop);stored.weights=resampleMeshPaintPayload(*before,*stored.weights,*current.geometry,stop);stored.level=level;}
            else if(stored.weights->vertexCount!=current.geometry->vertices.size())throw std::runtime_error("Stored paint vertex count no longer matches its source.");}
    }
    return result;
}
}
MeshPaintResources::MeshPaintResources(AssetLibrary& assets,ResourceManager& resources):assets_(assets),resources_(resources){}
ResourceHandle<ModelAsset> MeshPaintResources::request(const EntityRecord& record,std::uint32_t level,ResourcePriority priority,bool resample,std::string& diagnostic){
    if(!record.model){diagnostic="Select a rigid mesh.";return {};}
    if(level>MaximumMeshPaintLevel){diagnostic="Subdivision Level must be from 0 to 5.";return {};}
    const auto source=assets_.findModel(record.model->id,record.primitive);if(!source){diagnostic=assets_.modelDiagnostic(record.model->id);if(diagnostic.empty())diagnostic="Preparing original mesh geometry.";return {};}
    if(!record.modelAnimation.asset.empty()||!record.modelRagdoll.asset.empty()||!source->skins.empty()||!source->animations.empty()){diagnostic="Vertex painting supports rigid static meshes.";return {};}
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
PreparedMeshPaintModel MeshPaintResources::prepare(const EntityRecord& record,ResourcePriority priority){PreparedMeshPaintModel out;std::string diagnostic;const auto handle=request(record,record.meshPaint?record.meshPaint->subdivisionLevel:0,priority,false,diagnostic);out.diagnostic=diagnostic;out.failed=handle&&handle.state()==ResourceState::Failed;
    const auto snapshot=handle.snapshot();if(!snapshot||handle.state()!=ResourceState::Ready){statuses_[record.id]=out.diagnostic;return out;}
    const auto data=std::static_pointer_cast<const ModelData>(snapshot.shared());out.asset=data;out.revision=data->revision;out.signature=data->signature;out.ready=true;
    for(std::uint32_t i=0;i<data->paintedDraws.size();++i){const auto& d=data->paintedDraws[i];MeshPaintPreparedDraw result;result.draw=i;result.sourcePrimitive=d.sourcePrimitive;result.primitive=data->draws[i].primitive;result.geometry=d.geometry;
        const SceneMeshPaintDraw* stored=nullptr;if(record.meshPaint)for(const auto& candidate:record.meshPaint->draws)if(candidate.draw==i&&candidate.primitive==d.sourcePrimitive){stored=&candidate;break;}
        if(stored){result.compatible=stored->sourceSignature==d.geometry->sourceSignature&&stored->level==d.geometry->level&&stored->weights&&stored->weights->vertexCount==d.geometry->vertices.size();if(result.compatible)result.weights=stored->weights;else out.diagnostic="Vertex paint source changed. Reset Paint to start again.";}
        if(!result.weights)result.weights=d.base;out.bindings.push_back({result.draw,result.primitive,result.geometry,result.weights});out.draws.push_back(std::move(result));}
    if(record.meshPaint)for(const auto& stored:record.meshPaint->draws)if(stored.draw>=data->paintedDraws.size()||data->paintedDraws[stored.draw].sourcePrimitive!=stored.primitive)out.diagnostic="Vertex paint source changed. Reset Paint to start again.";
    statuses_[record.id]=out.diagnostic;return out;
}
PreparedMeshPaintModel MeshPaintResources::prepare(const ExtractedModel& model,ResourcePriority priority){EntityRecord record;record.id=model.id;record.model=AssetRef<ModelAsset>{model.assetId};record.primitive=model.primitive;record.modelMaterials=model.materialOverrides;record.modelAnimation=model.animation;record.modelRagdoll=model.ragdoll;record.meshPaint=model.meshPaint;return prepare(record,priority);}
VertexPaintTargets MeshPaintResources::targets(const SceneDocument& document,EntityId entity){VertexPaintTargets out;const auto record=document.scene().find(entity);const auto world=document.scene().worldTransform(entity);if(!record||!world||!document.scene().isVisible(entity)){out.diagnostic="Select a visible rigid mesh.";return out;}
    const auto prepared=prepare(*record,ResourcePriority::Selected);out.diagnostic=prepared.diagnostic;if(!prepared.ready||!out.diagnostic.empty())return out;
    try{const auto materials=assets_.resolvedModelMaterials(*prepared.asset,record->modelMaterials);for(const auto& draw:prepared.draws){const auto& p=prepared.asset->primitives[draw.primitive];if(p.material>=materials.size()||!materials[p.material].graph)continue;const auto& output=materialOutputNode(*materials[p.material].graph).output;
            if(output.shading!=MaterialShading::BlendPbr||output.blend!=MaterialBlend::Opaque||output.tessellation.enabled)continue;
            VertexPaintTarget target;target.entity=entity;target.draw=draw.draw;target.primitive=draw.primitive;target.sourcePrimitive=draw.sourcePrimitive;target.resourceRevision=prepared.revision;target.sourceNode=prepared.asset->draws[draw.draw].node;target.world=*world*prepared.asset->draws[draw.draw].transform;target.geometry=draw.geometry;target.weights=draw.weights;out.draws.push_back(std::move(target));}
        if(out.draws.empty())out.diagnostic="Assign an opaque Blend material to this mesh.";
    }catch(const std::exception& error){out.draws.clear();out.diagnostic=error.what();}return out;
}
bool MeshPaintResources::subdivision(const EntityRecord& record,std::uint32_t level,SceneMeshPaint& result,std::string& diagnostic){const auto handle=request(record,level,ResourcePriority::Selected,true,diagnostic);const auto snapshot=handle.snapshot();if(!snapshot||handle.state()!=ResourceState::Ready)return false;const auto data=std::static_pointer_cast<const ModelData>(snapshot.shared());if(!data->resampled)return false;result=*data->resampled;diagnostic.clear();return true;}
std::string MeshPaintResources::status(EntityId entity)const{const auto found=statuses_.find(entity);return found==statuses_.end()?std::string{}:found->second;}
void MeshPaintResources::collect(){const auto now=std::chrono::steady_clock::now();const auto expired=[&](const auto& entry){return now-entry.second.used>resources_.config().idleGrace;};std::erase_if(geometry_,expired);std::erase_if(resampling_,expired);
    std::unordered_set<EntityId> live;for(const auto& [key,entry]:geometry_){(void)key;live.insert(entry.entity);}for(const auto& [key,entry]:resampling_){(void)key;live.insert(entry.entity);}std::erase_if(statuses_,[&](const auto& entry){return !live.contains(entry.first);});}
void MeshPaintResources::clear(){geometry_.clear();resampling_.clear();statuses_.clear();}
}
