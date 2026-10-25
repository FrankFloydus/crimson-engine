#include "MaterialEditor.h"
#include "TextureAssetField.h"
#include "PropertyReset.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include <crimson/renderer/forward/BlendMaterial.h>
#include <imgui.h>
#include <algorithm>
#include <array>
#include <cmath>

namespace crimson {
namespace {
constexpr std::array<const char*,7> channelNames{"Base Color","Roughness","Metallic","Normal","AO","Height","Emission"};
bool canGenerate(std::size_t channel) { return channel==1||channel==2||channel==3||channel==5; }
bool colorChannel(std::size_t channel) { return channel==0||channel==6; }
bool materialField(AssetLibrary& assets,Renderer& renderer,const std::string& owner,std::string& id,std::string& message) {
    bool changed=false;
    const auto assign=[&](const std::string& candidate) {
        const auto source=assets.findMaterial(candidate);
        if(candidate==owner||(!candidate.empty()&&(!source||!blendMaterialSourceCompatible(*source)))) {
            message="Choose an ordinary Lit, Opaque material.";return;
        }
        if(id!=candidate){id=candidate;changed=true;}message.clear();
    };
    ImGui::TextUnformatted("Material");
    ImGui::BeginGroup();
    const float edge=ImGui::GetFrameHeight();
    const auto texture=renderer.thumbnailTexture(id);
    if(bgfx::isValid(texture))ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)),{edge,edge});
    else ImGui::Dummy({edge,edge});
    ImGui::SameLine();
    const auto label=id.empty()?std::string("None"):assets.displayName(id);
    const float available=std::max(edge,ImGui::GetContentRegionAvail().x-edge-ImGui::GetStyle().ItemSpacing.x);
    if(ImGui::Button((label+"##source").c_str(),{available,edge}))ImGui::OpenPopup("Select material");
    ImGui::SameLine();ImGui::BeginDisabled(id.empty());if(ImGui::Button("x##source",{edge,edge}))assign({});ImGui::EndDisabled();
    ImGui::EndGroup();
    if(ImGui::BeginDragDropTarget()) {
        if(const auto* payload=ImGui::AcceptDragDropPayload("CC_MATERIAL_ASSET")) {
            const auto* data=static_cast<const char*>(payload->Data);
            if(data&&payload->DataSize>1&&data[payload->DataSize-1]=='\0'&&
               std::find(data,data+payload->DataSize-1,'\0')==data+payload->DataSize-1)
                assign(std::string(data,static_cast<std::size_t>(payload->DataSize-1)));
        }
        ImGui::EndDragDropTarget();
    }
    static ImGuiTextFilter filter;
    if(ImGui::BeginPopup("Select material")) {
        if(ImGui::IsWindowAppearing()){filter.Clear();ImGui::SetKeyboardFocusHere();}
        filter.Draw("Search materials");
        if(ImGui::Selectable("None",id.empty())){assign({});ImGui::CloseCurrentPopup();}
        ImGui::BeginChild("Material choices",{420,260});
        for(const auto& entry:assets.catalog())if(entry.kind==AssetKind::Material&&entry.id!=owner) {
            const auto source=assets.findMaterial(entry.id);if(!source||!blendMaterialSourceCompatible(*source))continue;
            const auto name=assets.displayName(entry.id);if(!filter.PassFilter(name.c_str()))continue;
            ImGui::PushID(entry.id.c_str());
            if(ImGui::Selectable(name.c_str(),entry.id==id)){assign(entry.id);ImGui::CloseCurrentPopup();}
            ImGui::PopID();
        }
        ImGui::EndChild();ImGui::EndPopup();
    }
    return changed;
}
}

