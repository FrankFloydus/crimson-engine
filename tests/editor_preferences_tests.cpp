#include "editor/EditorPreferences.h"
#include "math/Json.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message) { if (!condition) throw std::runtime_error(message); }
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
    try { run(); std::cout<<"Editor preferences checks passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
