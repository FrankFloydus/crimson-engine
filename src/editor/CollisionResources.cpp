#include "CollisionResources.h"
#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "resources/StreamingModel.h"
#include <limits>
#include <stdexcept>

namespace crimson {
namespace {
std::shared_ptr<StaticTriangleMesh> flatten(const ModelAsset& asset,std::stop_token stop) {
    auto result=std::make_shared<StaticTriangleMesh>();
    std::unordered_map<std::uint32_t,std::shared_ptr<const ModelGeometryLevel>> originals;
    for(const auto& draw:asset.draws) {
        if(stop.stop_requested())throw std::runtime_error("Collision preparation canceled.");
        if(draw.primitive>=asset.primitives.size())throw std::runtime_error("Collision draw has an invalid primitive.");
        const ModelPrimitive* geometry=&asset.primitives[draw.primitive];
        if(geometry->vertices.empty() && asset.stream) {
            auto& original=originals[draw.primitive];
            if(!original)original=streaming::loadModelLod(asset.stream->primitives.at(draw.primitive).levels.at(0),draw.primitive,0);
            geometry=&original->geometry;
        }
        if(result->positions.size()+geometry->vertices.size()>4'000'000 || result->indices.size()+geometry->indices.size()>12'000'000)
            throw std::runtime_error("Collision geometry exceeds its preparation budget.");
        const auto first=static_cast<std::uint32_t>(result->positions.size());
        for(const auto& vertex:geometry->vertices) {
            const auto position=math::transformPoint(draw.transform,vertex.position);
            if(!math::finite(position))throw std::runtime_error("Collision geometry has nonfinite positions.");
            result->positions.push_back(position);math::include(result->bounds,position);
        }
        if(geometry->indices.size()%3)throw std::runtime_error("Collision indices are not triangles.");
        const bool reflected=math::handedness(draw.transform)<0;
        for(std::size_t i=0;i<geometry->indices.size();i+=3) {
            auto a=geometry->indices[i],b=geometry->indices[i+1],c=geometry->indices[i+2];
            if(a>=geometry->vertices.size() || b>=geometry->vertices.size() || c>=geometry->vertices.size())
                throw std::runtime_error("Collision triangle index is out of bounds.");
            if(reflected)std::swap(b,c);
            if(math::length(math::cross(result->positions[first+b]-result->positions[first+a],result->positions[first+c]-result->positions[first+a]))<=1e-12)continue;
            result->indices.insert(result->indices.end(),{first+a,first+b,first+c});
        }
    }
    if(result->indices.empty())throw std::runtime_error("Collision source has no usable triangles.");
    result->fingerprint=asset.stream?asset.stream->key:streaming::modelContentKey(asset);
    return result;
}
}
CollisionMeshResult CollisionResources::query(const std::string& id) {
    const auto resolved=assets_.references().resolve(id);
    if(resolved.empty())return {};
    auto& entry=entries_[resolved];
    entry.used=std::chrono::steady_clock::now();
    const auto model=assets_.findModel(resolved);
    const auto revision=assets_.modelRevision(resolved);
    if(model && (!entry.resource || entry.sourceRevision!=revision)) {
        entry.sourceRevision=revision;
        ResourceRequestOptions options;options.variant="static-collision-"+std::to_string(revision);
        options.priority=ResourcePriority::Placement;options.retainLoader=false;
        options.dependencies={resolveAssetPath(assets_.projectRoot(),resolved)};
        entry.resource=resources_.request<StaticTriangleMesh>(AssetRef<StaticTriangleMesh>{resolved},[model](std::stop_token stop) {
            auto mesh=flatten(*model,stop);const auto bytes=sizeof(StaticTriangleMesh)+mesh->positions.size()*sizeof(Vec3)+mesh->indices.size()*sizeof(std::uint32_t);
            return ResourceLoadResult<StaticTriangleMesh>{mesh,bytes,mesh->fingerprint,{}};
        },std::move(options));
    }
    if(entry.resource) {
        if(const auto snapshot=entry.resource.snapshot())entry.accepted={snapshot.shared(),snapshot.revision(),entry.resource.state()==ResourceState::Ready,entry.resource.diagnostic()};
        else {entry.accepted.current=false;entry.accepted.diagnostic=entry.resource.diagnostic();}
        if(entry.resource.state()==ResourceState::Failed && model && model->stream && entry.repairKey!=model->stream->key) {
            entry.repairKey=model->stream->key;assets_.rebuildModelCache(resolved);
        }
    }
    const auto modelError=assets_.modelDiagnostic(resolved);
    if(assets_.modelLoading(resolved) || !modelError.empty()) {
        entry.accepted.current=false;
        if(!modelError.empty())entry.accepted.diagnostic=modelError;
        else entry.accepted.diagnostic="Preparing mesh collision source: "+resolved;
    }
    if(!entry.accepted.mesh && entry.accepted.diagnostic.empty())entry.accepted.diagnostic="Preparing mesh collision: "+resolved;
    return entry.accepted;
}
void CollisionResources::collect() {
    const auto now=std::chrono::steady_clock::now();const auto grace=resources_.config().idleGrace;
    std::erase_if(entries_,[&](const auto& entry){return now-entry.second.used>grace;});
}
}
