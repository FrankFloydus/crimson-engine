#include "tools/InspectorTransform.h"
#include "tools/SceneDiagnostics.h"
#include "editor/SceneShortcuts.h"
#include "crimson-renderer/ModelTransform.h"
#include "scene/SceneDocument.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <array>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

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
    void tick(bool menuUndo=false,bool clippedAfterFocusLoss=false) {
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
        drawInspectorTransform(document,state,*document.scene().find(entity),transactions);
        window->SkipItems=skipped;
        for(unsigned property=0;property<3;++property){
            const auto id=axesTableId(property);const auto* table=ImGui::GetCurrentContext()->Tables.GetByKey(id);
            if(!table)continue;
            const float rowHeight=table->RowPosY2-table->RowPosY1;
            for(unsigned axis=0;axis<3;++axis)centers[property*3+axis]={
                (table->Columns[1].WorkMinX+table->Columns[1].WorkMaxX)*.5f,
                table->OuterRect.Min.y+table->RowCellPaddingY+ImGui::GetFrameHeight()*.5f+axis*rowHeight};
        }
        ImGui::End();
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
    void enter(){key(ImGuiKey_Enter,true);tick();key(ImGuiKey_Enter,false);tick();tick();}
    void key(ImGuiKey key,bool down){ImGui::GetIO().AddKeyEvent(key,down);}
    void undo(){key(ImGuiMod_Ctrl,true);key(ImGuiKey_Z,true);tick();key(ImGuiKey_Z,false);key(ImGuiMod_Ctrl,false);tick();tick();}
    void redo(){key(ImGuiMod_Ctrl,true);key(ImGuiKey_Y,true);tick();key(ImGuiKey_Y,false);key(ImGuiMod_Ctrl,false);tick();tick();}
    void mouse(Vec2 point,bool down){auto& io=ImGui::GetIO();io.AddMousePosEvent(point[0],point[1]);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);tick();}
    Mat4 matrix()const{return document.scene().find(entity)->local;}
    SceneDocument document;
    InspectorTransformState state;
    EntityId entity{};
    Mat4 baseline{};
    std::array<Vec2,9> centers{};
    bool sceneUndo{},sceneRedo{},diagnosticsEnabled{};
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
    run("zero scale drag and release",dragReleaseCommits);
    run("diagnostics preserve real Ctrl+Z after zero-scale drag (floating/docked, XYZ)",diagnosticAppearanceKeepsUndo);
    return failures?1:0;
}
