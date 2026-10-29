#include "tools/InspectorTransform.h"
#include "tools/SceneDiagnostics.h"
#include "editor/SceneShortcuts.h"
#include "crimson-renderer/ModelTransform.h"
#include "scene/SceneDocument.h"
#include "editor/DecalAuthoring.h"
#include "editor/ViewportSelection.h"
#include "editor/ViewportSelectionUi.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
struct Project {
    fs::path root=fs::temp_directory_path()/
        ("crimson-inspector-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project() {
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-inspector-test-"))fs::remove_all(target,error);
    }
};

// All queued input targets a private CPU-only ImGui context. No OS window,
// desktop input, SDL, graphics backend, or user project is accessed.
class Inspector {
public:
    explicit Inspector(const fs::path& root,bool docked=false):document(root),docked_(docked) {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize=ImVec2(960,640);io.DeltaTime=1.0f/60;io.ConfigInputTrickleEventQueue=false;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        if(docked_)io.ConfigFlags|=ImGuiConfigFlags_DockingEnable;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        entity=document.createModel("assets/models/fixture/manifest.json","Scale fixture");
        require(entity&&document.save("Inspector fixture")&&document.load(document.assetId()),"Cannot create an isolated Inspector scene.");
        document.select({entity},entity);baseline=document.scene().find(entity)->local;
        tick();tick();
    }
    ~Inspector(){ImGui::DestroyContext(context_);}
    void tick(bool menuUndo=false,bool clippedAfterFocusLoss=false,bool collapseTransform=false) {
        ImGui::NewFrame();
        if(docked_)layout();
        // IniFilename=nullptr isolates persistence. NoSavedSettings would also
        // prevent DockBuilder's in-memory placement from applying on first Begin.
        constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove;
        if(!docked_){ImGui::SetNextWindowPos(ImVec2(20,20));ImGui::SetNextWindowSize(ImVec2(580,570));}
        ImGui::Begin("Inspector",nullptr,flags);
        if(menuUndo){
            require(finishInspectorTransform(document,state),"Opening Edit did not finish the owned numeric gesture.");
            require(document.undo(),"Menu Undo remained blocked by the numeric gesture.");
        }
        if(activation_>=0){
            auto& g=*ImGui::GetCurrentContext();g.NavActivateId=fieldId(static_cast<unsigned>(activation_));
            g.NavActivateFlags=ImGuiActivateFlags_PreferInput;activation_=-1;
        }
        const InspectorTransformTransactions transactions{
            [&](unsigned,const char* label){if(document.transactionActive())document.commit();return document.begin(label);},
            [&](unsigned){document.commit();}};
        auto* window=ImGui::GetCurrentWindow();const bool skipped=window->SkipItems;
        if(clippedAfterFocusLoss){
            // A field outside a dock's submitted region cannot report its own
            // deactivation. The gesture must still release document ownership.
            ImGui::ClearActiveID();window->SkipItems=true;
        }
        if(collapseTransform)ImGui::SetNextItemOpen(false,ImGuiCond_Always);
        drawInspectorTransform(document,state,*document.scene().find(entity),transactions,decalSurfaceFrame);
        window->SkipItems=skipped;
        for(unsigned property=0;property<3;++property){
            const auto id=axesTableId(property);const auto* table=ImGui::GetCurrentContext()->Tables.GetByKey(id);
            if(!table)continue;
            columnCounts[property]=static_cast<unsigned>(table->ColumnsCount);
            const auto capability=document.scene().find(entity)->decal;
            const bool decal=capability&&capability->projection==SceneDecalProjection::Surface;
            const unsigned columns=decal&&property>0?(property==1?1u:2u):3u;
            for(unsigned column=0;column<columns;++column){const unsigned axis=decal&&property==1?2u:column;centers[property*3+axis]={
                (table->Columns[column].WorkMinX+ImGui::CalcTextSize("X").x+ImGui::GetStyle().ItemInnerSpacing.x+table->Columns[column].WorkMaxX)*.5f,
                table->OuterRect.Min.y+table->RowCellPaddingY+ImGui::GetFrameHeight()*.5f};
            }
        }
        ImGui::End();
        if(viewportSelectionEnabled){
            ImGui::SetNextWindowPos({650,300});ImGui::SetNextWindowSize({280,300});
            ImGui::Begin("Selection viewport",nullptr,flags);
            const auto origin=ImGui::GetCursorScreenPos();const ImVec2 size{240,250};
            ImGui::Image(ImTextureRef(static_cast<ImTextureID>(1)),size);
            viewportCenter={origin.x+size.x*.5f,origin.y+size.y*.5f};
            const auto interaction=viewportImageInteraction([&]{return finishInspectorTransform(document,state);});
            if(interaction.hovered && !interaction.textInputBlocked && !document.transactionActive() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
                require(selectViewportHit(document,viewportHit,ImGui::GetIO().KeyCtrl,ImGui::GetIO().KeyShift),
                    "A viewport click was blocked after releasing the Inspector edit.");
                ++viewportSelections;
            }
            ImGui::End();
        }
        if(docked_){
            // These panels follow Inspector in SceneEditor. They must not alter
            // the scene-panel focus used by the actual production shortcut gate.
            for(const auto* name:{"Outliner","Viewport","Asset Browser"}){
                ImGui::Begin(name,nullptr,ImGuiWindowFlags_NoFocusOnAppearing);
                ImGui::TextUnformatted(name);ImGui::End();
            }
        }
        const auto shortcuts=readSceneShortcuts({.transactionActive=document.transactionActive()});
        if(shortcuts.undo){sceneUndo=true;document.undo();}
        if(shortcuts.redo){sceneRedo=true;document.redo();}
        // EditorSession submits asynchronous renderer/resource diagnostics AFTER
        // SceneEditor and its shortcut phase. Zero scale first creates this panel
        // while the numeric gesture is still active, before the next Ctrl+Z.
        if(diagnosticsEnabled){
            Mat4 world,normal;float handedness{};
            const auto status=prepareModelMatrices(matrix(),Mat4{},world,normal,handedness);
            if(status==ModelTransformStatus::Collapsed){
                if(!docked_){ImGui::SetNextWindowPos(ImVec2(650,20));ImGui::SetNextWindowSize(ImVec2(280,260));}
                drawSceneDiagnostics("","","Hidden 1 draw with a collapsed transform (for example, zero scale).");
                ++diagnosticFrames;
            }
        }
        const auto* nav=ImGui::GetCurrentContext()->NavWindow;
        focus=nav ? nav->RootWindow->Name : "";
        ImGui::Render();
    }
    void activate(unsigned field){activation_=static_cast<int>(field);tick();require(state.editing&&document.transactionActive(),"The production numeric field did not start a gesture.");}
    void zero(){ImGui::GetIO().AddInputCharactersUTF8("0");tick();}
    void type(const char* text){ImGui::GetIO().AddInputCharactersUTF8(text);tick();}
    void replaceText(const char* text){
        key(ImGuiMod_Ctrl,true);key(ImGuiKey_A,true);tick();
        key(ImGuiKey_A,false);key(ImGuiMod_Ctrl,false);tick();type(text);
    }
    void setTransform(const Mat4& matrix){
        auto record=*document.scene().find(entity);record.local=matrix;
        require(document.updateEntity(entity,record)&&document.save()&&document.load(document.assetId()),"Cannot prepare the isolated transform fixture.");
        document.select({entity},entity);baseline=matrix;tick();tick();
    }
    void setDecalTransform(const Mat4& world,const std::optional<Mat4>& parent={}){
        EntityId parentId{};Mat4 local=world;
        if(parent){parentId=document.createEmpty("Decal parent");
            require(parentId!=0,"Could not create the decal parent.");auto group=*document.scene().find(parentId);group.local=*parent;
            require(document.updateEntity(parentId,group),"Could not place the decal parent.");local=(*math::inverse(*parent))*world;}
        SceneDecal value;value.asset.id="68011f04-64c3-4596-b6dc-1f12047863ba";
        value.seed.receiver=entity;value.seed.localNormal={1,0,0};
        entity=document.createDecal(value,"New Decal 2",local);require(entity!=0,"Could not create the decal fixture.");
        if(parentId){auto record=*document.scene().find(entity);record.parent=parentId;require(document.updateEntity(entity,record),"Could not parent the decal fixture.");}
        require(document.save()&&document.load(document.assetId()),"Could not save and reopen the decal fixture.");
        decalSurfaceFrame=decals::surfacePlacementFrame(math::transformPoint(world,{}),{1,0,0});
        document.select({entity},entity);baseline=document.scene().find(entity)->local;tick();tick();
    }
    void enter(){key(ImGuiKey_Enter,true);tick();key(ImGuiKey_Enter,false);tick();tick();}
    void key(ImGuiKey key,bool down){ImGui::GetIO().AddKeyEvent(key,down);}
    void undo(){key(ImGuiMod_Ctrl,true);key(ImGuiKey_Z,true);tick();key(ImGuiKey_Z,false);key(ImGuiMod_Ctrl,false);tick();tick();}
    void redo(){key(ImGuiMod_Ctrl,true);key(ImGuiKey_Y,true);tick();key(ImGuiKey_Y,false);key(ImGuiMod_Ctrl,false);tick();tick();}
    void mouse(Vec2 point,bool down){auto& io=ImGui::GetIO();io.AddMousePosEvent(point[0],point[1]);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);tick();}
    Mat4 matrix()const{return document.scene().find(entity)->local;}
    Mat4 worldMatrix()const{return *document.scene().worldTransform(entity);}
    SceneDocument document;
    InspectorTransformState state;
    EntityId entity{};
    Mat4 baseline{};
    std::optional<Mat4> decalSurfaceFrame;
    std::array<Vec2,9> centers{};
    std::array<unsigned,3> columnCounts{};
    bool sceneUndo{},sceneRedo{},diagnosticsEnabled{};
    bool viewportSelectionEnabled{};
    EntityId viewportHit{};
    Vec2 viewportCenter{};
    unsigned viewportSelections{};
    unsigned diagnosticFrames{};
    std::string focus;
