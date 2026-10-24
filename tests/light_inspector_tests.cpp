#include "tools/LightInspectorControls.h"
#include "runtime/SceneLights.h"
#include <imgui_internal.h>
#include <cmath>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
using Control=LightInspectorControl;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Field {ImGuiID id{};ImVec2 minimum{},maximum{};bool disabled{};};
// Every event targets a private CPU ImGui context; no native windows or input.
class Inspector {
public:
    Inspector() {
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1800,12000};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        tick();tick();
    }
    ~Inspector(){ImGui::DestroyContext(context);}
    void tick() {
        fields.clear();edited.clear();ImGui::NewFrame();
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({1000,11000});
        ImGui::Begin("Light Inspector",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove);
        if(activation) {
            context->NavActivateId=context->NavActivateDownId=context->NavActivatePressedId=activation;
            context->NavActivateFlags=ImGuiActivateFlags_PreferInput;context->NavInputSource=ImGuiInputSource_Keyboard;activation=0;
        }
        const auto edit=[&](Control control,const char*,bool changed) {
            const auto& item=context->LastItemData;
            fields[control]={item.ID,item.Rect.Min,item.Rect.Max,(item.ItemFlags&ImGuiItemFlags_Disabled)!=0};
            if(changed)edited.insert(control);
        };
        drawLightAppearanceControls(value,state,edit);
        ImGui::SetNextItemOpen(true,ImGuiCond_Always);drawLightFlickerControls(value,state,edit,{});
        ImGui::End();ImGui::Render();require(context->ErrorCountCurrentFrame==0,"Light controls left an invalid ImGui stack.");
    }
    void activate(Control control){activation=fields.at(control).id;tick();require(context->ActiveId!=0,"Numeric light field did not activate.");}
    void type(const char* text){ImGui::GetIO().AddInputCharactersUTF8(text);tick();}
    void key(ImGuiKey key){ImGui::GetIO().AddKeyEvent(key,true);tick();ImGui::GetIO().AddKeyEvent(key,false);tick();tick();}
    void click(ImVec2 point) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);tick();io.AddMouseButtonEvent(0,true);tick();io.AddMouseButtonEvent(0,false);tick();
    }
    void click(Control control){const auto& field=fields.at(control);click({(field.minimum.x+field.maximum.x)*.5f,(field.minimum.y+field.maximum.y)*.5f});}
    void activateId(ImGuiID id){activation=id;tick();}
    LightInspectorValue value;LightInspectorState state;ImGuiContext* context{};
    std::map<Control,Field> fields;std::set<Control> edited;
