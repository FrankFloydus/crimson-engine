#include "Tools.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "assets/AssetLibrary.h"
#include "crimson-renderer/Renderer.h"

#include <imgui.h>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>
#include <string>

namespace crimson {
namespace {

constexpr ImVec4 Bone{0.91f, 0.88f, 0.83f, 1.0f};
constexpr ImVec4 Muted{0.48f, 0.50f, 0.53f, 1.0f};
constexpr ImVec4 Accent{0.81f, 0.40f, 0.34f, 1.0f};
constexpr const char* TexturePayload = "CC_TEXTURE_ASSET";

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
    ImGui::TextColored(Accent, "%s", label);
    ImGui::Separator();
    ImGui::Spacing();
}

void drawAssetLabel(const TextureAsset* asset, const char* fallback, ImVec2 minimum, ImVec2 maximum, float padding) {
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
    draw->AddText(textMinimum, ImGui::GetColorU32(asset ? ImGuiCol_Text : ImGuiCol_TextDisabled), asset ? asset->name.c_str() : fallback);
    draw->PopClipRect();
}

template<typename Assign, typename Clear>
bool assetSlot(AssetLibrary& assets, const char* title, const char* interpretation, const std::string& assignedId,
               ImGuiTextFilter& filter, bool allowHdr, const char* emptyLabel, Assign assign, Clear clear) {
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
    drawAssetLabel(assets.find(assignedId), assignedId.empty() ? emptyLabel : "Missing (Texture)", minimum, maximum, 3.0f);
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
        if (ImGui::Selectable(emptyLabel, assignedId.empty())) {
            if (!assignedId.empty()) { clear(); changed = true; }
            ImGui::CloseCurrentPopup();
        }
        for (const TextureAsset& asset : assets.assets()) {
            if ((!allowHdr && asset.hdr) || !filter.PassFilter(asset.name.c_str())) continue;
            ImGui::PushID(asset.id.c_str());
            if (ImGui::Selectable("##texture", asset.id == assignedId, ImGuiSelectableFlags_None, ImVec2(0, height))) {
                changed |= assign(asset.id);
                ImGui::CloseCurrentPopup();
            }
            drawAssetLabel(&asset, "", ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), 3.0f);
            ImGui::PopID();
        }
        if (assets.assets().empty()) ImGui::TextDisabled("Import textures in the Asset Browser.");
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
        [&](const std::string& id) { return assets.assign(material, slot, id); }, [&] { assets.clear(material, slot); });
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

