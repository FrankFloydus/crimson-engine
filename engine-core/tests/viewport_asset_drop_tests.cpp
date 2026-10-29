#include "editor/ViewportAssetDrop.h"
#include "editor/ViewportMaterialDrop.h"
#include "tools/Tools.h"
#include "scene/SceneDocument.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace crimson;
namespace fs=std::filesystem;
const std::string firstModel="assets/models/first/manifest.json",secondModel="assets/models/second/manifest.json";
const std::string particleEffect="assets/particles/drop.crimson-particle";
const std::string materialAsset="assets/materials/drop.crimson-material";
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
std::vector<char> payload(const std::vector<std::string>& ids) {
    std::vector<char> result;
    for (const auto& id:ids) { result.insert(result.end(),id.begin(),id.end()); result.push_back('\0'); }
    return result;
}
struct Project {
    fs::path root=fs::temp_directory_path()/
        ("crimson-drop-focus-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project() {
        for (const auto& id:{firstModel,secondModel,particleEffect}) {
            fs::create_directories((root/id).parent_path()); std::ofstream(root/id)<<"Managed model remains owned by the browser.";
        }
    }
    ~Project() {
        std::error_code error; const auto temp=fs::weakly_canonical(fs::temp_directory_path(),error); if (error) return;
        const auto target=fs::weakly_canonical(root,error);
        if (!error && target.parent_path()==temp && target.filename().string().starts_with("crimson-drop-focus-test-")) fs::remove_all(target,error);
    }
};
struct Tick {
    EditorFrame frame;
    std::string focused;
    bool assetDelete{},entityDelete{};
};
// This is a private CPU-only ImGui context. Its queued input is never sent to an
// operating-system window, SDL, a renderer, or the user's desktop.
class Harness {
public:
    explicit Harness(const fs::path& root):document(root) {
        context_=ImGui::CreateContext(); auto& io=ImGui::GetIO(); io.IniFilename=nullptr;
        io.DisplaySize=ImVec2(960,540); io.DeltaTime=1.0f/60; io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{}; int width{},height{}; io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);
        io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        tick(); // Establish both windows before testing input and focus.
    }
    ~Harness() { ImGui::DestroyContext(context_); }
    void source(std::string type,std::vector<char> bytes) { type_=std::move(type);bytes_=std::move(bytes); }
    void mouse(float x,float y,bool down) {
        auto& io=ImGui::GetIO(); io.AddMousePosEvent(x,y); io.AddMouseButtonEvent(ImGuiMouseButton_Left,down);
    }
    Tick tick() {
        ImGui::NewFrame(); Tick result;
        constexpr auto flags=ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoFocusOnAppearing;
        ImGui::SetNextWindowPos(ImVec2(360,20)); ImGui::SetNextWindowSize(ImVec2(560,400));
        ImGui::Begin("Viewport",nullptr,flags);
        ImGui::Image(ImTextureRef(static_cast<ImTextureID>(1)),ImVec2(500,330));
        acceptViewportModelDrop(result.frame); // The same production receiver used by SceneViewport.
        ImGui::End();
        // Match production order: the browser is submitted after the viewport.
        ImGui::SetNextWindowPos(ImVec2(20,20)); ImGui::SetNextWindowSize(ImVec2(300,400));
        ImGui::Begin("Asset Browser",nullptr,flags); ImGui::BeginChild("Asset library",ImVec2(0,0));
        ImGui::InvisibleButton("Model card",ImVec2(220,150));
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) { sourceSelected=true; ImGui::SetWindowFocus(); }
        if (!type_.empty() && ImGui::BeginDragDropSource()) {
            ImGui::SetDragDropPayload(type_.c_str(),bytes_.empty() ? nullptr : bytes_.data(),bytes_.size());
            ImGui::TextUnformatted("Model source"); ImGui::EndDragDropSource();
        }
        ImGui::EndChild(); ImGui::End();
        const auto* nav=ImGui::GetCurrentContext()->NavWindow;
        result.focused=nav ? nav->RootWindow->Name : "";
        const auto& io=ImGui::GetIO();
        const bool keys=!io.WantTextInput && !ImGui::IsAnyItemActive() && !ImGui::GetDragDropPayload() &&
            !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup);
        if (keys && ImGui::IsKeyPressed(ImGuiKey_Delete,false)) {
            result.assetDelete=result.focused=="Asset Browser" && sourceSelected;
            result.entityDelete=result.focused=="Viewport" && result.frame.modelDropId.empty() && result.frame.modelDropIds.empty();
            if (result.entityDelete) document.eraseSelection();
        }
        ImGui::Render(); return result;
    }
    Tick drag() {
        mouse(75,75,true); const auto pressed=tick();
        require(pressed.focused=="Asset Browser" && sourceSelected,"Clicking the source did not establish browser keyboard focus.");
        mouse(500,100,true); tick(); // Source starts dragging after this frame's target was drawn.
        mouse(500,100,true); const auto hovered=tick();
        require(hovered.focused=="Asset Browser","Hovering a drag over the viewport stole focus before delivery.");
        mouse(500,100,false); return tick();
    }
    std::vector<EntityId> place(const EditorFrame& frame) {
        auto ids=frame.modelDropIds; if (!frame.modelDropId.empty()) ids.push_back(frame.modelDropId);
        std::vector<ModelPlacement> placements; for (const auto& id:ids) placements.push_back({id,"Dropped model",Mat4{}});
        return document.createModels(placements);
    }
    SceneDocument document;
    bool sourceSelected{};
