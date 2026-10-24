#include "VertexPaintTool.h"
#include "crimson-renderer/Renderer.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace crimson {
struct VertexPaintTool::ProfileFrame {
    VertexPaintTool& tool;
    explicit ProfileFrame(VertexPaintTool& value):tool(value) {
        tool.profiling_=shared::profiling::enabled()&&shared::profiling::connected();
        tool.cpuRays_=tool.gpuVisibilityChecks_=tool.appliedDabs_=tool.changedVertices_=0;
    }
    ~ProfileFrame() {
        if(!tool.profiling_)return;
        shared::profiling::counter("Vertex paint active",tool.active()?1:0);
        shared::profiling::counter("Vertex paint pending GPU",tool.pending_?1:0);
        shared::profiling::counter("Vertex paint queued dabs",double(tool.points_.size()));
        shared::profiling::counter("Vertex paint projected candidates",double(tool.brushCache_.candidates().size()));
        shared::profiling::counter("Vertex paint candidate preparations",double(tool.candidatePreparationCount_));
        shared::profiling::counter("Vertex paint CPU rays",double(tool.cpuRays_));
        shared::profiling::counter("Vertex paint GPU visibility checks",double(tool.gpuVisibilityChecks_));
        shared::profiling::counter("Vertex paint applied dabs",double(tool.appliedDabs_));
        shared::profiling::counter("Vertex paint changed vertices",double(tool.changedVertices_));
    }
};
namespace {
constexpr std::uint32_t TileSide=64;
constexpr std::size_t MaximumSurfaceTiles=256,MaximumQueuedDabs=4096;
std::uint64_t tileKey(std::uint32_t x,std::uint32_t y){return std::uint64_t(y/TileSide)<<32|x/TileSide;}
bool onTriangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c,double precision=1e-6) {
    const auto ab=b-a,ac=c-a,ap=p-a,n=math::cross(ab,ac);
    const double nn=math::dot(n,n);if(nn<1e-20)return false;
    const double scale=std::max(math::length(ab),math::length(ac));
    const double epsilon=std::max(precision,scale*1e-5);
    const double plane=math::dot(ap,n);if(plane*plane>epsilon*epsilon*nn)return false;
    const double d00=math::dot(ab,ab),d01=math::dot(ab,ac),d11=math::dot(ac,ac),d20=math::dot(ap,ab),d21=math::dot(ap,ac);
    const double determinant=d00*d11-d01*d01;if(determinant<=1e-20)return false;
    const double v=(d11*d20-d01*d21)/determinant,w=(d00*d21-d01*d20)/determinant;
    return v>=-1e-4&&w>=-1e-4&&v+w<=1.0001;
}
}
void VertexPaintTool::setEnabled(bool enabled,SceneDocument& document,Renderer& renderer) {
    if(enabled_==enabled)return;cancel(document,renderer);enabled_=enabled;diagnostic_.clear();
}
void VertexPaintTool::clear(bool retainFootprint) {
    brushCache_.clear(retainFootprint);points_.clear();lastPoint_.reset();lastWorldPoint_.reset();pending_.reset();missingSurface_.reset();inverses_.clear();seams_.clear();surfaces_.clear();hoverTargets_.clear();surfaceSnapshot_={};cursorHit_.reset();cursorReady_=false;holdSurface_=false;used_=0;paletteMaterial_.clear();
}
void VertexPaintTool::cancel(SceneDocument& document,Renderer& renderer) {
    CRIMSON_PROFILE_ZONE("Vertex paint cancel");
    waitForRelease_=waitForRelease_||active();gesture_.cancel(document);if(pending_)renderer.cancelPicking();clear();
    shared::profiling::counter("Vertex paint active",0);shared::profiling::counter("Vertex paint pending GPU",0);shared::profiling::counter("Vertex paint queued dabs",0);
}
void VertexPaintTool::finish(SceneDocument& document,Renderer& renderer) {
    CRIMSON_PROFILE_ZONE("Vertex paint release");
    // No GPU request or queued dab owns the document after mouse release.
    // Unavailable coverage is a preparation state, never a deferred command.
    if(pending_)renderer.cancelPicking();pending_.reset();
    gesture_.finish(document);points_.clear();lastPoint_.reset();lastWorldPoint_.reset();missingSurface_.reset();
    brushCache_.resetVisibility();brushCache_.resetStrokeWeights();surfaces_.clear();holdSurface_=false;
    shared::profiling::counter("Vertex paint active",0);shared::profiling::counter("Vertex paint pending GPU",0);shared::profiling::counter("Vertex paint queued dabs",0);
}
void VertexPaintTool::finishForCommand(SceneDocument& document,Renderer& renderer) {
    if(!active())return;
    waitForRelease_=true;
    if(gesture_.valid(document))finish(document,renderer);else cancel(document,renderer);
}
void VertexPaintTool::prepareUi(SceneDocument& document,Renderer& renderer,std::uint64_t& serial,
    Vec2 pointer,bool hovered,bool held,bool commandRequested,bool cancelRequested,const std::optional<PickResult>& pick) {
    if(!active())return;
    CRIMSON_PROFILE_ZONE("Vertex paint UI boundary");
    if(cancelRequested){cancel(document,renderer);return;}
    if(commandRequested){finishForCommand(document,renderer);return;}
    if(!held) {
        // Process the final pointer segment against the frozen stroke view, then
        // release ownership before menus, Outliner or Inspector are submitted.
        update(document,renderer,pick,serial,projection_,width_,height_,pointer,hovered,true,false,false,false,false);
    }
}
void VertexPaintTool::drawBrushSettings() {
    // The stroke captures its brush at press. Input ownership finishes it before
    // another control is activated; displaying settings never disables the UI.
    ImGui::DragFloat("Radius (m)",&brush_.radius,.01f,.01f,1000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
    ImGui::SliderFloat("Strength",&brush_.strength,0,1,"%.2f");
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Moves the current material blend toward the selected material.");
    ImGui::SliderFloat("Hardness",&brush_.hardness,0,1,"%.2f");
    ImGui::Checkbox("Show Subdivisions",&showSubdivisions_);
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Overlay the selected mesh's paint control grid.");
}
std::span<const VertexPaintTarget> VertexPaintTool::targets() const {return active()?gesture_.targets():std::span<const VertexPaintTarget>{hoverTargets_};}
bool VertexPaintTool::eligible(const VertexPaintSurfaceHit& hit) const {
    return std::any_of(targets().begin(),targets().end(),[&](const auto& target){return hit.entity==target.entity&&hit.draw==target.draw&&hit.primitive==target.primitive&&(!target.resourceRevision||hit.resourceRevision==target.resourceRevision);});
}
std::optional<VertexPaintSurfaceHit> VertexPaintTool::hit(Vec2 pixel) {
    if(!surfaceSnapshot_.ready||!surfaceSnapshot_.raycast||!math::finite(pixel)||pixel[0]<0||pixel[1]<0||pixel[0]>=width_||pixel[1]>=height_)return {};
    const auto ray=math::rayFromScreen(pixel[0],pixel[1],projection_,{0,0,float(width_),float(height_)},ClipDepth::NegativeOneToOne);if(!ray)return {};
    if(profiling_)++cpuRays_;
    auto result=surfaceSnapshot_.raycast(*ray);if(!result||result->exact)return result;
    const auto sample=surface(pixel);if(!sample||!sample->valid)return result;
    const bool matching=result->entity==sample->entityId&&result->draw==sample->drawIndex&&result->primitive==sample->primitive;
    auto normal=matching&&math::finite(result->worldNormal)&&math::length(result->worldNormal)>1e-8?result->worldNormal:-ray->direction;
    if(surfaceSnapshot_.raycastDraw) {
        const Ray sampleRay{ray->origin,math::normalized(sample->worldPosition-ray->origin)};
        if(profiling_)++cpuRays_;
        if(const auto resolved=surfaceSnapshot_.raycastDraw(sampleRay,sample->entityId,sample->drawIndex,sample->primitive)) {
            const auto precision=std::max(1e-5,math::length(sample->worldPosition)*std::numeric_limits<float>::epsilon()*4);
            if(math::length(resolved->worldPosition-sample->worldPosition)<=precision&&math::finite(resolved->worldNormal)&&math::length(resolved->worldNormal)>1e-8)normal=resolved->worldNormal;
        }
    }
    return VertexPaintSurfaceHit{sample->entityId,sample->drawIndex,sample->primitive,sample->resourceRevision,sample->worldPosition,normal,true};
}
void VertexPaintTool::enqueue(Vec2 pixel,bool final) {
    CRIMSON_PROFILE_ZONE("Vertex paint pointer dab generation");
    if(!math::finite(pixel))return;
    const auto current=hit(pixel);const auto world=current&&current->exact?std::optional{current->worldPosition}:std::nullopt;
    if(!lastPoint_){points_.push_back(pixel);lastPoint_=pixel;lastWorldPoint_=world;return;}
    const Vec2 delta{pixel[0]-(*lastPoint_)[0],pixel[1]-(*lastPoint_)[1]};
    const auto pixels=std::hypot(delta[0],delta[1]);
    if(pixels<.25)return;
    const auto spacing=std::max(.0001,double(strokeBrush_.radius)*.15);
    const auto distance=world&&lastWorldPoint_?math::length(*world-*lastWorldPoint_):spacing;
    const auto desired=std::floor(distance/spacing);
    // A pointer event spans at most one raster sample per pixel. Subpixel
    // repetitions cannot improve the viewport's surface visibility and can
    // explode when a tiny brush crosses a large world-space depth change.
    const auto count=std::size_t(std::min({desired,std::ceil(double(pixels)),double(MaximumQueuedDabs)}));
    if(count) {
        const auto first=*lastPoint_;const auto divisor=desired>count?double(count):distance/spacing;
        for(std::size_t i=1;i<=count;++i){const auto t=float(i/divisor);points_.push_back({first[0]+delta[0]*t,first[1]+delta[1]*t});}
        lastPoint_=points_.back();const auto last=hit(*lastPoint_);lastWorldPoint_=last&&last->exact?std::optional{last->worldPosition}:std::nullopt;
    }
    if(final&&std::hypot(pixel[0]-(*lastPoint_)[0],pixel[1]-(*lastPoint_)[1])>.25){points_.push_back(pixel);lastPoint_=pixel;lastWorldPoint_=world;}
}
const PickSurfaceSample* VertexPaintTool::surface(Vec2 pixel) {
    if(!math::finite(pixel)||pixel[0]<0||pixel[1]<0||pixel[0]>=width_||pixel[1]>=height_)return nullptr;
    const auto x=std::uint32_t(pixel[0]),y=std::uint32_t(pixel[1]);const auto tile=surfaces_.find(tileKey(x,y));
    if(tile==surfaces_.end())return nullptr;tile->second.used=++used_;
    const auto& value=tile->second;if(x<value.x||y<value.y||x-value.x>=value.width||y-value.y>=value.height)return nullptr;
    return &value.samples[std::size_t(y-value.y)*value.width+x-value.x];
}
bool VertexPaintTool::request(SceneDocument& document,Renderer& renderer,Vec2 pixel,std::uint64_t& serial) {
    if(pending_||!math::finite(pixel)||pixel[0]<0||pixel[1]<0||pixel[0]>=width_||pixel[1]>=height_)return false;
    CRIMSON_PROFILE_ZONE("Vertex paint GPU surface request");
    constexpr std::uint32_t side=1024;
    const auto alignedStart=[](float pixel,std::uint32_t extent) {
        const auto last=extent>side?((extent-side+TileSide-1)/TileSide)*TileSide:0;
        return std::min(std::uint32_t(std::max(0.f,pixel-side*.5f))/TileSide*TileSide,last);
    };
    const auto x=alignedStart(pixel[0],width_),y=alignedStart(pixel[1],height_);
    const auto w=std::min(side,width_-x),h=std::min(side,height_-y);
    holdSurface_=true;
    PickRequest request{document.scene().generation(),document.scene().revision(),++serial,x,y,renderer.pickResourceSignature(PickMode::Surface),PickMode::Surface,w,h};
    if(!renderer.requestPick(request)){holdSurface_=false;if(!active()){surfaces_.clear();brushCache_.resetVisibility();}diagnostic_=renderer.pickingStatus();if(diagnostic_.empty())diagnostic_="Viewport surface is not ready.";return false;}
    pending_=request;pendingSince_=ImGui::GetTime();return true;
}
void VertexPaintTool::accept(const PickResult& result) {
    if(!pending_||result.serial!=pending_->serial||result.generation!=pending_->generation||result.revision!=pending_->revision||result.mode!=PickMode::Surface)return;
    CRIMSON_PROFILE_ZONE("Vertex paint GPU surface accept");
    const auto request=*pending_;pending_.reset();if(result.surfacePending)return;
    const auto count=std::size_t(request.width)*request.height;
    std::array<PickSurfaceSample,1> single;
    std::span<const PickSurfaceSample> samples=result.surfaceSamples;
    if(count==1&&samples.empty()){single[0]={result.entityId,result.resourceRevision,result.drawIndex,result.primitive,result.worldPosition,result.surfaceValid};samples=single;}
    if(samples.size()!=count) {diagnostic_="Viewport surface readback is incomplete.";return;}
    for(std::uint32_t y=0;y<request.height;y+=TileSide)for(std::uint32_t x=0;x<request.width;x+=TileSide) {
        const auto key=tileKey(request.x+x,request.y+y);
        if(!surfaces_.contains(key)&&surfaces_.size()>=MaximumSurfaceTiles) {
            const auto oldest=std::min_element(surfaces_.begin(),surfaces_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
            if(oldest!=surfaces_.end())surfaces_.erase(oldest);
        }
        auto& tile=surfaces_[key];tile.x=request.x+x;tile.y=request.y+y;tile.width=std::min(TileSide,request.width-x);tile.height=std::min(TileSide,request.height-y);tile.used=++used_;
        tile.samples.resize(std::size_t(tile.width)*tile.height);
        for(std::uint32_t row=0;row<tile.height;++row)std::copy_n(samples.begin()+std::size_t(y+row)*request.width+x,tile.width,tile.samples.begin()+std::size_t(row)*tile.width);
    }
}
std::optional<bool> VertexPaintTool::visible(const VertexPaintCandidate& candidate) {
    const auto source=targets();if(candidate.target>=source.size()||!surfaceSnapshot_.raycast)return false;
    const auto& target=source[candidate.target];
    const auto screenRay=math::rayFromScreen(candidate.pixel[0],candidate.pixel[1],projection_,{0,0,float(width_),float(height_)},ClipDepth::NegativeOneToOne);if(!screenRay)return false;
    // Aim at the exact vertex rather than a rounded raster pixel. This keeps
    // UV/material seam vertices and subpixel triangles paintable at any zoom.
    const Ray ray{screenRay->origin,math::normalized(candidate.world-screenRay->origin)};
    if(profiling_)++cpuRays_;
    const auto nearest=surfaceSnapshot_.raycast(ray);if(!nearest)return false;
    if(!nearest->exact)return gpuVisible(candidate);
    const auto distance=math::length(candidate.world-ray.origin),epsilon=std::max(1e-5,distance*1e-6);
    if(nearest->entity!=target.entity||math::length(nearest->worldPosition-candidate.world)>epsilon)return false;
    const auto sameDraw=[&](const VertexPaintTarget& mesh){return nearest->draw==mesh.draw&&nearest->primitive==mesh.primitive&&(!mesh.resourceRevision||nearest->resourceRevision==mesh.resourceRevision);};
    if(sameDraw(target))return true;
    if(candidate.cell!=~0u)return false;
    if(const auto seam=seams_[candidate.target].find(candidate.vertex);seam!=seams_[candidate.target].end())for(const auto alias:seam->second)if(sameDraw(source[alias.target]))return true;
    return false;
}
std::optional<bool> VertexPaintTool::gpuVisible(const VertexPaintCandidate& candidate) {
    if(profiling_)++gpuVisibilityChecks_;
    const auto source=targets();if(candidate.target>=source.size())return false;
    const auto& target=source[candidate.target];
    bool missing=false;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        const Vec2 pixel{std::floor(candidate.pixel[0])+x+.5f,std::floor(candidate.pixel[1])+y+.5f};
        if(pixel[0]<0||pixel[1]<0||pixel[0]>=width_||pixel[1]>=height_)continue;
        const auto sample=surface(pixel);if(!sample){missing=true;if(!missingSurface_)missingSurface_=pixel;continue;}
        if(!sample->valid||sample->entityId!=target.entity)continue;
        const auto incident=[&](std::uint32_t targetIndex,std::uint32_t vertex) {
            const auto& meshSource=source[targetIndex];const auto& mesh=*meshSource.geometry;
            if(sample->drawIndex!=meshSource.draw||sample->primitive!=meshSource.primitive||(meshSource.resourceRevision&&sample->resourceRevision!=meshSource.resourceRevision))return false;
            const auto local=math::transformPoint(inverses_[targetIndex],sample->worldPosition);
            const auto worldPrecision=std::max(1e-5,math::length(sample->worldPosition)*std::numeric_limits<float>::epsilon()*4);
            const auto localPrecision=worldPrecision*std::max({math::length(math::transformDirection(inverses_[targetIndex],{1,0,0})),math::length(math::transformDirection(inverses_[targetIndex],{0,1,0})),math::length(math::transformDirection(inverses_[targetIndex],{0,0,1}))});
            if(candidate.cell!=~0u) {
                if(targetIndex!=candidate.target||candidate.cell>=mesh.cells.size())return false;
                const auto& cell=mesh.cells[candidate.cell];
                for(unsigned triangle=0;triangle<cell.cornerCount-2u;++triangle) {
                    const auto a=cell.vertices[cell.triangleCorners[triangle*3]],b=cell.vertices[cell.triangleCorners[triangle*3+1]],c=cell.vertices[cell.triangleCorners[triangle*3+2]];
                    if(onTriangle(local,mesh.vertices[a].position,mesh.vertices[b].position,mesh.vertices[c].position,localPrecision))return true;
                }
                return false;
            }
            if(vertex>=mesh.vertexTriangles.size())return false;
            for(const auto triangle:mesh.vertexTriangles[vertex]) {
                const auto base=std::size_t(triangle)*3;
                if(onTriangle(local,mesh.vertices[mesh.indices[base]].position,mesh.vertices[mesh.indices[base+1]].position,mesh.vertices[mesh.indices[base+2]].position,localPrecision))return true;
            }
            return false;
        };
        if(incident(candidate.target,candidate.vertex))return true;
        if(candidate.cell!=~0u)continue;
        if(candidate.vertex<target.geometry->seamVertices.size())for(const auto alias:target.geometry->seamVertices[candidate.vertex])if(incident(candidate.target,alias))return true;
        if(const auto seam=seams_[candidate.target].find(candidate.vertex);seam!=seams_[candidate.target].end())for(const auto alias:seam->second)if(incident(alias.target,alias.vertex))return true;
    }
    return missing?std::nullopt:std::optional{false};
}
float VertexPaintTool::screenRadius(Vec3 center,Vec2 pixel) const {
    // This conservative bound selects spatial bins only. The visible outline
    // below projects an actual world-radius circle on the hit tangent plane.
    float radius=1;
    for(unsigned corner=0;corner<8;++corner) {
        const auto r=active()?strokeBrush_.radius:brush_.radius;
        const Vec3 p=center+Vec3{corner&1?r:-r,corner&2?r:-r,corner&4?r:-r};
        const auto projected=math::project(p,projection_,{0,0,float(width_),float(height_)},ClipDepth::NegativeOneToOne);
        if(!projected)return float(std::max(width_,height_))*2;
        radius=std::max(radius,float(std::hypot((*projected)[0]-pixel[0],(*projected)[1]-pixel[1])));
    }
    return radius+2;
}
void VertexPaintTool::update(SceneDocument& document,Renderer& renderer,const std::optional<PickResult>& pick,std::uint64_t& serial,
    const Mat4& vp,std::uint32_t width,std::uint32_t height,Vec2 pointer,bool hovered,bool focused,bool blocked,bool pressed,bool held,bool escape) {
    if(!enabled_&&!active())return;
    CRIMSON_PROFILE_ZONE("Vertex paint tool update");
    ProfileFrame profile{*this};
    if(!held)waitForRelease_=false;
    if(!enabled_){if(active())cancel(document,renderer);return;}
    if(!active()&&escape){cancel(document,renderer);return;}
    if(!active()&&(hoverSelection_.ids!=document.selection().ids||hoverSelection_.primary!=document.selection().primary)) {
        if(pending_)renderer.cancelPicking();clear();hoverSelection_=document.selection();
    }
    if(!active()&&(!hovered||blocked||document.transient())) {
        if(pending_)renderer.cancelPicking();pending_.reset();surfaces_.clear();brushCache_.resetVisibility();holdSurface_=false;cursorHit_.reset();cursorReady_=false;return;
    }
    const bool viewChanged=projection_!=vp||width_!=width||height_!=height||documentGeneration_!=document.documentGeneration();
    if(active()&&(!gesture_.valid(document)||viewChanged||!focused||blocked||escape||document.transient())){cancel(document,renderer);return;}
    auto currentSurface=[&]{CRIMSON_PROFILE_ZONE("Vertex paint surface query");return surfaceQuery_?surfaceQuery_(document):VertexPaintSurfaceSnapshot{};}();
    if(active()&&(!currentSurface.ready||currentSurface.signature!=surfaceSnapshot_.signature)){diagnostic_="The painting surface changed.";cancel(document,renderer);return;}
    if(!active()) {
        if(viewChanged){
            // Projection changes invalidate projected candidates. The footprint
            // hierarchy still depends only on selected geometry and its world
            // transforms; prepare() validates those independently of occluders.
            const bool sameDocument=documentGeneration_==document.documentGeneration();
            if(pending_)renderer.cancelPicking();clear(sameDocument);
        } else if(currentSurface.signature!=surfaceSnapshot_.signature||currentSurface.ready!=surfaceSnapshot_.ready) {
            // Animated or otherwise changing occluders invalidate visibility,
            // not the selected mesh's projection, seam map or footprint BVH.
            // Hover owns no stroke, so discard stale readbacks immediately.
            if(pending_)renderer.cancelPicking();pending_.reset();missingSurface_.reset();surfaces_.clear();
            brushCache_.resetVisibility();holdSurface_=false;cursorHit_.reset();cursorReady_=false;
        }
        surfaceSnapshot_=std::move(currentSurface);projection_=vp;width_=width;height_=height;documentGeneration_=document.documentGeneration();
        CRIMSON_PROFILE_ZONE("Vertex paint target query");
        std::vector<VertexPaintTarget> selected;
        if(query_)for(const auto entity:document.selection().ids){auto result=query_(document,entity);if(!result.diagnostic.empty())diagnostic_=result.diagnostic;for(auto& target:result.draws)selected.push_back(std::move(target));}
        const auto same=[&]{if(selected.size()!=hoverTargets_.size())return false;for(std::size_t i=0;i<selected.size();++i){const auto& a=selected[i];const auto& b=hoverTargets_[i];if(a.entity!=b.entity||a.draw!=b.draw||a.primitive!=b.primitive||a.materialId!=b.materialId||a.world!=b.world||a.resourceRevision!=b.resourceRevision||!a.geometry||!b.geometry||a.geometry->signature!=b.geometry->signature)return false;}return true;}();
        hoverTargets_=std::move(selected);
        if(!same) {
            paletteMaterial_.clear();inverses_.clear();seams_.clear();
            ++candidatePreparationCount_;
            if(!brushCache_.prepare(hoverTargets_,vp,width,height))hoverTargets_.clear();
            for(const auto& target:hoverTargets_){const auto inverse=math::inverse(target.world);if(!inverse){hoverTargets_.clear();break;}inverses_.push_back(*inverse);}
            seams_=buildVertexPaintSeams(hoverTargets_);
        }
    } else if(query_) {
        CRIMSON_PROFILE_ZONE("Vertex paint source validation");
        std::unordered_map<EntityId,VertexPaintTargets> current;
        for(const auto& target:gesture_.targets()) {
            if(!current.contains(target.entity))current.emplace(target.entity,query_(document,target.entity));
            const auto& draws=current.at(target.entity).draws;
            const auto found=std::find_if(draws.begin(),draws.end(),[&](const auto& draw){return draw.draw==target.draw&&draw.primitive==target.primitive;});
            if(found==draws.end()||!found->geometry||found->geometry->signature!=target.geometry->signature||found->resourceRevision!=target.resourceRevision||found->sourceNode!=target.sourceNode||found->world!=target.world) {diagnostic_="The painting source changed.";cancel(document,renderer);return;}
        }
    }
    if(pick)accept(*pick);
    if(pending_&&(pending_->resourceSignature!=renderer.pickResourceSignature(PickMode::Surface)||!renderer.pickingStatus().empty())) {
        renderer.cancelPicking();pending_.reset();holdSurface_=false;surfaces_.clear();brushCache_.resetVisibility();
        diagnostic_=renderer.pickingStatus().empty()?"The viewport surface changed.":renderer.pickingStatus();
    }
    if(pending_&&ImGui::GetTime()-pendingSince_>5){renderer.cancelPicking();pending_.reset();holdSurface_=false;surfaces_.clear();brushCache_.resetVisibility();diagnostic_="Viewport surface readback timed out.";}
    cursorHit_=hovered?hit(pointer):std::nullopt;cursorReady_=cursorHit_&&cursorHit_->exact&&eligible(*cursorHit_);
    if(cursorReady_) {
        for(const auto& target:targets())if(target.entity==cursorHit_->entity&&target.draw==cursorHit_->draw&&target.primitive==cursorHit_->primitive){paletteMaterial_=target.materialId;break;}
        if(!active())diagnostic_.clear();
    } else if(hovered&&cursorHit_&&!cursorHit_->exact&&!blocked) {
        diagnostic_="Preparing surface visibility...";request(document,renderer,pointer,serial);
    } else if(hovered&&!surfaceSnapshot_.ready)diagnostic_=surfaceSnapshot_.diagnostic.empty()?"Preparing paint surfaces...":surfaceSnapshot_.diagnostic;
    else if(hovered&&cursorHit_&&!hoverTargets_.empty()&&!active())diagnostic_="Paint a selected mesh's Blend material slot.";
    if(!active()&&cursorReady_&&!blocked) {
        CRIMSON_PROFILE_ZONE("Vertex paint hover visibility preflight");
        const auto p=math::project(cursorHit_->worldPosition,vp,{0,0,float(width),float(height)},ClipDepth::NegativeOneToOne);
        const Vec2 center=p?Vec2{(*p)[0],(*p)[1]}:pointer;
        const VertexPaintDab dab{cursorHit_->worldPosition,center,screenRadius(cursorHit_->worldPosition,center)};
        missingSurface_.reset();
        if(const auto missing=brushCache_.prepareVisibility(dab,brush_,[&](const auto& candidate){return visible(candidate);})) {
            request(document,renderer,missingSurface_.value_or(missing->pixel),serial);cursorReady_=false;diagnostic_="Preparing surface visibility...";
        }
    }
    if(!active()&&pressed&&hovered&&!blocked&&!waitForRelease_&&!document.transactionActive()&&!document.transient()) {
        if(hoverTargets_.empty()){if(diagnostic_.empty())diagnostic_="Select a rigid mesh with an assigned Blend material.";return;}
        if(!cursorReady_){if(diagnostic_.empty())diagnostic_="Paint the selected mesh's Blend material slot.";return;}
        brushCache_.resetStrokeWeights();
        if(!gesture_.begin(document,hoverTargets_)){diagnostic_="The painting selection changed.";return;}
        strokeBrush_=brush_;enqueue(pointer);
    }
    if(!active())return;
    if(held&&hovered)enqueue(pointer);
    if(!held&&hovered)enqueue(pointer,true);
    if(points_.size()>MaximumQueuedDabs){diagnostic_="Painting input exceeded the stroke budget.";cancel(document,renderer);return;}
    auto frameWeights=gesture_.weights();
    std::vector<std::unordered_map<std::uint32_t,MeshPaintRgb>> frameChanges(frameWeights.size());
    while(!points_.empty()) {
        CRIMSON_PROFILE_ZONE("Vertex paint ordered dab batch");
        std::vector<VertexPaintDab> dabs;std::size_t consumed=0;
        for(const auto pixel:points_) {
            const auto sample=hit(pixel);++consumed;
            if(sample&&sample->exact&&eligible(*sample)) {
                const auto projected=math::project(sample->worldPosition,projection_,{0,0,float(width_),float(height_)},ClipDepth::NegativeOneToOne);
                const Vec2 center=projected?Vec2{(*projected)[0],(*projected)[1]}:pixel;
                dabs.push_back({sample->worldPosition,center,screenRadius(sample->worldPosition,center)});
            } else if(sample&&!sample->exact) {--consumed;request(document,renderer,pixel,serial);break;}
            if(consumed==8)break;
        }
        if(!dabs.empty()) {
            missingSurface_.reset();auto batch=brushCache_.apply(dabs,strokeBrush_,frameWeights,[&](const auto& vertex){return visible(vertex);});
            if(batch.missing){request(document,renderer,missingSurface_.value_or(batch.missing->pixel),serial);diagnostic_="Preparing surface visibility...";break;}
            if(profiling_)appliedDabs_+=dabs.size();
            for(const auto& change:batch.changes) {
                frameWeights[change.target]=updateMeshPaintPayload(frameWeights[change.target],change.vertices);
                for(const auto& vertex:change.vertices)frameChanges[change.target][vertex.vertex]=vertex.rgb;
            }
        }
        if(!consumed)break;
        for(std::size_t i=0;i<consumed;++i)points_.pop_front();
    }
    std::vector<VertexPaintDabChanges> changes;
    {
    CRIMSON_PROFILE_ZONE("Vertex paint frame change consolidation");
    for(std::size_t target=0;target<frameChanges.size();++target)if(!frameChanges[target].empty()) {
        if(profiling_)changedVertices_+=frameChanges[target].size();
        VertexPaintDabChanges edited;edited.target=std::uint32_t(target);edited.vertices.reserve(frameChanges[target].size());
        for(const auto& [vertex,rgb]:frameChanges[target])edited.vertices.push_back({vertex,rgb});
        std::sort(edited.vertices.begin(),edited.vertices.end(),[](const auto& a,const auto& b){return a.vertex<b.vertex;});changes.push_back(std::move(edited));
    }
    }
    // COW batches retain dab ordering, but authoring publishes only once per
    // input frame. Long movement does not repeatedly clone the entire scene.
    if(!gesture_.publish(document,changes)){diagnostic_="The painting destination changed.";cancel(document,renderer);return;}
    if(!held)finish(document,renderer);
}
void VertexPaintTool::drawCursor(ImDrawList& draw,const ViewportRegion& viewport,Vec2,std::uint32_t width,std::uint32_t height) const {
    if(!enabled_||!viewport.hovered||!width||!height||!cursorHit_||!eligible(*cursorHit_))return;
    CRIMSON_PROFILE_ZONE("Vertex paint cursor projection");
    const auto& hit=*cursorHit_;const auto radius=active()?strokeBrush_.radius:brush_.radius;
    const auto color=ImGui::GetColorU32(cursorReady_?ImGuiCol_Text:ImGuiCol_TextDisabled);
    std::optional<ImVec2> previous;
    for(const auto p:vertexPaintBrushCircle(hit.worldPosition,hit.worldNormal,radius)) {
        const auto projected=math::project(p,projection_,{0,0,float(width),float(height)},ClipDepth::NegativeOneToOne);
        if(!projected||(*projected)[2]<0||(*projected)[2]>1){previous.reset();continue;}
        const ImVec2 screen{viewport.x+(*projected)[0]/width*viewport.width,viewport.y+(*projected)[1]/height*viewport.height};
        if(previous)draw.AddLine(*previous,screen,color,1.5f);previous=screen;
    }
    if(const auto p=math::project(hit.worldPosition,projection_,{0,0,float(width),float(height)},ClipDepth::NegativeOneToOne))
        draw.AddCircleFilled({viewport.x+(*p)[0]/width*viewport.width,viewport.y+(*p)[1]/height*viewport.height},2,color,8);
}
}
