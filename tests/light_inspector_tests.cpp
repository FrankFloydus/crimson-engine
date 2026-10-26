#include "tools/LightInspectorControls.h"
#include "tools/SpotLightInspectorControls.h"
#include "runtime/SceneLights.h"
#include "scene/SceneDocument.h"
#include <imgui_internal.h>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <map>
#include <optional>
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

// The actual cone widgets drive a scene document so paired edits and resets
// must satisfy validation immediately and retain the ordinary gesture history.
class SpotInspector {
public:
    using Cone=SpotConeControl;
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("crimson-spot-inspector-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    SceneDocument document{root};EntityId entity{};ImGuiContext* context{};
    std::map<Cone,Field> fields;
    std::map<Cone,ImVec2> resets;
    std::set<Cone> resetClicks;
    unsigned commits{};
    SpotInspector() {
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.DisplaySize={1000,600};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        entity=document.createSpotLight();require(entity!=NoEntity,"Cannot create the Spot Inspector fixture.");tick();tick();
    }
    ~SpotInspector() {
        ImGui::DestroyContext(context);
        std::error_code error;const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(error)return;
        const auto target=std::filesystem::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-spot-inspector-"))std::filesystem::remove_all(target,error);
    }
    SceneSpotLight capture()const{return *document.scene().find(entity)->spot;}
    void set(const SceneSpotLight& value) {
        require(document.edit("Seed spot light",[&](auto& records,auto&){for(auto& record:records)if(record.id==entity)record.spot=value;}),
            "Cannot seed the Spot Inspector fixture.");tick();
    }
    void tick() {
        fields.clear();resets.clear();resetClicks.clear();ImGui::NewFrame();
        const bool canceled=ImGui::IsKeyPressed(ImGuiKey_Escape,false)&&document.transactionActive();
        if(canceled){document.cancel();ImGui::ClearActiveID();active.reset();}
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({900,500});
        ImGui::Begin("Spot Inspector",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        if(activation){context->NavActivateId=context->NavActivateDownId=context->NavActivatePressedId=activation;
            context->NavActivateFlags=ImGuiActivateFlags_PreferInput;context->NavInputSource=ImGuiInputSource_Keyboard;activation=0;}
        auto current=capture();
        drawSpotConeControls(current,[&](Cone control,const char* label,bool changed) {
            const auto& item=context->LastItemData;
            fields[control]={item.ID,item.Rect.Min,item.Rect.Max,(item.ItemFlags&ImGuiItemFlags_Disabled)!=0};
            resets[control]={item.Rect.Max.x+ImGui::GetStyle().ItemInnerSpacing.x+ImGui::GetFrameHeight()*.5f,(item.Rect.Min.y+item.Rect.Max.y)*.5f};
            const bool reset=propertyResetClicked();if(reset)resetClicks.insert(control);
            if(reset)require(!ImGui::IsItemActive()&&ImGui::IsItemEdited()&&ImGui::IsItemDeactivatedAfterEdit(),
                "Spot reset lost the ordinary atomic edit flags.");
            if(!canceled&&(ImGui::IsItemActivated()||changed)) {
                if(active!=control){if(document.transactionActive())document.commit();require(document.begin(label),"Spot gesture could not begin.");active=control;}
                if(changed)require(document.update([&](auto& records,auto&){for(auto& record:records)if(record.id==entity)record.spot=current;}),
                    "A paired cone edit exposed an invalid intermediate light.");
            }
            if(active==control&&(ImGui::IsItemDeactivated()||(changed&&!ImGui::IsItemActive()))) {
                require(document.commit(),"Spot gesture could not commit.");++commits;active.reset();
            }
        });
        ImGui::End();ImGui::Render();require(context->ErrorCountCurrentFrame==0,"Spot controls left an invalid ImGui stack.");
    }
    void text(Cone control,const char* value){activation=fields.at(control).id;tick();require(context->ActiveId!=0,"Spot numeric input did not activate.");ImGui::GetIO().AddInputCharactersUTF8(value);tick();}
    void key(ImGuiKey key){auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);tick();io.AddKeyEvent(key,false);tick();tick();}
    void reset(Cone control){const auto position=resets.at(control);auto& io=ImGui::GetIO();io.AddMousePosEvent(position.x,position.y);tick();
        io.AddMouseButtonEvent(0,true);tick();io.AddMouseButtonEvent(0,false);tick();}
private:
    ImGuiID activation{};std::optional<Cone> active;
};
void spotConeWidgetsAndHistory() {
    using Cone=SpotConeControl;SpotInspector ui;
    std::set<ImGuiID> ids;
    for(const auto control:{Cone::InnerAngle,Cone::OuterAngle,Cone::Falloff})
        require(ui.fields.at(control).id&&ids.insert(ui.fields.at(control).id).second,"Spot cone controls do not have distinct ordinary field identities.");
    const auto defaultHistory=ui.document.history().size();const auto defaultCommits=ui.commits;
    for(const auto control:{Cone::InnerAngle,Cone::OuterAngle,Cone::Falloff})ui.reset(control);
    require(ui.document.history().size()==defaultHistory&&ui.commits==defaultCommits&&!ui.document.transactionActive(),
        "A disabled Spot reset authored a command.");
    struct Edit{Cone control;const char* text;float inner,outer,falloff;};
    const std::array edits{
        Edit{Cone::InnerAngle,"120",45,45,1},Edit{Cone::OuterAngle,"10",10,10,1},
        Edit{Cone::OuterAngle,"100",10,89,1},Edit{Cone::OuterAngle,"-5",.1f,.1f,1},
        Edit{Cone::Falloff,"0",.1f,.1f,.01f},Edit{Cone::Falloff,"100",.1f,.1f,16}};
    for(const auto& edit:edits) {
        const auto before=ui.capture();const auto history=ui.document.history().size();ui.text(edit.control,edit.text);
        require(ui.document.transactionActive()&&ui.document.history().size()==history,"Spot numeric input committed before release.");
        const auto live=ui.capture();
        if(std::abs(live.innerAngle-edit.inner)>=1e-5f||std::abs(live.outerAngle-edit.outer)>=1e-5f||std::abs(live.coneFalloff-edit.falloff)>=1e-5f)
            throw std::runtime_error("Spot numeric clamp for field "+std::to_string(static_cast<unsigned>(edit.control))+" and input "+edit.text+
                " produced "+std::to_string(live.innerAngle)+", "+std::to_string(live.outerAngle)+", "+std::to_string(live.coneFalloff));
        ui.key(ImGuiKey_Enter);require(!ui.document.transactionActive()&&ui.document.history().size()==history+1,"Spot input did not produce one history command.");
        require(ui.document.undo()&&ui.capture()==before,"Spot numeric Undo did not restore both angles.");ui.tick();
        require(ui.document.redo()&&ui.capture()==live,"Spot numeric Redo did not restore the complete light.");ui.tick();
    }
    auto wide=ui.capture();wide.innerAngle=70;wide.outerAngle=80;wide.coneFalloff=3;wide.intensity=84;
    wide.flicker.enabled=true;wide.flicker.seed=47;ui.set(wide);
    const auto history=ui.document.history().size();const auto commits=ui.commits;ui.reset(Cone::OuterAngle);
    auto expected=wide;expected.outerAngle=SceneSpotLight{}.outerAngle;expected.innerAngle=expected.outerAngle;
    require(ui.capture()==expected&&ui.resetClicks.contains(Cone::OuterAngle)&&ui.commits==commits+1&&ui.document.history().size()==history+1&&!ui.document.transactionActive(),
        "Resetting the outer angle did not reduce the inner angle in exactly one atomic command.");
    require(ui.document.undo()&&ui.capture()==wide,"Undo of outer reset lost the original paired cone or flicker settings.");ui.tick();
    require(ui.document.redo()&&ui.capture()==expected,"Redo of outer reset lost the paired cone.");ui.tick();
    for(const auto control:{Cone::InnerAngle,Cone::Falloff}) {
        const auto before=ui.capture();const auto count=ui.document.history().size();ui.reset(control);auto reset=before;
        if(control==Cone::InnerAngle)reset.innerAngle=SceneSpotLight{}.innerAngle;else reset.coneFalloff=SceneSpotLight{}.coneFalloff;
        require(ui.capture()==reset&&ui.document.history().size()==count+1&&ui.resetClicks.contains(control),"A Spot property's own reset button failed or changed another property.");
    }
    auto narrow=ui.capture();narrow.innerAngle=10;narrow.outerAngle=15;ui.set(narrow);
    const auto narrowHistory=ui.document.history().size();const auto narrowCommits=ui.commits;ui.reset(Cone::InnerAngle);
    auto widened=narrow;widened.innerAngle=SceneSpotLight{}.innerAngle;widened.outerAngle=widened.innerAngle;
    require(ui.capture()==widened&&ui.commits==narrowCommits+1&&ui.document.history().size()==narrowHistory+1&&!ui.document.transactionActive(),
        "Resetting the inner angle did not restore its default and raise the outer angle in one atomic command.");
    require(ui.document.undo()&&ui.capture()==narrow,"Undo of inner reset lost the original narrow cone or other light settings.");ui.tick();
    require(ui.document.redo()&&ui.capture()==widened,"Redo of inner reset lost its paired outer-angle correction.");ui.tick();
    const auto before=ui.capture();const auto canceledHistory=ui.document.history().size();ui.text(Cone::OuterAngle,"18");ui.key(ImGuiKey_Escape);
    require(ui.capture()==before&&ui.document.history().size()==canceledHistory&&!ui.document.transactionActive(),"Escape did not cancel a paired Spot cone edit.");
    ui.tick();require(ui.capture()==before,"A canceled Spot input reapplied its buffered value.");
}
}
int main(){try{temperatureAndDisabledColor();behaviorAndOutputFields();pairedRangesAndIndependentPresets();spotConeWidgetsAndHistory();std::cout<<"Light Inspector mode, behavior, output ranges, Spot cone clamps, resets and gesture history passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