private:
    ImGuiContext* context_{};
    std::string type_;
    std::vector<char> bytes_;
};
void deliveryAndDelete(bool batch) {
    Project project; Harness ui(project.root);
    const auto previous=ui.document.createEmpty("Existing entity");
    const std::vector<std::string> expected=batch ? std::vector<std::string>{firstModel,secondModel} : std::vector<std::string>{firstModel};
    ui.source(batch ? "CC_MODEL_ASSETS" : "CC_MODEL_ASSET",payload(expected));
    const auto delivered=ui.drag();
    require(delivered.focused=="Viewport","Delivered model retained browser focus, so Delete would target the source asset.");
    require(delivered.frame.placementPreviewIds==expected,"Valid model IDs were not published for placement preview.");
    require(delivered.frame.modelDropPosition==Vec2{500,100},"Drop position did not match the delivered cursor.");
    if (batch) require(delivered.frame.modelDropIds==expected && delivered.frame.modelDropId.empty(),"Batch delivery changed identity or payload form.");
    else require(delivered.frame.modelDropId==firstModel && delivered.frame.modelDropIds.empty(),"Single delivery changed identity or payload form.");
    const auto created=ui.place(delivered.frame);
    require(created.size()==expected.size() && ui.document.selection().ids==created,"Queued placement did not select its created entities.");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete,true); const auto deleted=ui.tick();
    require(deleted.entityDelete && !deleted.assetDelete,"Delete after drop was routed to asset removal.");
    require(ui.sourceSelected && fs::exists(project.root/firstModel),"Deleting a scene instance changed its source asset selection or file.");
    require(ui.document.scene().contains(previous),"Deleting the dropped model removed the prior entity.");
    for (const auto id:created) require(!ui.document.scene().contains(id),"Entity Delete did not remove the dropped selection.");
    require(ui.document.undo(),"Dropped entity deletion was not undoable.");
    for (const auto id:created) require(ui.document.scene().contains(id),"Undo did not restore the dropped entity's original ID.");
}
void delayedPlacementDoesNotStealFocus() {
    Project project; Harness ui(project.root); ui.source("CC_MODEL_ASSET",payload({firstModel}));
    const auto delivered=ui.drag(); require(delivered.focused=="Viewport","Delivery did not transfer focus.");
    ui.mouse(75,75,true); ui.tick(); ui.mouse(75,75,false); ui.tick();
    require(ImGui::GetCurrentContext()->NavWindow->RootWindow->Name==std::string("Asset Browser"),"An explicit browser click did not own subsequent shortcuts.");
    require(ui.place(delivered.frame).size()==1,"Delayed placement failed.");
    require(ImGui::GetCurrentContext()->NavWindow->RootWindow->Name==std::string("Asset Browser"),"Asynchronous scene publication stole the user's new panel focus.");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete,true); const auto result=ui.tick();
    require(result.assetDelete && !result.entityDelete,"Explicit browser focus did not route Delete to its own selection.");
}
void malformedPayloads() {
    const auto good=payload({firstModel});
    auto embedded=good; embedded.insert(embedded.end(),{'x','\0'});
    auto missing=payload({firstModel,secondModel}); missing.pop_back();
    auto emptyItem=good; emptyItem.push_back('\0');
    const std::vector<std::pair<std::string,std::vector<char>>> cases{
        {"CC_MODEL_ASSET",{}},{"CC_MODEL_ASSET",{'\0'}},{"CC_MODEL_ASSET",{'a','b'}},{"CC_MODEL_ASSET",embedded},
        {"CC_MODEL_ASSETS",missing},{"CC_MODEL_ASSETS",emptyItem},{"CC_TEXTURE_ASSET",good},
        {"CC_PARTICLE_ASSET",{}},{"CC_PARTICLE_ASSET",{'a','b'}},{"CC_PARTICLE_ASSET",embedded},
        {"CC_MATERIAL_ASSET",{}},{"CC_MATERIAL_ASSET",{'a','b'}},{"CC_MATERIAL_ASSET",embedded}};
    for (const auto& [type,bytes]:cases) {
        Project project; Harness ui(project.root); ui.source(type,bytes); const auto result=ui.drag();
        require(result.frame.modelDropId.empty() && result.frame.modelDropIds.empty() && result.frame.placementPreviewIds.empty() && result.frame.particleDropId.empty() &&
            result.frame.materialDragId.empty() && !result.frame.materialDropDelivered && !result.frame.materialAssignment,
            "Malformed or incompatible payload published a partial model drop.");
        require(result.focused=="Asset Browser","An invalid model payload transferred keyboard focus.");
    }
}
void particleDeliveryAndDelete() {
    Project project;Harness ui(project.root);ui.source("CC_PARTICLE_ASSET",payload({particleEffect}));
    const auto delivered=ui.drag();
    require(delivered.focused=="Viewport","Delivered particle effect retained browser focus.");
    require(delivered.frame.particleDropId==particleEffect && delivered.frame.particleDropPosition==Vec2{500,100},"Particle delivery lost its reference or cursor position.");
    require(delivered.frame.modelDropId.empty() && delivered.frame.modelDropIds.empty() && delivered.frame.placementPreviewIds.empty(),"Particle delivery published a model placement.");
    const auto created=ui.document.createParticle(particleEffect,"Dropped particle");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Delete,true);const auto result=ui.tick();
    require(result.entityDelete && !result.assetDelete && !ui.document.scene().contains(created),"Delete after particle delivery targeted the source asset.");
    require(fs::exists(project.root/particleEffect),"Deleting a placed effect deleted its source asset.");
    require(ui.document.undo() && ui.document.scene().find(created)->particle->id==particleEffect,"Undo particle-instance deletion lost its shared asset reference.");
}
void materialHoverAndDelivery() {
    for(const bool deliver:{false,true}) {
        Project project;Harness ui(project.root);ui.source("CC_MATERIAL_ASSET",payload({materialAsset}));
        const auto revision=ui.document.scene().revision();
        const auto dirty=ui.document.dirty();
        const auto history=ui.document.history().size();
        ui.mouse(75,75,true);ui.tick();ui.mouse(500,100,true);ui.tick();
        ui.mouse(500,100,true);const auto hover=ui.tick();
        require(hover.frame.materialDragId.id==materialAsset && hover.frame.materialDragPosition==Vec2{500,100},"Material hover lost its typed asset or cursor position.");
        require(!hover.frame.materialDropDelivered && !hover.frame.materialAssignment,"Material hover delivered an authored assignment.");
        require(hover.focused=="Asset Browser","Material hover stole browser focus.");
        ui.mouse(deliver?500.f:950.f,deliver?100.f:500.f,false);const auto release=ui.tick();
        if(deliver) {
            require(release.frame.materialDropDelivered && release.frame.materialDragId.id==materialAsset,"Material release inside viewport did not publish its exact delivery input.");
            require(release.focused=="Viewport","Delivered material did not transfer shortcut ownership to the viewport.");
        } else {
            require(release.frame.materialDragId.empty() && !release.frame.materialDropDelivered,"Releasing outside viewport delivered a material.");
            require(release.focused=="Asset Browser","Canceled material drag stole browser focus.");
        }
        require(ui.document.scene().revision()==revision && ui.document.dirty()==dirty && ui.document.history().size()==history,
            "Material payload processing changed scene/history before a validated hit.");
        require(release.frame.placementPreviewIds.empty() && release.frame.modelDropIds.empty() && release.frame.particleDropId.empty(),"Material drag published another asset kind.");
    }
}
void asynchronousMaterialTargeting() {
    ViewportMaterialDrop drop;
    const AssetRef<MaterialAsset> material{materialAsset},replacement{"assets/materials/replacement.crimson-material"};
    const std::uint64_t firstEntity=0x123456789abcULL,releaseEntity=firstEntity+1;
    require(drop.update(material,{10,20},false) && drop.needsPick(),"Material hover did not create a new pick intent.");
    require(drop.request(1).pixel==ViewportMaterialDrop::Pixel{10,20},"Hover pick changed its pixel.");
    require(!drop.update(material,{30,40},false) && drop.pending()->serial==1 && !drop.needsPick(),"Pointer motion starved an outstanding hover readback.");
    require(!drop.resolve(1,firstEntity,3) && drop.preview()==MaterialSlotAssignment{firstEntity,3,material},"Hover readback committed or lost the exact mesh slot.");
    require(drop.needsPick(),"A moved cursor did not request its next hover target.");
    drop.request(2);
    require(drop.update(material,{50,60},true) && drop.delivered() && drop.needsPick(),"Release reused a stale hover pick.");
    require(!drop.resolve(2,firstEntity,3) && drop.active(),"An old hover result committed the released material.");
    require(drop.request(3).pixel==ViewportMaterialDrop::Pixel{50,60},"Delivery did not pick the exact release pixel.");
    const auto assignment=drop.resolve(3,releaseEntity,7);
    require(assignment==MaterialSlotAssignment{releaseEntity,7,material} && !drop.active() && !drop.preview(),"Exact release target was not emitted once and cleared.");
    require(!drop.resolve(3,firstEntity,1),"A repeated release result emitted another assignment.");
    drop.update(material,{1,2},false);drop.request(4);drop.cancel();
    require(!drop.resolve(4,firstEntity,0) && !drop.preview() && !drop.active(),"Canceled hover restored a stale preview.");
    drop.update(material,{1,2},false);drop.request(40);drop.resolve(40,firstEntity,0);
    require(drop.needsPick(),"A stationary cursor stopped following animated or newly resident geometry.");
    drop.cancel();
    drop.update(material,{1,2},false);drop.request(5);
    require(drop.update(replacement,{1,2},false) && !drop.preview(),"Changing dragged material kept the old preview.");
    require(!drop.resolve(5,firstEntity,0),"A superseded material produced an assignment.");
    drop.update(replacement,{1,2},true);drop.request(6);
    require(!drop.resolve(6,0,UINT32_MAX) && !drop.active(),"Dropping on the background retained or assigned the previous hover target.");
}
}
int main() {
    try { deliveryAndDelete(false); deliveryAndDelete(true); delayedPlacementDoesNotStealFocus(); malformedPayloads();particleDeliveryAndDelete();materialHoverAndDelivery();asynchronousMaterialTargeting();
        std::cout<<"Viewport asset drop: focus, Delete routing, undo, delayed placement, material hover/release/cancel and malformed payloads passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
