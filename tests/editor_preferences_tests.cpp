#include "editor/EditorPreferences.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
void require(bool condition,const std::string& message) { if (!condition) throw std::runtime_error(message); }
struct Temp {
    std::filesystem::path root=std::filesystem::temp_directory_path()/
        ("crimson-preferences-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Temp() { std::filesystem::create_directories(root); }
    ~Temp() {
        std::error_code error; const auto target=std::filesystem::weakly_canonical(root,error);
        const auto parent=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if (!error && target.parent_path()==parent && target.filename().string().starts_with("crimson-preferences-test-"))
            std::filesystem::remove_all(target,error);
    }
};
std::string readText(const std::filesystem::path& file) {
    std::ifstream input(file,std::ios::binary);require(bool(input),"Cannot read disposable preference fixture.");
    return {std::istreambuf_iterator<char>(input),{}};
}
void writeJson(const std::filesystem::path& file,const nlohmann::json& value) {
    std::ofstream output(file);output<<value.dump();require(bool(output),"Cannot write disposable preference fixture.");
}
bool samePreferences(const EditorPreferenceValues& a,const EditorPreferenceValues& b) {
    return a.appearance==b.appearance&&a.browserPreviewSize==b.browserPreviewSize&&a.pointShadowResolution==b.pointShadowResolution&&
        a.graphicsBackend==b.graphicsBackend;
}
void graphicsBackendPreferences() {
    Temp temp;const auto file=temp.root/"graphics-preferences.json";EditorPreferences preferences(file);
    require(preferences.values().graphicsBackend==GraphicsBackend::Direct3D12,"New preferences must select Direct3D 12.");
    require(!std::filesystem::exists(file),"Reading default graphics preferences wrote a preference file.");
    auto values=preferences.values();values.graphicsBackend=GraphicsBackend::Direct3D11;
    values.appearance.backgroundSrgb={.11f,.22f,.33f};values.browserPreviewSize=256;values.pointShadowResolution=512;
    require(preferences.setValues(values),"Cannot persist Direct3D 11 preference.");
    require(samePreferences(EditorPreferences(file).values(),values),"Graphics preference lost existing preference data on reopen.");
    const auto dx11Text=readText(file);const auto json=nlohmann::json::parse(dx11Text);
    require(json.at("version")==1&&json.at("graphicsBackend")=="dx11","Graphics backend persistence used the wrong representation.");
    require(!preferences.setValues(values)&&readText(file)==dx11Text,"Unchanged graphics preferences wrote a new preference value.");
    auto invalid=values;invalid.graphicsBackend=static_cast<GraphicsBackend>(99);
    require(!preferences.setValues(invalid)&&samePreferences(preferences.values(),values)&&readText(file)==dx11Text,
        "Invalid live graphics backend changed live or saved preferences.");
    values.graphicsBackend=GraphicsBackend::Direct3D12;
    require(preferences.setValues(values)&&samePreferences(EditorPreferences(file).values(),values),"Direct3D 12 preference did not round-trip.");
    auto legacy=json;legacy.erase("graphicsBackend");const auto old=temp.root/"legacy-graphics.json";writeJson(old,legacy);
    const auto oldText=readText(old);const EditorPreferences reopenedOld(old);
    require(reopenedOld.status().empty()&&samePreferences(reopenedOld.values(),values)&&readText(old)==oldText,
        "Older preferences did not retain existing fields and default to Direct3D 12 without rewriting.");
    const auto bad=temp.root/"invalid-graphics.json";
    for(const auto& backend:std::array<nlohmann::json,5>{"vulkan",17,nullptr,true,nlohmann::json::object()}) {
        auto malformed=json;malformed["graphicsBackend"]=backend;writeJson(bad,malformed);const auto original=readText(bad);
        const EditorPreferences rejected(bad);
        require(!rejected.status().empty()&&samePreferences(rejected.values(),EditorPreferenceValues{})&&readText(bad)==original,
            "Invalid saved graphics backend partially loaded or rewrote preferences.");
    }
}
void obsoletePlayerFadePreferencesAreIgnored() {
    Temp temp;const auto file=temp.root/"obsolete-fade.json";EditorPreferences initial(file);
    auto values=initial.values();values.appearance.backgroundSrgb={.17f,.23f,.31f};values.pointShadowResolution=512;
    require(initial.setValues(values),"Cannot create obsolete preference fixture.");
    auto json=nlohmann::json::parse(readText(file));
    json["playerOcclusionFade"]=true;
    json["playerOcclusionSettings"]={{"minimumOpacity",.3f},{"fadeStartDistance",4}};
    writeJson(file,json);const auto before=readText(file);EditorPreferences reopened(file);
    require(reopened.status().empty()&&samePreferences(reopened.values(),values)&&readText(file)==before,
        "Loading obsolete player-fade preferences lost current values or rewrote their file.");
    auto changed=reopened.values();changed.browserPreviewSize=256;
    require(reopened.setValues(changed),"Cannot save current preferences after reading obsolete fading data.");
    auto saved=nlohmann::json::parse(readText(file));
    require(!saved.contains("playerOcclusionFade")&&!saved.contains("playerOcclusionSettings"),
        "Current preference writer retained removed player fading settings.");
    json["playerOcclusionFade"]=nlohmann::json::array();json["playerOcclusionSettings"]=nullptr;
    writeJson(file,json);const auto malformed=readText(file);const EditorPreferences ignored(file);
    require(ignored.status().empty()&&samePreferences(ignored.values(),values)&&readText(file)==malformed,
        "Malformed obsolete player-fade fields rejected otherwise valid preferences.");
}
void run() {
    Temp temp; const auto file=temp.root/"preferences.json";
    EditorPreferences preferences(file);
    require(preferences.values().browserPreviewSize==128,"Wrong default preview size.");
    require(preferences.values().pointShadowResolution==1024,"Wrong default point shadow resolution.");
    require(preferences.values().appearance.gridMinorWidth==.325f && preferences.values().appearance.gridMajorWidth==.25f,"Quader grid defaults changed.");
    auto values=preferences.values(); values.appearance.backgroundSrgb={.2f,.3f,.4f};
    values.appearance.gridMajorSrgb={1,.5f,.25f,.8f}; values.appearance.gridMinorWidth=2.125f;
    require(preferences.setValues(values),"Cannot persist valid preferences.");
    EditorPreferences reopened(file);
    require(reopened.values().appearance==values.appearance,"Viewport preferences did not round-trip.");
    for (const auto size:BrowserPreviewSizes) {
        reopened.setBrowserPreviewSize(size);
        require(reopened.values().browserPreviewSize==size,"Browser tick was not retained.");
    }
    reopened.setBrowserPreviewSize(999); require(reopened.values().browserPreviewSize==512,"Preview size did not clamp to largest tick.");
    const auto before=reopened.values(); auto invalid=before; invalid.appearance.gridMajorWidth=0;
    require(!reopened.setValues(invalid) && reopened.values().appearance==before.appearance,"Invalid width changed preferences.");
    invalid=before; invalid.appearance.backgroundSrgb[1]=std::numeric_limits<float>::quiet_NaN();
    require(!reopened.setValues(invalid),"Nonfinite color was accepted.");
    invalid=before; invalid.appearance.gridMinorSrgb[3]=1.2f; require(!reopened.setValues(invalid),"Invalid alpha was accepted.");
    invalid=before; invalid.browserPreviewSize=127; require(!reopened.setValues(invalid),"Non-tick browser size was accepted.");
    invalid=before;invalid.pointShadowResolution=768;require(!reopened.setValues(invalid) && reopened.values().pointShadowResolution==before.pointShadowResolution,
        "Unsupported point shadow resolution changed preferences.");
    for(const auto resolution:PointShadowResolutions) {
        auto changed=reopened.values();changed.pointShadowResolution=resolution;
        require(reopened.setValues(changed),"Cannot persist a supported point shadow resolution.");
        EditorPreferences roundTrip(file);require(roundTrip.values().pointShadowResolution==resolution,"Point shadow resolution did not round-trip.");
    }
    nlohmann::json legacy;{std::ifstream input(file);input>>legacy;}legacy.erase("pointShadowResolution");
    const auto old=temp.root/"legacy.json";{std::ofstream output(old);output<<legacy.dump();}
    EditorPreferences oldPreferences(old);
    require(oldPreferences.values().pointShadowResolution==1024 && oldPreferences.values().appearance==reopened.values().appearance,
        "Loading older preferences changed viewport settings or shadow defaults.");
    const auto bad=temp.root/"damaged.json"; { std::ofstream out(bad); out<<"{ broken"; }
    EditorPreferences damaged(bad);
    require(!damaged.status().empty() && damaged.values().browserPreviewSize==128,"Damaged preferences lacked a safe default/diagnostic.");
    std::ifstream unchanged(bad); std::string contents{std::istreambuf_iterator<char>(unchanged),{}};
    require(contents=="{ broken","Loading malformed preferences rewrote the original.");
}
}
int main() {
    try { run();obsoletePlayerFadePreferencesAreIgnored();graphicsBackendPreferences(); std::cout<<"Editor preferences checks passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
