#include "tools/GameplayTagControls.h"
#include "gameplay/Attributes.h"
#include "scene/SceneComponents.h"
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void pickerPolicies() {
    const GameplayTag health{"Stats.Health"},custom{"Stats.Custom.Unimplemented"},outside{"StatsExtra.Health"};
    GameplayTagRegistry registry({health,custom,outside,GameplayTag{"State.Ready"}});
    AttributeDefinition definition;definition.tag=health;definition.displayName="Health";
    AttributeCatalog catalog(registry,{definition});const GameplayTagPickerFilter filter{GameplayTag{"Stats"},&catalog};
    const auto choices=gameplayTagPickerChoices(registry,filter);
    require(choices.size()==1 && choices.front()==health,"The attribute picker offered a hierarchy parent, undefined Stat or outside-root tag.");
    require(gameplayTagPickerChoices(registry,filter,"hEaLtH")==choices,"Tag search did not match canonical names without case sensitivity.");
    require(gameplayTagPickerChoices(registry,filter,"missing").empty(),"Search returned an unrelated attribute.");
    require(!gameplayTagPickerAllows(GameplayTag{"Stats.Unknown"},registry,filter),"An unregistered value became an authorable picker option.");
    require(gameplayTagPickerAllows(outside,registry),"The generic tag picker inherited an attribute-only restriction.");
    require(gameplayTagPickerAllows(custom,registry,{GameplayTag{"Stats"},{}}),"A root-only picker incorrectly required an attribute definition.");
    require(!gameplayTagPickerAllows(outside,registry,{GameplayTag{"Stats"},{}}),"Stats-root filtering used a raw string prefix.");
    const auto tree=gameplayTagPickerHierarchy(registry,filter);
    require(tree.size()==2 && tree.front().tag==GameplayTag{"Stats"} && !tree.front().selectable && tree.front().children.size()==1 &&
        tree.back().tag==health && tree.back().selectable,"The hierarchy omitted the nonselectable Stats root or exposed an undefined branch.");
}
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
void activate(ImGuiID id) {
    auto& context=*ImGui::GetCurrentContext();context.NavActivateId=id;
    context.NavActivateDownId=id;context.NavActivatePressedId=id;
}
// Input is sent only to an isolated CPU ImGui context, never a native window.
class TagUi {
public:
    TagUi():registry({GameplayTag{"Stats.Health"},GameplayTag{"State.Ready"}}){tick();tick();}
    void tick() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({650,650});
        ImGui::Begin("Gameplay controls",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        if(openNext){ImGui::PushID("Owned");activate(ImGui::GetID("Add..."));ImGui::PopID();openNext=false;}
        if(removeNext) {
            ImGui::PushID("Owned");ImGui::PushID("Stats.Health");activate(ImGui::GetID("x"));ImGui::PopID();ImGui::PopID();removeNext=false;
        }
        if(pickNext) {
            auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
            require(!popups.empty() && popups.back().Window,"The tag picker popup disappeared.");
            const auto* popup=popups.back().Window;require(!popup->DC.ChildWindows.empty(),"The tag picker choices did not draw.");
            const auto* choices=popup->DC.ChildWindows.front();const auto scope=ImHashStr("Stats.Health",0,choices->ID);
            activate(ImHashStr("Stats.Health",0,scope));pickNext=false;
        }
        changed=drawGameplayTagContainerControl("Owned",value,state,{},registry);
        GameplayTag single{"Stats.Health"};drawGameplayTagControl("Single",single,singleState,{},registry);
        ImGui::End();ImGui::Render();
        require(ImGui::GetCurrentContext()->ErrorCountCurrentFrame==0,"Tag chips or the picker left invalid ImGui state.");
    }
    void pick() {
        openNext=true;tick();tick();auto& popups=ImGui::GetCurrentContext()->OpenPopupStack;
        require(!popups.empty() && popups.back().Window,"The tag picker popup did not open.");
        std::copy_n("Health",7,state.search.data());tick();tick();pickNext=true;tick();
    }
    GameplayTagRegistry registry;GameplayTagContainer value;GameplayTagPickerState state,singleState;
    bool changed{},openNext{},removeNext{},pickNext{};
private:Context context_;
};
void chipInteraction() {
    TagUi ui;ui.pick();require(ui.changed && ui.value.has(GameplayTag{"Stats.Health"},true),"Picking a registered tag did not assign it.");
    ui.tick();require(!ui.changed && ui.value.tags().size()==1,"Redrawing chips changed authored membership or materialized parents.");
    ui.removeNext=true;ui.tick();require(ui.changed && ui.value.empty(),"The chip x did not remove the explicit tag.");
    ui.value.add(GameplayTag{"Unknown.Preserved"});ui.tick();require(!ui.changed && !ui.value.empty(),"Drawing an unresolved tag discarded its authored value.");
}

}
int main(){
    try{pickerPolicies();chipInteraction();std::cout<<"Gameplay UI checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
