#include "tools/PrimitiveMeshInspector.h"
#include "scene/SceneDocument.h"
#include <imgui_internal.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-primitive-inspector-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-primitive-inspector-"))std::filesystem::remove_all(path,error);}
};
// Events target a private CPU ImGui context, without an OS window or desktop input.
class Inspector {
public:
    Inspector(const std::filesystem::path& root,bool cylinder=false):document(root) {
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={900,700};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        entity=cylinder?document.createCylinder():document.createSphere();tick();tick();
    }
    ~Inspector(){ImGui::DestroyContext(context);}
    PrimitiveMeshSettings value()const{return document.scene().find(entity)->primitive.value_or(PrimitiveMeshSettings{});}
    void tick() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({600,600});ImGui::SetNextWindowFocus();
        ImGui::Begin("Inspector",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove);
        if(!activation.empty()) {
            ImGui::PushID("PrimitiveMeshProperties");ImGui::PushID("Geometry");context->NavActivateId=ImGui::GetID(activation.c_str());ImGui::PopID();ImGui::PopID();
            context->NavActivateFlags=ImGuiActivateFlags_PreferInput;activation.clear();
        }
        const PrimitiveMeshInspectorTransactions transactions{
            [&](PrimitiveMeshControl control,const char* label){
                if(document.transactionActive()&&state.editing&&state.active==control)return true;
                if(document.transactionActive())document.commit();return document.begin(label);
            },[&](PrimitiveMeshControl){document.commit();}};
        drawPrimitiveMeshInspector(document,state,*document.scene().find(entity),transactions);
        ImGui::End();ImGui::Render();
        require(context->ErrorCountCurrentFrame==0,"Primitive Inspector left invalid ImGui state.");
    }
    void activate(const char* label){activation=label;tick();require(state.editing&&document.transactionActive(),"Primitive numeric entry did not acquire its transaction.");}
    void type(const char* text){ImGui::GetIO().AddInputCharactersUTF8(text);tick();}
    void key(ImGuiKey key){ImGui::GetIO().AddKeyEvent(key,true);tick();ImGui::GetIO().AddKeyEvent(key,false);tick();tick();}
    SceneDocument document;PrimitiveMeshInspectorState state;EntityId entity{};ImGuiContext* context{};
private:std::string activation;
};
void radiusTransactionsAndCancellation() {
    Project project;Inspector ui(project.root);const auto initial=ui.value();const auto history=ui.document.history().size();
    const auto transform=ui.document.scene().find(ui.entity)->local;
    ui.activate("Radius (m)");ui.type("2.25");
    require(ui.value().radius==2.25f && ui.document.transactionActive() && ui.document.history().size()==history,
        "Sphere radius did not update live within one gesture.");
    ui.key(ImGuiKey_Enter);require(!ui.document.transactionActive()&&ui.document.history().size()==history+1,"Sphere radius entry did not commit one command.");
    require(ui.document.undo()&&ui.value()==initial&&ui.document.redo()&&ui.value().radius==2.25f,"Sphere radius Undo/Redo lost geometry parameters.");
    ui.tick();const auto before=ui.value();const auto count=ui.document.history().size();
    ui.activate("Radius (m)");ui.type("3.5");require(ui.value().radius==3.5f,"Sphere cancellation fixture did not change radius.");ui.key(ImGuiKey_Escape);
    require(ui.value()==before&&!ui.document.transactionActive()&&ui.document.history().size()==count,"Escape published Sphere radius or added history.");
    ui.tick();require(ui.value()==before,"Canceled primitive numeric text reapplied its value.");
    ui.activate("Radius (m)");ui.type("4.25");releasePrimitiveMeshInspector(ui.state);
    require(ui.document.commit()&&ui.document.undo(),"Finishing a primitive field did not release it for menu Undo.");
    ui.tick();ui.tick();require(ui.value()==before&&!ui.document.transactionActive(),"Stale primitive text reapplied after menu Undo.");
    require(ui.document.scene().find(ui.entity)->local==transform&&!ui.document.scene().find(ui.entity)->collider,
        "Visual radius tuning changed the entity transform or added collision.");
}
void cylinderSideCount() {
    Project project;Inspector ui(project.root,true);const auto before=ui.value();const auto history=ui.document.history().size();
    ui.activate("Sides");ui.type("7");require(ui.value().sides==7,"Cylinder sides did not update its authored topology.");ui.key(ImGuiKey_Enter);
    require(ui.document.history().size()==history+1&&!ui.document.transactionActive()&&ui.value().radius==before.radius&&ui.value().height==before.height,
        "Cylinder topology entry changed dimensions or did not form one command.");
    require(ui.document.undo()&&ui.value()==before&&ui.document.redo()&&ui.value().sides==7,"Cylinder side-count history did not restore topology.");
}
}
int main(){try{radiusTransactionsAndCancellation();cylinderSideCount();std::cout<<"Primitive Inspector numeric history, topology, cancellation and synchronization passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
