#include "tools/CollisionFilterControls.h"
#include "tools/CollisionLayersEditor.h"
#include "scene/SceneDocument.h"
#include <imgui_internal.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/
        ("crimson-collision-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){
        std::error_code error;const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-collision-ui-"))fs::remove_all(target,error);
    }
};
struct Context {
    Context(){
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1000,900};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    }
    ~Context(){ImGui::DestroyContext(context);}
    ImGuiContext* context{};
};
void activate(ImGuiID id){
    auto& context=*ImGui::GetCurrentContext();context.NavActivateId=id;
    context.NavActivateDownId=id;context.NavActivatePressedId=id;
}

// These fixtures send input only to a private CPU ImGui context. They never
// create a native window, access SDL, send desktop input or open authored files.
class MaskUi {
public:
    MaskUi(){tick();tick();}
    void tick(){
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({500,700});
        ImGui::Begin("Collision controls",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        if(openNext_){activate(ImGui::GetID("Mask"));openNext_=false;}
        if(activateSlot_>=0){
            const auto* popup=popupWindow();require(popup,"The mask popup disappeared before its checkbox was activated.");
            const auto slot=activateSlot_;const auto scope=ImHashData(&slot,sizeof(slot),popup->ID);
            activate(ImHashStr("##layer",0,scope));activateSlot_=-1;
        }
        changed=drawCollisionMaskControl("Mask",value,names,editable,"%s is reserved for shared level collision.");
        ImGui::End();ImGui::Render();
        require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"The mask picker left invalid ImGui state.");
    }
    void open(){openNext_=true;tick();tick();require(popupWindow(),"The real mask popup did not open.");}
    void checkbox(unsigned slot){
        auto* popup=popupWindow();require(popup,"Cannot activate a closed mask popup.");
        ImGui::SetScrollY(popup,slot<8?0.f:popup->ScrollMax.y);tick();tick();
        activateSlot_=static_cast<int>(slot);tick();
    }
    ImGuiWindow* popupWindow()const{
        const auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
        return popups.empty()?nullptr:popups.back().Window;
    }
    CollisionLayerNames names=defaultCollisionLayerNames();
    std::uint32_t value=WorldCollisionLayer,editable=~WorldCollisionLayer;
    bool changed{};
private:
    Context context_;bool openNext_{};int activateSlot_{-1};
};
void maskLocksAndFullWidth(){
    MaskUi ui;ui.names[0]="Terrain";ui.names[31]="High bit";ui.open();
    ui.checkbox(0);require(ui.value==WorldCollisionLayer&&!ui.changed,"The reserved shared level collision checkbox was editable.");
    ui.checkbox(2);require(ui.value==(WorldCollisionLayer|EnemyCollisionLayer)&&ui.changed,"An editable mask checkbox did not assign its stable bit.");
    ui.checkbox(31);require(ui.value==(WorldCollisionLayer|EnemyCollisionLayer|(std::uint32_t{1}<<31))&&ui.changed,
        "The mask picker truncated or omitted collision layer 32.");
    ui.names[2]="Renamed enemy";ui.tick();require(ui.value==(WorldCollisionLayer|EnemyCollisionLayer|(std::uint32_t{1}<<31)),
        "Renaming a collision layer changed persistent mask identities.");
    ui.value=AllCollisionLayers;ui.editable=0;ui.checkbox(31);require(ui.value==AllCollisionLayers&&!ui.changed,
        "The read-only Solid Collider mask allowed an excluded layer.");
}

void atomicColliderModeHistory(const fs::path& root){
    SceneDocument document(root);const auto id=document.createCollider();require(id!=NoEntity,"Cannot create a Collider fixture.");
    const auto before=*document.scene().find(id)->collider;const auto history=document.history().size();
    const auto triggerFilter=collisionFilterForMode(before.collision,ColliderMode::Trigger);
    require(document.edit("Collider mode",[&](auto& records,auto&){for(auto& record:records)if(record.id==id){
        record.collider->mode=ColliderMode::Trigger;record.collider->collision=triggerFilter;
    }}),"The Collider mode transition was rejected.");
    const auto trigger=*document.scene().find(id)->collider;
    require(trigger.mode==ColliderMode::Trigger&&trigger.collision.layers==TriggerCollisionLayer&&trigger.collision.mask==CharacterCollisionLayers&&
        document.history().size()==history+1,"Solid to Trigger did not normalize mode, membership and mask in one command.");
    require(document.undo()&&*document.scene().find(id)->collider==before,"Undo did not restore the Solid Collider definition atomically.");
    require(document.redo()&&*document.scene().find(id)->collider==trigger,"Redo did not restore the normalized Trigger definition.");
    const auto custom=CollisionFilter{EnemyCollisionLayer|(std::uint32_t{1}<<31),PlayerCollisionLayer};
    const auto solid=collisionFilterForMode(custom,ColliderMode::Solid);
    require(solid.layers==(custom.layers|WorldCollisionLayer)&&solid.mask==AllCollisionLayers,
        "Trigger to Solid dropped authored extra memberships or retained a restricted mask.");
    const auto roundtrip=collisionFilterForMode(solid,ColliderMode::Trigger);
    require(roundtrip.layers==custom.layers&&roundtrip.mask==CharacterCollisionLayers,
        "A Trigger mode transition discarded nonreserved memberships.");
}

