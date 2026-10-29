#include "editor/NavigationPreview.h"
#include "integration/LevelNavigationPreview.h"
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
struct Project {
    std::filesystem::path root=std::filesystem::temp_directory_path()/("crimson-nav-preview-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    Project(){std::filesystem::create_directories(root);}
    ~Project(){std::error_code error;const auto path=std::filesystem::weakly_canonical(root,error),temp=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path(),error);
        if(!error && path.parent_path()==temp && path.filename().string().starts_with("crimson-nav-preview-"))std::filesystem::remove_all(path,error);}
};
std::vector<EntityRecord> records(const std::string& source) {
    EntityRecord floor;floor.id=1;floor.name="Floor";floor.collider.emplace();floor.collider->shape=ColliderShape::Trimesh;floor.collider->source=AssetRef<ModelAsset>{source};
    EntityRecord volume;volume.id=2;volume.name="NavigationVolume";volume.navigationVolume.emplace();
    return {floor,volume};
}
void install(SceneDocument& document,const std::string& source) {
    require(document.edit("Fixture",[&](auto& target,auto& properties){target=records(source);properties={};}),"Could not install the preview scene fixture.");
}
NavigationStatus settle(ResourceManager& resources,NavigationPreview& preview,const SceneDocument& document,const CollisionMeshQuery& query) {
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    do {
        resources.pump();preview.update(document,query,{});
        const auto status=preview.world().readiness(DefaultNavigationAgentProfileId);
        if(status!=NavigationStatus::Preparing)return status;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }while(std::chrono::steady_clock::now()<limit);
    throw std::runtime_error("Navigation preview workers did not settle.");
}
std::shared_ptr<const StaticTriangleMesh> plane() {
    return makeCollisionMesh({{-8,0,-8},{8,0,-8},{8,0,8},{-8,0,8}},{0,2,1,0,3,2});
}
void domainIsolation(const std::filesystem::path& root) {
    ResourceManager resources;NavigationPreview preview(resources,root);SceneDocument document(root);install(document,newAssetId());
    auto prepared=CollisionMeshResult{plane(),1,true,{}};const CollisionMeshQuery query=[&](const auto&){return prepared;};
    preview.update(document,query,{});require(settle(resources,preview,document,query)==NavigationStatus::Ready,"First scene failed to prepare its mesh floor.");
    auto invalid=std::make_shared<StaticTriangleMesh>(*prepared.mesh);invalid->indices[1]=999;
    prepared={invalid,2,true,{}};
    document.newScene();install(document,newAssetId());
    preview.update(document,query,{});
    require(settle(resources,preview,document,query)==NavigationStatus::Unreachable,"A reused EntityId retained the previous document's accepted floor after rejection.");
    require(preview.world().debugSnapshot(DefaultNavigationAgentProfileId).triangles.empty(),"Old-domain triangles survived into a rejected replacement scene.");
}
void pendingSourcePolicy(const std::filesystem::path& root) {
    ResourceManager resources;NavigationPreview preview(resources,root);SceneDocument document(root);install(document,newAssetId());
    auto prepared=CollisionMeshResult{plane(),1,true,{}};const CollisionMeshQuery query=[&](const auto&){return prepared;};
    preview.update(document,query,{});require(settle(resources,preview,document,query)==NavigationStatus::Ready,"Policy fixture failed to prepare navigation.");
    prepared.current=false;prepared.diagnostic="Preparing collision.";preview.update(document,query,{});
    require(preview.world().readiness(DefaultNavigationAgentProfileId)==NavigationStatus::Preparing,"Pending source did not block current readiness.");
    require(document.edit("Disable volume",[](auto& target,auto&){for(auto& record:target)if(record.navigationVolume)record.navigationVolume->enabled=false;}),"Could not disable pending-source volume.");
    preview.update(document,query,{});
    require(preview.world().readiness(DefaultNavigationAgentProfileId)==NavigationStatus::Unavailable &&
        preview.world().debugSnapshot(DefaultNavigationAgentProfileId).triangles.empty(),"Disabling a volume while collision was pending retained its old navigation.");
    require(document.undo(),"Could not restore the volume after pending-source removal.");preview.update(document,query,{});
    require(document.edit("Remove type",[](auto& target,auto&){for(auto& record:target)if(record.navigationVolume)record.navigationVolume->profiles.clear();}),"Could not remove a supported profile.");
    preview.update(document,query,{});
    require(preview.world().readiness(DefaultNavigationAgentProfileId)==NavigationStatus::Unavailable,"Removing a supported type waited for unrelated collision preparation.");
}
void externalWorldPendingOverlay(const std::filesystem::path& root) {
    ResourceManager resources;NavigationWorld play(resources);NavigationPreview overlayOwner(resources,root);
    NavigationVolumeDescriptor volume;volume.entity=2;
    const std::vector profiles{*defaultNavigationAgentProfile()};
    const std::vector boxes{PhysicsBox{1,math::translation({0,-.5f,0}),{16,1,16}}};
    play.reconcile(1,volume,profiles,boxes,{});
    const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds(15);
    while(play.readiness(DefaultNavigationAgentProfileId)==NavigationStatus::Preparing && std::chrono::steady_clock::now()<limit) {
        resources.pump();play.pumpPublication();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    require(play.readiness(DefaultNavigationAgentProfileId)==NavigationStatus::Ready,"External overlay fixture failed to prepare navigation.");
    const auto ready=overlayOwner.overlays(&play,DefaultNavigationAgentProfileId);
    require(!ready.empty() && ready.front().color[1]>ready.front().color[0],"Accepted external navigation did not display its current color.");
    play.setSourceReadiness(false,"Coalescing a Play volume edit.");
    const auto pending=overlayOwner.overlays(&play,DefaultNavigationAgentProfileId);
    require(play.summary(DefaultNavigationAgentProfileId).pendingSources && !pending.empty() && pending.front().color[0]>pending.front().color[1],
        "Pending Play sources displayed old accepted geometry as current navigation.");
    play.setSourceReadiness(true);
    const auto resumed=overlayOwner.overlays(&play,DefaultNavigationAgentProfileId);
    require(!resumed.empty() && resumed.front().color[1]>resumed.front().color[0],"Accepted external navigation retained its pending color after publication resumed.");
}
void heldGestureWithIdleFrames(const std::filesystem::path& root) {
    ResourceManager resources;NavigationPreview preview(resources,root);SceneDocument document(root);install(document,newAssetId());
    const auto prepared=CollisionMeshResult{plane(),1,true,{}};const CollisionMeshQuery query=[&](const auto&){return prepared;};
    preview.update(document,query,{});require(settle(resources,preview,document,query)==NavigationStatus::Ready,"Gesture fixture failed to prepare navigation.");
    require(document.begin("Resize volume"),"Could not begin the held volume gesture.");
    const auto change=[&] {require(document.update([](auto& target,auto&){for(auto& record:target)if(record.navigationVolume)record.navigationVolume->size[0]+=.2f;}),"Live volume resize failed.");};
    const auto beforeIdle=preview.world().buildCount();
    for(unsigned frame=0;frame<8;++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));resources.pump();preview.update(document,query,{});
    }
    change();preview.update(document,query,{});
    require(preview.world().buildCount()>beforeIdle,"Unchanged held frames restarted the accepted-input timer and delayed the next resize.");
    const auto first=preview.world().buildCount();
    // Mouse deltas arrive every other frame. The intervening unchanged frames
    // must not restart the coalescing timer and starve the held gesture.
    for(unsigned frame=0;frame<14;++frame) {
        std::this_thread::sleep_for(std::chrono::milliseconds(16));
        if(frame%2==0)change();resources.pump();preview.update(document,query,{});
    }
    require(document.transactionActive() && preview.world().buildCount()>first,"Unchanged editor frames starved live navigation updates during a held resize.");
    document.cancel();preview.update(document,query,{});
}
void nativeProfileAndOverlayAdapter(const std::filesystem::path& root) {
    ResourceManager resources;integration::LevelNavigationPreview preview(resources,root);SceneDocument document(root);
    install(document,newAssetId());const auto authored=document.scene().records();const auto history=document.history().size();
    const CollisionMeshResult prepared{plane(),1,true,{}};const CollisionMeshQuery query=[&](const auto&){return prepared;};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{15};
    do {
        resources.pump();preview.update(document,query,{});
        if(preview.authoringWorld().readiness(DefaultNavigationAgentProfileId)!=NavigationStatus::Preparing)break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(std::chrono::steady_clock::now()<deadline);
    require(preview.profile()==DefaultNavigationAgentProfileId&&preview.authoringWorld().readiness(preview.profile())==NavigationStatus::Ready,
        "The native level adapter did not select and bake its volume's supported profile.");
    require(preview.overlays().empty(),"The native adapter displayed debug navigation before its view toggle.");
    preview.setVisible(true);const auto first=preview.overlays();
    require(!first.empty()&&first.front().geometry,"The native navigation view did not expose the retained debug geometry.");
    const auto builds=preview.authoringWorld().buildCount();
    for(unsigned frame=0;frame<8;++frame){preview.update(document,query,{});const auto stable=preview.overlays();require(!stable.empty()&&stable.front().geometry==first.front().geometry,"Stable native frames rebuilt navigation overlay geometry.");}
    const auto after=document.scene().records();
    require(after.size()==authored.size()&&std::equal(after.begin(),after.end(),authored.begin(),[](const auto& a,const auto& b){return sameEntity(a,b);})&&
        document.history().size()==history&&preview.authoringWorld().buildCount()==builds,
        "Read-only native navigation altered authored records, history or a stable bake.");
    NavigationWorld runtime(resources);NavigationVolumeDescriptor volume;volume.entity=20;
    const std::vector profiles{*defaultNavigationAgentProfile()};const std::vector boxes{PhysicsBox{21,math::translation({0,-.5f,0}),{16,1,16}}};
    runtime.reconcile(900,volume,profiles,boxes,{});const auto runtimeBuilds=runtime.buildCount();
    preview.update(document,query,{},&runtime);(void)preview.overlays();
    require(runtime.buildCount()==runtimeBuilds&&preview.authoringWorld().buildCount()==builds,
        "Borrowing the Play navigation view changed either world's simulation/bake ownership.");
    require(document.edit("Remove native preview profiles",[](auto& records,auto&){for(auto& record:records)if(record.navigationVolume)record.navigationVolume->profiles.clear();}),"Cannot remove the adapter profile fixture.");
    preview.update(document,query,{});
    require(preview.profile().empty()&&preview.overlays().empty(),"Removed profiles retained a stale native navigation overlay.");
}
}
int main(){try{Project project;domainIsolation(project.root);pendingSourcePolicy(project.root);externalWorldPendingOverlay(project.root);heldGestureWithIdleFrames(project.root);nativeProfileAndOverlayAdapter(project.root);std::cout<<"Navigation preview document isolation, native supported-profile/overlay adapter and pending collision policy passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
