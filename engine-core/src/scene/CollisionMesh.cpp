#include "CollisionMesh.h"
#include <bit>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace crimson {
std::shared_ptr<const CollisionMesh> makeCollisionMesh(std::vector<Vec3> positions,std::vector<std::uint32_t> indices) {
    if(positions.empty() || positions.size()>4u*1024u*1024u || indices.empty() || indices.size()%3 || indices.size()>12u*1024u*1024u)
        throw std::runtime_error("Collider mesh exceeds its triangle limits or has no complete triangles.");
    auto mesh=std::make_shared<CollisionMesh>();
    std::uint64_t fingerprint=14695981039346656037ull;
    const auto hash=[&](std::uint32_t value) {for(unsigned byte=0;byte<4;++byte){fingerprint^=(value>>(byte*8))&255u;fingerprint*=1099511628211ull;}};
    hash(static_cast<std::uint32_t>(positions.size()));hash(static_cast<std::uint32_t>(indices.size()));
    for(const auto& position:positions) {
        if(!math::finite(position))throw std::runtime_error("Collider mesh positions must be finite.");
        math::include(mesh->bounds,position);
        for(const auto value:position)hash(std::bit_cast<std::uint32_t>(value));
    }
    for(std::size_t i=0;i<indices.size();i+=3) {
        const auto a=indices[i],b=indices[i+1],c=indices[i+2];
        if(a>=positions.size() || b>=positions.size() || c>=positions.size())throw std::runtime_error("Collider mesh triangle index exceeds its positions.");
        const auto cross=math::cross(positions[b]-positions[a],positions[c]-positions[a]);
        if(!math::finite(cross) || !(math::dot(cross,cross)>0))throw std::runtime_error("Collider mesh contains a degenerate triangle.");
        hash(a);hash(b);hash(c);
    }
    mesh->positions=std::move(positions);mesh->indices=std::move(indices);mesh->fingerprint=fingerprint;mesh->validated_=true;
    return mesh;
}
bool sameCollisionMesh(const std::shared_ptr<const CollisionMesh>& a,const std::shared_ptr<const CollisionMesh>& b) {
    return a==b || (a && b && a->fingerprint==b->fingerprint && a->positions==b->positions && a->indices==b->indices);
}
}
