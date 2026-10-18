#include "EditorPreferences.h"
#include "assets/AtomicFile.h"
#include "math/Json.h"
#include "resources/Paths.h"
#include <SDL3/SDL.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
std::filesystem::path preferenceFile() {
    char* directory=SDL_GetPrefPath("CrimsonEngine","Editor");
    if (!directory) throw std::runtime_error("Cannot locate editor preferences: "+std::string(SDL_GetError()));
    const std::filesystem::path result=pathFromUtf8(directory)/"preferences.json";
    SDL_free(directory); return result;
}
bool same(const EditorPreferenceValues& a,const EditorPreferenceValues& b) {
    return a.appearance.backgroundSrgb==b.appearance.backgroundSrgb &&
        a.appearance.gridMinorSrgb==b.appearance.gridMinorSrgb && a.appearance.gridMajorSrgb==b.appearance.gridMajorSrgb &&
        a.appearance.gridMinorWidth==b.appearance.gridMinorWidth && a.appearance.gridMajorWidth==b.appearance.gridMajorWidth &&
        a.browserPreviewSize==b.browserPreviewSize && a.pointShadowResolution==b.pointShadowResolution &&
        a.graphicsBackend==b.graphicsBackend;
}
bool valid(const EditorPreferenceValues& value) {
    const auto color=[](const auto& channels) {
        return math::finite(channels) && std::all_of(channels.begin(),channels.end(),[](float c){return c>=0 && c<=1;});
    };
    return color(value.appearance.backgroundSrgb) && color(value.appearance.gridMinorSrgb) && color(value.appearance.gridMajorSrgb) &&
        std::isfinite(value.appearance.gridMinorWidth) && value.appearance.gridMinorWidth>=.25f && value.appearance.gridMinorWidth<=8 &&
        std::isfinite(value.appearance.gridMajorWidth) && value.appearance.gridMajorWidth>=.25f && value.appearance.gridMajorWidth<=8 &&
        std::find(BrowserPreviewSizes.begin(),BrowserPreviewSizes.end(),value.browserPreviewSize)!=BrowserPreviewSizes.end() &&
        std::find(PointShadowResolutions.begin(),PointShadowResolutions.end(),value.pointShadowResolution)!=PointShadowResolutions.end() &&
        validGraphicsBackend(value.graphicsBackend);
}
}
EditorPreferences::EditorPreferences(std::filesystem::path file) {
    try {
        file_=file.empty() ? preferenceFile() : std::move(file);
        if (!std::filesystem::exists(file_)) return;
        std::ifstream input(file_); const auto json=nlohmann::json::parse(input);
        if (json.at("version")!=1) throw std::runtime_error("Unsupported preferences version.");
        EditorPreferenceValues candidate;
        const auto& appearance=json.at("viewport");
        candidate.appearance.backgroundSrgb=appearance.at("background").get<Vec3>();
        candidate.appearance.gridMinorSrgb=appearance.at("gridMinorColor").get<Vec4>();
        candidate.appearance.gridMajorSrgb=appearance.at("gridMajorColor").get<Vec4>();
        candidate.appearance.gridMinorWidth=appearance.at("gridMinorWidth").get<float>();
        candidate.appearance.gridMajorWidth=appearance.at("gridMajorWidth").get<float>();
        candidate.browserPreviewSize=json.value("browserPreviewSize",128);
        candidate.pointShadowResolution=json.value("pointShadowResolution",1024);
        if(const auto found=json.find("graphicsBackend");found!=json.end()) {
            const auto parsed=parseGraphicsBackend(found->get<std::string>());
            if(!parsed)throw std::runtime_error("Preferences contain an unsupported graphics backend.");
            candidate.graphicsBackend=*parsed;
        }
        if (!valid(candidate)) throw std::runtime_error("Preferences contain invalid values.");
        values_=candidate;
    } catch (const std::exception& error) { status_=error.what(); }
}
bool EditorPreferences::setValues(const EditorPreferenceValues& value) {
    if (!valid(value)) { status_="Preferences contain invalid values."; return false; }
    if (same(values_,value)) return false;
    values_=value; return save();
}
int EditorPreferences::previewSizeIndex(int size) {
    const auto found=std::min_element(BrowserPreviewSizes.begin(),BrowserPreviewSizes.end(),[size](int a,int b){return std::abs(a-size)<std::abs(b-size);});
    return static_cast<int>(found-BrowserPreviewSizes.begin());
}
bool EditorPreferences::setBrowserPreviewSize(int size) {
    auto candidate=values_; candidate.browserPreviewSize=BrowserPreviewSizes[static_cast<size_t>(previewSizeIndex(size))];
    return setValues(candidate);
}
bool EditorPreferences::save() {
    try {
        const auto& appearance=values_.appearance;
        const nlohmann::json json{{"version",1},{"browserPreviewSize",values_.browserPreviewSize},{"pointShadowResolution",values_.pointShadowResolution},
            {"graphicsBackend",graphicsBackendKey(values_.graphicsBackend)},
            {"viewport",{{"background",appearance.backgroundSrgb},{"gridMinorColor",appearance.gridMinorSrgb},
                {"gridMajorColor",appearance.gridMajorSrgb},{"gridMinorWidth",appearance.gridMinorWidth},{"gridMajorWidth",appearance.gridMajorWidth}}}};
        writeFileAtomically(file_,json.dump(2)+"\n"); status_.clear(); return true;
    } catch (const std::exception& error) { status_=error.what(); return false; }
}
void EditorPreferences::draw(std::optional<GraphicsBackend> activeBackend) {
    if (!open_) return;
    ImGui::SetNextWindowSize(ImVec2(480,480),ImGuiCond_FirstUseEver);
    if (focusRequested_) { ImGui::SetNextWindowFocus(); focusRequested_=false; }
    if (ImGui::Begin("Preferences",&open_,ImGuiWindowFlags_NoDocking)) {
        auto candidate=values_; auto& appearance=candidate.appearance; bool changed=false;
        ImGui::SeparatorText("Viewport");
        changed|=ImGui::ColorEdit3("Background",appearance.backgroundSrgb.data());
        ImGui::SeparatorText("Grid");
        changed|=ImGui::ColorEdit4("Major line color",appearance.gridMajorSrgb.data());
        changed|=ImGui::DragFloat("Major line width",&appearance.gridMajorWidth,.025f,.25f,8,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        changed|=ImGui::ColorEdit4("Minor line color",appearance.gridMinorSrgb.data());
        changed|=ImGui::DragFloat("Minor line width",&appearance.gridMinorWidth,.025f,.25f,8,"%.3f",ImGuiSliderFlags_AlwaysClamp);
        ImGui::SeparatorText("Rendering");
        if(ImGui::BeginCombo("Renderer (restart required)",graphicsBackendName(candidate.graphicsBackend))) {
            for(const auto backend:{GraphicsBackend::Direct3D12,GraphicsBackend::Direct3D11}) {
                const bool selected=backend==candidate.graphicsBackend;
                if(ImGui::Selectable(graphicsBackendName(backend),selected)) {candidate.graphicsBackend=backend;changed=true;}
                if(selected)ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) {
            if(activeBackend)ImGui::SetTooltip("Active: %s. The selected renderer applies on the next launch.",graphicsBackendName(*activeBackend));
            else ImGui::SetTooltip("The selected renderer applies on the next launch.");
        }
        const auto resolutionLabel=std::to_string(candidate.pointShadowResolution);
        if (ImGui::BeginCombo("Shadow resolution",resolutionLabel.c_str())) {
            for (const auto resolution:PointShadowResolutions) {
                const bool selected=resolution==candidate.pointShadowResolution;
                const auto label=std::to_string(resolution);
                if (ImGui::Selectable(label.c_str(),selected)) { candidate.pointShadowResolution=resolution;changed=true; }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        if (changed) setValues(candidate);
        if (!status_.empty()) ImGui::TextWrapped("%s",status_.c_str());
        if (ImGui::Button("Close")) open_=false;
    }
    ImGui::End();
}
} // namespace crimson
