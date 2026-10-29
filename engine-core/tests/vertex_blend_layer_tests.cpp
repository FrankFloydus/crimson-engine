#include "editor/VertexBlendLayerEdit.h"
#include "tools/VertexBlendLayerControls.h"
#include "resources/AssetId.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace crimson {
struct VertexBlendLayerControlsTestAccess {
    static bool has(const VertexBlendLayerControls& controls,const std::string& label) {
        return std::any_of(controls.controls_.begin(),controls.controls_.end(),[&](const auto& item){return item.label==label;});
    }
    static ImVec2 point(const VertexBlendLayerControls& controls,const std::string& label,float fraction=.25f,bool slider=false) {
        for(const auto& item:controls.controls_)if(item.label==label) {
            const float trailing=slider?ImGui::CalcTextSize(label.c_str()).x+ImGui::GetStyle().ItemInnerSpacing.x:0;
            return {item.minimum[0]+(item.maximum[0]-item.minimum[0]-trailing)*fraction,(item.minimum[1]+item.maximum[1])*.5f};
        }
        throw std::runtime_error("Missing vertex blend layer control: "+label);
    }
};
}

namespace {
using namespace crimson;
namespace fs=std::filesystem;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Project {
    fs::path root=fs::temp_directory_path()/("crimson-blend-layer-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){fs::create_directories(root);}
    ~Project() {
        std::error_code error;
        const auto temporary=fs::weakly_canonical(fs::temp_directory_path(),error);if(error)return;
        const auto target=fs::weakly_canonical(root,error);
        if(!error&&target.parent_path()==temporary&&target.filename().string().starts_with("crimson-blend-layer-"))fs::remove_all(target,error);
    }
};
struct Fixture {
    Project project;
    SceneDocument document{project.root};
    std::string material=canonicalAssetId("assets/materials/shared-blend.crimson-material");
    EntityId first{},second{};
    std::shared_ptr<const MeshPaintPayload> paint;
    std::vector<std::uint8_t> paintBytes;
    Fixture() {
        const auto model=canonicalAssetId("assets/models/blend-model.glb");
        first=document.createModel(model,"First painted mesh");second=document.createModel(model,"Second painted mesh");
        const auto initial=makeMeshPaintPayload(4);
        const std::array<MeshPaintChange,3> changes{{{0,{30,100,20}},{1,{150,25,10}},{3,{0,50,190}}}};
        paint=updateMeshPaintPayload(initial,changes);paintBytes=meshPaintBytes(*paint);
        require(document.edit("Painted meshes sharing one material",[&](auto& records,auto&) {
            for(auto& record:records)if(record.id==first||record.id==second) {
                record.modelMaterials={AssetRef<MaterialAsset>{material},AssetRef<MaterialAsset>{material}};
                record.meshPaint=SceneMeshPaint{0,MeshPaintRecipeVersion,{{0,0,0,123,paint}}};
            }
        }),"Could not author blend-layer scene fixture.");
        document.select({first},first);
        require(document.save("Blend layer controls fixture"),document.status().c_str());
    }
    VertexBlendLayerTarget target(std::uint32_t layer=1,EntityId entity=NoEntity,std::uint32_t slot=0) const {
        return {entity?entity:first,slot,layer,material};
    }
    std::optional<MaterialBlendLayer> value(std::uint32_t layer=1,EntityId entity=NoEntity,std::uint32_t slot=0) const {
        const auto record=document.scene().find(entity?entity:first);
        const auto stored=record?findSceneBlendLayer(record->blendSettings,slot,material,layer):nullptr;
        return stored?std::optional<MaterialBlendLayer>{*stored}:std::nullopt;
    }
    void unchangedPaint() const {
        for(const auto id:{first,second}) {
            const auto record=document.scene().find(id);
            require(record&&record->meshPaint&&record->meshPaint->draws.size()==1&&record->meshPaint->draws.front().weights==paint,
                "Editing blend settings replaced authored paint payload ownership.");
            require(meshPaintBytes(*record->meshPaint->draws.front().weights)==paintBytes,"Editing blend settings changed RGB8 paint bytes.");
        }
        require(meshPaintBytes(*paint)==paintBytes,"Editing blend settings mutated immutable saved paint pages.");
    }
};

void editHistoryIsolationAndNoOp() {
    Fixture fixture;auto& document=fixture.document;VertexBlendLayerEdit edit;
    const auto first=*document.scene().find(fixture.first),second=*document.scene().find(fixture.second);
    const auto history=document.history().size();
    require(edit.begin(document,fixture.target(),"No-op layer edit")&&edit.update(document,std::nullopt)&&edit.finish(document),
        "An empty layer edit could not complete.");
    require(document.history().size()==history&&!document.dirty()&&!document.transactionActive(),"An unchanged blend edit created history or dirty state.");
    MaterialBlendLayer value;value.heightBlend=true;value.heightInfluence=.75f;value.blendSoftness=.45f;
    require(edit.begin(document,fixture.target(),"Live layer drag"),"Could not begin layer edit.");
    const auto transaction=document.transactionId();
    require(edit.update(document,value)&&fixture.value()&&*fixture.value()==value,"A layer drag did not publish its first live value.");
    value.blendSoftness=.65f;value.tint={.7f,.8f,1.4f};
    require(edit.update(document,value)&&document.transactionId()==transaction&&document.history().size()==history,
        "One layer drag changed transaction identity or added intermediate history entries.");
    require(!fixture.value(0)&&!fixture.value(2)&&!fixture.value(1,fixture.second)&&!fixture.value(1,fixture.first,1)&&
        sameEntity(*document.scene().find(fixture.second),second),"Layer settings leaked into Base, another layer, material slot or mesh sharing the asset.");
    fixture.unchangedPaint();
    require(edit.finish(document)&&!edit.active()&&!document.transactionActive()&&document.history().size()==history+1,
        "Releasing one layer edit did not produce exactly one scene command.");
    const auto changed=*document.scene().find(fixture.first);
    require(document.begin("Immediate next editor command"),"Releasing layer settings retained editor command ownership.");document.cancel();
    require(document.undo()&&sameEntity(*document.scene().find(fixture.first),first)&&sameEntity(*document.scene().find(fixture.second),second),
        "Undo did not restore the exact selected-mesh state.");
    fixture.unchangedPaint();
    require(document.redo()&&sameEntity(*document.scene().find(fixture.first),changed),"Redo did not restore the complete layer edit.");
    const auto unchangedHistory=document.history().size();
    require(edit.begin(document,fixture.target(),"Unchanged existing settings")&&edit.update(document,value)&&edit.finish(document)&&
        document.history().size()==unchangedHistory,"Reapplying identical layer settings added an undo entry.");
    require(edit.begin(document,fixture.target(),"Canceled layer drag"),"Could not start cancel test.");
    value.heightScale=3.5f;require(edit.update(document,value),"Canceled layer drag did not update.");edit.cancel(document);
    require(!edit.active()&&!document.transactionActive()&&sameEntity(*document.scene().find(fixture.first),changed)&&
        document.history().size()==unchangedHistory,"Cancel did not restore exact prior layer state without history.");
    fixture.unchangedPaint();
}

void editForeignTransactionAndTransientGuards() {
    for(const bool finish:{false,true}) {
        Fixture fixture;auto& document=fixture.document;VertexBlendLayerEdit edit;MaterialBlendLayer value;value.heightScale=2;
        require(edit.begin(document,fixture.target(),"Superseded layer gesture")&&edit.update(document,value),"Could not prepare stale edit owner.");
        document.cancel();
        require(document.begin("Foreign inspector edit")&&document.update([&](auto& records,auto&) {
            for(auto& record:records)if(record.id==fixture.second)record.name="Foreign edit remains live";
        }),"Could not prepare foreign scene transaction.");
        const auto foreign=document.transactionId(),history=document.history().size();
        require(!edit.update(document,value),"A stale layer gesture changed a foreign transaction.");
        if(finish)require(!edit.finish(document),"A stale layer gesture claimed to finish a foreign transaction.");else edit.cancel(document);
        require(document.transactionActive()&&document.transactionId()==foreign&&document.history().size()==history&&
            document.scene().find(fixture.second)->name=="Foreign edit remains live","A stale layer owner canceled or committed a foreign edit.");
        require(!edit.begin(document,fixture.target(),"Blocked layer edit")&&document.transactionId()==foreign,
            "A layer gesture took over an existing foreign transaction.");
        document.cancel();fixture.unchangedPaint();
    }
    Fixture fixture;VertexBlendLayerEdit edit;SceneDocument runtime(fixture.project.root);
    require(runtime.resetTransientFrom(fixture.document)&&!edit.begin(runtime,fixture.target(),"Invalid Play edit"),
        "Layer-property authoring was allowed in the transient Play document.");
    auto invalid=fixture.target();invalid.layer=4;
    require(!edit.begin(fixture.document,invalid,"Invalid layer")&&!fixture.document.transactionActive(),"An invalid layer acquired document ownership.");
    invalid=fixture.target();invalid.material.clear();
    require(!edit.begin(fixture.document,invalid,"Empty material")&&!fixture.document.transactionActive(),"An empty Blend identity acquired document ownership.");
}

struct Ui:Fixture {
    ImGuiContext* context=ImGui::CreateContext();
    VertexBlendLayerControls controls;
    VertexBlendLayerTarget selected=target();
    MaterialBlendLayer inherited;
    Ui() {
        auto& io=ImGui::GetIO();io.IniFilename=nullptr;io.DisplaySize={1000,1200};io.DeltaTime=1.f/60;io.ConfigInputTrickleEventQueue=false;
        unsigned char* pixels{};int width{},height{};io.Fonts->GetTexDataAsRGBA32(&pixels,&width,&height);io.Fonts->SetTexID(static_cast<ImTextureID>(1));
        frame(3);
    }
    ~Ui(){controls.cancel(document);ImGui::DestroyContext(context);}
    void frame(int count=1,bool command=false,bool undoBeforeDraw=false) {
        for(int index=0;index<count;++index) {
            ImGui::NewFrame();
            controls.prepareUi(document,ImGui::IsMouseDown(ImGuiMouseButton_Left),command,ImGui::IsKeyPressed(ImGuiKey_Escape,false));
            if(undoBeforeDraw)require(document.undo(),"The pre-panel scene Undo command did not see the completed layer edit.");
            ImGui::SetNextWindowPos({20,20});ImGui::SetNextWindowSize({720,1120});
            ImGui::Begin("Private selected-mesh blend controls",nullptr,ImGuiWindowFlags_NoSavedSettings);
            ImGui::PushItemWidth(420);controls.draw(document,selected,inherited);ImGui::PopItemWidth();
            ImGui::End();ImGui::Render();
            require(ImGui::GetDrawData()&&ImGui::GetDrawData()->Valid,"Layer controls produced invalid private ImGui draw data.");
        }
    }
    bool has(const char* label) const {return VertexBlendLayerControlsTestAccess::has(controls,label);}
    ImVec2 point(const char* label,float fraction=.25f,bool slider=false) const {return VertexBlendLayerControlsTestAccess::point(controls,label,fraction,slider);}
    void move(ImVec2 position){ImGui::GetIO().AddMousePosEvent(position.x,position.y);frame();}
    void click(const char* label) {
        move(point(label));ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();
        ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame(2);
    }
    void startSlider(const char* label,float fraction) {
        move(point(label,fraction,true));ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();
    }
    void release(){ImGui::GetIO().AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();}
    void key(ImGuiKey key) {
        ImGui::GetIO().AddKeyEvent(key,true);frame();ImGui::GetIO().AddKeyEvent(key,false);frame(2);
    }
    void startNumber(const char* label) {
        move(point(label,.35f,true));auto& io=ImGui::GetIO();
        io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();io.AddMouseButtonEvent(ImGuiMouseButton_Left,true);frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left,false);frame();io.AddKeyEvent(ImGuiMod_Ctrl,false);frame(2);
        require(ImGui::GetCurrentContext()->TempInputId!=0&&ImGui::GetIO().WantTextInput,
            "Ctrl-click did not open the actual numeric text field.");
    }
    void replaceNumber(const char* value) {
        auto& io=ImGui::GetIO();io.AddKeyEvent(ImGuiMod_Ctrl,true);frame();key(ImGuiKey_A);
        io.AddKeyEvent(ImGuiMod_Ctrl,false);frame();io.AddInputCharactersUTF8(value);frame(2);
    }
};

void uiSelectedLayerAndSceneOnlyCheckbox() {
    Ui ui;const auto baseline=*ui.document.scene().find(ui.first);const auto history=ui.document.history().size();
    const auto inherited=ui.inherited;
    require(ui.has("Height Blend")&&!ui.has("Height Influence")&&!ui.has("Blend Softness")&&ui.has("Height Scale")&&
        ui.has("Normal Influence")&&ui.has("Roughness Influence"),"Overlay layer controls do not match the selected layer and Height Blend state.");
    ui.selected.layer=0;ui.frame(2);
    require(!ui.has("Height Blend")&&!ui.has("Height Influence")&&!ui.has("Blend Softness")&&!ui.has("Normal Influence")&&
        !ui.has("Roughness Influence")&&ui.has("Height Scale")&&ui.has("Height Midpoint")&&ui.has("Tint")&&ui.has("Roughness Multiplier"),
        "Base showed overlay controls or lost its own height and appearance controls.");
    ui.selected.layer=1;ui.frame(2);ui.click("Height Blend");
    require(ui.value()&&ui.value()->heightBlend&&ui.has("Height Influence")&&ui.has("Blend Softness")&&
        !ui.controls.active()&&!ui.document.transactionActive()&&ui.document.history().size()==history+1,
        "Clicking Height Blend did not commit one immediate scene-only edit and expose its controls.");
    require(ui.inherited==inherited&&!ui.value(0)&&!ui.value(2)&&!ui.value(1,ui.second),"A checkbox changed inherited material data or another mesh/layer.");
    ui.unchangedPaint();
    require(ui.document.save(),ui.document.status().c_str());
    SceneDocument loaded(ui.project.root);require(loaded.load(ui.document.assetId())&&!loaded.dirty(),"Could not reopen scene-owned layer settings.");
    const auto loadedRecord=loaded.scene().find(ui.first);
    const auto saved=findSceneBlendLayer(loadedRecord->blendSettings,0,ui.material,1);
    require(saved&&saved->heightBlend&&loaded.scene().find(ui.second)->blendSettings.empty(),"Saved checkbox settings escaped the selected mesh or were lost.");
    for(const auto& entry:fs::recursive_directory_iterator(ui.project.root))if(entry.is_regular_file())
        require(entry.path().extension()!=".crimson-material","A scene layer checkbox created or saved a material asset.");
    require(ui.document.undo()&&sameEntity(*ui.document.scene().find(ui.first),baseline),"Checkbox Undo did not restore exact paint and layer state.");
    require(ui.document.redo()&&ui.value()&&ui.value()->heightBlend,"Checkbox Redo lost the saved scene override.");
    ui.unchangedPaint();
}

void uiSliderReleaseUndoAndCancel() {
    Ui ui;ui.click("Height Blend");const auto baseline=*ui.document.scene().find(ui.first);const auto history=ui.document.history().size();
    ui.startSlider("Blend Softness",.35f);
    require(ui.controls.active()&&ui.document.transactionActive()&&ui.value()&&ui.value()->blendSoftness!=.2f,
        "Pressing Blend Softness did not start a live scene gesture.");
    const auto transaction=ui.document.transactionId();const auto firstValue=ui.value()->blendSoftness;
    ui.move(ui.point("Blend Softness",.8f,true));
    require(ui.document.transactionId()==transaction&&ui.value()->blendSoftness>firstValue&&ui.document.history().size()==history,
        "Dragging Blend Softness did not publish live values inside one transaction.");
    ui.unchangedPaint();ui.release();
    require(!ui.controls.active()&&!ui.document.transactionActive()&&ui.document.history().size()==history+1,
        "Slider release retained layer/document ownership or split the drag into history entries.");
    const auto committed=*ui.document.scene().find(ui.first);
    require(ui.document.begin("Immediate command after slider release"),"Layer release delayed the next editor command.");ui.document.cancel();
    require(ui.document.undo()&&sameEntity(*ui.document.scene().find(ui.first),baseline),"Slider Undo did not restore the exact pre-drag state.");
    require(ui.document.redo()&&sameEntity(*ui.document.scene().find(ui.first),committed),"Slider Redo lost final live values.");ui.frame(2);
    const auto beforeCancel=ui.document.history().size();ui.startSlider("Height Influence",.3f);
    ui.move(ui.point("Height Influence",.6f,true));
    require(ui.controls.active()&&!sameEntity(*ui.document.scene().find(ui.first),committed),"Escape fixture did not change live layer values.");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,true);ui.frame();
    require(!ui.controls.active()&&!ui.document.transactionActive()&&sameEntity(*ui.document.scene().find(ui.first),committed)&&
        ui.document.history().size()==beforeCancel,"Escape did not cancel a layer gesture exactly and immediately.");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape,false);ui.release();ui.frame(2);
    require(sameEntity(*ui.document.scene().find(ui.first),committed)&&!ui.document.transactionActive(),"Releasing after Escape reapplied canceled layer settings.");
    ui.startSlider("Height Influence",.2f);
    require(ui.controls.active()&&ui.document.transactionActive(),"Escape suppressed a later independent layer edit.");
    ui.release();require(ui.document.undo()&&sameEntity(*ui.document.scene().find(ui.first),committed),"A new edit after Escape did not retain independent undo.");
    ui.unchangedPaint();
}

