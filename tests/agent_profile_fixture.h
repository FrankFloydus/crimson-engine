#pragma once
#include "scene/Scene.h"
#include <unordered_map>

namespace crimson::testing {
inline auto& agentProfiles() {
    static std::unordered_map<std::string,std::shared_ptr<const NavigationAgentProfile>> profiles;
    return profiles;
}
inline AssetRef<NavigationAgentProfile> agentProfile(AgentTraversalSettings traversal) {
    auto profile=std::make_shared<const NavigationAgentProfile>(NavigationAgentProfile{newAssetId(),"Fixture Agent",traversal});
    agentProfiles()[profile->id]=profile;return AssetRef<NavigationAgentProfile>{profile->id};
}
inline NavigationAgentProfileQuery agentProfileQuery() {
    return [](const std::string& id)->std::shared_ptr<const NavigationAgentProfile> {
        const auto found=agentProfiles().find(id);return found==agentProfiles().end()?nullptr:found->second;
    };
}
}
