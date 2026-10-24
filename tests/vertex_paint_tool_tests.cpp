#include "tools/VertexPaintTool.h"
#include "crimson-renderer/Renderer.h"
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-paint-tool-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-paint-tool-"))std::filesystem::remove_all(path,error);}
};
ModelPrimitive plane() {
    ModelPrimitive p;p.vertices.resize(4);p.vertices[0].position={-1,-1,0};p.vertices[1].position={1,-1,0};p.vertices[2].position={1,1,0};p.vertices[3].position={-1,1,0};
    p.indices={0,1,2,0,2,3};p.sourceFaces={{{0,1,2,3},{0,1},4}};return p;
}
class Fixture {
public:
    Fixture():document(project.root) {
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;
        unsigned char* pixels{};int w{},h{};io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);io.Fonts->SetTexID(static_cast<ImTextureID>(1));ImGui::NewFrame();
        entity=document.createBox("Room-shaped paint target");document.select({entity},entity);
        const auto p=plane();geometry=buildMeshPaintGeometry(p,4);empty=makeMeshPaintPayload(std::uint32_t(geometry->vertices.size()));
        const auto expanded=meshPaintRenderPrimitive(p,*geometry);bvh=build_shadow_ray_mesh(expanded);
        world=math::translation({3,.6666667f,22.75f})*math::scaling({2,1.3333334f,1});
        const auto center=math::transformPoint(world,{0,0,0});vp=math::perspective(1.2,800./600,.1,1000,ClipDepth::NegativeOneToOne)*math::lookAt(center+Vec3{0,0,4},center);
        tool.setTargetQuery([this](const SceneDocument&,EntityId id){VertexPaintTargets result;if(id!=entity)return result;VertexPaintTarget target;target.entity=entity;target.draw=2;target.primitive=7;target.sourcePrimitive=3;target.resourceRevision=71;target.sourceNode=4;target.materialId="paint-parent";target.materialSlot=1;target.world=world;target.geometry=geometry;target.weights=weights();result.draws.push_back(std::move(target));return result;});
        tool.setSurfaceQuery([this](const SceneDocument&){VertexPaintSurfaceMesh surface{bvh,world,*math::inverse(world),bvh->bounds,entity,2,7,71,false,true};auto surfaces=std::vector{surface};if(occluded){surface.world=math::translation({0,0,.1f})*world;surface.inverse=*math::inverse(surface.world);surface.draw=3;surface.primitive=8;surfaces.push_back(surface);}return makeVertexPaintSurfaceSnapshot(std::move(surfaces),signature);});
        tool.setEnabled(true,document,renderer);
    }
    ~Fixture(){tool.cancel(document,renderer);ImGui::EndFrame();ImGui::DestroyContext(context);}
    void tick(Vec2 pixel={400,300},bool pressed=false,bool held=false,bool escape=false){tool.update(document,renderer,{},serial,vp,800,600,pixel,true,true,false,pressed,held,escape);}
    std::shared_ptr<const MeshPaintPayload> weights() const {const auto record=document.scene().find(entity);return record&&record->meshPaint&&!record->meshPaint->draws.empty()?record->meshPaint->draws.front().weights:empty;}
    std::size_t painted() const {const auto payload=weights();std::size_t count=0;for(std::uint32_t i=0;i<payload->vertexCount;++i)if(payload->rgb(i)!=MeshPaintRgb{})++count;return count;}
    Project project;SceneDocument document;Renderer renderer;VertexPaintTool tool;EntityId entity{};std::uint64_t serial{},signature{1234};bool occluded{};
    Mat4 world,vp;std::shared_ptr<const MeshPaintGeometry> geometry;std::shared_ptr<const MeshPaintPayload> empty;std::shared_ptr<const ShadowRayMesh> bvh;ImGuiContext* context{};
};
void firstClickAndImmediateRelease() {
    Fixture fixture;const auto baseline=*fixture.document.scene().find(fixture.entity);const auto history=fixture.document.history().size();
    // No hover warm-up or GPU surface readback precedes this first short click.
    fixture.tick({400,300},true,true);require(fixture.tool.active()&&fixture.painted()>0,"The first CPU-visible paint click did not publish paint immediately.");
    const auto center=std::find_if(fixture.geometry->vertices.begin(),fixture.geometry->vertices.end(),[](const auto& vertex){return math::length(vertex.position)<1e-6;});
    require(center!=fixture.geometry->vertices.end()&&fixture.weights()->rgb(std::uint32_t(center-fixture.geometry->vertices.begin()))==blendMeshPaintRgb({},1,.5f),"Hover/press preflight double-applied the first dab.");
    fixture.tick({400,300},false,false);
    require(!fixture.tool.active()&&!fixture.document.transactionActive(),"Mouse release retained painting or document ownership.");
    require(fixture.document.history().size()==history+1,"The first short click was not one undo command.");
    require(fixture.document.begin("Immediate following command"),"A released stroke blocked the next editor command.");fixture.document.cancel();
    require(fixture.document.undo()&&sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"Immediate undo failed to restore the untouched mesh.");
    fixture.tick({400,300},true,true);fixture.tick({400,300},false,false);require(!fixture.tool.active()&&fixture.painted()>0,"A new stroke could not begin immediately after undo.");
}
void dragAndCancellation() {
    Fixture fixture;fixture.tick({365,300},true,true);const auto first=fixture.painted(),revision=fixture.document.scene().revision();fixture.tick({445,300},false,true);require(fixture.painted()>first,"A held stroke did not follow the world-space path.");
    require(fixture.document.scene().revision()<=revision+1,"Batched pointer movement repeatedly published the whole scene in one input frame.");
    fixture.tick({465,300},false,false);require(!fixture.tool.active()&&!fixture.document.transactionActive(),"A dragged stroke queued work after release.");
    const auto painted=*fixture.document.scene().find(fixture.entity);fixture.tick({400,330},true,true);fixture.tick({400,350},false,true,true);
    require(!fixture.tool.active()&&!fixture.document.transactionActive()&&sameEntity(*fixture.document.scene().find(fixture.entity),painted),"Escape failed to restore the exact previous paint pages.");
    fixture.tick({400,350},false,false);fixture.tick({400,330},true,true);++fixture.signature;fixture.tick({400,335},false,true);
    require(!fixture.tool.active()&&sameEntity(*fixture.document.scene().find(fixture.entity),painted),"An occluder/source change retained a stale stroke.");
}
void drawIdentityOcclusion() {
    Fixture fixture;fixture.occluded=true;++fixture.signature;fixture.tick({400,300},true,true);fixture.tick({400,300},false,false);
    require(!fixture.tool.active()&&!fixture.document.transactionActive()&&fixture.painted()==0,"A nearer non-Blend slot of the same mesh failed to block painting.");
}
void actualCursorRadius() {
    Fixture fixture;fixture.tick();
    ImDrawList draw(ImGui::GetDrawListSharedData());draw._ResetForNewFrame();draw.PushClipRectFullScreen();fixture.tool.drawCursor(draw,{0,0,800,600,true},{400,300},800,600);draw.PopClipRect();
    require(!draw.VtxBuffer.empty(),"Hover did not display the world brush before the first stroke.");
    float left=800,right=0,top=600,bottom=0;for(const auto& vertex:draw.VtxBuffer){left=std::min(left,vertex.pos.x);right=std::max(right,vertex.pos.x);top=std::min(top,vertex.pos.y);bottom=std::max(bottom,vertex.pos.y);}
    const auto center=math::transformPoint(fixture.world,{0,0,0});const auto edge=math::project(center+Vec3{.5f,0,0},fixture.vp,{0,0,800,600},ClipDepth::NegativeOneToOne);
    require(edge.has_value(),"The expected half-metre brush edge did not project.");const auto expected=(*edge)[0]-400;
    require(std::abs((right-left)*.5f-expected)<2&&std::abs((bottom-top)*.5f-expected)<2,"The production viewport cursor did not represent the authored 0.5m radius.");
}
void longMovementCost() {
    Fixture fixture;fixture.world=math::translation({3,.6666667f,22.75f})*math::scaling({1000000,1000000,1});
    const auto center=math::transformPoint(fixture.world,{0,0,0});fixture.vp=math::perspective(1.2,800./600,.1,10000000,ClipDepth::NegativeOneToOne)*math::lookAt(center+Vec3{0,0,4000000},center);
    fixture.tick({400,300},true,true);const auto revision=fixture.document.scene().revision();
    const auto started=std::chrono::steady_clock::now();fixture.tick({500,300},false,true);fixture.tick({530,300},false,false);
    const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
    require(!fixture.tool.active()&&!fixture.document.transactionActive(),"A long-distance tiny-brush event retained ownership after release.");
    require(fixture.document.scene().revision()<=revision+2,"Long movement published more than once per pointer frame.");
    std::cout<<"Long world-distance CPU drag + release: "<<elapsed<<" ms; scene publications "<<fixture.document.scene().revision()-revision<<".\n";
}
}
int main(){try{firstClickAndImmediateRelease();dragAndCancellation();drawIdentityOcclusion();actualCursorRadius();longMovementCost();std::cout<<"Vertex paint production first click, world cursor, occlusion, drag, release and cancellation passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
