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
bool sameSettings(const PlayerOcclusionSettings& a,const PlayerOcclusionSettings& b) {
    return a.minimumOpacity==b.minimumOpacity&&a.fadeStartDistance==b.fadeStartDistance&&a.fullFadeDistance==b.fullFadeDistance&&
        a.fadeOutSeconds==b.fadeOutSeconds&&a.fadeInSeconds==b.fadeInSeconds&&a.restoreDelaySeconds==b.restoreDelaySeconds;
}
bool samePreferences(const EditorPreferenceValues& a,const EditorPreferenceValues& b) {
    return a.appearance==b.appearance&&a.browserPreviewSize==b.browserPreviewSize&&a.pointShadowResolution==b.pointShadowResolution&&
        a.playerOcclusionFade==b.playerOcclusionFade&&sameSettings(a.playerOcclusionSettings,b.playerOcclusionSettings);
}
void playerOcclusionSettings() {
    Temp temp;const auto file=temp.root/"fade-preferences.json";EditorPreferences preferences(file);
    const PlayerOcclusionSettings defaults;
    require(defaults.minimumOpacity==.12f&&defaults.fadeStartDistance==4&&defaults.fullFadeDistance==.5f&&
        defaults.fadeOutSeconds==.045f&&defaults.fadeInSeconds==.14f&&defaults.restoreDelaySeconds==.07f,
        "Player fade tuning defaults changed.");
    require(sameSettings(preferences.values().playerOcclusionSettings,defaults),"Fresh preferences lost player fade defaults.");
    struct Field {const char* name;float PlayerOcclusionSettings::*member;float authored;};
    const std::array<Field,6> fields{{
        {"minimumOpacity",&PlayerOcclusionSettings::minimumOpacity,.28f},
        {"fadeStartDistance",&PlayerOcclusionSettings::fadeStartDistance,7.5f},
        {"fullFadeDistance",&PlayerOcclusionSettings::fullFadeDistance,1.25f},
        {"fadeOutSeconds",&PlayerOcclusionSettings::fadeOutSeconds,.08f},
        {"fadeInSeconds",&PlayerOcclusionSettings::fadeInSeconds,.32f},
        {"restoreDelaySeconds",&PlayerOcclusionSettings::restoreDelaySeconds,.11f}}};
    for(const auto& field:fields) {
        auto changed=preferences.values();changed.playerOcclusionSettings.*field.member=field.authored;
        require(preferences.setValues(changed),std::string("Changing only fade setting was ignored: ")+field.name);
        const EditorPreferences reopened(file);
        require(!reopened.values().playerOcclusionFade&&samePreferences(reopened.values(),changed),
            std::string("Fade setting did not persist while master was off: ")+field.name);
    }
    auto tuned=preferences.values();tuned.appearance.backgroundSrgb={.17f,.23f,.31f};tuned.playerOcclusionFade=true;
    require(preferences.setValues(tuned),"Cannot enable fading with retained tuning.");
    tuned.playerOcclusionFade=false;require(preferences.setValues(tuned),"Cannot disable fading without resetting tuning.");
    require(samePreferences(EditorPreferences(file).values(),tuned),"Disabling player fading lost authored tuning.");
    require(!preferences.setValues(tuned),"Unchanged player fade tuning created a preference change.");
    const auto saved=readText(file);const auto json=nlohmann::json::parse(saved);
    require(json.at("version")==1&&json.at("playerOcclusionSettings").is_object()&&json.at("playerOcclusionSettings").size()==fields.size(),
        "Player fade settings changed the preference version or omitted their settings object.");
    for(const auto& field:fields)require(json.at("playerOcclusionSettings").at(field.name)==tuned.playerOcclusionSettings.*field.member,
        std::string("Fade setting used an incorrect JSON key or value: ")+field.name);

    const auto oldFile=temp.root/"old-fade-preferences.json";
    const auto checkDefaults=[&](nlohmann::json value,PlayerOcclusionSettings expected,const char* message) {
        writeJson(oldFile,value);const auto before=readText(oldFile);const EditorPreferences older(oldFile);
        require(older.status().empty()&&older.values().appearance==tuned.appearance&&
            sameSettings(older.values().playerOcclusionSettings,expected),message);
        require(readText(oldFile)==before,"Loading older fade preferences rewrote their file.");
    };
    auto legacy=json;legacy.erase("playerOcclusionSettings");checkDefaults(legacy,defaults,"Missing fade object did not retain current defaults.");
    legacy["playerOcclusionSettings"]=nlohmann::json::object();checkDefaults(legacy,defaults,"Empty fade object did not retain current defaults.");
    auto partial=defaults;partial.minimumOpacity=.42f;
    legacy["playerOcclusionSettings"]={{"minimumOpacity",partial.minimumOpacity}};
    checkDefaults(legacy,partial,"Missing individual fade fields did not default independently.");

    auto boundary=tuned;auto& limits=boundary.playerOcclusionSettings;
    limits.minimumOpacity=0;limits.fullFadeDistance=0;limits.fadeStartDistance=.001f;
    limits.fadeOutSeconds=limits.fadeInSeconds=limits.restoreDelaySeconds=0;
    require(preferences.setValues(boundary)&&samePreferences(EditorPreferences(file).values(),boundary),
        "Zero opacity/timings or zero full-fade distance failed valid persistence.");
    limits.minimumOpacity=1;limits.fullFadeDistance=999;limits.fadeStartDistance=1000;
    limits.fadeOutSeconds=limits.fadeInSeconds=limits.restoreDelaySeconds=10;
    require(preferences.setValues(boundary)&&samePreferences(EditorPreferences(file).values(),boundary),
        "Maximum supported fade tuning failed valid persistence.");
    require(preferences.setValues(tuned),"Cannot restore tuned preferences after boundary checks.");
    const auto reject=[&](const PlayerOcclusionSettings& invalid,const std::string& message) {
        auto changed=tuned;changed.playerOcclusionSettings=invalid;changed.playerOcclusionFade=true;
        changed.appearance.backgroundSrgb={.8f,.7f,.6f};
        require(!preferences.setValues(changed)&&samePreferences(preferences.values(),tuned)&&readText(file)==saved,message);
    };
    for(const auto& field:fields)for(float value:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity()}) {
        auto invalid=tuned.playerOcclusionSettings;invalid.*field.member=value;
        reject(invalid,std::string("Nonfinite fade setting changed live/saved preferences: ")+field.name);
    }
    for(const auto& field:fields) {
        auto invalid=tuned.playerOcclusionSettings;invalid.*field.member=-.01f;
        reject(invalid,std::string("Negative fade setting changed live/saved preferences: ")+field.name);
        invalid=tuned.playerOcclusionSettings;
        invalid.*field.member=field.member==&PlayerOcclusionSettings::minimumOpacity?1.01f:
            field.member==&PlayerOcclusionSettings::fadeStartDistance||field.member==&PlayerOcclusionSettings::fullFadeDistance?1000.01f:10.01f;
        reject(invalid,std::string("Excessive fade setting changed live/saved preferences: ")+field.name);
    }
    auto invalid=tuned.playerOcclusionSettings;invalid.fadeStartDistance=invalid.fullFadeDistance;
    reject(invalid,"Equal fade distances changed live/saved preferences.");
    invalid.fadeStartDistance=invalid.fullFadeDistance-.1f;reject(invalid,"Reversed fade distances changed live/saved preferences.");

    const auto badFile=temp.root/"invalid-fade-preferences.json";
    for(const auto& badSettings:std::array<nlohmann::json,4>{nlohmann::json{{"minimumOpacity",1.01f}},
        nlohmann::json{{"fadeStartDistance",.5f},{"fullFadeDistance",.5f}},nlohmann::json{{"fadeInSeconds",nullptr}},nlohmann::json::array()}) {
        auto malformed=json;malformed["playerOcclusionSettings"]=badSettings;writeJson(badFile,malformed);
        const auto contents=readText(badFile);const EditorPreferences rejected(badFile);
        require(!rejected.status().empty()&&samePreferences(rejected.values(),EditorPreferenceValues{})&&readText(badFile)==contents,
            "Invalid saved fade tuning partially loaded or rewrote preferences.");
    }
}
void run() {
    Temp temp; const auto file=temp.root/"preferences.json";
    EditorPreferences preferences(file);
    require(preferences.values().browserPreviewSize==128,"Wrong default preview size.");
    require(preferences.values().pointShadowResolution==1024,"Wrong default point shadow resolution.");
    require(!preferences.values().playerOcclusionFade,"Player occluder fading must be opt-in.");
    require(preferences.values().appearance.gridMinorWidth==.325f && preferences.values().appearance.gridMajorWidth==.25f,"Quader grid defaults changed.");
    auto values=preferences.values(); values.appearance.backgroundSrgb={.2f,.3f,.4f};
    values.appearance.gridMajorSrgb={1,.5f,.25f,.8f}; values.appearance.gridMinorWidth=2.125f;
    require(preferences.setValues(values),"Cannot persist valid preferences.");
    EditorPreferences reopened(file);
    require(reopened.values().appearance==values.appearance,"Viewport preferences did not round-trip.");
    auto enabled=reopened.values();enabled.playerOcclusionFade=true;
    require(reopened.setValues(enabled),"Changing only the player occluder preference was ignored.");
    require(EditorPreferences(file).values().playerOcclusionFade,"Player occluder preference did not persist.");
    enabled.playerOcclusionFade=false;
    require(reopened.setValues(enabled) && !EditorPreferences(file).values().playerOcclusionFade,
        "Player occluder preference could not be disabled again.");
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
    nlohmann::json legacy;{std::ifstream input(file);input>>legacy;}legacy.erase("pointShadowResolution");legacy.erase("playerOcclusionFade");
    const auto old=temp.root/"legacy.json";{std::ofstream output(old);output<<legacy.dump();}
    EditorPreferences oldPreferences(old);
    require(oldPreferences.values().pointShadowResolution==1024 && oldPreferences.values().appearance==reopened.values().appearance,
        "Loading older preferences changed viewport settings or shadow defaults.");
    require(!oldPreferences.values().playerOcclusionFade,"Older preferences unexpectedly enabled player occluder fading.");
    const auto bad=temp.root/"damaged.json"; { std::ofstream out(bad); out<<"{ broken"; }
    EditorPreferences damaged(bad);
    require(!damaged.status().empty() && damaged.values().browserPreviewSize==128,"Damaged preferences lacked a safe default/diagnostic.");
    std::ifstream unchanged(bad); std::string contents{std::istreambuf_iterator<char>(unchanged),{}};
    require(contents=="{ broken","Loading malformed preferences rewrote the original.");
}
}
int main() {
    try { run();playerOcclusionSettings(); std::cout<<"Editor preferences checks passed.\n"; return 0; }
    catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