void MaterialEditor::blendWorkspace(AssetLibrary& assets,Renderer& renderer,ResourceManager& resources,double dt) {
    if(!document_.definition().blendDefinition)return;
    auto definition=*document_.definition().blendDefinition;
    const bool editable=document_.editable();
    const bool escape=propertyEditing_&&ImGui::IsKeyPressed(ImGuiKey_Escape,false);
    const bool canceled=suppressFinalizedUpdate_||escape;suppressFinalizedUpdate_=false;
    if(escape){document_.cancel();propertyEditing_=false;for(auto& state:blendColorEditors_)state.cancel();definition=*document_.definition().blendDefinition;}
    const auto finish=[&] {
        if(propertyEditing_&&!document_.commit()){message_=document_.status();document_.cancel();}
        propertyEditing_=false;blendControlId_=0;
    };
    const auto apply=[&](const char* label,ImGuiID owner,bool changed,bool activated,bool deactivated) {
        if(!editable||canceled)return;
        if((activated||changed)&&propertyEditing_&&blendControlId_!=owner)finish();
        if((activated||changed)&&!document_.transactionActive()){propertyEditing_=document_.begin(label);blendControlId_=owner;}
        if(changed&&propertyEditing_) {
            if(!document_.update([&](auto& asset){asset.blendDefinition=definition;}))message_=document_.status();
            else message_.clear();
        }
        if(deactivated&&propertyEditing_&&blendControlId_==owner)finish();
    };
    const auto control=[&](const char* label,const auto& draw) {
        const auto owner=ImGui::GetID(label);
        ImGui::BeginDisabled(!editable);const bool changed=draw();
        const bool activated=ImGui::IsItemActivated(),deactivated=ImGui::IsItemDeactivated();ImGui::EndDisabled();
        apply(label,owner,changed,activated,deactivated);
    };
    const auto scalar=[&](const char* label,float& value,float defaults,float minimum,float maximum) {
        control(label,[&]{return resettableProperty(value,defaults,[&]{return ImGui::SliderFloat(label,&value,minimum,maximum,"%.3f",ImGuiSliderFlags_AlwaysClamp);});});
    };
    std::size_t colorSlot{};
    const auto color=[&](const char* label,Vec3& value,Vec3 defaults,float maximum=1.f) {
        ImGui::BeginDisabled(!editable);
        const auto change=drawHdrColorControl(label,value,nullptr,blendColorEditors_[blendLayer_*21+colorSlot+1],false,maximum,"Alpha",defaults);
        ImGui::EndDisabled();apply(label,ImGui::GetID(label),change.changed,change.activated,change.deactivated&&!change.active);
    };
    // Child publications replace immutable snapshots. Include their revisions
    // even while this document has no local edits.
    std::uint64_t sourceRevision=assets.references().revision();
    std::vector<std::shared_ptr<const MaterialAsset>> sources;
    for(const auto& id:materialMaterialReferences(document_.definition())) {
        sourceRevision^=assets.materialRevision(id)+0x9e3779b97f4a7c15ull+(sourceRevision<<6)+(sourceRevision>>2);
        if(auto source=assets.findMaterial(id))sources.push_back(std::move(source));
    }
    // The renderer helper creates independent diagnostic endpoints after each
    // layer's adjustments, retaining source-image provenance for async baking.
    if(blendImageRevision_!=document_.previewRevision()||blendSourceRevision_!=sourceRevision) {
        blendChannelGraphs_={};
        blendImageRevision_=document_.previewRevision();blendSourceRevision_=sourceRevision;
    }
    const auto previewGraph=[&](std::size_t layer,std::size_t channel) {
        auto& graph=blendChannelGraphs_[layer*7+channel];
        if(!graph)graph=std::make_shared<const MaterialGraph>(blend_material_channel_preview_graph(document_.definition(),sources,layer,static_cast<BlendMaterialChannel>(channel)));
        return graph;
    };
    if(blendPreview_) {
        try {blendImageGraph_=previewGraph(blendPreviewChannel_/7,blendPreviewChannel_%7);}
        catch(const std::exception& error){message_=error.what();}
    }
    if(!ImGui::BeginTable("Blend workspace",2,ImGuiTableFlags_Resizable|ImGuiTableFlags_BordersInnerV))return;
    ImGui::TableSetupColumn("Preview",ImGuiTableColumnFlags_WidthStretch,.85f);
    ImGui::TableSetupColumn("Layers",ImGuiTableColumnFlags_WidthStretch,1.f);
    ImGui::TableNextColumn();
    ImGui::BeginChild("Blend preview",{0,0});
    if(ImGui::Button("Full material")){blendPreview_.reset();preview_.setNodePreview({});}
    auto previewMaterial=document_.definition();
    if(blendPreview_&&blendImageGraph_) {
        previewMaterial.graph=*blendImageGraph_;previewMaterial.blendDefinition.reset();
        previewMaterial.surface.graph=blendImageGraph_;
        preview_.setImageGraph(blendImageGraph_);preview_.setNodePreview(blendPreview_);
    } else {preview_.setImageGraph(imageGraph_);preview_.setNodePreview({});}
    preview_.draw(assets,renderer,resources,previewMaterial,dt);
    if(preview_.takeMaterialSelectionRequest())blendPreview_.reset();
    ImGui::EndChild();ImGui::TableNextColumn();
    ImGui::BeginChild("Blend layers",{0,0});
    if(!editable)ImGui::TextDisabled("Read-only");
    if(ImGui::TreeNodeEx("Material",ImGuiTreeNodeFlags_SpanAvailWidth)) {
        control("Double sided",[&]{return resettableProperty(definition.output.doubleSided,false,[&]{return ImGui::Checkbox("Double sided",&definition.output.doubleSided);});});
        control("Receive Fog",[&]{return resettableProperty(definition.output.receiveFog,true,[&]{return ImGui::Checkbox("Receive Fog",&definition.output.receiveFog);});});
        ImGui::TreePop();
    }
    std::size_t layerCount=3;
    if((!definition.preservedExpressions.empty()&&definition.layers[3].referenceNode)||definition.layers[3].enabled||!definition.layers[3].material.empty())layerCount=4;
    if(ImGui::BeginTabBar("Blend layer tabs")) {
        for(std::size_t index=0;index<layerCount;++index) {
            const auto title="Layer "+std::to_string(index+1)+(index==0?" (Base)":"");
            if(!ImGui::BeginTabItem(title.c_str()))continue;
            if(blendLayer_!=index){finish();for(auto& state:blendColorEditors_)state.cancel();blendLayer_=index;}
            ImGui::PushID(static_cast<int>(index));auto& layer=definition.layers[index];
            if(index||!layer.enabled)control("Enable layer",[&]{return ImGui::Checkbox("Enabled",&layer.enabled);});
            ImGui::BeginDisabled(!layer.enabled);
            control("Layer source",[&]{int source=static_cast<int>(layer.source);const bool changed=ImGui::Combo("Source",&source,"Inline\0Existing Material\0");if(changed)layer.source=static_cast<BlendMaterialSource>(source);return changed;});
            if(layer.source==BlendMaterialSource::Existing)
                control("Assign layer material",[&]{return materialField(assets,renderer,document_.assetId(),layer.material.id,message_);});
            if(ImGui::TreeNodeEx("Mapping",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                if(layer.source==BlendMaterialSource::Existing)control("Override mapping",[&]{return ImGui::Checkbox("Override mapping",&layer.appearance.mappingOverride);});
                ImGui::BeginDisabled(layer.source==BlendMaterialSource::Existing&&!layer.appearance.mappingOverride);
                auto& mapping=layer.appearance.mapping;
                control("Mapping coordinates",[&]{int mode=static_cast<int>(mapping.mode);const bool changed=ImGui::Combo("Coordinates",&mode,"UV0\0UV1\0Local Triplanar\0World Triplanar\0");if(changed)mapping.mode=static_cast<MaterialUvMode>(mode);return changed;});
                const bool triplanar=mapping.mode==MaterialUvMode::LocalTriplanar||mapping.mode==MaterialUvMode::WorldTriplanar;
                control("Link mapping scale",[&]{return resettableProperty(mapping.scaleLinked,true,[&]{return ImGui::Checkbox("Link scale",&mapping.scaleLinked);});});
                control("Mapping scale",[&] {
                    const auto before=mapping.scale;
                    const bool changed=resettableProperty(mapping.scale,Vec3{1,1,1},[&]{return triplanar?ImGui::DragFloat3("Scale",mapping.scale.data(),.01f,0,0,"%.3f"):ImGui::DragFloat2("Scale XY",mapping.scale.data(),.01f,0,0,"%.3f");});
                    if(ImGui::IsItemActivated())uvScaleBaseline_=before;
                    if(changed&&mapping.scaleLinked&&!propertyResetClicked())for(unsigned axis=0;axis<(triplanar?3u:2u);++axis)if(mapping.scale[axis]!=before[axis]) {
                        const auto baseline=ImGui::IsItemActive()?uvScaleBaseline_:before;const float value=mapping.scale[axis];
                        if(std::abs(baseline[axis])>1e-8f)mapping.scale=baseline*(static_cast<double>(value)/baseline[axis]);
                        else mapping.scale=before+Vec3{value-before[axis],value-before[axis],value-before[axis]};break;
                    }
                    return changed;
                });
                control("Mapping rotation",[&]{return triplanar?resettableProperty(mapping.rotation,Vec3{},[&]{return ImGui::DragFloat3("Rotation",mapping.rotation.data(),.5f,0,0,"%.1f deg");}):resettableProperty(mapping.rotation[2],0.f,[&]{return ImGui::DragFloat("Rotation",&mapping.rotation[2],.5f,0,0,"%.1f deg");});});
                control("Mapping offset",[&]{return resettableProperty(mapping.offset,Vec3{},[&]{return triplanar?ImGui::DragFloat3("Offset",mapping.offset.data(),.01f,0,0,"%.3f"):ImGui::DragFloat2("Offset XY",mapping.offset.data(),.01f,0,0,"%.3f");});});
                ImGui::EndDisabled();ImGui::TreePop();
            }
            for(std::size_t channel=0;channel<channelNames.size();++channel) {
                colorSlot=channel*3;
                ImGui::PushID(static_cast<int>(channel));
                const auto target=blend_material_channel_preview_target(definition,index,static_cast<BlendMaterialChannel>(channel));
                const auto image=[&](float edge,unsigned resolution) {
                    PreparedTexture texture;
                    try {texture=assets.materialNodeImagePreview(imageDocument_,previewGraph(index,channel),target,resolution);}
                    catch(const std::exception& error){texture.diagnostic=error.what();}
                    const auto start=ImGui::GetCursorScreenPos();
                    ImGui::InvisibleButton("Map preview",{edge,edge});
                    auto* draw=ImGui::GetWindowDrawList();draw->AddRectFilled(start,{start.x+edge,start.y+edge},ImGui::GetColorU32(ImGuiCol_FrameBg));
                    if(bgfx::isValid(texture.handle))draw->AddImage(ImGuiBgfx::textureId(texture.handle),start,{start.x+edge,start.y+edge});
                    else draw->AddText({start.x+3,start.y+3},ImGui::GetColorU32(ImGuiCol_TextDisabled),"View");
                    if(ImGui::IsItemClicked()){finish();blendPreview_=target;blendPreviewChannel_=index*7+channel;preview_.setMode(MaterialPreviewMode::TwoD);}
                    if(ImGui::IsItemHovered()) {
                        if(!texture.diagnostic.empty())ImGui::SetTooltip("%s\nClick for a live 2D or 3D preview.",texture.diagnostic.c_str());
                        else ImGui::SetTooltip("Preview %s",channelNames[channel]);
                    }
                };
                image(36,64);ImGui::SameLine();
                const bool expanded=ImGui::TreeNodeEx(channelNames[channel],ImGuiTreeNodeFlags_SpanAvailWidth);
                if(expanded) {
                    image(std::min(160.f,ImGui::GetContentRegionAvail().x),128);
                    auto& recipe=layer.channels[channel];
                    if(layer.source==BlendMaterialSource::Inline) {
                        control("Map source",[&]{int mode=static_cast<int>(recipe.mode);const bool changed=ImGui::Combo("Input",&mode,canGenerate(channel)?"Value\0Texture\0Generated\0":"Value\0Texture\0");if(changed){recipe.mode=static_cast<BlendChannelMode>(mode);if(recipe.mode==BlendChannelMode::Generated&&channel==3)recipe.generatedFrom=BlendMaterialChannel::Height;}return changed;});
                        if(recipe.mode==BlendChannelMode::Value) {
                            if(colorChannel(channel)) {
                                Vec3 rgb{recipe.value[0],recipe.value[1],recipe.value[2]};
                                ImGui::BeginDisabled(!editable);const auto change=drawHdrColorControl("Value",rgb,nullptr,blendColorEditors_[index*21+colorSlot],false,channel==6?1000000.f:1.f,"Alpha",channel==6?Vec3{}:Vec3{1,1,1});ImGui::EndDisabled();
                                if(change.changed)for(std::size_t component=0;component<3;++component)recipe.value[component]=rgb[component];
                                apply("Map value",ImGui::GetID("Map value"),change.changed,change.activated,change.deactivated&&!change.active);
                            } else if(channel==3)control("Normal value",[&]{return resettableProperty(recipe.value,Vec4{.5f,.5f,1,1},[&]{return ImGui::SliderFloat3("Value",recipe.value.data(),0,1,"%.3f");});});
                            else scalar("Value",recipe.value[0],channel==1||channel==4?1.f:channel==5?.5f:0.f,0,1);
                        } else if(recipe.mode==BlendChannelMode::Texture) {
                            control("Assign map",[&]{return drawTextureAssetField(assets,"Texture",recipe.image.id,message_,channel==3);});
                            if(!colorChannel(channel)&&channel!=3)control("Texture channel",[&]{int component=static_cast<int>(recipe.component);const bool changed=ImGui::Combo("Channel",&component,"R\0G\0B\0A\0Luminance\0");if(changed)recipe.component=static_cast<BlendTextureComponent>(component);return changed;});
                        } else {
                            control("Generate from",[&]{bool changed=false;const auto source=static_cast<std::size_t>(recipe.generatedFrom);if(ImGui::BeginCombo("From",channelNames[source])){for(std::size_t input=0;input<channelNames.size();++input)if(input!=channel&&input!=3&&input!=6)if(ImGui::Selectable(channelNames[input],input==source)){recipe.generatedFrom=static_cast<BlendMaterialChannel>(input);changed=true;}ImGui::EndCombo();}return changed;});
                            if(channel==2) {
                                color("Metal color",recipe.colorKey,{.5f,.5f,.5f});
                                scalar("Tolerance",recipe.tolerance,.1f,0,1);scalar("Softness",recipe.softness,.1f,0,1);
                                ImGui::SetItemTooltip("Creates an editable mask from color or grayscale; it does not identify physical metal.");
                            } else {scalar("Detail",recipe.detail,1,0,2);scalar("Smoothness",recipe.smoothness,0,0,32);}
                        }
                        if(channel!=3)control("Invert map",[&]{return resettableProperty(recipe.invert,false,[&]{return ImGui::Checkbox("Invert",&recipe.invert);});});
                    } else ImGui::TextDisabled("Inherited from %s",layer.material.empty()?"default surface":assets.displayName(layer.material.id).c_str());
                    auto& adjustments=layer.adjustments;
                    if(channel==0) {color("Tint",layer.appearance.tint,{1,1,1},1000000.f);scalar("Brightness",adjustments.colorBrightness,1,0,4);scalar("Contrast",adjustments.colorContrast,1,0,4);scalar("Saturation",adjustments.colorSaturation,1,0,2);}
                    if(channel==1) {scalar("Brightness",layer.appearance.roughnessScale,1,0,4);scalar("Contrast",adjustments.roughnessContrast,1,0,4);}
                    if(channel==2) {scalar("Brightness",adjustments.metallicBrightness,1,0,2);scalar("Contrast",adjustments.metallicContrast,1,0,4);}
                    if(channel==3) {scalar("Strength",adjustments.normalStrength,1,0,4);control("Flip normal Y",[&]{return resettableProperty(adjustments.normalFlipY,false,[&]{return ImGui::Checkbox("Flip Y",&adjustments.normalFlipY);});});}
                    if(channel==4)scalar("Strength",adjustments.occlusionStrength,1,0,1);
                    if(channel==5) {scalar("Scale",layer.appearance.heightScale,1,-16,16);scalar("Midpoint",layer.appearance.heightMidpoint,.5f,0,1);}
                    if(channel==6)scalar("Intensity",adjustments.emissionIntensity,1,0,64);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            if(index&&ImGui::TreeNodeEx("Transition",ImGuiTreeNodeFlags_DefaultOpen|ImGuiTreeNodeFlags_SpanAvailWidth)) {
                control("Transition",[&]{int mode=layer.appearance.heightBlend?1:0;const bool changed=ImGui::Combo("Mode",&mode,"Smooth\0Height\0");if(changed)layer.appearance.heightBlend=mode==1;return changed;});
                if(layer.appearance.heightBlend){scalar("Height Influence",layer.appearance.heightInfluence,1,0,1);scalar("Softness",layer.appearance.blendSoftness,.2f,.001f,1);}
                ImGui::TreePop();
            }
            if(ImGui::TreeNodeEx("Channel influences",ImGuiTreeNodeFlags_SpanAvailWidth)) {
                scalar("Color",layer.appearance.colorInfluence,1,0,1);scalar("Roughness",layer.appearance.roughnessInfluence,1,0,1);
                scalar("Metallic",layer.appearance.metallicInfluence,1,0,1);scalar("Normal",layer.appearance.normalInfluence,1,0,1);scalar("AO",layer.appearance.occlusionInfluence,1,0,1);
                ImGui::TreePop();
            }
            ImGui::EndDisabled();ImGui::PopID();ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }
    if(propertyEditing_&&!ImGui::IsAnyItemActive()&&!ImGui::IsPopupOpen(nullptr,ImGuiPopupFlags_AnyPopup))finish();
    const auto diagnostic=assets.materialDiagnostic(document_.assetId());
    if(!diagnostic.empty())ImGui::TextWrapped("%s",diagnostic.c_str());
    if(!document_.status().empty())ImGui::TextWrapped("%s",document_.status().c_str());
    if(!message_.empty())ImGui::TextWrapped("%s",message_.c_str());
    ImGui::EndChild();ImGui::EndTable();
}
}