bool environmentControls(EnvironmentSettings& environment, AssetLibrary& assets, const Renderer& renderer) {
    bool changed = false;
    static ImGuiTextFilter filter;
    sectionTitle("ENVIRONMENT");
    changed |= ImGui::Checkbox("Enabled##environment", &environment.enabled);
    ImGui::TextUnformatted("Environment image");
    changed |= assetSlot(assets, "Environment image", "Equirectangular panorama. HDR is linear radiance; PNG/JPEG/TGA/BMP are sRGB.",
        environment.map, filter, true, "Built-in studio",
        [&](const std::string& id) { return assets.assignEnvironment(environment, id); }, [&] { assets.clearEnvironment(environment); });
    changed |= ImGui::SliderFloat("Intensity##environment", &environment.intensity, 0.0f, 10.0f, "%.2f");
    changed |= ImGui::SliderFloat("Rotation##environment", &environment.rotation, -180.0f, 180.0f, "%.1f deg");
    help("Rotate environment lighting around the world Y axis.");
    if (!renderer.environmentStatus().empty()) ImGui::TextWrapped("%s", renderer.environmentStatus().c_str());
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

void viewControls(SceneSettings& settings) {
    sectionTitle("VIEW");
    ImGui::SliderFloat("Exposure", &settings.exposure, 0.05f, 8.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
    help("Exposure affects the HDR scene before tone mapping. The editor stays at normal brightness.");
    ImGui::SliderFloat("Field of view", &settings.camera.fov, 25.0f, 90.0f, "%.1f deg");
    ImGui::Spacing();
    if (ImGui::Button("Reset camera", ImVec2(-1, 0))) settings.camera = Camera{};
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

void assetBrowser(AssetLibrary& assets, SDL_Window* window) {
    if (ImGui::Begin("Asset Browser")) {
        if (ImGui::Button("Import textures...")) assets.requestImport(window);
        ImGui::SameLine();
        ImGui::TextColored(Muted, "PNG / JPG / TGA / BMP / HDR");
        if (!assets.status().empty()) ImGui::TextWrapped("%s", assets.status().c_str());
        ImGui::BeginChild("Texture library", ImVec2(0, 0), ImGuiChildFlags_None);
        if (assets.assets().empty()) {
            ImGui::TextColored(Muted, "Import images, then drag a thumbnail onto a material or environment slot.");
        } else {
            const float thumbnailExtent = ImGui::GetFontSize() * 5.5f;
            const float cardWidth = thumbnailExtent + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
            const int columns = std::clamp(static_cast<int>(ImGui::GetContentRegionAvail().x / cardWidth), 1, 64);
            if (ImGui::BeginTable("Texture grid", columns, ImGuiTableFlags_SizingFixedFit)) {
                for (const TextureAsset& asset : assets.assets()) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(asset.id.c_str());
                    const TextureAsset* selected = assets.selected();
                    const bool isSelected = selected && selected->id == asset.id;
                    if (isSelected) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.38f, 0.23f, 0.21f, 1.0f));
                    const ImTextureRef texture(ImGuiBgfx::textureId(asset.thumbnail));
                    const ImVec2 previewSize = thumbnailSize(asset, thumbnailExtent);
                    if (ImGui::ImageButton("thumbnail", texture, previewSize)) assets.select(asset.id);
                    if (isSelected) ImGui::PopStyleColor();
                    if (ImGui::BeginDragDropSource()) {
                        ImGui::SetDragDropPayload(TexturePayload, asset.id.c_str(), asset.id.size() + 1);
                        ImGui::Image(texture, previewSize);
                        ImGui::TextUnformatted(asset.name.c_str());
                        ImGui::EndDragDropSource();
                    }
                    help(asset.id.c_str());
                    std::string shortName = asset.name;
                    const float labelWidth = cardWidth - ImGui::GetStyle().ItemSpacing.x;
                    if (ImGui::CalcTextSize(shortName.c_str()).x > labelWidth) {
                        while (!shortName.empty() && ImGui::CalcTextSize((shortName + "...").c_str()).x > labelWidth) {
                            size_t last = shortName.size() - 1;
                            while (last > 0 && (static_cast<unsigned char>(shortName[last]) & 0xc0) == 0x80) --last;
                            shortName.resize(last);
                        }
                        shortName += "...";
                    }
                    ImGui::TextUnformatted(shortName.c_str());
                    ImGui::TextColored(Muted, "%u x %u%s", asset.width, asset.height, asset.hdr ? " HDR" : "");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

} // namespace

void applyToolsStyle(float dpiScale) {
    dpiScale = std::clamp(dpiScale, 0.75f, 3.0f);
    ImGuiStyle& style = ImGui::GetStyle();
    style = ImGuiStyle{};
    ImGui::StyleColorsDark(&style);
    style.WindowPadding = ImVec2(18, 17);
    style.FramePadding = ImVec2(9, 6);
    style.ItemSpacing = ImVec2(9, 9);
    style.ItemInnerSpacing = ImVec2(8, 5);
    style.WindowRounding = 7.0f;
    style.ChildRounding = 4.0f;
    style.FrameRounding = 3.0f;
    style.PopupRounding = 5.0f;
    style.GrabRounding = 3.0f;
    style.ScrollbarRounding = 5.0f;
    style.WindowBorderSize = 1.0f;
    style.FrameBorderSize = 0.0f;
    style.TabRounding = 3.0f;
    style.ScrollbarSize = 12.0f;
    style.Colors[ImGuiCol_Text] = Bone;
    style.Colors[ImGuiCol_TextDisabled] = Muted;
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.075f, 0.079f, 0.090f, 0.98f);
    style.Colors[ImGuiCol_ChildBg] = ImVec4(0.075f, 0.079f, 0.090f, 0.0f);
    style.Colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.11f, 0.13f, 0.99f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.26f, 0.25f, 0.25f, 0.68f);
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.145f, 0.155f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.22f, 0.235f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.29f, 0.25f, 0.26f, 1.0f);
    style.Colors[ImGuiCol_Button] = ImVec4(0.19f, 0.20f, 0.23f, 1.0f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.25f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.43f, 0.28f, 0.26f, 1.0f);
    style.Colors[ImGuiCol_Header] = ImVec4(0.17f, 0.18f, 0.21f, 1.0f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.27f, 0.24f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.37f, 0.26f, 0.25f, 1.0f);
    style.Colors[ImGuiCol_CheckMark] = Accent;
    style.Colors[ImGuiCol_SliderGrab] = ImVec4(0.66f, 0.31f, 0.27f, 1.0f);
    style.Colors[ImGuiCol_SliderGrabActive] = Accent;
    style.Colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.15f, 0.18f, 1.0f);
    style.Colors[ImGuiCol_TabHovered] = ImVec4(0.42f, 0.28f, 0.26f, 1.0f);
    style.Colors[ImGuiCol_TabSelected] = ImVec4(0.29f, 0.22f, 0.22f, 1.0f);
    style.Colors[ImGuiCol_TabSelectedOverline] = Accent;
    style.ScaleAllSizes(dpiScale);
    style.FontScaleDpi = dpiScale;
}

