#include "tools/ParticleAppearanceEditor.h"
#include "tools/HdrColorEditor.h"
#include "tools/ParticleEmissionEditor.h"
#include "editor/ParticleDocument.h"
#include "particles/ParticleAppearance.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <implot.h>
#include <imgui_gradient/imgui_gradient.hpp>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
void near(float actual,float expected,const char* message) { require(std::isfinite(actual) && std::abs(actual-expected)<1e-5f,message); }
struct Project {
    fs::path root=fs::temp_directory_path()/
        ("crimson-appearance-ui-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project() {
        std::error_code error;const auto temporary=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error && target.parent_path()==temporary && target.filename().string().starts_with("crimson-appearance-ui-"))fs::remove_all(target,error);
    }
};
struct Context {
    ImGuiContext* value{ImGui::CreateContext()};
    ImPlotContext* plot{ImPlot::CreateContext()};
    ~Context() { ImPlot::DestroyContext(plot);ImGui::DestroyContext(value); }
};
// Synthetic input belongs only to this private CPU ImGui context. No SDL or
// native window, OS input injection, graphics device, or user project is used.
class AppearanceUi {
public:
    Context context;
    ParticleEffectDocument document;
    ParticleCurveEditorState curve;
    ParticleGradientEditorState gradient;
    HdrColorEditorState hdr;
    ParticleAppearanceEdit curveChanges{},gradientChanges{};
    HdrColorEdit hdrChanges{};
    ParticleEffectAsset baseline;
    float windowWidth{480};
    bool otherDocumentFocused{};
    Vec3 hdrDefaultColor{ParticleEmitterDefinition{}.color};
    float hdrDefaultOpacity{ParticleEmitterDefinition{}.opacity};
    explicit AppearanceUi(const fs::path& root,float width=480):document(root),windowWidth(width) {
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={960,1000};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int fontWidth{},fontHeight{};io.Fonts->GetTexDataAsRGBA32(&pixels,&fontWidth,&fontHeight);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        require(document.create("Appearance fixture"),"Could not create a private particle document.");
        require(document.edit("Configure fixture",[](auto& effect) {
            auto& e=effect.emitter;e.sizeTrack.mode=ParticleAppearanceMode::Curve;
            e.sizeTrack.curve.keys={{1,0,.02f},{2,.5f,.08f},{3,1,.14f}};
            e.colorMode=ParticleColorMode::Gradient;
            e.color={4,1,.25f};e.opacity=.7f;
            e.colorGradient.stops={{11,0,{1,0,0,.1f}},{22,.5f,{0,16,0,.6f}},{33,1,{0,0,1,.2f}}};
        }),"Could not configure the private appearance fixture.");
        require(document.save() && document.load(document.assetId()),"Could not save/reopen the private appearance fixture.");
        baseline=document.definition();tick();tick();
    }
    void tick() {
        ImGui::NewFrame();
        const bool canceled=document.transactionActive() && ImGui::IsKeyPressed(ImGuiKey_Escape,false);
        if(canceled) { document.cancel();curve.cancel();gradient.cancel();hdr.cancel();ImGui::ClearActiveID(); }
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({windowWidth,950});
        ImGui::Begin("Appearance fixture",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        ImGui::PushID("Curve");activate("Curve editor",0);
        auto candidate=document.definition();
        curveChanges=drawParticleCurveEditor(candidate.emitter.sizeTrack.curve,curve,0,10000,candidate.emitter.size,"Size (m)");
        apply("Edit size curve",candidate,curveChanges,canceled);ImGui::PopID();
        ImGui::Separator();ImGui::PushID("Gradient");activate("Gradient editor",1);
        candidate=document.definition();
        gradientChanges=drawParticleGradientEditor(candidate.emitter.colorGradient,gradient,{1,1,1,1});
        apply("Edit color gradient",candidate,gradientChanges,canceled);ImGui::PopID();
        ImGui::Separator();candidate=document.definition();
        hdrChanges=drawHdrColorControl("HDR color",candidate.emitter.color,&candidate.emitter.opacity,hdr,false,MaximumParticleColor,"Opacity",hdrDefaultColor,hdrDefaultOpacity);
        apply("Edit HDR color",candidate,{hdrChanges.changed,hdrChanges.activated,hdrChanges.active,hdrChanges.deactivated},canceled);
        if(document.transactionActive() && !curveChanges.active && !gradientChanges.active && !hdrChanges.active && !ImGui::IsAnyItemActive())document.commit();
        ImGui::End();
        if(otherDocumentFocused) {
            ImGui::SetNextWindowPos({620,20});ImGui::SetNextWindowSize({260,150});ImGui::SetNextWindowFocus();
            ImGui::Begin("Other document",nullptr,ImGuiWindowFlags_NoSavedSettings);ImGui::TextUnformatted("Focus fixture");ImGui::End();
        }
        ImGui::Render();
    }
    void mouse(const Vec2& position,bool down) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(position[0],position[1]);io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);tick();
    }
    void escape() { ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);tick();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);tick(); }
    void doubleClick(const Vec2& position) {
        mouse(position,false);mouse(position,true);mouse(position,false);mouse(position,true);mouse(position,false);
    }
    void middleClick(const Vec2& position) {
        auto& io=ImGui::GetIO();io.AddMousePosEvent(position[0],position[1]);io.AddMouseButtonEvent(ImGuiMouseButton_Middle,true);tick();
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle,false);tick();
    }
    void key(ImGuiKey value) { ImGui::GetIO().AddKeyEvent(value,true);tick();ImGui::GetIO().AddKeyEvent(value,false);tick(); }
    Vec2 curvePoint(size_t index)const {
        const auto& key=document.definition().emitter.sizeTrack.curve.keys[index];
        return curvePixel(key.age,key.value);
    }
    Vec2 curvePixel(float age,float value)const {
        return {curve.plotMinimum[0]+age*(curve.plotMaximum[0]-curve.plotMinimum[0]),
            curve.plotMaximum[1]-(value-curve.lower)/(curve.upper-curve.lower)*(curve.plotMaximum[1]-curve.plotMinimum[1])};
    }
    Vec2 gradientPoint(size_t index)const {
        return {gradient.barMinimum[0]+document.definition().emitter.colorGradient.stops[index].age*(gradient.barMaximum[0]-gradient.barMinimum[0]),
            gradient.barMaximum[1]+.3f*ImGui::GetFontSize()};
    }
    void button(int owner,const char* name) { buttonOwner_=owner;button_=name;tick();tick(); }
