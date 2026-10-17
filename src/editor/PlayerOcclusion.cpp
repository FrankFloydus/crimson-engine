#include "PlayerOcclusion.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>

namespace crimson {
struct PlayerOcclusionGeometry {
    struct Node {Aabb bounds;std::uint32_t first{},count{},right{};};
    std::shared_ptr<const StaticTriangleMesh> source;
    std::vector<std::uint32_t> triangles;
    std::vector<Node> nodes;
};
namespace {
constexpr double SettleEpsilon=.001;
std::atomic_uint64_t nextOwner{1};
void ease(double& opacity,double target,double seconds,const PlayerOcclusionSettings& settings) {
    const auto time=target<opacity?settings.fadeOutSeconds:settings.fadeInSeconds;
    if(time<=0){opacity=target;return;}
    if(seconds<=0)return;
    opacity=target+(opacity-target)*std::exp(-seconds/time);
    if(std::abs(opacity-target)<=SettleEpsilon)opacity=target;
}
bool intersects(const Aabb& bounds,const Vec3& origin,const Vec3& direction,double maximum=1) {
    if(!bounds.valid)return false;
    double first=0,last=maximum;
    for(unsigned axis=0;axis<3;++axis) {
        if(direction[axis]==0) {if(origin[axis]<bounds.minimum[axis]||origin[axis]>bounds.maximum[axis])return false;continue;}
        auto a=(double(bounds.minimum[axis])-origin[axis])/direction[axis],b=(double(bounds.maximum[axis])-origin[axis])/direction[axis];
        if(a>b)std::swap(a,b);first=std::max(first,a);last=std::min(last,b);
        if(first>last)return false;
    }
    return true;
}
std::shared_ptr<const PlayerOcclusionGeometry> build(std::shared_ptr<const StaticTriangleMesh> source,std::stop_token stop) {
    auto result=std::make_shared<PlayerOcclusionGeometry>();result->source=std::move(source);
    const auto& mesh=*result->source;
    result->triangles.reserve(mesh.indices.size()/3);
    for(std::size_t first=0;first+2<mesh.indices.size();first+=3) {
        if((first%3072)==0&&stop.stop_requested())throw std::runtime_error("Player occlusion preparation canceled.");
        bool valid=true;
        for(unsigned i=0;i<3;++i)if(mesh.indices[first+i]>=mesh.positions.size()||!math::finite(mesh.positions[mesh.indices[first+i]])){valid=false;break;}
        if(valid)result->triangles.push_back(static_cast<std::uint32_t>(first));
    }
    if(result->triangles.empty())return result;
    result->nodes.reserve(result->triangles.size()/4+1);
    const auto coordinate=[&](std::uint32_t triangle,unsigned axis) {
        double sum{};for(unsigned i=0;i<3;++i)sum+=mesh.positions[mesh.indices[triangle+i]][axis];return sum/3;
    };
    const auto split=[&](auto&& self,std::uint32_t first,std::uint32_t count)->std::uint32_t {
        if(stop.stop_requested())throw std::runtime_error("Player occlusion preparation canceled.");
        Aabb bounds,centers;
        for(std::uint32_t i=first;i<first+count;++i) {
            if((i&1023)==0&&stop.stop_requested())throw std::runtime_error("Player occlusion preparation canceled.");
            const auto triangle=result->triangles[i];
            for(unsigned vertex=0;vertex<3;++vertex)math::include(bounds,mesh.positions[mesh.indices[triangle+vertex]]);
            math::include(centers,Vec3{coordinate(triangle,0),coordinate(triangle,1),coordinate(triangle,2)});
        }
        const auto index=static_cast<std::uint32_t>(result->nodes.size());result->nodes.push_back({bounds,first,count,0});
        if(count<=8)return index;
        const auto extent=centers.maximum-centers.minimum;unsigned axis{};
        for(unsigned i=1;i<3;++i)if(extent[i]>extent[axis])axis=i;
        const auto middle=first+count/2;
        std::nth_element(result->triangles.begin()+first,result->triangles.begin()+middle,result->triangles.begin()+first+count,
            [&](auto a,auto b){return coordinate(a,axis)<coordinate(b,axis);});
        const auto left=self(self,first,middle-first),right=self(self,middle,first+count-middle);
        result->nodes[index]={bounds,left,0,right};return index;
    };
    split(split,0,static_cast<std::uint32_t>(result->triangles.size()));return result;
}
double triangleHit(const Vec3& origin,const Vec3& direction,const Vec3& a,const Vec3& b,const Vec3& c,double maximum) {
    using D=std::array<double,3>;
    const auto cross=[](const D& u,const D& v)->D{return {u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]};};
    const auto dot=[](const D& u,const D& v){return u[0]*v[0]+u[1]*v[1]+u[2]*v[2];};
    D u{},v{},d{},offset{};
    for(unsigned axis=0;axis<3;++axis){u[axis]=double(b[axis])-a[axis];v[axis]=double(c[axis])-a[axis];d[axis]=direction[axis];offset[axis]=double(origin[axis])-a[axis];}
    const auto h=cross(d,v);const auto determinant=dot(u,h);
    if(std::abs(determinant)<=1e-12*std::sqrt(dot(u,u)*dot(v,v)*dot(d,d)))return maximum;
    const auto first=dot(offset,h)/determinant;if(first<0||first>1)return maximum;
    const auto q=cross(offset,u);const auto second=dot(d,q)/determinant;if(second<0||first+second>1)return maximum;
    const auto t=dot(v,q)/determinant;return t>1e-5&&t<maximum?t:maximum;
}
double hit(const PlayerOcclusionGeometry& geometry,const Vec3& origin,const Vec3& direction,double maximum,PlayerOcclusionStats& stats) {
    if(geometry.nodes.empty())return maximum;
    std::array<std::uint32_t,64> stack{};unsigned count=1;double closest=maximum;
    const auto& mesh=*geometry.source;
    while(count) {
        const auto& node=geometry.nodes[stack[--count]];++stats.nodeTests;
        if(!intersects(node.bounds,origin,direction,closest))continue;
        if(!node.count){stack[count++]=node.first;stack[count++]=node.right;continue;}
        for(std::uint32_t i=node.first;i<node.first+node.count;++i) {
            const auto triangle=geometry.triangles[i];++stats.triangleTests;
            closest=triangleHit(origin,direction,mesh.positions[mesh.indices[triangle]],mesh.positions[mesh.indices[triangle+1]],mesh.positions[mesh.indices[triangle+2]],closest);
        }
    }
    return closest;
}
}
PlayerOcclusion::PlayerOcclusion(ResourceManager& resources,NavigationAgentProfileQuery profiles):resources_(resources),profiles_(std::move(profiles)),owner_(nextOwner.fetch_add(1)){}
void PlayerOcclusion::clear(){meshes_.clear();fades_.clear();opacities_.clear();stats_={};generation_=0;player_=camera_=0;}
const PlayerOcclusionGeometry* PlayerOcclusion::geometry(const std::string& id,const CollisionMeshQuery& query) {
    auto& entry=meshes_[id];entry.used=frame_;
    if(entry.queried!=frame_) {
        entry.queried=frame_;const auto source=query(id);
        if(source.mesh&&(!entry.pending||source.mesh!=entry.source.lock()||source.revision!=entry.revision)) {
            entry.source=source.mesh;entry.revision=source.revision;
            ResourceRequestOptions options;options.priority=ResourcePriority::Visible;options.retainLoader=false;options.cancelWhenUnused=true;
            entry.pending=resources_.requestDerived<PlayerOcclusionGeometry>("player-occlusion-"+std::to_string(owner_)+"-"+std::to_string(++job_),
                [mesh=source.mesh](std::stop_token stop) {
                    const auto geometry=build(mesh,stop);
                    const auto bytes=sizeof(PlayerOcclusionGeometry)+geometry->triangles.size()*sizeof(std::uint32_t)+geometry->nodes.size()*sizeof(PlayerOcclusionGeometry::Node);
                    return ResourceLoadResult<PlayerOcclusionGeometry>{geometry,bytes,mesh->fingerprint,{}};
                },std::move(options));
        }
        if(entry.pending)if(const auto ready=entry.pending.snapshot())entry.accepted=ready.shared();
    }
    return entry.accepted.get();
}
const PlayerOcclusionOpacities& PlayerOcclusion::update(const Scene& scene,std::span<const ExtractedModel> models,const Camera& camera,
    std::uint64_t generation,float seconds,const ModelBoundsQuery& boundsQuery,const CollisionMeshQuery& query,const PlayerOcclusionSettings& settings,const NavigationAgentProfileQuery& profiles) {
    if(!validPlayerOcclusionSettings(settings)){fades_.clear();opacities_.clear();stats_={};return opacities_;}
    const auto player=scene.find(scene.properties().playerCharacter);
    const auto world=player?scene.worldTransform(player->id):std::nullopt;
    if(!player||!scene.isVisible(player->id)||!player->controller||!world||!math::finite(camera.position)||!query||!boundsQuery){clear();return opacities_;}
    const auto traversal=resolveAgentTraversal(*player->controller,profiles?profiles:profiles_);
    if(!traversal){clear();return opacities_;}
    if(generation_!=generation||player_!=player->id||camera_!=scene.properties().activeCamera) {
        clear();generation_=generation;player_=player->id;camera_=scene.properties().activeCamera;
    }
    ++frame_;stats_={};
    std::erase_if(meshes_,[&](const auto& entry){return frame_-entry.second.used>180;});
    for(auto& [id,fade]:fades_)fade.obstructed=false;
    const auto ignored=scene.subtree(player->id);
    std::array<Vec3,3> targets,starts;
    std::array<double,3> lengths{};
    for(unsigned i=0;i<3;++i) {
        targets[i]=math::transformPoint(*world,{0,traversal->height*(.25f+.3f*i),0});
        const auto towardCamera=camera.position-targets[i];const auto distance=math::length(towardCamera);
        lengths[i]=std::min(double(settings.fadeStartDistance),distance);
        starts[i]=targets[i]+math::normalized(towardCamera,{0,0,0},0)*lengths[i];
    }
    for(const auto& model:models) {
        if(!model.visible||!model.fadeWhenOccluding||std::find(ignored.begin(),ignored.end(),model.id)!=ignored.end()||model.assetId.empty())continue;
        const auto inverse=math::inverse(model.world);if(!inverse)continue;
        Aabb bounds;if(const auto metadata=boundsQuery(model.assetId))bounds=*metadata;
        if(const auto cached=meshes_.find(model.assetId);cached!=meshes_.end()&&cached->second.accepted)
            if(!cached->second.accepted->nodes.empty())math::include(bounds,cached->second.accepted->nodes.front().bounds);
        if(!bounds.valid)continue;
        std::array<Vec3,3> origins,directions;bool candidate=false;
        for(unsigned i=0;i<3;++i) {
            origins[i]=math::transformPoint(*inverse,targets[i]);directions[i]=math::transformPoint(*inverse,starts[i])-origins[i];
            if(lengths[i]>1e-6&&math::finite(origins[i])&&math::finite(directions[i])&&intersects(bounds,origins[i],directions[i]))candidate=true;
        }
        if(!candidate)continue;
        ++stats_.boundsCandidates;const auto* mesh=geometry(model.assetId,query);if(!mesh)continue;
        double nearest=settings.fadeStartDistance;
        for(unsigned i=0;i<3;++i)if(lengths[i]>1e-6) {
            const auto maximum=std::min(1.,nearest/lengths[i]);
            const auto t=hit(*mesh,origins[i],directions[i],maximum,stats_);
            if(t<maximum)nearest=std::min(nearest,t*lengths[i]);
        }
        if(nearest<settings.fadeStartDistance) {
            const auto distance=std::clamp((nearest-settings.fullFadeDistance)/(settings.fadeStartDistance-settings.fullFadeDistance),0.,1.);
            const auto amount=distance*distance*(3-2*distance);
            const auto minimum=double(settings.minimumOpacity);
            auto& fade=fades_[model.id];fade.target=minimum+(1-minimum)*amount;fade.obstructed=true;
        }
    }
    const auto elapsed=std::isfinite(seconds)?std::max(0.,double(seconds)):0.;
    for(auto item=fades_.begin();item!=fades_.end();) {
        if(std::none_of(models.begin(),models.end(),[&](const auto& model){return model.id==item->first&&model.visible&&model.fadeWhenOccluding&&
            std::find(ignored.begin(),ignored.end(),model.id)==ignored.end();})) {
            opacities_.erase(item->first);item=fades_.erase(item);continue;
        }
        auto& fade=item->second;
        fade.opacity=std::max(fade.opacity,double(settings.minimumOpacity));fade.target=std::max(fade.target,double(settings.minimumOpacity));
        if(fade.obstructed){fade.clearSeconds=0;ease(fade.opacity,fade.target,elapsed,settings);}
        else {
            // Continue approaching the last obstruction target across brief
            // edge losses. Split a frame at release so timing is independent
            // of how the same elapsed interval is divided into frames.
            const auto remaining=std::max(0.,double(settings.restoreDelaySeconds)-fade.clearSeconds);
            const auto held=std::min(elapsed,remaining);
            if(remaining>0)ease(fade.opacity,fade.target,held,settings);
            fade.clearSeconds+=elapsed;
            if(fade.clearSeconds>=settings.restoreDelaySeconds)ease(fade.opacity,1,elapsed-held,settings);
        }
        if(fade.opacity>=1&&(!fade.obstructed||fade.target>=1)){opacities_.erase(item->first);item=fades_.erase(item);}
        else {opacities_.insert_or_assign(item->first,static_cast<float>(fade.opacity));++item;}
    }
    stats_.cachedMeshes=meshes_.size();
    for(const auto& [id,entry]:meshes_)stats_.pendingMeshes+=entry.pending&&!entry.pending.snapshot();
    return opacities_;
}
}