EditorFrame drawEditor(SceneSettings& settings, Renderer& renderer, AssetLibrary& assets, SDL_Window* window) {
    EditorFrame frame;
    bool resetLayout = false;
    if (ImGui::BeginMainMenuBar()) {
        ImGui::TextColored(Accent, "CRIMSON ENGINE");
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
            const ImVec2 scale = ImGui::GetIO().DisplayFramebufferScale;
            const auto width = static_cast<uint32_t>(std::max(1.0f, std::round(available.x * scale.x)));
            const auto height = static_cast<uint32_t>(std::max(1.0f, std::round(available.y * scale.y)));
            renderer.resizeViewport(width, height);
            const ImVec2 position = ImGui::GetCursorScreenPos();
            const auto texture = renderer.viewportTexture();
            ImGui::Image(ImTextureRef(ImGuiBgfx::textureId(texture)), available);
            frame.viewport = {position.x, position.y, available.x, available.y, ImGui::IsItemHovered(),
                ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) || ImGui::GetDragDropPayload() != nullptr};
            if (frame.viewport.hovered && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Right) || ImGui::IsMouseClicked(ImGuiMouseButton_Middle))) ImGui::SetWindowFocus();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    if (ImGui::Begin("Settings")) {
        const ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("%.0f FPS / %.2f ms", io.Framerate, io.Framerate > 0 ? 1000.0f / io.Framerate : 0.0f);
        ImGui::TextColored(Muted, "%s", renderer.backendName());
        ImGui::Separator();
        ImGui::BeginChild("Properties", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), ImGuiChildFlags_None);
        ImGui::PushItemWidth(std::max(80.0f, ImGui::GetContentRegionAvail().x * 0.58f));
        if (ImGui::BeginTabBar("Render settings")) {
            if (ImGui::BeginTabItem("Material")) {
                frame.materialChanged |= materialControls(settings.material, assets);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Lights")) {
                frame.environmentChanged |= environmentControls(settings.environment, assets, renderer);
                lightControls(settings);
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("View")) { viewControls(settings); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        ImGui::PopItemWidth();
        ImGui::EndChild();
        if (ImGui::Button("Reset scene", ImVec2(-1, 0))) {
            settings = SceneSettings{};
            frame.materialChanged = true;
            frame.environmentChanged = true;
        }
    }
    ImGui::End();
    assetBrowser(assets, window);
    // A popup/asset drag may have been opened by a later panel this same frame.
    frame.viewport.inputBlocked |= ImGui::GetIO().WantTextInput ||
        ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup) || ImGui::GetDragDropPayload() != nullptr;
    return frame;
}

} // namespace crimson
