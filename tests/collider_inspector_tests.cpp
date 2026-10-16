#include "tools/ColliderInspector.h"
#include "scene/SceneDocument.h"
#include "scene/BuiltinModels.h"
#include <imgui_internal.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-collider-inspector-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error&&path.parent_path()==temp&&path.filename().string().starts_with("crimson-collider-inspector-"))std::filesystem::remove_all(path,error);}
};
// Input is delivered only to this private CPU ImGui context, with no OS window.
class Inspector {
public:
    explicit Inspector(const std::filesystem::path& root):document(root) {
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={900,700};io.DeltaTime=1.f/60;
        io.ConfigInputTrickleEventQueue=false;io.ConfigFlags|=ImGuiConfigFlags_NavEnableKeyboard;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        entity=document.createCollider();configure(ColliderShape::Capsule);document.select({entity},entity);tick();tick();
    }
    ~Inspector(){ImGui::DestroyContext(context);}
    SceneCollider value() const {return *document.scene().find(entity)->collider;}
    void configure(ColliderShape shape) {
        auto record=*document.scene().find(entity);record.collider=colliderForShape(document.scene(),entity,*record.collider,shape);
        require(document.updateEntity(entity,record,"Choose fixture shape"),"Cannot configure collider Inspector fixture.");
    }
    void tick() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({600,600});ImGui::SetNextWindowFocus();
        ImGui::Begin("Inspector",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove);
        if(!activation.empty()) {
            ImGui::PushID("ColliderProperties");context->NavActivateId=ImGui::GetID(activation.c_str());ImGui::PopID();
            context->NavActivateFlags=ImGuiActivateFlags_PreferInput;activation.clear();
        }
        const ColliderInspectorTransactions transactions{
            [&](ColliderControl control,const char* label){
                if(document.transactionActive()&&state.editing&&state.active==control)return true;
                if(document.transactionActive())document.commit();return document.begin(label);
            },[&](ColliderControl){document.commit();}};
        drawColliderInspector(document,state,*document.scene().find(entity),{},transactions);
        ImGui::End();ImGui::Render();
    }
    void activate(const char* label){activation=label;tick();require(state.editing&&document.transactionActive(),"A numeric Inspector field did not acquire its transaction.");}
    void type(const char* text){ImGui::GetIO().AddInputCharactersUTF8(text);tick();}
    void enter(){ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,true);tick();ImGui::GetIO().AddKeyEvent(ImGuiKey_Enter,false);tick();tick();}
    void escape(){ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);tick();ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);tick();tick();}
    SceneDocument document;
    ColliderInspectorState state;
    EntityId entity{};
    ImGuiContext* context{};
private:std::string activation;
};
void sourceInferenceAndFallbacks() {
    Scene scene;EntityRecord model;model.id=1;model.name="Visual";model.model=AssetRef<ModelAsset>{BuiltinBoxModelId};
    EntityRecord collision;collision.id=2;collision.name="Collider";collision.parent=1;collision.collider.emplace();scene.apply({model,collision},{});
    auto authored=*collision.collider;authored.size={3,4,5};authored.radius=2;authored.height=1;
    const auto mesh=colliderForShape(scene,2,authored,ColliderShape::Trimesh);
    require(mesh.source.id==BuiltinBoxModelId&&mesh.size==authored.size&&mesh.radius==2,"Trimesh did not infer its visual ancestor or preserve primitive fields.");
    const auto capsule=colliderForShape(scene,2,mesh,ColliderShape::Capsule);
    require(capsule.radius==2&&capsule.height==4&&capsule.source==mesh.source,"Capsule selection lost its source or violated height>=diameter.");
    require(colliderForShape(scene,2,capsule,ColliderShape::Sphere).height==4,"Changing shape discarded disconnected dimension fallbacks.");
    Scene empty;EntityRecord standalone;standalone.id=3;standalone.name="Standalone collider";standalone.collider.emplace();empty.apply({standalone},{});
    require(colliderForShape(empty,3,*standalone.collider,ColliderShape::Trimesh).source.empty(),"A standalone Trimesh fabricated a model source.");
}
void numericHistoryAndSynchronization() {
    Project project;Inspector ui(project.root);const auto initial=ui.value();const auto history=ui.document.history().size();
    ui.activate("Radius (m)");ui.type("2");
    require(ui.value().radius==2&&ui.value().height==4,"Capsule radius input did not update its total height immediately.");
    ui.enter();require(!ui.document.transactionActive()&&ui.document.history().size()==history+1,"Numeric entry did not commit one undo command.");
    require(ui.document.undo()&&ui.value()==initial,"Undo did not restore both coupled capsule dimensions.");
    require(ui.document.redo()&&ui.value().radius==2&&ui.value().height==4,"Redo lost coupled collision dimensions.");
    ui.tick();ui.activate("Height (m)");ui.type("1");ui.enter();
    require(ui.value().height==1&&ui.value().radius==.5f,"Capsule height input did not constrain its radius.");
    const auto before=ui.value();const auto count=ui.document.history().size();
    ui.activate("Radius (m)");ui.type("3");require(ui.value().radius==3,"Cancellation fixture never changed radius.");ui.escape();
    require(ui.value()==before&&!ui.document.transactionActive()&&ui.document.history().size()==count,"Escape left a numeric collider mutation or history entry.");
    auto record=*ui.document.scene().find(ui.entity);record.collider->radius=1.25f;record.collider->height=3;
    require(ui.document.updateEntity(ui.entity,record,"Viewport dimensions"),"Cannot publish external handle dimensions.");ui.tick();
    const auto unchanged=ui.value();ui.activate("Radius (m)");
    require(std::string(ui.context->InputTextState.GetText()).find("1.250")!=std::string::npos,"Inspector input did not refresh from viewport-authored dimensions.");
    ui.escape();require(ui.value()==unchanged,"Inspecting a new viewport value changed it.");
    ui.activate("Radius (m)");ui.type("2");
    releaseColliderInspector(ui.state);require(ui.document.commit()&&ui.document.undo(),"Explicit Edit menu completion did not enable Undo.");
    for(unsigned i=0;i<3;++i)ui.tick();
    require(ui.value()==unchanged&&!ui.document.transactionActive(),"An Inspector text buffer reapplied its value after menu Undo.");
    ui.activate("Radius (m)");ui.type("3");ui.document.cancel();ui.tick();ui.tick();
    require(ui.value()==unchanged&&!ui.document.transactionActive()&&!ui.state.editing,"An external scene cancellation left a live Collider input buffer.");
    ui.configure(ColliderShape::Trimesh);const auto mesh=ui.value();const auto meshHistory=ui.document.history().size();
    for(unsigned i=0;i<8;++i)ui.tick();
    require(ui.value()==mesh&&ui.document.history().size()==meshHistory&&!ui.document.transactionActive(),"Read-only Trimesh inspection changed geometry or history.");
}
}
int main(){try{sourceInferenceAndFallbacks();numericHistoryAndSynchronization();std::cout<<"Collider Inspector source, numeric history and synchronization passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
