#pragma once
#include "runtime/PlaySession.h"
#include "agent_profile_fixture.h"
#include <chrono>
#include <thread>

namespace crimson::testing {
inline bool waitForNavigation(PlaySession& play,std::chrono::seconds timeout=std::chrono::seconds(15)) {
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<deadline) {
        play.advance(0,Vec2{},Camera{});
        bool pending=false;
        for(const auto& record:play.document().scene().records())if(record.navigationVolume&&record.navigationVolume->enabled)
            for(const auto& profile:record.navigationVolume->profiles) {
                const auto state=play.navigationReadiness(profile.id);
                if(state==NavigationStatus::Failed||state==NavigationStatus::Unavailable)return false;
                pending|=state==NavigationStatus::Preparing;
            }
        if(!pending)return true;
        std::this_thread::yield();
    }
    return false;
}
inline bool waitForNavigation(PlaySession& play,GameInput& input,const Camera& camera,std::chrono::seconds timeout=std::chrono::seconds(15)) {
    const auto deadline=std::chrono::steady_clock::now()+timeout;
    while(std::chrono::steady_clock::now()<deadline) {
        play.advance(0,input,camera);
        bool pending=false;
        for(const auto& record:play.document().scene().records())if(record.navigationVolume&&record.navigationVolume->enabled)
            for(const auto& profile:record.navigationVolume->profiles) {
                const auto state=play.navigationReadiness(profile.id);
                if(state==NavigationStatus::Failed||state==NavigationStatus::Unavailable)return false;
                pending|=state==NavigationStatus::Preparing;
            }
        if(!pending)return true;
        std::this_thread::yield();
    }
    return false;
}
// Older movement fixtures explicitly author their navigation setup in the Play
// copy. Production scenes never acquire a volume implicitly.
inline bool startWithNavigation(PlaySession& play,const SceneDocument& authored) {
    play.setAgentProfiles(agentProfileQuery());
    SceneDocument prepared(authored.projectRoot());if(!prepared.resetTransientFrom(authored))return false;
    bool volume=false;for(const auto& record:prepared.scene().records())volume|=record.navigationVolume.has_value();
    if(!volume) {
        const auto id=prepared.createNavigationVolume();if(!id)return false;
        if(!prepared.edit("Fixture navigation coverage",[id](auto& records,auto&){
            for(auto& record:records)if(record.id==id)record.navigationVolume->size={100,30,100};
        }))return false;
    }
    if(!play.start(prepared))return false;
    return waitForNavigation(play);
}
}
