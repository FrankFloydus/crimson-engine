#include "LevelNavigationPreview.h"
#include <algorithm>

namespace crimson::integration {
LevelNavigationPreview::LevelNavigationPreview(ResourceManager& resources,const std::filesystem::path& root):preview_(resources,root){}
void LevelNavigationPreview::update(const SceneDocument& document,const CollisionMeshQuery& meshes,
    const NavigationAgentProfileQuery& profiles,const NavigationWorld* runtime) {
    const auto records=document.scene().records();
    const auto volume=std::find_if(records.begin(),records.end(),[](const auto& record){return record.navigationVolume&&record.navigationVolume->enabled;});
    if(volume==records.end()){profile_.clear();volume_=NoEntity;}
    else {
        const auto& supported=volume->navigationVolume->profiles;
        const auto contains=[&](const std::string& id){return std::any_of(supported.begin(),supported.end(),[&](const auto& reference){return reference.id==id;});};
        if(generation_!=document.documentGeneration()||volume_!=volume->id||!contains(profile_)) {
            profile_=supported.empty()?std::string{}:supported.front().id;
            if(const auto player=document.scene().find(document.scene().properties().playerCharacter);player&&player->controller&&contains(player->controller->profile.id))profile_=player->controller->profile.id;
        }
        volume_=volume->id;
    }
    generation_=document.documentGeneration();
    if(!runtime)preview_.update(document,meshes,profiles);
    source_=runtime?runtime:&preview_.world();
}
std::string LevelNavigationPreview::status(const std::string& profile)const{return preview_.status(source_,profile);}
std::vector<RenderCollisionOverlay> LevelNavigationPreview::overlays(){return visible_?preview_.overlays(source_,profile_):std::vector<RenderCollisionOverlay>{};}
}
