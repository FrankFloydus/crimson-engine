#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void fixture(SceneDocument& document) {
    require(document.edit("Fixture",[](auto& records,auto& properties){
        records.clear();EntityRecord floor;floor.id=1;floor.name="Floor";floor.collider=SceneCollider{{100,1,100}};floor.local=math::translation({0,-.5f,0});records.push_back(floor);
        EntityRecord player;player.id=2;player.name="Character";player.character.emplace();player.local=math::translation({0,.05f,0});records.push_back(player);
        EntityRecord visual;visual.id=3;visual.parent=2;visual.name="Visual";visual.local=math::translation({0,1,0});records.push_back(visual);
        properties.playerCharacter=2;
    }),"Cannot prepare runtime fixture.");
}
Camera camera(){Camera result;result.position={0,5,5};result.target={0,0,0};return result;}
float x(const Scene& scene){return scene.worldTransform(2)->at(0,3);}
void lifecycleAndEditing() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";
    SceneDocument authored(root);fixture(authored);const auto original=authored.scene().records();const auto historySize=authored.history().size();
    PlaySession play(root);require(play.start(authored),"Play did not clone authored data.");
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
    play.stop();require(!play.playing(),"Stop retained its runtime.");require(play.start(authored),"Restart failed.");require(std::abs(x(play.document().scene()))<1e-5,"Restart retained movement.");
    const auto final=authored.scene().records();require(final.size()==original.size(),"Stop changed authored entities.");
    for(std::size_t i=0;i<final.size();++i)require(sameEntity(final[i],original[i]),"Play modified authored records.");
}
void fixedTimingAndInterpolation() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    PlaySession a(root),b(root);require(a.start(authored) && b.start(authored),"Cannot start timing runtimes.");
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
    PlaySession turn(root);require(turn.start(turning),"Cannot start turning runtime.");
    Camera look;look.position={0,2,0};look.target={-std::sin(-179*radians),2,-std::cos(-179*radians)};
    turn.advance(PlaySession::FixedStep*1.5,Vec2{0,1},look);
    require((*turn.presentationScene().worldTransform(2))[10]<-.99f,"Yaw interpolation took the long path across the angle wrap.");
}
void particlesUseRuntimeClock() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    const auto effect=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());effect->id="assets/particles/test.crimson-particle";effect->emitter.rate=60;
    authored.edit("Emitter",[&](auto& records,auto&){EntityRecord emitter;emitter.id=4;emitter.parent=2;emitter.name="Emitter";emitter.particle=AssetRef<ParticleEffectAsset>{effect->id};records.push_back(emitter);});
    PlaySession play(root);play.setParticleDefinitions([effect](const std::string&){return ParticleDefinitionVersion{effect,1,{}};});require(play.start(authored),"Cannot start particle runtime.");
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
    require(play.start(authored),"Cannot start preprocessed particle runtime.");
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
    play.stop();require(play.start(authored),"Cannot restart preprocessed Play session.");
    require(play.particles().instance(4)->stats().ticks==120 && play.particles().presentation().size()==firstImage.size(),"Stop/Play did not immediately reproduce the warm initial presentation.");
    for(std::size_t i=0;i<firstImage.size();++i)require(firstImage[i].position==play.particles().presentation()[i].position &&
        firstImage[i].spawnOrdinal==play.particles().presentation()[i].spawnOrdinal,"Stop/Play changed the seeded initial fire state.");play.stop();

    SceneDocument many(root);fixture(many);effect->emitter.capacity=32;
    require(many.edit("Many preprocessed emitters",[&](auto& records,auto&){for(EntityId id=100;id<116;++id){
        EntityRecord emitter;emitter.id=id;emitter.name="Fire";emitter.particle=AssetRef<ParticleEffectAsset>{effect->id};records.push_back(emitter);
    }}),"Cannot prepare many-emitter Play fixture.");
    require(play.start(many),"Cannot start many-emitter pre-process runtime.");
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
        follow.camera->followTarget=3;follow.camera->followOffset={0,5,5};records.push_back(follow);properties.activeCamera=4;
        EntityRecord child;child.id=5;child.parent=4;child.name="Camera child";child.local=math::translation({0,2,0});records.push_back(child);
        EntityRecord other;other.id=6;other.name="Unrelated falling actor";other.character.emplace();other.local=math::translation({8,100,0});records.push_back(other);
        EntityRecord chained;chained.id=7;chained.name="Chained follow camera";chained.camera.emplace();chained.camera->followTarget=5;chained.camera->followOffset={3,3,3};records.push_back(chained);
    }),"Cannot prepare following-camera lock fixture.");
    PlaySession play(root);require(play.start(authored),"Cannot start camera-lock runtime.");
    for(const auto edited:std::array<EntityId,3>{4,5,7}) {
        play.advance(PlaySession::FixedStep,Vec2{1,0},camera());
        const auto playerBefore=*play.document().scene().worldTransform(2);
        const float otherBefore=(*play.document().scene().worldTransform(6))[13];
        require(play.document().beginTransform("Move following camera dependency",{edited}),"Cannot begin camera dependency edit.");
        require(play.document().update([edited](auto& records,auto&){for(auto& record:records)if(record.id==edited) {
            if(record.camera)record.camera->followOffset[0]+=1;
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
    PlaySession play(root);require(play.start(authored),"Cannot start pointer runtime.");
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

    PlaySession a(root),b(root);require(a.start(authored)&&b.start(authored),"Cannot start pointer timing runtimes.");
    GameInput first,second;first.setViewport(viewport,true,false);second.setViewport(viewport,true,false);first.acquire();second.acquire();
    const auto far=screenPoint(view,viewport,{8,0,0});first.submitPointer(far,true);second.submitPointer(far,true);
    for(int i=0;i<60;++i)a.advance(1.0/60,first,view);
    for(int i=0;i<240;++i)b.advance(1.0/240,second,view);
    require(std::abs(x(a.document().scene())-x(b.document().scene()))<1e-4,"Pointer control changed with render frame rate.");
}
void pointerCollisionAndNoFloor() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Wall",[](auto& records,auto&){EntityRecord wall;wall.id=9;wall.name="Wall";wall.collider=SceneCollider{{.2f,2,4}};wall.local=math::translation({1,1,0});records.push_back(wall);});
    PlaySession play(root);require(play.start(authored),"Cannot start click collision runtime.");
    auto view=camera();view.position={0,10,8};const ViewportRegion viewport{0,0,800,600,true,false,9};
    GameInput input;input.setViewport(viewport,true,false);input.acquire();input.submitPointer(screenPoint(view,viewport,{4,0,0}),true);
    double detour=0;
    for(int i=0;i<240;++i){play.advance(PlaySession::FixedStep,input,view);detour=std::max(detour,std::abs(static_cast<double>((*play.document().scene().worldTransform(2))[14])));}
    require(detour>2.3 && std::abs(x(play.document().scene())-4)<.02f,"Clicked movement did not follow a safe path around the wall and arrive.");
    const auto navigationRevision=play.navigationRevision();
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.navigationRevision()==navigationRevision,"Simulation poses rebuilt the cached navigation mesh.");
    play.document().edit("Remove wall",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==9;});});
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(play.navigationRevision()==navigationRevision+1,"Live wall deletion did not rebuild navigation once.");
    input.submitPointer(screenPoint(view,viewport,{-4,0,0}),true);play.advance(PlaySession::FixedStep,input,view);
    const auto beforeFloorDeletion=x(play.document().scene());
    play.document().edit("Remove support",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.collider.has_value();});});
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(std::abs(x(play.document().scene())-beforeFloorDeletion)<1e-4 && play.path().empty() &&
        !play.destination() && (*play.document().scene().worldTransform(2))[13]<-3,
        "Deleting support geometry retained a stale route or invented a floor.");
    authored.edit("No floor",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.collider.has_value();});});
    play.stop();require(play.start(authored),"Cannot restart without colliders.");input.cancel();input.acquire();input.submitPointer(screenPoint(view,viewport,{4,0,0}),true);
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
    PlaySession play(root);require(play.start(authored),"Cannot start unreachable runtime.");
    auto view=camera();view.position={0,10,8};const ViewportRegion viewport{0,0,800,600,true,false,9};
    GameInput input;input.setViewport(viewport,true,false);input.acquire();input.submitPointer(screenPoint(view,viewport,{4,0,0}),true);
    for(int i=0;i<30;++i)play.advance(PlaySession::FixedStep,input,view);
    require(x(play.document().scene())>.1f && x(play.document().scene())<.65f && play.path().empty() && !play.destination(),
        "Blocked ground click did not approach and stop safely on this side of the wall.");
    require(play.lastClickedGround() && math::length(*play.lastClickedGround()-Vec3{4,0,0})<.001,"Unreachable ground click lost its debug hit point.");
    play.document().edit("Open route",[](auto& records,auto&){std::erase_if(records,[](const auto& record){return record.id==9;});});
    for(int i=0;i<60;++i)play.advance(PlaySession::FixedStep,input,view);
    require(std::abs(x(play.document().scene())-4)<.02f,"Live collider deletion did not replan the retained destination.");
    input.submitPointer(screenPoint(view,viewport,{-4,0,0}),true);play.advance(PlaySession::FixedStep,input,view);
    play.document().edit("Block active route",[](auto& records,auto&){EntityRecord wall;wall.id=9;wall.name="Wall";
        wall.collider=SceneCollider{{.2f,2,24}};wall.local=math::translation({1,1,0});records.push_back(wall);});
    const float before=x(play.document().scene());
    for(int i=0;i<30;++i)play.advance(PlaySession::FixedStep,input,view);
    require(x(play.document().scene())<before-.5f && x(play.document().scene())>1.4f && x(play.document().scene())<2 && play.path().empty(),
        "Inserted collider did not replace the active route with a safe approach point.");
    input.cancel();input.acquire();play.advance(PlaySession::FixedStep,input,view);
    require(!play.destination() && play.lastClickedGround(),"Focus cancellation erased the debug marker or retained movement.");
}
void heldTargetsAndQueryBudget() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Small floor",[](auto& records,auto&){for(auto& record:records)if(record.id==1)record.collider->size={20,1,20};});
    PlaySession play(root);require(play.start(authored),"Cannot start held-target runtime.");
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
    PlaySession play(root);require(play.start(authored),"Cannot start held-projection runtime.");
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
    PlaySession play(root);require(play.start(authored),"Cannot start held-unreachable runtime.");
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
        PlaySession play(root);require(play.start(authored),"Cannot start obstacle click runtime.");
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
void heldEditsRequireFreshPress() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    authored.edit("Edit fixture",[](auto& records,auto&){
        for(auto& record:records)if(record.id==1)record.collider->size={20,1,20};
        EntityRecord other;other.id=4;other.name="Other Character";other.character.emplace();other.local=math::translation({-4,.05f,0});records.push_back(other);
    });
    PlaySession play(root);require(play.start(authored),"Cannot start held-edit runtime.");
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
int main(){try{lifecycleAndEditing();fixedTimingAndInterpolation();particlesUseRuntimeClock();particlePreprocessIsReadyBeforePlayReturns();followCameraTransformLocks();pointerMovementAndFacing();pointerCollisionAndNoFloor();unreachableAndReplanning();heldTargetsAndQueryBudget();heldStationaryCursorFollowsDisplayedProjection();heldProjectedTargetsAndMisses();obstacleSurfaceClicks();heldEditsRequireFreshPress();std::cout<<"Play runtime checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
