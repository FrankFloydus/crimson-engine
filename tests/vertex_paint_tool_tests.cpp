#include "tools/VertexPaintTool.h"
#include "tools/ScenePanels.h"
#include "crimson-renderer/Renderer.h"
#include <imgui_internal.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace crimson {
struct VertexPaintToolTestAccess {
    static std::uint64_t candidatePreparations(const VertexPaintTool& tool){return tool.candidatePreparationCount_;}
};
}
namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-paint-tool-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-paint-tool-"))std::filesystem::remove_all(path,error);}
};
ModelPrimitive plane(bool separatedQuad=false) {
    ModelPrimitive p;p.vertices.resize(4);p.vertices[0].position={-1,-1,0};p.vertices[1].position={1,-1,0};p.vertices[2].position={1,1,0};p.vertices[3].position={-1,1,0};
    p.indices={0,1,2,0,2,3};p.sourceFaces={{{0,1,2,3},{0,1},4}};
    if(separatedQuad) {
        for(std::uint32_t i=0;i<4;++i){auto vertex=p.vertices[i];vertex.position[0]+=4;p.vertices.push_back(vertex);}
        p.indices.insert(p.indices.end(),{4,5,6,4,6,7});p.sourceFaces.push_back({{4,5,6,7},{2,3},4});
    }
    return p;
}
class Fixture {
public:
    explicit Fixture(std::uint32_t level=4,bool separatedQuad=false):document(project.root) {
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;
        unsigned char* pixels{};int w{},h{};io.Fonts->GetTexDataAsRGBA32(&pixels,&w,&h);io.Fonts->SetTexID(static_cast<ImTextureID>(1));ImGui::NewFrame();
        entity=document.createBox("Room-shaped paint target");document.select({entity},entity);
        const auto p=plane(separatedQuad);geometry=buildMeshPaintGeometry(p,level);empty=makeMeshPaintPayload(std::uint32_t(geometry->vertices.size()));
        const auto expanded=meshPaintRenderPrimitive(p,*geometry);bvh=build_shadow_ray_mesh(expanded);
        world=math::translation({3,.6666667f,22.75f})*math::scaling({2,1.3333334f,1});
        const auto center=math::transformPoint(world,{0,0,0});vp=math::perspective(1.2,800./600,.1,1000,ClipDepth::NegativeOneToOne)*math::lookAt(center+Vec3{0,0,4},center);
        tool.setTargetQuery([this](const SceneDocument&,EntityId id){VertexPaintTargets result;if(id!=entity)return result;VertexPaintTarget target;target.entity=entity;target.draw=2;target.primitive=7;target.sourcePrimitive=3;target.resourceRevision=71;target.sourceNode=4;target.materialId="paint-parent";target.materialSlot=1;target.world=world;target.geometry=geometry;target.weights=weights();result.draws.push_back(std::move(target));return result;});
        tool.setSurfaceQuery([this](const SceneDocument&){VertexPaintSurfaceMesh surface{bvh,world,*math::inverse(world),bvh->bounds,entity,2,7,71,false,true};auto surfaces=std::vector{surface};if(occluded){surface.world=math::translation(occluderOffset)*world;surface.inverse=*math::inverse(surface.world);surface.draw=3;surface.primitive=8;surfaces.push_back(surface);}auto snapshot=makeVertexPaintSurfaceSnapshot(std::move(surfaces),signature);snapshot.raycast=[this,raycast=std::move(snapshot.raycast)](const Ray& ray){++surfaceRays;return raycast(ray);};if(unready){snapshot.ready=false;snapshot.diagnostic="Paint surface preparation is pending.";}return snapshot;});
        tool.setEnabled(true,document,renderer);
    }
    ~Fixture(){tool.cancel(document,renderer);ImGui::EndFrame();ImGui::DestroyContext(context);}
    void tick(Vec2 pixel={400,300},bool pressed=false,bool held=false,bool escape=false){tool.update(document,renderer,{},serial,vp,800,600,pixel,true,true,false,pressed,held,escape);}
    std::shared_ptr<const MeshPaintPayload> weights() const {const auto record=document.scene().find(entity);return record&&record->meshPaint&&!record->meshPaint->draws.empty()?record->meshPaint->draws.front().weights:empty;}
    std::size_t painted() const {const auto payload=weights();std::size_t count=0;for(std::uint32_t i=0;i<payload->vertexCount;++i)if(payload->rgb(i)!=MeshPaintRgb{})++count;return count;}
    void seed(MeshPaintRgb rgb) {
        std::vector<MeshPaintChange> values;for(std::uint32_t vertex=0;vertex<geometry->vertices.size();++vertex)values.push_back({vertex,rgb});
        auto record=*document.scene().find(entity);record.meshPaint=SceneMeshPaint{geometry->level,MeshPaintRecipeVersion,{{2,3,geometry->level,geometry->sourceSignature,updateMeshPaintPayload(empty,values)}}};
        require(document.updateEntity(entity,record,"Existing authored paint"),"Could not seed the saved-paint fixture.");
    }
    Project project;SceneDocument document;Renderer renderer;VertexPaintTool tool;EntityId entity{};std::uint64_t serial{},signature{1234},surfaceRays{};bool occluded{},unready{};Vec3 occluderOffset{0,0,.1f};
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
void uiOwnershipAndEnabledControls() {
    Fixture fixture;const auto baseline=*fixture.document.scene().find(fixture.entity);const auto history=fixture.document.history().size();
    fixture.tick({400,300},true,true);
    require(fixture.tool.active()&&fixture.document.transactionActive(),"The UI test did not begin a published stroke.");
    fixture.tool.prepareUi(fixture.document,fixture.renderer,fixture.serial,{420,300},true,true,false,false);
    require(fixture.tool.active(),"Drawing UI prematurely ended a held stroke.");
    ImGui::Begin("Vertex Paint Tool");fixture.tool.drawBrushSettings();
    require(!(ImGui::GetCurrentContext()->LastItemData.ItemFlags&ImGuiItemFlags_Disabled),"Brush settings were disabled during a held stroke.");ImGui::End();
    // Use the same policy submitted by both real scene panels, and check actual
    // ImGui item flags rather than only the tool's active bit.
    for(const auto* name:{"Outliner","Inspector"}) {
        ImGui::Begin(name);
        ImGui::BeginDisabled(scenePanelEditBlocked(true,false,fixture.document.transactionActive(),false,fixture.tool.active()));
        ImGui::Button("Scene control");
        require(!(ImGui::GetCurrentContext()->LastItemData.ItemFlags&ImGuiItemFlags_Disabled),"A scene panel grayed its controls during painting.");
        ImGui::EndDisabled();ImGui::End();
    }
    const auto beforeRelease=fixture.painted();
    fixture.tool.prepareUi(fixture.document,fixture.renderer,fixture.serial,{455,300},true,false,false,false);
    require(!fixture.tool.active()&&!fixture.document.transactionActive(),"The pre-UI mouse release kept editor commands owned.");
    require(fixture.painted()>beforeRelease,"The pre-UI release dropped the final pointer segment.");
    require(fixture.document.history().size()==history+1,"The pre-UI release split a stroke into multiple history entries.");
    require(!scenePanelEditBlocked(true,false,fixture.document.transactionActive(),false,fixture.tool.active()),"Released painting left a disabled scene panel.");
    require(scenePanelEditBlocked(true,false,true,false,false)&&scenePanelEditBlocked(false,false,true,false,true),"The painting exception weakened unrelated transaction/modal ownership.");
    require(fixture.document.undo()&&sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"Undo after the pre-UI release did not restore exact paint.");
    fixture.tick({400,300},true,true);
    fixture.tool.prepareUi(fixture.document,fixture.renderer,fixture.serial,{410,300},true,true,true,false);
    require(!fixture.tool.active()&&!fixture.document.transactionActive(),"A conflicting command could not finish the paint stroke.");
    require(fixture.document.undo()&&sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"A command did not see the stroke as one complete undo entry.");
    fixture.tick({415,300},true,true);require(!fixture.tool.active(),"A command-finished stroke restarted before mouse release.");
    fixture.tick({415,300},false,false);fixture.tick({400,300},true,true);
    fixture.tool.prepareUi(fixture.document,fixture.renderer,fixture.serial,{410,300},true,true,false,true);
    require(!fixture.tool.active()&&!fixture.document.transactionActive()&&sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"Pre-UI Escape/focus cancellation retained disabled UI or paint changes.");
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
void baseInsideCoarseCell() {
    Fixture fixture(0,true);fixture.seed({35,180,20});fixture.tool.selectLayer(0);
    const auto center=math::transformPoint(fixture.world,{0,0,0});
    require(std::none_of(fixture.geometry->vertices.begin(),fixture.geometry->vertices.end(),[&](const auto& vertex){return math::length(math::transformPoint(fixture.world,vertex.position)-center)<=.5;}),"The coarse-cell regression accidentally placed a vertex inside the brush.");
    const auto baseline=*fixture.document.scene().find(fixture.entity);const auto saved=fixture.weights();const auto savedBytes=meshPaintBytes(*saved);const auto history=fixture.document.history().size();
    const auto centerOverlay=[&]{float value=0;for(const auto vertex:fixture.geometry->cells.front().vertices)for(const auto weight:fixture.weights()->rgb(vertex))value+=weight*.25f;return value;};
    const auto before=centerOverlay();
    fixture.tick({400,300},true,true);
    require(fixture.tool.active()&&fixture.document.transactionActive()&&centerOverlay()<before,"Base painting in a coarse cell did not erase the overlay directly under the brush.");
    require(meshPaintBytes(*saved)==savedBytes,"Base cleanup mutated existing immutable paint pages.");
    for(std::uint32_t vertex=0;vertex<fixture.geometry->vertices.size();++vertex)
        if(fixture.geometry->vertices[vertex].position[0]>2)require(fixture.weights()->rgb(vertex)==saved->rgb(vertex),"A Base brush changed an unrelated disconnected quad.");
    fixture.tool.prepareUi(fixture.document,fixture.renderer,fixture.serial,{400,300},true,false,false,false);
    require(!fixture.tool.active()&&!fixture.document.transactionActive(),"Coarse-cell Base cleanup retained ownership after release.");
    require(fixture.document.history().size()==history+1,"Coarse-cell Base cleanup was not one undo command.");
    const auto cleaned=*fixture.document.scene().find(fixture.entity);
    require(fixture.document.begin("Immediate command after Base cleanup"),"Base cleanup blocked an editor command after release.");fixture.document.cancel();
    require(fixture.document.undo()&&sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"Undo did not restore exact pre-existing paint after Base cleanup.");
    require(fixture.document.redo()&&sameEntity(*fixture.document.scene().find(fixture.entity),cleaned),"Redo did not restore the exact Base cleanup.");
    fixture.tick({400,300},true,true);require(fixture.tool.active()&&centerOverlay()<before,"Base could not start another cleanup stroke immediately.");
    fixture.tick({410,300},false,true,true);
    require(!fixture.tool.active()&&!fixture.document.transactionActive()&&sameEntity(*fixture.document.scene().find(fixture.entity),cleaned),"Canceling Base cleanup failed to preserve the preceding authored paint exactly.");
}
void baseVisibilityAndUnreadyRelease() {
    for(const bool unready:{false,true}) {
        Fixture fixture(0);fixture.seed({35,180,20});fixture.tool.selectLayer(0);fixture.unready=unready;fixture.occluded=!unready;++fixture.signature;
        const auto baseline=*fixture.document.scene().find(fixture.entity);const auto history=fixture.document.history().size();
        // An unfinished CPU snapshot has no valid brush hit yet. This fixture
        // deliberately exercises preparation ownership without a GPU backend.
        fixture.tick();fixture.tick({400,300},true,true);
        require(!fixture.tool.active()&&!fixture.document.transactionActive(),"Unverified Base cleanup acquired document ownership.");
        require(fixture.serial==0,"An unready CPU paint surface attempted a GPU request.");
        fixture.tick({400,300},false,false);
        require(!fixture.tool.active()&&!fixture.document.transactionActive()&&!fixture.tool.surfaceHeld(),"An unavailable Base surface retained a held stroke after release.");
        require(fixture.document.history().size()==history&&sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"Base cleanup crossed an occluder or changed paint before surface verification.");
        require(fixture.document.begin("Immediate command after unready Base click"),"An unready Base click blocked editor commands after release.");fixture.document.cancel();
    }
}
void drawIdentityOcclusion() {
    Fixture fixture;fixture.occluded=true;++fixture.signature;fixture.tick({400,300},true,true);fixture.tick({400,300},false,false);
    require(!fixture.tool.active()&&!fixture.document.transactionActive()&&fixture.painted()==0,"A nearer non-Blend slot of the same mesh failed to block painting.");
}
void changingOccludersRetainSelectedGeometry() {
    Fixture fixture(5);const auto baseline=*fixture.document.scene().find(fixture.entity);
    fixture.tick();const auto preparations=VertexPaintToolTestAccess::candidatePreparations(fixture.tool);
    require(preparations==1,"The first dense-mesh hover did not prepare its selected geometry once.");
    const auto stableStart=fixture.surfaceRays;fixture.tick();const auto stableRays=fixture.surfaceRays-stableStart;
    for(unsigned frame=0;frame<8;++frame) {
        ++fixture.signature;const auto firstRay=fixture.surfaceRays;fixture.tick();
        require(VertexPaintToolTestAccess::candidatePreparations(fixture.tool)==preparations,"Changing occluders rebuilt the unchanged selected-mesh projection and footprint.");
        require(fixture.surfaceRays-firstRay>stableRays,"Changing occluders retained stale cached candidate visibility.");
    }
    require(sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"Hover cache refresh changed authored paint.");
    // The center remains visible while a newly arrived non-Blend draw covers
    // only the right edge of the brush. Previously cached visible controls must
    // become hidden without rebuilding any selected geometry.
    fixture.occluded=true;fixture.occluderOffset={2.25f,0,.1f};++fixture.signature;
    fixture.tick({400,300},true,true);
    require(fixture.tool.active()&&fixture.painted()>0,"A partial occluder prevented painting the visible brush center.");
    const auto vertex=[&](float x) {
        const auto found=std::find_if(fixture.geometry->vertices.begin(),fixture.geometry->vertices.end(),[&](const auto& v){return math::length(v.position-Vec3{x,0,0})<1e-6;});
        require(found!=fixture.geometry->vertices.end(),"The dense visibility fixture has no expected brush control.");
        return std::uint32_t(found-fixture.geometry->vertices.begin());
    };
    require(fixture.weights()->rgb(vertex(.1875f))==MeshPaintRgb{}&&fixture.weights()->rgb(vertex(-.1875f))!=MeshPaintRgb{},"An occluder change painted previously cached controls through the new foreground draw.");
    require(VertexPaintToolTestAccess::candidatePreparations(fixture.tool)==preparations,"A visibility-only change rebuilt selected geometry when starting a stroke.");
    fixture.tool.prepareUi(fixture.document,fixture.renderer,fixture.serial,{400,300},true,false,false,false);
    require(!fixture.tool.active()&&!fixture.document.transactionActive(),"A retained geometry cache delayed stroke release.");
    require(fixture.document.undo()&&sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"A cached visibility stroke failed to undo exactly.");
    fixture.occluded=false;++fixture.signature;fixture.tick();
    const auto center=math::transformPoint(fixture.world,{0,0,0});
    fixture.vp=math::perspective(1.2,800./600,.1,1000,ClipDepth::NegativeOneToOne)*math::lookAt(center+Vec3{0,0,4.2f},center);
    fixture.tick();
    require(VertexPaintToolTestAccess::candidatePreparations(fixture.tool)==preparations+1,"A changed camera retained stale projected candidates.");
    fixture.world=math::translation({.01f,0,0})*fixture.world;fixture.tick();
    require(VertexPaintToolTestAccess::candidatePreparations(fixture.tool)==preparations+2,"A changed selected transform retained stale projected candidates.");
    fixture.geometry=buildMeshPaintGeometry(plane(),4);fixture.empty=makeMeshPaintPayload(std::uint32_t(fixture.geometry->vertices.size()));
    fixture.bvh=build_shadow_ray_mesh(meshPaintRenderPrimitive(plane(),*fixture.geometry));++fixture.signature;fixture.tick();
    require(VertexPaintToolTestAccess::candidatePreparations(fixture.tool)==preparations+3,"Changed selected topology retained stale projected candidates.");
    fixture.tick({400,300},true,true);require(fixture.tool.active(),"The refreshed target could not begin a new stroke.");
    fixture.vp=math::perspective(1.2,800./600,.1,1000,ClipDepth::NegativeOneToOne)*math::lookAt(center+Vec3{0,0,4.4f},center);
    fixture.tick({400,300},false,true);
    require(!fixture.tool.active()&&!fixture.document.transactionActive()&&sameEntity(*fixture.document.scene().find(fixture.entity),baseline),"A real view change failed to cancel a cached stroke and restore its exact paint.");
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
int main(){try{firstClickAndImmediateRelease();uiOwnershipAndEnabledControls();dragAndCancellation();baseInsideCoarseCell();baseVisibilityAndUnreadyRelease();drawIdentityOcclusion();changingOccludersRetainSelectedGeometry();actualCursorRadius();longMovementCost();std::cout<<"Vertex paint production first click, coarse-cell Base cleanup, saved paint, enabled UI, command boundaries, world cursor, changing occluders, geometry caching, drag, release and cancellation passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
