#include "tools/SceneInspectorSections.h"
#include "tools/InspectorTransform.h"
#include "tools/CameraAuthoringControls.h"
#include "tools/SceneOutlinerColor.h"
#include "editor/SceneShortcuts.h"
#include "scene/CameraAuthoring.h"
#include "scene/BuiltinModels.h"

#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <numbers>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/
        ("crimson-entity-ui-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-entity-ui-test-"))fs::remove_all(target,error);
    }
};
struct Rect{ImVec2 low{},high{};ImVec2 center()const{return {(low.x+high.x)*.5f,(low.y+high.y)*.5f};}};
struct Context {
    Context(){
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1100,900};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    }
    ~Context(){ImGui::DestroyContext(context);}
    ImGuiContext* context{};
};

// Only a private CPU ImGui context receives these events. No native window,
// desktop input, SDL renderer, clipboard or authored project is accessed.
class EntityUi {
public:
    explicit EntityUi(const fs::path& root):document(root){
        point=document.createPointLight();player=document.createPlayer();
        const auto models=document.createModels({{BuiltinBoxModelId,"Body",math::translation({.2f,.3f,-.4f})}},player);
        require(models.size()==1,"Could not create an owned Player visual.");model=models.front();
        camera=resolveOwnedActorCamera(document.scene(),player);document.select({point},point);tick();tick();
    }
    void tick(){
        ImGui::NewFrame();rects.clear();sections.clear();
        constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings;
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({400,840});ImGui::Begin("Outliner",nullptr,flags);
        rows=sceneOutlinerRows(document.scene());
        std::unordered_map<EntityId,std::vector<EntityId>> children;
        for(const auto& row:rows)children[row.parent].push_back(row.entity);
        struct Work{EntityId id{};bool close{};};std::vector<Work> pending;
        for(auto it=children[NoEntity].rbegin();it!=children[NoEntity].rend();++it)pending.push_back({*it});
        while(!pending.empty()){
            const auto work=pending.back();pending.pop_back();if(work.close){ImGui::TreePop();continue;}
            const auto record=document.scene().find(work.id);require(record.has_value(),"An Outliner row has no entity.");
            const bool selected=std::find(document.selection().ids.begin(),document.selection().ids.end(),work.id)!=document.selection().ids.end();
            const bool leaf=children[work.id].empty();
            auto rowFlags=ImGuiTreeNodeFlags_OpenOnArrow|ImGuiTreeNodeFlags_SpanAvailWidth|ImGuiTreeNodeFlags_FramePadding;
            if(selected)rowFlags|=ImGuiTreeNodeFlags_Selected;
            if(leaf)rowFlags|=ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;
            else ImGui::SetNextItemOpen(true,ImGuiCond_Once);
            bool open{};
            {
                SceneOutlinerRowColorScope color(sceneOutlinerColor(*record));
                const auto id=std::to_string(work.id);open=ImGui::TreeNodeEx(id.c_str(),rowFlags,"%s",record->name.c_str());color.finish(selected);
            }
            if(ImGui::IsItemClicked()&&!ImGui::IsItemToggledOpen()){
                auto ids=ImGui::GetIO().KeyCtrl?document.selection().ids:std::vector<EntityId>{};
                const auto existing=std::find(ids.begin(),ids.end(),work.id);
                if(existing==ids.end())ids.push_back(work.id);else ids.erase(existing);
                document.select(std::move(ids),work.id);
            }
            rects[work.id]={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
            if(open&&!leaf){
                pending.push_back({work.id,true});
                for(auto it=children[work.id].rbegin();it!=children[work.id].rend();++it)pending.push_back({*it});
            }
        }
        ImGui::End();
        ImGui::SetNextWindowPos({450,20});ImGui::SetNextWindowSize({620,840});ImGui::Begin("Inspector",nullptr,flags);
        for(const auto id:sceneEntityParts(document.scene(),document.selection().primary)){
            const auto record=*document.scene().find(id);
            for(const auto section:sceneInspectorSections(record)){
                sections.push_back({id,section});ImGui::TextUnformatted(sceneComponentName(section));
            }
        }
        ImGui::InvisibleButton("Inspector focus",{500,220});inspector={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
        ImGui::End();
        const auto shortcuts=readSceneShortcuts({.transactionActive=document.transactionActive()});
        if(shortcuts.eraseSelection)erased=document.eraseSelection();
        ImGui::Render();
    }
    void click(EntityId entity,bool ctrl=false){
        const auto center=rects.at(entity).center();auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,ctrl);
        io.AddMousePosEvent(center.x,center.y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();io.AddKeyEvent(ImGuiMod_Ctrl,false);tick();
    }
    void focusInspector(){const auto center=inspector.center();auto& io=ImGui::GetIO();io.AddMousePosEvent(center.x,center.y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();}
    void toggle(EntityId entity){
        const auto rect=rects.at(entity);auto& io=ImGui::GetIO();
        io.AddMousePosEvent(rect.low.x+ImGui::GetFontSize()*.5f,rect.center().y);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();tick();
    }
    void remove(){auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiKey_Delete,true);tick();io.AddKeyEvent(ImGuiKey_Delete,false);tick();}
    bool has(EntityId id,SceneComponentKind component)const{
        return std::find(sections.begin(),sections.end(),std::pair{id,component})!=sections.end();
    }
    SceneDocument document;
    EntityId point{},player{},model{},camera{};
    std::vector<SceneOutlinerRow> rows;
    std::map<EntityId,Rect> rects;
    std::vector<std::pair<EntityId,SceneComponentKind>> sections;
    Rect inspector;
    bool erased{};
private:
    Context context_;
};

void directEntitySelectionAndSections(const fs::path& root){
    EntityUi ui(root);
    require(ui.rows.size()==3&&ui.rects.contains(ui.point)&&ui.rects.contains(ui.player)&&
        ui.rects.contains(ui.model)&&!ui.rects.contains(ui.camera),"The Outliner hid a real Player model or exposed a synthetic settings row.");
    ui.click(ui.model);
    require(ui.document.selection().primary==ui.model&&ui.has(ui.model,SceneComponentKind::Model)&&
        !ui.has(ui.player,SceneComponentKind::Controller),"Selecting an attached model redirected its Inspector to Player.");
    ui.click(ui.player);ui.click(ui.point);
    require(ui.document.selection().primary==ui.point&&ui.sections==
        std::vector<std::pair<EntityId,SceneComponentKind>>{{ui.point,SceneComponentKind::Transform},{ui.point,SceneComponentKind::Point}},
        "Selecting a Point Light did not expose its Transform and light properties directly.");
    ui.click(ui.player);
    for(const auto kind:{SceneComponentKind::Controller,SceneComponentKind::Motor,SceneComponentKind::Navigation,
                        SceneComponentKind::PlayerInput,SceneComponentKind::LightRig,SceneComponentKind::DarknessReveal})
        require(ui.has(ui.player,kind),"Selecting Player hid one of its capability subsections.");
    require(ui.has(ui.model,SceneComponentKind::Transform)&&ui.has(ui.model,SceneComponentKind::Model)&&
        ui.has(ui.camera,SceneComponentKind::Camera)&&!ui.has(ui.camera,SceneComponentKind::CameraFollow)&&
        !ui.has(ui.player,SceneComponentKind::Actor),"Player did not expose its owned visual/Camera in consolidated subsections.");
    const auto expected=std::vector{SceneComponentKind::Transform,SceneComponentKind::Controller,SceneComponentKind::Motor,
        SceneComponentKind::Navigation,SceneComponentKind::PlayerInput,SceneComponentKind::LightRig,SceneComponentKind::DarknessReveal};
    require(sceneInspectorSections(*ui.document.scene().find(ui.player))==expected,
        "Player Inspector subsection ordering changed or retained an empty ownership subsection.");
    const auto history=ui.document.history().size();ui.focusInspector();ui.remove();
    require(ui.erased&&!ui.document.scene().contains(ui.player)&&!ui.document.scene().contains(ui.camera)&&
        !ui.document.scene().contains(ui.model)&&ui.document.scene().contains(ui.point),
        "Delete from the entity Inspector removed a capability or left Player internals behind.");
    require(ui.document.history().size()==history+1&&ui.document.undo(),"Consolidated entity deletion did not create one undo command.");
    ui.tick();ui.click(ui.player);
    require(ui.has(ui.camera,SceneComponentKind::Camera)&&ui.has(ui.model,SceneComponentKind::Model),
        "Undo did not restore the Player and its complete Inspector.");
    ui.click(ui.point,true);require(ui.document.selection().ids.size()==2,"Ctrl-click did not retain entity multi-selection.");
    ui.focusInspector();ui.remove();require(ui.document.scene().records().empty(),"Entity multi-selection Delete left capability or visual records behind.");
}

void preserveRealHierarchy(const fs::path& root){
    EntityUi ui(root);const auto quader=ui.document.createQuader("Quader fixture");
    const auto imported=ui.document.createEmpty("Imported");ui.document.select({imported},imported);
    require(ui.document.reparentSelection(quader),"Cannot prepare real Quader import parent.");
    const auto models=ui.document.createModels({{BuiltinBoxModelId,"Imported floor",math::translation({2,0,-1})}},imported);
    require(models.size()==1,"Cannot prepare real imported model.");const auto model=models.front();
    const auto collider=ui.document.createCollider("Floor collision",model);require(collider!=NoEntity,"Cannot prepare real model collision child.");
    const auto records=ui.document.scene().records();const auto roles=ui.document.scene().properties();
    const auto world=*ui.document.scene().worldTransform(collider);const auto revision=ui.document.scene().revision();
    ui.tick();ui.tick();
    require(ui.rows.size()==7&&ui.rects.size()==7,"Real imported entities were duplicated into component rows or hidden.");
    const auto parent=[&](EntityId entity){
        const auto found=std::find_if(ui.rows.begin(),ui.rows.end(),[entity](const auto& row){return row.entity==entity;});
        require(found!=ui.rows.end(),"Real imported hierarchy entity has no Outliner row.");return found->parent;
    };
    require(parent(quader)==NoEntity&&parent(imported)==quader&&parent(model)==imported&&parent(collider)==model,
        "Removing synthetic component rows flattened the real Quader/import/model hierarchy.");
    ui.click(collider);
    require(ui.document.selection().primary==collider&&ui.has(collider,SceneComponentKind::Transform)&&ui.has(collider,SceneComponentKind::Collider),
        "Selecting a real nested Collider did not expose its properties directly.");
    ui.toggle(quader);
    require(ui.rects.contains(quader)&&!ui.rects.contains(imported)&&!ui.rects.contains(model)&&!ui.rects.contains(collider)&&
        ui.document.selection().primary==collider,"Collapsing a real hierarchy failed to hide descendants or changed the selected entity.");
    ui.toggle(quader);ui.click(model);
    require(ui.rects.contains(imported)&&ui.rects.contains(model)&&ui.rects.contains(collider)&&
        ui.document.selection().primary==model&&ui.has(model,SceneComponentKind::Model),
        "Reopening an imported hierarchy lost nested entity selection or direct Model properties.");
    const auto after=ui.document.scene().records();require(after.size()==records.size(),"Outliner hierarchy drawing changed entity count.");
    for(std::size_t index=0;index<after.size();++index)require(sameEntity(after[index],records[index]),"Outliner hierarchy drawing changed authored entity data.");
    require(ui.document.scene().revision()==revision&&*ui.document.scene().worldTransform(collider)==world&&
        sameProperties(ui.document.scene().properties(),roles),"Outliner hierarchy selection changed transforms, roles or runtime scheduling.");
}

void directEnvironmentProperties(const fs::path& root){
    EntityUi ui(root);const auto environment=ui.document.createEnvironment("Environment fixture");ui.tick();ui.tick();
    require(ui.rows.size()==4&&std::count_if(ui.rows.begin(),ui.rows.end(),[environment](const auto& row){return row.entity==environment;})==1,
        "Environment acquired a duplicate selectable settings row.");
    ui.click(ui.point);ui.click(environment);
    require(ui.document.selection().primary==environment&&ui.sections==
        std::vector<std::pair<EntityId,SceneComponentKind>>{{environment,SceneComponentKind::Transform},{environment,SceneComponentKind::Environment}},
        "Selecting Environment did not expose its complete properties directly.");
}
void reparentedPlayerChildrenStaySelectable(const fs::path& root){
    EntityUi ui(root);const auto box=ui.document.createBox();ui.tick();ui.tick();ui.click(box);
    require(ui.document.reparentSelection(ui.player),"Could not parent a real Box to Player.");ui.tick();ui.tick();
    require(ui.rects.contains(box)&&ui.document.selection().primary==box&&ui.has(box,SceneComponentKind::Model),
        "Parenting a Box to Player hid its row, selected its parent or removed its direct properties.");
    ui.click(ui.point);ui.click(box);
    require(ui.document.selection().primary==box&&ui.has(box,SceneComponentKind::Transform)&&ui.has(box,SceneComponentKind::Model)&&
        !ui.has(ui.player,SceneComponentKind::Controller),"A real Player model child was not independently selectable.");
    const auto particle=ui.document.createParticle("assets/particles/attached.crimson-particle","Attached particles");
    require(particle!=NoEntity&&ui.document.reparentSelection(box),"Could not attach a real particle entity below the Box.");
    ui.tick();ui.tick();ui.click(particle);
    require(ui.document.selection().primary==particle&&ui.has(particle,SceneComponentKind::Particle)&&
        std::find(ui.rows.begin(),ui.rows.end(),SceneOutlinerRow{particle,box})!=ui.rows.end(),
        "A Player model's real particle child was folded into Player.");
    ui.toggle(ui.player);
    require(!ui.rects.contains(box)&&!ui.rects.contains(particle),"Collapsing Player left its real child rows detached.");
    ui.toggle(ui.player);ui.click(box);
    require(ui.rects.contains(box)&&ui.rects.contains(particle)&&ui.document.selection().primary==box,
        "Reopening Player lost real model/particle hierarchy or selection.");
}

class VisualTransformUi {
public:
    explicit VisualTransformUi(const fs::path& root):document(root){
        player=document.createPlayer();const auto local=math::translation({.2f,.3f,-.4f})*
            math::eulerRotationXYZ({.15f,.3f,-.2f})*math::scaling({.7f,1.3f,.9f});
        const auto models=document.createModels({{BuiltinBoxModelId,"Body",local}},player);require(models.size()==1,"Cannot create owned visual.");
        visual=models.front();document.select({player},player);playerBefore=*document.scene().find(player);
        camera=resolveOwnedActorCamera(document.scene(),player);cameraBefore=*document.scene().find(camera);tick();tick();
    }
    void tick(bool collapse=false){
        ImGui::NewFrame();
        if(ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&document.transactionActive()){
            document.cancel();ImGui::ClearActiveID();state={};
        }
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({760,700});ImGui::Begin("Inspector",nullptr,
            ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        if(activate_>=0){auto& g=*ImGui::GetCurrentContext();g.NavActivateId=fieldId(static_cast<unsigned>(activate_));
            g.NavActivateFlags=ImGuiActivateFlags_PreferInput;activate_=-1;}
        const InspectorTransformTransactions transactions{
            [&](unsigned,const char* label){return document.beginTransform(label,{visual});},
            [&](unsigned){document.commit();}};
        if(collapse)ImGui::SetNextItemOpen(false,ImGuiCond_Always);
        drawInspectorTransform(document,state,*document.scene().find(visual),transactions);
        ImGui::End();ImGui::Render();
    }
    void text(unsigned field,const char* text){
        activate_=static_cast<int>(field);tick();require(document.transactionActive()&&state.editing&&
            document.transformEditRoots()==std::vector<EntityId>{visual},"Owned visual numeric entry locked the Player instead of its visual.");
        ImGui::GetIO().AddInputCharactersUTF8(text);tick();
    }
    void key(ImGuiKey key){auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);tick();io.AddKeyEvent(key,false);tick();tick();}
    void ownerUnchanged()const{
        require(document.selection().primary==player&&sameEntity(*document.scene().find(player),playerBefore)&&
            sameEntity(*document.scene().find(camera),cameraBefore),"Editing an owned visual changed the Player pose, selection or Camera.");
    }
    SceneDocument document;
    EntityId player{},visual{},camera{};
    EntityRecord playerBefore,cameraBefore;
    InspectorTransformState state;
private:
    ImGuiID fieldId(unsigned field)const{
        auto* window=ImGui::GetCurrentWindow();const auto outer=window->GetID("Transform properties");const int property=static_cast<int>(field/3);
        const auto propertyId=ImHashData(&property,sizeof(property),outer);const auto table=ImHashStr("Axes",0,propertyId);
        const int axis=static_cast<int>(field%3);const auto row=ImHashData(&axis,sizeof(axis),table);return ImHashStr("##value",0,row);
    }
    Context context_;
    int activate_{-1};
};
void inlineVisualTransform(const fs::path& root){
    VisualTransformUi ui(root);
    for(const auto& [field,text]:std::array<std::pair<unsigned,const char*>,3>{{{0,"4"},{4,"35"},{6,"1.75"}}}){
        const auto before=*ui.document.scene().find(ui.visual);const auto history=ui.document.history().size();
        ui.text(field,text);ui.ownerUnchanged();const auto changed=*ui.document.scene().find(ui.visual);
        require(changed.local!=before.local,"The owned visual numeric widget did not update its local offset/alignment.");
        const auto baseline=math::affineTransformFields(before.local);require(baseline.has_value(),"Could not decompose the visual fixture.");
        auto requested=field==0?baseline->position:field==4?baseline->rotationRadians:baseline->scale;
        requested[field%3]=field==0?4.f:field==4?35.f*std::numbers::pi_v<float>/180.f:1.75f;
        const auto expectedLocal=field==0?math::withAffinePosition(before.local,requested):
            field==4?math::withAffineRotation(before.local,*baseline,requested):math::withAffineScale(before.local,*baseline,requested);
        require(expectedLocal.has_value(),"Could not construct the expected visual gesture.");
        for(unsigned index=0;index<16;++index)require(std::abs(changed.local[index]-(*expectedLocal)[index])<1e-4f,
            "The visual numeric widget did not preserve the requested local offset, rotation or scale.");
        auto expected=before;expected.local=changed.local;
        require(sameEntity(expected,changed),"A visual transform gesture changed material, animation or other component data.");
        ui.key(ImGuiKey_Enter);ui.ownerUnchanged();
        require(!ui.document.transactionActive()&&ui.document.history().size()==history+1,"Owned visual gesture did not commit exactly one command.");
        require(ui.document.undo()&&sameEntity(*ui.document.scene().find(ui.visual),before),"Visual transform Undo did not restore the complete child.");
        ui.tick();require(ui.document.redo(),"Visual transform did not redo.");ui.tick();ui.ownerUnchanged();
    }
    const auto before=*ui.document.scene().find(ui.visual);const auto history=ui.document.history().size();
    ui.text(0,"8");ui.key(ImGuiKey_Escape);ui.ownerUnchanged();
    require(sameEntity(*ui.document.scene().find(ui.visual),before)&&ui.document.history().size()==history&&!ui.document.transactionActive(),
        "Canceling an owned visual offset did not restore it without history.");
    ui.tick();require(sameEntity(*ui.document.scene().find(ui.visual),before),"A canceled visual buffer reapplied its value.");
    ui.text(0,"6");ui.tick(true);ui.ownerUnchanged();
    require(!ui.document.transactionActive()&&ui.document.history().size()==history+1,"Collapsing an active visual Transform did not finish its gesture.");
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
        require(sceneOutlinerRows(document.scene())==std::vector<SceneOutlinerRow>{{player,NoEntity}}&&
            sceneEntityOwner(document.scene(),camera)==player&&sceneEntityParts(document.scene(),player)==std::vector<EntityId>{player,camera},
            "Selecting the consolidated Player would expose a separate Camera row.");
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
    explicit ColorPopupUi(const fs::path& root):document(root){
        first=document.createPointLight("First"),second=document.createPointLight("Second"),other=document.createPointLight("Other");
        document.select({first,second},second);tick();tick();
    }
    void tick(){
        ImGui::NewFrame();const auto original=ImGui::GetStyle();
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({820,800});ImGui::Begin("Outliner",nullptr,
            ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        auto* draw=ImGui::GetWindowDrawList();const auto begin=draw->VtxBuffer.Size;const auto color=sceneOutlinerColor(*document.scene().find(first));
        {
            SceneOutlinerRowColorScope appearance(color);
            ImGui::Selectable("First",selected);appearance.finish(selected);bounds={ImGui::GetItemRectMin(),ImGui::GetItemRectMax()};
        }
        vertices=draw->VtxBuffer.Size-begin;colorVertices=0;
        if(color){
            const auto expected=ImGui::GetColorU32(ImVec4{(*color)[0],(*color)[1],(*color)[2],(*color)[3]});
            for(auto index=begin;index<draw->VtxBuffer.Size;++index)if(draw->VtxBuffer[index].col==expected)++colorVertices;
        }
        for(unsigned index=0;index<ImGuiCol_COUNT;++index){
            const auto a=original.Colors[index],b=ImGui::GetStyle().Colors[index];
            require(a.x==b.x&&a.y==b.y&&a.z==b.z&&a.w==b.w,"Entity row tint changed the shared ImGui theme.");
        }
        if(!activate_.empty()){
            auto& g=*ImGui::GetCurrentContext();require(!g.OpenPopupStack.empty()&&g.OpenPopupStack.back().Window,"No color popup for its action.");
            const auto id=g.OpenPopupStack.back().Window->GetID(activate_.c_str());
            g.NavActivateId=id;g.NavActivateDownId=id;g.NavActivatePressedId=id;activate_.clear();
        }
        drawSceneOutlinerColorPopup(document,popup);open=ImGui::IsPopupOpen(SceneOutlinerColorPopup);
        ImGui::End();ImGui::Render();
    }
    void begin(std::vector<SceneOutlinerColorTarget> targets){
        require(requestSceneOutlinerColor(document,popup,std::move(targets)),"Cannot stage entity color targets.");tick();tick();
        require(open,"The actual entity color popup did not open.");
    }
    void button(const char* label){activate_=label;tick();tick();}
    void escape(){auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiKey_Escape,true);tick();io.AddKeyEvent(ImGuiKey_Escape,false);tick();}
    SceneDocument document;
    EntityId first{},second{},other{};
    SceneOutlinerColorPopupState popup;
    Rect bounds;
    bool selected{},open{};
    int vertices{},colorVertices{};
private:
    Context context_;
    std::string activate_;
};
void entityColorPopup(const fs::path& root){
    ColorPopupUi ui(root);const Vec4 color{.16f,.48f,.72f,.8f};
    const auto history=ui.document.history().size(),revision=ui.document.scene().revision();
    ui.begin({{ui.first},{ui.second}});ui.popup.color=color;ui.tick();
    require(!sceneOutlinerColor(*ui.document.scene().find(ui.first))&&!sceneOutlinerColor(*ui.document.scene().find(ui.second))&&
        ui.document.history().size()==history,"Staged entity tint authored a value before Apply.");
    ui.document.select({ui.other},ui.other);ui.button("Apply");
    require(!ui.open&&sceneOutlinerColor(*ui.document.scene().find(ui.first))==color&&
        sceneOutlinerColor(*ui.document.scene().find(ui.second))==color&&!sceneOutlinerColor(*ui.document.scene().find(ui.other)),
        "Apply did not preserve its original entity targets after selection changed.");
    require(ui.document.history().size()==history+1&&ui.document.scene().revision()==revision,
        "Entity row tint changed runtime scheduling or created several history commands.");
    require(ui.colorVertices>=4,"Entity row did not draw its explicit tint.");
    const auto plainVertices=ui.vertices;ui.selected=true;const auto center=ui.bounds.center();
    ImGui::GetIO().AddMousePosEvent(center.x,center.y);ui.tick();
    require(ui.colorVertices>=4&&ui.vertices>plainVertices,"Selected entity row lost tint or a readable selection cue.");
    require(ui.document.undo(),"Entity color did not undo.");ui.tick();
    require(!sceneOutlinerColor(*ui.document.scene().find(ui.first))&&!sceneOutlinerColor(*ui.document.scene().find(ui.second)),
        "Undo did not restore entity colors atomically.");
    require(ui.document.redo(),"Entity color did not redo.");ui.tick();
    const auto beforeCancel=ui.document.history().size();
    ui.begin({{ui.first}});ui.popup.color={1,0,0,1};ui.tick();ui.button("Cancel");
    require(!ui.open&&sceneOutlinerColor(*ui.document.scene().find(ui.first))==color&&ui.document.history().size()==beforeCancel,
        "Cancel committed a staged entity tint.");
    ui.begin({{ui.first}});ui.popup.color={0,1,0,1};ui.tick();ui.escape();
    require(!ui.open&&sceneOutlinerColor(*ui.document.scene().find(ui.first))==color&&ui.document.history().size()==beforeCancel,
        "Escape committed a staged entity tint.");
    ui.begin({{ui.first}});ui.popup.color={1,0,1,1};
    const SceneSnapshot replacement{"assets/scenes/color-replacement.crimson-scene","Replacement",ui.document.scene().records(),ui.document.scene().properties()};
    require(ui.document.openPreparedSnapshot(replacement),"Cannot replace a scene during staged entity tint.");ui.tick();
    require(!ui.open&&sceneOutlinerColor(*ui.document.scene().find(ui.first))==color,
        "Stale entity popup edited reused identities in a replacement document.");
}
}

int main(){
    try{
        Project project;directEntitySelectionAndSections(project.root);directEnvironmentProperties(project.root);
        preserveRealHierarchy(project.root);reparentedPlayerChildrenStaySelectable(project.root);inlineVisualTransform(project.root);
        inlineOwnedCamera(project.root);entityColorPopup(project.root);
        std::cout<<"Direct entity selection, retained hierarchy, complete Inspector sections, owned visual/Camera gestures and entity colors passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