private:
    void layout(){
        const auto dock=ImHashStr("Inspector regression dock");
        const auto* main=ImGui::GetMainViewport();
        if(!ImGui::DockBuilderGetNode(dock)){
            ImGui::DockBuilderAddNode(dock,ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(dock,main->WorkPos);ImGui::DockBuilderSetNodeSize(dock,main->WorkSize);
            ImGuiID center=dock,browser{},inspector{},outliner{},diagnostics{};
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.15f,&browser,&center);
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Right,.65f,&inspector,&center);
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Up,.25f,&outliner,&center);
            ImGui::DockBuilderSplitNode(center,ImGuiDir_Down,.3f,&diagnostics,&center);
            ImGui::DockBuilderDockWindow("Inspector",inspector);ImGui::DockBuilderDockWindow("Outliner",outliner);
            ImGui::DockBuilderDockWindow("Viewport",center);ImGui::DockBuilderDockWindow("Asset Browser",browser);
            ImGui::DockBuilderDockWindow("Scene diagnostics",diagnostics);ImGui::DockBuilderFinish(dock);
        }
        ImGui::DockSpaceOverViewport(dock,main,ImGuiDockNodeFlags_AutoHideTabBar);
    }
    ImGuiID axesTableId(unsigned property)const{
        auto* window=ImGui::GetCurrentWindow();
        const auto outer=window->GetID("Transform properties");const int index=static_cast<int>(property);
        const auto row=ImHashData(&index,sizeof(index),outer);return ImHashStr("Axes",0,row);
    }
    ImGuiID fieldId(unsigned field)const{
        const auto table=axesTableId(field/3);const int axis=static_cast<int>(field%3);
        const auto row=ImHashData(&axis,sizeof(axis),table);return ImHashStr("##value",0,row);
    }
    ImGuiContext* context_{};
    int activation_{-1};
    bool docked_{};
};
void zeroTextAndUndo(){
    for(unsigned axis=0;axis<3;++axis){
        Project project;Inspector ui(project.root);ui.activate(6+axis);ui.zero();
        for(unsigned row=0;row<3;++row)require(ui.matrix()[axis*4+row]==0,"Typing zero did not collapse the selected scale column.");
        require(ui.document.transactionActive()&&ui.document.history().size()==0,"Typing committed the gesture before validation.");
        ui.enter();require(!ui.document.transactionActive()&&ui.document.history().size()==1,"Validated zero scale did not create exactly one completed command.");
        const auto collapsed=ui.matrix();ui.undo();require(ui.sceneUndo&&ui.matrix()==ui.baseline&&!ui.document.dirty(),"Ctrl+Z did not recover the exact pre-zero matrix and clean state.");
        ui.redo();require(ui.sceneRedo&&ui.matrix()==collapsed&&ui.document.dirty(),"Ctrl+Y did not restore the committed zero scale.");
    }
}
void numericTextOwnsUndo(){
    Project project;Inspector ui(project.root);ui.activate(6);ui.zero();
    ui.key(ImGuiMod_Ctrl,true);ui.key(ImGuiKey_Z,true);ui.tick();
    require(!ui.sceneUndo&&ui.document.history().size()==0&&ui.document.transactionActive(),"Active numeric text incorrectly routed Ctrl+Z to scene history.");
    require(std::string(ImGui::GetCurrentContext()->InputTextState.GetText()).empty(),"The first numeric text Undo did not reverse the inserted digit.");
    ui.key(ImGuiKey_Z,false);ui.tick();ui.key(ImGuiKey_Z,true);ui.tick();
    require(!ui.sceneUndo&&std::string(ImGui::GetCurrentContext()->InputTextState.GetText())=="1.000", "Numeric text Undo did not restore the replaced initial text.");
    require(ui.matrix()==ui.baseline,"Numeric text Undo did not restore its earlier value.");
    ui.key(ImGuiKey_Z,false);ui.key(ImGuiMod_Ctrl,false);ui.tick();ui.enter();
    require(ui.document.history().size()==0&&!ui.document.dirty(),"Undoing the numeric text left a spurious scene edit.");
}
void menuFinishesAndDoesNotReapply(){
    Project project;Inspector ui(project.root);ui.activate(7);ui.zero();ui.tick(true);
    require(!ui.document.transactionActive()&&ui.matrix()==ui.baseline&&ui.document.history().size()==1,"Menu Undo did not finish and reverse a zero-value numeric edit.");
    ui.tick();ui.tick();require(ui.matrix()==ui.baseline&&!ui.document.transactionActive(),"A stale numeric text buffer reapplied zero after menu Undo.");
}
void missedDeactivationReleasesOwnership(){
    Project project;Inspector ui(project.root);ui.activate(8);ui.zero();ui.tick(false,true);
    require(!ui.document.transactionActive()&&!ui.state.editing&&ui.document.history().size()==1,"A clipped field with no deactivation event stranded the scene transaction.");
    require(ui.document.undo()&&ui.matrix()==ui.baseline,"Undo remained blocked after a clipped numeric field lost focus.");
}
void collapsedTransformReleasesOwnership(){
    Project project;Inspector ui(project.root);ui.activate(8);ui.zero();ui.tick(false,false,true);
    require(!ui.document.transactionActive()&&!ui.state.editing&&ui.document.history().size()==1,
        "Collapsing Transform stranded its active numeric transaction.");
    ui.undo();require(ui.sceneUndo&&ui.matrix()==ui.baseline,"A collapsed Transform section blocked scene Undo.");
}
void viewportClickReleasesInspectorOwnership(){
    Project project;Inspector ui(project.root);
    const auto other=ui.document.createBox("Other mesh");
    ui.document.select({ui.entity},ui.entity);
    ui.viewportSelectionEnabled=true;ui.tick();ui.tick();
    const auto history=ui.document.history().size();
    ui.activate(0);ui.replaceText("3");ui.tick();
    require(ui.document.transactionActive() && ImGui::GetIO().WantTextInput,
        "The viewport regression did not retain an active numeric text edit.");
    ui.viewportHit=other;ui.mouse(ui.viewportCenter,false);ui.mouse(ui.viewportCenter,true);
    require(ui.viewportSelections==1 && ui.document.selection().primary==other &&
        !ui.document.transactionActive() && !ui.state.editing,
        "The first viewport click after an Inspector edit left selection locked or needed a second click.");
    require(ui.document.history().size()==history+1 && std::abs(ui.matrix().at(0,3)-3.f)<1e-6f,
        "Picking another entity discarded the completed Inspector value or created duplicate history.");
    ui.mouse(ui.viewportCenter,false);ui.viewportHit=NoEntity;ui.mouse(ui.viewportCenter,true);
    require(ui.document.selection().ids.empty(),"Clicking an empty viewport after an Inspector edit did not deselect.");
    ui.mouse(ui.viewportCenter,false);ui.viewportHit=ui.entity;ui.mouse(ui.viewportCenter,true);
    require(ui.document.selection().primary==ui.entity && ui.viewportSelections==3,
        "Selection remained locked after clearing the previous selection.");
    ui.mouse(ui.viewportCenter,false);
}
void dragReleaseCommits(){
    Project project;Inspector ui(project.root);const auto start=ui.centers[6];
    ui.mouse(start,true);ui.mouse({start[0]-25,start[1]},true);ui.mouse({start[0]-50,start[1]},true);
    require(ui.matrix()[0]==0,"Dragging the scale field did not reach zero.");
    ui.mouse({start[0]-50,start[1]},false);ui.tick();
    require(!ui.document.transactionActive()&&ui.document.history().size()==1,"Releasing a zero-scale drag did not commit one command.");
    ui.undo();require(ui.sceneUndo&&ui.matrix()==ui.baseline,"Zero-scale drag could not be undone after release.");
}
void diagnosticAppearanceKeepsUndo(){
    for(const bool docked:{false,true})for(unsigned axis=0;axis<3;++axis){
        std::cout<<"CHECK diagnostic focus "<<(docked?"docked ":"floating ")<<"XYZ"[axis]<<'\n';
        Project project;Inspector ui(project.root,docked);ui.diagnosticsEnabled=true;
        const auto start=ui.centers[6+axis];
        ui.mouse(start,true);ui.mouse({start[0]-25,start[1]},true);ui.mouse({start[0]-50,start[1]},true);
        require(ui.matrix()[axis*4+axis]==0,"The diagnostic regression did not drag its scale axis to zero.");
        require(ui.diagnosticFrames>0,"The collapsed model did not show the production diagnostics panel.");
        require(ui.focus=="Inspector","New renderer diagnostics took focus away from the active scale gesture.");
        ui.mouse({start[0]-50,start[1]},false);
        require(!ui.document.transactionActive()&&ui.document.history().size()==1,"Diagnostic appearance prevented release from committing the scale gesture.");
        const auto collapsed=ui.matrix();
        // No viewport click or explicit focus reset: this is the user's exact
        // release -> Ctrl+Z sequence through the production shortcut function.
        ui.undo();require(ui.sceneUndo&&ui.matrix()==ui.baseline,"Automatic renderer diagnostics blocked the real scene Ctrl+Z shortcut.");
        require(ui.document.selection().primary==ui.entity&&!ui.document.dirty(),"Undo after a diagnostic lost selection or the saved-state token.");
        const auto beforeReappearance=ui.diagnosticFrames;
        ui.redo();require(ui.sceneRedo&&ui.matrix()==collapsed&&ui.diagnosticFrames>beforeReappearance,"Ctrl+Y did not restore zero scale and its diagnostic.");
        require(ui.focus=="Inspector","Reappearing diagnostics stole keyboard focus after Redo.");
        ui.sceneUndo=false;ui.undo();
        require(ui.sceneUndo&&ui.matrix()==ui.baseline&&ui.document.history().size()==1,"A reappearing diagnostic blocked the second Undo or created extra history.");
    }
}

