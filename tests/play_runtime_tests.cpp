#include "navigation_fixture.h"
#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include "scene/CameraAuthoring.h"
#include "scene/BuiltinModels.h"
#include "scene/SceneExtraction.h"
#include "scene/SceneOutlinerRows.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
using crimson::testing::startWithNavigation;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void fixture(SceneDocument& document) {
    require(document.edit("Fixture",[](auto& records,auto& properties){
        records.clear();EntityRecord floor;floor.id=1;floor.name="Floor";floor.collider=SceneCollider{{100,1,100}};floor.local=math::translation({0,-.5f,0});records.push_back(floor);
        EntityRecord player;player.id=2;player.name="Character";player.actor.emplace();player.controller.emplace();player.motor.emplace();player.navigation.emplace();player.playerInput.emplace();player.local=math::translation({0,.05f,0});records.push_back(player);
        EntityRecord visual;visual.id=3;visual.parent=2;visual.name="Visual";visual.local=math::translation({0,1,0});records.push_back(visual);
        properties.playerCharacter=2;
    }),"Cannot prepare runtime fixture.");
}
Camera camera(){Camera result;result.position={0,5,5};result.target={0,0,0};return result;}
float x(const Scene& scene){return scene.worldTransform(2)->at(0,3);}
void lifecycleAndEditing() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";
    SceneDocument authored(root);fixture(authored);const auto original=authored.scene().records();const auto historySize=authored.history().size();
    PlaySession play(root);require(startWithNavigation(play,authored),"Play did not clone authored data.");
    require(play.document().transient() && !play.document().dirty(),"Runtime did not start with independent clean history.");
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
    require(x(play.document().scene())>4.8f,"Runtime character did not move.");
    require(x(authored.scene())==0 && authored.history().size()==historySize,"Simulation changed authored transforms/history.");
    require(!play.document().dirty() && play.document().history().size()==0,"Simulation polluted runtime undo history.");
    require(!play.document().save(),"Runtime scene was saved to authored storage.");
    const auto child=play.document().scene().worldTransform(3);require(std::abs((*child)[12]-x(play.document().scene()))<1e-5,"Visual child did not inherit the resolved actor pose.");
    require(play.document().edit("Rename",[](auto& records,auto&){for(auto& r:records)if(r.id==2)r.name="Running";}),"Runtime explicit edit failed.");
    const float before=x(play.document().scene());play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
    require(x(play.document().scene())>before,"An unrelated edit reset simulation movement.");
    require(play.document().undo(),"Runtime undo failed.");require(x(play.document().scene())>before,"Undo of a name edit rolled movement back.");
    require(play.document().beginTransform("Teleport",{2}),"Transform transaction failed.");
    require(play.document().update([](auto& records,auto&){for(auto& r:records)if(r.id==2)r.local=math::translation({12,4,0});}),"Transform edit failed.");
    for(int i=0;i<8;++i)play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
    require(std::abs(x(play.document().scene())-12)<1e-5 && std::abs((*play.document().scene().worldTransform(2))[13]-4)<1e-5,"Physics fought an active transform gesture.");
    require(play.document().commit(),"Transform commit failed.");play.advance(PlaySession::FixedStep,Vec2{},camera());
    require((*play.document().scene().worldTransform(2))[13]>3.99f,"Teleport resumed stale gravity.");
    play.stop();require(!play.playing(),"Stop retained its runtime.");require(startWithNavigation(play,authored),"Restart failed.");require(std::abs(x(play.document().scene()))<1e-5,"Restart retained movement.");
    const auto final=authored.scene().records();require(final.size()==original.size(),"Stop changed authored entities.");
    for(std::size_t i=0;i<final.size();++i)require(sameEntity(final[i],original[i]),"Play modified authored records.");
}
void flatPlayerPresentationPreservesRuntime() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";
    SceneDocument authored(root);fixture(authored);
    const auto visualLocal=math::translation({.7f,.4f,-.2f})*math::eulerRotationXYZ({.2f,.4f,-.1f})*math::scaling({.7f,1.3f,.9f});
    const auto cameraSettings=defaultPlayerCameraTemplate();
    require(authored.edit("Owned player presentation",[&](auto& records,auto& properties){
        for(auto& record:records)if(record.id==3){record.model=AssetRef<ModelAsset>{BuiltinBoxModelId};record.local=visualLocal;}
        EntityRecord follow;follow.id=4;follow.parent=2;follow.name="Camera";follow.camera=cameraSettings.lens;
        follow.cameraFollow=SceneCameraFollow{2,cameraSettings.offset,cameraSettings.worldBasis};records.push_back(follow);
        EntityRecord attached;attached.id=5;attached.parent=1;attached.name="Attached light";attached.point.emplace();
        attached.local=math::translation({1,2,3});records.push_back(attached);
        properties.activeCamera=4;
    }),"Cannot prepare the flat Player runtime fixture.");
    const auto original=authored.scene().records();const auto roles=authored.scene().properties();
    const auto history=authored.history().size(),revision=authored.scene().revision();
    const auto rows=sceneOutlinerRows(authored.scene());
    require(rows.size()==4&&std::count_if(rows.begin(),rows.end(),[](const auto& row){return row.entity==2;})==1&&
        std::find(rows.begin(),rows.end(),SceneOutlinerRow{3,2})!=rows.end()&&
        std::find(rows.begin(),rows.end(),SceneOutlinerRow{5,1})!=rows.end(),
        "The Player projection hid a real visual or flattened a real attached-light parent.");
    require(sceneEntityOwner(authored.scene(),3)==3&&sceneEntityOwner(authored.scene(),4)==2&&
        sceneEntityParts(authored.scene(),2)==std::vector<EntityId>{2,3,4},
        "The flat Player lost its owned visual or Camera identity.");
    require(authored.scene().revision()==revision&&authored.history().size()==history&&sameProperties(authored.scene().properties(),roles),
        "Inspecting the flat Player changed runtime revisions, history or role IDs.");
    PlaySession play(root);require(play.start(authored),"The flat Player could not enter Play.");
    for(int tick=0;tick<30;++tick)play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
    const auto& simulated=play.document().scene();const auto actorWorld=*simulated.worldTransform(2);
    const auto expectedVisual=actorWorld*visualLocal,actualVisual=*simulated.worldTransform(3);
    for(unsigned field=0;field<16;++field)require(std::abs(expectedVisual[field]-actualVisual[field])<1e-5f,
        "Flat Player presentation changed its visual's local affine alignment during motion.");
    const auto actorPosition=math::transformPoint(actorWorld,{});
    const auto resolved=resolveGameCamera(simulated);require(resolved&&
        math::length(resolved->position-actorPosition-cameraSettings.offset)<1e-4,
        "Flat Player presentation changed the following Camera pose during motion.");
    const auto extracted=extractScene(simulated);
    require(extracted.models.size()==1&&extracted.models.front().actorOwner==2&&
        extracted.models.front().lightingCategory==lighting_scope::Character,
        "Flat Player presentation changed Character lighting ownership.");
    require(sceneOutlinerRows(play.presentationScene())==rows&&!play.document().dirty()&&play.document().history().size()==0,
        "Play interpolation changed the flat entity identities or authored simulation history.");
    play.stop();const auto after=authored.scene().records();require(after.size()==original.size(),"Play changed the authored Player records.");
    for(std::size_t index=0;index<after.size();++index)require(sameEntity(after[index],original[index]),"Play changed an authored Player part.");
    require(authored.history().size()==history&&sameProperties(authored.scene().properties(),roles),"Play changed authored Player history or roles.");
    authored.select({2},2);require(authored.duplicateSelection(),"The consolidated Player could not be duplicated.");
    const auto copied=authored.selection().primary,copiedCamera=resolveOwnedActorCamera(authored.scene(),copied);
    const auto copiedParts=sceneEntityParts(authored.scene(),copied);
    require(copied!=2&&copiedParts.size()==3&&copiedCamera&&copiedCamera!=4&&
        authored.scene().find(copiedCamera)->cameraFollow->target==copied,
        "Duplicating the flat Player lost or shared an owned part.");
    require(sceneOutlinerRows(authored.scene()).size()==rows.size()+2&&sameProperties(authored.scene().properties(),roles),
        "Duplicating Player hid its real visual row, exposed a duplicate Camera row or stole scene roles.");
    authored.select({copied},copied);require(authored.eraseSelection()&&sceneOutlinerRows(authored.scene())==rows,
        "Deleting a flat Player left its owned parts in the Outliner.");
    require(authored.scene().contains(2)&&authored.scene().contains(3)&&authored.scene().contains(4),
        "Deleting the duplicate changed the original Player's parts.");
}
void fixedTimingAndInterpolation() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    PlaySession a(root),b(root);require(startWithNavigation(a,authored) && startWithNavigation(b,authored),"Cannot start timing runtimes.");
    for(int i=0;i<60;++i)a.advance(1.0/60.0,Vec2{1,0},camera());
    for(int i=0;i<240;++i)b.advance(1.0/240.0,Vec2{1,0},camera());
    require(a.tickCount()==60 && b.tickCount()==60 && std::abs(x(a.document().scene())-x(b.document().scene()))<1e-4,"Render frame size changed fixed simulation results.");
    const float completed=x(a.document().scene());a.advance(PlaySession::FixedStep*.5,Vec2{},camera());
    require(x(a.presentationScene())<completed && x(a.presentationScene())>completed-.1f,"Presentation did not interpolate completed actor poses.");
    const auto before=a.tickCount();a.advance(10,Vec2{1,0},camera());require(a.tickCount()==before+8 && a.droppedTicks()>0,"Catch-up was not bounded.");
    a.advance(5,Vec2{1,0},camera(),false);const float suspended=x(a.document().scene());const auto suspendedTicks=a.tickCount();
    a.advance(5,Vec2{1,0},camera(),true);require(a.tickCount()==suspendedTicks && x(a.document().scene())==suspended,"Focus resume replayed suspended wall time.");
    SceneDocument turning(root);fixture(turning);
    constexpr double radians=std::numbers::pi/180;
    turning.edit("Facing",[](auto& records,auto&){for(auto& record:records)if(record.id==2)record.local=math::translation({0,.05f,0})*math::rotation(math::axisAngle({0,1,0},179*radians));});
    PlaySession turn(root);require(startWithNavigation(turn,turning),"Cannot start turning runtime.");
    Camera look;look.position={0,2,0};look.target={-std::sin(-179*radians),2,-std::cos(-179*radians)};
    turn.advance(PlaySession::FixedStep*1.5,Vec2{0,1},look);
    require((*turn.presentationScene().worldTransform(2))[10]<-.99f,"Yaw interpolation took the long path across the angle wrap.");
}
void particlesUseRuntimeClock() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    const auto effect=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());effect->id="assets/particles/test.crimson-particle";effect->emitter.rate=60;
    authored.edit("Emitter",[&](auto& records,auto&){EntityRecord emitter;emitter.id=4;emitter.parent=2;emitter.name="Emitter";emitter.particle=AssetRef<ParticleEffectAsset>{effect->id};records.push_back(emitter);});
    PlaySession play(root);play.setParticleDefinitions([effect](const std::string&){return ParticleDefinitionVersion{effect,1,{}};});require(startWithNavigation(play,authored),"Cannot start particle runtime.");
    for(int i=0;i<10;++i)play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
    const auto* particle=play.particles().instance(4);require(particle && std::abs(particle->elapsed()-10*PlaySession::FixedStep)<1e-8,"Runtime effects did not share the fixed clock.");
    require(particle->particles().back().position[0]>.5f,"Child emitter births did not follow the moving parent.");
    play.stop();require(!play.particles().instance(4),"Stop retained transient particles.");
}
void particlePreprocessIsReadyBeforePlayReturns() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    auto effect=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());effect->id="assets/particles/fire.crimson-particle";
    effect->preprocessSeconds=2;effect->emitter.rate=60;effect->emitter.lifetime=2;effect->emitter.initialVelocity={0,2,0};
    require(authored.edit("Preprocessed child emitter",[&](auto& records,auto&){
        EntityRecord emitter;emitter.id=4;emitter.parent=2;emitter.name="Fire";emitter.local=math::translation({3,2,-1});
        emitter.particle=AssetRef<ParticleEffectAsset>{effect->id};records.push_back(emitter);
    }),"Cannot prepare preprocessed Play fixture.");
    PlaySession play(root);play.setParticleDefinitions([effect](const std::string&){return ParticleDefinitionVersion{effect,1,{}};});
    require(startWithNavigation(play,authored),"Cannot start preprocessed particle runtime.");
    const auto* warmed=play.particles().instance(4);
    require(warmed && warmed->stats().ticks==120 && !play.particles().presentation().empty() && play.tickCount()==0,
        "Play returned before two seconds of fire pre-process were ready for the first Game image.");
    auto ordinary=*effect;ordinary.preprocessSeconds=0;CpuParticleSystem reference;reference.restart(ordinary,ordinary.defaultSeed,*authored.scene().worldTransform(4));
    for(int tick=0;tick<120;++tick)reference.tick();
    require(warmed->particles().size()==reference.particles().size() && warmed->elapsed()==reference.elapsed(),"Play pre-process did not reproduce explicit fixed ticks.");
    for(std::size_t i=0;i<warmed->particles().size();++i)require(warmed->particles()[i].position==reference.particles()[i].position &&
        warmed->particles()[i].velocity==reference.particles()[i].velocity,"Play pre-process lost the authored child world transform or force integration.");
    const std::vector<ParticlePresentation> firstImage(play.particles().presentation().begin(),play.particles().presentation().end());
    play.advance(0,Vec2{},camera());require(warmed->stats().ticks==120 && play.particles().presentation().size()==firstImage.size(),"First zero-delta Play frame restarted or hid warmed particles.");
    play.advance(PlaySession::FixedStep,Vec2{1,0},camera());require(warmed->stats().ticks==121 && !play.document().dirty() && !play.document().history().size(),
        "Ready preprocessed particles did not join the ordinary Play clock cleanly.");
    play.stop();require(startWithNavigation(play,authored),"Cannot restart preprocessed Play session.");
    require(play.particles().instance(4)->stats().ticks==120 && play.particles().presentation().size()==firstImage.size(),"Stop/Play did not immediately reproduce the warm initial presentation.");
    for(std::size_t i=0;i<firstImage.size();++i)require(firstImage[i].position==play.particles().presentation()[i].position &&
        firstImage[i].spawnOrdinal==play.particles().presentation()[i].spawnOrdinal,"Stop/Play changed the seeded initial fire state.");play.stop();

    SceneDocument many(root);fixture(many);effect->emitter.capacity=32;
    require(many.edit("Many preprocessed emitters",[&](auto& records,auto&){for(EntityId id=100;id<116;++id){
        EntityRecord emitter;emitter.id=id;emitter.name="Fire";emitter.particle=AssetRef<ParticleEffectAsset>{effect->id};records.push_back(emitter);
    }}),"Cannot prepare many-emitter Play fixture.");
    require(startWithNavigation(play,many),"Cannot start many-emitter pre-process runtime.");
    const auto replayTicks=[&]{std::uint64_t ticks{};for(EntityId id=100;id<116;++id){const auto* instance=play.particles().instance(id);require(instance!=nullptr,"Play omitted a bounded emitter.");ticks+=instance->stats().ticks;}return ticks;};
    require(replayTicks()==16u*120u && play.particles().groups().size()==16,"Play left later emitters blank on their first presentation.");
    for(const auto& group:play.particles().groups())require(group.count>0,"An admitted emitter had no ready particles on first Play entry.");
    play.advance(.2,Vec2{},camera());require(replayTicks()==16u*128u && play.tickCount()==8,"Play repeated warm-up during catch-up instead of joining the ordinary clock.");
    for(EntityId id=100;id<116;++id)require(play.particles().instance(id)->stats().droppedCatchUpTicks==0,"Synchronous pre-process was counted as discarded wall time.");
    play.stop();require(!play.particles().instance(100),"Leaving Play retained preprocessed transient state.");
}
void followCameraTransformLocks() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    require(authored.edit("Following camera fixture",[](auto& records,auto& properties){
        EntityRecord follow;follow.id=4;follow.name="Follow camera";follow.camera.emplace();
        // Following a visual child also depends on that visual's Character ancestor.
        follow.cameraFollow.emplace();follow.cameraFollow->target=3;follow.cameraFollow->offset={0,5,5};records.push_back(follow);properties.activeCamera=4;
        EntityRecord child;child.id=5;child.parent=4;child.name="Camera child";child.local=math::translation({0,2,0});records.push_back(child);
        EntityRecord other;other.id=6;other.name="Unrelated falling actor";other.actor.emplace();other.controller.emplace();other.motor.emplace();other.local=math::translation({8,100,0});records.push_back(other);
        EntityRecord chained;chained.id=7;chained.name="Chained follow camera";chained.camera.emplace();chained.cameraFollow.emplace();chained.cameraFollow->target=5;chained.cameraFollow->offset={3,3,3};records.push_back(chained);
    }),"Cannot prepare following-camera lock fixture.");
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start camera-lock runtime.");
    for(const auto edited:std::array<EntityId,3>{4,5,7}) {
        play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
        const auto playerBefore=*play.document().scene().worldTransform(2);
        const float otherBefore=(*play.document().scene().worldTransform(6))[13];
        require(play.document().beginTransform("Move following camera dependency",{edited}),"Cannot begin camera dependency edit.");
        require(play.document().update([edited](auto& records,auto&){for(auto& record:records)if(record.id==edited) {
            if(record.cameraFollow)record.cameraFollow->offset[0]+=1;
            else record.local[12]+=1;
        }}),"Cannot update camera dependency edit.");
        const auto editedWorld=*play.document().scene().worldTransform(edited);
        for(int tick=0;tick<12;++tick)play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
        const auto playerAfter=*play.document().scene().worldTransform(2),editedAfter=*play.document().scene().worldTransform(edited);
        require(math::length(math::transformPoint(playerAfter,{})-math::transformPoint(playerBefore,{}))<1e-5,"Editing a follow Camera or its child did not hold the dependent Character.");
        require(math::length(math::transformPoint(editedAfter,{})-math::transformPoint(editedWorld,{}))<1e-5,"Captured camera world-space baseline drifted during transform editing.");
        require((*play.document().scene().worldTransform(6))[13]<otherBefore-.001f,"Camera dependency lock paused an unrelated actor.");
        require(play.document().commit(),"Cannot commit camera dependency edit.");
        play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
        require(x(play.document().scene())>playerAfter[12]+.05f,"Character did not resume after camera dependency edit commit.");
    }
}
Vec2 screenPoint(const Camera& view,const ViewportRegion& viewport,const Vec3& point) {
    const auto projected=math::project(point,view.projectionMatrix(viewport.width/viewport.height,ClipDepth::ZeroToOne)*view.viewMatrix(),
        {viewport.x,viewport.y,viewport.width,viewport.height},ClipDepth::ZeroToOne);
    require(projected.has_value(),"Cannot project pointer test destination.");return {(*projected)[0],(*projected)[1]};
}
void pointerMovementAndFacing() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start pointer runtime.");
    auto view=camera();view.position={0,10,8};view.fovAxis=CameraFovAxis::Vertical;
    const ViewportRegion viewport{100,200,800,600,true,false,9};GameInput input;input.setViewport(viewport,true,false);input.acquire();
    input.submitPointer(screenPoint(view,viewport,{3,0,0}),true);
    play.advance(1.0/240,input,view);require(play.tickCount()==0,"Sub-tick click fixture executed too early.");
    for(int i=0;i<240;++i)play.advance(1.0/240,input,view);
    require(std::abs(x(play.document().scene())-3)<.06f,"A short click did not keep moving to its destination and stop.");
    const auto stopped=*play.document().scene().worldTransform(2);
    input.submitPointer(screenPoint(view,viewport,{-3,0,0}),false);
    play.advance(PlaySession::FixedStep,input,view);
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    const auto facing=math::transformDirection(*play.document().scene().worldTransform(2),{0,0,-1});
    const auto stoppedFacing=math::transformDirection(stopped,{0,0,-1});
    require(math::length(facing-stoppedFacing)<1e-5 && std::abs(x(play.document().scene())-stopped[12])<1e-4,"Hovering changed idle facing or the clicked destination.");
    require(play.lastClickedGround() && math::length(*play.lastClickedGround()-Vec3{3,0,0})<.001,"Arrival discarded the debug click point.");
    input.submitPointer(screenPoint(view,viewport,{7,0,0}),true);play.advance(.2,input,view);
    const float released=x(play.document().scene());input.cancel();input.acquire();
    play.advance(.1,input,view);
    require(std::abs(x(play.document().scene())-released)<1e-4,"Reacquiring Game replayed a destination canceled by focus loss.");
    SDL_Event oldKey{};oldKey.type=SDL_EVENT_KEY_DOWN;oldKey.key.windowID=9;oldKey.key.scancode=SDL_SCANCODE_W;
    require(!input.handleEvent(oldKey),"WASD remained an active gameplay binding.");
    play.advance(.1,input,view);require(std::abs(x(play.document().scene())-released)<1e-4,"WASD moved the click-controlled player.");

    PlaySession a(root),b(root);require(startWithNavigation(a,authored)&&startWithNavigation(b,authored),"Cannot start pointer timing runtimes.");
    GameInput first,second;first.setViewport(viewport,true,false);second.setViewport(viewport,true,false);first.acquire();second.acquire();
    const auto far=screenPoint(view,viewport,{8,0,0});first.submitPointer(far,true);second.submitPointer(far,true);
    for(int i=0;i<60;++i)a.advance(1.0/60,first,view);
    for(int i=0;i<240;++i)b.advance(1.0/240,second,view);
    require(std::abs(x(a.document().scene())-x(b.document().scene()))<1e-4,"Pointer control changed with render frame rate.");
}
void pointerCollisionAndNoFloor() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Wall",[](auto& records,auto&){EntityRecord wall;wall.id=9;wall.name="Wall";wall.collider=SceneCollider{{.2f,2,4}};wall.local=math::translation({1,1,0});records.push_back(wall);});
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start click collision runtime.");
    auto view=camera();view.position={0,10,8};const ViewportRegion viewport{0,0,800,600,true,false,9};
    GameInput input;input.setViewport(viewport,true,false);input.acquire();input.submitPointer(screenPoint(view,viewport,{4,0,0}),true);
    double detour=0;
    for(int i=0;i<240;++i){play.advance(PlaySession::FixedStep,input,view);detour=std::max(detour,std::abs(static_cast<double>((*play.document().scene().worldTransform(2))[14])));}
    require(detour>2.3 && std::abs(x(play.document().scene())-4)<.02f,"Clicked movement did not follow a safe path around the wall and arrive.");
    const auto navigationRevision=play.navigationRevision();
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.navigationRevision()==navigationRevision,"Simulation poses rebuilt the cached navigation mesh.");
    play.document().edit("Remove wall",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==9;});});
    require(testing::waitForNavigation(play,input,view),"Wall deletion tiles did not finish preparing.");
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.navigationRevision()>navigationRevision,"Live wall deletion did not publish replacement navigation.");
    input.submitPointer(screenPoint(view,viewport,{-4,0,0}),true);play.advance(PlaySession::FixedStep,input,view);
    const auto beforeFloorDeletion=x(play.document().scene());
    play.document().edit("Remove support",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.collider.has_value();});});
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(std::abs(x(play.document().scene())-beforeFloorDeletion)<1e-4 && play.path().empty() &&
        !play.destination() && (*play.document().scene().worldTransform(2))[13]<-3,
        "Deleting support geometry retained a stale route or invented a floor.");
    authored.edit("No floor",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.collider.has_value();});});
    play.stop();require(startWithNavigation(play,authored),"Cannot restart without colliders.");input.cancel();input.acquire();input.submitPointer(screenPoint(view,viewport,{4,0,0}),true);
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(std::abs(x(play.document().scene()))<1e-5 && (*play.document().scene().worldTransform(2))[13]<-3,"Clicking empty space supplied movement or an implicit floor.");
}
void unreachableAndReplanning() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Divided floor",[](auto& records,auto&){
        for(auto& record:records)if(record.id==1)record.collider->size={20,1,20};
        EntityRecord wall;wall.id=9;wall.name="Wall";wall.collider=SceneCollider{{.2f,2,24}};
        wall.local=math::translation({1,1,0});records.push_back(wall);
    });
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start unreachable runtime.");
    auto view=camera();view.position={0,10,8};const ViewportRegion viewport{0,0,800,600,true,false,9};
    GameInput input;input.setViewport(viewport,true,false);input.acquire();input.submitPointer(screenPoint(view,viewport,{4,0,0}),true);
    for(int i=0;i<30;++i)play.advance(PlaySession::FixedStep,input,view);
    require(x(play.document().scene())>.1f && x(play.document().scene())<.65f && play.path().empty() && !play.destination(),
        "Blocked ground click did not approach and stop safely on this side of the wall.");
    require(play.lastClickedGround() && math::length(*play.lastClickedGround()-Vec3{4,0,0})<.001,"Unreachable ground click lost its debug hit point.");
    play.document().edit("Open route",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==9;});});
    require(testing::waitForNavigation(play,input,view),"Opened route tiles did not finish preparing.");
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(std::abs(x(play.document().scene())-4)<.02f,"Live collider deletion did not replan the retained destination.");
    input.submitPointer(screenPoint(view,viewport,{-4,0,0}),true);play.advance(PlaySession::FixedStep,input,view);
    play.document().edit("Block active route",[](auto& records,auto&){EntityRecord wall;wall.id=9;wall.name="Wall";
        wall.collider=SceneCollider{{.2f,2,24}};wall.local=math::translation({1,1,0});records.push_back(wall);});
    const float before=x(play.document().scene());
    require(testing::waitForNavigation(play,input,view),"Blocked route tiles did not finish preparing.");
    for(int i=0;i<30;++i)play.advance(PlaySession::FixedStep,input,view);
    require(x(play.document().scene())<before-.5f && x(play.document().scene())>1.4f && x(play.document().scene())<2 && play.path().empty(),
        "Inserted collider did not replace the active route with a safe approach point.");
    input.cancel();input.acquire();play.advance(PlaySession::FixedStep,input,view);
    require(!play.destination() && play.lastClickedGround(),"Focus cancellation erased the debug marker or retained movement.");
}
void heldTargetsAndQueryBudget() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Small floor",[](auto& records,auto&){for(auto& record:records)if(record.id==1)record.collider->size={20,1,20};});
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start held-target runtime.");
    auto view=camera();view.position={0,10,8};view.fovAxis=CameraFovAxis::Vertical;
    const ViewportRegion viewport{0,0,800,600,true,false,9};GameInput input;input.setViewport(viewport,true,false);input.acquire();input.setProjection(view,800,600);
    input.submitUiPointer(screenPoint(view,viewport,{3,0,0}),true,true);
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(std::abs(x(play.document().scene())-3)<.06f && !play.destination(),"Holding a stationary ground point did not arrive and stop.");
    const auto navigationRevision=play.navigationRevision();
    require(play.routeQueryCount()==1,"Stationary held target repeated Detour route queries.");
    input.submitUiPointer(screenPoint(view,viewport,{3.02f,0,0}),false,true);
    for(int i=0;i<6;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.routeQueryCount()==1 && play.lastClickedGround() && std::abs((*play.lastClickedGround())[0]-3.02f)<.001f,"Sub-threshold held motion queried a new path or failed to update the ground marker.");
    input.submitUiPointer(screenPoint(view,viewport,{-3,0,2}),false,true);
    for(int i=0;i<3;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.routeQueryCount()==2 && play.destination() && math::length(*play.destination()-Vec3{-3,0,2})<.001,"Moving a held cursor did not replace the accepted destination.");
    const auto before=play.routeQueryCount();
    for(int frame=0;frame<120;++frame) {
        input.submitUiPointer(screenPoint(view,viewport,{frame%2?4.f:5.f,0,-3}),false,true);
        play.advance(PlaySession::FixedStep,input,view);
    }
    require(play.routeQueryCount()>before+20 && play.routeQueryCount()<=before+40,"Held retargeting was not bounded to 20 route queries per second.");
    require(play.navigationRevision()==navigationRevision,"Held cursor motion rebuilt unchanged navigation geometry.");
    require(play.lastClickedGround().has_value(),"Held steering lost its latest accepted ground point.");
    const auto releasedTarget=*play.lastClickedGround();const auto releasedQueries=play.routeQueryCount();
    input.submitUiPointer(screenPoint(view,viewport,{-3,0,0}),false,false);
    for(int i=0;i<180;++i)play.advance(PlaySession::FixedStep,input,view);
    const auto arrived=math::transformPoint(*play.document().scene().worldTransform(2),{});
    require(std::hypot(arrived[0]-releasedTarget[0],arrived[2]-releasedTarget[2])<.06f && play.routeQueryCount()==releasedQueries,"Release canceled the last route or subsequent hover retargeted it.");
}
void heldStationaryCursorFollowsDisplayedProjection() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Small floor",[](auto& records,auto&){for(auto& record:records)if(record.id==1)record.collider->size={20,1,20};});
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start held-projection runtime.");
    auto view=camera();view.position={0,10,8};view.fovAxis=CameraFovAxis::Vertical;
    const ViewportRegion viewport{100,200,800,600,true,false,9};GameInput input;input.setViewport(viewport,true,false);input.acquire();input.setProjection(view,1600,1200);
    const auto cursor=screenPoint(view,viewport,{2,0,0});input.submitUiPointer(cursor,true,true);
    play.advance(PlaySession::FixedStep,input,view);
    require(play.destination() && math::length(*play.destination()-Vec3{2,0,0})<.001,"Initial held press lost its original displayed world point.");
    auto moved=view;moved.position=moved.position+Vec3{2,0,0};moved.target=moved.target+Vec3{2,0,0};
    input.setProjection(moved,1600,1200);
    // Neither the cursor nor fallback camera moves. Only the image actually
    // displayed is different, exactly as with an interpolated follow camera.
    for(int i=0;i<3;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.destination() && math::length(*play.destination()-Vec3{4,0,0})<.001 && play.routeQueryCount()==2,"Stationary screen cursor did not retarget through the new displayed camera.");
    input.submitUiPointer(cursor,false,false);input.setProjection(view,1600,1200);
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(std::abs(x(play.document().scene())-4)<.06f && play.routeQueryCount()==2,"A camera change after release moved the last held destination.");
}
void heldProjectedTargetsAndMisses() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Divided floor",[](auto& records,auto&){
        for(auto& record:records) {
            if(record.id==1)record.collider->size={20,1,20};
            if(record.id==2)record.local=math::translation({-4,.05f,0});
        }
        EntityRecord wall;wall.id=9;wall.name="Wall";wall.collider=SceneCollider{{.2f,2,24}};wall.local=math::translation({1,1,0});records.push_back(wall);
    });
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start held-unreachable runtime.");
    auto view=camera();view.position={0,10,8};view.fovAxis=CameraFovAxis::Vertical;
    ViewportRegion viewport{0,0,800,600,true,false,9};GameInput input;input.setViewport(viewport,true,false);input.acquire();input.setProjection(view,800,600);
    input.submitUiPointer(screenPoint(view,viewport,{-5,0,0}),true,true);play.advance(PlaySession::FixedStep,input,view);
    input.submitUiPointer(screenPoint(view,viewport,{4,0,0}),false,true);
    for(int i=0;i<6;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.destination() && (*play.destination())[0]>.1f && (*play.destination())[0]<.65f && !play.path().empty(),
        "Blocked held ground did not replace the old route with a reachable approach point.");
    const auto approach=*play.destination();
    require(play.lastClickedGround() && math::length(*play.lastClickedGround()-Vec3{4,0,0})<.001 && play.routeQueryCount()==2,"Projected held ground lost its debug marker or repeated the same route query.");
    input.submitUiPointer({400,1},false,true);
    for(int i=0;i<6;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.destination() && math::length(*play.destination()-approach)<.001 && play.routeQueryCount()==2,"Empty-space held ray discarded or replaced the last accepted route.");
    viewport.hovered=false;input.setViewport(viewport,true,false);
    for(int i=0;i<6;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.destination() && input.ownsKeyboard() && play.routeQueryCount()==2,"Leaving the image canceled the accepted route or continued retargeting.");
    viewport.hovered=true;input.setViewport(viewport,true,false);input.submitUiPointer(screenPoint(view,viewport,{-4,0,0}),false,true);
    for(int i=0;i<3;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.destination() && math::length(*play.destination()-Vec3{-4,0,0})<.001 && play.routeQueryCount()==3,"Reentering the image failed to resume the owned held gesture.");
}
void obstacleSurfaceClicks() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Box obstacle",[](auto& records,auto&){
        for(auto& record:records)if(record.id==1)record.collider->size={20,1,20};
        EntityRecord box;box.id=9;box.name="Box";box.collider=SceneCollider{{2,2,2}};
        box.local=math::translation({4,1,0});records.push_back(box);
    });
    auto view=camera();view.position={0,10,8};view.fovAxis=CameraFovAxis::Vertical;
    const ViewportRegion viewport{100,200,800,600,true,false,9};
    for(const auto target:{Vec3{4,1,1},Vec3{4,2,0}}) for(const bool held:{false,true}) {
        PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start obstacle click runtime.");
        GameInput input;input.setViewport(viewport,true,false);input.acquire();input.setProjection(view,1600,1200);
        input.submitUiPointer(screenPoint(view,viewport,target),true,held);play.advance(PlaySession::FixedStep,input,view);
        require(play.destination() && !play.path().empty(),"Clicking a box wall or roof did not create an approach route.");
        const auto endpoint=*play.destination();
        require(endpoint[1]<.15f && (endpoint[0]<2.7f || endpoint[0]>5.3f || std::abs(endpoint[2])>1.3f),
            "Box click selected its disconnected roof or an endpoint inside the capsule clearance.");
        require(play.lastClickedGround() && math::length(*play.lastClickedGround()-target)<.001,
            "Projected box click changed the debug hit away from the actual surface.");
        for(int i=0;i<150;++i)play.advance(PlaySession::FixedStep,input,view);
        const auto position=math::transformPoint(*play.document().scene().worldTransform(2),{});
        require(std::hypot(position[0]-endpoint[0],position[2]-endpoint[2])<.04f && position[1]<.1f && !play.destination(),
            "Character did not reach the box approach point and stop on the floor.");
        require(play.routeQueryCount()==1,"Stationary held box click repeatedly replanned after reaching its approach point.");
        input.submitUiPointer(screenPoint(view,viewport,target),false,false);
        const auto stopped=position;for(int i=0;i<15;++i)play.advance(PlaySession::FixedStep,input,view);
        require(math::length(math::transformPoint(*play.document().scene().worldTransform(2),{})-stopped)<.001,
            "Releasing an arrived box click restarted movement.");
    }
}
void directionalInputRespectsRoleAndComponentChanges() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    require(authored.edit("Other input-capable actor",[](auto& records,auto&){
        EntityRecord actor;actor.id=4;actor.name="Other actor";actor.actor.emplace();actor.controller.emplace();actor.motor.emplace();
        actor.playerInput.emplace();actor.local=math::translation({0,.05f,4});records.push_back(actor);
    }),"Cannot create directional ownership fixture.");
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start directional ownership fixture.");
    const auto position=[&](EntityId id){return math::transformPoint(*play.document().scene().worldTransform(id),{});};
    for(int i=0;i<10;++i)play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
    require(position(2)[0]>.7f,"Directional player input did not start.");
    require(play.document().edit("Disable Player Input",[](auto& records,auto&){for(auto& r:records)if(r.id==2)r.playerInput->enabled=false;}),
        "Cannot disable directional player input.");
    const auto stopped=position(2);for(int i=0;i<10;++i)play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
    require(std::abs(position(2)[0]-stopped[0])<.001,"Disabling Player Input retained a directional command.");
    require(play.document().edit("Enable Player Input",[](auto& records,auto&){for(auto& r:records)if(r.id==2)r.playerInput->enabled=true;}),
        "Cannot enable directional player input.");
    play.advance(PlaySession::FixedStep,Vec2{1,0},camera());const auto old=position(2);
    require(play.document().setPlayerCharacter(4),"Cannot switch directional player.");
    for(int i=0;i<10;++i)play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
    require(std::abs(position(2)[0]-old[0])<.001&&position(4)[0]>.7f,"Switching the player retained the old actor's directional command.");
}
void composedActorsShareNavigationAndKeepIndependentCommands() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);
    require(authored.edit("Multiple composed actors",[](auto& records,auto& properties){
        records.clear();properties={};EntityRecord floor;floor.id=1;floor.name="Floor";
        floor.collider=SceneCollider{{40,1,40}};floor.local=math::translation({0,-.5f,0});records.push_back(floor);
        for(EntityId id:{2,4,5,6}) {
            EntityRecord actor;actor.id=id;actor.name="Actor "+std::to_string(id);actor.actor.emplace();actor.controller.emplace();
            actor.motor.emplace();actor.navigation.emplace();actor.local=math::translation({-4,.05f,id==2?-5.f:id==4?2.f:id==5?-2.f:5.f});
            if(id==2)actor.playerInput.emplace();
            if(id==6)actor.controller->profile=testing::agentProfile({2,.6f,45,.3f});
            records.push_back(actor);
        }
        EntityRecord falling;falling.id=7;falling.name="Controller without Motor";falling.controller.emplace();
        falling.local=math::translation({7,3,0});records.push_back(falling);properties.playerCharacter=2;
    }),"Cannot prepare composed actor fixture.");
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start composed actors.");
    const auto initialBuilds=play.navigationBuildCount();
    require(play.navigationProfileCount()==2&&initialBuilds>0,"Compatible actors did not share navigation clearance data.");
    require(play.setActorDestination(4,{4,0,2})&&play.setActorDestination(5,{4,0,-2})&&play.setActorDestination(6,{3,0,5}),
        "Non-player actors could not request independent destinations.");
    require(play.actorDestination(4)!=play.actorDestination(5)&&play.actorPath(4).data()!=play.actorPath(5).data(),
        "Agents shared a destination or transient path allocation.");
    require(play.actorRouteQueryCount(4)==1&&play.actorRouteQueryCount(5)==1&&play.routeQueryCount()==0,
        "A non-player command mutated the player's route state.");
    require(play.setActorDestination(4,{4,0,2})&&play.actorRouteQueryCount(4)==1&&play.navigationBuildCount()==initialBuilds,
        "An unchanged external destination repeated path queries or rebuilt navigation.");
    require(!play.setActorDestination(7,{4,0,0})&&!play.setActorMovement(99,{1,0,0}),"Missing movement capabilities accepted actor commands.");
    const auto position=[&](EntityId id){return math::transformPoint(*play.document().scene().worldTransform(id),{});};
    for(int i=0;i<180;++i)play.advance(PlaySession::FixedStep,Vec2{},camera());
    require(std::hypot(position(4)[0]-4,position(4)[2]-2)<.06&&std::hypot(position(5)[0]-4,position(5)[2]+2)<.06&&
        std::hypot(position(6)[0]-3,position(6)[2]-5)<.06,"Independent agents did not arrive at their own destinations.");
    require(!play.actorDestination(4)&&!play.actorDestination(5)&&play.actorRemainingPath(6).empty(),"Arrived agents retained active steering paths.");
    require(position(2)[0]==-4&&position(7)[1]<.2f,"Non-player navigation moved the player or a controller-only body failed to fall.");
    require(play.navigationBuildCount()==initialBuilds,"Destinations or ordinary actor poses rebuilt shared navigation.");
    require(play.document().edit("Reuse an existing clearance profile",[](auto& records,auto&){
        AssetRef<NavigationAgentProfile> existing;
        for(const auto& record:records)if(record.id==6)existing=record.controller->profile;
        for(auto& record:records)if(record.id==5)record.controller->profile=existing;
    }),
        "Cannot edit actor clearance.");
    play.advance(0,Vec2{},camera());
    require(play.navigationProfileCount()==2&&play.navigationBuildCount()==initialBuilds,"Changing to an existing compatible profile rebuilt its navigation mesh.");
    require(play.setActorMovement(4,{100,0,0}),"Non-player direct movement was rejected.");const auto before=position(4);
    for(int i=0;i<30;++i)play.advance(PlaySession::FixedStep,Vec2{},camera());
    require(position(4)[0]>before[0]+2.3f&&position(4)[0]<before[0]+2.7f,"Direct movement ignored the Motor's speed limit.");
    require(play.clearActorMovement(4),"Could not stop direct actor movement.");const auto stopped=position(4);
    for(int i=0;i<10;++i)play.advance(PlaySession::FixedStep,Vec2{},camera());
    require(math::length(position(4)-stopped)<.001,"Cleared actor movement replayed a stale command.");

    GameInput input;input.setViewport({0,0,800,600,true,false,9},true,false);input.acquire();input.setProjection(camera(),800,600);
    play.advance(0,input,camera());
    require(!play.setActorMovement(2,{1,0,0})&&!play.setActorDestination(2,{2,0,-5})&&!play.clearActorMovement(2),
        "An external command overrode the Game input owner.");
    require(play.setActorMovement(5,{0,0,1}),"Player input blocked commands for a different actor.");
    const auto independent=position(5);for(int i=0;i<15;++i)play.advance(PlaySession::FixedStep,input,camera());
    require(position(5)[2]>independent[2]+.2f&&position(5)[2]<independent[2]+.3f,
        "Local input changed another actor's requested one-metre-per-second velocity.");
    input.cancel();play.advance(0,input,camera());require(play.setActorMovement(2,{1,0,0}),"Released Game input did not release command ownership.");
    const auto released=position(2);for(int i=0;i<15;++i)play.advance(PlaySession::FixedStep,input,camera());
    require(position(2)[0]>released[0]+.2f,"External movement required enabled native input ownership.");
    require(play.clearActorMovement(2)&&play.clearActorMovement(5),"Could not clear independent runtime commands.");
    require(play.document().edit("Disable movement components",[](auto& records,auto&){
        for(auto& r:records){if(r.id==4)r.motor->enabled=false;if(r.id==5)r.navigation->enabled=false;}
    }),"Cannot disable actor components.");
    require(!play.setActorMovement(4,{1,0,0})&&!play.setActorDestination(5,{0,0,0})&&play.setActorMovement(5,{1,0,0}),
        "Disabled Motor/Navigation Agent capabilities were not respected independently.");
    require(play.document().edit("Change static navigation geometry",[](auto& records,auto&){for(auto& r:records)if(r.id==1)r.collider->size={42,1,40};}),
        "Cannot edit shared navigation geometry.");
    play.advance(0,Vec2{},camera());
    require(testing::waitForNavigation(play)&&play.navigationBuildCount()>initialBuilds,"A world change did not rebuild the affected retained clearance tiles.");
    require(play.document().transient()&&authored.scene().find(1)->collider->size==Vec3{40,1,40}&&
        authored.scene().find(4)->motor->enabled&&authored.scene().find(5)->navigation->enabled,
        "Runtime component edits changed the authored actors or geometry.");
}
void heldEditsRequireFreshPress() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Edit fixture",[](auto& records,auto&){
        for(auto& record:records)if(record.id==1)record.collider->size={20,1,20};
        EntityRecord other;other.id=4;other.name="Other Character";other.actor.emplace();other.controller.emplace();other.motor.emplace();other.navigation.emplace();other.playerInput.emplace();other.local=math::translation({-4,.05f,0});records.push_back(other);
    });
    PlaySession play(root);require(startWithNavigation(play,authored),"Cannot start held-edit runtime.");
    auto view=camera();view.position={0,10,8};view.fovAxis=CameraFovAxis::Vertical;
    const ViewportRegion viewport{0,0,800,600,true,false,9};GameInput input;input.setViewport(viewport,true,false);input.acquire();input.setProjection(view,800,600);
    const auto cursor=screenPoint(view,viewport,{5,0,0});
    auto press=[&]{input.acquire();input.submitUiPointer(cursor,true,true);play.advance(PlaySession::FixedStep,input,view);require(play.destination().has_value(),"Fresh owned press failed to start movement.");};
    auto assertCanceled=[&](const char* message){
        play.advance(PlaySession::FixedStep,input,view);require(!input.ownsKeyboard() && !play.destination(),message);
        const auto queries=play.routeQueryCount();input.acquire();input.submitUiPointer(cursor,false,true);
        for(int i=0;i<6;++i)play.advance(PlaySession::FixedStep,input,view);
        require(!play.destination() && play.routeQueryCount()==queries,"A canceled held gesture restarted without a fresh press.");
    };
    press();
    play.document().edit("Teleport player",[](auto& records,auto&){for(auto& record:records)if(record.id==2)record.local[12]+=1;});
    assertCanceled("Authored player pose edit retained its held route or gesture.");
    press();play.document().edit("Replace player",[](auto&,auto& properties){properties.playerCharacter=4;});
    assertCanceled("Replacing the controlled Character retained its held route or gesture.");
    press();require(play.document().beginTransform("Transform active Character",{4}),"Cannot begin held-input transform fixture.");
    assertCanceled("An active transform gesture retained gameplay input or queued a held route.");
    play.document().cancel();
    const auto queries=play.routeQueryCount();input.acquire();input.submitUiPointer(cursor,false,true);
    for(int i=0;i<6;++i)play.advance(PlaySession::FixedStep,input,view);
    require(!play.destination() && play.routeQueryCount()==queries,"Finishing a transform gesture replayed held movement without a new press.");
    press();input.cancel();input.acquire();input.submitUiPointer(cursor,false,true);play.advance(PlaySession::FixedStep,input,view);
    require(!play.destination() && !input.heldMoveRay(view),"Cancel/reacquire in one frame retained a held route.");
}
}
int main(){try{lifecycleAndEditing();flatPlayerPresentationPreservesRuntime();fixedTimingAndInterpolation();particlesUseRuntimeClock();particlePreprocessIsReadyBeforePlayReturns();followCameraTransformLocks();pointerMovementAndFacing();pointerCollisionAndNoFloor();unreachableAndReplanning();heldTargetsAndQueryBudget();heldStationaryCursorFollowsDisplayedProjection();heldProjectedTargetsAndMisses();obstacleSurfaceClicks();heldEditsRequireFreshPress();directionalInputRespectsRoleAndComponentChanges();composedActorsShareNavigationAndKeepIndependentCommands();std::cout<<"Play runtime checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
