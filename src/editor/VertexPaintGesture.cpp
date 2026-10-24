#include "VertexPaintGesture.h"
#include <crimson/renderer/core/Profiler.hpp>
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
VertexPaintCandidate footprintCandidate(const VertexPaintFootprintSample& sample) {
    return {sample.target,sample.vertex,sample.world,sample.pixel,sample.cell};
}
// Scatter the requested reduction of the sampled mask onto its contributing
// controls. Solve the bounded, piecewise-linear reduction instead of merely
// multiplying by the basis: that would barely erase a remote painted corner
// when the corner nearest the brush is already Base. Higher-basis controls
// change first; full opacity removes every contributing non-Base weight.
std::array<float,4> baseFootprintAmounts(const VertexPaintFootprintInfluence& footprint,const std::array<Vec4,4>& weights) {
    std::array<double,4> contribution{},slope{};std::array<float,4> amounts{};
    const auto maximum=*std::max_element(footprint.basis.begin(),footprint.basis.end());
    if(!(maximum>0))return amounts;
    double mass{},derivative{};
    for(unsigned i=0;i<footprint.cornerCount;++i) {
        contribution[i]=footprint.basis[i]*std::max(0.,double(weights[i][1])+weights[i][2]+weights[i][3]);
        slope[i]=footprint.basis[i]/maximum;mass+=contribution[i];derivative+=contribution[i]*slope[i];
    }
    if(!(mass>0&&derivative>0))return amounts;
    const auto requested=std::clamp(double(footprint.amount),0.,1.)*mass;
    double scale=requested/derivative;
    for(unsigned iteration=0;iteration<4;++iteration) {
        double removed{},remainingSlope{};
        for(unsigned i=0;i<footprint.cornerCount;++i) {
            if(scale*slope[i]>=1)removed+=contribution[i];
            else remainingSlope+=contribution[i]*slope[i];
        }
        if(!(remainingSlope>0))break;
        scale=std::max(scale,std::max(0.,requested-removed)/remainingSlope);
    }
    for(unsigned i=0;i<footprint.cornerCount;++i)if(contribution[i]>0)
        amounts[i]=footprint.amount>=1?1.f:float(std::clamp(scale*slope[i],0.,1.));
    return amounts;
}
}
std::array<Vec3,VertexPaintCircleSegments+1> vertexPaintBrushCircle(Vec3 center,Vec3 normal,float radius) {
    std::array<Vec3,VertexPaintCircleSegments+1> points;
    normal=math::normalized(normal);
    const auto tangent=math::normalized(math::cross(normal,std::abs(normal[1])<.9f?Vec3{0,1,0}:Vec3{1,0,0}));
    const auto bitangent=math::cross(normal,tangent);
    for(std::size_t i=0;i<VertexPaintCircleSegments;++i) {
        const auto angle=double(i)*6.283185307179586/VertexPaintCircleSegments;
        points[i]=center+(tangent*std::cos(angle)+bitangent*std::sin(angle))*radius;
    }
    points.back()=points.front();return points;
}
VertexPaintSeams buildVertexPaintSeams(std::span<const VertexPaintTarget> targets) {
    CRIMSON_PROFILE_ZONE("Vertex paint source seam preparation");
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
void VertexPaintBrushCache::clear(bool retainFootprint) {width_=height_=columns_=0;candidates_.clear();bins_.clear();visibility_.clear();resetStrokeWeights();if(!retainFootprint)footprint_.clear();}
void VertexPaintBrushCache::resetVisibility() {std::fill(visibility_.begin(),visibility_.end(),std::int8_t{-1});}
void VertexPaintBrushCache::resetStrokeWeights() {strokeWeights_.clear();}
bool VertexPaintBrushCache::prepare(std::span<const VertexPaintTarget> targets,const Mat4& vp,std::uint32_t width,std::uint32_t height) {
    CRIMSON_PROFILE_ZONE("Vertex paint candidate projection");
    clear(true);if(!width||!height||width>16384||height>16384||!math::finite(vp)){clear();return false;}
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
    std::vector<VertexPaintFootprintMesh> meshes;meshes.reserve(targets.size());
    for(const auto& target:targets)meshes.push_back({target.geometry,target.world});
    if(!footprint_.prepare(meshes,vp,width,height)){clear();return false;}
    visibility_.assign(candidates_.size(),-1);return true;
}
std::optional<VertexPaintCandidate> VertexPaintBrushCache::prepareVisibility(const VertexPaintDab& dab,const VertexPaintBrush& brush,const Visibility& visible) {
    CRIMSON_PROFILE_ZONE("Vertex paint candidate visibility preflight");
    if(!validBrush(brush)||!visible||!width_||!height_||!math::finite(dab.center)||!math::finite(dab.pixel)||!std::isfinite(dab.screenRadius)||dab.screenRadius<0)return {};
    const auto left=std::uint32_t(std::clamp(std::floor((dab.pixel[0]-dab.screenRadius)/BinSize),0.f,float(columns_-1)));
    const auto right=std::uint32_t(std::clamp(std::floor((dab.pixel[0]+dab.screenRadius)/BinSize),0.f,float(columns_-1)));
    const auto rows=(height_+BinSize-1)/BinSize;
    const auto top=std::uint32_t(std::clamp(std::floor((dab.pixel[1]-dab.screenRadius)/BinSize),0.f,float(rows-1)));
    const auto bottom=std::uint32_t(std::clamp(std::floor((dab.pixel[1]+dab.screenRadius)/BinSize),0.f,float(rows-1)));
    for(auto y=top;y<=bottom;++y)for(auto x=left;x<=right;++x)for(const auto candidate:bins_[std::size_t(y)*columns_+x]) {
        const auto& vertex=candidates_[candidate];if(visibility_[candidate]>=0||influence(vertex,dab,brush)<=0)continue;
        const auto answer=visible(vertex);if(!answer)return vertex;visibility_[candidate]=*answer?1:0;
    }
    if(brush.layer==0) {
        const auto footprint=footprint_.collect(dab.center,brush.radius,brush.strength,brush.hardness,
            [&](const auto& sample){return visible(footprintCandidate(sample));});
        if(footprint.missing)return footprintCandidate(*footprint.missing);
    }
    return {};
}
VertexPaintBatch VertexPaintBrushCache::apply(std::span<const VertexPaintDab> dabs,const VertexPaintBrush& brush,
    std::span<const std::shared_ptr<const MeshPaintPayload>> weights,const Visibility& visible) {
    CRIMSON_PROFILE_ZONE("Vertex paint brush apply");
    VertexPaintBatch result;if(!validBrush(brush)||!visible||dabs.size()>8||!width_||!height_)return result;
    struct Influence {std::uint32_t target{},vertex{};float amount{};};
    std::vector<std::vector<Influence>> affected(dabs.size());
    std::vector<VertexPaintFootprintResult> footprints(dabs.size());
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
            if(visibility_[candidate])affected[index].push_back({vertex.target,vertex.vertex,influence(vertex,dab,brush)});
        }
        if(brush.layer==0) {
            footprints[index]=footprint_.collect(dab.center,brush.radius,brush.strength,brush.hardness,
                [&](const auto& sample){return visible(footprintCandidate(sample));});
            if(footprints[index].missing){result.missing=footprintCandidate(*footprints[index].missing);return result;}
            for(const auto& footprint:footprints[index].influences) {
                if(footprint.target>=weights.size()||!weights[footprint.target])return {};
                for(unsigned corner=0;corner<footprint.cornerCount;++corner)if(footprint.vertices[corner]>=weights[footprint.target]->vertexCount)return {};
            }
        }
    }
    // Preserve every dab and its order, while looking up exact visibility once
    // per projected vertex across overlapping batches in this frozen view.
    // Resolve the complete batch before changing even fractional state. A
    // deferred batch must be safe to retry without applying its earlier dabs
    // twice. RGB8 publication must never become the next dab's baseline: soft
    // strokes otherwise stop at a quantization plateau instead of converging.
    strokeWeights_.resize(weights.size());
    std::vector<std::vector<std::uint32_t>> edited(weights.size());
    const auto readWeight=[&](std::uint32_t target,std::uint32_t vertex)->Vec4 {
        const auto entry=strokeWeights_[target].find(vertex);if(entry!=strokeWeights_[target].end())return entry->second;
        const auto rgb=weights[target]->rgb(vertex);
        return {float(255-rgb[0]-rgb[1]-rgb[2]),float(rgb[0]),float(rgb[1]),float(rgb[2])};
    };
    const auto currentWeight=[&](std::uint32_t target,std::uint32_t vertex)->Vec4& {
        auto [entry,inserted]=strokeWeights_[target].try_emplace(vertex);
        if(inserted) {
            const auto rgb=weights[target]->rgb(vertex);
            entry->second={float(255-rgb[0]-rgb[1]-rgb[2]),float(rgb[0]),float(rgb[1]),float(rgb[2])};
        }
        return entry->second;
    };
    for(std::size_t dab=0;dab<dabs.size();++dab) {
        for(const auto& footprint:footprints[dab].influences) {
            std::array<Vec4,4> current{};
            for(unsigned corner=0;corner<footprint.cornerCount;++corner)current[corner]=readWeight(footprint.target,footprint.vertices[corner]);
            const auto amounts=baseFootprintAmounts(footprint,current);
            for(unsigned corner=0;corner<footprint.cornerCount;++corner)if(amounts[corner]>0)
                affected[dab].push_back({footprint.target,footprint.vertices[corner],amounts[corner]});
        }
        auto& influences=affected[dab];
        std::sort(influences.begin(),influences.end(),[](const auto& a,const auto& b){return a.target!=b.target?a.target<b.target:a.vertex<b.vertex;});
        for(std::size_t i=0;i<influences.size();) {
            const auto vertex=influences[i++];float amount=vertex.amount;
            while(i<influences.size()&&influences[i].target==vertex.target&&influences[i].vertex==vertex.vertex)amount=std::max(amount,influences[i++].amount);
            auto& value=currentWeight(vertex.target,vertex.vertex);
            for(auto& component:value)component*=1-amount;
            value[brush.layer]+=255*amount;
            edited[vertex.target].push_back(vertex.vertex);
        }
    }
    for(std::size_t target=0;target<edited.size();++target)if(!edited[target].empty()) {
        auto& vertices=edited[target];std::sort(vertices.begin(),vertices.end());vertices.erase(std::unique(vertices.begin(),vertices.end()),vertices.end());
        VertexPaintDabChanges changes;changes.target=std::uint32_t(target);changes.vertices.reserve(vertices.size());
        for(const auto vertex:vertices) {
            const auto rgb=normalizedMeshPaintRgb(strokeWeights_[target].at(vertex));
            if(rgb!=weights[target]->rgb(vertex))changes.vertices.push_back({vertex,rgb});
        }
        if(!changes.vertices.empty())result.changes.push_back(std::move(changes));
    }
    return result;
}
bool VertexPaintGesture::begin(SceneDocument& document,std::vector<VertexPaintTarget> targets) {
    CRIMSON_PROFILE_ZONE("Vertex paint stroke begin");
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
    CRIMSON_PROFILE_ZONE("Vertex paint publish and scene update");
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
    CRIMSON_PROFILE_ZONE("Vertex paint history commit");
    if(!valid(document)){cancel(document);return false;}const bool okay=!transaction_||document.commit();reset();return okay;
}
void VertexPaintGesture::cancel(SceneDocument& document) {
    CRIMSON_PROFILE_ZONE("Vertex paint history cancel");
    if(owner_==&document&&generation_==document.documentGeneration()&&transaction_&&document.transactionId()==transaction_)document.cancel();reset();
}
void VertexPaintGesture::reset(){owner_=nullptr;generation_=revision_=transaction_=0;selection_={};targets_.clear();ownerWorlds_.clear();}
}