void horizontalTransformRows(){
    Project project;Inspector ui(project.root);
    for(unsigned property=0;property<3;++property){
        const auto& x=ui.centers[property*3];const auto& y=ui.centers[property*3+1];const auto& z=ui.centers[property*3+2];
        require(x[0]<y[0]&&y[0]<z[0]&&x[1]==y[1]&&y[1]==z[1],"Transform axes are not arranged in a horizontal XYZ row.");
        if(property)require(x[1]>ui.centers[(property-1)*3][1],"Transform property rows overlap.");
    }
}
void linkedScalePreservesAffineAndHistory(){
    Project project;Inspector ui(project.root);
    auto shear=Mat4{};shear.at(0,1)=.35f;shear.at(1,2)=-.2f;
    ui.setTransform(math::translation({7,-3,9})*math::eulerRotationXYZ({.2f,-.5f,.7f})*shear*math::scaling({-2,3,5}));
    ui.state.scaleLinked=true;
    const auto target=std::to_string(ui.state.fields.scale[0]*2);
    ui.activate(6);ui.type(target.c_str());
    const auto scaled=ui.matrix();
    for(unsigned column=0;column<3;++column)for(unsigned row=0;row<3;++row)
        require(std::abs(scaled.at(row,column)-2*ui.baseline.at(row,column))<1e-5f,"Linked scale lost a reflected or sheared column's ratio.");
    for(unsigned row=0;row<4;++row)require(scaled.at(row,3)==ui.baseline.at(row,3),"Linked scale changed translation.");
    ui.enter();require(ui.document.history().size()==1&&!ui.document.transactionActive(),"Linked scale did not commit one gesture.");
    ui.tick();require(ui.matrix()==scaled,"Idle transform drawing recomposed the affine matrix.");
    ui.undo();require(ui.sceneUndo&&ui.matrix()==ui.baseline&&!ui.document.dirty(),"Linked scale Undo did not restore the exact affine matrix.");
    ui.redo();require(ui.sceneRedo&&ui.matrix()==scaled,"Linked scale Redo did not restore all three columns.");
}
void linkedScaleCrossesZeroAndRecovers(){
    Project project;Inspector ui(project.root);ui.setTransform(math::translation({1,2,3})*math::scaling({2,3,5}));
    ui.state.scaleLinked=true;ui.activate(6);ui.zero();
    for(unsigned column=0;column<3;++column)for(unsigned row=0;row<3;++row)
        require(ui.matrix().at(row,column)==0,"Linked zero did not collapse every scale axis.");
    ui.replaceText("-2");
    require(ui.matrix()==math::translation({1,2,3})*math::scaling({-2,-3,-5}),"Crossing zero lost the linked gesture's original proportions.");
    ui.enter();require(ui.document.history().size()==1,"A linked zero crossing split one numeric gesture into several commands.");
    ui.undo();require(ui.matrix()==ui.baseline,"A linked zero crossing could not recover its initial transform.");

    ui.setTransform(math::translation({1,2,3})*math::scaling({0,0,0}));
    ui.activate(7);ui.type("2");ui.enter();
    require(ui.matrix()==math::translation({1,2,3})*math::scaling({2,2,2}),"Linked scale could not recover from an initially collapsed matrix.");
    require(ui.document.history().size()==1,"Recovering linked zero scale did not create one command.");
    ui.undo();require(ui.matrix()==ui.baseline,"Undo after linked zero recovery lost the collapsed baseline.");
}