private:
    int buttonOwner_{-1};std::string button_;
    void activate(const char* scope,int owner) {
        if(buttonOwner_!=owner)return;
        ImGui::PushID(scope);auto& g=*ImGui::GetCurrentContext();const auto id=ImGui::GetID(button_.c_str());
        g.NavActivateId=id;g.NavActivateDownId=id;g.NavActivatePressedId=id;g.NavActivateFlags=0;
        ImGui::PopID();buttonOwner_=-1;button_.clear();
    }
    void apply(const char* label,const ParticleEffectAsset& candidate,const ParticleAppearanceEdit& change,bool canceled) {
        // This is the compound-widget contract used by ParticleEditor: draft
        // changes belong to the document until the originating widget releases.
        if(!canceled && (change.activated || change.changed) && !document.transactionActive())require(document.begin(label),"Could not begin an appearance gesture.");
        if(!canceled && change.changed)require(document.update([&](auto& effect){effect.emitter=candidate.emitter;}),"The appearance widget produced an invalid draft.");
        if(change.deactivated && !change.active && document.transactionActive())require(document.commit(),"Could not commit an appearance gesture.");
    }
};
void curveDragOneTransaction() {
    Project project;AppearanceUi ui(project.root);const auto start=ui.curvePoint(1);
    const auto revision=ui.document.revision();
    ui.mouse(start,true);require(ui.curveChanges.activated && ui.curveChanges.active && ui.document.transactionActive(),"Curve canvas did not activate its document gesture.");
    const float lower=ui.curve.lower,upper=ui.curve.upper;
    ui.mouse({start[0]+30,start[1]-15},true);ui.mouse({start[0]+50,start[1]-30},true);
    require(ui.curveChanges.changed && ui.curveChanges.active && ui.document.transactionActive(),"Dragging a curve key lost its compound active/change flags.");
    require(ui.document.revision()==revision && ui.document.previewRevision()>revision,"Curve draft published a committed revision while dragging.");
    require(ui.document.definition().emitter.sizeTrack.curve!=ui.baseline.emitter.sizeTrack.curve,"Curve key dragging did not change the draft.");
    require(ui.curve.lower==lower && ui.curve.upper==upper,"Curve axes moved while the key was being dragged.");
    const auto modified=ui.document.definition();
    ui.mouse({start[0]+50,start[1]-30},false);
    require(ui.curveChanges.deactivated && !ui.curveChanges.active && !ui.document.transactionActive() && ui.document.revision()==revision+1,
        "Curve release did not publish exactly one completed command.");
    require(ui.document.undo() && ui.document.definition()==ui.baseline && !ui.document.canUndo() && !ui.document.dirty(),"Curve drag was split into multiple undo commands or lost its saved state.");
    require(ui.document.redo() && ui.document.definition()==modified,"Curve drag redo did not restore the complete key edit.");
}
void gradientDragOneTransaction() {
    Project project;AppearanceUi ui(project.root);const auto start=ui.gradientPoint(1);const auto revision=ui.document.revision();
    ui.mouse(start,false);for(int frame=0;frame<4;++frame)ui.tick();
    ui.mouse(start,true);require(ui.gradientChanges.activated && ui.gradientChanges.active && ui.document.transactionActive(),"Gradient stop did not activate its document gesture.");
    ui.mouse({start[0]+30,start[1]},true);ui.mouse({start[0]+55,start[1]},true);
    require(ui.gradientChanges.changed && ui.gradientChanges.active && ui.document.revision()==revision,"Gradient drag did not retain draft-only ownership.");
    require(ui.document.definition().emitter.colorGradient.stops[1].age>.5f,"Gradient stop dragging did not change its age.");
    const auto modified=ui.document.definition();ui.mouse({start[0]+55,start[1]},false);
    require(ui.gradient.selected==22,"Gradient selection disappeared after a hovered stop drag.");
    require(ui.gradientChanges.deactivated && !ui.document.transactionActive() && ui.document.revision()==revision+1,"Gradient release did not commit exactly once.");
    require(ui.document.undo() && ui.document.definition()==ui.baseline && !ui.document.canUndo() && !ui.document.dirty(),"Gradient drag created extra history or did not undo exactly.");
    require(ui.document.redo() && ui.document.definition()==modified,"Gradient drag redo lost its authored stop.");
}
void escapeRestoresDrafts() {
    for(const bool gradient:{false,true}) {
        Project project;AppearanceUi ui(project.root);const auto start=gradient?ui.gradientPoint(1):ui.curvePoint(1);
        const auto revision=ui.document.revision();ui.mouse(start,true);ui.mouse({start[0]+45,start[1]-15},true);
        require(ui.document.definition()!=ui.baseline && ui.document.transactionActive(),"Escape fixture never created a changed draft.");
        ui.escape();
        require(ui.document.definition()==ui.baseline && ui.document.revision()==revision && !ui.document.transactionActive() && !ui.document.canUndo() && !ui.document.dirty(),
            "Escape did not restore the whole appearance draft without history.");
        require(ImGui::GetCurrentContext()->ActiveId==0 && !ui.curve.dragging && !ui.gradient.dragging,"Escape retained an active appearance drag.");
        ui.mouse({start[0]+70,start[1]-30},true);ui.mouse({start[0]+70,start[1]-30},false);ui.tick();
        require(ui.document.definition()==ui.baseline && !ui.document.canUndo(),"Canceled key/stop movement reapplied on a later frame.");
    }
}
void edgePointsMoveAndUndo() {
    Project project;AppearanceUi ui(project.root);
    for(const bool gradient:{false,true})for(const size_t index:{size_t{0},size_t{2}}) {
        const auto first=gradient?ui.gradientPoint(index):ui.curvePoint(index);
        const Vec2 moved{first[0]+(index==0?45.f:-45.f),first[1]};
        ui.mouse(first,true);
        ui.mouse(moved,true);ui.mouse(moved,false);
        const auto& e=ui.document.definition().emitter;
        const auto age=gradient?e.colorGradient.stops[index].age:e.sizeTrack.curve.keys[index].age;
        if(!(index==0?age>0 && age<.5f:age>.5f && age<1))
            throw std::runtime_error(std::string(gradient?"Gradient":"Curve")+" edge "+std::to_string(index)+
                " did not move: age="+std::to_string(age)+", selected="+std::to_string(gradient?ui.gradient.selected:ui.curve.selected)+
                ", x="+std::to_string(first[0])+", y="+std::to_string(first[1]));
        require(ui.document.undo() && ui.document.definition()==ui.baseline && !ui.document.canUndo(),"An edge drag did not undo as a single complete edit.");
        ui.tick();ui.tick();
    }
}
void gradientLibraryMatchesRuntime() {
    ParticleColorGradient source{ParticleInterpolation::Linear,{{1,.2f,{16,.1f,4,.15f}},{2,.8f,{.1f,8,.3f,.9f}}}};
    for(const auto mode:{ParticleInterpolation::Linear,ParticleInterpolation::Constant}) {
        source.interpolation=mode;const auto prepared=prepareParticleGradient(source);
        ImGG::Gradient gradient;gradient.clear();
        for(const auto& stop:source.stops)gradient.add_mark(ImGG::Mark{ImGG::RelativePosition{stop.age},
            ImGG::ColorRGBA{stop.color[0],stop.color[1],stop.color[2],stop.color[3]}});
        gradient.interpolation_mode()=mode==ParticleInterpolation::Linear?ImGG::Interpolation::Linear:ImGG::Interpolation::Constant;
        for(const auto age:{0.f,.1f,.2f,.3f,.5f,.7f,.8f,1.f}) {
            const auto expected=prepared.evaluate(age);
            const auto actual=gradient.at(ImGG::RelativePosition{age,ImGG::WrapMode::Clamp});
            near(actual.x,expected[0],"Gradient editor changed linear HDR red interpolation or endpoint holds.");
            near(actual.y,expected[1],"Gradient editor changed linear HDR green interpolation.");
            near(actual.z,expected[2],"Gradient editor changed linear HDR blue interpolation.");
            near(actual.w,expected[3],"Gradient editor premultiplied alpha or used the wrong constant stop.");
        }
    }
}
void directInsertionRemovalAndNarrowLayout() {
    Project project;AppearanceUi ui(project.root,280);
    require(ui.curve.plotMaximum[0]<20+280 && ui.gradient.barMaximum[0]<20+280,
        "A library editor overflowed the narrow particle property column.");
    const auto curve=prepareParticleCurve(ui.document.definition().emitter.sizeTrack.curve);
    ui.doubleClick(ui.curvePixel(.25f,curve.evaluate(.25)));
    require(ui.document.definition().emitter.sizeTrack.curve.keys.size()==4,"Double-clicking the curve did not add a key.");
    const auto inserted=prepareParticleCurve(ui.document.definition().emitter.sizeTrack.curve);
    // Double-click authors the clicked position. ImGui rounds mouse positions
    // to pixels, so a click on the line may differ by one pixel in each axis.
    const float clickTolerance=(ui.curve.upper-ui.curve.lower)/(ui.curve.plotMaximum[1]-ui.curve.plotMinimum[1])+
        .12f/(ui.curve.plotMaximum[0]-ui.curve.plotMinimum[0])+1e-5f;
    for(int i=0;i<=16;++i)require(std::abs(inserted.evaluate(i/16.0)-curve.evaluate(i/16.0))<=clickTolerance,
        "Curve insertion moved farther than the clicked pixel position.");
    const auto firstKey=ui.curvePoint(0);ui.mouse(firstKey,true);ui.mouse(firstKey,false);
    const auto selectedStop=ui.gradientPoint(1);ui.mouse(selectedStop,true);ui.mouse(selectedStop,false);
    ui.mouse(firstKey,false);
    if(ui.curve.selected!=1 || ui.gradient.selected!=22)throw std::runtime_error("Shortcut fixture selections: curve="+
        std::to_string(ui.curve.selected)+", gradient="+std::to_string(ui.gradient.selected));
    ui.key(ImGuiKey_Delete);
    require(ui.document.definition().emitter.sizeTrack.curve.keys.size()==3 &&
        ui.document.definition().emitter.sizeTrack.curve.keys.front().id!=1,"Curve Delete still protected an edge key.");
    require(ui.document.definition().emitter.colorGradient.stops==ui.baseline.emitter.colorGradient.stops,
        "A curve Delete shortcut also removed a stop from another property.");
    require(ui.document.undo() && ui.document.undo() && ui.document.definition()==ui.baseline,"Curve insert/remove did not form separate complete commands.");
    ui.tick();ui.tick();
    const auto gradient=prepareParticleGradient(ui.document.definition().emitter.colorGradient);
    const Vec2 add{ui.gradient.barMinimum[0]+.25f*(ui.gradient.barMaximum[0]-ui.gradient.barMinimum[0]),
        (ui.gradient.barMinimum[1]+ui.gradient.barMaximum[1])*.5f};
    ui.mouse(add,true);ui.mouse(add,false);
    require(ui.document.definition().emitter.colorGradient.stops.size()==4,"Clicking the gradient bar did not insert a stop.");
    const auto added=prepareParticleGradient(ui.document.definition().emitter.colorGradient);
    for(int i=0;i<=16;++i) {
        const auto expected=gradient.evaluate(i/16.0),actual=added.evaluate(i/16.0);
        for(size_t channel=0;channel<4;++channel)near(actual[channel],expected[channel],"Inserting a gradient stop changed its profile.");
    }
    ui.middleClick(ui.gradientPoint(0));
    require(ui.document.definition().emitter.colorGradient.stops.size()==3 &&
        ui.document.definition().emitter.colorGradient.stops.front().id!=11,"Gradient middle-click still protected an edge stop.");
    require(ui.document.undo() && ui.document.undo() && ui.document.definition()==ui.baseline,"Gradient insert/remove lost stable IDs or complete commands.");
}
void plotNavigationAndKeyboardFocus() {
    Project project;AppearanceUi ui(project.root);
    const float lower=ui.curve.lower,upper=ui.curve.upper;
    const auto empty=ui.curvePixel(.7f,lower+(upper-lower)*.6f);ui.mouse(empty,false);
    ImGui::GetIO().AddMouseWheelEvent(0,1);ui.tick();ui.tick();
    require(ui.curve.lower!=lower || ui.curve.upper!=upper,"ImPlot wheel navigation did not change the plot view.");
    require(ui.document.definition()==ui.baseline && !ui.document.dirty() && !ui.document.canUndo(),"Plot navigation changed the authored effect/history.");
    ui.button(0,"Frame");near(ui.curve.lower,lower,"Frame did not restore the authored curve's lower extent.");
    near(ui.curve.upper,upper,"Frame did not restore the authored curve's upper extent.");
    const auto key=ui.curvePoint(1);ui.mouse(key,true);ui.mouse(key,false);
    ui.otherDocumentFocused=true;ui.tick();ui.key(ImGuiKey_Delete);
    require(ui.document.definition()==ui.baseline && !ui.document.canUndo(),"A hovered curve consumed Delete while another document held keyboard focus.");
}
void gradientPickerSelection() {
    Project project;AppearanceUi ui(project.root);const auto stop=ui.gradientPoint(1);
    ui.mouse(stop,false);for(int frame=0;frame<4;++frame)ui.tick();ui.doubleClick(stop);ui.tick();
    require(ui.gradient.selected==22 && !ImGui::GetCurrentContext()->OpenPopupStack.empty(),
        "Double-clicking a selected stop did not retain it and open its RGBA picker.");
    require(ui.document.definition()==ui.baseline && !ui.document.dirty() && !ui.document.canUndo(),"Opening the gradient picker changed authored data.");
}
Vec2 numericPoint(const Vec2& minimum,const Vec2& maximum) {
    return {minimum[0]+(maximum[0]-minimum[0])*.25f,(minimum[1]+maximum[1])*.5f};
}
void hdrColorMathAndPassiveFrames() {
    for(const auto color:{Vec3{0,0,0},Vec3{.002f,.25f,.9f},Vec3{16,.25f,4},Vec3{1000000,20000,10}}) {
        const auto decomposition=decomposeHdrColor(color);
        const auto composed=composeHdrColor(decomposition);
        for(size_t component=0;component<3;++component)
            require(std::abs(composed[component]-color[component])<=std::max(1e-6f,color[component]*2e-6f),
                "HDR hue/exposure decomposition lost brightness or saturation.");
        const auto display=hdrColorDisplay({color[0],color[1],color[2],.3f});
        for(size_t component=0;component<3;++component)
            require(std::isfinite(display[component]) && display[component]>=0 && display[component]<=1,
                "An HDR display swatch escaped its finite display range.");
        near(display[3],.3f,"HDR swatch display mapping changed independent opacity.");
    }
    HdrColorDecomposition dim{{1,.5f,.25f},-4};const auto dimmed=composeHdrColor(dim);
    near(dimmed[0],1.f/16,"Negative EV did not dim linear brightness.");
    const auto white=hdrColorDisplay({1,1,1,1});near(white[0],1,"Ordinary white swatches were incorrectly tone mapped.");
    Project project;AppearanceUi ui(project.root);
    for(int frame=0;frame<20;++frame)ui.tick();
    require(ui.document.definition()==ui.baseline && !ui.document.dirty() && !ui.document.canUndo(),
        "Showing HDR controls or their normalized hue drifted the canonical authored color.");
}
void hdrChannelAndOpacityTransactions() {
    for(const bool opacity:{false,true}) {
        Project project;AppearanceUi ui(project.root);
        ui.hdr.expanded=true;ui.tick();
        require(ui.document.definition()==ui.baseline && !ui.document.canUndo(),"Expanding color channels changed the authored value.");
        const auto start=numericPoint(opacity?ui.hdr.opacityMinimum:ui.hdr.channelMinimum[0],
            opacity?ui.hdr.opacityMaximum:ui.hdr.channelMaximum[0]);
        const auto revision=ui.document.revision();ui.mouse(start,false);ui.mouse(start,true);
        require(ui.hdrChanges.active && ui.document.transactionActive(),"HDR numeric control did not own its edit gesture.");
        ui.mouse({start[0]+25,start[1]},true);ui.mouse({start[0]+55,start[1]},true);
        const auto modified=ui.document.definition();
        if(opacity) {
            require(modified.emitter.opacity!=ui.baseline.emitter.opacity,"Independent HDR opacity did not edit.");
            require(modified.emitter.color==ui.baseline.emitter.color,"Opacity changed HDR RGB or intensity.");
        } else {
            require(modified.emitter.color[0]>ui.baseline.emitter.color[0] && modified.emitter.color[0]>1,
                "HDR channel was clamped to SDR or did not edit.");
            near(modified.emitter.color[1],ui.baseline.emitter.color[1],"Editing R changed G.");
            near(modified.emitter.color[2],ui.baseline.emitter.color[2],"Editing R changed B.");
            near(modified.emitter.opacity,ui.baseline.emitter.opacity,"HDR channel changed opacity.");
            for(int frame=0;frame<4;++frame)ui.tick();
            require(ui.document.definition()==modified,"A stable HDR gesture drifted its linear channels.");
        }
        require(ui.document.revision()==revision,"An HDR numeric drag published history before release.");
        ui.mouse({start[0]+55,start[1]},false);
        require(!ui.document.transactionActive() && ui.document.revision()==revision+1,"HDR release did not publish one command.");
        require(ui.document.undo() && ui.document.definition()==ui.baseline && !ui.document.canUndo(),"HDR numeric drag split its undo or lost exact color.");
        require(ui.document.redo() && ui.document.definition()==modified,"HDR numeric redo lost its canonical color/opacity.");
    }
    Project project;AppearanceUi ui(project.root);
    ui.hdr.expanded=true;ui.tick();
    const auto start=numericPoint(ui.hdr.channelMinimum[0],ui.hdr.channelMaximum[0]);
    ui.mouse(start,false);ui.mouse(start,true);ui.mouse({start[0]+50,start[1]},true);
    require(ui.document.definition()!=ui.baseline && ui.document.transactionActive(),"HDR Escape fixture did not produce a draft.");
    ui.escape();ui.mouse({start[0]+70,start[1]},false);ui.tick();
    require(ui.document.definition()==ui.baseline && !ui.document.canUndo() && !ui.document.dirty() && !ui.hdr.activeItem,
        "Escape did not restore HDR channels or retained an active/stale picker edit.");
}
void hdrDefaultResetTransactions() {
    for(const int field:{0,1,2,3}) {
        Project project;AppearanceUi ui(project.root);
        if(field==3){ui.hdrDefaultColor={0,0,0};ui.hdrDefaultOpacity=0;ui.tick();}
        if(field==1||field==2){ui.hdr.expanded=true;ui.tick();}
        const bool whole=field==0||field==3;
        const auto maximum=whole?ui.hdr.swatchMaximum:field==1?ui.hdr.channelMaximum[0]:ui.hdr.opacityMaximum;
        const auto minimum=whole?ui.hdr.swatchMinimum:field==1?ui.hdr.channelMinimum[0]:ui.hdr.opacityMinimum;
        const Vec2 reset{maximum[0]+ImGui::GetStyle().ItemInnerSpacing.x+ImGui::GetFrameHeight()*.5f,
            (minimum[1]+maximum[1])*.5f};
        const auto revision=ui.document.revision();
        ui.mouse(reset,false);ui.mouse(reset,true);ui.mouse(reset,false);
        auto expected=ui.baseline;
        if(whole){expected.emitter.color=ui.hdrDefaultColor;expected.emitter.opacity=ui.hdrDefaultOpacity;}
        if(field==1)expected.emitter.color[0]=ParticleEmitterDefinition{}.color[0];
        if(field==2)expected.emitter.opacity=ParticleEmitterDefinition{}.opacity;
        require(ui.document.definition()==expected,"HDR reset changed the wrong color components, opacity or particle identities.");
        require(!ui.document.transactionActive()&&!ui.hdrChanges.active&&ui.document.revision()==revision+1,
            "HDR reset did not complete exactly one atomic document command.");
        for(int frame=0;frame<4;++frame)ui.tick();
        require(ui.document.definition()==expected&&ui.document.revision()==revision+1,
            "HDR reset retained a stale hue/exposure state or created extra history.");
        ui.mouse(reset,true);ui.mouse(reset,false);
        require(ui.document.definition()==expected&&ui.document.revision()==revision+1,
            "Clicking the disabled default reset dirtied the document.");
        require(ui.document.undo()&&ui.document.definition()==ui.baseline&&!ui.document.canUndo()&&!ui.document.dirty(),
            "HDR reset did not undo to the complete saved authored color.");
        require(ui.document.redo()&&ui.document.definition()==expected,"HDR reset redo lost color, opacity or identity.");
    }
}
void gradientDefaultResetTransactions() {
    Project project;AppearanceUi ui(project.root);
    const Vec2 reset{(ui.gradient.resetMinimum[0]+ui.gradient.resetMaximum[0])*.5f,
        (ui.gradient.resetMinimum[1]+ui.gradient.resetMaximum[1])*.5f};
    const auto revision=ui.document.revision();
    ui.mouse(reset,false);ui.mouse(reset,true);ui.mouse(reset,false);
    auto expected=ui.baseline;
    expected.emitter.colorGradient={ParticleColorGradient{}.interpolation,
        {{ui.baseline.emitter.colorGradient.stops.front().id,0,{1,1,1,1}},
         {ui.baseline.emitter.colorGradient.stops.back().id,1,{1,1,1,1}}}};
    require(ui.document.definition()==expected,"Default gradient reset changed unrelated values or replaced retained endpoint identities.");
    require(ui.document.revision()==revision+1&&!ui.document.transactionActive(),"Default gradient reset did not form one atomic command.");
    ui.tick();ui.mouse(reset,true);ui.mouse(reset,false);
    require(ui.document.definition()==expected&&ui.document.revision()==revision+1,"Disabled default gradient reset changed history.");
    require(ui.document.undo()&&ui.document.definition()==ui.baseline&&!ui.document.canUndo(),"Default gradient reset failed to undo all stops and identities.");
    require(ui.document.redo()&&ui.document.definition()==expected,"Default gradient reset failed to redo completely.");

    require(ui.document.edit("Move gradient endpoints",[](auto& effect){
        effect.emitter.colorGradient.stops.front().age=.2f;effect.emitter.colorGradient.stops.back().age=.8f;
    })&&ui.document.save()&&ui.document.load(ui.document.assetId()),"Could not configure gradient endpoint reset fixture.");
    for(const size_t endpoint:{size_t{0},size_t{1}}){
        const auto baseline=ui.document.definition();ui.tick();ui.tick();
        const auto stop=ui.gradientPoint(endpoint);ui.mouse(stop,false);ui.mouse(stop,true);ui.mouse(stop,false);
        const Vec2 ageReset{ui.gradient.positionMaximum[0]+ImGui::GetStyle().ItemInnerSpacing.x+ImGui::GetFrameHeight()*.5f,
            (ui.gradient.positionMinimum[1]+ui.gradient.positionMaximum[1])*.5f};
        ui.mouse(ageReset,false);ui.mouse(ageReset,true);ui.mouse(ageReset,false);
        auto bounded=baseline;bounded.emitter.colorGradient.stops[endpoint].age=static_cast<float>(endpoint);
        require(ui.document.definition()==bounded&&!ui.document.transactionActive(),"Gradient endpoint reset did not restore normalized age while retaining valid spacing and identity.");
        require(ui.document.undo()&&ui.document.definition()==baseline&&!ui.document.canUndo(),"Gradient endpoint reset did not undo as one command.");
    }
}
void clusteredGradientResetIsIdempotent() {
    Project project;AppearanceUi ui(project.root);
    require(ui.document.edit("Cluster gradient stops",[](auto& effect){
        effect.emitter.colorGradient.stops={{11,.1f,{1,1,1,1}},{22,.2f,{1,1,1,1}},
            {33,.25f,{1,1,1,1}},{44,.9f,{1,1,1,1}}};
    })&&ui.document.save()&&ui.document.load(ui.document.assetId()),"Could not configure clustered gradient reset fixture.");
    const auto baseline=ui.document.definition();ui.tick();ui.tick();
    const auto stop=ui.gradientPoint(1);ui.mouse(stop,false);ui.mouse(stop,true);ui.mouse(stop,false);
    const Vec2 reset{ui.gradient.positionMaximum[0]+ImGui::GetStyle().ItemInnerSpacing.x+ImGui::GetFrameHeight()*.5f,
        (ui.gradient.positionMinimum[1]+ui.gradient.positionMaximum[1])*.5f};
    const auto revision=ui.document.revision();
    ui.mouse(reset,false);ui.mouse(reset,true);ui.mouse(reset,false);
    const auto expected=ui.document.definition();const auto& stops=expected.emitter.colorGradient.stops;
    require(stops.size()==4&&stops[1].id==22&&stops[2].id==33&&stops[1].age>.2f&&stops[1].age<.25f&&
        stops[2].age-stops[1].age>=MinimumParticleKeySpacing,
        "Clustered stop reset reordered identities or violated its neighbor spacing.");
    auto restored=expected;restored.emitter.colorGradient.stops[1].age=.2f;
    require(restored==baseline&&ui.document.revision()==revision+1&&!ui.document.transactionActive(),
        "Clustered stop reset changed unrelated values or failed to complete one command.");
    for(int click=0;click<3;++click){ui.tick();ui.mouse(reset,true);ui.mouse(reset,false);}
    require(ui.document.definition()==expected&&ui.document.revision()==revision+1&&!ui.document.transactionActive(),
        "Repeating a clustered stop reset changed its sorted index, target or history.");
    require(ui.document.undo()&&ui.document.definition()==baseline&&!ui.document.canUndo(),
        "Clustered stop reset did not undo as exactly one command.");
}
void gradientHdrPickerTransactions() {
    for(const bool cancel:{false,true}) {
        Project project;AppearanceUi ui(project.root);const auto stop=ui.gradientPoint(1);
        ui.mouse(stop,false);for(int frame=0;frame<4;++frame)ui.tick();ui.doubleClick(stop);ui.tick();
        const auto start=numericPoint(ui.gradient.colorChannelMinimum[1],ui.gradient.colorChannelMaximum[1]);
        require(start[0]>0 && start[1]>0,"Gradient stop popup did not expose linear channel fields.");
        ui.mouse(start,false);ui.mouse(start,true);ui.mouse({start[0]+50,start[1]},true);
        require(ui.gradientChanges.active && ui.document.transactionActive() && ui.document.definition().emitter.colorGradient.stops[1].color[1]>16,
            "The selected gradient stop's HDR channel did not retain a live compound transaction.");
        if(cancel) {
            ui.escape();ui.mouse({start[0]+60,start[1]},false);ui.tick();
            require(ui.document.definition()==ui.baseline && !ui.document.canUndo() && !ui.document.dirty(),
                "Escape did not restore the selected stop's HDR color exactly.");
        } else {
            const auto modified=ui.document.definition();ui.mouse({start[0]+50,start[1]},false);
            require(!ui.document.transactionActive() && ui.document.undo() && ui.document.definition()==ui.baseline && !ui.document.canUndo(),
                "Gradient HDR channel did not form exactly one undo command.");
            require(ui.document.redo() && ui.document.definition()==modified,"Gradient HDR redo lost stop color or identity.");
        }
    }
}
void gradientPickerCacheChurn() {
    Project project;AppearanceUi ui(project.root);
    for(int cycle=0;cycle<40;++cycle) {
        const auto stop=ui.gradientPoint(1);
        ui.mouse(stop,false);for(int frame=0;frame<4;++frame)ui.tick();ui.doubleClick(stop);ui.tick();
        require(ui.gradient.cachedColorEditors()==1,"Opening one stop picker did not retain exactly one bounded editor state.");
        ui.escape();ui.tick();
        ui.middleClick(ui.gradientPoint(1));
        require(ui.document.definition().emitter.colorGradient.stops.size()==2 && ui.gradient.cachedColorEditors()==0,
            "Deleting a stop retained its HDR picker state across authoring churn.");
        const Vec2 add{(ui.gradient.barMinimum[0]+ui.gradient.barMaximum[0])*.5f,
            (ui.gradient.barMinimum[1]+ui.gradient.barMaximum[1])*.5f};
        ui.mouse(add,true);ui.mouse(add,false);ui.tick();
        require(ui.document.definition().emitter.colorGradient.stops.size()==3 && ui.gradient.cachedColorEditors()==0,
            "Adding an untouched stop inherited a deleted stop's HDR picker state.");
    }
}

