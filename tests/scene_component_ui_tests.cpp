#include "tools/SceneComponentSelection.h"
#include "tools/SceneComponentRow.h"
#include "tools/CameraAuthoringControls.h"
#include "tools/SceneOutlinerColor.h"
#include "editor/SceneShortcuts.h"
#include "scene/CameraAuthoring.h"

#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool condition,const char* message) {if(!condition)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/
        ("crimson-component-ui-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project() {
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-component-ui-test-"))fs::remove_all(target,error);
    }
};
using Row=std::pair<EntityId,SceneComponentKind>;
struct Rect {ImVec2 low{},high{};ImVec2 center()const{return {(low.x+high.x)*.5f,(low.y+high.y)*.5f};}};

// This fixture exercises the production row, selection, Inspector routing and
// reorder receiver using a private CPU ImGui context. No native window, desktop
// input, graphics backend, clipboard or authored project is accessed.
class ComponentUi {
public:
    explicit ComponentUi(const fs::path& root):document(root) {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1100,760};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        player=document.createPlayer();enemy=document.createEnemy();
        document.select({player},player);selection.bind(document);tick();tick();
    }
    ~ComponentUi(){ImGui::DestroyContext(context_);}
    void tick() {
        ImGui::NewFrame();selection.sync(document);rects.clear();
        constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|
            ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({430,710});
        ImGui::Begin("Outliner",nullptr,flags);
        struct Reorder {EntityId owner{};SceneOutlinerRowDrop drop;};
        std::vector<Reorder> reorders;
        for(const auto owner:{player,enemy}) {
            const auto record=document.scene().find(owner);if(!record)continue;
            ImGui::TextUnformatted(record->name.c_str());
            const auto components=sceneComponents(*record);
            for(std::size_t index=0;index<components.size();++index) {
                const auto kind=components[index];
                const bool expandable=kind==SceneComponentKind::Actor;
                const auto result=drawSceneComponentRow(document,owner,kind,
                    selection.selected(owner,kind),expandable,sceneOutlinerColor(*record,kind));
                rects[{owner,kind}]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
                if(result.clicked)selection.select(document,owner,kind,ImGui::GetIO().KeyCtrl);
                const auto next=index+1<components.size()?std::optional{SceneOutlinerRow::componentRow(components[index+1])}:std::nullopt;
                if(const auto drop=acceptSceneOutlinerRowReorder(document,owner,SceneOutlinerRow::componentRow(kind),next))
                    reorders.push_back({owner,*drop});
                if(expandable&&result.open)ImGui::TreePop();
            }
        }
        ImGui::End();
        for(const auto& action:reorders)if(reorderSceneOutlinerRow(document,action.owner,action.drop.row,action.drop.before))++reorderCommands;
        ImGui::SetNextWindowPos({480,20});ImGui::SetNextWindowSize({590,710});
        ImGui::Begin("Inspector",nullptr,flags);
        visible.clear();
        if(const auto record=document.scene().find(document.selection().primary)) {
            for(const auto kind:sceneComponents(*record))if(selection.allows(kind)) {
                visible.push_back(kind);ImGui::TextUnformatted(sceneComponentName(kind));
            }
        }
        ImGui::InvisibleButton("Inspector focus",{500,450});
        inspector={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
        ImGui::End();
        const auto* nav=ImGui::GetCurrentContext()->NavWindow;
        focus=nav?nav->RootWindow->Name:"";
        const auto keys=readSceneShortcuts({.transactionActive=document.transactionActive()});
        if(keys.eraseSelection) {
            if(selection.component())removed=selection.erase(document);
            else removed=document.eraseSelection();
        }
        ImGui::Render();
    }
    void mouse(ImVec2 point,bool down) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);tick();
    }
    ImVec2 label(Row row)const {
        const auto rect=rects.at(row);return {rect.low.x+ImGui::GetTreeNodeToLabelSpacing()+35,(rect.low.y+rect.high.y)*.5f};
    }
    void click(Row row) {const auto point=label(row);mouse(point,true);mouse(point,false);tick();}
    void ctrlClick(Row row) {
        ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);click(row);ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);tick();
    }
    void drag(Row source,Row target) {
        const auto from=label(source);auto to=label(target);to.y-=(rects.at(target).high.y-rects.at(target).low.y)*.25f;
        mouse(from,true);mouse(to,true);mouse(to,true);mouse(to,false);tick();
    }
    SceneDocument document;
    SceneComponentSelection selection;
    EntityId player{},enemy{};
    std::map<Row,Rect> rects;
    std::vector<SceneComponentKind> visible;
    Rect inspector;
    std::string focus;
    unsigned reorderCommands{};
    bool removed{};
private:
    ImGuiContext* context_{};
};