Mat4 savedNewDecalTransform(){
    // Exact saved New Decal 2 linear basis, including its unused 215 m Z axis.
    Mat4 value;value.values={
        1.242539525f,.399440706f,-1.746122241f,0,
        .522620201f,-2.223793983f,-.136816323f,0,
        -170.141799927f,-32.085025787f,-128.412567139f,0,
        .000000856617f,2.934373379f,24.563133240f,1};return value;
}
Vec3 column(const Mat4& matrix,unsigned index){return {matrix.at(0,index),matrix.at(1,index),matrix.at(2,index)};}
void decalSurfaceRotationAndPosition(){
    for(const bool parented:{false,true}){
        Project project;Inspector ui(project.root);
        const auto parent=parented?std::optional<Mat4>{math::translation({3,-2,7})*math::eulerRotationXYZ({.3f,-.4f,.2f})*math::scaling({1.5f,.7f,-2.f})}:std::nullopt;
        ui.setDecalTransform(savedNewDecalTransform(),parent);
        const auto original=ui.worldMatrix(),local=ui.matrix();
        const auto visible=*decalEditFrame(ui.document.scene(),ui.entity,ui.decalSurfaceFrame);
        require(ui.columnCounts==std::array<unsigned,3>{3,1,2},"Decal Inspector did not replace Euler/XYZ scale with one surface rotation and width/height.");
        ui.tick();ui.tick();ui.activate(5);ui.enter();
        require(ui.matrix()==local&&!ui.document.dirty()&&ui.document.history().size()==0,
            "Displaying or activating a decal field rewrote the saved tilted transform.");

        const auto target=std::to_string(ui.state.degrees[2]+90.f);
        ui.activate(5);ui.type(target.c_str());ui.enter();const auto rotated=ui.worldMatrix();
        const auto expected=math::rotate(column(visible,0),Vec3{1,0,0},std::numbers::pi_v<double>*.5);
        require(math::length(column(rotated,0)-expected)<1e-4,"Decal rotation did not turn its visible U axis by 90 degrees on the wall.");
        require(std::abs(math::dot(column(rotated,0),Vec3{1,0,0}))+std::abs(math::dot(column(rotated,1),Vec3{1,0,0}))<1e-4,
            "Decal rotation tilted its width/height axes away from the receiving wall.");
        for(unsigned axis=0;axis<2;++axis)require(std::abs(math::length(column(rotated,axis))-math::length(column(original,axis)))<1e-4,
            "Decal rotation changed its width or height.");
        require(math::length(column(rotated,2)-Vec3{1,0,0})<1e-4&&math::length(column(rotated,3)-column(original,3))<1e-4,
            "Decal rotation retained hidden depth or changed authored translation.");
        require(ui.document.history().size()==1&&!ui.document.transactionActive(),"Decal rotation did not commit exactly one gesture.");
        const auto rotatedLocal=ui.matrix();ui.undo();
        require(ui.sceneUndo&&ui.matrix()==local&&!ui.document.dirty(),"Decal rotation Undo lost the exact saved affine matrix.");
        ui.redo();require(ui.sceneRedo&&ui.matrix()==rotatedLocal,"Decal rotation Redo did not restore its edited matrix.");

        ui.undo();const auto position=std::to_string(ui.state.fields.position[0]+.25f);
        ui.activate(0);ui.type(position.c_str());ui.enter();
        for(unsigned axis=0;axis<3;++axis)for(unsigned row=0;row<3;++row)
            require(ui.matrix().at(row,axis)==local.at(row,axis),"Decal position editing canonicalized its unedited affine basis.");
        require(std::abs(ui.matrix().at(0,3)-local.at(0,3)-.25f)<1e-4,"Decal Position stopped using parent-local coordinates.");
        ui.undo();require(ui.matrix()==local,"Decal position Undo did not restore its exact local matrix.");
    }
}
void decalSurfaceSizeAndCancellation(){
    for(const bool linked:{false,true}){
        Project project;Inspector ui(project.root);
        auto authored=savedNewDecalTransform();
        if(linked)for(unsigned row=0;row<3;++row){authored.at(row,1)+=authored.at(row,0)*.2f;authored.at(row,0)*=-1;}
        ui.setDecalTransform(authored);ui.state.scaleLinked=linked;
        const auto original=ui.worldMatrix(),local=ui.matrix();
        const auto visible=*decalEditFrame(ui.document.scene(),ui.entity,ui.decalSurfaceFrame);
        const auto originalSize=decals::decalSize(visible);const auto width=std::to_string(originalSize[0]*2);
        ui.activate(6);ui.type(width.c_str());ui.enter();const auto resized=ui.worldMatrix();
        require(std::abs(math::length(column(resized,0))-originalSize[0]*2)<1e-4,
            "Decal width did not resize proportionally in world metres.");
        require(std::abs(math::length(column(resized,1))-std::abs(originalSize[1])*(linked?2:1))<1e-4,
            "Decal Lock proportions did not scale just its width and height.");
        require(math::length(column(resized,2)-Vec3{1,0,0})<1e-5&&math::length(column(resized,3)-column(original,3))<1e-5,
            "Resizing a decal mixed its old huge depth into its width or moved its center.");
        require(math::handedness(resized)==math::handedness(original),"Decal size editing lost reflection.");
        const auto cosine=[](const Mat4& m){return math::dot(column(m,0),column(m,1))/(math::length(column(m,0))*math::length(column(m,1)));};
        require(std::abs(cosine(resized)-cosine(visible))<1e-5,"Decal size editing lost XY shear.");
        const auto resizedLocal=ui.matrix();ui.undo();
        require(ui.matrix()==local&&!ui.document.dirty()&&ui.document.history().size()==1,"Decal size Undo lost its exact saved transform or gesture boundary.");
        ui.redo();require(ui.matrix()==resizedLocal,"Decal size Redo did not restore its edited transform.");
        ui.undo();ui.activate(7);ui.type(std::to_string(originalSize[1]*.5f).c_str());
        require(ui.document.transactionActive(),"Decal size input did not retain its live transaction.");
        ui.document.cancel();ImGui::ClearActiveID();ui.tick();ui.tick();
        require(ui.matrix()==local&&!ui.document.dirty()&&!ui.document.transactionActive(),"Canceling a decal size gesture canonicalized its saved transform.");
    }
}

