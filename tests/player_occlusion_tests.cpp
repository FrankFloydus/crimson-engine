#include "editor/PlayerOcclusion.h"
#include <array>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
float opacity(const PlayerOcclusionOpacities& values,EntityId id){const auto found=values.find(id);return found==values.end()?1:found->second;}
void near(float actual,float expected,const char* message){require(std::abs(actual-expected)<1e-4,message);}
ResourceConfig config(){ResourceConfig value;value.workers=1;value.idleGrace=std::chrono::milliseconds{0};value.cpuRetentionBytes=0;return value;}
struct World {
    Scene scene;std::vector<EntityRecord> records;SceneProperties properties;
    World() {
        EntityRecord player;player.id=1;player.name="Player";player.character.emplace();player.character->height=2;
        player.character->lightRig.enabled=false;player.character->darknessReveal.enabled=false;
        EntityRecord child;child.id=2;child.name="Player visual";child.parent=1;
        EntityRecord camera;camera.id=10;camera.name="Game camera";camera.camera.emplace();
        auto alternate=camera;alternate.id=11;alternate.name="Alternate camera";
        records={player,child,camera,alternate};properties.playerCharacter=1;properties.activeCamera=10;publish();
    }
    void publish(){scene.apply(records,properties);}
};
Camera camera(){Camera value;value.position={0,1,8};value.target={0,1,0};return value;}
ExtractedModel model(EntityId id,const Mat4& world=Mat4{}){ExtractedModel value;value.id=id;value.assetId="wall";value.world=world;value.fadeWhenOccluding=true;return value;}
void rectangle(StaticTriangleMesh& mesh,float x0,float x1,float y0,float y1) {
    const auto first=static_cast<std::uint32_t>(mesh.positions.size());
    mesh.positions.insert(mesh.positions.end(),{{x0,y0,0},{x1,y0,0},{x1,y1,0},{x0,y1,0}});
    mesh.indices.insert(mesh.indices.end(),{first,first+1,first+2,first,first+2,first+3});
}
std::shared_ptr<StaticTriangleMesh> wall(bool doorway=false,unsigned subdivisions=1) {
    auto mesh=std::make_shared<StaticTriangleMesh>();
    if(doorway){rectangle(*mesh,-2,-.6f,0,3);rectangle(*mesh,.6f,2,0,3);rectangle(*mesh,-.6f,.6f,2,3);}
    else for(unsigned y=0;y<subdivisions;++y)for(unsigned x=0;x<subdivisions;++x)
        rectangle(*mesh,-2+4.f*x/subdivisions,-2+4.f*(x+1)/subdivisions,3.f*y/subdivisions,3.f*(y+1)/subdivisions);
    for(const auto& point:mesh->positions)math::include(mesh->bounds,point);mesh->fingerprint=doorway?2:1;return mesh;
}
struct Query {
    CollisionMeshResult source;std::size_t calls{};
    explicit Query(std::shared_ptr<const StaticTriangleMesh> mesh):source{std::move(mesh),1,true,{}}{}
    ModelBoundsQuery bounds(){return [this](const std::string&)->std::optional<Aabb>{if(source.mesh)return source.mesh->bounds;return std::nullopt;};}
    CollisionMeshQuery triangles(){return [this](const std::string&){++calls;return source;};}
};
template<class Predicate> void await(ResourceManager& resources,Predicate predicate) {
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
    do{resources.pump();if(predicate())return;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(std::chrono::steady_clock::now()<deadline);
    throw std::runtime_error("Player occlusion BVH did not publish.");
}
void warm(ResourceManager& resources,PlayerOcclusion& fade,const World& world,const std::vector<ExtractedModel>& models,Query& query,const Camera& view=camera(),std::uint64_t generation=1,const PlayerOcclusionSettings& settings={}) {
    await(resources,[&]{fade.update(world.scene,models,view,generation,0,query.bounds(),query.triangles(),settings);return fade.stats().cachedMeshes&&fade.stats().pendingMeshes==0;});
}
void explicitParticipation() {
    require(!EntityRecord{}.fadeWhenOccluding&&!ModelReference{}.fadeWhenOccluding&&!ExtractedModel{}.fadeWhenOccluding,
        "Model occlusion fading must default to disabled at every authored/extracted boundary.");
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.3f})),model(21,math::translation({0,0,.6f}))};
    for(auto& pillar:models)pillar.fadeWhenOccluding=false;
    std::size_t boundsCalls{};
    const ModelBoundsQuery bounds=[&](const std::string&)->std::optional<Aabb>{++boundsCalls;return query.source.mesh->bounds;};
    require(fade.update(world.scene,models,camera(),1,.5f,bounds,query.triangles()).empty()&&query.calls==0&&boundsCalls==0&&
        fade.stats().boundsCandidates==0&&fade.stats().triangleTests==0,"Unchecked pillars requested bounds/triangles or faded despite default opt-out.");
    models.push_back(model(22,math::translation({0,0,.3f})));warm(resources,fade,world,models,query);
    const auto& selected=fade.update(world.scene,models,camera(),1,.5f,bounds,query.triangles());
    require(!selected.contains(20)&&!selected.contains(21)&&opacity(selected,22)<.2f&&fade.stats().boundsCandidates==1,
        "Selecting one participating model also faded unchecked intervening pillars.");
    models.back().fadeWhenOccluding=false;const auto queries=query.calls;boundsCalls=0;
    require(fade.update(world.scene,models,camera(),1,0,bounds,query.triangles()).empty()&&query.calls==queries&&boundsCalls==0&&
        fade.stats().triangleTests==0,"Unchecking participation retained opacity through hysteresis or requested geometry.");
}
void hiddenVisualsDoNotFade() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.3f}))};models.front().visible=false;
    require(fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles()).empty()&&query.calls==0&&
        fade.stats().boundsCandidates==0,"A hidden wall requested occlusion geometry or faded.");
    models.front().visible=true;warm(resources,fade,world,models,query);
    require(opacity(fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles()),20)<.2f,
        "Showing a wall did not restore its participation.");
    models.front().visible=false;const auto calls=query.calls;
    require(fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles()).empty()&&query.calls==calls,
        "Hiding a wall retained stale fade state or queried its geometry.");
    models.front().visible=true;warm(resources,fade,world,models,query);
    fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles());
    EntityRecord group;group.id=30;group.name="Hidden player group";group.visible=false;
    world.records[0].parent=group.id;world.records.push_back(group);world.publish();const auto hiddenCalls=query.calls;
    require(fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles()).empty()&&query.calls==hiddenCalls,
        "An inherited-hidden player continued fading walls.");
}
void originalsDoorwayAndBroadphase() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.3f}))};
    const auto& cold=fade.update(world.scene,models,camera(),1,.15f,query.bounds(),query.triangles());
    require(cold.empty()&&resources.stats().loads==0,"Original geometry BVH was synchronously built/published on the frame thread.");
    warm(resources,fade,world,models,query);near(opacity(fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles()),20),.12f,"A nearby original triangle did not fade with player lighting disabled.");
    query.source={wall(true),2,true,{}};
    const auto& pending=fade.update(world.scene,models,camera(),1,.05f,query.bounds(),query.triangles());
    near(opacity(pending,20),.12f,"A pending BVH revision discarded the last-good visibility result.");
    warm(resources,fade,world,models,query);
    near(opacity(fade.update(world.scene,models,camera(),1,.05f,query.bounds(),query.triangles()),20),.12f,"A short doorway-edge loss bypassed the release delay.");
    const auto releasing=opacity(fade.update(world.scene,models,camera(),1,.1f,query.bounds(),query.triangles()),20);
    require(releasing>.12f&&releasing<1,"An actual door opening did not start smoothly restoring its wall after the release delay.");
    const auto& restored=fade.update(world.scene,models,camera(),1,1.25f,query.bounds(),query.triangles());
    require(restored.empty(),"A doorway hole or stale fade left the visual wall in the transparent queue.");
    require(fade.stats().triangleTests>0,"Doorway test bypassed original triangles.");
    models.front().world=math::translation({30,0,.3f});const auto calls=query.calls;
    require(fade.update(world.scene,models,camera(),1,.15f,query.bounds(),query.triangles()).empty()&&query.calls==calls&&fade.stats().boundsCandidates==0,
        "A distant non-occluding mesh requested original geometry.");
    // A flat floor cannot obstruct these three body samples from an above-ground camera.
    auto floor=wall();for(auto& point:floor->positions)std::swap(point[1],point[2]);floor->bounds={};for(const auto& point:floor->positions)math::include(floor->bounds,point);
    query.source={floor,3,true,{}};models.front().world=Mat4{};fade.clear();const auto floorCalls=query.calls;
    require(fade.update(world.scene,models,camera(),1,.15f,query.bounds(),query.triangles()).empty()&&query.calls==floorCalls,"An unobstructing floor faded or queued triangle work.");
}
void allOccludersAndDistance() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.3f})),model(21,math::translation({0,0,2})),model(22,math::translation({0,0,3.5f})),model(23,math::translation({0,0,5}))};
    warm(resources,fade,world,models,query);const auto calls=query.calls;
    const auto& result=fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles());
    require(opacity(result,20)<opacity(result,21)&&opacity(result,21)<opacity(result,22)&&opacity(result,22)<1&&opacity(result,23)==1,
        "All intervening meshes must fade monotonically with their hit-to-player distance, within four metres.");
    require(query.calls==calls+1&&resources.stats().loads==1,"Instances of one source duplicated original queries or BVH work.");
    models.erase(models.begin());const auto& removed=fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles());
    require(!removed.contains(20),"A removed visual retained transient opacity state.");
}
void affinePresentationAndPlayerExclusion() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    auto transform=math::translation({0,0,.3f})*math::eulerRotationXYZ({0,.4f,0})*math::scaling({-2,1.5f,.6f});transform.at(0,1)+=.2f;
    std::vector models{model(20,transform),model(1,math::translation({0,0,.2f})),model(2,math::translation({0,0,.1f}))};
    warm(resources,fade,world,models,query);const auto& result=fade.update(world.scene,models,camera(),1,.15f,query.bounds(),query.triangles());
    require(opacity(result,20)<.2f&&!result.contains(1)&&!result.contains(2),"Affine/reflected walls did not fade correctly or the player subtree faded itself.");
    fade.clear();models={model(20,math::scaling({0,1,1}))};const auto calls=query.calls;
    require(fade.update(world.scene,models,camera(),1,.15f,query.bounds(),query.triangles()).empty()&&query.calls==calls,"Singular mesh transforms requested occlusion work.");
    world.records[0].local=math::translation({0,0,4});world.publish();models={model(20,math::translation({0,0,4.3f}))};
    warm(resources,fade,world,models,query);near(opacity(fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles()),20),.12f,
        "Visibility used an unpresented/rest player position instead of the supplied scene transform.");
}
void resetsAndSmoothRestore() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.3f}))};warm(resources,fade,world,models,query);
    const auto approaching=opacity(fade.update(world.scene,models,camera(),1,.05f,query.bounds(),query.triangles()),20);
    require(approaching>.12f&&approaching<1,"Fade-in did not ease toward its target.");
    fade.update(world.scene,models,camera(),1,.15f,query.bounds(),query.triangles());
    auto moved=camera();moved.position[0]=80;
    const auto firstRestore=opacity(fade.update(world.scene,models,moved,1,.1f,query.bounds(),query.triangles()),20);
    require(firstRestore>.12f&&firstRestore<1,"Ordinary camera movement reset opacity instead of smoothly restoring it.");
    require(fade.update(world.scene,models,moved,1,1.25f,query.bounds(),query.triangles()).empty(),"Restoration did not reach exactly opaque.");
    fade.update(world.scene,models,camera(),1,.15f,query.bounds(),query.triangles());world.properties.activeCamera=11;world.publish();
    require(fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles()).empty(),"Changing camera identity retained the former view's fade.");
    warm(resources,fade,world,models,query);fade.update(world.scene,models,camera(),1,.15f,query.bounds(),query.triangles());
    require(fade.update(world.scene,models,camera(),2,0,query.bounds(),query.triangles()).empty(),"Document/Play identity reset retained opacity.");
    warm(resources,fade,world,models,query,camera(),2);fade.update(world.scene,models,camera(),2,.15f,query.bounds(),query.triangles());
    world.properties.playerCharacter=0;world.publish();const auto calls=query.calls;
    require(fade.update(world.scene,models,camera(),2,.15f,query.bounds(),query.triangles()).empty()&&query.calls==calls&&fade.stats().cachedMeshes==0,
        "No player did not clear state or continued requesting geometry.");
    fade.clear();require(fade.stats().triangleTests==0,"Clearing disabled/hidden Game state retained query work.");
}
void smoothSpatialResponse() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.6f})),model(21,math::translation({0,0,2.25f})),model(22,math::translation({0,0,3.9f}))};
    warm(resources,fade,world,models,query);
    const auto& values=fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles());
    require(opacity(values,20)>.12f&&opacity(values,20)<.13f&&opacity(values,22)>.995f&&opacity(values,22)<1,
        "Spatial fade did not flatten smoothly near its strongest/start distances.");
    require(opacity(values,20)<opacity(values,21)&&opacity(values,21)<opacity(values,22),"Smooth spatial response stopped being monotonic.");
}
void doorwayEdgeHysteresis() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({1.98f,0,.3f})),model(21,math::translation({8,0,.3f}))};
    warm(resources,fade,world,models,query);fade.update(world.scene,models,camera(),1,.5f,query.bounds(),query.triangles());
    for(unsigned frame=0;frame<24;++frame) {
        models[0].world=math::translation({frame%2?1.98f:2.02f,0,.3f});
        const auto& values=fade.update(world.scene,models,camera(),1,.02f,query.bounds(),query.triangles());
        near(opacity(values,20),.12f,"A short sight-line edge miss made a faded wall flash back toward opaque.");
        require(!values.contains(21),"Release hysteresis faded a nearby mesh that never obstructed the player.");
    }
    models[0].world=math::translation({2.02f,0,.3f});
    const auto held=opacity(fade.update(world.scene,models,camera(),1,.05f,query.bounds(),query.triangles()),20);
    near(held,.12f,"Continuous clear sight released before the short hysteresis interval.");
    const auto restoring=opacity(fade.update(world.scene,models,camera(),1,.07f,query.bounds(),query.triangles()),20);
    require(restoring>.12f&&restoring<.6f,"Clear sight did not restore gradually after hysteresis.");
    models[0].world=math::translation({1.98f,0,.3f});
    const auto reversed=opacity(fade.update(world.scene,models,camera(),1,.03f,query.bounds(),query.triangles()),20);
    require(reversed>=.12f&&reversed<restoring,"An occluder returning during restoration did not reverse smoothly.");
    require(resources.stats().loads==1,"Doorway-edge jitter rebuilt the immutable original-geometry BVH.");
}
void framePartitionIndependent() {
    auto custom=PlayerOcclusionSettings{};custom.minimumOpacity=.3f;custom.fadeStartDistance=6;custom.fullFadeDistance=.7f;
    custom.fadeOutSeconds=.09f;custom.fadeInSeconds=.2f;custom.restoreDelaySeconds=.11f;
    for(const auto& settings:std::array{PlayerOcclusionSettings{},custom}) {
    ResourceManager resources(config());PlayerOcclusion whole(resources),partitioned(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.3f}))};
    warm(resources,whole,world,models,query,camera(),1,settings);warm(resources,partitioned,world,models,query,camera(),1,settings);
    const auto advance=[&](PlayerOcclusion& fade,const Camera& view,float duration,unsigned frames) {
        for(unsigned i=0;i<frames;++i)fade.update(world.scene,models,view,1,duration/frames,query.bounds(),query.triangles(),settings);
        return opacity(fade.update(world.scene,models,view,1,0,query.bounds(),query.triangles(),settings),20);
    };
    const auto approach=advance(whole,camera(),.18f,1);
    near(approach,advance(partitioned,camera(),.18f,18),"Exponential approach depended on the number of frames.");
    auto clear=camera();clear.position[0]=80;
    const auto restored=advance(whole,clear,.17f,1);
    near(restored,advance(partitioned,clear,.17f,17),"Restoration timing changed when a frame crossed the release-delay boundary.");
    const auto reversed=advance(whole,camera(),.06f,1);
    near(reversed,advance(partitioned,camera(),.06f,6),"Reversing the fade introduced frame-rate dependence.");
    require(reversed>=settings.minimumOpacity&&reversed<restored,"Fade reversal overshot or continued restoring.");
    near(advance(whole,clear,1.6f,1),1,"One long clear interval did not settle exactly opaque.");
    near(advance(partitioned,clear,1.6f,160),1,"Partitioned clear intervals did not settle exactly opaque.");
    require(whole.update(world.scene,models,clear,1,0,query.bounds(),query.triangles(),settings).empty()&&
        partitioned.update(world.scene,models,clear,1,0,query.bounds(),query.triangles(),settings).empty(),"Settled fade state remained in the transparent queue.");
    }
}
void settingsValidationAndLiveTweaks() {
    const PlayerOcclusionSettings defaults;require(defaults==PlayerOcclusionSettings{}&&validPlayerOcclusionSettings(defaults),"Default occlusion settings are invalid or lack value equality.");
    constexpr std::array fields{&PlayerOcclusionSettings::minimumOpacity,&PlayerOcclusionSettings::fadeStartDistance,&PlayerOcclusionSettings::fullFadeDistance,
        &PlayerOcclusionSettings::fadeOutSeconds,&PlayerOcclusionSettings::fadeInSeconds,&PlayerOcclusionSettings::restoreDelaySeconds};
    for(const auto field:fields)for(const auto invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        auto value=defaults;value.*field=invalid;require(!validPlayerOcclusionSettings(value),"Nonfinite occlusion settings were accepted.");
    }
    for(const auto field:fields){auto value=defaults;value.*field=-.01f;require(!validPlayerOcclusionSettings(value),"Negative occlusion settings were accepted.");}
    auto value=defaults;value.minimumOpacity=1.01f;require(!validPlayerOcclusionSettings(value),"Opacity above one was accepted.");
    value=defaults;value.fadeStartDistance=1000.01f;require(!validPlayerOcclusionSettings(value),"Fade range above 1000 metres was accepted.");
    value=defaults;value.fadeStartDistance=value.fullFadeDistance;require(!validPlayerOcclusionSettings(value),"Empty distance range was accepted.");
    for(const auto field:{&PlayerOcclusionSettings::fadeOutSeconds,&PlayerOcclusionSettings::fadeInSeconds,&PlayerOcclusionSettings::restoreDelaySeconds}) {
        value=defaults;value.*field=10.01f;require(!validPlayerOcclusionSettings(value),"Fade timing above ten seconds was accepted.");
    }
    value={1,1000,0,10,10,10};require(validPlayerOcclusionSettings(value)&&value!=defaults,"Supported settings endpoints were rejected.");
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.75f})),model(21,math::translation({0,0,2})),model(22,math::translation({0,0,3}))};
    PlayerOcclusionSettings settings{.35f,2.5f,1,0,0,0};warm(resources,fade,world,models,query,camera(),1,settings);
    const auto& initial=fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings);
    near(opacity(initial,20),.35f,"Configured minimum/full-fade distance or zero fade-out was ignored.");
    require(opacity(initial,21)>.35f&&opacity(initial,21)<1&&!initial.contains(22)&&fade.stats().boundsCandidates==2,
        "Configured distance range did not limit visibility work.");
    const auto loads=resources.stats().loads;
    settings.fadeOutSeconds=.5f;settings.fadeInSeconds=.5f;settings.minimumOpacity=.8f;
    near(opacity(fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings),20),.8f,
        "Raising Minimum Opacity left an existing fade below the new minimum.");
    settings.fadeOutSeconds=settings.fadeInSeconds=0;settings.minimumOpacity=.2f;settings.fadeStartDistance=6;
    models.push_back(model(23,math::translation({0,0,5})));
    const auto& extended=fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings);
    near(opacity(extended,20),.2f,"Live minimum/zero-smoothing edits did not immediately change the existing fade.");
    require(opacity(extended,23)<1,"Configured start distance did not extend the original-triangle sight probes beyond four metres.");
    settings.minimumOpacity=0;near(opacity(fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings),20),0,"Zero Minimum Opacity did not fully fade the occluder.");
    settings.minimumOpacity=1;require(fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings).empty(),"Minimum Opacity one retained transparent instances.");
    settings.minimumOpacity=.2f;settings.fadeStartDistance=.4f;settings.fullFadeDistance=.1f;const auto calls=query.calls;
    require(fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings).empty()&&query.calls==calls,
        "A shorter live range still queried distant meshes.");
    settings.fadeStartDistance=4;settings.fullFadeDistance=.5f;settings.minimumOpacity=.4f;
    fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings);
    auto invalid=settings;invalid.fadeStartDistance=invalid.fullFadeDistance;const auto invalidCalls=query.calls;
    require(fade.update(world.scene,models,camera(),1,1,query.bounds(),query.triangles(),invalid).empty()&&query.calls==invalidCalls&&fade.stats().triangleTests==0,
        "Invalid runtime settings advanced visibility work or left a stale fade.");
    fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings);
    require(resources.stats().loads==loads&&fade.stats().pendingMeshes==0,"Live/invalid settings edits rebuilt immutable source BVHs.");
}
void configuredTimingAndInstantRestore() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall());
    std::vector models{model(20,math::translation({0,0,.3f}))};PlayerOcclusionSettings settings{.3f,4,.5f,.2f,.4f,.25f};
    warm(resources,fade,world,models,query,camera(),1,settings);
    const auto approaching=opacity(fade.update(world.scene,models,camera(),1,.1f,query.bounds(),query.triangles(),settings),20);
    require(approaching>.65f&&approaching<.8f,"Configured fade-out timing was ignored.");
    fade.update(world.scene,models,camera(),1,2,query.bounds(),query.triangles(),settings);
    auto clear=camera();clear.position[0]=80;
    near(opacity(fade.update(world.scene,models,clear,1,.2f,query.bounds(),query.triangles(),settings),20),.3f,"Configured restore delay was ignored.");
    const auto restored=opacity(fade.update(world.scene,models,clear,1,.2f,query.bounds(),query.triangles(),settings),20);
    require(restored>.45f&&restored<.6f,"Configured fade-in timing or split release interval was ignored.");
    require(fade.update(world.scene,models,clear,1,4,query.bounds(),query.triangles(),settings).empty(),"Configured restoration did not settle exactly opaque.");
    settings.fadeOutSeconds=settings.fadeInSeconds=settings.restoreDelaySeconds=0;
    near(opacity(fade.update(world.scene,models,camera(),1,0,query.bounds(),query.triangles(),settings),20),.3f,"Zero fade-out did not act immediately.");
    require(fade.update(world.scene,models,clear,1,0,query.bounds(),query.triangles(),settings).empty(),"Zero delay/fade-in did not restore immediately.");
}
void repeatedQueries() {
    ResourceManager resources(config());PlayerOcclusion fade(resources);World world;Query query(wall(false,50));
    std::vector<ExtractedModel> models;models.reserve(1000);
    for(unsigned i=0;i<1000;++i)models.push_back(model(100+i,math::translation({i<5?0.f:100.f+4*i,0,.3f+.6f*(i%5)})));
    warm(resources,fade,world,models,query);const auto loads=resources.stats().loads;const auto calls=query.calls;
    constexpr unsigned frames=200;const auto started=std::chrono::steady_clock::now();
    for(unsigned i=0;i<frames;++i)fade.update(world.scene,models,camera(),1,1.f/120,query.bounds(),query.triangles());
    const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()/frames;
    require(resources.stats().loads==loads&&loads==1&&query.calls==calls+frames,"Warm repeated visibility queries rebuilt or duplicated source geometry.");
    require(fade.stats().boundsCandidates==5&&fade.stats().triangleTests<3000,"The broadphase/BVH scanned distant instances or most source triangles.");
    std::cout<<std::fixed<<std::setprecision(3)<<"Player occlusion: 1000 instances, 5000 source triangles, "<<frames<<" warm frames: "<<milliseconds
        <<" ms/update; "<<fade.stats().boundsCandidates<<" candidates, "<<fade.stats().nodeTests<<" BVH nodes, "<<fade.stats().triangleTests<<" triangle tests/frame.\n";
}
}
int main(){try{explicitParticipation();hiddenVisualsDoNotFade();originalsDoorwayAndBroadphase();allOccludersAndDistance();affinePresentationAndPlayerExclusion();resetsAndSmoothRestore();smoothSpatialResponse();doorwayEdgeHysteresis();framePartitionIndependent();settingsValidationAndLiveTweaks();configuredTimingAndInstantRestore();repeatedQueries();
    std::cout<<"Player occlusion originals, doorway, affine transforms, distances, eased transitions, hysteresis, reset and repeated-query checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