void selectionAndInspector(const fs::path& root) {
    ComponentUi ui(root);
    require(ui.visible==std::vector{SceneComponentKind::Transform},"Entity selection flooded the Inspector with component properties.");
    ui.click({ui.player,SceneComponentKind::Motor});
    require(ui.selection.entity()==ui.player&&ui.selection.component()==SceneComponentKind::Motor,"Clicking the Motor row did not select its component.");
    require(ui.visible==std::vector{SceneComponentKind::Motor},"Motor selection exposed properties from another component.");
    ui.click({ui.player,SceneComponentKind::Controller});
    require(ui.visible==std::vector{SceneComponentKind::Controller},"Controller selection exposed another component's properties.");
    ui.click({ui.enemy,SceneComponentKind::Motor});
    require(ui.selection.entity()==ui.enemy&&ui.document.selection().primary==ui.enemy,"Identical component labels on different owners shared selection identity.");
    require(ui.visible==std::vector{SceneComponentKind::Motor},"Enemy Motor selection exposed unrelated fields.");
    const auto rect=ui.rects.at({ui.enemy,SceneComponentKind::Actor});
    const ImVec2 arrow{rect.low.x+ImGui::GetFontSize()*.5f,(rect.low.y+rect.high.y)*.5f};
    ui.mouse(arrow,true);ui.mouse(arrow,false);ui.tick();
    require(ui.selection.entity()==ui.enemy&&ui.selection.component()==SceneComponentKind::Motor,"Expanding Visual Root changed the selected component.");
    ui.selection.clear();ui.tick();require(ui.visible==std::vector{SceneComponentKind::Transform},"Clearing a component retained its Inspector fields.");
}

void selectionLifetime(const fs::path& root) {
    ComponentUi ui(root);ui.click({ui.player,SceneComponentKind::Motor});
    require(ui.document.begin("Edit selected Motor"),"Could not start a component property gesture.");
    require(ui.document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==ui.player)record.motor->moveSpeed=7;}),"Could not update selected Motor.");
    ui.tick();require(ui.selection.component()==SceneComponentKind::Motor,"A live property edit cleared component selection.");
    require(!ui.selection.select(ui.document,ui.enemy,SceneComponentKind::Motor),"Component switching bypassed an active document transaction.");
    require(ui.selection.entity()==ui.player,"Rejected selection changed the Inspector owner.");
    ui.document.cancel();ui.tick();require(ui.selection.component()==SceneComponentKind::Motor,"Gesture cancellation cleared component selection.");
    auto record=*ui.document.scene().find(ui.player);record.motor->moveSpeed=3;
    require(ui.document.updateEntity(ui.player,record,"Tune Motor"),"Could not commit selected Motor change.");
    ui.tick();require(ui.selection.component()==SceneComponentKind::Motor,"Committed property edit cleared component selection.");
    require(ui.document.undo(),"Could not undo Motor change.");ui.tick();
    require(ui.selection.component()==SceneComponentKind::Motor,"Property Undo cleared the still-present component.");
    require(ui.document.redo(),"Could not redo Motor change.");ui.tick();
    require(ui.selection.component()==SceneComponentKind::Motor,"Property Redo cleared the still-present component.");
    ui.document.select({ui.enemy},ui.enemy);ui.tick();
    require(!ui.selection.component(),"External owner selection retained the old component target.");
    require(ui.selection.select(ui.document,ui.player,SceneComponentKind::Motor),"Could not restore selected Motor.");
    require(removeSceneComponent(ui.document,ui.player,SceneComponentKind::Motor),"Could not remove selected Motor.");
    ui.tick();require(!ui.selection.component(),"Removing a component left a stale Inspector target.");
    require(ui.document.undo(),"Could not undo component removal.");ui.tick();
    require(!ui.selection.component()&&ui.document.scene().find(ui.player)->motor,"Component Undo resurrected stale selection or lost component data.");
    require(ui.selection.select(ui.document,ui.player,SceneComponentKind::Motor),"Restored component could not be selected.");
    const SceneSnapshot replacement{"assets/scenes/replacement.crimson-scene","Replacement",ui.document.scene().records(),ui.document.scene().properties()};
    require(ui.document.openPreparedSnapshot(replacement),"Could not replace the document with matching entity identities.");
    ui.document.select({ui.player},ui.player);ui.tick();
    require(!ui.selection.component(),"Opening another document retained component selection through a reused entity ID.");
}