void boxDecalSizeAndHistory(){
    Project project;Inspector ui(project.root);
    const auto parent=math::translation({3,-2,7})*math::eulerRotationXYZ({.3f,-.4f,.2f})*math::scaling({1.5f,.7f,-2.f});
    ui.setDecalTransform(savedNewDecalTransform(),parent);
    auto record=*ui.document.scene().find(ui.entity);record.decal->projection=SceneDecalProjection::Box;
    require(ui.document.updateEntity(ui.entity,record),"Cannot change isolated fixture to Box.");
    ui.state.refresh=true;ui.tick();ui.tick();
    require(ui.columnCounts==std::array<unsigned,3>{3,3,3},"Box Inspector omitted full rotation or depth.");
    require(!decalEditFrame(ui.document.scene(),ui.entity),"Box edits entered surface canonicalization.");
    const auto before=ui.worldMatrix(),local=ui.matrix();
    const float depth=math::length(column(before,2));
    ui.activate(8);ui.type(std::to_string(depth*.5f).c_str());ui.enter();const auto after=ui.worldMatrix();
    require(std::abs(math::length(column(after,2))-depth*.5f)<1e-3,"Box depth did not resize in world metres under an affine parent.");
    for(unsigned axis:{0u,1u,3u})require(math::length(column(after,axis)-column(before,axis))<1e-4,"Box depth changed footprint, shear or centre.");
    require(math::handedness(after)==math::handedness(before),"Box size lost reflection.");
    const auto edited=ui.matrix();ui.undo();require(ui.matrix()==local&&!ui.document.transactionActive(),"Box depth undo lost exact authored transform.");
    ui.redo();require(ui.matrix()==edited,"Box depth redo lost edited transform.");
}

