#include "Tools.h"
#include "AssetBrowser.h"
#include "LightingControls.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "assets/AssetLibrary.h"
#include "assets/ModelMaterialSlots.h"
#include "crimson-renderer/Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace crimson {
namespace {

constexpr const char* TexturePayload = "CC_TEXTURE_ASSET";
constexpr const char* ModelPayload = "CC_MODEL_ASSET";

ImVec2 thumbnailSize(const TextureAsset& asset, float maximum) {
    const float longest = static_cast<float>(std::max(1u, std::max(asset.width, asset.height)));
    return {maximum * static_cast<float>(asset.width) / longest, maximum * static_cast<float>(asset.height) / longest};
}

void help(const char* text) {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 23.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

void sectionTitle(const char* label) {
    ImGui::Spacing();
    ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_Text], "%s", label);
    ImGui::Separator();
    ImGui::Spacing();
}

void drawAssetLabel(const AssetLibrary& assets, const TextureAsset* asset, const char* fallback, ImVec2 minimum, ImVec2 maximum, float padding) {
    ImDrawList* draw = ImGui::GetWindowDrawList();
    float textX = minimum.x + padding;
    if (asset && bgfx::isValid(asset->thumbnail)) {
        const float extent = maximum.y - minimum.y - padding * 2.0f;
        const ImVec2 size = thumbnailSize(*asset, extent);
        const ImVec2 imageMinimum(minimum.x + padding, minimum.y + (maximum.y - minimum.y - size.y) * 0.5f);
        draw->AddImage(ImTextureRef(ImGuiBgfx::textureId(asset->thumbnail)), imageMinimum,
                      ImVec2(imageMinimum.x + size.x, imageMinimum.y + size.y));
        textX += extent + padding;
    }
    const ImVec2 textMinimum(textX, minimum.y + (maximum.y - minimum.y - ImGui::GetFontSize()) * 0.5f);
    draw->PushClipRect(ImVec2(textX, minimum.y), ImVec2(maximum.x - padding, maximum.y), true);
    const auto name = !asset ? std::string(fallback) : asset->id==GrayTextureId ? std::string("Default gray") :
        asset->id==NeutralNormalTextureId ? std::string("Neutral normal") : assets.displayName(asset->id);
    draw->AddText(textMinimum, ImGui::GetColorU32(asset ? ImGuiCol_Text : ImGuiCol_TextDisabled), name.c_str());
    draw->PopClipRect();
}