void explosivenessTransactions() {
    Project project;Context context;ParticleEffectDocument document(project.root);
    auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={700,400};io.DeltaTime=1.f/60;
    io.ConfigInputTrickleEventQueue=false;
    unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
    require(document.create("Explosiveness widget"),"Could not create emission fixture.");
    require(document.edit("Legacy schedule",[](auto& asset) {
        auto& emitter=asset.emitter;emitter.duration=2;emitter.rate=10;emitter.bursts={{1,.2f,10}};
    })&&document.save()&&document.load(document.assetId()),"Could not configure legacy emission fixture.");
    auto baseline=document.definition();ImVec2 minimum{},maximum{};std::string diagnostic;bool durationMode{};
    const auto frame=[&] {
        ImGui::NewFrame();const bool canceled=document.transactionActive()&&ImGui::IsKeyPressed(ImGuiKey_Escape,false);
        if(canceled){document.cancel();ImGui::ClearActiveID();}
        ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({600,200});
        ImGui::Begin("Emission fixture",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
        auto candidate=document.definition();ImGui::SetNextItemWidth(400);
        const bool changed=durationMode?drawParticleDuration(candidate.emitter):drawParticleExplosiveness(candidate.emitter,diagnostic);
        minimum=ImGui::GetItemRectMin();maximum=ImGui::GetItemRectMax();
        if(!canceled&&(ImGui::IsItemActivated()||changed)&&!document.transactionActive())document.begin("Change emission explosiveness");
        if(!canceled&&changed)require(document.update([&](auto& asset){asset=candidate;}),"Explosiveness widget candidate failed validation.");
        if(ImGui::IsItemDeactivated()&&document.transactionActive())require(document.commit(),"Explosiveness widget failed to complete its transaction.");
        ImGui::End();ImGui::Render();require(ImGui::GetDrawData()->Valid,"Explosiveness widget produced invalid draw data.");
    };
    frame();frame();require(document.definition()==baseline&&!document.dirty()&&!document.canUndo(),"Passive Explosiveness display converted legacy emission.");
    const auto move=[&](float fraction) {io.AddMousePosEvent(minimum.x+400*fraction,(minimum.y+maximum.y)*.5f);frame();};
    move(.4f);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();move(.8f);
    require(document.transactionActive()&&document.definition().emitter.bursts.empty()&&document.definition().emitter.explosiveness>.7f,
        "Explosiveness drag did not author a converted emission schedule.");
    near(document.definition().emitter.rate,15,"Explosiveness drag converted legacy burst count more than once.");
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();const auto edited=document.definition();
    require(!document.transactionActive()&&document.undo()&&document.definition()==baseline&&!document.canUndo(),
        "Explosiveness drag did not preserve the entire old schedule in one undo command.");
    require(document.redo()&&document.definition()==edited&&document.undo(),"Explosiveness conversion failed to redo completely.");frame();
    move(.3f);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();move(.6f);
    io.AddKeyEvent(ImGuiKey_Escape,true);frame();io.AddKeyEvent(ImGuiKey_Escape,false);io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();
    require(document.definition()==baseline&&!document.transactionActive()&&!document.canUndo(),"Escape failed to restore the legacy schedule and explosiveness.");

    require(document.edit("Overflow schedule",[](auto& asset){asset.emitter.rate=MaximumParticleRate;})&&document.save()&&document.load(document.assetId()),
        "Could not configure overflow rejection fixture.");baseline=document.definition();frame();
    move(.5f);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();
    require(!diagnostic.empty()&&document.definition()==baseline&&!document.transactionActive()&&!document.canUndo(),
        "Rejected legacy conversion escaped the widget, changed values or left history active.");

    require(document.edit("Finite legacy timing",[](auto& asset){asset.emitter.finiteDuration=true;})&&document.save()&&document.load(document.assetId()),
        "Could not configure legacy duration widget.");baseline=document.definition();durationMode=true;
    // The preceding slider click uses the same screen position. Let it age out
    // before testing a drag, rather than double-clicking into numeric entry.
    for(int index=0;index<30;++index)frame();
    move(.5f);io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();
    io.AddMousePosEvent(minimum.x-1000,(minimum.y+maximum.y)*.5f);frame();
    io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();
    const auto& bounded=document.definition().emitter;
    require(bounded.bursts==baseline.emitter.bursts&&bounded.duration==particleDurationLowerBound(baseline.emitter)&&
        bounded.duration>bounded.bursts.front().time&&!document.transactionActive(),
        "Reducing Duration shifted retained legacy events instead of bounding the cycle end.");
    require(document.undo()&&document.definition()==baseline&&!document.canUndo(),"Duration bound did not preserve legacy records in one undo step.");
    const auto records=baseline.emitter.bursts;
    auto maximumTime=baseline.emitter;maximumTime.bursts.front().time=MaximumParticleTime;
    require(particleDurationLowerBound(maximumTime)>MaximumParticleTime&&baseline.emitter.bursts==records,
        "Maximum-time legacy events were silently shifted to fit a finite duration.");
}
}
int main() {
    try {
        curveDragOneTransaction();gradientDragOneTransaction();escapeRestoresDrafts();edgePointsMoveAndUndo();gradientLibraryMatchesRuntime();
        directInsertionRemovalAndNarrowLayout();plotNavigationAndKeyboardFocus();gradientPickerSelection();
        hdrColorMathAndPassiveFrames();hdrChannelAndOpacityTransactions();hdrDefaultResetTransactions();gradientDefaultResetTransactions();clusteredGradientResetIsIdempotent();gradientHdrPickerTransactions();gradientPickerCacheChurn();explosivenessTransactions();
        std::cout<<"Particle library widget transactions, movable edges, linear HDR color/gradient compatibility and Escape checks passed.\n";return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