void uiSwitchAndForeignOwnership() {
    Ui ui;ui.click("Height Blend");const auto history=ui.document.history().size();
    ui.startSlider("Height Influence",.3f);const auto oldLayer=*ui.value();
    require(ui.controls.active(),"Layer-switch fixture did not own a gesture.");
    ui.selected.layer=2;ui.frame();
    require(!ui.controls.active()&&!ui.document.transactionActive()&&ui.document.history().size()==history+1&&
        *ui.value(1)==oldLayer&&!ui.value(2)&&!ui.has("Height Influence"),"Switching selected layer failed to finish the previous layer and display the new one.");
    ui.release();ui.click("Height Blend");
    require(ui.value(2)&&ui.value(2)->heightBlend&&*ui.value(1)==oldLayer,"Editing the newly selected layer modified the previous layer.");
    ui.selected.entity=ui.second;ui.document.select({ui.second},ui.second);ui.frame(2);
    require(!ui.has("Height Influence")&&!ui.value(2,ui.second),"A mesh sharing the Blend material inherited another occurrence's settings.");
    const auto beforeForeign=*ui.document.scene().find(ui.second);
    require(ui.document.begin("Foreign active inspector")&&ui.document.update([&](auto& records,auto&) {
        for(auto& record:records)if(record.id==ui.second)record.name="Foreign transaction";
    }),"Could not prepare UI foreign-transaction guard.");
    const auto transaction=ui.document.transactionId();ui.frame();ui.click("Height Blend");
    ui.controls.cancel(ui.document);ui.controls.finish(ui.document);
    require(ui.document.transactionActive()&&ui.document.transactionId()==transaction&&ui.document.scene().find(ui.second)->name=="Foreign transaction"&&
        !ui.value(2,ui.second),"Disabled layer controls or cleanup stole a foreign document transaction.");
    ui.document.cancel();require(sameEntity(*ui.document.scene().find(ui.second),beforeForeign),"Foreign gesture cleanup failed to retain its independent undo baseline.");
    ui.unchangedPaint();
}

