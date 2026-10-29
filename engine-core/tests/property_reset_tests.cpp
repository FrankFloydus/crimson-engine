#include "tools/PropertyReset.h"
#include "scene/SceneDocument.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct Fixture {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("crimson-property-reset-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    SceneDocument document{root};EntityId entity{};ImGuiContext* context{};
    ImVec2 field{},reset{};unsigned commits{};bool resetClicked{};
    Fixture() {
        context=ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={800,500};
        io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        entity=document.createPointLight();set(80.f);tick();tick();
    }
    ~Fixture() {
        ImGui::DestroyContext(context);
        std::error_code error;const auto temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);if(error)return;
        const auto target=std::filesystem::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temp&&target.filename().string().starts_with("crimson-property-reset-"))std::filesystem::remove_all(target,error);
    }
    float value()const{return document.scene().find(entity)->point->intensity;}
    void set(float value) {
        document.edit("Seed intensity",[id=entity,value](auto& records,auto&){for(auto& item:records)if(item.id==id)item.point->intensity=value;});
    }
    void tick() {
        ImGui::NewFrame();ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({600,300});
        ImGui::Begin("Properties",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        float current=value();ImGui::SetNextItemWidth(300);
        const auto expected=ImGui::GetID("Intensity");
        const bool changed=resettableProperty(current,ScenePointLight{}.intensity,[&]{return ImGui::DragFloat("Intensity",&current,.1f,0,10000,"%.2f");});
        resetClicked=propertyResetClicked();
        require(ImGui::GetItemID()==expected,"Reset helper changed the ordinary control identity.");
        const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        field={a.x+90,(a.y+b.y)*.5f};reset={b.x+ImGui::GetStyle().ItemInnerSpacing.x+ImGui::GetFrameHeight()*.5f,(a.y+b.y)*.5f};
        if((ImGui::IsItemActivated()||changed)&&!document.transactionActive())document.begin("Change intensity");
        if(changed)document.update([id=entity,current](auto& records,auto&){for(auto& item:records)if(item.id==id)item.point->intensity=current;});
        if((ImGui::IsItemDeactivated()||(changed&&!ImGui::IsItemActive()))&&document.transactionActive()) {
            if(document.commit())++commits;
        }
        if(resetClicked)require(!ImGui::IsItemActive()&&ImGui::IsItemEdited()&&ImGui::IsItemDeactivatedAfterEdit(),"Reset did not preserve atomic edit status.");
        ImGui::End();ImGui::Render();
    }
    void move(ImVec2 point){ImGui::GetIO().AddMousePosEvent(point.x,point.y);tick();}
    void mouse(bool down){ImGui::GetIO().AddMouseButtonEvent(0,down);tick();}
    void clickReset(){move(reset);mouse(true);mouse(false);}
};
}

int main()try {
    // Events target a private CPU ImGui context, never the user's desktop.
    Fixture f;const auto initialCommits=f.commits;f.clickReset();
    require(f.value()==ScenePointLight{}.intensity&&f.resetClicked,"Reset icon did not apply the actual point-light default.");
    require(!f.document.transactionActive()&&f.commits==initialCommits+1,"Reset did not finish exactly one history command.");
    require(f.document.undo()&&f.value()==80.f,"Undo did not restore the value before reset.");
    require(f.document.redo()&&f.value()==ScenePointLight{}.intensity,"Redo did not restore the reset default.");
    const auto atDefault=f.commits;f.tick();f.clickReset();
    require(f.commits==atDefault&&!f.document.transactionActive(),"Disabled default icon created an edit.");
    f.set(80.f);f.tick();const auto beforeDrag=f.commits;f.move(f.field);f.mouse(true);
    f.move({f.field.x+25,f.field.y});f.move({f.field.x+55,f.field.y});
    require(f.document.transactionActive()&&f.value()>80.f,"Reset decoration broke ordinary live dragging.");
    f.mouse(false);
    require(!f.document.transactionActive()&&f.commits==beforeDrag+1,"Ordinary drag no longer forms one undo command.");
    require(f.document.undo()&&f.value()==80.f,"Drag undo did not restore its baseline.");
    std::cout<<"Property reset identity, defaults, disabled state, drag and history passed.\n";
    return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
