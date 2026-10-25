#include "VertexBlendLayerControls.h"
#include "PropertyReset.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>

namespace crimson {
void VertexBlendLayerControls::finish(SceneDocument& document) {
    if(edit_.active()) {
        finishedControl_=activeControl_;finishedFrame_=ImGui::GetFrameCount();
        if(activeItem_&&ImGui::GetActiveID()==activeItem_)ImGui::ClearActiveID();
    }
    edit_.finish(document);activeControl_=0;mouseGesture_=false;
    activeItem_=0;
}
void VertexBlendLayerControls::cancel(SceneDocument& document) {
    if(edit_.active()) {
        finishedControl_=activeControl_;finishedFrame_=ImGui::GetFrameCount();
        if(activeItem_&&ImGui::GetActiveID()==activeItem_)ImGui::ClearActiveID();
    }
    edit_.cancel(document);activeControl_=0;mouseGesture_=false;tint_.cancel();
    activeItem_=0;
}
void VertexBlendLayerControls::prepareUi(SceneDocument& document,bool held,bool command,bool canceled) {
    if(canceled)canceledFrame_=ImGui::GetFrameCount();
    if(!active())return;
    if(canceled){cancel(document);return;}
    if(!edit_.valid(document)){cancel(document);return;}
    if(command||document.selection().primary!=edit_.target().entity||
        (mouseGesture_&&!held&&!ImGui::GetIO().WantTextInput))finish(document);
}
bool VertexBlendLayerControls::update(SceneDocument& document,const MaterialBlendLayer& candidate,const char* label,std::uint32_t control,bool active) {
    if(canceledFrame_==ImGui::GetFrameCount()||!target_)return false;
    if(finishedFrame_==ImGui::GetFrameCount()&&finishedControl_==control)return false;
    if(edit_.active()&&activeControl_!=control)finish(document);
    if(!edit_.active()) {
        if(!edit_.begin(document,*target_,label))return false;
        activeControl_=control;mouseGesture_=ImGui::IsMouseDown(ImGuiMouseButton_Left);activeItem_=ImGui::GetActiveID();
    }
    if(!edit_.update(document,candidate)){cancel(document);return false;}
    if(active)activeItem_=ImGui::GetActiveID();
    if(!active)finish(document);
    return true;
}
void VertexBlendLayerControls::remember(const char* label) {
    const auto a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
    controls_.push_back({label,{a.x,a.y},{b.x,b.y}});
}
void VertexBlendLayerControls::draw(SceneDocument& document,const VertexBlendLayerTarget& target,const MaterialBlendLayer& inherited) {
    controls_.clear();
    if(target_!=target) {finish(document);target_=target;tint_.cancel();}
    const auto record=document.scene().find(target.entity);if(!record||!record->model)return;
    const auto stored=findSceneBlendLayer(record->blendSettings,target.slot,target.material,target.layer);
    auto value=stored?*stored:inherited;
    const bool editable=!document.transient()&&(!document.transactionActive()||edit_.valid(document));
    ImGui::PushID(static_cast<int>(target.slot));ImGui::PushID(static_cast<int>(target.layer));
    ImGui::SeparatorText("Layer Settings");
    ImGui::BeginDisabled(!editable);
    const auto control=[&](const char* name,const char* label,const auto& draw) {
        const bool changed=draw();const auto id=ImGui::GetItemID();
        const bool active=ImGui::IsItemActive(),deactivated=ImGui::IsItemDeactivated();remember(name);
        if(changed&&editable)update(document,value,label,id,active);
        if(deactivated&&edit_.active()&&activeControl_==id)finish(document);
    };
    if(target.layer!=0) {
        control("Height Blend","Toggle layer height blending",[&]{return resettableProperty(value.heightBlend,inherited.heightBlend,[&]{return ImGui::Checkbox("Height Blend",&value.heightBlend);});});
        ImGui::SetItemTooltip("Shape painted coverage using this layer's height and the underlying surface height.");
        if(value.heightBlend) {
            control("Height Influence","Change layer height influence",[&]{return resettableProperty(value.heightInfluence,inherited.heightInfluence,[&]{return ImGui::SliderFloat("Height Influence",&value.heightInfluence,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
            control("Blend Softness","Change layer blend softness",[&]{return resettableProperty(value.blendSoftness,inherited.blendSoftness,[&]{return ImGui::SliderFloat("Blend Softness",&value.blendSoftness,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        }
    }
    control("Height Scale","Change layer height scale",[&]{return resettableProperty(value.heightScale,inherited.heightScale,[&]{return ImGui::DragFloat("Height Scale",&value.heightScale,.01f,-16,16,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
    control("Height Midpoint","Change layer height midpoint",[&]{return resettableProperty(value.heightMidpoint,inherited.heightMidpoint,[&]{return ImGui::SliderFloat("Height Midpoint",&value.heightMidpoint,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
    ImGui::SeparatorText("Appearance");
    const auto color=drawHdrColorControl("Tint",value.tint,nullptr,tint_,false,1000000.f,"Alpha",inherited.tint);remember("Tint");
    if(color.changed&&editable)update(document,value,"Change layer tint",ImGui::GetID("Layer tint gesture"),color.active);
    if(color.deactivated&&edit_.active()&&activeControl_==ImGui::GetID("Layer tint gesture"))finish(document);
    control("Roughness Multiplier","Change layer roughness scale",[&]{return resettableProperty(value.roughnessScale,inherited.roughnessScale,[&]{return ImGui::DragFloat("Roughness Multiplier",&value.roughnessScale,.01f,0,4,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
    if(target.layer!=0) {
        control("Normal Influence","Change layer normal influence",[&]{return resettableProperty(value.normalInfluence,inherited.normalInfluence,[&]{return ImGui::SliderFloat("Normal Influence",&value.normalInfluence,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        ImGui::SetItemTooltip("Zero retains the underlying normal. One fully applies this layer's normal at full coverage.");
        control("Roughness Influence","Change layer roughness influence",[&]{return resettableProperty(value.roughnessInfluence,inherited.roughnessInfluence,[&]{return ImGui::SliderFloat("Roughness Influence",&value.roughnessInfluence,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
        if(ImGui::TreeNodeEx("Advanced##Layer",ImGuiTreeNodeFlags_SpanAvailWidth)) {
            control("Color Influence","Change layer color influence",[&]{return resettableProperty(value.colorInfluence,inherited.colorInfluence,[&]{return ImGui::SliderFloat("Color Influence",&value.colorInfluence,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
            control("Metallic Influence","Change layer metallic influence",[&]{return resettableProperty(value.metallicInfluence,inherited.metallicInfluence,[&]{return ImGui::SliderFloat("Metallic Influence",&value.metallicInfluence,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
            control("AO Influence","Change layer occlusion influence",[&]{return resettableProperty(value.occlusionInfluence,inherited.occlusionInfluence,[&]{return ImGui::SliderFloat("AO Influence",&value.occlusionInfluence,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
            ImGui::TreePop();
        }
    }
    if(ImGui::TreeNodeEx("Mapping##Layer",ImGuiTreeNodeFlags_SpanAvailWidth)) {
        control("Override Mapping","Toggle layer mapping override",[&]{return resettableProperty(value.mappingOverride,inherited.mappingOverride,[&]{return ImGui::Checkbox("Override Mapping",&value.mappingOverride);});});
        ImGui::SetItemTooltip("Replace source material texture mapping for this mesh's selected layer.");
        if(value.mappingOverride) {
            control("Mapping","Change layer mapping",[&]{int mode=static_cast<int>(value.mapping.mode);const bool changed=resettableProperty(mode,static_cast<int>(inherited.mapping.mode),[&]{return ImGui::Combo("Mapping",&mode,"UV0\0UV1\0Local Triplanar\0World Triplanar\0");});if(changed)value.mapping.mode=static_cast<MaterialUvMode>(mode);return changed;});
            const bool triplanar=value.mapping.mode==MaterialUvMode::LocalTriplanar||value.mapping.mode==MaterialUvMode::WorldTriplanar;
            control("Link scale","Link layer scale",[&]{return resettableProperty(value.mapping.scaleLinked,inherited.mapping.scaleLinked,[&]{return ImGui::Checkbox("Link scale",&value.mapping.scaleLinked);});});
            control("Scale","Change layer scale",[&]{auto& mapping=value.mapping;const auto before=mapping.scale;
                const bool changed=resettableProperty(mapping.scale,inherited.mapping.scale,[&]{return triplanar?ImGui::DragFloat3("Scale",mapping.scale.data(),.01f,0,0,"%.3f"):ImGui::DragFloat2("Scale XY",mapping.scale.data(),.01f,0,0,"%.3f");});
                if(ImGui::IsItemActivated())scaleBaseline_=before;
                if(changed&&mapping.scaleLinked&&!propertyResetClicked())for(unsigned axis=0;axis<(triplanar?3u:2u);++axis)if(mapping.scale[axis]!=before[axis]) {
                    const auto baseline=ImGui::IsItemActive()?scaleBaseline_:before;const auto next=mapping.scale[axis];
                    if(std::abs(baseline[axis])>1e-8f)mapping.scale=baseline*(static_cast<double>(next)/baseline[axis]);
                    else mapping.scale=before+Vec3{next-before[axis],next-before[axis],next-before[axis]};break;
                }return changed;
            });
            control("Rotation (deg)","Change layer rotation",[&]{auto& mapping=value.mapping;return resettableProperty(mapping.rotation,inherited.mapping.rotation,[&]{return triplanar?ImGui::DragFloat3("Rotation (deg)",mapping.rotation.data(),.5f,0,0,"%.2f"):ImGui::DragFloat("Rotation (deg)",&mapping.rotation[2],.5f,0,0,"%.2f");});});
            control("Offset","Change layer offset",[&]{auto& mapping=value.mapping;return resettableProperty(mapping.offset,inherited.mapping.offset,[&]{return triplanar?ImGui::DragFloat3("Offset",mapping.offset.data(),.01f,0,0,"%.3f"):ImGui::DragFloat2("Offset XY",mapping.offset.data(),.01f,0,0,"%.3f");});});
        }
        ImGui::TreePop();
    }
    ImGui::BeginDisabled(!stored);
    if(ImGui::Button("Reset Layer Settings")&&editable&&canceledFrame_!=ImGui::GetFrameCount()) {
        finish(document);
        if(edit_.begin(document,target,"Reset layer settings")) {
            if(edit_.update(document,std::nullopt))finish(document);else cancel(document);
        }
        tint_.cancel();
    }
    ImGui::SetItemTooltip("Restore this mesh's layer settings to the blend material's defaults. Paint weights are preserved.");
    ImGui::EndDisabled();ImGui::EndDisabled();ImGui::PopID();ImGui::PopID();
}
}