class LayerNamesUi {
public:
    explicit LayerNamesUi(const fs::path& root):editor(root){editor.requestOpen();tick();tick();}
    void tick(){
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20},ImGuiCond_Always);editor.draw();ImGui::Render();
        require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"The collision settings editor left invalid ImGui state.");
    }
    void text(unsigned slot,const char* value){
        auto* child=namesWindow();require(child,"Collision layer name fields were not drawn.");
        ImGui::SetScrollY(child,slot<8?0.f:child->ScrollMax.y);tick();tick();child=namesWindow();
        const auto origin=child->DC.CursorStartPos;
        const auto y=origin.y+static_cast<float>(slot)*ImGui::GetFrameHeightWithSpacing()+ImGui::GetFrameHeight()*.5f;
        const auto x=child->Pos.x+child->Size.x*.6f;auto& io=ImGui::GetIO();
        io.AddMousePosEvent(x,y);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);tick();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);tick();
        require(editor.transactionActive(),"Editing a collision layer name did not start a property gesture.");
        io.AddKeyEvent(ImGuiMod_Ctrl,true);io.AddKeyEvent(ImGuiKey_A,true);tick();
        io.AddKeyEvent(ImGuiKey_A,false);io.AddKeyEvent(ImGuiMod_Ctrl,false);tick();
        io.AddInputCharactersUTF8(value);tick();
    }
    void escape(){auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiKey_Escape,true);tick();io.AddKeyEvent(ImGuiKey_Escape,false);tick();tick();}
    ImGuiWindow* namesWindow()const{
        const auto* root=ImGui::FindWindowByID(ImHashStr("###CollisionLayers"));if(!root)return nullptr;
        for(auto* child:root->DC.ChildWindows)if(std::string_view(child->Name).find("Layer names")!=std::string_view::npos)return child;
        return nullptr;
    }
    CollisionLayersEditor editor;
private:
    Context context_;
};
void layerNameGestures(const fs::path& root){
    LayerNamesUi ui(root);require(!fs::exists(root/".crimson/collision-layers.json"),"Opening settings wrote default project data.");
    ui.text(0,"Terrain");require(ui.editor.names()[0]=="Terrain"&&ui.editor.transactionActive(),
        "The renamed shared level collision name did not update live Inspector labels.");
    ui.editor.finishInteraction();ui.tick();require(ui.editor.canUndo(),"The name gesture did not create an undo command.");
    ui.editor.undo();ui.tick();require(ui.editor.names()[0]=="World"&&ui.editor.canRedo(),"Undo did not restore the first collision layer name.");
    ui.editor.redo();ui.tick();require(ui.editor.names()[0]=="Terrain","Redo did not restore the renamed first layer.");
    ui.text(1,"Canceled label");ui.escape();require(ui.editor.names()[1]=="Player"&&!ui.editor.transactionActive(),
        "Escape committed a collision layer name or reapplied its canceled text buffer.");
    ui.text(31,"High bit");ui.editor.finishInteraction();ui.tick();require(ui.editor.names()[31]=="High bit","Collision layer 32 was not editable.");
    ui.editor.save();ui.tick();CollisionLayersDocument disk(root);
    require(disk.names()[0]=="Terrain"&&disk.names()[31]=="High bit"&&!disk.dirty(),"Save did not persist the renamed layer settings.");
}
}
int main(){
    try{Project project;maskLocksAndFullWidth();atomicColliderModeHistory(project.root/"scene");layerNameGestures(project.root/"settings");
        std::cout<<"Collision UI checks passed.\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
