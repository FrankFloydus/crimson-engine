#include "PrimitiveMeshInspector.h"
#include "PropertyReset.h"
#include "scene/SceneDocument.h"
#include "scene/PrimitiveMesh.h"
#include <imgui_internal.h>
#include <algorithm>
#include <array>

namespace crimson {
void releasePrimitiveMeshInspector(PrimitiveMeshInspectorState& state) {
    if(state.editing && ImGui::GetActiveID()==state.activeItem)ImGui::ClearActiveID();
    state.editing=false;
}
void drawPrimitiveMeshInspector(SceneDocument& document,PrimitiveMeshInspectorState& state,const EntityRecord& record,
    const PrimitiveMeshInspectorTransactions& transactions) {
    if(!record.model || !isBuiltinModel(record.model->id))return;
    if(!document.transactionActive())releasePrimitiveMeshInspector(state);
    bool canceled=false;
    if(state.editing && (ImGui::IsKeyPressed(ImGuiKey_Escape,false)||ImGui::GetIO().AppFocusLost||
        state.entity!=record.id||state.documentGeneration!=document.documentGeneration())) {
        document.cancel();ImGui::ClearActiveID();state.editing=false;canceled=true;
    }
    if(state.editing && ImGui::GetActiveID()!=state.activeItem) {
        transactions.finish(state.active);state.editing=false;
    }
    ImGui::PushID("PrimitiveMeshProperties");
    if(ImGui::TreeNodeEx("Geometry",ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BeginDisabled(canceled);
        auto value=record.primitive.value_or(PrimitiveMeshSettings{});
        const PrimitiveMeshSettings defaults;
        const auto edit=[&](PrimitiveMeshControl control,const char* label,bool changed) {
            if((ImGui::IsItemActivated()||changed) && transactions.begin(control,label)) {
                state.active=control;state.activeItem=ImGui::GetItemID();state.editing=true;
                state.entity=record.id;state.documentGeneration=document.documentGeneration();
                if(changed)document.update([id=record.id,value](auto& records,auto&) {
                    const auto found=std::find_if(records.begin(),records.end(),[id](const auto& item){return item.id==id;});
                    if(found!=records.end())found->primitive=value;
                });
            }
            if(state.editing && state.active==control && (ImGui::IsItemDeactivated() || (changed&&!ImGui::IsItemActive()))) {
                transactions.finish(control);state.editing=false;
            }
        };
        if(record.model->id==BuiltinBoxModelId) {
            ImGui::TextUnformatted("Size (m)");
            if(ImGui::BeginTable("PrimitiveAxes",3,ImGuiTableFlags_SizingStretchSame)) {
                constexpr std::array<const char*,3> labels{"X","Y","Z"};
                const std::array<ImU32,3> colors{IM_COL32(170,0,0,255),IM_COL32(0,170,0,255),IM_COL32(0,0,170,255)};
                for(const auto* label:labels)ImGui::TableSetupColumn(label,ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableNextRow();
                for(unsigned axis=0;axis<3;++axis) {
                    ImGui::TableSetColumnIndex(static_cast<int>(axis));ImGui::AlignTextToFramePadding();ImGui::TextUnformatted(labels[axis]);
                    ImGui::SameLine(0,ImGui::GetStyle().ItemInnerSpacing.x);ImGui::PushID(static_cast<int>(axis));ImGui::SetNextItemWidth(-1);
                    const bool changed=resettableProperty(value.size[axis],defaults.size[axis],[&]{return ImGui::DragFloat("##size",&value.size[axis],.02f,.001f,100000.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(a,{a.x+std::max(1.f,ImGui::GetFontSize()*.15f),b.y},ImGui::GetColorU32(colors[axis]));
                    edit(static_cast<PrimitiveMeshControl>(static_cast<unsigned>(PrimitiveMeshControl::SizeX)+axis),"Resize primitive mesh",changed);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        } else {
            const bool radius=resettableProperty(value.radius,defaults.radius,[&]{return ImGui::DragFloat("Radius (m)",&value.radius,.01f,.001f,100000.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            edit(PrimitiveMeshControl::Radius,"Change primitive radius",radius);
            if(record.model->id==BuiltinCylinderModelId) {
                const bool height=resettableProperty(value.height,defaults.height,[&]{return ImGui::DragFloat("Height (m)",&value.height,.02f,.001f,100000.f,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                edit(PrimitiveMeshControl::Height,"Change cylinder height",height);
            }
            int sides=static_cast<int>(value.sides);
            const bool sidesChanged=resettableProperty(sides,static_cast<int>(defaults.sides),[&]{return ImGui::DragInt("Sides",&sides,1,3,256,"%d",ImGuiSliderFlags_AlwaysClamp);});
            if(sidesChanged)value.sides=static_cast<std::uint32_t>(sides);
            edit(PrimitiveMeshControl::Sides,"Change primitive sides",sidesChanged);
            if(record.model->id==BuiltinSphereModelId) {
                int rings=static_cast<int>(value.rings);
                const bool ringsChanged=resettableProperty(rings,static_cast<int>(defaults.rings),[&]{return ImGui::DragInt("Rings",&rings,1,2,256,"%d",ImGuiSliderFlags_AlwaysClamp);});
                if(ringsChanged)value.rings=static_cast<std::uint32_t>(rings);
                edit(PrimitiveMeshControl::Rings,"Change sphere rings",ringsChanged);
            }
        }
        ImGui::EndDisabled();ImGui::TreePop();
    } else if(state.editing) {
        transactions.finish(state.active);releasePrimitiveMeshInspector(state);
    }
    ImGui::PopID();
}
}
