#include "VertexPaintGesture.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <tuple>
#include <unordered_map>

namespace crimson {
namespace {
bool validBrush(const VertexPaintBrush& brush) {
    return std::isfinite(brush.radius)&&brush.radius>0&&std::isfinite(brush.strength)&&brush.strength>=0&&brush.strength<=1&&
        std::isfinite(brush.hardness)&&brush.hardness>=0&&brush.hardness<=1&&brush.layer<4;
}
float influence(const VertexPaintCandidate& vertex,const VertexPaintDab& dab,const VertexPaintBrush& brush) {
    const auto distance=math::length(vertex.world-dab.center)/brush.radius;
    if(distance>1)return 0;
    if(distance<=brush.hardness || brush.hardness>=1)return brush.strength;
    const auto t=std::clamp(float((1-distance)/(1-brush.hardness)),0.f,1.f);
    return brush.strength*t*t*(3-2*t);
}
}
VertexPaintSeams buildVertexPaintSeams(std::span<const VertexPaintTarget> targets) {
    VertexPaintSeams result(targets.size());
    using EdgeKey=std::tuple<EntityId,std::int32_t,std::uint32_t,std::array<float,3>,std::array<float,3>>;
    struct EdgeUse {std::uint32_t target{},first{},second{};int direction{};};
    std::map<EdgeKey,std::vector<EdgeUse>> edges;
    for(std::size_t index=0;index<targets.size();++index) {
        const auto& target=targets[index];if(!target.geometry)continue;const auto& geometry=*target.geometry;
        for(const auto edge:geometry.boundaryEdges) {
            auto a=edge[0],b=edge[1];
            if(a>=geometry.vertices.size()||b>=geometry.vertices.size()||a>=geometry.vertexTriangles.size())continue;
            auto p=math::transformPoint(target.world,geometry.vertices[a].position).values;
            auto q=math::transformPoint(target.world,geometry.vertices[b].position).values;
            if(q<p){std::swap(p,q);std::swap(a,b);}if(p==q)continue;
            if(a>=geometry.vertexTriangles.size())continue;
            int direction=0;std::uint32_t uses=0;
            for(const auto triangle:geometry.vertexTriangles[a]) {
                const auto base=std::size_t(triangle)*3;if(base+2>=geometry.indices.size())continue;
                for(std::uint32_t corner=0;corner<3;++corner) {
                    const auto first=geometry.indices[base+corner],second=geometry.indices[base+(corner+1)%3];
                    if(first==a&&second==b){direction=1;++uses;}
                    else if(first==b&&second==a){direction=-1;++uses;}
                }
            }
            if(uses!=1)continue;
            // Unknown source-node identity cannot join unrelated draw calls.
            edges[{target.entity,target.sourceNode,target.sourceNode<0?target.draw:0,p,q}].push_back({std::uint32_t(index),a,b,direction});
        }
    }
    for(const auto& [key,uses]:edges)if(uses.size()==2) {
        static_cast<void>(key);const auto& a=uses[0];const auto& b=uses[1];
        if(a.direction==b.direction||targets[a.target].world!=targets[b.target].world)continue;
        if(a.target==b.target&&a.first==b.first&&a.second==b.second)continue;
        result[a.target][a.first].push_back({b.target,b.first});result[b.target][b.first].push_back({a.target,a.first});
        result[a.target][a.second].push_back({b.target,b.second});result[b.target][b.second].push_back({a.target,a.second});
    }
    return result;
}
void VertexPaintBrushCache::clear() {width_=height_=columns_=0;candidates_.clear();bins_.clear();visibility_.clear();}
bool VertexPaintBrushCache::prepare(std::span<const VertexPaintTarget> targets,const Mat4& vp,std::uint32_t width,std::uint32_t height) {
    clear();if(!width||!height||width>16384||height>16384||!math::finite(vp))return false;
    width_=width;height_=height;columns_=(width+BinSize-1)/BinSize;
    bins_.resize(std::size_t(columns_)*((height+BinSize-1)/BinSize));
    for(std::size_t target=0;target<targets.size();++target) {
        const auto& mesh=targets[target];if(!mesh.geometry||!math::finite(mesh.world)){clear();return false;}
        if(mesh.geometry->vertices.size()>MaximumMeshPaintVertices){clear();return false;}
        for(std::size_t vertex=0;vertex<mesh.geometry->vertices.size();++vertex) {
            const auto world=math::transformPoint(mesh.world,mesh.geometry->vertices[vertex].position);
            const auto p=math::project(world,vp,{0,0,float(width),float(height)},ClipDepth::NegativeOneToOne);
            if(!p||(*p)[2]<0||(*p)[2]>1||(*p)[0]<0||(*p)[1]<0||(*p)[0]>=width||(*p)[1]>=height)continue;
            const auto index=std::uint32_t(candidates_.size());
            candidates_.push_back({std::uint32_t(target),std::uint32_t(vertex),world,{(*p)[0],(*p)[1]}});
            const auto x=std::uint32_t((*p)[0])/BinSize,y=std::uint32_t((*p)[1])/BinSize;
            bins_[std::size_t(y)*columns_+x].push_back(index);
        }
    }
    visibility_.assign(candidates_.size(),-1);return true;
}
VertexPaintBatch VertexPaintBrushCache::apply(std::span<const VertexPaintDab> dabs,const VertexPaintBrush& brush,
    std::span<const std::shared_ptr<const MeshPaintPayload>> weights,const Visibility& visible) {
    VertexPaintBatch result;if(!validBrush(brush)||!visible||dabs.size()>8||!width_||!height_)return result;
    std::vector<std::vector<std::uint32_t>> affected(dabs.size());
    for(std::size_t index=0;index<dabs.size();++index) {
        const auto& dab=dabs[index];if(!math::finite(dab.center)||!math::finite(dab.pixel)||!std::isfinite(dab.screenRadius)||dab.screenRadius<0)return {};
        const auto left=std::uint32_t(std::clamp(std::floor((dab.pixel[0]-dab.screenRadius)/BinSize),0.f,float(columns_-1)));
        const auto right=std::uint32_t(std::clamp(std::floor((dab.pixel[0]+dab.screenRadius)/BinSize),0.f,float(columns_-1)));
        const auto rows=(height_+BinSize-1)/BinSize;
        const auto top=std::uint32_t(std::clamp(std::floor((dab.pixel[1]-dab.screenRadius)/BinSize),0.f,float(rows-1)));
        const auto bottom=std::uint32_t(std::clamp(std::floor((dab.pixel[1]+dab.screenRadius)/BinSize),0.f,float(rows-1)));
        for(auto y=top;y<=bottom;++y)for(auto x=left;x<=right;++x)for(const auto candidate:bins_[std::size_t(y)*columns_+x]) {
            const auto& vertex=candidates_[candidate];if(influence(vertex,dab,brush)<=0)continue;
            if(vertex.target>=weights.size()||!weights[vertex.target]||vertex.vertex>=weights[vertex.target]->vertexCount)return {};
            if(visibility_[candidate]<0) {
                const auto answer=visible(vertex);if(!answer){result.missing=vertex;return result;}
                visibility_[candidate]=*answer?1:0;
            }
            if(visibility_[candidate])affected[index].push_back(candidate);
        }
    }
    // Preserve every dab and its order, while looking up exact visibility once
    // per projected vertex across overlapping batches in this frozen view.
    std::vector<std::unordered_map<std::uint32_t,MeshPaintRgb>> edited(weights.size());
    for(std::size_t dab=0;dab<dabs.size();++dab)for(const auto candidate:affected[dab]) {
        const auto& vertex=candidates_[candidate];auto& changes=edited[vertex.target];
        const auto previous=changes.find(vertex.vertex);
        const auto value=previous==changes.end()?weights[vertex.target]->rgb(vertex.vertex):previous->second;
        const auto next=blendMeshPaintRgb(value,brush.layer,influence(vertex,dabs[dab],brush));
        if(next!=value)changes[vertex.vertex]=next;
    }
    for(std::size_t target=0;target<edited.size();++target)if(!edited[target].empty()) {
        VertexPaintDabChanges changes;changes.target=std::uint32_t(target);changes.vertices.reserve(edited[target].size());
        for(const auto& [vertex,rgb]:edited[target])if(rgb!=weights[target]->rgb(vertex))changes.vertices.push_back({vertex,rgb});
        std::sort(changes.vertices.begin(),changes.vertices.end(),[](const auto& a,const auto& b){return a.vertex<b.vertex;});
        if(!changes.vertices.empty())result.changes.push_back(std::move(changes));
    }
    return result;
}
bool VertexPaintGesture::begin(SceneDocument& document,std::vector<VertexPaintTarget> targets) {
    if(active()||document.transactionActive()||document.transient()||targets.empty())return false;
    std::vector<Mat4> ownerWorlds;ownerWorlds.reserve(targets.size());
    for(auto& target:targets) {
        const auto record=document.scene().find(target.entity);const auto world=document.scene().worldTransform(target.entity);
        if(!record||!record->model||!document.scene().isVisible(target.entity)||!world||!math::finite(target.world)||!math::inverse(target.world)||!target.geometry||target.geometry->vertices.empty()||target.geometry->vertices.size()>MaximumMeshPaintVertices)return false;
        ownerWorlds.push_back(*world);
        if(!target.weights)target.weights=makeMeshPaintPayload(std::uint32_t(target.geometry->vertices.size()));
        if(!validMeshPaintPayload(*target.weights)||target.weights->vertexCount!=target.geometry->vertices.size())return false;
    }
    owner_=&document;generation_=document.documentGeneration();revision_=document.scene().revision();selection_=document.selection();targets_=std::move(targets);ownerWorlds_=std::move(ownerWorlds);return true;
}
bool VertexPaintGesture::valid(const SceneDocument& document) const {
    if(owner_!=&document||generation_!=document.documentGeneration()||revision_!=document.scene().revision()||document.transactionId()!=transaction_||
        selection_.ids!=document.selection().ids||selection_.primary!=document.selection().primary)return false;
    for(std::size_t index=0;index<targets_.size();++index) {
        const auto& target=targets_[index];
        const auto world=document.scene().worldTransform(target.entity);
        if(!world||*world!=ownerWorlds_[index]||!document.scene().isVisible(target.entity))return false;
    }
    return true;
}
bool VertexPaintGesture::publish(SceneDocument& document,std::span<const VertexPaintDabChanges> changes) {
    if(!valid(document)){cancel(document);return false;}
    if(changes.empty())return true;
    auto next=weights();bool changed=false;
    for(const auto& change:changes) {
        if(change.target>=targets_.size()){cancel(document);return false;}
        next[change.target]=updateMeshPaintPayload(next[change.target],change.vertices);
        changed=changed||!sameMeshPaintPayload(next[change.target],targets_[change.target].weights);
    }
    if(!changed)return true;
    if(!transaction_) {if(!document.begin("Vertex paint material")){reset();return false;}transaction_=document.transactionId();}
    const bool updated=document.update([&](auto& records,auto&) {
        for(std::size_t index=0;index<targets_.size();++index) {
            const auto& target=targets_[index];if(next[index]==target.weights)continue;
            const auto record=std::find_if(records.begin(),records.end(),[&](const auto& r){return r.id==target.entity;});
            if(record==records.end())continue;
            if(!record->meshPaint)record->meshPaint.emplace();
            record->meshPaint->subdivisionLevel=target.geometry->level;
            auto& draws=record->meshPaint->draws;
            auto draw=std::find_if(draws.begin(),draws.end(),[&](const auto& d){return d.draw==target.draw&&d.primitive==target.sourcePrimitive;});
            if(draw==draws.end())draws.push_back({target.draw,target.sourcePrimitive,target.geometry->level,target.geometry->sourceSignature,next[index]});
            else {draw->level=target.geometry->level;draw->sourceSignature=target.geometry->sourceSignature;draw->weights=next[index];}
        }
    });
    if(!updated){cancel(document);return false;}
    for(std::size_t index=0;index<targets_.size();++index)targets_[index].weights=std::move(next[index]);
    revision_=document.scene().revision();return true;
}
std::vector<std::shared_ptr<const MeshPaintPayload>> VertexPaintGesture::weights() const {
    std::vector<std::shared_ptr<const MeshPaintPayload>> result;result.reserve(targets_.size());for(const auto& target:targets_)result.push_back(target.weights);return result;
}
bool VertexPaintGesture::finish(SceneDocument& document) {
    if(!valid(document)){cancel(document);return false;}const bool okay=!transaction_||document.commit();reset();return okay;
}
void VertexPaintGesture::cancel(SceneDocument& document) {
    if(owner_==&document&&generation_==document.documentGeneration()&&transaction_&&document.transactionId()==transaction_)document.cancel();reset();
}
void VertexPaintGesture::reset(){owner_=nullptr;generation_=revision_=transaction_=0;selection_={};targets_.clear();ownerWorlds_.clear();}
}