void deletionFromInspector(const fs::path& root) {
    ComponentUi ui(root);ui.click({ui.player,SceneComponentKind::Motor});
    const auto point=ui.inspector.center();ui.mouse(point,true);ui.mouse(point,false);ui.tick();
    require(ui.focus=="Inspector"&&ui.selection.component()==SceneComponentKind::Motor,"Inspector focus lost the selected component.");
    const auto before=ui.document.history().size();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete,true);ui.tick();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete,false);ui.tick();
    require(ui.removed&&ui.document.scene().contains(ui.player),"Delete from Inspector erased the component's owning entity.");
    const auto record=*ui.document.scene().find(ui.player);
    require(!record.motor&&record.controller&&record.actor&&record.lightRig,"Component Delete removed unrelated controller or lighting data.");
    require(ui.document.history().size()==before+1,"Component Delete did not create exactly one undo command.");
    require(ui.document.undo()&&ui.document.scene().find(ui.player)->motor,"Undo did not restore the deleted component.");
    ui.tick();require(!ui.selection.component(),"Undo restored a stale component selection.");
    require(ui.selection.select(ui.document,ui.player,SceneComponentKind::Transform),"Transform row could not be selected.");
    require(!ui.selection.erase(ui.document)&&ui.document.scene().contains(ui.player),"Deleting an immutable Transform erased its owner.");
}
void multipleComponentSelection(const fs::path& root) {
    ComponentUi ui(root);ui.click({ui.player,SceneComponentKind::Motor});
    ui.ctrlClick({ui.player,SceneComponentKind::Navigation});
    require(ui.selection.targets().size()==2&&ui.selection.selected(ui.player,SceneComponentKind::Motor)&&
        ui.selection.selected(ui.player,SceneComponentKind::Navigation),"Ctrl-click did not retain multiple selected component rows.");
    require(ui.visible==std::vector{SceneComponentKind::Navigation},"Multiple selected components flooded the Inspector instead of showing its primary.");
    ui.ctrlClick({ui.enemy,SceneComponentKind::LightRig});
    require(ui.selection.targets().size()==3&&ui.document.selection().ids.size()==2&&ui.document.selection().primary==ui.enemy,
        "Component selection across owners lost the selected row identities.");
    require(ui.visible==std::vector{SceneComponentKind::LightRig},"Cross-owner component selection did not keep its primary Inspector target.");
    require(ui.selection.focus(ui.document,ui.player,SceneComponentKind::Motor)&&ui.selection.targets().size()==3,
        "Context focusing an already-selected component collapsed the selected target set.");ui.tick();
    require(ui.visible==std::vector{SceneComponentKind::Motor},"Context focus did not update the primary Inspector component.");
    const auto point=ui.inspector.center();ui.mouse(point,true);ui.mouse(point,false);ui.tick();
    const auto history=ui.document.history().size();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete,true);ui.tick();ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete,false);ui.tick();
    require(ui.removed&&ui.document.scene().contains(ui.player)&&ui.document.scene().contains(ui.enemy),
        "Deleting multiple components erased an owning entity.");
    require(!ui.document.scene().find(ui.player)->motor&&!ui.document.scene().find(ui.player)->navigation&&
        !ui.document.scene().find(ui.enemy)->lightRig&&ui.document.scene().find(ui.player)->controller,
        "Multiple component Delete did not remove precisely the selected dependency closures.");
    require(ui.document.history().size()==history+1,"Multiple component Delete created separate history commands.");
    require(ui.document.undo(),"Multiple component Delete did not undo.");ui.tick();
    require(ui.document.scene().find(ui.player)->motor&&ui.document.scene().find(ui.player)->navigation&&ui.document.scene().find(ui.enemy)->lightRig,
        "Undo did not restore all removed components together.");
    ui.click({ui.player,SceneComponentKind::Motor});ui.ctrlClick({ui.player,SceneComponentKind::Navigation});
    ui.ctrlClick({ui.player,SceneComponentKind::Navigation});
    require(ui.selection.targets().size()==1&&ui.selection.component()==SceneComponentKind::Motor,"Ctrl-click did not deselect an individual component row.");
}

void reorderRows(const fs::path& root) {
    ComponentUi ui(root);ui.click({ui.player,SceneComponentKind::Motor});
    const auto before=ui.document.history().size();
    ui.drag({ui.player,SceneComponentKind::LightRig},{ui.player,SceneComponentKind::Motor});
    const auto order=sceneComponents(*ui.document.scene().find(ui.player));
    const auto rig=std::find(order.begin(),order.end(),SceneComponentKind::LightRig);
    const auto motor=std::find(order.begin(),order.end(),SceneComponentKind::Motor);
    require(ui.reorderCommands==1&&rig<motor,"Dragging a component row did not deliver same-owner reordering.");
    require(ui.document.history().size()==before+1,"A component reorder created more than one history command.");
    require(ui.selection.entity()==ui.player&&ui.selection.component()==SceneComponentKind::LightRig,"Dragging a row lost its selected component.");
    require(ui.document.undo(),"Row reorder did not undo.");ui.tick();
    const auto restored=sceneComponents(*ui.document.scene().find(ui.player));
    require(std::find(restored.begin(),restored.end(),SceneComponentKind::Motor)<std::find(restored.begin(),restored.end(),SceneComponentKind::LightRig),"Undo did not restore component order.");
    const auto commands=ui.reorderCommands;const auto history=ui.document.history().size();
    ui.drag({ui.player,SceneComponentKind::LightRig},{ui.enemy,SceneComponentKind::Motor});
    require(ui.reorderCommands==commands&&ui.document.history().size()==history,"Cross-owner component dragging moved capabilities between actors.");
    const DraggedSceneOutlinerRow payload{&ui.document,ui.document.documentGeneration(),ui.player,SceneOutlinerRow::componentRow(SceneComponentKind::LightRig)};
    require(validSceneOutlinerRowReorder(ui.document,ui.player,payload,SceneOutlinerRow::componentRow(SceneComponentKind::Motor)),"Valid same-owner reorder was rejected.");
    require(!validSceneOutlinerRowReorder(ui.document,ui.player,payload,SceneOutlinerRow::componentRow(SceneComponentKind::Transform)),"Transform accepted a component reorder before it.");
    require(!validSceneOutlinerRowReorder(ui.document,ui.player,payload,payload.row),"Component accepted itself as a reorder target.");
    auto stale=payload;--stale.documentGeneration;
    require(!validSceneOutlinerRowReorder(ui.document,ui.player,stale,SceneOutlinerRow::componentRow(SceneComponentKind::Motor)),"Stale document payload accepted a reorder.");
    SceneDocument other(root);stale=payload;stale.document=&other;
    require(!validSceneOutlinerRowReorder(ui.document,ui.player,stale,SceneOutlinerRow::componentRow(SceneComponentKind::Motor)),"A different document's component payload accepted a reorder.");
}