void uiTypedValuesAcceptCancelAndCommand() {
    Ui ui;const auto baseline=*ui.document.scene().find(ui.first);const auto history=ui.document.history().size();
    ui.startNumber("Height Scale");
    require(!ui.controls.active()&&!ui.document.transactionActive()&&ui.document.history().size()==history,
        "Opening numeric text input changed the scene or created a gesture.");
    ui.replaceNumber("2.750");
    require(ui.controls.active()&&ui.document.transactionActive()&&ui.value()&&ui.value()->heightScale==2.75f,
        "Typing a numeric layer value did not publish the live scene value.");
    ui.frame(3);
    require(ui.controls.active()&&ui.document.transactionActive(),"Mouse-up prematurely finished a numeric text edit.");
    ui.key(ImGuiKey_Enter);
    require(!ui.controls.active()&&!ui.document.transactionActive()&&ui.value()->heightScale==2.75f&&
        ui.document.history().size()==history+1,"Enter did not accept one complete numeric layer edit.");
    const auto accepted=*ui.document.scene().find(ui.first);
    require(ui.document.undo()&&sameEntity(*ui.document.scene().find(ui.first),baseline)&&ui.document.redo()&&
        sameEntity(*ui.document.scene().find(ui.first),accepted),"Typed numeric entry did not retain one exact undo/redo command.");ui.frame(2);
    ui.startNumber("Height Scale");ui.replaceNumber("4.125");
    require(ui.controls.active()&&ui.value()->heightScale==4.125f,"Numeric Escape fixture did not publish its typed value.");
    ui.key(ImGuiKey_Escape);
    require(!ui.controls.active()&&!ui.document.transactionActive()&&sameEntity(*ui.document.scene().find(ui.first),accepted)&&
        ui.document.history().size()==history+1,"Escape did not restore a numeric field's complete pre-edit scene state.");
    ui.frame(3);require(sameEntity(*ui.document.scene().find(ui.first),accepted),"A canceled numeric buffer was reapplied on the following frames.");
    ui.startNumber("Height Scale");ui.replaceNumber("6.500");
    require(ui.controls.active()&&ui.value()->heightScale==6.5f,"The numeric command fixture did not own a changed live value.");
    // Mirrors a scene-menu Undo: finish the owned edit before executing the
    // command, then draw the same widget in that frame with the old input state.
    ui.frame(1,true,true);
    require(!ui.controls.active()&&!ui.document.transactionActive()&&sameEntity(*ui.document.scene().find(ui.first),accepted),
        "A scene command failed to finish numeric input, or the old buffer overwrote its Undo.");
    ui.frame(3);require(sameEntity(*ui.document.scene().find(ui.first),accepted)&&!ui.document.transactionActive(),
        "A command-finished numeric buffer resumed editing after Undo.");
    ui.unchangedPaint();
}