template<typename Assign, typename Clear>
bool assetSlot(AssetLibrary& assets, const char* title, const char* interpretation, const std::string& assignedId,
               ImGuiTextFilter& filter, bool allowHdr, const char* emptyLabel, Assign assign, Clear clear, bool normal = false) {
    bool changed = false, openPicker = false;
    ImGui::PushID(title);
    ImGui::BeginGroup();
    const float height = ImGui::GetFrameHeight();
    const float gap = ImGui::GetStyle().ItemInnerSpacing.x;
    const float fieldWidth = std::max(height, ImGui::GetContentRegionAvail().x - height * 2.0f - gap * 2.0f);
    openPicker |= ImGui::InvisibleButton("Object field", ImVec2(fieldWidth, height));
    const ImVec2 minimum = ImGui::GetItemRectMin(), maximum = ImGui::GetItemRectMax();
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(minimum, maximum, ImGui::GetColorU32(ImGui::IsItemHovered() ? ImGuiCol_FrameBgHovered : ImGuiCol_FrameBg),
                        ImGui::GetStyle().FrameRounding);
    draw->AddRect(minimum, maximum, ImGui::GetColorU32(ImGuiCol_Border), ImGui::GetStyle().FrameRounding);
    const auto displayedId=allowHdr ? assets.references().resolveEnvironment(assignedId) : assets.references().resolveTexture(assignedId,normal);
    const char* fallback=displayedId.empty() ? emptyLabel : displayedId==GrayTextureId ? "Default gray" :
        displayedId==NeutralNormalTextureId ? "Neutral normal" : "Missing (Texture)";
    drawAssetLabel(assets, assets.find(displayedId), fallback, minimum, maximum, 3.0f);
    help(interpretation);
    ImGui::SameLine(0.0f, gap);
    openPicker |= ImGui::Button("##picker", ImVec2(height, height));
    const ImVec2 picker = ImGui::GetItemRectMin();
    const ImVec2 center(picker.x + height * 0.5f, picker.y + height * 0.5f);
    draw->AddCircle(center, height * 0.17f, ImGui::GetColorU32(ImGuiCol_Text), 12, 1.0f);
    draw->AddCircleFilled(center, height * 0.05f, ImGui::GetColorU32(ImGuiCol_Text));
    help("Choose a texture");
    ImGui::SameLine(0.0f, gap);
    ImGui::BeginDisabled(assignedId.empty());
    if (ImGui::Button("x##clear", ImVec2(height, height))) { clear(); changed = true; }
    help(allowHdr ? "Use the built-in studio environment" : "Clear texture");
    ImGui::EndDisabled();
    ImGui::EndGroup();
    // Dragging onto any part of the object field uses the same assignment path.
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(TexturePayload)) {
            if (payload->DataSize > 0) {
                const char* id = static_cast<const char*>(payload->Data);
                const auto length = static_cast<size_t>(payload->DataSize);
                if (id[length - 1] == '\0') changed |= assign(std::string(id, length - 1));
            }
        }
        ImGui::EndDragDropTarget();
    }
    if (openPicker) { filter.Clear(); ImGui::OpenPopup("Texture picker"); }
    ImGui::SetNextWindowSize(ImVec2(std::max(260.0f, fieldWidth + height * 2.0f + gap * 2.0f), 0), ImGuiCond_Appearing);
    if (ImGui::BeginPopup("Texture picker")) {
        ImGui::TextUnformatted(title);
        if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputTextWithHint("##search", "Search textures...", filter.InputBuf, IM_ARRAYSIZE(filter.InputBuf))) filter.Build();
        ImGui::BeginChild("Choices", ImVec2(0, ImGui::GetFontSize() * 14.0f), ImGuiChildFlags_None);
        if (ImGui::Selectable(emptyLabel, displayedId.empty())) {
            if (!assignedId.empty()) { clear(); changed = true; }
            ImGui::CloseCurrentPopup();
        }
        for (const TextureAsset& asset : assets.assets()) {
            if ((!allowHdr && asset.hdr) || !filter.PassFilter(assets.displayName(asset.id).c_str())) continue;
            ImGui::PushID(asset.id.c_str());
            if (ImGui::Selectable("##texture", asset.id == displayedId, ImGuiSelectableFlags_None, ImVec2(0, height))) {
                changed |= assign(asset.id);
                ImGui::CloseCurrentPopup();
            }
            drawAssetLabel(assets, &asset, "", ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 3.0f);
            ImGui::PopID();
        }
        if (assets.assets().empty()) ImGui::TextDisabled("No textures");
        ImGui::EndChild();
        ImGui::EndPopup();
    }
    ImGui::PopID();
    return changed;
}

bool textureSlot(Material& material, AssetLibrary& assets, MaterialSlot slot, const char* title, const char* interpretation,
                 const std::string& assignedId) {
    static ImGuiTextFilter filters[4];
    return assetSlot(assets, title, interpretation, assignedId, filters[static_cast<size_t>(slot)], false, "None (Texture)",
        [&](const std::string& id) { return assets.assign(material, slot, id); }, [&] { assets.clear(material, slot); },slot==MaterialSlot::Normal);
}

