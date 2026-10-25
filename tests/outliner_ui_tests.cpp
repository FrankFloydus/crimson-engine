#include "tools/SceneOutlinerUi.h"
#include "scene/SceneDocument.h"
#include "scene/SceneOutlinerRows.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-outliner-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto owner=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;const auto target=fs::weakly_canonical(root,error);if(!error&&target.parent_path()==owner&&target.filename().string().starts_with("crimson-outliner-ui-"))fs::remove_all(target,error);}
};
struct Context {
    ImGuiContext* value=ImGui::CreateContext();
    Context(){auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,600};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));}
    ~Context(){ImGui::DestroyContext(value);}
};
ImVec2 center(const ImRect& rect){return {(rect.Min.x+rect.Max.x)*.5f,(rect.Min.y+rect.Max.y)*.5f};}

// Input targets this private CPU ImGui context only. The shared controls are
// the actual menu/hit-region helpers used by ScenePanels, with scene commands
// supplied here instead of linking the unrelated Inspector/GPU/asset owners.
struct Ui {
    Context context;
    SceneDocument document;
    EntityId parent{},child{},other{},rename{};
    bool allowed{true},focusRename{},addVisible{},backgroundMenu{},rowMenu{},rootTarget{};
    std::map<EntityId,ImRect> rows;
    ImRect content{},add{},addItem{},scrollbar{};
    bool hasScrollbar{};
    char name[64]{"Child"};
    explicit Ui(const fs::path& root):document(root){
        parent=document.createEmpty("Parent");child=document.createEmpty("Child");other=document.createEmpty("Other");
        document.select({child},child);require(document.reparentSelection(parent),"Cannot prepare the child fixture.");
        for(unsigned i=0;i<80;++i)document.createEmpty("Entity "+std::to_string(i));
        document.select({child},child);tick();tick();
    }
    void tick(){
        ImGui::NewFrame();rows.clear();addVisible=backgroundMenu=rowMenu=rootTarget=false;
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({340,220});
        ImGui::Begin("Outliner",nullptr,SceneOutlinerWindowFlags|ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        auto* window=ImGui::GetCurrentWindow();const auto menu=window->MenuBarRect();
        add={{menu.Min.x+ImGui::GetStyle().WindowPadding.x,menu.Min.y},{menu.Min.x+ImGui::GetStyle().WindowPadding.x+ImGui::CalcTextSize("Add").x,menu.Max.y}};
        drawSceneOutlinerAddMenu(allowed,[&]{addVisible=true;if(ImGui::MenuItem("Empty / group"))document.createEmpty("Added group");addItem={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};});
        content=window->InnerRect;hasScrollbar=window->ScrollbarY;if(hasScrollbar)scrollbar=ImGui::GetWindowScrollbarRect(window,ImGuiAxis_Y);
        std::unordered_map<EntityId,std::vector<EntityId>> children;for(const auto& row:sceneOutlinerRows(document.scene()))children[row.parent].push_back(row.entity);
        struct Work{EntityId id;bool close{};};std::vector<Work> pending;for(auto it=children[NoEntity].rbegin();it!=children[NoEntity].rend();++it)pending.push_back({*it});
        bool hoveredRow=false;EntityId delivered{},deliveredParent{};
        const auto target=[&](EntityId owner){if(const auto* payload=ImGui::AcceptDragDropPayload("OUTLINER_UI_ENTITIES",ImGuiDragDropFlags_AcceptBeforeDelivery))if(payload->IsDelivery()){delivered=*static_cast<const EntityId*>(payload->Data);deliveredParent=owner;}};
        while(!pending.empty()){
            const auto work=pending.back();pending.pop_back();if(work.close){ImGui::TreePop();continue;}const auto record=*document.scene().find(work.id);const bool leaf=children[work.id].empty();
            const bool selected=std::find(document.selection().ids.begin(),document.selection().ids.end(),work.id)!=document.selection().ids.end();
            ImGui::BeginDisabled(!allowed);if(work.id==parent)ImGui::SetNextItemOpen(true,ImGuiCond_Once);const auto id=std::to_string(work.id);
            const bool open=ImGui::TreeNodeEx(id.c_str(),sceneOutlinerRowFlags(selected,leaf,rename==work.id),"%s",rename==work.id?"":record.name.c_str());
            rows.emplace(work.id,ImRect{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()});hoveredRow|=sceneOutlinerRowHovered();
            if(allowed&&rename!=work.id&&ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen())document.select({work.id},work.id);
            if(allowed&&rename!=work.id&&ImGui::BeginDragDropSource()){ImGui::SetDragDropPayload("OUTLINER_UI_ENTITIES",&work.id,sizeof(work.id));ImGui::TextUnformatted(record.name.c_str());ImGui::EndDragDropSource();}
            if(allowed&&rename!=work.id&&ImGui::BeginDragDropTarget()){target(work.id);ImGui::EndDragDropTarget();}
            if(rename!=work.id&&ImGui::BeginPopupContextItem()){rowMenu=true;ImGui::MenuItem("Rename");ImGui::EndPopup();}
            if(rename==work.id){const auto next=ImGui::GetCursorScreenPos();const auto& rect=rows.at(work.id);ImGui::SetCursorScreenPos({rect.Min.x+ImGui::GetTreeNodeToLabelSpacing(),rect.Min.y});ImGui::SetNextItemWidth(rect.GetWidth()-ImGui::GetTreeNodeToLabelSpacing());if(focusRename){ImGui::SetKeyboardFocusHere();focusRename=false;}ImGui::InputText("##rename",name,sizeof(name));ImGui::SetCursorScreenPos(next);}
            ImGui::EndDisabled();if(open&&!leaf){pending.push_back({work.id,true});for(auto it=children[work.id].rbegin();it!=children[work.id].rend();++it)pending.push_back({*it});}
        }
        if(sceneOutlinerBackgroundClicked(allowed,hoveredRow))document.select({});
        if(allowed&&beginSceneOutlinerBackgroundDropTarget(hoveredRow)){rootTarget=true;target(NoEntity);ImGui::EndDragDropTarget();}
        ImGui::BeginDisabled(!allowed);if(beginSceneOutlinerBackgroundMenu(allowed,hoveredRow)){backgroundMenu=true;if(ImGui::BeginMenu("Add")){ImGui::MenuItem("Empty / group");ImGui::EndMenu();}ImGui::EndPopup();}ImGui::EndDisabled();
        ImGui::End();if(delivered){document.select({delivered},delivered);document.reparentSelection(deliveredParent);}ImGui::Render();
    }
    void move(ImVec2 position){ImGui::GetIO().AddMousePosEvent(position.x,position.y);tick();}
    void click(ImVec2 position,ImGuiMouseButton button=ImGuiMouseButton_Left){auto& io=ImGui::GetIO();io.AddMousePosEvent(position.x,position.y);io.AddMouseButtonEvent(button,true);tick();io.AddMouseButtonEvent(button,false);tick();}
    ImVec2 blank(EntityId entity)const{return {content.Max.x-12,center(rows.at(entity)).y};}
    void drag(EntityId source,ImVec2 destination){auto& io=ImGui::GetIO();const auto from=center(rows.at(source));io.AddMousePosEvent(from.x,from.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();io.AddMousePosEvent(from.x+8,from.y);tick();io.AddMousePosEvent(destination.x,destination.y);tick();tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();tick();}
};
void directBlankSelectionAndGuards(){
    Project project;Ui ui(project.root);require(ui.hasScrollbar,"Crowded Outliner fixture did not scroll.");
    require(ui.rows.at(ui.child).Max.x<ui.content.Max.x-40,"A short row still owns its empty right-hand area.");
    ui.document.select({ui.parent,ui.child},ui.child);const auto history=ui.document.history().size();
    ui.move(center(ui.rows.at(ui.child)));ui.click(ui.blank(ui.child));
    require(ui.document.selection().ids.empty()&&ui.document.history().size()==history,"Immediate label-to-blank click did not deselect without history.");
    ui.click(center(ui.rows.at(ui.child)));require(ui.document.selection().primary==ui.child,"Label hit no longer selects its entity.");
    ui.click({ui.content.Min.x+2,center(ui.rows.at(ui.child)).y});require(ui.document.selection().ids.empty(),"Child indentation gutter was treated as part of the row.");
    ui.document.select({ui.child},ui.child);ui.click({ui.rows.at(ui.parent).Min.x+ImGui::GetFontSize()*.5f,center(ui.rows.at(ui.parent)).y});
    require(ui.document.selection().primary==ui.child,"Expanding/collapsing a parent cleared the existing selection.");
    ui.click(center(ui.scrollbar));require(ui.document.selection().primary==ui.child,"Scrollbar interaction cleared selection.");
    ui.click({ui.content.Max.x-5,center(ui.add).y});require(ui.document.selection().primary==ui.child,"Empty menu-bar space cleared selection.");
    ImGui::FindWindowByName("Outliner")->Scroll.y=0;ui.tick();ui.tick();ui.allowed=false;ui.click(ui.blank(ui.parent));require(ui.document.selection().primary==ui.child,"Blocked background bypassed the edit guard.");
}
void crowdedAddAndContextMenus(){
    Project project;Ui ui(project.root);const auto history=ui.document.history().size();const auto count=ui.document.scene().records().size();
    ui.click(center(ui.add));require(ui.addVisible,"Persistent Add was inaccessible with a crowded tree.");
    ui.click(center(ui.addItem));require(ui.document.scene().records().size()==count+1&&ui.document.history().size()==history+1,"Add did not create exactly one undoable root entity.");
    require(ui.document.scene().find(ui.document.selection().primary)->parent==NoEntity,"Persistent Add unexpectedly parented to the selection.");
    require(ui.document.undo()&&ui.document.scene().records().size()==count,"Undo did not restore the crowded Add command.");
    ui.tick();ui.move(center(ui.rows.at(ui.parent)));ui.click(ui.blank(ui.parent),ImGuiMouseButton_Right);
    require(ui.backgroundMenu&&!ui.rowMenu,"Row-right background did not open the root context menu.");
    const auto selection=ui.document.selection().ids;ui.click(ui.blank(ui.parent));require(ui.document.selection().ids==selection,"An open menu let its dismissal click clear selection underneath.");
    ui.click(center(ui.rows.at(ui.parent)),ImGuiMouseButton_Right);require(ui.rowMenu&&!ui.backgroundMenu,"Label context actions were replaced by the background menu.");
}
void renameAndDragOwnership(){
    Project project;Ui ui(project.root);ui.rename=ui.child;ui.focusRename=true;ui.tick();ui.tick();
    const auto selection=ui.document.selection().ids;ui.click(ui.blank(ui.child));
    require(ui.document.selection().ids==selection&&!ui.backgroundMenu,"Inline name field lost its row input ownership.");
    ImGui::ClearActiveID();ui.rename=NoEntity;ui.tick();ui.tick();
    const auto history=ui.document.history().size();ui.drag(ui.child,ui.blank(ui.child));
    require(ui.document.scene().find(ui.child)->parent==NoEntity&&ui.document.history().size()==history+1,"Dragging to row-right blank space did not retain the scene-root drop target.");
    require(ui.document.undo()&&ui.document.scene().find(ui.child)->parent==ui.parent,"Root-drop Undo lost the original parent.");
    ui.tick();ui.tick();ui.drag(ui.other,center(ui.rows.at(ui.parent)));
    require(ui.document.scene().find(ui.other)->parent==ui.parent,"Label-sized row stopped accepting reparent drops.");
}
}
int main(){try{directBlankSelectionAndGuards();crowdedAddAndContextMenus();renameAndDragOwnership();std::cout<<"Outliner crowded Add, immediate blank deselection, context/input guards and row/root drop proofs passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