void uiTypedValueRebinding() {
    for(const bool anotherMesh:{false,true}) {
        Ui ui;const auto history=ui.document.history().size();
        ui.startNumber("Height Scale");ui.replaceNumber("3.250");
        require(ui.controls.active()&&ui.value()&&ui.value()->heightScale==3.25f,"Numeric rebind fixture did not acquire a live value.");
        if(anotherMesh) {ui.selected.entity=ui.second;ui.document.select({ui.second},ui.second);}else ui.selected.layer=2;
        ui.frame();
        require(!ui.controls.active()&&!ui.document.transactionActive()&&ui.document.history().size()==history+1&&
            ui.value(1)&&ui.value(1)->heightScale==3.25f&&!ui.value(2)&&!ui.value(1,ui.second),
            "Rebinding a numeric field leaked its old text buffer into another layer or mesh.");
        ui.frame(3);
        require(!ui.value(2)&&!ui.value(1,ui.second)&&!ui.document.transactionActive(),"An old numeric input resumed after a layer/mesh rebind.");
        ui.unchangedPaint();
    }
}

void uiSharedMaterialSlotBindings() {
    Ui ui;ui.click("Height Blend");const auto slotZero=*ui.value();const auto history=ui.document.history().size();
    ui.selected.slot=1;ui.frame(2);
    require(!ui.value(1,ui.first,1)&&!ui.has("Height Influence")&&ui.document.history().size()==history,
        "Selecting another slot using the same material copied settings from the first slot.");
    ui.click("Height Blend");
    require(ui.value(1,ui.first,1)&&ui.value(1,ui.first,1)->heightBlend&&*ui.value()==slotZero&&
        ui.document.history().size()==history+1,"A checkbox edited the first matching material slot instead of the selected slot.");
    ui.startSlider("Blend Softness",.75f);ui.release();
    const auto selectedSlot=*ui.value(1,ui.first,1);
    require(selectedSlot.blendSoftness>.5f&&*ui.value()==slotZero,"The selected slot's blend slider changed another slot sharing its material.");
    require(ui.document.undo()&&ui.value(1,ui.first,1)->blendSoftness==ui.inherited.blendSoftness&&*ui.value()==slotZero&&
        ui.document.redo()&&*ui.value(1,ui.first,1)==selectedSlot,"Slot-specific blend edits did not retain independent undo/redo.");
    require(ui.document.save(),ui.document.status().c_str());SceneDocument loaded(ui.project.root);
    require(loaded.load(ui.document.assetId()),"Could not reopen multiple slots sharing one Blend material.");
    const auto loadedRecord=loaded.scene().find(ui.first);
    const auto& settings=loadedRecord->blendSettings;
    const auto first=findSceneBlendLayer(settings,0,ui.material,1),second=findSceneBlendLayer(settings,1,ui.material,1);
    require(first&&second&&*first==slotZero&&*second==selectedSlot,"Saving conflated separate slots using the same Blend material.");
    ui.unchangedPaint();
}
}

int main() {
    try {
        editHistoryIsolationAndNoOp();editForeignTransactionAndTransientGuards();
        uiSelectedLayerAndSceneOnlyCheckbox();uiSliderReleaseUndoAndCancel();uiSwitchAndForeignOwnership();
        uiTypedValuesAcceptCancelAndCommand();uiTypedValueRebinding();uiSharedMaterialSlotBindings();
        std::cout<<"Per-mesh vertex blend layer history, immutable paint, private ImGui controls and command ownership checks passed.\n";return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