void particleShortcutOwnership(){
    struct ContextLifetime {
        ImGuiContext* value{ImGui::CreateContext()};
        ~ContextLifetime(){ImGui::DestroyContext(value);}
    } lifetime;
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={960,640};io.DeltaTime=1.f/60;
    io.ConfigInputTrickleEventQueue=false;
    unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
    io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    ImVec2 paintFieldLow{},paintFieldHigh{};float paintHeight=.5f;
    const auto tick=[&](SceneShortcutContext ownership,const char* focus) {
        ImGui::NewFrame();
        for(const auto* name:{"Inspector","Particle effect###ParticleEffect","Sprite material###SpriteMaterial","Vertex Paint Tool"}) {
            ImGui::SetNextWindowPos(std::string_view(name)=="Inspector" ? ImVec2(20,20):ImVec2(440,20));
            ImGui::SetNextWindowSize({380,400});
            ImGui::Begin(name,nullptr,ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing);
            if(focus && std::string_view(name)==focus)ImGui::SetWindowFocus();
            ImGui::TextUnformatted(name);
            if(std::string_view(name)=="Vertex Paint Tool") {
                ImGui::InputFloat("Height Influence",&paintHeight);
                paintFieldLow=ImGui::GetItemRectMin();paintFieldHigh=ImGui::GetItemRectMax();
            }
            ImGui::End();
        }
        const auto result=readSceneShortcuts(ownership);ImGui::Render();return result;
    };
    tick({},nullptr);
    const auto press=[&](ImGuiKey key,SceneShortcutContext ownership,const char* focus,bool control=true,bool shift=false) {
        io.AddKeyEvent(ImGuiMod_Ctrl,control);io.AddKeyEvent(ImGuiMod_Shift,shift);io.AddKeyEvent(key,true);
        const auto result=tick(ownership,focus);
        io.AddKeyEvent(key,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);io.AddKeyEvent(ImGuiMod_Shift,false);tick(ownership,focus);
        return result;
    };
    for(const bool material:{false,true}) {
        const auto* assetWindow=material?"Sprite material###SpriteMaterial":"Particle effect###ParticleEffect";
        const SceneShortcutContext owner{.particleDocumentFocus=!material,.materialDocumentFocus=material};
        auto result=press(ImGuiKey_Z,owner,assetWindow);
        require(result.undo && !result.redo,"Asset-document focus did not emit its Undo shortcut.");
        result=press(ImGuiKey_Z,owner,assetWindow,true,true);
        require(result.redo && !result.undo,"Asset-document Ctrl+Shift+Z did not emit Redo.");
        require(press(ImGuiKey_Y,owner,assetWindow).redo,"Asset-document Ctrl+Y did not emit Redo.");
        require(press(ImGuiKey_S,owner,assetWindow).save,"Asset-document Ctrl+S did not emit Save.");
        require(press(ImGuiKey_S,owner,assetWindow,true,true).saveAs,"Asset-document Ctrl+Shift+S did not emit Save As.");
        for(const auto key:{ImGuiKey_D,ImGuiKey_Delete,ImGuiKey_F}) {
            result=press(key,owner,assetWindow,key==ImGuiKey_D);
            require(!result.duplicate && !result.eraseSelection && !result.focusSelection,"An asset document emitted a scene entity command.");
        }
        auto blocked=owner;blocked.preferencesKeyboard=true;
        require(!press(ImGuiKey_Z,blocked,assetWindow).undo,"Preferences input ownership leaked asset-document Undo.");
        blocked=owner;blocked.transactionActive=true;
        require(!press(ImGuiKey_Z,blocked,assetWindow).undo,"An active document transaction leaked Undo.");
        blocked=owner;blocked.modelDropPending=true;
        require(!press(ImGuiKey_Z,blocked,assetWindow).undo,"A same-frame asset delivery leaked document Undo.");
        blocked=owner;blocked.navigationActive=true;
        require(!press(ImGuiKey_S,blocked,assetWindow).save,"Navigation input ownership leaked asset-document Save.");
        require(!press(ImGuiKey_Z,{},assetWindow).undo,"An unrelated focused window emitted scene Undo.");
    }
    require(press(ImGuiKey_Z,{},"Inspector").undo,"Returning to Inspector lost scene Undo.");
    require(press(ImGuiKey_D,{},"Inspector").duplicate,"Returning to Inspector lost scene Duplicate.");
    require(press(ImGuiKey_Delete,{},"Inspector",false).eraseSelection,"Returning to Inspector lost scene Delete.");
    require(press(ImGuiKey_F,{},"Inspector",false).focusSelection,"Returning to Inspector lost scene Focus.");
    require(press(ImGuiKey_Z,{},"Vertex Paint Tool").undo,"Inactive paint-panel focus lost scene Undo.");
    require(press(ImGuiKey_Z,{},"Vertex Paint Tool",true,true).redo,"Inactive paint-panel focus lost scene Ctrl+Shift+Z Redo.");
    require(press(ImGuiKey_Y,{},"Vertex Paint Tool").redo,"Inactive paint-panel focus lost scene Ctrl+Y Redo.");
    require(press(ImGuiKey_D,{},"Vertex Paint Tool").duplicate,"Inactive paint-panel focus lost scene Duplicate.");
    require(press(ImGuiKey_Delete,{},"Vertex Paint Tool",false).eraseSelection,"Inactive paint-panel focus lost scene Delete.");
    require(press(ImGuiKey_F,{},"Vertex Paint Tool",false).focusSelection,"Inactive paint-panel focus lost scene Focus.");
    require(!press(ImGuiKey_Z,{.transactionActive=true},"Vertex Paint Tool").undo,
        "An active paint-property transaction leaked scene Undo.");
    require(!press(ImGuiKey_Z,{.navigationActive=true},"Vertex Paint Tool").undo,
        "Scene navigation ownership leaked Undo through paint-panel focus.");
    require(!press(ImGuiKey_Z,{.preferencesKeyboard=true},"Vertex Paint Tool").undo,
        "Preferences keyboard ownership leaked Undo through paint-panel focus.");
    io.AddMousePosEvent((paintFieldLow.x+paintFieldHigh.x)*.5f,(paintFieldLow.y+paintFieldHigh.y)*.5f);
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick({},"Vertex Paint Tool");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick({},"Vertex Paint Tool");
    require(ImGui::IsAnyItemActive(),"Paint numeric-input shortcut fixture did not activate its real widget.");
    require(!press(ImGuiKey_Z,{},"Vertex Paint Tool").undo,
        "An active paint numeric input emitted scene Undo instead of retaining text-input ownership.");
    ImGui::ClearActiveID();tick({},"Vertex Paint Tool");tick({},"Vertex Paint Tool");
    require(press(ImGuiKey_Z,{},"Vertex Paint Tool").undo,
        "Releasing paint numeric-input ownership did not restore scene Undo.");
}
}
int main(){
    unsigned failures{};
    const auto run=[&](const char* name,auto test){
        try{test();std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failures;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    run("zero text, validation and scene Undo/Redo",zeroTextAndUndo);
    run("active numeric text keeps its own Undo",numericTextOwnsUndo);
    run("menu Undo finalizes and does not reapply text",menuFinishesAndDoesNotReapply);
    run("clipped scalar finalizes without deactivation",missedDeactivationReleasesOwnership);
    run("collapsing Transform finalizes its active numeric field",collapsedTransformReleasesOwnership);
    run("viewport click releases Inspector ownership before picking and empty-space deselection",viewportClickReleasesInspectorOwnership);
    run("zero scale drag and release",dragReleaseCommits);
    run("diagnostics preserve real Ctrl+Z after zero-scale drag (floating/docked, XYZ)",diagnosticAppearanceKeepsUndo);
    run("transform properties use horizontal XYZ rows",horizontalTransformRows);
    run("linked scale preserves affine ratios and one undo gesture",linkedScalePreservesAffineAndHistory);
    run("linked scale crosses zero and recovers collapsed axes",linkedScaleCrossesZeroAndRecovers);
    run("decal surface rotation, parent conversion and position keep exact history",decalSurfaceRotationAndPosition);
    run("decal metre sizes, XY proportions, reflection, shear and cancellation",decalSurfaceSizeAndCancellation);
    run("box decal world sizes, affine parent, depth and exact history",boxDecalSizeAndHistory);
    run("particle/material document shortcuts preserve scene-only commands and input ownership",particleShortcutOwnership);
    return failures?1:0;
}