bool materialControls(Material& material, AssetLibrary& assets) {
    bool changed = false;
    sectionTitle("MATERIAL");
    ImGui::TextUnformatted(material.albedoMap.empty() ? "Albedo color" : "Albedo tint");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::ColorEdit3("##baseColor", material.baseColor.data(), ImGuiColorEditFlags_Float);
    help("Editor color is sRGB. An assigned albedo texture is multiplied by this tint in linear space.");
    changed |= textureSlot(material, assets, MaterialSlot::Albedo, "Albedo texture", "RGB / sRGB", material.albedoMap);
    ImGui::Spacing();
    ImGui::TextUnformatted(material.roughnessMap.empty() ? "Roughness" : "Roughness factor");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::SliderFloat("##roughness", &material.roughness, material.roughnessMap.empty() ? 0.045f : 0.0f, 1.0f, "%.3f");
    help("Lower roughness gives narrower reflections. With a map assigned, this is its multiplier.");
    changed |= textureSlot(material, assets, MaterialSlot::Roughness, "Roughness texture", "R channel / linear", material.roughnessMap);
    ImGui::Spacing();
    ImGui::TextUnformatted(material.metallicMap.empty() ? "Metalness" : "Metalness factor");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::SliderFloat("##metallic", &material.metallic, 0.0f, 1.0f, "%.2f");
    help("0 is a dielectric; 1 is a metal. With a map assigned, this is its multiplier.");
    changed |= textureSlot(material, assets, MaterialSlot::Metallic, "Metalness texture", "R channel / linear", material.metallicMap);
    ImGui::Spacing();
    ImGui::TextUnformatted("Normal strength");
    ImGui::SetNextItemWidth(-1);
    changed |= ImGui::SliderFloat("##normalStrength", &material.normalStrength, 0.0f, 2.0f, "%.2f");
    help("Tangent-space normals use +Y (OpenGL) green. Zero strength restores the smooth surface normal.");
    changed |= textureSlot(material, assets, MaterialSlot::Normal, "Normal texture", "RGB / linear tangent-space normal (+Y)", material.normalMap);
    ImGui::Spacing();
    if (ImGui::Button("Reset material", ImVec2(-1, 0))) { material = Material{}; changed = true; }
    return changed;
}

bool environmentControls(EnvironmentSettings& environment, AssetLibrary& assets, const Renderer& renderer, bool persistLegacy = true) {
    bool changed = false;
    static ImGuiTextFilter filter;
    sectionTitle("ENVIRONMENT");
    changed |= ImGui::Checkbox("Environment lighting##environment", &environment.enabled);
    ImGui::TextUnformatted("Environment image");
    changed |= assetSlot(assets, "Environment image", "Equirectangular panorama. HDR is linear radiance; PNG/JPEG/TGA/BMP are sRGB.",
        environment.map, filter, true, "Built-in studio",
        [&](const std::string& id) { return assets.assignEnvironment(environment, id, persistLegacy); }, [&] { assets.clearEnvironment(environment,persistLegacy); });
    changed |= ImGui::SliderFloat("Intensity##environment", &environment.intensity, 0.0f, 10.0f, "%.2f");
    changed |= ImGui::SliderFloat("Rotation##environment", &environment.rotation, -180.0f, 180.0f, "%.1f deg");
    help("Rotate environment lighting around the world Y axis.");
    if(ImGui::CollapsingHeader("Ambient Light"))
        drawAmbientLightControls(environment.ambientLight,[&](AmbientLightControl,bool edited){changed|=edited;});
    changed |= drawToneMappingControl(environment.toneMapping);
    changed |= drawToneMappingParameterControls(environment.toneMapping,environment.toneMappingParameters);
    changed |= drawGlowControls(environment.glow);
    if (!renderer.environmentStatus().empty()) ImGui::TextWrapped("%s", renderer.environmentStatus().c_str());
    if (!renderer.glowStatus().empty()) ImGui::TextWrapped("%s", renderer.glowStatus().c_str());
    ImGui::Spacing();
    if (ImGui::Button("Reset environment", ImVec2(-1, 0))) { environment = EnvironmentSettings{}; changed = true; }
    return changed;
}

