#include "EditorPreferences.h"
#include "assets/AtomicFile.h"
#include "math/Json.h"
#include "resources/Paths.h"
#include <Windows.h>
#include <ShlObj.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace crimson {
namespace {
std::filesystem::path preferenceFile() {
    PWSTR directory{};
    if(FAILED(SHGetKnownFolderPath(FOLDERID_RoamingAppData,0,nullptr,&directory)))
        throw std::runtime_error("Cannot locate editor preferences.");
    const std::unique_ptr<wchar_t,decltype(&CoTaskMemFree)> owned(directory,CoTaskMemFree);
    return std::filesystem::path(owned.get())/L"CrimsonEngine"/L"Editor"/L"preferences.json";
}
bool same(const EditorPreferenceValues& a,const EditorPreferenceValues& b) {
    return a.appearance.backgroundSrgb==b.appearance.backgroundSrgb &&
        a.appearance.gridMinorSrgb==b.appearance.gridMinorSrgb && a.appearance.gridMajorSrgb==b.appearance.gridMajorSrgb &&
        a.appearance.gridMinorWidth==b.appearance.gridMinorWidth && a.appearance.gridMajorWidth==b.appearance.gridMajorWidth &&
        a.browserPreviewSize==b.browserPreviewSize && a.pointShadowResolution==b.pointShadowResolution &&
        a.graphicsBackend==b.graphicsBackend && a.antiAliasing==b.antiAliasing;
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
        validGraphicsBackend(value.graphicsBackend) &&
        (value.antiAliasing==AntiAliasing::Off || value.antiAliasing==AntiAliasing::Smaa);
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
        if(const auto found=json.find("antiAliasing");found!=json.end()) {
            const auto mode=found->get<std::string>();
            if(mode=="off")candidate.antiAliasing=AntiAliasing::Off;
            else if(mode=="smaa")candidate.antiAliasing=AntiAliasing::Smaa;
            else throw std::runtime_error("Preferences contain an unsupported antialiasing mode.");
        }
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
    const auto accepted=values_;values_=value;
    if(save())return true;
    values_=accepted;return false;
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
            {"antiAliasing",values_.antiAliasing==AntiAliasing::Smaa?"smaa":"off"},
            {"viewport",{{"background",appearance.backgroundSrgb},{"gridMinorColor",appearance.gridMinorSrgb},
                {"gridMajorColor",appearance.gridMajorSrgb},{"gridMinorWidth",appearance.gridMinorWidth},{"gridMajorWidth",appearance.gridMajorWidth}}}};
        writeFileAtomically(file_,json.dump(2)+"\n"); status_.clear(); return true;
    } catch (const std::exception& error) { status_=error.what(); return false; }
}
} // namespace crimson