private:ImGuiID activation{};
};
void temperatureAndDisabledColor() {
    Inspector ui;ui.value.color={.2f,.5f,.7f};const auto manual=ui.value.color;
    ui.value.colorMode=LightColorMode::Temperature;ui.value.temperatureKelvin=4100;ui.tick();
    require(ui.fields.at(Control::Color).disabled,"Temperature mode left the color picker editable.");
    ui.activate(Control::Temperature);ui.type("7800");ui.key(ImGuiKey_Enter);
    require(ui.value.temperatureKelvin==7800&&ui.value.color==manual,"Temperature editing overwrote the independent manual color.");
    ui.value.colorMode=LightColorMode::Color;ui.tick();
    require(!ui.fields.at(Control::Color).disabled&&!ui.fields.contains(Control::Temperature),"Color mode retained a temperature field or disabled picker.");
    require(ui.value.temperatureKelvin==7800&&ui.value.color==manual,"Color mode discarded independent authored values.");
}
void behaviorAndOutputFields() {
    Inspector ui;ui.value.flicker.enabled=true;ui.tick();
    require(ui.fields.contains(Control::SlowAmount)&&!ui.fields.contains(Control::StableMinimum),"Fire behavior showed another family's controls.");
    ui.value.flicker.behavior=LightFlickerBehavior::FailingFluorescent;ui.tick();
    require(ui.fields.contains(Control::StableMinimum)&&ui.fields.contains(Control::RecoveryTime)&&ui.fields.contains(Control::FlashesMaximum)&&!ui.fields.contains(Control::SlowAmount),"Electrical behavior omitted burst tuning.");
    ui.value.flicker.behavior=LightFlickerBehavior::FluorescentStartup;ui.value.flicker.playback=LightFlickerPlayback::Once;ui.tick();
    require(!ui.fields.contains(Control::StableMinimum)&&!ui.fields.contains(Control::StableMaximum)&&ui.fields.contains(Control::RecoveryTime),"One-shot startup exposed an unused stable interval.");
    ui.value.flicker.playback=LightFlickerPlayback::Loop;ui.tick();require(ui.fields.contains(Control::StableMinimum),"Looping startup omitted its stable interval.");
    ui.value.flicker.behavior=LightFlickerBehavior::SoftBreathing;ui.tick();
    require(ui.fields.contains(Control::Period)&&!ui.fields.contains(Control::SecondaryStrength)&&!ui.fields.contains(Control::FlashMinimum),"Single rhythmic pulse exposed irrelevant burst fields.");
    ui.value.flicker.behavior=LightFlickerBehavior::Heartbeat;ui.tick();require(ui.fields.contains(Control::SecondaryStrength),"Heartbeat omitted its secondary pulse.");
    ui.value.flicker.behavior=LightFlickerBehavior::Lightning;ui.tick();
    require(ui.fields.contains(Control::EventFrequency)&&ui.fields.contains(Control::Afterglow)&&ui.fields.contains(Control::RestMaximum),"Irregular events omitted burst or decay controls.");
    ui.value.directional=true;ui.tick();require(!ui.fields.contains(Control::RadiusEnabled),"Directional light exposed a radius output.");
    ui.value.colorMode=LightColorMode::Temperature;ui.tick();
    require(ui.fields.contains(Control::ColorMinimumTemperature)&&!ui.fields.contains(Control::ColorMinimumTint),"Temperature output did not use relative Kelvin shifts.");
    const auto distinctFields=[&] {
        std::map<ImGuiID,Control> ids;
        for(const auto& [control,field]:ui.fields) {
            if(!field.id)throw std::runtime_error("Light control "+std::to_string(static_cast<unsigned>(control))+" has a zero ImGui field identity.");
            const auto [found,inserted]=ids.emplace(field.id,control);
            if(!inserted)throw std::runtime_error("Light controls "+std::to_string(static_cast<unsigned>(found->second))+" and "+std::to_string(static_cast<unsigned>(control))+" share ImGui field "+std::to_string(field.id)+".");
        }
    };
    distinctFields();ui.value.colorMode=LightColorMode::Color;ui.tick();distinctFields();
}
void pairedRangesAndIndependentPresets() {
    Inspector ui;ui.value.flicker.enabled=true;ui.tick();
    ui.activate(Control::IntensityMinimum);ui.type("3");ui.key(ImGuiKey_Enter);
    require(ui.value.flicker.intensity.minimum==3&&ui.value.flicker.intensity.maximum==3,"Raising a minimum left an invalid output range.");
    ui.activate(Control::IntensityMaximum);ui.type("0.5");ui.key(ImGuiKey_Enter);
    require(ui.value.flicker.intensity.minimum==.5f&&ui.value.flicker.intensity.maximum==.5f,"Lowering a maximum left an invalid output range.");
    ui.value.flicker.seed=42;ui.value.flicker.timeOffset=2.5f;const auto baseColor=ui.value.color;const auto baseIntensity=ui.value.intensity;
    // Selecting through the actual filtered popup exercises popup field identity,
    // behavior assignment and independent fixture/flicker storage.
    ui.click(Control::FlickerPreset);ui.state.flickerFilter=ImGuiTextFilter("Heartbeat");ui.tick();
    require(!ui.context->OpenPopupStack.empty(),"Flicker preset selector did not open.");
    const auto presets=lightFlickerPresets();const LightFlickerPreset* heartbeat{};
    for(const auto& preset:presets)if(preset.settings.behavior==LightFlickerBehavior::Heartbeat)heartbeat=&preset;
    require(heartbeat,"Heartbeat preset unavailable.");
    ImGuiWindow* child{};for(auto* window:ui.context->Windows)if(window->Active&&std::string(window->Name).find("##behaviors")!=std::string::npos)child=window;
    require(child,"Flicker preset list was not drawn.");
    const auto presetId=ImHashStr(heartbeat->id.data(),heartbeat->id.size(),child->IDStack.back());
    ui.activateId(ImHashStr(heartbeat->name.data(),heartbeat->name.size(),presetId));ui.tick();
    require(ui.value.flicker.behavior==LightFlickerBehavior::Heartbeat,"Filtered preset row did not assign the selected behavior.");
    require(ui.value.flicker.enabled&&ui.value.flicker.seed==42&&ui.value.flicker.timeOffset==2.5f&&ui.value.color==baseColor&&ui.value.intensity==baseIntensity,
        "Flicker preset changed fixture appearance or independent playback identity.");
}
}
int main(){try{temperatureAndDisabledColor();behaviorAndOutputFields();pairedRangesAndIndependentPresets();std::cout<<"Light Inspector mode, behavior, output ranges and gesture identities passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
