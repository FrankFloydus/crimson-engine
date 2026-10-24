#include "VertexPaintTool.h"
#include "crimson-renderer/Renderer.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <limits>

namespace crimson {
namespace {
constexpr std::uint32_t TileSide=64,ReadbackSide=256;
constexpr std::size_t MaximumSurfaceTiles=256,MaximumQueuedDabs=8192;
std::uint64_t tileKey(std::uint32_t x,std::uint32_t y){return std::uint64_t(y/TileSide)<<32|x/TileSide;}
bool onTriangle(Vec3 p,Vec3 a,Vec3 b,Vec3 c) {
    const auto ab=b-a,ac=c-a,ap=p-a,n=math::cross(ab,ac);
    const double nn=math::dot(n,n);if(nn<1e-20)return false;
    const double scale=std::max(math::length(ab),math::length(ac));
    const double epsilon=std::max(1e-6,scale*1e-5);
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
void VertexPaintTool::drawControls(SceneDocument& document,Renderer& renderer) {
    if(!enabled_)return;
    bool open=true;ImGui::SetNextWindowSize({290,230},ImGuiCond_FirstUseEver);
    if(ImGui::Begin("Vertex Paint Tool",&open)) {
        ImGui::BeginDisabled(active());
        if(ImGui::BeginCombo("Mode","Material")){ImGui::Selectable("Material",true);ImGui::EndCombo();}
        constexpr const char* layers[]{"Base","R","G","B"};
        ImGui::TextUnformatted("Layer");
        for(std::uint32_t i=0;i<4;++i){if(i)ImGui::SameLine();if(ImGui::RadioButton(layers[i],brush_.layer==i))brush_.layer=i;}
        ImGui::DragFloat("Radius (m)",&brush_.radius,.01f,.01f,1000.f,"%.2f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SliderFloat("Strength",&brush_.strength,0,1,"%.2f");
        ImGui::SliderFloat("Hardness",&brush_.hardness,0,1,"%.2f");
        ImGui::EndDisabled();
        if(!diagnostic_.empty())ImGui::TextWrapped("%s",diagnostic_.c_str());
    }
    ImGui::End();if(!open)setEnabled(false,document,renderer);
}
void VertexPaintTool::clear() {
    brushCache_.clear();points_.clear();lastPoint_.reset();pending_.reset();missingSurface_.reset();inverses_.clear();seams_.clear();surfaces_.clear();released_=false;used_=0;cursorRadius_=12;
}
void VertexPaintTool::cancel(SceneDocument& document,Renderer& renderer) {
    waitForRelease_=waitForRelease_||active();gesture_.cancel(document);if(pending_)renderer.cancelPicking();clear();
}
void VertexPaintTool::enqueue(Vec2 pixel,bool final) {
    if(!math::finite(pixel))return;
    if(!lastPoint_){points_.push_back(pixel);lastPoint_=pixel;return;}
    const Vec2 delta{pixel[0]-(*lastPoint_)[0],pixel[1]-(*lastPoint_)[1]};const double distance=std::hypot(delta[0],delta[1]);
    constexpr double spacing=4;
    const auto count=std::uint32_t(std::floor(distance/spacing));
    if(count) {
        const auto first=*lastPoint_;
        for(std::uint32_t i=1;i<=count;++i){const auto t=float(i*spacing/distance);points_.push_back({first[0]+delta[0]*t,first[1]+delta[1]*t});}
        lastPoint_=points_.back();
    }
    if(final&&std::hypot(pixel[0]-(*lastPoint_)[0],pixel[1]-(*lastPoint_)[1])>.25){points_.push_back(pixel);lastPoint_=pixel;}
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
    const auto x=std::uint32_t(pixel[0])/ReadbackSide*ReadbackSide,y=std::uint32_t(pixel[1])/ReadbackSide*ReadbackSide;
    PickRequest request{document.scene().generation(),document.scene().revision(),++serial,x,y,0,PickMode::Surface,
        std::min(ReadbackSide,width_-x),std::min(ReadbackSide,height_-y)};
    if(!renderer.requestPick(request)){diagnostic_=renderer.pickingStatus();if(diagnostic_.empty())diagnostic_="Viewport surface is not ready.";return false;}
    pending_=request;pendingSince_=ImGui::GetTime();return true;
}
void VertexPaintTool::accept(const PickResult& result) {
    if(!pending_||result.serial!=pending_->serial||result.generation!=pending_->generation||result.revision!=pending_->revision||result.mode!=PickMode::Surface)return;
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
    const auto targets=gesture_.targets();if(candidate.target>=targets.size())return false;
    const auto& target=targets[candidate.target];
    // A few adjacent raster samples admit silhouette vertices while requiring
    // a sample on an incident triangle. A nearby front wall in the same mesh
    // cannot prove visibility of a separate back surface.
    bool missing=false;
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x) {
        const Vec2 pixel{std::floor(candidate.pixel[0])+x+.5f,std::floor(candidate.pixel[1])+y+.5f};
        if(pixel[0]<0||pixel[1]<0||pixel[0]>=width_||pixel[1]>=height_)continue;
        const auto sample=surface(pixel);if(!sample){missing=true;if(!missingSurface_)missingSurface_=pixel;continue;}
        if(!sample->valid||sample->entityId!=target.entity)continue;
        const auto incident=[&](std::uint32_t targetIndex,std::uint32_t vertex) {
            const auto& source=targets[targetIndex];const auto& mesh=*source.geometry;
            if(sample->drawIndex!=source.draw||sample->primitive!=source.primitive||(source.resourceRevision&&sample->resourceRevision!=source.resourceRevision))return false;
            const auto local=math::transformPoint(inverses_[targetIndex],sample->worldPosition);
            if(vertex>=mesh.vertexTriangles.size())return false;
            for(const auto triangle:mesh.vertexTriangles[vertex]) {
                const auto base=std::size_t(triangle)*3;
                if(onTriangle(local,mesh.vertices[mesh.indices[base]].position,mesh.vertices[mesh.indices[base+1]].position,mesh.vertices[mesh.indices[base+2]].position))return true;
            }
            return false;
        };
        if(incident(candidate.target,candidate.vertex))return true;
        if(candidate.vertex<target.geometry->seamVertices.size())for(const auto alias:target.geometry->seamVertices[candidate.vertex])if(incident(candidate.target,alias))return true;
        if(const auto seam=seams_[candidate.target].find(candidate.vertex);seam!=seams_[candidate.target].end())for(const auto alias:seam->second)if(incident(alias.target,alias.vertex))return true;
    }
    return missing?std::nullopt:std::optional{false};
}
float VertexPaintTool::screenRadius(Vec3 center,Vec2 pixel) const {
    float radius=1;
    for(unsigned corner=0;corner<8;++corner) {
        const auto r=strokeBrush_.radius;
        const Vec3 p=center+Vec3{corner&1?r:-r,corner&2?r:-r,corner&4?r:-r};
        const auto projected=math::project(p,projection_,{0,0,float(width_),float(height_)},ClipDepth::NegativeOneToOne);
        if(!projected)return float(std::max(width_,height_))*2;
        radius=std::max(radius,float(std::hypot((*projected)[0]-pixel[0],(*projected)[1]-pixel[1])));
    }
    return radius+2;
}
void VertexPaintTool::update(SceneDocument& document,Renderer& renderer,const std::optional<PickResult>& pick,std::uint64_t& serial,
    const Mat4& vp,std::uint32_t width,std::uint32_t height,Vec2 pointer,bool hovered,bool focused,bool blocked,bool pressed,bool held,bool escape) {
    if(!held)waitForRelease_=false;
    if(!enabled_){if(active())cancel(document,renderer);return;}
    if(active()&&released_&&pressed){cancel(document,renderer);diagnostic_="The pending stroke was canceled by a new press.";return;}
    if(active()&&(!gesture_.valid(document)||projection_!=vp||width_!=width||height_!=height||!focused||blocked||escape||document.transient())){cancel(document,renderer);return;}
    if(active()&&query_) {
        std::unordered_map<EntityId,VertexPaintTargets> current;
        for(const auto& target:gesture_.targets()) {
            if(!current.contains(target.entity))current.emplace(target.entity,query_(document,target.entity));
            const auto& draws=current.at(target.entity).draws;
            const auto found=std::find_if(draws.begin(),draws.end(),[&](const auto& draw){return draw.draw==target.draw&&draw.primitive==target.primitive;});
            if(found==draws.end()||!found->geometry||found->geometry->signature!=target.geometry->signature||found->resourceRevision!=target.resourceRevision||found->sourceNode!=target.sourceNode||found->world!=target.world) {
                diagnostic_="The painting source changed.";cancel(document,renderer);return;
            }
        }
    }
    if(!active()&&pressed&&hovered&&!blocked&&!waitForRelease_&&!document.transactionActive()&&!document.transient()) {
        clear();diagnostic_.clear();std::vector<VertexPaintTarget> targets;
        if(query_)for(const auto entity:document.selection().ids){auto query=query_(document,entity);if(!query.diagnostic.empty())diagnostic_=query.diagnostic;for(auto& target:query.draws)targets.push_back(std::move(target));}
        if(targets.empty()){if(diagnostic_.empty())diagnostic_="Select a rigid mesh with an assigned Blend material.";return;}
        if(!gesture_.begin(document,std::move(targets))){diagnostic_="The painting selection changed.";return;}
        projection_=vp;width_=width;height_=height;strokeBrush_=brush_;
        if(!brushCache_.prepare(gesture_.targets(),vp,width,height)){diagnostic_="Cannot prepare the selected paint geometry.";cancel(document,renderer);return;}
        for(const auto& target:gesture_.targets()) {
            const auto inverse=math::inverse(target.world);if(!inverse){cancel(document,renderer);return;}inverses_.push_back(*inverse);
        }
        seams_=buildVertexPaintSeams(gesture_.targets());
        enqueue(pointer);
    }
    if(!active())return;
    if(held&&hovered)enqueue(pointer);
    if(!held&&!released_){released_=true;if(hovered)enqueue(pointer,true);}
    if(points_.size()>MaximumQueuedDabs){diagnostic_="Painting input exceeded the pending surface budget.";cancel(document,renderer);return;}
    if(pick)accept(*pick);
    if(pending_) {
        if(ImGui::GetTime()-pendingSince_>5){diagnostic_="Viewport surface readback timed out.";cancel(document,renderer);}
        return;
    }
    std::vector<VertexPaintDab> dabs;std::size_t consumed=0;
    for(const auto pixel:points_) {
        const auto sample=surface(pixel);if(!sample){request(document,renderer,pixel,serial);return;}
        ++consumed;
        if(sample->valid&&std::any_of(gesture_.targets().begin(),gesture_.targets().end(),[&](const auto& target){return sample->entityId==target.entity&&sample->drawIndex==target.draw&&sample->primitive==target.primitive;})) {
            const auto projected=math::project(sample->worldPosition,projection_,{0,0,float(width_),float(height_)},ClipDepth::NegativeOneToOne);
            const Vec2 center=projected?Vec2{(*projected)[0],(*projected)[1]}:pixel;
            cursorRadius_=screenRadius(sample->worldPosition,center);dabs.push_back({sample->worldPosition,center,cursorRadius_});
        }
        if(dabs.size()==8||consumed==8)break;
    }
    if(!dabs.empty()) {
        missingSurface_.reset();const auto weights=gesture_.weights();auto batch=brushCache_.apply(dabs,strokeBrush_,weights,[&](const auto& vertex){return visible(vertex);});
        if(batch.missing){request(document,renderer,missingSurface_.value_or(batch.missing->pixel),serial);return;}
        if(!gesture_.publish(document,batch.changes)){diagnostic_="The painting destination changed.";cancel(document,renderer);return;}
    }
    for(std::size_t i=0;i<consumed;++i)points_.pop_front();
    if(released_&&points_.empty()){gesture_.finish(document);clear();}
}
void VertexPaintTool::drawCursor(ImDrawList& draw,const ViewportRegion& viewport,Vec2 pixel,std::uint32_t width,std::uint32_t height) const {
    if(!enabled_||!viewport.hovered||!width||!height)return;
    const ImVec2 center{viewport.x+pixel[0]/width*viewport.width,viewport.y+pixel[1]/height*viewport.height};
    const auto color=ImGui::GetColorU32(ImGuiCol_Text);
    draw.AddCircle(center,std::max(2.f,cursorRadius_*viewport.width/width),color,48,1.5f);
    draw.AddCircleFilled(center,2,color,8);
}
}
