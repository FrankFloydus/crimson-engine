#include "MaterialEditor.h"
#include "DocumentEditorMenu.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cfloat>

namespace crimson {
namespace {
float toSrgb(float linear) { return linear<=.0031308f ? linear*12.92f : 1.055f*std::pow(linear,1.f/2.4f)-.055f; }
float toLinear(float srgb) { return srgb<=.04045f ? srgb/12.92f : std::pow((srgb+.055f)/1.055f,2.4f); }
// Pickers and delivery use the same strict image compatibility checks.
bool textureField(AssetLibrary& assets,const char* title,std::string& id,std::string& message,bool procedural=false,bool normal=false) {
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
    const auto resolved=id.empty()?std::string{}:procedural?assets.references().resolveParticleTexture(id):assets.references().resolveTexture(id,normal);
    const auto* texture=resolved.empty()?nullptr:assets.find(resolved);
    float textX=minimum.x+3;
    if(texture && bgfx::isValid(texture->thumbnail)) {
        draw->AddImage(ImGuiBgfx::textureId(texture->thumbnail),{minimum.x+3,minimum.y+3},{minimum.x+height-3,maximum.y-3});textX+=height;
    }
    const auto name=id.empty()?std::string(procedural?"Procedural sprite":"None"):texture?assets.displayName(texture->id):"Missing: "+assets.displayName(id);
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
        if(ImGui::Selectable(procedural?"Procedural sprite":"None",id.empty())){if(!id.empty()){id.clear();changed=true;}ImGui::CloseCurrentPopup();}
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
    if(assets)publish(*assets);
}
void MaterialEditor::request(Pending pending,std::string id,AssetLibrary* assets) {
    if(pending_!=Pending::None) return;
    if(document_.transactionActive() && !document_.commit()) { message_=document_.status();return; }
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
        assets.protectMaterialDocument(document_.assetId());publishedRevision_=0;
        publish(assets);assets.refreshMaterials();focusNext_=true;externalChanged_=false;
    } else message_=document_.status();
    pendingId_.clear();pendingFolder_.clear();
}
void MaterialEditor::publish(AssetLibrary& assets) {
    if(!document_.isOpen()) return;
    std::vector<std::string> textures;
    const auto& surface=document_.definition().surface;
    for(const auto* slot:{&surface.baseColorTexture,&surface.emissiveTexture,&surface.metallicRoughnessTexture,&surface.normalTexture,&surface.occlusionTexture})
        if(!slot->image.id.empty())textures.push_back(slot->image.id);
    assets.setMaterialDocumentReferences(std::move(textures),document_.historicalTextureReferences());
    if(publishedRevision_!=document_.revision() || publishedId_!=document_.assetId()) {
        if(!document_.assetId().starts_with("builtin:"))assets.publishMaterial(document_.committed());
        publishedRevision_=document_.revision();publishedId_=document_.assetId();
    }
}
void MaterialEditor::save(AssetLibrary& assets,bool saveAs) {
    if(!document_.isOpen()) return;
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
    for(const auto& path:paths) if(path.filename()==pathFromUtf8(document_.assetId()).filename())
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
                try { if(!previous.starts_with("builtin:"))assets.publishMaterial(std::make_shared<MaterialAsset>(loadMaterialAsset(assets.projectRoot(),previous))); }
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
    const bool editable=document_.editable();
    const auto& material=document_.definition();
    if(!editable)ImGui::TextDisabled("Read-only");
    const bool escape=document_.transactionActive() && ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    const bool canceled=suppressFinalizedUpdate_ || escape;
    suppressFinalizedUpdate_=false;
    if(escape)document_.cancel();
    if(particleMaterialCompatible(material) || modelMaterialCompatible(material)) {
        const auto control=[&](const char* label,const auto& draw) {
            auto candidate=document_.definition();
            ImGui::BeginDisabled(!editable);
            const bool changed=draw(candidate);
            ImGui::EndDisabled();
            if(!editable)return;
            if(!canceled && (ImGui::IsItemActivated() || changed) && !document_.transactionActive())document_.begin(label);
            if(!canceled && changed)document_.update([&](auto& asset){asset=candidate;});
            if(ImGui::IsItemDeactivated() && document_.transactionActive())document_.commit();
        };
        if(ImGui::CollapsingHeader("Material",ImGuiTreeNodeFlags_DefaultOpen)) {
            control("Change material shading",[](auto& asset) {
                bool changed=false;
                const auto& surface=asset.surface;
                const bool unlitReady=surface.metallicRoughnessTexture.image.empty() && surface.normalTexture.image.empty() &&
                    surface.occlusionTexture.image.empty() && surface.emissiveTexture.image.empty() &&
                    (surface.baseColorTexture.image.empty() || surface.baseColorTexture.texCoord==0);
                if(ImGui::BeginCombo("Shading",asset.shading==MaterialShading::Unlit?"Unlit":"Lit")) {
                    ImGui::BeginDisabled(!unlitReady);
                    if(ImGui::Selectable("Unlit",asset.shading==MaterialShading::Unlit) && asset.shading!=MaterialShading::Unlit) {
                        asset.shading=MaterialShading::Unlit;asset.blend=MaterialBlend::Alpha;asset.surface.alphaMask=false;changed=true;
                    }
                    ImGui::EndDisabled();
                    if(!unlitReady && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled|ImGuiHoveredFlags_DelayShort))
                        ImGui::SetTooltip("Requires empty lighting textures and base color UV0.");
                    if(ImGui::Selectable("Lit",asset.shading==MaterialShading::StandardPbr) && asset.shading!=MaterialShading::StandardPbr) {
                        asset.shading=MaterialShading::StandardPbr;asset.blend=MaterialBlend::Opaque;asset.surface.alphaMask=false;changed=true;
                    }
                    ImGui::EndCombo();
                }
                return changed;
            });
        }
        const bool sprite=document_.definition().shading==MaterialShading::Unlit;
        if(ImGui::CollapsingHeader("Appearance",ImGuiTreeNodeFlags_DefaultOpen)) {
            control("Change material color",[](auto& asset) {
                auto color=asset.surface.baseColor;for(size_t channel=0;channel<3;++channel)color[channel]=toSrgb(color[channel]);
                const bool changed=ImGui::ColorEdit4("Color / opacity",color.data());
                if(changed){for(size_t channel=0;channel<3;++channel)color[channel]=toLinear(std::clamp(color[channel],0.f,1.f));asset.surface.baseColor=color;}
                return changed;
            });
            control("Change material blend",[&](auto& asset) {
                int mode=sprite?(asset.blend==MaterialBlend::Alpha?0:1):(asset.blend==MaterialBlend::Opaque?0:1);
                const bool changed=ImGui::Combo("Blend",&mode,sprite?"Alpha\0Additive\0":"Opaque\0Mask\0");
                if(changed) {
                    asset.blend=sprite?(mode==0?MaterialBlend::Alpha:MaterialBlend::Additive):(mode==0?MaterialBlend::Opaque:MaterialBlend::Mask);
                    asset.surface.alphaMask=asset.blend==MaterialBlend::Mask;
                }
                return changed;
            });
            if(!sprite) {
                if(document_.definition().blend==MaterialBlend::Mask)
                    control("Change alpha cutoff",[](auto& asset){return ImGui::SliderFloat("Alpha cutoff",&asset.surface.alphaCutoff,0,1,"%.3f");});
                control("Change roughness",[](auto& asset){return ImGui::SliderFloat("Roughness",&asset.surface.roughness,0,1,"%.3f");});
                control("Change metallic",[](auto& asset){return ImGui::SliderFloat("Metallic",&asset.surface.metallic,0,1,"%.3f");});
                control("Change double-sided rendering",[](auto& asset){return ImGui::Checkbox("Double sided",&asset.surface.doubleSided);});
            }
        }
        if(ImGui::CollapsingHeader("UV mapping")) {
            control("Link UV scale",[](auto& asset){return ImGui::Checkbox("Link scale XYZ",&asset.surface.uvScaleLinked);});
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("Lock the ratio between the three scale components.");
            control("Change UV scale",[&](auto& asset) {
                const auto before=asset.surface.uvScale;
                const bool changed=ImGui::DragFloat3("Scale",asset.surface.uvScale.data(),.01f,0,0,"%.3f");
                if(ImGui::IsItemActivated())uvScaleBaseline_=before;
                if(changed && asset.surface.uvScaleLinked) {
                    const auto baseline=ImGui::IsItemActive()?uvScaleBaseline_:before;
                    for(unsigned axis=0;axis<3;++axis)if(asset.surface.uvScale[axis]!=before[axis]) {
                        const auto value=asset.surface.uvScale[axis];
                        if(std::abs(baseline[axis])>1e-8f)asset.surface.uvScale=baseline*(static_cast<double>(value)/baseline[axis]);
                        else asset.surface.uvScale=before+Vec3{value-before[axis],value-before[axis],value-before[axis]};
                        break;
                    }
                }
                return changed;
            });
            control("Change UV offset",[](auto& asset){return ImGui::DragFloat3("Offset",asset.surface.uvOffset.data(),.01f,0,0,"%.3f");});
            control("Change UV rotation",[](auto& asset){return ImGui::DragFloat3("Rotation (deg)",asset.surface.uvRotation.data(),.5f,0,0,"%.2f");});
            control("Change triplanar mapping",[](auto& asset){return ImGui::Checkbox("Triplanar",&asset.surface.triplanar);});
            ImGui::BeginDisabled(sprite || !document_.definition().surface.triplanar);
            control("Change triplanar space",[](auto& asset){return ImGui::Checkbox("World Triplanar",&asset.surface.triplanarWorld);});
            ImGui::EndDisabled();
        }
        if(ImGui::CollapsingHeader("Sampler")) {
            const auto samplerSeed=[](const ModelMaterial& surface) {
                for(const auto* slot:{&surface.baseColorTexture,&surface.metallicRoughnessTexture,&surface.normalTexture,&surface.occlusionTexture,&surface.emissiveTexture})
                    if(!slot->image.empty())return materialSampler(surface,*slot);
                return materialSampler(surface,surface.baseColorTexture);
            };
            if(!document_.definition().surface.sharedSampler) {
                const auto& surface=document_.definition().surface;const auto seed=samplerSeed(surface);
                bool mixed=false;
                for(const auto* slot:{&surface.baseColorTexture,&surface.metallicRoughnessTexture,&surface.normalTexture,&surface.occlusionTexture,&surface.emissiveTexture})
                    if(!slot->image.empty() && materialSampler(surface,*slot)!=seed)mixed=true;
                ImGui::TextDisabled("%s",mixed?"Mixed samplers":"Per-texture sampler");
                ImGui::BeginDisabled(!editable);
                if(ImGui::Button("Use one sampler") && editable) {
                    if(!document_.transactionActive() || document_.commit())
                        document_.edit("Use shared material sampler",[&](auto& asset){asset.surface.sharedSampler=samplerSeed(asset.surface);});
                }
                ImGui::EndDisabled();
            }
            control("Change material horizontal wrapping",[&](auto& asset) {
                auto sampler=samplerSeed(asset.surface);int mode=static_cast<int>(sampler.wrapU);
                const bool changed=ImGui::Combo("Wrap U",&mode,"Repeat\0Clamp\0Mirror\0");
                if(changed){sampler.wrapU=static_cast<ModelWrap>(mode);asset.surface.sharedSampler=sampler;}return changed;
            });
            control("Change material vertical wrapping",[&](auto& asset) {
                auto sampler=samplerSeed(asset.surface);int mode=static_cast<int>(sampler.wrapV);
                const bool changed=ImGui::Combo("Wrap V",&mode,"Repeat\0Clamp\0Mirror\0");
                if(changed){sampler.wrapV=static_cast<ModelWrap>(mode);asset.surface.sharedSampler=sampler;}return changed;
            });
            control("Change material filtering",[&](auto& asset) {
                auto sampler=samplerSeed(asset.surface);const bool nearest=sampler.magFilter==ModelFilter::Nearest;bool changed=false;
                if(ImGui::BeginCombo("Filtering",nearest?"Nearest":"Linear")) {
                    if(ImGui::Selectable("Nearest",nearest)){sampler.magFilter=ModelFilter::Nearest;sampler.minFilter=ModelFilter::NearestMipmapNearest;changed=true;}
                    if(ImGui::Selectable("Linear",!nearest)){sampler.magFilter=ModelFilter::Linear;sampler.minFilter=ModelFilter::LinearMipmapLinear;changed=true;}
                    ImGui::EndCombo();
                }
                if(changed)asset.surface.sharedSampler=sampler;return changed;
            });
        }
        const auto texture=[&](const char* title,ModelTexture ModelMaterial::* slot,bool normal=false) {
            ImGui::PushID(title);
            auto textureId=(document_.definition().surface.*slot).image.id;
            ImGui::BeginDisabled(!editable);
            if(textureField(assets,title,textureId,message_,sprite,normal) && editable) {
                if(!document_.transactionActive() || document_.commit())
                    document_.edit(std::string("Assign ")+title,[&](auto& asset){(asset.surface.*slot).image.id=textureId;});
            }
            ImGui::EndDisabled();
            if(!sprite) {
                ImGui::BeginDisabled(document_.definition().surface.triplanar);
                control("Change texture UV set",[&](auto& asset){auto& source=asset.surface.*slot;int coordinate=static_cast<int>(source.texCoord);const bool changed=ImGui::Combo("UV set",&coordinate,"UV0\0UV1\0");if(changed)source.texCoord=static_cast<uint32_t>(coordinate);return changed;});
                ImGui::EndDisabled();
            }
            ImGui::PopID();
        };
        if(ImGui::CollapsingHeader("Textures",ImGuiTreeNodeFlags_DefaultOpen)) {
            texture(sprite?"Texture":"Base color texture",&ModelMaterial::baseColorTexture);
            if(!sprite) {
                if(ImGui::TreeNodeEx("Metallic / roughness texture",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    texture("Metallic / roughness texture",&ModelMaterial::metallicRoughnessTexture);
                    ImGui::TreePop();
                }
                if(ImGui::TreeNodeEx("Normal texture",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    control("Change normal strength",[](auto& asset){return ImGui::DragFloat("Strength##normal",&asset.surface.normalScale,.01f,0,10,"%.3f",ImGuiSliderFlags_AlwaysClamp);});
                    texture("Normal texture",&ModelMaterial::normalTexture,true);
                    ImGui::TreePop();
                }
                if(ImGui::TreeNodeEx("Occlusion texture",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    control("Change occlusion strength",[](auto& asset){return ImGui::SliderFloat("Strength##occlusion",&asset.surface.occlusionStrength,0,1,"%.3f");});
                    texture("Occlusion texture",&ModelMaterial::occlusionTexture);
                    ImGui::TreePop();
                }
                if(ImGui::TreeNodeEx("Emission",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                    control("Change emissive color",[](auto& asset) {
                        auto color=asset.surface.emissive;for(auto& channel:color)channel=toSrgb(channel);
                        const bool changed=ImGui::ColorEdit3("Emissive color",color.data(),ImGuiColorEditFlags_HDR|ImGuiColorEditFlags_Float);
                        if(changed){for(auto& channel:color)channel=toLinear(std::max(channel,0.f));asset.surface.emissive=color;}
                        return changed;
                    });
                    texture("Emissive texture",&ModelMaterial::emissiveTexture);
                    ImGui::TreePop();
                }
            }
        }
        if(document_.transactionActive() && !ImGui::IsAnyItemActive())document_.commit();
    } else {
        if(ImGui::CollapsingHeader("Appearance",ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Color: %.3f, %.3f, %.3f / alpha %.3f",toSrgb(material.surface.baseColor[0]),toSrgb(material.surface.baseColor[1]),toSrgb(material.surface.baseColor[2]),material.surface.baseColor[3]);
            ImGui::Text("Roughness: %.3f",material.surface.roughness);
            ImGui::Text("Metallic: %.3f",material.surface.metallic);
            const auto& image=material.surface.baseColorTexture.image.id;
            ImGui::TextWrapped("Texture: %s",image.empty()?"None":assets.displayName(image).c_str());
        }
    }
    const auto diagnostic=assets.materialDiagnostic(document_.assetId());
    if(!diagnostic.empty())ImGui::TextWrapped("%s",diagnostic.c_str());
    if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
    if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
}
void MaterialEditor::draw(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,double dt) {
    if(pending_==Pending::Exit && !document_.dirty())perform(assets);
    if(document_.isOpen() && assets.references().deleted(document_.assetId())) {
        document_.close();assets.protectMaterialDocument({});assets.setMaterialDocumentReferences({},{});message_="The open material asset was deleted.";
    }
    if(document_.isOpen() && externalChanged_ && !document_.transactionActive() && pending_==Pending::None) {
        externalChanged_=false;
        if(document_.externalFileChanged()) {
            if(document_.dirty())externalPopup_=true;
            else { const auto id=document_.assetId();if(!document_.load(id))message_=document_.status(); }
        }
    }
    if(!isOpen()){preview_.release(renderer);modals(assets);return;}
    if(focusNext_){ImGui::SetNextWindowFocus();focusNext_=false;}
    ImGui::SetNextWindowDockID(0,ImGuiCond_Always);
    ImGui::SetNextWindowSize({1100,700},ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints({720,420},{FLT_MAX,FLT_MAX});
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
            ImGui::TableSetupColumn("Properties",ImGuiTableColumnFlags_WidthFixed,340);
            ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableNextColumn();
            ImGui::BeginChild("Material properties",{0,0});
            properties(assets);
            ImGui::EndChild();
            ImGui::TableNextColumn();
            ImGui::BeginChild("Material preview",{0,0});
            preview_.draw(assets,renderer,resources,document_.definition(),dt);
            ImGui::EndChild();
            ImGui::EndTable();
        }
    } else preview_.cancelInteraction();
    ImGui::End();publish(assets);modals(assets);
}
} // namespace crimson
