#include "MaterialEditor.h"
#include "DocumentEditorMenu.h"
#include "MaterialAtomicControls.h"
#include "assets/AssetLibrary.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>

namespace crimson {
namespace {
// Pickers and delivery use the same strict image compatibility checks.
bool textureField(AssetLibrary& assets,const char* title,std::string& id,std::string& message,bool normal=false) {
    ImGui::PushID(title);
    bool changed=false;
    const auto assign=[&](const std::string& candidate) {
        const auto* texture=assets.find(candidate);
        if(!texture || texture->hdr) { message="Choose a regular image texture. HDR images are environment sources.";return; }
        if(candidate!=id){id=candidate;changed=true;}message.clear();
    };
    ImGui::TextUnformatted(title);
    ImGui::BeginGroup();
    const float height=ImGui::GetFrameHeight(),gap=ImGui::GetStyle().ItemInnerSpacing.x;
    const float width=std::max(height,ImGui::GetContentRegionAvail().x-height*2-gap*2);
    bool picker=ImGui::InvisibleButton("Texture asset",{width,height});
    const auto minimum=ImGui::GetItemRectMin(),maximum=ImGui::GetItemRectMax();
    auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum,maximum,ImGui::GetColorU32(ImGui::IsItemHovered()?ImGuiCol_FrameBgHovered:ImGuiCol_FrameBg),ImGui::GetStyle().FrameRounding);
    const auto resolved=id.empty()?std::string{}:assets.references().resolveTexture(id,normal);
    const auto* texture=resolved.empty()?nullptr:assets.find(resolved);
    float textX=minimum.x+3;
    if(texture && bgfx::isValid(texture->thumbnail)) {
        draw->AddImage(ImGuiBgfx::textureId(texture->thumbnail),{minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3});textX+=height;
    }
    const auto name=id.empty()?std::string("None"):texture?assets.displayName(texture->id):"Missing: "+assets.displayName(id);
    draw->PushClipRect({textX,minimum.y},{maximum.x-3,maximum.y},true);
    draw->AddText({textX,minimum.y+(height-ImGui::GetFontSize())*.5f},ImGui::GetColorU32(ImGuiCol_Text),name.c_str());draw->PopClipRect();
    ImGui::SameLine(0,gap);picker|=ImGui::Button("...##texture",{height,height});
    ImGui::SameLine(0,gap);ImGui::BeginDisabled(id.empty());
    if(ImGui::Button("x##texture",{height,height})){id.clear();changed=true;}ImGui::EndDisabled();
    ImGui::EndGroup();
    if(ImGui::BeginDragDropTarget()) {
        if(const auto* payload=ImGui::AcceptDragDropPayload("CC_TEXTURE_ASSET")) {
            const auto* bytes=static_cast<const char*>(payload->Data);
            if(payload->DataSize>1 && bytes && bytes[payload->DataSize-1]=='\0' &&
               std::find(bytes,bytes+payload->DataSize-1,'\0')==bytes+payload->DataSize-1)
                assign(std::string(bytes,static_cast<size_t>(payload->DataSize-1)));
            else message="The texture payload is invalid.";
        }
        ImGui::EndDragDropTarget();
    }
    static ImGuiTextFilter filter;
    if(picker){filter.Clear();ImGui::OpenPopup("Select texture");}
    if(ImGui::BeginPopup("Select texture")) {
        filter.Draw("Search");
        if(ImGui::Selectable("None",id.empty())){if(!id.empty()){id.clear();changed=true;}ImGui::CloseCurrentPopup();}
        ImGui::BeginChild("Texture choices",{360,ImGui::GetFontSize()*12});
        for(const auto& candidate:assets.assets())if(!candidate.hdr && filter.PassFilter(assets.displayName(candidate.id).c_str())) {
            ImGui::PushID(candidate.id.c_str());
            if(ImGui::Selectable(assets.displayName(candidate.id).c_str(),candidate.id==id)){assign(candidate.id);ImGui::CloseCurrentPopup();}
            ImGui::PopID();
        }
        ImGui::EndChild();ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}
}
bool MaterialEditor::focused() const {
    const auto* window=ImGui::GetCurrentContext()->NavWindow;
    return isOpen() && window && window->RootWindow->ID==ImHashStr("###SpriteMaterial");
}
bool MaterialEditor::canUndo() const { return document_.canUndo(); }
bool MaterialEditor::canRedo() const { return document_.canRedo(); }
void MaterialEditor::undo() { document_.undo(); }
void MaterialEditor::redo() { document_.redo(); }
void MaterialEditor::finishInteraction(AssetLibrary* assets) {
    const bool editing=document_.transactionActive();
    if(editing && !document_.commit())return;
    if(editing) {
        const auto* active=ImGui::GetCurrentContext()->ActiveIdWindow;
        for(auto* window=active;window;window=window->ParentWindow)
            if(window->RootWindow->ID==ImHashStr("###SpriteMaterial")){ImGui::ClearActiveID();break;}
        // InputText can deliver its last buffer after releasing focus. A menu
        // Undo may already have restored another value before properties draw.
        suppressFinalizedUpdate_=true;
    }
    propertyEditing_=false;graph_.finishInteraction();colorEditor_.cancel();emissionEditor_.cancel();alphaEditor_.cancel();
    if(assets)publish(*assets);
}
void MaterialEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None) return;
    finishInteraction(assets);
    if(document_.transactionActive()) { message_=document_.status();return; }
    pending_=pending;pendingId_=std::move(id);
    if(document_.isOpen() && document_.dirty()) dirtyPopup_=true;
    else if(assets) perform(*assets);
    else if(pending==Pending::Exit) { pending_=Pending::None;exitReady_=true; }
}
void MaterialEditor::requestExit() { if(!exitReady_) request(Pending::Exit,{},nullptr); }
void MaterialEditor::requestOpen(std::string id,AssetLibrary& assets) {
    if(document_.isOpen() && document_.assetId()==id) { focusNext_=true;return; }
    request(Pending::Open,std::move(id),&assets);
}
void MaterialEditor::requestCreate(std::string folder,AssetLibrary& assets) {
    pendingFolder_=std::move(folder);request(Pending::Create,{},&assets);
}
void MaterialEditor::perform(AssetLibrary& assets) {
    const auto pending=pending_;
    pending_=Pending::None;
    continueAfterSave_=false;
    if(pending==Pending::Exit){exitReady_=true;return;}
    if(pending==Pending::Close) {
        graph_.clearPreview();texturePickerNode_=0;
        document_.close();assets.protectMaterialDocument({});assets.setMaterialDocumentReferences({},{});assets.refreshMaterials();
        return;
    }
    message_.clear();
    bool loaded=false;
    if(pending==Pending::Open) loaded=document_.load(pendingId_);
    if(pending==Pending::Create) {
        loaded=document_.create();
        if(loaded) {
            assets.noteAssetWrite(document_.assetId());assets.refreshMaterials();
            try {
                if(!pendingFolder_.empty()) {
                    const std::array<std::string,1> ids{document_.assetId()};
                    assets.browserCatalog().move(ids,pendingFolder_);assets.noteAssetWrite("assets/browser.json");
                }
                assets.select(document_.assetId());
            } catch(const std::exception& error) { message_=error.what(); }
        }
    }
    if(loaded) {
        graph_.clearPreview();texturePickerNode_=0;
        assets.protectMaterialDocument(document_.assetId());publishedRevision_=0;
        reconcileReferences(assets,true);
        publish(assets);assets.refreshMaterials();focusNext_=true;externalChanged_=false;
    } else message_=document_.status();
    pendingId_.clear();pendingFolder_.clear();
}
bool MaterialEditor::reconcileReferences(AssetLibrary& assets,bool force) {
    if(!document_.isOpen())return true;
    const auto& policy=assets.references();
    // Clipboard data can outlive a texture deletion. Recheck committed edits,
    // even if the project deletion policy itself has not changed since copying.
    if(!force && referenceRevision_==policy.revision() && referenceDocumentId_==document_.assetId() &&
        referenceMaterialRevision_==document_.revision())return true;
    if(!document_.reconcileTextureReferences([&](const std::string& id,MaterialTextureRole role) {
        if(!policy.deleted(id))return id;
        return policy.resolveTexture(id,role==MaterialTextureRole::Normal);
    })) {message_=document_.status();return false;}
    referenceRevision_=policy.revision();referenceDocumentId_=document_.assetId();
    referenceMaterialRevision_=document_.revision();return true;
}
void MaterialEditor::publish(AssetLibrary& assets) {
    if(!document_.isOpen()) return;
    if(!reconcileReferences(assets))return;
    auto textures=materialGraphTextureReferences(document_.definition().graph);
    assets.setMaterialDocumentReferences(std::move(textures),document_.historicalTextureReferences());
    if(publishedRevision_!=document_.revision() || publishedId_!=document_.assetId()) {
        if(!isEngineAssetId(document_.assetId()))assets.publishMaterial(document_.committed());
        publishedRevision_=document_.revision();publishedId_=document_.assetId();
    }
}
void MaterialEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!document_.isOpen()) return;
    if(!reconcileReferences(assets))return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
    publish(assets);
    if(saveAs) {
        std::snprintf(saveName_.data(),saveName_.size(),"%s",document_.definition().name.c_str());savePopup_=true;return;
    }
    if(document_.externalFileChanged()) { externalPopup_=true;message_="The material changed on disk. Choose Overwrite, Reload, or Save As.";return; }
    if(document_.save()) { assets.noteAssetWrite(document_.assetId());assets.refreshMaterials();message_.clear(); }
    else message_=document_.status();
}
void MaterialEditor::filesChanged(std::span<const std::filesystem::path> paths) {
    if(!document_.isOpen()) return;
    if(isEngineAssetId(document_.assetId()))return;
    const auto file=resolveAssetPath(document_.projectRoot(),document_.assetId()).lexically_normal();
    for(const auto& path:paths) if(resourcePathKey(path)==resourcePathKey(file))
        externalChanged_=true;
}
void MaterialEditor::modals(AssetLibrary& assets) {
    if(dirtyPopup_) { ImGui::OpenPopup("Unsaved material");dirtyPopup_=false; }
    if(ImGui::BeginPopupModal("Unsaved material",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("Save changes to %s?",document_.definition().name.c_str());
        if(ImGui::Button("Save")) {
            continueAfterSave_=true;
            save(assets);
            if(!document_.dirty()) { perform(assets);continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
            else if(externalPopup_ || savePopup_)ImGui::CloseCurrentPopup();
        }
        ImGui::SameLine();
        if(ImGui::Button("Discard")) {
            if(document_.discard()) { publish(assets);perform(assets);ImGui::CloseCurrentPopup(); }
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) { pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(savePopup_) { ImGui::OpenPopup("Save material as");savePopup_=false; }
    if(ImGui::BeginPopupModal("Save material as",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        const bool enter=ImGui::InputText("Name",saveName_.data(),saveName_.size(),ImGuiInputTextFlags_EnterReturnsTrue);
        if((ImGui::Button("Save") || enter) && saveName_[0]) {
            const auto previous=document_.assetId();
            if(document_.save(saveName_.data(),true)) {
                assets.protectMaterialDocument(document_.assetId());
                try { if(!isEngineAssetId(previous))assets.publishMaterial(std::make_shared<MaterialAsset>(loadMaterialAsset(assets.projectRoot(),previous))); }
                catch(const std::exception& error) { message_=error.what(); }
                publish(assets);assets.noteAssetWrite(document_.assetId());assets.refreshMaterials();assets.select(document_.assetId());
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;
                ImGui::CloseCurrentPopup();
            } else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Cancel")) { if(continueAfterSave_)pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
    if(externalPopup_) { ImGui::OpenPopup("Material changed on disk");externalPopup_=false; }
    if(ImGui::BeginPopupModal("Material changed on disk",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("The material changed on disk.");
        if(ImGui::Button("Overwrite")) {
            if(document_.save()) {
                assets.noteAssetWrite(document_.assetId());assets.refreshMaterials();message_.clear();
                if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }
            else message_=document_.status();
        }
        ImGui::SameLine();
        if(ImGui::Button("Reload and discard edits")) {
            const auto id=document_.assetId();
            if(document_.load(id)) {
                reconcileReferences(assets,true);
                publish(assets);message_.clear();if(continueAfterSave_)perform(assets);
                continueAfterSave_=false;ImGui::CloseCurrentPopup();
            }
            else message_=document_.status();
        }
        ImGui::SameLine();if(ImGui::Button("Keep editing")) { pending_=Pending::None;continueAfterSave_=false;ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();if(ImGui::Button("Save As")) { save(assets,true);ImGui::CloseCurrentPopup(); }
        if(!message_.empty()) ImGui::TextWrapped("%s",message_.c_str());
        ImGui::EndPopup();
    }
}
void MaterialEditor::properties(AssetLibrary& assets) {
    if(document_.definition().graph.empty())return;
    const bool editable=document_.editable();
    if(!editable)ImGui::TextDisabled("Read-only");
    auto selected=graph_.selectedNode();
    const auto* selectedNode=findMaterialNode(document_.definition().graph,selected);
    if(!selectedNode || selectedNode->kind==MaterialNodeKind::Output)selected=materialOutputNode(document_.definition().graph).id;
    if(propertyNode_!=selected) {propertyNode_=selected;colorEditor_.cancel();emissionEditor_.cancel();alphaEditor_.cancel();}
    const bool escape=propertyEditing_ && ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    const bool canceled=suppressFinalizedUpdate_ || escape;suppressFinalizedUpdate_=false;
    if(escape){document_.cancel();propertyEditing_=false;colorEditor_.cancel();emissionEditor_.cancel();alphaEditor_.cancel();}
    const auto finish=[&] {
        if(!propertyEditing_)return;
        if(!document_.commit()){message_=document_.status();document_.cancel();}
        propertyEditing_=false;
    };
    const auto control=[&](const char* label,const auto& draw) {
        auto candidate=document_.definition();auto* node=findMaterialNode(candidate.graph,selected);if(!node)return;
        ImGui::BeginDisabled(!editable);
        const bool changed=draw(*node);const bool activated=ImGui::IsItemActivated(),deactivated=ImGui::IsItemDeactivated();
        ImGui::EndDisabled();if(!editable)return;
        if(!canceled && (activated || changed) && !document_.transactionActive())propertyEditing_=document_.begin(label);
        if(!canceled && changed && propertyEditing_)document_.update([&](auto& asset){asset=std::move(candidate);});
        if(deactivated && propertyEditing_)finish();
    };
    const auto hdr=[&](const char* label,const char* transaction,bool outputColor,bool emission=false) {
        auto candidate=document_.definition();auto* node=findMaterialNode(candidate.graph,selected);if(!node)return;
        Vec3 value=outputColor?(emission?node->output.emission:node->output.baseColor):Vec3{node->color[0],node->color[1],node->color[2]};
        float alpha=node->color[3];ImGui::BeginDisabled(!editable);
        auto edit=drawHdrColorControl(label,value,outputColor?nullptr:&alpha,emission?emissionEditor_:colorEditor_,false,1000000.f,"Alpha");
        ImGui::EndDisabled();if(!editable)return;
        if(!canceled && (edit.activated || edit.changed) && !document_.transactionActive())propertyEditing_=document_.begin(transaction);
        if(!canceled && edit.changed && propertyEditing_) {
            if(outputColor){if(emission)node->output.emission=value;else node->output.baseColor=value;}
            else node->color={value[0],value[1],value[2],alpha};
            document_.update([&](auto& asset){asset=std::move(candidate);});
        }
        if(edit.deactivated && propertyEditing_)finish();
    };
    const auto node=*findMaterialNode(document_.definition().graph,selected);
    if(node.kind==MaterialNodeKind::Output) {
        if(ImGui::TreeNodeEx("Material",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth)) {
            control("Change shader model",[](auto& n) {
                int model=n.output.shading==MaterialShading::StandardPbr?0:1;
                const bool changed=ImGui::Combo("Shader Model",&model,"Lit (PBR)\0Unlit\0");
                if(changed)n.output.shading=model==0?MaterialShading::StandardPbr:MaterialShading::Unlit;
                return changed;
            });
            control("Change material blending",[](auto& n) {
                int blend=static_cast<int>(n.output.blend);
                const bool changed=ImGui::Combo("Blend Mode",&blend,"Opaque\0Masked\0Translucent\0Additive\0");
                if(changed)n.output.blend=static_cast<MaterialBlend>(blend);return changed;
            });
            if(materialOutputNode(document_.definition().graph).output.blend==MaterialBlend::Mask)
                control("Change alpha cutoff",[](auto& n){return ImGui::SliderFloat("Alpha cutoff",&n.output.alphaCutoff,0,1,"%.3f");});
            control("Change material culling",[](auto& n){return ImGui::Checkbox("Double sided",&n.output.doubleSided);});
            control("Toggle material fog",[](auto& n){return ImGui::Checkbox("Receive Fog",&n.output.receiveFog);});
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Apply the scene's distance and height fog to this material.");
            if(materialOutputNode(document_.definition().graph).output.shading==MaterialShading::StandardPbr)
                control("Change occlusion strength",[](auto& n){return ImGui::SliderFloat("AO strength",&n.output.occlusionStrength,0,1,"%.3f");});
            control("Toggle material tessellation",[](auto& n){return ImGui::Checkbox("Tessellation",&n.output.tessellation.enabled);});
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Subdivide and displace mesh geometry from Height. Requires Direct3D 12.");
            if(materialOutputNode(document_.definition().graph).output.tessellation.enabled) {
                control("Change displacement amount",[](auto& n){return ImGui::DragFloat("Displacement Amount",&n.output.tessellation.amount,.001f,0,0,"%.3f");});
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Mesh-local displacement before object scaling. Negative values reverse the relief.");
                control("Change displacement midpoint",[](auto& n){return ImGui::SliderFloat("Midpoint",&n.output.tessellation.midpoint,0,1,"%.3f");});
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Height at which displacement is zero. Height is bounded to 0-1.");
                control("Change tessellation factor",[](auto& n) {
                    int factor=static_cast<int>(n.output.tessellation.maxFactor);
                    const bool changed=ImGui::SliderInt("Tessellation Factor",&factor,1,64);
                    if(changed)n.output.tessellation.maxFactor=static_cast<unsigned>(factor);return changed;
                });
                if(ImGui::IsItemHovered())ImGui::SetTooltip("Uniform subdivision per triangle edge. Higher values increase GPU work.");
                if(ImGui::TreeNodeEx("Advanced##Tessellation",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    control("Change displacement texture LOD",[](auto& n){return ImGui::SliderFloat("Height Texture LOD",&n.output.tessellation.heightLod,0,16,"%.2f");});
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Explicit texture mip level for displacement. Higher levels smooth height detail.");
                    ImGui::TreePop();
                }
            }
            ImGui::TreePop();
        }
        if(ImGui::TreeNodeEx("Default inputs",ImGuiTreeNodeFlags_SpanAvailWidth)) {
            const auto disconnected=[&](MaterialPin pin){return !materialInputLink(document_.definition().graph,selected,pin);};
            if(disconnected(MaterialPin::BaseColor))hdr(node.output.shading==MaterialShading::Unlit?"Emissive Color":"Base Color","Change default base color",true);
            if(materialOutputNode(document_.definition().graph).output.shading==MaterialShading::StandardPbr) {
                if(disconnected(MaterialPin::Roughness))control("Change default roughness",[](auto& n){return ImGui::SliderFloat("Roughness",&n.output.roughness,0,1,"%.3f");});
                if(disconnected(MaterialPin::Metallic))control("Change default metallic",[](auto& n){return ImGui::SliderFloat("Metallic",&n.output.metallic,0,1,"%.3f");});
                if(disconnected(MaterialPin::Specular))control("Change default specular",[](auto& n){return ImGui::SliderFloat("Specular",&n.output.specular,0,1,"%.3f");});
                if(disconnected(MaterialPin::Occlusion))control("Change default occlusion",[](auto& n){return ImGui::SliderFloat("Ambient Occlusion",&n.output.occlusion,0,1,"%.3f");});
                if(disconnected(MaterialPin::Emission))hdr("Emissive Color","Change default emission",true,true);
            }
            const auto& output=materialOutputNode(document_.definition().graph).output;
            if(materialPinActive(output.shading,output.blend,MaterialPin::Opacity) && disconnected(MaterialPin::Opacity))
                control("Change default opacity",[](auto& n){return ImGui::SliderFloat("Opacity",&n.output.opacity,0,1,"%.3f");});
            if(materialPinActive(output.shading,output.blend,MaterialPin::OpacityMask) && disconnected(MaterialPin::OpacityMask))
                control("Change default opacity mask",[](auto& n){return ImGui::SliderFloat("Opacity Mask",&n.output.opacityMask,0,1,"%.3f");});
            if(output.tessellation.enabled && disconnected(MaterialPin::Height))
                control("Change default height",[](auto& n){return ImGui::SliderFloat("Height",&n.output.height,0,1,"%.3f");});
            ImGui::TreePop();
        }
    } else {
        control("Rename material node",[](auto& n) {
            std::array<char,256> text{};std::snprintf(text.data(),text.size(),"%s",n.name.c_str());
            const bool changed=ImGui::InputText("Name",text.data(),text.size());if(changed)n.name=text.data();return changed;
        });
        if(node.kind==MaterialNodeKind::UV) {
            ImGui::SeparatorText("UV mapping");
            control("Change UV mapping",[&](auto& n) {
                int mode=static_cast<int>(n.uv.mode);
                const bool changed=ImGui::Combo("Mapping",&mode,"UV0\0UV1\0Local Triplanar\0World Triplanar\0");
                if(changed)n.uv.mode=static_cast<MaterialUvMode>(mode);return changed;
            });
            const bool scaleLinked=materialInputLink(document_.definition().graph,selected,MaterialPin::ScaleXY)!=nullptr;
            const bool offsetLinked=materialInputLink(document_.definition().graph,selected,MaterialPin::OffsetXY)!=nullptr;
            const bool triplanar=node.uv.mode==MaterialUvMode::LocalTriplanar || node.uv.mode==MaterialUvMode::WorldTriplanar;
            if(!scaleLinked)control("Link UV scale",[](auto& n){return ImGui::Checkbox("Link scale",&n.uv.scaleLinked);});
            if(!scaleLinked)
            control("Change UV scale",[&](auto& n) {
                const auto before=n.uv.scale;const bool changed=triplanar?ImGui::DragFloat3("Scale",n.uv.scale.data(),.01f,0,0,"%.3f"):ImGui::DragFloat2("Scale XY",n.uv.scale.data(),.01f,0,0,"%.3f");
                if(ImGui::IsItemActivated())uvScaleBaseline_=before;
                if(changed && n.uv.scaleLinked)for(unsigned axis=0;axis<3;++axis)if(n.uv.scale[axis]!=before[axis]) {
                    const auto baseline=ImGui::IsItemActive()?uvScaleBaseline_:before;const float value=n.uv.scale[axis];
                    if(std::abs(baseline[axis])>1e-8f)n.uv.scale=baseline*(static_cast<double>(value)/baseline[axis]);
                    else n.uv.scale=before+Vec3{value-before[axis],value-before[axis],value-before[axis]};break;
                }
                return changed;
            });
            else if(triplanar)control("Change UV scale Z",[](auto& n){return ImGui::DragFloat("Scale Z",&n.uv.scale[2],.01f,0,0,"%.3f");});
            control("Change UV rotation",[&](auto& n){return triplanar?ImGui::DragFloat3("Rotation (deg)",n.uv.rotation.data(),.5f,0,0,"%.2f"):ImGui::DragFloat("Rotation (deg)",&n.uv.rotation[2],.5f,0,0,"%.2f");});
            if(!offsetLinked)control("Change UV offset",[&](auto& n){return triplanar?ImGui::DragFloat3("Offset",n.uv.offset.data(),.01f,0,0,"%.3f"):ImGui::DragFloat2("Offset XY",n.uv.offset.data(),.01f,0,0,"%.3f");});
            else if(triplanar)control("Change UV offset Z",[](auto& n){return ImGui::DragFloat("Offset Z",&n.uv.offset[2],.01f,0,0,"%.3f");});
        }
        if(node.kind==MaterialNodeKind::Scalar) {
            ImGui::SeparatorText("Scalar (1D)");
            control("Change scalar",[](auto& n){return ImGui::SliderFloat("Value",&n.scalar,n.scalarRange[0],n.scalarRange[1],"%.3f");});
            control("Change scalar bounds",[](auto& n){return ImGui::InputFloat2("Slider range",n.scalarRange.data(),"%.3f");});
        }
        if(node.kind==MaterialNodeKind::Vector2) {
            ImGui::SeparatorText("Vector2");
            control("Change vector",[](auto& n){return ImGui::DragFloat2("X / Y",n.vector2.data(),.01f,0,0,"%.3f");});
        }
        if(node.kind==MaterialNodeKind::Time) {
            ImGui::SeparatorText("Time");
            control("Change time clock",[](auto& n){return ImGui::Checkbox("Ignore pause",&n.time.ignorePause);});
            control("Toggle time period",[](auto& n){return ImGui::Checkbox("Use period",&n.time.periodEnabled);});
            if(node.time.periodEnabled)control("Change time period",[](auto& n){return ImGui::DragFloat("Period (seconds)",&n.time.period,.01f,0,0,"%.3f");});
        }
        if(node.kind==MaterialNodeKind::Panner) {
            ImGui::SeparatorText("Panner");
            if(!materialInputLink(document_.definition().graph,selected,MaterialPin::UV))
                control("Change panner coordinate",[](auto& n){int coordinate=static_cast<int>(n.panner.coordinate);const bool changed=ImGui::Combo("Coordinate",&coordinate,"UV0\0UV1\0");if(changed)n.panner.coordinate=static_cast<MaterialUvMode>(coordinate);return changed;});
            if(!materialInputLink(document_.definition().graph,selected,MaterialPin::Speed))
                control("Change panner speed",[](auto& n){return ImGui::DragFloat2("Speed X / Y",n.panner.speed.data(),.01f,0,0,"%.3f");});
            control("Change panner fractional part",[](auto& n){return ImGui::Checkbox("Fractional part",&n.panner.fractionalPart);});
        }
        if(node.kind==MaterialNodeKind::Grunge) {
            ImGui::SeparatorText("Grunge");
            control("Change grunge seed",[](auto& n){return ImGui::SliderInt("Seed",&n.grunge.seed,0,65535,"%d",ImGuiSliderFlags_AlwaysClamp);});
            control("Change grunge tiling",[](auto& n){return ImGui::SliderInt("Tiling",&n.grunge.tiling,1,64,"%d",ImGuiSliderFlags_AlwaysClamp);});
            control("Change grunge coverage",[](auto& n){return ImGui::SliderFloat("Coverage",&n.grunge.coverage,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            control("Change grunge disorder",[](auto& n){return ImGui::SliderFloat("Disorder",&n.grunge.disorder,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            control("Change grunge detail",[](auto& n){return ImGui::SliderFloat("Detail",&n.grunge.detail,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            if(ImGui::TreeNodeEx("Layers",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                control("Change grunge base noisiness",[](auto& n){return ImGui::SliderFloat("Base Noisiness",&n.grunge.baseNoisiness,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change grunge dirt specks opacity",[](auto& n){return ImGui::SliderFloat("Dirt Specks Opacity",&n.grunge.dirtSpecksOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change grunge stains opacity",[](auto& n){return ImGui::SliderFloat("Stains Opacity",&n.grunge.stainsOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change grunge scratches opacity",[](auto& n){return ImGui::SliderFloat("Scratches Opacity",&n.grunge.scratchesOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change grunge leaks opacity",[](auto& n){return ImGui::SliderFloat("Leaks Opacity",&n.grunge.leaksOpacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change grunge sharpen",[](auto& n){return ImGui::SliderFloat("Sharpen",&n.grunge.sharpen,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change grunge large variation intensity",[](auto& n){return ImGui::SliderFloat("Large Variation Intensity",&n.grunge.largeVariationIntensity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Toggle grunge dirt inversion",[](auto& n){return ImGui::Checkbox("Invert Dirt",&n.grunge.invertDirt);});
                ImGui::TreePop();
            }
        }
        if(materialNodeImageOperation(node.kind) || node.kind==MaterialNodeKind::Noise || node.kind==MaterialNodeKind::ProceduralTexture) {
            if(node.kind>=MaterialNodeKind::Blur) {
                ImGui::SeparatorText(materialNodeKindName(node.kind));
                const auto parameter=[&](const char*,const char* transaction,const auto& draw){control(transaction,draw);};
                const auto option=[&](const char* label,const char* transaction,const char* values,const auto& get,const auto& set) {
                    control(transaction,[&](auto& n){int selected=get(n);const bool changed=ImGui::Combo(label,&selected,values);if(changed)set(n,selected);return changed;});
                };
                draw_material_atomic_controls(node,parameter,option,materialInputLink(document_.definition().graph,selected,MaterialPin::UV)!=nullptr);
            }
            if(node.kind==MaterialNodeKind::Resize) {
                ImGui::SeparatorText("Resize");
                control("Change resize width",[](auto& n){int value=static_cast<int>(n.resize.width);const bool changed=ImGui::DragInt("Width",&value,1,1,4096,"%d",ImGuiSliderFlags_AlwaysClamp);if(changed)n.resize.width=static_cast<unsigned>(value);return changed;});
                control("Change resize height",[](auto& n){int value=static_cast<int>(n.resize.height);const bool changed=ImGui::DragInt("Height",&value,1,1,4096,"%d",ImGuiSliderFlags_AlwaysClamp);if(changed)n.resize.height=static_cast<unsigned>(value);return changed;});
                control("Change resize filter",[](auto& n){int value=static_cast<int>(n.resize.filter);const bool changed=ImGui::Combo("Filter",&value,"Nearest\0Linear\0Area\0");if(changed)n.resize.filter=static_cast<MaterialResizeFilter>(value);return changed;});
            } else if(node.kind==MaterialNodeKind::Levels) {
                ImGui::SeparatorText("Levels");
                control("Change input black",[](auto& n){return ImGui::DragFloat("Input black",&n.image.inputBlack,.005f,0,1,"%.3f");});
                control("Change input white",[](auto& n){return ImGui::DragFloat("Input white",&n.image.inputWhite,.005f,0,1,"%.3f");});
                control("Change midtones",[](auto& n){return ImGui::DragFloat("Midtones",&n.image.gamma,.01f,.01f,100,"%.3f");});
                control("Change output black",[](auto& n){return ImGui::DragFloat("Output black",&n.image.outputBlack,.005f,0,1,"%.3f");});
                control("Change output white",[](auto& n){return ImGui::DragFloat("Output white",&n.image.outputWhite,.005f,0,1,"%.3f");});
            } else if(node.kind==MaterialNodeKind::Blend) {
                ImGui::SeparatorText("Blend");
                control("Change image blend mode",[](auto& n){int mode=static_cast<int>(n.image.blendMode);const bool changed=ImGui::Combo("Mode",&mode,"Mix\0Multiply\0Add\0Subtract\0Screen\0Overlay\0Minimum\0Maximum\0Difference\0Add/Subtract\0Soft Light\0");if(changed)n.image.blendMode=static_cast<MaterialImageBlend>(mode);return changed;});
                control("Change blend opacity",[](auto& n){return ImGui::SliderFloat("Opacity",&n.image.opacity,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            } else if(node.kind==MaterialNodeKind::Grayscale) {
                ImGui::SeparatorText("Grayscale");
                control("Change grayscale method",[](auto& n){int mode=static_cast<int>(n.image.grayMode);const bool changed=ImGui::Combo("Method",&mode,"Luminance\0Red\0Green\0Blue\0Alpha\0Average\0Desaturation\0Minimum\0Maximum\0");if(changed)n.image.grayMode=static_cast<MaterialGrayscaleMode>(mode);return changed;});
            } else if(node.kind==MaterialNodeKind::Noise) {
                ImGui::SeparatorText("Noise");
                control("Change noise pattern",[](auto& n){int mode=static_cast<int>(n.image.noisePattern);const bool changed=ImGui::Combo("Pattern",&mode,"Clouds\0Spots\0");if(changed)n.image.noisePattern=static_cast<MaterialNoisePattern>(mode);return changed;});
                control("Change noise seed",[](auto& n){int seed=static_cast<int>(n.image.seed);const bool changed=ImGui::SliderInt("Seed",&seed,0,65535,"%d",ImGuiSliderFlags_AlwaysClamp);if(changed)n.image.seed=static_cast<unsigned>(seed);return changed;});
                control("Change noise feature size",[](auto& n){return ImGui::DragFloat("Feature size",&n.image.featureSize,.1f,.1f,100,"%.2f");});
                ImGui::BeginDisabled(node.image.noisePattern==MaterialNoisePattern::Spots);
                control("Change noise detail",[](auto& n){return ImGui::SliderFloat("Detail amount",&n.image.roughness,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))ImGui::SetTooltip("Clouds only. Adds finer layers of detail.");
                ImGui::EndDisabled();
                control("Change noise contrast",[](auto& n){return ImGui::SliderFloat("Contrast",&n.image.noiseContrast,0,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            } else if(node.kind==MaterialNodeKind::BrightnessContrast) {
                ImGui::SeparatorText("Brightness / Contrast");
                control("Change image brightness",[](auto& n){return ImGui::SliderFloat("Brightness",&n.image.brightness,-1,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                control("Change image contrast",[](auto& n){return ImGui::SliderFloat("Contrast",&n.image.contrast,-1,1,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
            } else if(node.kind==MaterialNodeKind::Sharpen) {
                ImGui::SeparatorText("Sharpen");
                control("Change sharpen amount",[](auto& n){return ImGui::DragFloat("Amount",&n.image.sharpen,.01f,0,4,"%.3f");});
            } else if(node.kind==MaterialNodeKind::HeightToNormal) {
                ImGui::SeparatorText("Height to Normal");
                control("Change height normal strength",[](auto& n){return ImGui::DragFloat("Strength",&n.image.normalStrength,.01f,0,0,"%.3f");});
                control("Change height normal Y",[](auto& n){return ImGui::Checkbox("Flip Y",&n.image.normalFlipY);});
            }
            if(node.kind==MaterialNodeKind::Sharpen || node.kind==MaterialNodeKind::HeightToNormal) {
                if(ImGui::TreeNodeEx("Advanced##Image",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    control("Change image detail size",[](auto& n){float size=n.image.radius*1024;const bool changed=ImGui::DragFloat("Detail size",&size,.05f,.01f,1024,"%.2f");if(changed)n.image.radius=size/1024;return changed;});
                    if(ImGui::IsItemHovered())ImGui::SetTooltip("Higher values sample across broader features.");
                    ImGui::TreePop();
                }
            }
            const auto inputs=materialNodeInputs(document_.definition().graph,node,materialOutputNode(document_.definition().graph).output.shading,materialOutputNode(document_.definition().graph).output.blend);
            bool heading=false;
            for(const auto& input:inputs) {
                if(node.kind==MaterialNodeKind::ImageInput || input.type==MaterialValueType::Mapping || input.type==MaterialValueType::Normal || materialInputLink(document_.definition().graph,selected,input.pin))continue;
                if(!heading){ImGui::SeparatorText("Default inputs");heading=true;}
                ImGui::PushID(static_cast<int>(input.pin));
                const bool dynamic=materialNodeAdaptableImage(node.kind)&&
                    (input.pin==MaterialPin::Left||input.pin==MaterialPin::Right);
                if(dynamic)control("Change image input type",[&](auto& n) {
                    auto value=materialInputDefault(n,input.pin);int type=value.type==MaterialValueType::Color4?2:value.type==MaterialValueType::Color?1:0;
                    const bool changed=ImGui::Combo("Type",&type,"Grayscale\0RGB\0RGBA\0");
                    if(changed) {
                        const auto previous=value.type;value.type=type==2?MaterialValueType::Color4:type==1?MaterialValueType::Color:MaterialValueType::Scalar;
                        if(previous==MaterialValueType::Scalar && type>0)value.value={value.value[0],value.value[0],value.value[0],1};
                        else if(previous==MaterialValueType::Color && type==2)value.value[3]=1;
                        setMaterialInputDefault(n,input.pin,value);
                    }return changed;
                });
                auto candidate=document_.definition();auto* edited=findMaterialNode(candidate.graph,selected);auto value=materialInputDefault(*edited,input.pin);
                if(value.type==MaterialValueType::Color||value.type==MaterialValueType::Color4) {
                    Vec3 rgb{value.value[0],value.value[1],value.value[2]};float alpha=value.value[3];
                    ImGui::BeginDisabled(!editable);
                    auto change=drawHdrColorControl(input.name,rgb,nullptr,input.pin==MaterialPin::Right?emissionEditor_:colorEditor_);
                    if(value.type==MaterialValueType::Color4)change.include(ImGui::DragFloat("Alpha",&alpha,.01f,0,0,"%.3f"));
                    ImGui::EndDisabled();
                    if(editable&&!canceled&&(change.activated||change.changed)&&!document_.transactionActive())propertyEditing_=document_.begin("Change image input default");
                    if(editable&&!canceled&&change.changed&&propertyEditing_) {
                        value.value={rgb[0],rgb[1],rgb[2],alpha};setMaterialInputDefault(*edited,input.pin,value);
                        document_.update([&](auto& asset){asset=std::move(candidate);});
                    }
                    if(change.deactivated&&propertyEditing_)finish();
                } else control("Change image input default",[&](auto& n) {
                    auto value=materialInputDefault(n,input.pin);const bool changed=ImGui::DragFloat(input.name,value.value.data(),.01f,0,0,"%.3f");
                    if(changed)setMaterialInputDefault(n,input.pin,value);return changed;
                });
                ImGui::PopID();
            }
        }
        if(node.kind==MaterialNodeKind::Color || node.kind==MaterialNodeKind::ImageInput)hdr(node.kind==MaterialNodeKind::ImageInput?"Constant fallback":"Constant","Change color",false);
        if(node.kind==MaterialNodeKind::Texture2D || node.kind==MaterialNodeKind::ImageInput) {
            {
                ImGui::SeparatorText("Texture");
                auto image=node.texture.image.id;ImGui::BeginDisabled(!editable);
                const bool changed=textureField(assets,"Image",image,message_,node.texture.role==MaterialTextureRole::Normal);ImGui::EndDisabled();
                if(changed && editable) {finish();document_.edit("Assign texture",[&](auto& asset){if(auto* n=findMaterialNode(asset.graph,selected))n->texture.image.id=image;});}
                control("Change texture interpretation",[](auto& n){int role=static_cast<int>(n.texture.role);const bool changed=ImGui::Combo("Sample Type",&role,n.kind==MaterialNodeKind::ImageInput?"Color\0Linear Data\0":"Color\0Linear Data\0Normal\0");if(changed)n.texture.role=static_cast<MaterialTextureRole>(role);return changed;});
                if(findMaterialNode(document_.definition().graph,selected)->texture.role==MaterialTextureRole::Normal)
                    control("Change normal strength",[](auto& n){return ImGui::DragFloat("Normal strength",&n.texture.normalStrength,.01f,0,0,"%.3f");});
            }
            if(ImGui::TreeNodeEx("Sampler",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth)) {
                control("Change horizontal wrapping",[](auto& n){int mode=static_cast<int>(n.texture.sampler.wrapU);const bool changed=ImGui::Combo("Wrap U",&mode,"Repeat\0Clamp\0Mirror\0");if(changed)n.texture.sampler.wrapU=static_cast<ModelWrap>(mode);return changed;});
                control("Change vertical wrapping",[](auto& n){int mode=static_cast<int>(n.texture.sampler.wrapV);const bool changed=ImGui::Combo("Wrap V",&mode,"Repeat\0Clamp\0Mirror\0");if(changed)n.texture.sampler.wrapV=static_cast<ModelWrap>(mode);return changed;});
                control("Change texture minification",[](auto& n) {
                    int filter=static_cast<int>(n.texture.sampler.minFilter);
                    const bool changed=ImGui::Combo("Minification",&filter,"Linear (no mipmaps)\0Nearest (no mipmaps)\0Nearest / nearest mip\0Linear / nearest mip\0Nearest / linear mip\0Linear / linear mip\0");
                    if(changed)n.texture.sampler.minFilter=static_cast<ModelFilter>(filter);return changed;
                });
                control("Change texture magnification",[](auto& n) {
                    int filter=static_cast<int>(n.texture.sampler.magFilter);
                    const bool changed=ImGui::Combo("Magnification",&filter,"Linear\0Nearest\0");
                    if(changed)n.texture.sampler.magFilter=static_cast<ModelFilter>(filter);return changed;
                });
                ImGui::TreePop();
            }
        }
        if(node.kind==MaterialNodeKind::Clamp && !materialInputLink(document_.definition().graph,selected,MaterialPin::Left))
            control("Change clamp input",[](auto& n){return ImGui::DragFloat("Input",&n.left.value[0],.01f,0,0,"%.3f");});
        if(node.kind==MaterialNodeKind::Multiply||node.kind==MaterialNodeKind::Add||node.kind==MaterialNodeKind::Lerp||node.kind==MaterialNodeKind::Power||materialNodeImageAdjustment(node.kind)) {
            ImGui::SeparatorText("Default inputs");
            for(const auto pin:{MaterialPin::Left,MaterialPin::Right,MaterialPin::Alpha}) {
                if(pin==MaterialPin::Alpha&&node.kind!=MaterialNodeKind::Lerp)continue;
                if(materialInputLink(document_.definition().graph,selected,pin))continue;
                const bool adjustment=materialNodeImageAdjustment(node.kind);
                const char* label=adjustment?(pin==MaterialPin::Left?"Input":node.kind==MaterialNodeKind::HueShift?"Degrees":node.kind==MaterialNodeKind::Desaturation?"Saturation":"Contrast"):
                    pin==MaterialPin::Alpha?"Alpha":node.kind==MaterialNodeKind::Power?(pin==MaterialPin::Left?"Base":"Exponent"):pin==MaterialPin::Left?"A":"B";
                const auto fallback=[&](auto& n)->auto& {return pin==MaterialPin::Left?n.left:pin==MaterialPin::Right?n.right:n.alpha;};
                ImGui::PushID(static_cast<int>(pin));
                if(!adjustment && !(node.kind==MaterialNodeKind::Power&&pin==MaterialPin::Right))
                    control("Change input default type",[&](auto& n){auto& value=fallback(n);int type=value.type==MaterialValueType::Color?1:value.type==MaterialValueType::Vector2?2:value.type==MaterialValueType::Color4?3:0;
                        const bool changed=ImGui::Combo("Type",&type,"Scalar\0Color\0Vector2\0RGBA\0");
                        if(changed) {
                            const auto previous=value.type;
                            value.type=type==0?MaterialValueType::Scalar:type==1?MaterialValueType::Color:type==2?MaterialValueType::Vector2:MaterialValueType::Color4;
                            if(previous==MaterialValueType::Scalar&&value.type!=MaterialValueType::Scalar)value.value={value.value[0],value.value[0],value.value[0],value.type==MaterialValueType::Color4?1.f:value.value[0]};
                            else if(previous==MaterialValueType::Color&&value.type==MaterialValueType::Color4)value.value[3]=1;
                        }return changed;});
                auto candidate=document_.definition();auto* edited=findMaterialNode(candidate.graph,selected);
                auto& value=fallback(*edited);
                if(value.type==MaterialValueType::Color||value.type==MaterialValueType::Color4) {
                    Vec3 rgb{value.value[0],value.value[1],value.value[2]};
                    float alpha=value.value[3];
                    ImGui::BeginDisabled(!editable);
                    auto change=drawHdrColorControl(label,rgb,nullptr,pin==MaterialPin::Left?colorEditor_:pin==MaterialPin::Right?emissionEditor_:alphaEditor_);
                    if(value.type==MaterialValueType::Color4)change.include(ImGui::DragFloat("Alpha",&alpha,.01f,0,0,"%.3f"));
                    ImGui::EndDisabled();
                    if(editable && !canceled && (change.activated || change.changed) && !document_.transactionActive())propertyEditing_=document_.begin("Change input default");
                    if(editable && !canceled && change.changed && propertyEditing_) {
                        value.value={rgb[0],rgb[1],rgb[2],alpha};
                        document_.update([&](auto& asset){asset=std::move(candidate);});
                    }
                    if(change.deactivated && propertyEditing_)finish();
                } else control("Change input default",[&](auto& n){auto& value=fallback(n);
                    if(adjustment && pin==MaterialPin::Right) {
                        const bool hue=n.kind==MaterialNodeKind::HueShift;
                        const bool changed=ImGui::SliderFloat(label,value.value.data(),hue?-180.f:0.f,hue?180.f:2.f,hue?"%.1f deg":"%.3f");
                        if(hue)ImGui::SetItemTooltip("Positive degrees rotate red toward yellow and green. A full turn is 360 degrees.");
                        return changed;
                    }
                    return value.type==MaterialValueType::Vector2?ImGui::DragFloat2(label,value.value.data(),.01f,0,0,"%.3f"):ImGui::DragFloat(label,value.value.data(),.01f,0,0,"%.3f");});
                ImGui::PopID();
            }
        }
    }
    if(propertyEditing_ && !ImGui::IsAnyItemActive() && !ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup))finish();
    const auto diagnostic=assets.materialDiagnostic(document_.assetId());
    if(!diagnostic.empty())ImGui::TextWrapped("%s",diagnostic.c_str());
    if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
    if(!graph_.status().empty())ImGui::TextWrapped("%s",graph_.status().c_str());
    if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
}
void MaterialEditor::draw(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,double dt) {
    if(pending_==Pending::Exit && !document_.dirty())perform(assets);
    if(document_.isOpen() && assets.references().deleted(document_.assetId())) {
        document_.close();assets.protectMaterialDocument({});assets.setMaterialDocumentReferences({},{});message_="The open material asset was deleted.";
    }
    if(document_.isOpen())reconcileReferences(assets);
    if(document_.isOpen() && externalChanged_ && !document_.transactionActive() && pending_==Pending::None) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty())externalPopup_=true;
            else { const auto id=document_.assetId();if(!document_.load(id))message_=document_.status();else reconcileReferences(assets,true); }
        }
    }
    if(!isOpen()){
        if(!imageDocument_.empty())assets.releaseMaterialNodeImagePreviews(imageDocument_);
        imageGraph_.reset();imageDocument_.clear();graph_.releaseImages();preview_.release(renderer);modals(assets);return;
    }
    if(!imageGraph_||imageDocument_!=document_.assetId()||imageRevision_!=document_.previewRevision()) {
        if(!imageDocument_.empty()&&imageDocument_!=document_.assetId())assets.releaseMaterialNodeImagePreviews(imageDocument_);
        imageDocument_=document_.assetId();imageRevision_=document_.previewRevision();
        imageGraph_=std::make_shared<const MaterialGraph>(document_.definition().graph);
    }
    preview_.setImageGraph(imageGraph_);
    if(focusNext_){ImGui::SetNextWindowFocus();focusNext_=false;}
    ImGui::SetNextWindowDockID(0,ImGuiCond_Always);
    ImGui::SetNextWindowSize({1200,760},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({860,480},{FLT_MAX,FLT_MAX});
    bool visible=true;
    const auto title="Material Editor - "+document_.definition().name+(document_.dirty()?" *":"")+"###SpriteMaterial";
    const bool contents=ImGui::Begin(title.c_str(),&visible,DocumentEditorWindowFlags);
    if(!visible)request(Pending::Close,{},&assets);
    if(contents && isOpen()) {
        const auto action=drawDocumentEditorMenuBar([&]{finishInteraction(&assets);},[&] {
            return DocumentEditorMenuState{canSave(),canSaveAs(),canUndo(),canRedo(),document_.undoLabel(),document_.redoLabel()};
        },[&]{preview_.environmentMenu(assets);});
        switch(action) {
        case DocumentEditorAction::Save:save(assets);break;
        case DocumentEditorAction::SaveAs:save(assets,true);break;
        case DocumentEditorAction::Close:request(Pending::Close,{},&assets);break;
        case DocumentEditorAction::Undo:undo();break;
        case DocumentEditorAction::Redo:redo();break;
        case DocumentEditorAction::None:break;
        }
        if(isOpen() && ImGui::BeginTable("Material workspace",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("Preview and properties",ImGuiTableColumnFlags_WidthFixed,340);
            ImGui::TableSetupColumn("Graph",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::BeginChild("Material sidebar",{0,0});
            const float available=ImGui::GetContentRegionAvail().y;
            previewHeight_=std::clamp(previewHeight_,100.f,std::max(100.f,available-140));
            ImGui::BeginChild("Material preview",{0,previewHeight_});
            preview_.setNodePreview(graph_.previewTarget());
            preview_.draw(assets,renderer,resources,document_.definition(),dt);
            if(preview_.takeMaterialSelectionRequest())graph_.clearSelection();
            ImGui::EndChild();
            ImGui::InvisibleButton("Resize material preview",{ImGui::GetContentRegionAvail().x,6});
            if(ImGui::IsItemHovered() || ImGui::IsItemActive())ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            if(ImGui::IsItemActive())previewHeight_+=ImGui::GetIO().MouseDelta.y;
            const auto splitMinimum=ImGui::GetItemRectMin(),splitMaximum=ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine({splitMinimum.x,(splitMinimum.y+splitMaximum.y)*.5f},{splitMaximum.x,(splitMinimum.y+splitMaximum.y)*.5f},ImGui::GetColorU32(ImGuiCol_Border));
            ImGui::BeginChild("Material properties",{0,0});properties(assets);ImGui::EndChild();
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("Material graph workspace",{0,0});
            MaterialGraphEditor::Assets graphAssets;
            graphAssets.describe=[&](const std::string& id) {
                MaterialGraphEditor::TexturePresentation result;
                result.name=id.empty()?"None":assets.displayName(id);
                if(const auto* texture=assets.find(id);texture && !texture->hdr) {
                    result.available=true;result.width=texture->width;result.height=texture->height;
                    if(bgfx::isValid(texture->thumbnail))result.textureId=ImGuiBgfx::textureId(texture->thumbnail);
                }
                return result;
            };
            graphAssets.validTexture=[&](const std::string& id) {const auto* texture=assets.find(id);return texture && !texture->hdr;};
            graphAssets.generatedTexture=[&](const MaterialNode& node) {
                MaterialGraphEditor::TexturePresentation result;
                try {
                    auto type=MaterialValueType::Scalar;
                    if(const auto target=graph_.previewTarget();target&&target->node==node.id)
                        type=materialOutputType(document_.definition().graph,target->node,target->pin);
                    auto texture=assets.grungePreview(node.grunge,256,type);
                    result.available=texture.sourceReady && bgfx::isValid(texture.handle);
                    result.width=texture.width;result.height=texture.height;result.lease=std::move(texture.lease);
                    result.diagnostic=std::move(texture.diagnostic);
                    if(result.available)result.textureId=ImGuiBgfx::textureId(texture.handle,true);
                } catch(const std::exception& error){result.diagnostic=error.what();}
                return result;
            };
            graphAssets.nodeImage=[&](MaterialGraphPreviewTarget target,unsigned edge) {
                MaterialGraphEditor::TexturePresentation result;
                try {
                    auto texture=assets.materialNodeImagePreview(imageDocument_,imageGraph_,target,edge);
                    result.available=texture.sourceReady&&texture.complete&&bgfx::isValid(texture.handle);
                    result.width=texture.width;result.height=texture.height;result.lease=std::move(texture.lease);
                    result.diagnostic=std::move(texture.diagnostic);
                    if(result.available)result.textureId=ImGuiBgfx::textureId(texture.handle);
                } catch(const std::exception& error){result.diagnostic=error.what();}
                return result;
            };
            bool openTexturePicker=false;
            graphAssets.choose=[&](MaterialNodeId node){texturePickerNode_=node;openTexturePicker=true;};
            graph_.draw(document_,graphAssets);
            static ImGuiTextFilter graphTextureFilter;
            if(openTexturePicker){graphTextureFilter.Clear();ImGui::OpenPopup("Graph texture picker");}
            if(ImGui::BeginPopup("Graph texture picker")) {
                const auto* textureNode=findMaterialNode(document_.definition().graph,texturePickerNode_);
                if(!textureNode || (textureNode->kind!=MaterialNodeKind::Texture2D && textureNode->kind!=MaterialNodeKind::ImageInput) || !document_.editable())ImGui::CloseCurrentPopup();
                else {
                    graphTextureFilter.Draw("Search");
                    const auto selectedImage=textureNode->texture.image.id;
                    const auto assign=[&](const std::string& id) {
                        finishInteraction();
                        if(!document_.edit("Assign texture",[&](auto& asset){
                            if(auto* node=findMaterialNode(asset.graph,texturePickerNode_))node->texture.image.id=id;
                        }))message_=document_.status();
                        ImGui::CloseCurrentPopup();
                    };
                    if(ImGui::Selectable("None",selectedImage.empty()))assign({});
                    ImGui::BeginChild("Images",{360,ImGui::GetFontSize()*14});
                    for(const auto& texture:assets.assets())if(!texture.hdr && graphTextureFilter.PassFilter(assets.displayName(texture.id).c_str())) {
                        ImGui::PushID(texture.id.c_str());
                        if(ImGui::Selectable(assets.displayName(texture.id).c_str(),selectedImage==texture.id))assign(texture.id);
                        ImGui::PopID();
                    }
                    ImGui::EndChild();
                }
                ImGui::EndPopup();
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
    } else { preview_.cancelInteraction();finishInteraction(&assets); }
    ImGui::End();publish(assets);modals(assets);
}
} // namespace crimson