void lightControls(SceneSettings& settings) {
    sectionTitle("LIGHTING");
    if (ImGui::CollapsingHeader("Directional light", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Enabled##directional", &settings.directional.enabled);
        ImGui::ColorEdit3("Color##directional", settings.directional.color.data(), ImGuiColorEditFlags_Float);
        ImGui::DragFloat3("Direction", settings.directional.direction.data(), 0.02f, -1.0f, 1.0f, "%.2f");
        help("Direction in which the light travels. The renderer normalizes this vector.");
        ImGui::SliderFloat("Intensity##directional", &settings.directional.intensity, 0.0f, 12.0f, "%.2f");
    }
    for (size_t index = 0; index < settings.points.size(); ++index) {
        ImGui::PushID(static_cast<int>(index));
        const char* title = index == 0 ? "Point light 1 - warm" : "Point light 2 - cool";
        if (ImGui::CollapsingHeader(title, ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enabled", &settings.points[index].enabled);
            ImGui::ColorEdit3("Color", settings.points[index].color.data(), ImGuiColorEditFlags_Float);
            ImGui::DragFloat3("Position", settings.points[index].position.data(), 0.025f, -8.0f, 8.0f, "%.2f");
            ImGui::SliderFloat("Intensity", &settings.points[index].intensity, 0.0f, 200.0f, "%.1f", ImGuiSliderFlags_Logarithmic);
            help("Point-light intensity uses inverse-square distance attenuation.");
        }
        ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("Reset lights", ImVec2(-1, 0))) {
        const SceneSettings defaults;
        settings.directional = defaults.directional;
        settings.points = defaults.points;
    }
}

void viewControls(SceneSettings& settings, EditorFrame& frame, bool hasModel) {
    sectionTitle("VIEW");
    ImGui::SliderFloat("Exposure", &settings.exposure, 0.05f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    help("Exposure affects the HDR scene before tone mapping. The editor stays at normal brightness.");
    ImGui::SliderFloat("Field of view", &settings.camera.fov, 25.0f, 90.0f, "%.1f deg");
    ImGui::Spacing();
    if (ImGui::Button("Reset camera", ImVec2(-1, 0))) {
        settings.camera = Camera{};
        frame.frameModel = hasModel;
    }
}

void modelControls(const ModelAsset& model, EditorFrame& frame) {
    sectionTitle("MODEL PREVIEW");
    ImGui::TextWrapped("%s", model.name.c_str());
    if (ImGui::Button("Show default sphere", ImVec2(-1, 0))) frame.showSphere = true;
    if (!model.warnings.empty()) {
        sectionTitle("IMPORT WARNINGS");
        for (const auto& warning : model.warnings) ImGui::TextWrapped("%s", warning.c_str());
    }
    sectionTitle("MATERIALS");
    for (const auto index:usedModelMaterialSlots(model)) {
        ImGui::PushID(static_cast<int>(index));
        const auto& material = model.materials[index];
        const auto title = material.name.empty() ? "Material " + std::to_string(index + 1) : material.name;
        if (ImGui::TreeNode(title.c_str())) {
            ImGui::Text("Base color: %.3f, %.3f, %.3f", material.baseColor[0], material.baseColor[1], material.baseColor[2]);
            ImGui::Text("Alpha: %.3f / %s", material.baseColor[3], material.alphaMask ? "Mask" : "Opaque");
            ImGui::Text("Metallic: %.3f / Roughness: %.3f", material.metallic, material.roughness);
            ImGui::Text("Normal scale: %.3f / Occlusion: %.3f", material.normalScale, material.occlusionStrength);
            ImGui::Text("Emissive: %.3f, %.3f, %.3f", material.emissive[0], material.emissive[1], material.emissive[2]);
            ImGui::TextUnformatted(material.doubleSided ? "Double sided" : "Single sided");
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

void seedLayout(ImGuiID dockId, const ImGuiViewport* viewport) {
    ImGui::DockBuilderRemoveNode(dockId);
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodePos(dockId, viewport->WorkPos);
    ImGui::DockBuilderSetNodeSize(dockId, viewport->WorkSize);
    ImGuiID upper = dockId, browser{}, settings{}, scene{};
    ImGui::DockBuilderSplitNode(upper, ImGuiDir_Down, 0.25f, &browser, &upper);
    ImGui::DockBuilderSplitNode(upper, ImGuiDir_Right, 0.25f, &settings, &scene);
    ImGui::DockBuilderDockWindow("Viewport", scene);
    ImGui::DockBuilderDockWindow("Settings", settings);
    ImGui::DockBuilderDockWindow("Asset Browser", browser);
    ImGui::DockBuilderFinish(dockId);
}


} // namespace

void applyToolsStyle(float) {
    ImGui::GetStyle() = ImGuiStyle{};
    ImGui::StyleColorsDark();
}

BrowserActions drawAssetBrowser(AssetLibrary& assets, SDL_Window* window) {
    static AssetBrowserPanel diagnosticBrowser;
    return diagnosticBrowser.draw(assets, window);
}

bool drawEnvironmentControls(EnvironmentSettings& environment, AssetLibrary& assets, const Renderer& renderer, bool persistLegacy) {
    return environmentControls(environment, assets, renderer,persistLegacy);
}
bool drawEnvironmentImageField(EnvironmentSettings& environment, AssetLibrary& assets) {
    static ImGuiTextFilter filter;
    return assetSlot(assets,"HDRI","Equirectangular HDR or color panorama.",environment.map,filter,true,"Built-in studio",
        [&](const std::string& id){return assets.assignEnvironment(environment,id,false);},[&]{assets.clearEnvironment(environment,false);});
}
bool drawToneMappingControl(ToneMapping& mode) {
    int selected=static_cast<int>(mode);
    const char* names[]{"Reinhard (legacy)","Linear","ACES","AgX"};
    const bool changed=ImGui::Combo("Tone mapping",&selected,names,4);
    if(changed)mode=static_cast<ToneMapping>(selected);
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
        ImGui::SetTooltip("Maps scene brightness to the display after exposure.");
    return changed;
}

bool drawToneMappingParameterControls(ToneMapping mode,ToneMappingParameters& parameters,const ToneMappingParameterEdit& edit) {
    bool changed=false;
    const auto field=[&](ToneMappingParameterControl control,bool updated) {
        changed|=updated;
        if(edit)edit(control,updated);
    };
    ImGui::PushID("ToneMappingParameters");
    ImGui::PushID(static_cast<int>(mode));
    if(mode==ToneMapping::Reinhard) {
        field(ToneMappingParameterControl::ReinhardWhite,
            ImGui::DragFloat("White Point",&parameters.reinhardWhite,.05f,0,64,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Scene-linear brightness mapped to white. 0 keeps the original unlimited Reinhard curve.");
    } else if(mode==ToneMapping::ACES) {
        field(ToneMappingParameterControl::AcesWhite,
            ImGui::DragFloat("White Point",&parameters.acesWhite,.05f,1,64,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Highlight white-point scale. Higher values retain more bright detail.");
    } else if(mode==ToneMapping::AgX) {
        field(ToneMappingParameterControl::AgxWhite,
            ImGui::DragFloat("White Point",&parameters.agxWhite,.05f,2,64,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("Scene-linear highlight limit of the AgX curve.");
        field(ToneMappingParameterControl::AgxContrast,
            ImGui::SliderFloat("Contrast",&parameters.agxContrast,1,4,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip))ImGui::SetTooltip("AgX contrast around middle gray. Higher values deepen shadows and strengthen highlights.");
    }
    ImGui::PopID();
    ImGui::PopID();
    return changed;
}

bool drawSsrControls(SsrSettings& reflection,const SsrControlEdit& edit) {
    bool changed=false;
    const auto field=[&](SsrControl control,bool updated){changed|=updated;if(edit)edit(control,updated);};
    ImGui::PushID("SSR");
    if(ImGui::CollapsingHeader("Reflections")) {
        field(SsrControl::Enabled,ImGui::Checkbox("SSR",&reflection.enabled));
        ImGui::BeginDisabled(!reflection.enabled);
        field(SsrControl::Strength,ImGui::SliderFloat("Strength",&reflection.strength,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        int quality=static_cast<int>(reflection.quality);const char* qualities[]{"Low","Medium","High"};
        const bool qualityChanged=ImGui::Combo("Quality",&quality,qualities,3);
        if(qualityChanged)reflection.quality=static_cast<SsrQuality>(quality);
        field(SsrControl::Quality,qualityChanged);
        field(SsrControl::Distance,ImGui::DragFloat("Max Distance (m)",&reflection.maxDistance,.1f,.001f,10000,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Maximum reflected ray distance. Only visible opaque surfaces can supply reflections.");
        field(SsrControl::Roughness,ImGui::SliderFloat("Max Roughness",&reflection.maxRoughness,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        field(SsrControl::Tolerance,ImGui::DragFloat("Depth Tolerance (m)",&reflection.depthTolerance,.001f,.000001f,10,"%.4f",ImGuiSliderFlags_AlwaysClamp));
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_ForTooltip|ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("Depth agreement required at a reflected hit. Larger values can leak through thin geometry.");
        field(SsrControl::Bias,ImGui::DragFloat("Ray Bias (m)",&reflection.bias,.0001f,0,1,"%.4f",ImGuiSliderFlags_AlwaysClamp));
        ImGui::EndDisabled();
    }
    ImGui::PopID();return changed;
}

bool drawGlowControls(GlowSettings& glow, const GlowControlEdit& edit) {
    bool changed=false;
    const auto field=[&](GlowControl control,bool updated) {
        changed|=updated;
        if(edit)edit(control,updated);
    };
    ImGui::PushID("Glow");
    if (ImGui::CollapsingHeader("Glow",ImGuiTreeNodeFlags_DefaultOpen)) {
        field(GlowControl::Enabled,ImGui::Checkbox("Enabled##glow",&glow.enabled));
        help("Scatters visible HDR highlights, including the sun, reflections and emission.");
        ImGui::BeginDisabled(!glow.enabled);
        field(GlowControl::Strength,ImGui::SliderFloat("Intensity",&glow.strength,0,10,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        help("Overall halo intensity. Zero disables Glow in every blend mode.");
        int blendMode=static_cast<int>(glow.blendMode);const char* blendModes[]{"Additive","Screen","Soft Light","Replace","Mix"};
        const bool modeChanged=ImGui::Combo("Blend Mode",&blendMode,blendModes,5);
        if(modeChanged)glow.blendMode=static_cast<GlowBlendMode>(blendMode);
        field(GlowControl::BlendMode,modeChanged);
        constexpr std::array<const char*,5> blendHelp{"Adds the halo to scene lighting.","Brightens with a soft screen blend.",
            "Uses the halo to shape image contrast.","Shows the halo in place of scene lighting.","Crossfades scene lighting and the halo."};
        help(blendHelp[static_cast<std::size_t>(glow.blendMode)]);
        if(glow.blendMode==GlowBlendMode::Mix) {
            field(GlowControl::Mix,ImGui::SliderFloat("Mix",&glow.mix,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            help("Zero keeps the scene; one shows the halo.");
        }
        field(GlowControl::Size,ImGui::SliderFloat("Spread",&glow.radius,0,8,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        help("Screen-space halo spread. Zero disables the halo.");
        field(GlowControl::Threshold,ImGui::DragFloat("Highlight Threshold",&glow.threshold,.02f,0,100000,"%.2f",ImGuiSliderFlags_AlwaysClamp));
        help("Scene brightness after exposure that starts contributing to Glow.");
        field(GlowControl::Tint,ImGui::ColorEdit3("Tint",glow.tint.data(),ImGuiColorEditFlags_Float));
        help("White preserves the highlight color.");
        if(ImGui::TreeNodeEx("Levels",ImGuiTreeNodeFlags_DefaultOpen)) {
            constexpr std::array<const char*,GlowLevelCount> labels{"Level 1","Level 2","Level 3","Level 4","Level 5","Level 6","Level 7"};
            for(std::size_t level=0;level<glow.levels.size();++level) {
                const auto control=static_cast<GlowControl>(static_cast<int>(GlowControl::Level1)+static_cast<int>(level));
                field(control,ImGui::SliderFloat(labels[level],&glow.levels[level],0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp));
                help("Independent contribution: Level 1 is tightest and Level 7 widest. Zero removes this level; all zero disables Glow.");
            }
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Advanced")) {
            field(GlowControl::Saturation,ImGui::SliderFloat("Saturation",&glow.saturation,0,2,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            field(GlowControl::SoftKnee,ImGui::SliderFloat("Threshold Softness",&glow.softKnee,0,1,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            field(GlowControl::Anamorphic,ImGui::SliderFloat("Anamorphic Stretch",&glow.anamorphic,-1,1,"%.2f",ImGuiSliderFlags_AlwaysClamp));
            help("Negative stretches vertically; positive stretches horizontally.");
            int quality=static_cast<int>(glow.quality);const char* qualities[]{"Low","High"};
            const bool qualityChanged=ImGui::Combo("Quality",&quality,qualities,2);
            if(qualityChanged)glow.quality=static_cast<GlowQuality>(quality);
            field(GlowControl::Quality,qualityChanged);
            help("High improves stability around small, bright highlights.");
            ImGui::TreePop();
        }
        ImGui::EndDisabled();
    }
    ImGui::PopID();
    return changed;
}

EditorFrame drawEditor(SceneSettings& settings, Renderer& renderer, AssetLibrary& assets, SDL_Window* window,
                       const ModelInstance* model, const std::string& previewStatus) {
    EditorFrame frame;
    bool resetLayout = false;
    if (ImGui::BeginMainMenuBar()) {
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_Text], "CRIMSON ENGINE");
        if (ImGui::BeginMenu("Layout")) {
            resetLayout = ImGui::MenuItem("Reset layout");
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
    const ImGuiViewport* main = ImGui::GetMainViewport();
    const ImGuiID dockId = ImGui::GetID("CrimsonDockSpace");
    if (resetLayout || !ImGui::DockBuilderGetNode(dockId)) seedLayout(dockId, main);
    ImGui::DockSpaceOverViewport(dockId, main);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        const ImVec2 available = ImGui::GetContentRegionAvail();
        if (available.x >= 1.0f && available.y >= 1.0f) {
            const auto* host=ImGui::GetWindowViewport();
            const ImVec2 scale = host->FramebufferScale;
            const auto width = static_cast<uint32_t>(std::max(1.0f, std::round(available.x * scale.x)));
            const auto height = static_cast<uint32_t>(std::max(1.0f, std::round(available.y * scale.y)));
            renderer.resizeViewport(width, height);
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const auto texture = renderer.viewportTexture();
            ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)), available);
            frame.viewport = {position.x, position.y, available.x, available.y, ImGui::IsItemHovered(),
                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) || ImGui::GetDragDropPayload() != nullptr};
            frame.viewport.windowId=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(host->PlatformHandle));
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(ModelPayload)) {
                    if (payload->DataSize > 0) {
                        const auto* id = static_cast<const char*>(payload->Data);
                        const auto size = static_cast<size_t>(payload->DataSize);
                        if (id[size - 1] == '\0') frame.modelDropId.assign(id, size - 1);
                    }
                }
                ImGui::EndDragDropTarget();
            }
            if (frame.viewport.hovered && !frame.viewport.inputBlocked && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) ImGui::SetWindowFocus();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (ImGui::Begin("Settings")) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("%.0f FPS / %.2f ms", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
        ImGui::TextColored(ImGui::GetStyle().Colors[ImGuiCol_TextDisabled], "%s", renderer.backendName());
        if (!previewStatus.empty()) ImGui::TextWrapped("%s", previewStatus.c_str());
        if (model && model->asset && !renderer.modelStatus().empty() && renderer.modelStatus() != previewStatus)
            ImGui::TextWrapped("%s", renderer.modelStatus().c_str());
        ImGui::Separator();
        ImGui::BeginChild("Properties", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None);
        ImGui::PushItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.58f));
        if (ImGui::BeginTabBar("Render settings")) {
            if (ImGui::BeginTabItem("Material")) {
                if (model && model->asset) modelControls(*model->asset, frame);
                else frame.materialChanged |= materialControls(settings.material, assets);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Lights")) {
                frame.environmentChanged |= environmentControls(settings.environment, assets, renderer);
                lightControls(settings);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("View")) { viewControls(settings, frame, model && model->asset); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();
        if (ImGui::Button("Reset scene", ImVec2(-1, 0))) {
            settings = SceneSettings{};
            frame.materialChanged = true;
            frame.environmentChanged = true;
            frame.showSphere = true;
        }
    }
    ImGui::End();
    drawAssetBrowser(assets, window);
    // A popup/asset drag may have been opened by a later panel this same frame.
    frame.viewport.inputBlocked |= ImGui::GetIO().WantTextInput ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) || ImGui::GetDragDropPayload() != nullptr;
    return frame;
}

} // namespace crimson