using MixedKey=std::tuple<EntityId,unsigned,unsigned,EntityId>;
MixedKey rowKey(EntityId owner,SceneOutlinerRow row) {
    return {owner,static_cast<unsigned>(row.kind),static_cast<unsigned>(row.component),row.child};
}
// Uses the same indexed mixed-row planner, component/Camera widgets and edge
// receiver as ScenePanels. Real model and Camera descendants also exercise tree
// scopes: the parent row must remain the drop target before its children draw.
class MixedOutlinerUi {
public:
    explicit MixedOutlinerUi(const fs::path& root):document(root) {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={900,800};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        player=document.createPlayer();camera=resolveOwnedActorCamera(document.scene(),player);
        const auto placed=document.createModels({{"assets/models/visual/manifest.json","Visual model",math::translation({1,0,0})}},player);
        require(placed.size()==1,"Could not prepare real Visual Root child.");model=placed.front();
        marker=document.createEmpty("Camera marker");document.select({marker},marker);
        require(document.reparentSelection(camera),"Could not prepare expanded Camera descendant.");
        document.select({player},player);tick();tick();
    }
    ~MixedOutlinerUi(){ImGui::DestroyContext(context_);}
    void tick() {
        ImGui::NewFrame();rects.clear();sequence.clear();plans.clear();
        records_=document.scene().records();byId_.clear();children_.clear();
        for(const auto& record:records_){byId_[record.id]=&record;children_[record.parent].push_back(&record);}
        for(auto& [parent,children]:children_) {
            static_cast<void>(parent);
            std::sort(children.begin(),children.end(),[](const auto* a,const auto* b){return a->order!=b->order?a->order<b->order:a->id<b->id;});
        }
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({820,740});
        ImGui::Begin("Outliner",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetNextItemOpen(true,ImGuiCond_Always);
        if(ImGui::TreeNodeEx("Player",ImGuiTreeNodeFlags_FramePadding|ImGuiTreeNodeFlags_SpanAvailWidth)) {
            drawChildren(player);ImGui::TreePop();
        }
        ImGui::End();
        for(const auto& action:pending_)if(reorderSceneOutlinerRow(document,action.owner,action.drop.row,action.drop.before))++commands;
        pending_.clear();ImGui::Render();
    }
    std::vector<SceneOutlinerRow> rows(EntityId owner)const{return plans.at(owner);}
    void drag(SceneOutlinerRow source,SceneOutlinerRow target,bool lower) {
        const auto sourceBounds=rects.at(rowKey(player,source)),targetBounds=rects.at(rowKey(player,target));
        const ImVec2 from{sourceBounds.low.x+60,(sourceBounds.low.y+sourceBounds.high.y)*.5f};
        const ImVec2 to{targetBounds.low.x+60,targetBounds.low.y+(targetBounds.high.y-targetBounds.low.y)*(lower?.75f:.25f)};
        mouse(from,true);mouse(to,true);mouse(to,true);mouse(to,false);tick();
    }
    void tightSpacing()const {
        for(std::size_t index=1;index<sequence.size();++index) {
            const auto gap=sequence[index].second.low.y-sequence[index-1].second.high.y;
            require(std::abs(gap-ImGui::GetStyle().ItemSpacing.y)<.01f,
                "The mixed component/Camera Outliner inserted a permanent blank row or unexpected layout gap.");
        }
    }
    SceneDocument document;
    EntityId player{},camera{},model{},marker{};
    std::map<MixedKey,Rect> rects;
    std::vector<std::pair<MixedKey,Rect>> sequence;
    std::map<EntityId,std::vector<SceneOutlinerRow>> plans;
    unsigned commands{};
private:
    struct Pending {EntityId owner{};SceneOutlinerRowDrop drop;};
    void capture(EntityId owner,SceneOutlinerRow row) {
        const auto key=rowKey(owner,row);const Rect bounds{ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
        rects[key]=bounds;sequence.push_back({key,bounds});
    }
    void drawChildren(EntityId owner) {
        const auto record=byId_.at(owner);
        const auto rows=visibleSceneOutlinerRows(*record,children_[owner]);plans[owner]=rows;
        // Snapshot planning must exactly match the public scene boundary.
        require(rows==visibleSceneOutlinerRows(document.scene(),owner),"Indexed Outliner planning changed the mixed Camera/component sequence.");
        for(std::size_t index=0;index<rows.size();++index) {
            const auto row=rows[index];
            const auto next=index+1<rows.size()?std::optional{rows[index+1]}:std::nullopt;
            if(row.kind==SceneOutlinerRowKind::Component) {
                const bool visual=row.component==SceneComponentKind::Actor;
                if(visual)ImGui::SetNextItemOpen(true,ImGuiCond_Always);
                const auto widget=drawSceneComponentRow(document,owner,row.component,false,visual);
                capture(owner,row);
                if(const auto drop=acceptSceneOutlinerRowReorder(document,owner,row,next))pending_.push_back({owner,*drop});
                if(visual&&widget.open) {
                    for(const auto* child:children_[owner])if(!child->camera)drawNestedEntity(owner,*child);
                    ImGui::TreePop();
                }
            } else {
                const auto child=byId_.at(row.child);
                const bool expandable=!visibleSceneOutlinerRows(*child,children_[child->id]).empty();
                if(expandable)ImGui::SetNextItemOpen(true,ImGuiCond_Always);
                const auto widget=drawSceneOutlinerRow(document,owner,row,child->name.c_str(),false,expandable,false);
                capture(owner,row);
                // Production retains its entity-source ownership path; a single
                // actor-owned Camera publishes this same mixed payload.
                if(ImGui::BeginDragDropSource()) {
                    setSceneOutlinerRowDragPayload(document,owner,row);
                    ImGui::TextUnformatted(child->name.c_str());ImGui::EndDragDropSource();
                }
                if(const auto drop=acceptSceneOutlinerRowReorder(document,owner,row,next))pending_.push_back({owner,*drop});
                if(expandable&&widget.open){drawChildren(child->id);ImGui::TreePop();}
            }
        }
    }
    void drawNestedEntity(EntityId owner,const EntityRecord& child) {
        const auto row=SceneOutlinerRow::childRow(child.id);
        drawSceneOutlinerRow(document,owner,row,child.name.c_str(),false,false,false);capture(owner,row);
    }
    void mouse(ImVec2 position,bool down) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(position.x,position.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);tick();
    }
    ImGuiContext* context_{};
    std::vector<EntityRecord> records_;
    std::map<EntityId,const EntityRecord*> byId_;
    std::map<EntityId,std::vector<const EntityRecord*>> children_;
    std::vector<Pending> pending_;
};

void mixedCameraRows(const fs::path& root) {
    MixedOutlinerUi ui(root);ui.tightSpacing();
    const auto camera=SceneOutlinerRow::childRow(ui.camera);
    const auto motor=SceneOutlinerRow::componentRow(SceneComponentKind::Motor);
    const auto reveal=SceneOutlinerRow::componentRow(SceneComponentKind::DarknessReveal);
    const auto rig=SceneOutlinerRow::componentRow(SceneComponentKind::LightRig);
    const auto initial=ui.rows(ui.player);
    require(std::count(initial.begin(),initial.end(),camera)==1&&initial.back()==camera,
        "The real supplied child Camera was absent or duplicated in the mixed Outliner sequence.");
    require(ui.rects.contains(rowKey(ui.player,SceneOutlinerRow::childRow(ui.model)))&&
        ui.rects.contains(rowKey(ui.camera,SceneOutlinerRow::childRow(ui.marker))),
        "The mixed-row fixture did not draw real expanded Visual Root and Camera children.");
    const auto baseline=*ui.document.scene().find(ui.camera);const auto world=ui.document.scene().worldTransform(ui.camera);
    const auto roles=ui.document.scene().properties();const auto generation=ui.document.scene().generation();const auto revision=ui.document.scene().revision();
    const auto preserved=[&] {
        const auto record=*ui.document.scene().find(ui.camera);
        require(record.parent==baseline.parent&&record.local==baseline.local&&record.camera==baseline.camera&&record.cameraFollow==baseline.cameraFollow&&
            ui.document.scene().worldTransform(ui.camera)==world&&sameProperties(ui.document.scene().properties(),roles),
            "Reordering the actual Camera changed ownership, transform, follow settings, lens or active role IDs.");
        require(ui.document.scene().generation()==generation&&ui.document.scene().revision()==revision,
            "Mixed Outliner ordering invalidated the runtime scene.");
        ui.tightSpacing();
    };
    const auto history=ui.document.history().size();
    ui.drag(camera,motor,false);auto order=ui.rows(ui.player);
    require(ui.commands==1&&std::find(order.begin(),order.end(),camera)+1==std::find(order.begin(),order.end(),motor),
        "Upper-half Camera drop did not place the actual child immediately before Motor.");preserved();
    ui.drag(camera,reveal,true);order=ui.rows(ui.player);
    require(ui.commands==2&&order.back()==camera&&std::find(order.begin(),order.end(),reveal)+1==std::find(order.begin(),order.end(),camera),
        "Lower-half drop below the last Darkness Reveal did not move Camera to the end.");preserved();
    ui.drag(rig,camera,false);order=ui.rows(ui.player);
    require(ui.commands==3&&std::find(order.begin(),order.end(),rig)+1==std::find(order.begin(),order.end(),camera),
        "A component could not move immediately before the actual Camera row.");preserved();
    ui.drag(rig,camera,true);order=ui.rows(ui.player);
    require(ui.commands==4&&order.back()==rig&&std::find(order.begin(),order.end(),camera)+1==std::find(order.begin(),order.end(),rig),
        "A lower-half drop after Camera did not move the component to the end.");preserved();
    require(ui.document.history().size()==history+4,"Mixed row gestures did not produce exactly one command each.");
    require(ui.document.undo(),"Mixed row reorder did not undo.");ui.tick();preserved();
    auto restored=ui.rows(ui.player);
    require(std::find(restored.begin(),restored.end(),rig)+1==std::find(restored.begin(),restored.end(),camera),"Mixed row Undo lost the component's Camera-relative position.");
    require(ui.document.redo(),"Mixed row reorder did not redo.");ui.tick();preserved();

    const auto second=ui.document.createCamera("Second Camera");ui.document.select({second},second);
    require(ui.document.reparentSelection(ui.player),"Could not prepare a second actual Camera child.");ui.tick();
    const auto secondRow=SceneOutlinerRow::childRow(second);
    order=ui.rows(ui.player);require(std::count(order.begin(),order.end(),camera)==1&&std::count(order.begin(),order.end(),secondRow)==1,
        "Multiple Camera children shared or lost mixed-row identity.");
    ui.drag(secondRow,motor,false);order=ui.rows(ui.player);ui.tightSpacing();
    require(std::find(order.begin(),order.end(),secondRow)+1==std::find(order.begin(),order.end(),motor)&&
        std::find(order.begin(),order.end(),camera)>std::find(order.begin(),order.end(),motor),
        "Dragging a second Camera moved the first Camera or failed to order the chosen child.");
    const auto firstAfter=*ui.document.scene().find(ui.camera);
    require(firstAfter.parent==baseline.parent&&firstAfter.local==baseline.local&&firstAfter.camera==baseline.camera&&firstAfter.cameraFollow==baseline.cameraFollow&&
        sameProperties(ui.document.scene().properties(),roles),"Second-Camera reordering changed the first Camera or active role IDs.");
}

class CameraUi {
public:
    explicit CameraUi(const fs::path& root):document(root) {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={960,640};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        player=document.createPlayer();camera=resolveOwnedActorCamera(document.scene(),player);
        require(camera!=NoEntity,"Player Inspector did not resolve its supplied camera.");
        playerMatrix=document.scene().find(player)->local;tick();tick();
    }
    ~CameraUi(){ImGui::DestroyContext(context_);}
    void tick() {
        ImGui::NewFrame();
        if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&document.transactionActive()) {
            document.cancel();ImGui::ClearActiveID();active.reset();state={};
        }
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({760,570});
        ImGui::Begin("Inspector",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        auto value=capture();
        drawCameraAuthoringControls(state,camera,value,[&](CameraAuthoringControl control,bool changed) {
            const auto index=static_cast<std::size_t>(control);
            const auto low=ImGui::GetItemRectMin(),high=ImGui::GetItemRectMax();
            const float inputWidth=ImGui::CalcItemWidth();
            const bool vector=control==CameraAuthoringControl::Offset||control==CameraAuthoringControl::Rotation;
            centers[index]={low.x+inputWidth*(vector?1.f/6.f:.5f),(low.y+high.y)*.5f};
            if(ImGui::IsItemActivated()||changed) {
                if(active!=control) {
                    if(document.transactionActive())document.commit();
                    require(document.begin("Edit child camera"),"Inline camera property could not start its gesture.");active=control;
                }
                if(changed)require(updateCameraAuthoring(document,camera,value),"Inline camera property failed to update its child camera.");
            }
            if(active==control&&ImGui::IsItemDeactivated()) {
                document.commit();active.reset();
            }
        });
        ImGui::End();ImGui::Render();
    }
    CameraTemplate capture()const {
        const auto record=document.scene().find(camera);
        const auto value=captureCameraTemplate(document.scene(),camera,record->cameraFollow?record->cameraFollow->target:NoEntity);
        require(value.has_value(),"Could not capture the owned camera's current authoring settings.");return *value;
    }
    void text(CameraAuthoringControl control,const char* value) {
        const auto center=centers[static_cast<std::size_t>(control)];auto& io=ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddMousePosEvent(center.x,center.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();io.AddKeyEvent(ImGuiMod_Ctrl,false);tick();
        require(document.transactionActive(),"The real inline camera numeric field did not activate.");
        io.AddInputCharactersUTF8(value);tick();
    }
    void key(ImGuiKey key) {
        auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);tick();io.AddKeyEvent(key,false);tick();tick();
    }
    void unchangedOwner()const {
        const auto record=*document.scene().find(camera);
        require(document.selection().primary==player&&document.scene().find(player)->local==playerMatrix,
            "Editing the inline camera changed the selected Player or its transform.");
        require(record.parent==player&&record.cameraFollow&&record.cameraFollow->target==player,
            "Editing the inline camera changed its child ownership or follow target.");
    }
    SceneDocument document;
    EntityId player{},camera{};
    Mat4 playerMatrix{};
    CameraAuthoringControlsState state;
    std::array<ImVec2,5> centers{};
    std::optional<CameraAuthoringControl> active;
private:
    ImGuiContext* context_{};
};
bool closeMatrix(const Mat4& first,const Mat4& second) {
    for(std::size_t index=0;index<first.size();++index)if(std::abs(first[index]-second[index])>1e-5f)return false;
    return true;
}
void inlineOwnedCamera(const fs::path& root) {
    CameraUi ui(root);
    const std::pair<CameraAuthoringControl,const char*> changes[]{
        {CameraAuthoringControl::Offset,"9"},{CameraAuthoringControl::Rotation,"-40"},
        {CameraAuthoringControl::Fov,"50"},{CameraAuthoringControl::Near,"0.2"},{CameraAuthoringControl::Far,"300"}};
    for(const auto& [control,text]:changes) {
        const auto before=ui.capture();const auto history=ui.document.history().size();
        ui.text(control,text);require(ui.document.transactionActive(),"Inline camera committed before numeric entry was accepted.");
        const auto live=ui.capture();
        if(control==CameraAuthoringControl::Offset)require(std::abs(live.offset[0]-9.f)<1e-5f,"Inline camera offset did not update the actual camera.");
        else if(control==CameraAuthoringControl::Rotation)require(std::abs(cameraTemplateRotationDegrees(live)[0]+40.f)<.001f,"Inline camera rotation did not update the actual camera.");
        else if(control==CameraAuthoringControl::Fov)require(std::abs(live.lens.verticalFov-50.f)<1e-5f,"Inline camera FOV did not update the actual camera.");
        else if(control==CameraAuthoringControl::Near)require(std::abs(live.lens.nearPlane-.2f)<1e-5f,"Inline camera near plane did not update the actual camera.");
        else require(std::abs(live.lens.farPlane-300.f)<1e-5f,"Inline camera far plane did not update the actual camera.");
        ui.key(ImGuiKey_Enter);ui.unchangedOwner();
        require(!ui.document.transactionActive()&&ui.document.history().size()==history+1,"Inline camera gesture did not produce exactly one command.");
        require(ui.document.undo(),"Inline camera change did not undo.");ui.tick();
        const auto restored=ui.capture();
        require(restored.offset==before.offset&&restored.lens==before.lens&&closeMatrix(restored.worldBasis,before.worldBasis),
            "Undo from Player Inspector did not restore the child's camera settings.");
        require(ui.document.redo(),"Inline camera change did not redo.");ui.tick();ui.unchangedOwner();
    }
    const auto before=ui.capture();const auto history=ui.document.history().size();
    ui.text(CameraAuthoringControl::Rotation,"-75");ui.key(ImGuiKey_Escape);ui.unchangedOwner();
    const auto canceled=ui.capture();
    require(!ui.document.transactionActive()&&ui.document.history().size()==history&&closeMatrix(canceled.worldBasis,before.worldBasis),
        "Escape did not cancel the child camera rotation gesture without adding history.");
    ui.tick();require(closeMatrix(ui.capture().worldBasis,before.worldBasis),"The camera rotation buffer reapplied a canceled value.");
}

class ColorPopupUi {
public:
    explicit ColorPopupUi(const fs::path& root):document(root) {
        context_=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={900,800};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        player=document.createPlayer();camera=resolveOwnedActorCamera(document.scene(),player);enemy=document.createEnemy();
        document.select({player,camera},camera);tick();tick();
    }
    ~ColorPopupUi(){ImGui::DestroyContext(context_);}
    void tick() {
        ImGui::NewFrame();const auto original=ImGui::GetStyle();
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({820,730});
        ImGui::Begin("Outliner",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        auto* draw=ImGui::GetWindowDrawList();const auto begin=draw->VtxBuffer.Size;
        const auto color=sceneOutlinerColor(*document.scene().find(camera));
        drawSceneOutlinerRow(document,player,SceneOutlinerRow::childRow(camera),"Camera",selectedCamera,false,false,color);
        cameraBounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
        cameraVertices=draw->VtxBuffer.Size-begin;colorVertices=0;
        if(color) {
            const auto expected=ImGui::GetColorU32(ImVec4{(*color)[0],(*color)[1],(*color)[2],(*color)[3]});
            for(auto index=begin;index<draw->VtxBuffer.Size;++index)if(draw->VtxBuffer[index].col==expected)++colorVertices;
        }
        const auto playerRecord=*document.scene().find(player);
        drawSceneComponentRow(document,player,SceneComponentKind::Motor,false,false,sceneOutlinerColor(playerRecord,SceneComponentKind::Motor));
        for(unsigned index=0;index<ImGuiCol_COUNT;++index) {
            const auto a=original.Colors[index],b=ImGui::GetStyle().Colors[index];
            require(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w,"Semantic row coloring changed the editor's shared ImGui theme.");
        }
        if(!activate_.empty()) {
            auto& g=*ImGui::GetCurrentContext();
            require(!g.OpenPopupStack.empty()&&g.OpenPopupStack.back().Window,"The actual color popup was not open for its button action.");
            const auto id=g.OpenPopupStack.back().Window->GetID(activate_.c_str());
            g.NavActivateId=id;g.NavActivateDownId=id;g.NavActivatePressedId=id;activate_.clear();
        }
        applied=drawSceneOutlinerColorPopup(document,popup);open=ImGui::IsPopupOpen(SceneOutlinerColorPopup);
        ImGui::End();ImGui::Render();
    }
    void begin(std::vector<SceneOutlinerColorTarget> targets) {
        require(requestSceneOutlinerColor(document,popup,std::move(targets)),"Could not stage the selected Outliner color targets.");tick();tick();
        require(open,"The real Set Color popup did not open.");
    }
    void button(const char* label){activate_=label;tick();tick();}
    void escape(){auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiKey_Escape,true);tick();io.AddKeyEvent(ImGuiKey_Escape,false);tick();}
    void hoverCamera() {const auto point=cameraBounds.center();ImGui::GetIO().AddMousePosEvent(point.x,point.y);tick();tick();}
    SceneDocument document;
    EntityId player{},camera{},enemy{};
    SceneOutlinerColorPopupState popup;
    Rect cameraBounds;
    bool selectedCamera{},applied{},open{};
    int colorVertices{},cameraVertices{};
private:
    ImGuiContext* context_{};
    std::string activate_;
};
void outlinerColorPopup(const fs::path& root) {
    ColorPopupUi ui(root);const Vec4 entityColor{.16f,.48f,.72f,.8f};
    SceneComponentSelection contextSelection;
    require(contextSelection.focusEntity(ui.document,ui.player)&&ui.document.selection().ids.size()==2&&ui.document.selection().primary==ui.player,
        "Right-click focusing a selected entity collapsed its entity/Camera multi-selection.");
    const auto history=ui.document.history().size();const auto generation=ui.document.scene().generation();const auto revision=ui.document.scene().revision();
    const auto cameraBefore=*ui.document.scene().find(ui.camera);const auto roles=ui.document.scene().properties();
    ui.begin({{ui.player,std::nullopt},{ui.camera,std::nullopt}});ui.popup.color=entityColor;ui.tick();
    require(!sceneOutlinerColor(*ui.document.scene().find(ui.player))&&!sceneOutlinerColor(*ui.document.scene().find(ui.camera))&&
        ui.document.history().size()==history,"Editing the color picker authored metadata before Apply.");
    // Changing current selection cannot redirect a popup's frozen identities.
    ui.document.select({ui.enemy},ui.enemy);ui.button("Apply");
    require(!ui.open&&sceneOutlinerColor(*ui.document.scene().find(ui.player))==entityColor&&sceneOutlinerColor(*ui.document.scene().find(ui.camera))==entityColor,
        "Apply did not color both originally selected entity/Camera rows.");
    require(!sceneOutlinerColor(*ui.document.scene().find(ui.enemy))&&!sceneOutlinerColor(*ui.document.scene().find(ui.player),SceneComponentKind::Motor),
        "Entity color leaked to a later selection or inherited into component rows.");
    require(ui.document.history().size()==history+1&&ui.document.scene().generation()==generation&&ui.document.scene().revision()==revision,
        "Applying row colors created multiple commands or invalidated the runtime scene.");
    const auto cameraAfter=*ui.document.scene().find(ui.camera);
    require(cameraAfter.parent==cameraBefore.parent&&cameraAfter.local==cameraBefore.local&&cameraAfter.camera==cameraBefore.camera&&cameraAfter.cameraFollow==cameraBefore.cameraFollow&&
        sameProperties(ui.document.scene().properties(),roles),"Coloring Camera changed its actual scene behavior or role IDs.");
    ui.selectedCamera=false;ui.tick();const auto normalVertices=ui.cameraVertices;
    require(ui.colorVertices>=4,"An unselected actual Camera row did not draw its authored background.");
    ui.selectedCamera=true;ui.hoverCamera();
    require(ui.colorVertices>=4&&ui.cameraVertices>normalVertices,"Selected/hovered Camera lost its authored tint or a distinct selection cue.");
    require(ui.document.undo(),"Outliner entity colors did not undo.");ui.tick();
    require(!sceneOutlinerColor(*ui.document.scene().find(ui.player))&&!sceneOutlinerColor(*ui.document.scene().find(ui.camera)),"Undo did not restore both entity row colors together.");
    require(ui.document.redo(),"Outliner entity colors did not redo.");ui.tick();

    const auto beforeCancel=ui.document.history().size();
    ui.begin({{ui.player,SceneComponentKind::Motor}});ui.popup.color={1,0,0,1};ui.tick();ui.button("Cancel");
    require(!ui.open&&!sceneOutlinerColor(*ui.document.scene().find(ui.player),SceneComponentKind::Motor)&&ui.document.history().size()==beforeCancel,
        "Cancel authored a staged component color.");
    ui.begin({{ui.player,SceneComponentKind::DarknessReveal}});ui.popup.color={0,1,0,1};ui.tick();ui.escape();
    require(!ui.open&&!sceneOutlinerColor(*ui.document.scene().find(ui.player),SceneComponentKind::DarknessReveal)&&ui.document.history().size()==beforeCancel,
        "Escape authored a staged component color.");

    SceneComponentSelection selected;
    require(selected.select(ui.document,ui.player,SceneComponentKind::Motor)&&
        selected.select(ui.document,ui.player,SceneComponentKind::Navigation,true)&&
        selected.select(ui.document,ui.enemy,SceneComponentKind::LightRig,true),"Could not prepare multiple explicit component color targets.");
    const auto targets=selected.targets();ui.begin(std::vector<SceneOutlinerColorTarget>(targets.begin(),targets.end()));
    const Vec4 componentColor{.9f,.75f,.12f,1};ui.popup.color=componentColor;ui.tick();selected.clear();ui.document.select({ui.camera},ui.camera);ui.button("Apply");
    require(sceneOutlinerColor(*ui.document.scene().find(ui.player),SceneComponentKind::Motor)==componentColor&&
        sceneOutlinerColor(*ui.document.scene().find(ui.player),SceneComponentKind::Navigation)==componentColor&&
        sceneOutlinerColor(*ui.document.scene().find(ui.enemy),SceneComponentKind::LightRig)==componentColor,
        "Apply lost a frozen Ctrl-selected component color target.");
    require(sceneOutlinerColor(*ui.document.scene().find(ui.player))==entityColor&&
        !sceneOutlinerColor(*ui.document.scene().find(ui.player),SceneComponentKind::Controller)&&!sceneOutlinerColor(*ui.document.scene().find(ui.enemy)),
        "Component colors affected their owning entity or unrelated component rows.");
    require(selected.select(ui.document,ui.player,SceneComponentKind::Motor)&&selected.select(ui.document,ui.enemy,SceneComponentKind::LightRig,true),
        "Could not prepare component-selection exit fixture.");
    require(selected.focusEntity(ui.document,ui.camera)&&!selected.component()&&ui.document.selection().ids==std::vector{ui.camera},
        "Right-clicking an entity did not exit component mode with an explicit entity target.");
    ui.begin({{ui.camera,std::nullopt}});ui.popup.color={1,0,1,1};
    const SceneSnapshot replacement{"assets/scenes/color-replacement.crimson-scene","Replacement",ui.document.scene().records(),ui.document.scene().properties()};
    require(ui.document.openPreparedSnapshot(replacement),"Could not replace a scene during a staged color edit.");ui.tick();
    require(!ui.open&&sceneOutlinerColor(*ui.document.scene().find(ui.camera))==entityColor,
        "A stale color popup mutated reused identities after document replacement.");
}
}

int main() {
    try {
        Project project;selectionAndInspector(project.root);selectionLifetime(project.root);
        deletionFromInspector(project.root);multipleComponentSelection(project.root);reorderRows(project.root);mixedCameraRows(project.root);
        inlineOwnedCamera(project.root);outlinerColorPopup(project.root);
        std::cout<<"Outliner selection, mixed Camera/component ordering, staged row colors and inline camera authoring passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
