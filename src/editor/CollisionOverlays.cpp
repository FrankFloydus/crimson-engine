#include "CollisionOverlays.h"
#include "CollisionWire.h"
#include "MeshCollisionOverlay.h"
#include <algorithm>
#include <atomic>
#include <bit>

namespace crimson {
namespace {
std::atomic_uint64_t nextOwner{1};
template<class Point> std::shared_ptr<const CollisionOverlayGeometry> convert(const CollisionDebugMesh& debug,Point point,std::stop_token stop={},const SceneCollider* primitive=nullptr) {
    std::size_t processed{};
    const auto checkpoint=[&]{if((processed++&1023)==0&&stop.stop_requested())throw std::runtime_error("Collision overlay preparation canceled.");};
    auto result=std::make_shared<CollisionOverlayGeometry>();
    result->vertices.reserve(debug.vertices.size());
    for(const auto& vertex:debug.vertices){checkpoint();result->vertices.push_back(point(vertex));}
    result->triangles.reserve(debug.triangles.size()*3);
    for(const auto& triangle:debug.triangles){checkpoint();result->triangles.insert(result->triangles.end(),triangle.begin(),triangle.end());}
    result->wireVertices.reserve(debug.wireVertices.size());
    for(const auto& vertex:debug.wireVertices){checkpoint();result->wireVertices.push_back(point(vertex));}
    result->edges.reserve(debug.edges.size()+debug.silhouetteEdges.size());
    for(const auto& edge:debug.edges) {
        checkpoint();auto normalA=edge.normalA,normalB=edge.normalB;
        if(primitive) {
            const auto normals=colliderCollisionNormals(result->wireVertices[edge[0]],result->wireVertices[edge[1]],*primitive);
            normalA=normals[0];normalB=normals[1];
        }
        result->edges.push_back({edge[0],edge[1],normalA,normalB,false,edge.concave,edge.planeFallback});
    }
    for(const auto& edge:debug.silhouetteEdges){checkpoint();result->edges.push_back({edge.vertices[0],edge.vertices[1],edge.normalA,edge.normalB,true,edge.concave,edge.planeFallback});}
    return result;
}
std::size_t geometryBytes(const CollisionOverlayGeometry& geometry) {
    return sizeof(geometry)+geometry.vertices.size()*sizeof(Vec3)+geometry.triangles.size()*sizeof(std::uint32_t)+
        geometry.wireVertices.size()*sizeof(Vec3)+geometry.edges.size()*sizeof(CollisionOverlayEdge);
}
}
CollisionOverlays::CollisionOverlays(ResourceManager& resources):resources_(resources),owner_(nextOwner.fetch_add(1)){}
std::shared_ptr<const CollisionOverlayGeometry> CollisionOverlays::primitive(const SceneCollider& collider) {
    std::array<std::uint32_t,4> key{static_cast<std::uint32_t>(collider.shape)};
    if(collider.shape==ColliderShape::Box)for(unsigned i=0;i<3;++i)key[i+1]=std::bit_cast<std::uint32_t>(collider.size[i]);
    else {
        key[1]=std::bit_cast<std::uint32_t>(collider.radius);
        if(collider.shape!=ColliderShape::Sphere)key[2]=std::bit_cast<std::uint32_t>(collider.height);
    }
    auto& entry=primitives_[key];entry.used=frame_;
    if(!entry.geometry)entry.geometry=convert(colliderCollisionMesh(collider),[&](const Vec3& point){return colliderCollisionPoint(point,collider);},{},&collider);
    return entry.geometry;
}
std::shared_ptr<const CollisionOverlayGeometry> CollisionOverlays::mesh(const std::string& id,const CollisionMeshResult& source) {
    auto& entry=meshes_[id];entry.used=frame_;
    if(source.mesh && (entry.source.lock()!=source.mesh || source.revision!=entry.revision || !entry.pending)) {
        entry.source=source.mesh;entry.revision=source.revision;
        ResourceRequestOptions options;options.priority=ResourcePriority::Visible;options.retainLoader=false;options.cancelWhenUnused=true;
        entry.pending=resources_.requestDerived<CollisionOverlayGeometry>(
            "collision-overlay-"+std::to_string(owner_)+"-"+std::to_string(++job_),
            [original=source.mesh](std::stop_token stop) {
                const auto debug=buildMeshCollisionDebug(*original,stop);
                auto geometry=convert(debug,[](const Vec3& point){return point;},stop);
                return ResourceLoadResult<CollisionOverlayGeometry>{geometry,geometryBytes(*geometry),original->fingerprint,{}};
            },std::move(options));
    }
    if(entry.pending)if(const auto ready=entry.pending.snapshot())entry.accepted=ready.shared();
    return entry.accepted;
}
PreparedCollisionOverlays CollisionOverlays::prepare(const Scene& scene,std::span<const EntityId> selection,bool enabled,const CollisionMeshQuery& query) {
    ++frame_;
    std::erase_if(primitives_,[&](const auto& item){return frame_-item.second.used>4;});
    std::erase_if(meshes_,[&](const auto& item){return frame_-item.second.used>4;});
    PreparedCollisionOverlays result;
    if(!enabled)return result;
    for(const auto& record:scene.records()) {
        if(!scene.isVisible(record.id))continue;
        if(!record.collider && !record.character)continue;
        const auto world=scene.worldTransform(record.id);if(!world || !math::inverse(*world))continue;
        const bool selected=std::find(selection.begin(),selection.end(),record.id)!=selection.end();
        const auto append=[&](std::shared_ptr<const CollisionOverlayGeometry> geometry,const Mat4& transform,Vec4 color,bool inGame) {
            if(!geometry || color[3]<=0)return;
            if(inGame)result.game.push_back({geometry,transform,color});
            color[3]*=selected?.8f:.2f;
            result.scene.push_back({std::move(geometry),transform,color});
        };
        if(record.collider) {
            const auto& collider=*record.collider;
            if(collider.shape==ColliderShape::Trimesh) {
                if(collider.mesh || (query && !collider.source.empty())) {
                    const auto source=colliderMeshResult(collider,query);
                    // A retained model identity may temporarily have no current
                    // source snapshot. Its accepted overlay remains usable.
                    append(mesh(collider.mesh?"native:"+std::to_string(collider.mesh->fingerprint)+":"+
                        std::to_string(reinterpret_cast<std::uintptr_t>(collider.mesh.get())):collider.source.id,source),*world,collider.collisionColor,collider.showCollisionInGame);
                }
            } else append(primitive(collider),*world,collider.collisionColor,collider.showCollisionInGame);
        }
        if(record.character) {
            const auto& character=*record.character;SceneCollider capsule;
            capsule.shape=ColliderShape::Capsule;capsule.radius=character.radius;capsule.height=character.height;
            append(primitive(capsule),*world*math::translation({0,character.height*.5f,0}),character.collisionColor,character.showCollisionInGame);
        }
    }
    return result;
}
}
