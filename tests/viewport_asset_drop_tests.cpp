#include "editor/ViewportAssetDrop.h"
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
        {"CC_PARTICLE_ASSET",{}},{"CC_PARTICLE_ASSET",{'a','b'}},{"CC_PARTICLE_ASSET",embedded}};
    for (const auto& [type,bytes]:cases) {
        Project project; Harness ui(project.root); ui.source(type,bytes); const auto result=ui.drag();
        require(result.frame.modelDropId.empty() && result.frame.modelDropIds.empty() && result.frame.placementPreviewIds.empty() && result.frame.particleDropId.empty(),
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
}
int main() {
    try { deliveryAndDelete(false); deliveryAndDelete(true); delayedPlacementDoesNotStealFocus(); malformedPayloads();particleDeliveryAndDelete();
        std::cout<<"Viewport asset drop: focus, Delete routing, undo, delayed placement and malformed payloads passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
