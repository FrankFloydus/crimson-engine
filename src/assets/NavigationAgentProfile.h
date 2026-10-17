#pragma once
#include "resources/AssetId.h"
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {
struct AgentTraversalSettings {
    float height{1.8f},radius{.3f},maxSlope{45.f},maxStep{.3f};
    bool operator==(const AgentTraversalSettings&) const = default;
};
inline bool validAgentTraversal(const AgentTraversalSettings& value) {
    return std::isfinite(value.height)&&std::isfinite(value.radius)&&std::isfinite(value.maxSlope)&&std::isfinite(value.maxStep)&&
        value.radius>0&&value.height>=2*value.radius&&value.maxSlope>=0&&value.maxSlope<90&&value.maxStep>=0;
}
struct NavigationBakeSettings {
    float cellSize{.1f},cellHeight{.05f};
    std::uint32_t tileSize{128};
    float maxSimplificationError{.5f};
    bool operator==(const NavigationBakeSettings&) const = default;
};
inline bool validNavigationBakeSettings(const NavigationBakeSettings& value) {
    return std::isfinite(value.cellSize)&&value.cellSize>=.01f&&value.cellSize<=10&&
        std::isfinite(value.cellHeight)&&value.cellHeight>=.005f&&value.cellHeight<=10&&
        value.tileSize>=16&&value.tileSize<=1024&&
        std::isfinite(value.maxSimplificationError)&&value.maxSimplificationError>=0&&value.maxSimplificationError<=10;
}
struct NavigationAgentProfile {
    std::string id,name{"New Navigation Agent Profile"};
    AgentTraversalSettings traversal;
    bool operator==(const NavigationAgentProfile&) const = default;
};
inline const std::string DefaultNavigationAgentProfileId=canonicalAssetId("builtin:navigation-agent/default");
using NavigationAgentProfileQuery=std::function<std::shared_ptr<const NavigationAgentProfile>(const std::string&)>;
inline std::shared_ptr<const NavigationAgentProfile> defaultNavigationAgentProfile() {
    static const auto value=std::make_shared<const NavigationAgentProfile>(NavigationAgentProfile{DefaultNavigationAgentProfileId,"Default Agent",{}});
    return value;
}
inline std::shared_ptr<const NavigationAgentProfile> resolveNavigationAgentProfile(const std::string& id,const NavigationAgentProfileQuery& query={}) {
    if(id==DefaultNavigationAgentProfileId)return defaultNavigationAgentProfile();
    const auto value=query?query(id):nullptr;
    return value&&validAgentTraversal(value->traversal)?value:nullptr;
}
void validateNavigationAgentProfile(const NavigationAgentProfile&);
std::string serializeNavigationAgentProfile(const NavigationAgentProfile&);
NavigationAgentProfile deserializeNavigationAgentProfile(std::string_view,const std::string& expectedId={});
std::filesystem::path navigationAgentProfilePath(const std::filesystem::path&,const std::string&);
NavigationAgentProfile loadNavigationAgentProfile(const std::filesystem::path&,const std::string&);
void saveNavigationAgentProfile(const std::filesystem::path&,const NavigationAgentProfile&);
NavigationAgentProfile createNavigationAgentProfile(const std::filesystem::path&,const std::string& name="New Navigation Agent Profile");
NavigationAgentProfile copyNavigationAgentProfile(const std::filesystem::path&,const NavigationAgentProfile&,const std::string& name={});
void ensureEngineNavigationAgentProfile(const std::filesystem::path&);
}
