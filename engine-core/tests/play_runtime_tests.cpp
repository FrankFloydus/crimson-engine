#include "scene/SceneComponents.h"
#include "navigation_fixture.h"
#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include "scene/CameraAuthoring.h"
#include "scene/BuiltinModels.h"
#include "scene/SceneExtraction.h"
#include "scene/SceneOutlinerRows.h"
#include "assets/GameplayEffectAsset.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
using crimson::testing::startWithNavigation;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void fixture(SceneDocument& document) {
    require(document.edit("Fixture",[](auto& records,auto& properties){
        records.clear();EntityRecord floor;floor.id=1;floor.name="Floor";floor.collider=SceneCollider{{100,1,100}};floor.local=math::translation({0,-.5f,0});records.push_back(floor);
        EntityRecord player;player.id=2;player.name="Character";player.actor.emplace();player.controller.emplace();player.movement.emplace();player.navigation.emplace();player.playerInput.emplace();player.local=math::translation({0,.05f,0});records.push_back(player);
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
    require(std::abs(a.lightPresentationSeconds()-1)<1e-8&&std::abs(a.lightPresentationSeconds()-b.lightPresentationSeconds())<1e-8,
        "Render-frame partition changed the accepted light presentation clock.");
    const float completed=x(a.document().scene());a.advance(PlaySession::FixedStep*.5,Vec2{},camera());
    require(x(a.presentationScene())<completed && x(a.presentationScene())>completed-.1f,"Presentation did not interpolate completed actor poses.");
    require(std::abs(a.lightPresentationSeconds()-1-PlaySession::FixedStep*.5)<1e-8,"Light phase did not include the fixed-step remainder.");
    const auto before=a.tickCount();const auto beforeLight=a.lightPresentationSeconds();a.advance(10,Vec2{1,0},camera());require(a.tickCount()==before+8 && a.droppedTicks()>0,"Catch-up was not bounded.");
    require(std::abs(a.lightPresentationSeconds()-beforeLight-8*PlaySession::FixedStep)<1e-8,"Discarded simulation stalls advanced light phase.");
    const auto pausedLight=a.lightPresentationSeconds();
    a.advance(5,Vec2{1,0},camera(),false);const float suspended=x(a.document().scene());const auto suspendedTicks=a.tickCount();
    a.advance(5,Vec2{1,0},camera(),true);require(a.tickCount()==suspendedTicks && x(a.document().scene())==suspended,"Focus resume replayed suspended wall time.");
    require(a.lightPresentationSeconds()==pausedLight,"Suspension or resume rewound or advanced light phase.");
    a.stop();require(a.lightPresentationSeconds()==0,"Stop retained the runtime light clock.");
    require(startWithNavigation(a,authored)&&a.lightPresentationSeconds()==0,"A new Play run retained the prior light phase.");
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
void addPlayerCamera(SceneDocument& document,bool follow=true) {
    require(document.edit("Player eye",[&](auto& records,auto& properties) {
        EntityRecord eye;eye.id=4;eye.parent=2;eye.name="Player Camera";eye.camera=SceneCamera{73.f,.035f,1234.f};
        if(follow)eye.cameraFollow=SceneCameraFollow{2,{.25f,1.6f,-.15f},Mat4{}};
        else eye.local=math::translation({.25f,1.6f,-.15f})*math::eulerRotationXYZ({.2f,-.3f,.17f})*math::scaling({.8f,1.2f,.9f});
        records.push_back(eye);properties.activeCamera=4;
    }),"Cannot prepare the Player eye.");
}
void playFromHereTransformsAndIsolation() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";
    for(const bool follow:{false,true}) {
        SceneDocument authored(root);fixture(authored);addPlayerCamera(authored,follow);
        auto parent=math::translation({-3,.2f,2})*math::eulerRotationXYZ({.13f,.37f,-.08f})*math::scaling({2.f,.7f,1.6f});
        parent[4]+=.15f;
        const auto originalPlayer=math::translation({0,.05f,0})*math::rotation(math::axisAngle({0,1,0},.27));
        const auto inverseParent=math::inverse(parent);require(inverseParent.has_value(),"The parent fixture is singular.");
        require(authored.edit("Affine parent and separate authored camera",[&](auto& records,auto& properties) {
            EntityRecord group;group.id=10;group.name="Scaled and sheared group";group.local=parent;records.push_back(group);
            for(auto& record:records)if(record.id==2){record.parent=10;record.local=*inverseParent*originalPlayer;}
            EntityRecord cinematic;cinematic.id=11;cinematic.name="Authored active Camera";cinematic.camera.emplace();
            cinematic.local=math::translation({-20,8,5});records.push_back(cinematic);properties.activeCamera=11;
        }),"Cannot prepare the affine Player fixture.");
        authored.select({2},2);
        const auto before=authored.scene().records();const auto roles=authored.scene().properties();
        const auto history=authored.history().size(),revision=authored.scene().revision();const bool dirty=authored.dirty();
        const auto basis=*authored.scene().worldTransform(4);
        PlaySession play(root);
        for(int cycle=0;cycle<3;++cycle) {
            Camera view;view.position={5.f+cycle,3.2f,-4.f};
            const auto direction=math::normalized(Vec3{.7f,-.24f,-.6f});view.target=view.position+direction;view.fov=99;
            const Vec3 clicked{7.f+cycle,0,-3.f};
            require(play.startFromPoint(authored,clicked,view),"Play From Here rejected a clear parented Player.");
            const auto expectedEye=math::transformPoint(*play.document().scene().worldTransform(4),{});
            const auto resolved=resolveGameCamera(play.presentationScene());
            require(resolved && math::length(resolved->position-expectedEye)<3e-4f && math::dot(resolved->forward(),direction)>.99999f,
                "Play From Here lost the authored eye offset or initial view orientation.");
            require(resolved->fov==73 && resolved->nearPlane==.035f && resolved->farPlane==1234,
                "Play From Here replaced the authored Player lens with the editor lens.");
            const auto actual=*play.presentationScene().worldTransform(4);
            for(int a=0;a<3;++a)for(int b=0;b<3;++b) {
                double original{},relocated{};
                for(int row=0;row<3;++row){original+=basis[4*a+row]*basis[4*b+row];relocated+=actual[4*a+row]*actual[4*b+row];}
                require(std::abs(original-relocated)<2e-4,"Play From Here changed the eye's residual affine basis.");
            }
            const auto playerWorld=*play.document().scene().worldTransform(2);
            require(math::length(math::transformPoint(playerWorld,{})-clicked)<3e-4f,
                "Play From Here placed the eye or editor camera at the clicked point instead of the capsule feet.");
            require(std::abs(playerWorld[4])<2e-5f && std::abs(playerWorld[5]-1)<2e-5f && std::abs(playerWorld[6])<2e-5f,
                "Play From Here inherited parent scale or tilt into the upright collision capsule.");
            require(play.document().scene().find(4)->cameraFollow==authored.scene().find(4)->cameraFollow &&
                play.document().scene().find(10)->local==parent && play.document().scene().properties().activeCamera==4 &&
                !play.document().dirty() && play.document().history().size()==0,
                "Play From Here changed follow definitions, its parent, or the clean transient history.");
            play.advance(0,Vec2{},view);
            const auto initialized=resolveGameCamera(play.presentationScene());
            require(initialized && math::length(initialized->position-expectedEye)<3e-4f && math::dot(initialized->forward(),direction)>.99999f,
                "The first ordinary Play frame reset the initial eye look.");
            play.stop();
            const auto after=authored.scene().records();require(after.size()==before.size(),"Play From Here added authored entities.");
            for(std::size_t i=0;i<after.size();++i)require(sameEntity(after[i],before[i]),"Play From Here modified an authored record.");
            require(sameProperties(authored.scene().properties(),roles) && authored.history().size()==history &&
                authored.scene().revision()==revision && authored.dirty()==dirty && authored.selection().primary==2,
                "Play From Here changed authored roles, history, revision, dirtiness or selection.");
        }
        require(play.start(authored),"Normal Play failed after repeated Play From Here.");
        require(play.document().scene().properties().activeCamera==11 &&
            math::length(math::transformPoint(*play.document().scene().worldTransform(2),{})-Vec3{0,.05f,0})<1e-5f,
            "Normal Play retained a previous relocated Player or temporary active-camera choice.");play.stop();
    }
}
void playFromHereClearance() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";
    SceneDocument authored(root);fixture(authored);addPlayerCamera(authored);
    require(authored.edit("Placement blockers",[](auto& records,auto&) {
        EntityRecord wall;wall.id=6;wall.name="Solid blocker";wall.collider=SceneCollider{{2,4,2}};wall.local=math::translation({5,2,0});records.push_back(wall);
        EntityRecord peer;peer.id=7;peer.name="Other Character";peer.actor.emplace();peer.controller.emplace();peer.local=math::translation({-5,0,0});records.push_back(peer);
        EntityRecord sensor;sensor.id=8;sensor.name="Nonblocking trigger";sensor.collider=SceneCollider{{2,4,2}};
        sensor.collider->mode=ColliderMode::Trigger;sensor.collider->collision={TriggerCollisionLayer,AllCollisionLayers};
        sensor.local=math::translation({10,2,0});records.push_back(sensor);
    }),"Cannot prepare placement blockers.");
    const auto before=authored.scene().records();const auto history=authored.history().size();PlaySession play(root);
    const auto attempt=[&](const Vec3& feet) {
        Camera view;view.position=feet+authored.scene().find(4)->cameraFollow->offset;view.target=view.position+Vec3{0,0,-1};
        return play.startFromPoint(authored,feet,view);
    };
    require(!attempt({5,0,0}) && !play.playing() && play.status().find("blocked")!=std::string::npos,
        "Play From Here admitted a capsule penetrating solid collision.");
    require(!attempt({-5,0,0}) && !play.playing(),"Play From Here admitted a capsule overlapping another Character.");
    require(!attempt({0,-.1f,0}) && !play.playing(),"Play From Here admitted a capsule penetrating its support.");
    require(attempt({0,0,0}),"Touching the floor incorrectly blocked Play From Here.");play.stop();
    require(attempt({10,0,0}),"A trigger incorrectly blocked Play From Here.");play.stop();
    Camera invalid;invalid.position={0,2,0};invalid.target=invalid.position;
    require(!play.startFromPoint(authored,{},invalid) && !play.playing(),"A degenerate Scene view started Play From Here.");
    require(!play.startFromPoint(authored,{std::numeric_limits<float>::quiet_NaN(),0,0},camera()) && !play.playing(),
        "A nonfinite clicked point started Play From Here.");
    const auto after=authored.scene().records();require(after.size()==before.size()&&authored.history().size()==history,"Failed placement changed authored history.");
    for(std::size_t i=0;i<after.size();++i)require(sameEntity(after[i],before[i]),"Failed placement modified authored records.");
    SceneDocument missing(root);fixture(missing);
    require(!play.startFromPoint(missing,{},camera()) && !play.playing(),"Play From Here invented a missing Player camera.");
}
InputEvent gameKey(PhysicalKeyCode code) {
    InputEvent event{};event.type=InputEventType::KeyDown;event.surface=9;event.physical=code;return event;
}
void pauseAndExactSingleStep() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";
    SceneDocument authored(root);fixture(authored);addPlayerCamera(authored);
    auto effect=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());effect->id=newAssetId();effect->emitter.rate=60;
    auto periodic=std::make_shared<GameplayEffectAsset>();periodic->id=newAssetId();periodic->name="Fixed tick fixture";
    periodic->durationPolicy=EffectDurationPolicy::HasDuration;periodic->duration=1;periodic->period=PlaySession::FixedStep;
    periodic->executeOnApplication=false;periodic->modifiers={{stats::MovementSpeed,EffectModifierOperation::AddBase,{1}}};
    const auto clip=newAssetId();
    require(authored.edit("Iteration clocks",[&](auto& records,auto&) {
        for(auto& record:records)if(record.id==3) {
            record.model=AssetRef<ModelAsset>{BuiltinBoxModelId};record.modelAnimation.asset=AssetRef<AnimationAsset>{clip};
            record.modelAnimation.startTime=.25;record.modelAnimation.speed=2;
        }
        EntityRecord emitter;emitter.id=5;emitter.parent=2;emitter.name="Emitter";emitter.particle=AssetRef<ParticleEffectAsset>{effect->id};records.push_back(emitter);
    }),"Cannot prepare iteration clocks.");
    PlaySession play(root);play.setParticleDefinitions([effect](const std::string&){return ParticleDefinitionVersion{effect,1,{}};});
    play.setGameplayEffects([periodic](const std::string& id){return id==periodic->id?EffectDefinitionSnapshot{periodic,1,{}}:EffectDefinitionSnapshot{};});
    require(!play.setPaused(true) && !play.step(camera()),"Idle editor accepted Pause or Step.");
    require(play.start(authored) && !play.paused() && !play.step(camera()),"Play started paused or accepted Step while running.");
    const auto applied=play.applyEffect(2,2,periodic->id);require(static_cast<bool>(applied),"Cannot prepare periodic clock fixture.");
    GameInput input;input.setViewport({0,0,800,600,true,false,9},true,false);input.acquire();
    play.advance(0,input,camera());
    for(int i=0;i<4;++i)play.advance(PlaySession::FixedStep,input,camera());
    // Queue press edges on a render frame with no physics tick, then pause.
    input.handleEvent(gameKey(PhysicalKeyCode::W));input.handleEvent(gameKey(PhysicalKeyCode::Space));
    play.advance(0,input,camera());
    input.handleEvent(gameKey(PhysicalKeyCode::F));
    InputEvent look{};look.type=InputEventType::MouseMotion;look.surface=9;look.relative[0]=80;look.relative[1]=40;input.handleEvent(look);
    require(play.setPaused(true) && play.paused(),"Cannot pause Play.");
    const auto ticks=play.tickCount(),dropped=play.droppedTicks();const auto seconds=play.acceptedSeconds();
    const auto position=math::transformPoint(*play.document().scene().worldTransform(2),{});
    const auto remaining=play.effects().active(2).front().remaining;
    const auto animationSeconds=[&] {auto extracted=extractScene(play.presentationScene());play.prepareRagdollPresentation(extracted);return extracted.models.front().animationSeconds;};
    const auto animation=animationSeconds(),particleSeconds=play.particles().instance(5)->elapsed();
    for(int i=0;i<3;++i) {
        play.advance(5,input,camera());play.advance(.7,Vec2{1,1},camera(),false);
    }
    require(play.paused() && play.tickCount()==ticks && play.droppedTicks()==dropped && play.acceptedSeconds()==seconds &&
        play.particles().instance(5)->elapsed()==particleSeconds && animationSeconds()==animation &&
        play.effects().active(2).front().remaining==remaining,
        "Paused wall time advanced simulation, effects, particles, animation or accepted presentation time.");
    require(!input.ownsKeyboard() && input.movement()==Vec2{} && !input.consumeJump() && !input.consumeTrace() && input.consumeLookDelta()==Vec2{},
        "Pause retained Game input ownership or pending gameplay edges.");
    require(play.step(camera()) && play.paused() && play.tickCount()==ticks+1 && play.droppedTicks()==dropped,
        "Single Step did not execute exactly one tick while staying paused.");
    require(std::abs(play.acceptedSeconds()-seconds-PlaySession::FixedStep)<1e-10 &&
        std::abs(play.particles().instance(5)->elapsed()-particleSeconds-PlaySession::FixedStep)<1e-10 &&
        std::abs(animationSeconds()-animation-2*PlaySession::FixedStep)<1e-10 &&
        std::abs(*play.effects().active(2).front().remaining-*remaining+PlaySession::FixedStep)<1e-10,
        "Single Step did not share the exact fixed interval across runtime clocks.");
    const auto stepped=math::transformPoint(*play.document().scene().worldTransform(2),{});
    const auto presented=math::transformPoint(*play.presentationScene().worldTransform(2),{});
    require(math::length(stepped-presented)<1e-6f && std::hypot(stepped[0]-position[0],stepped[2]-position[2])<1e-4f && stepped[1]<=position[1]+1e-4f,
        "Single Step displayed an old sample or replayed paused movement/jump input.");
    play.advance(10,input,camera());require(play.tickCount()==ticks+1 && play.paused(),"A Step resumed ordinary simulation.");
    require(play.setPaused(false),"Cannot resume Play.");play.advance(10,input,camera());
    require(play.tickCount()==ticks+1 && play.acceptedSeconds()==seconds+PlaySession::FixedStep,"Resume caught up paused wall time.");
    play.advance(PlaySession::FixedStep,input,camera());require(play.tickCount()==ticks+2,"Resumed Play did not accept its next ordinary tick.");
    for(int cycle=0;cycle<3;++cycle) {
        require(play.setPaused(true) && play.step(camera()) && play.setPaused(false),"Repeated Pause/Step/Resume failed.");
        const auto before=play.tickCount();play.advance(20,input,camera());require(play.tickCount()==before,"A later Resume replayed wall time.");
    }
    require(play.setPaused(true),"Cannot pause before Stop.");play.stop();
    require(!play.playing() && !play.paused() && play.tickCount()==0 && play.acceptedSeconds()==0,"Stop retained paused runtime state.");
    require(play.start(authored) && !play.paused(),"A new Play run inherited Pause.");play.stop();
}
void pausedStepRetainsTriggerBatch() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";
    SceneDocument authored(root);fixture(authored);
    require(authored.edit("Stepped trigger",[](auto& records,auto&) {
        EntityRecord trigger;trigger.id=4;trigger.name="Player detector";trigger.collider=SceneCollider{{3,4,3}};
        trigger.collider->mode=ColliderMode::Trigger;trigger.collider->collision={TriggerCollisionLayer,CharacterCollisionLayer};
        trigger.local=math::translation({0,2,0});records.push_back(trigger);
    }),"Cannot prepare the stepped trigger.");
    PlaySession play(root);require(play.start(authored)&&play.setPaused(true)&&play.step(camera()),"Cannot step an initially paused trigger world.");
    const std::vector<TriggerEvent> entered(play.triggerEvents().begin(),play.triggerEvents().end());
    require(entered.size()==1&&entered[0].trigger==4&&entered[0].other==2&&entered[0].kind==TriggerEventKind::Enter,
        "Single Step did not publish its initial trigger Enter.");
    for(int frame=0;frame<3;++frame) {
        play.advance(0,Vec2{},camera());play.advance(10,Vec2{},camera(),false);
        require(std::vector<TriggerEvent>(play.triggerEvents().begin(),play.triggerEvents().end())==entered,
            "A paused render or focus frame consumed the stepped trigger batch.");
    }
    require(play.step(camera())&&play.triggerEvents().empty(),"A later Step replayed its predecessor's trigger batch.");
    require(play.document().edit("Move stepped trigger away",[](auto& records,auto&) {
        for(auto& record:records)if(record.id==4)record.local=math::translation({20,2,0});
    }),"Cannot edit the paused trigger.");
    play.advance(0,Vec2{},camera());require(play.step(camera()),"Cannot step the edited trigger.");
    const std::vector<TriggerEvent> exited(play.triggerEvents().begin(),play.triggerEvents().end());
    require(exited.size()==1&&exited[0].kind==TriggerEventKind::Exit&&exited[0].reason==TriggerEventReason::Configuration,
        "The edited trigger did not publish a stepped configuration Exit.");
    play.advance(0,Vec2{},camera());
    require(std::vector<TriggerEvent>(play.triggerEvents().begin(),play.triggerEvents().end())==exited,
        "Paused reconciliation consumed the stepped Exit.");
    require(play.setPaused(false)&&play.triggerEvents().empty(),"Resume retained a stale stepped trigger batch.");
    play.advance(0,Vec2{},camera());require(play.triggerEvents().empty(),"An ordinary zero-tick advance replayed stepped events.");
}
void pauseAndStepContainDefinitionFailures() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";
    SceneDocument authored(root);fixture(authored);
    auto effect=std::make_shared<ParticleEffectAsset>(defaultParticleEffect());effect->id=newAssetId();
    require(authored.edit("Throwing particle definition",[&](auto& records,auto&) {
        EntityRecord emitter;emitter.id=4;emitter.name="Failure fixture";emitter.particle=AssetRef<ParticleEffectAsset>{effect->id};records.push_back(emitter);
    }),"Cannot prepare the definition-failure fixture.");
    const auto original=authored.scene().records();const auto history=authored.history().size();
    std::uint64_t queries{},throwAt{};PlaySession play(root);
    play.setParticleDefinitions([&](const std::string&) {
        if(++queries==throwAt)throw std::runtime_error("Fixture particle definition failed.");
        return ParticleDefinitionVersion{effect,1,{}};
    });
    require(play.start(authored),"Cannot start the definition-failure fixture.");
    throwAt=queries+1;
    require(!play.setPaused(true)&&play.playing()&&play.status().find("Fixture particle definition failed")!=std::string::npos,
        "Pause let a definition exception escape or lost its useful failure status.");
    require(!play.step(camera())&&!play.setPaused(false),"A failed Pause resumed simulation without Stop.");
    play.stop();throwAt=0;
    require(play.start(authored)&&play.setPaused(true),"Stop did not reset a failed Pause.");
    // The fixed tick and advanceInternal's presentation each query once; the
    // third query is Step's final completed-pose publication outside that owner.
    throwAt=queries+3;const auto before=play.tickCount();
    require(!play.step(camera())&&queries==throwAt&&play.tickCount()==before+1&&
        play.status().find("Fixture particle definition failed")!=std::string::npos,
        "Step's completed-pose publication let a definition exception escape or executed another tick.");
    require(!play.step(camera()),"A failed Step accepted another physics tick.");play.stop();throwAt=0;
    require(play.start(authored)&&!play.paused()&&play.tickCount()==0,"Stop did not reset a failed Step.");play.stop();
    const auto after=authored.scene().records();require(after.size()==original.size()&&authored.history().size()==history,"Definition failures changed authored history.");
    for(std::size_t index=0;index<after.size();++index)require(sameEntity(after[index],original[index]),"Definition failures changed authored records.");
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
        EntityRecord other;other.id=6;other.name="Unrelated falling actor";other.actor.emplace();other.controller.emplace();other.movement.emplace();other.local=math::translation({8,100,0});records.push_back(other);
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
void directionalInputRespectsRoleAndComponentChanges() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-play-cpu-unused";SceneDocument authored(root);fixture(authored);
    require(authored.edit("Other input-capable actor",[](auto& records,auto&){
        EntityRecord actor;actor.id=4;actor.name="Other actor";actor.actor.emplace();actor.controller.emplace();actor.movement.emplace();
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
            actor.movement.emplace();actor.navigation.emplace();actor.local=math::translation({-4,.05f,id==2?-5.f:id==4?2.f:id==5?-2.f:5.f});
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

    GameInput input;input.setViewport({0,0,800,600,true,false,9},true,false);input.acquire();
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
        for(auto& r:records){if(r.id==4)r.movement->enabled=false;if(r.id==5)r.navigation->enabled=false;}
    }),"Cannot disable actor components.");
    require(!play.setActorMovement(4,{1,0,0})&&!play.setActorDestination(5,{0,0,0})&&play.setActorMovement(5,{1,0,0}),
        "Disabled Motor/Navigation Agent capabilities were not respected independently.");
    require(play.document().edit("Change static navigation geometry",[](auto& records,auto&){for(auto& r:records)if(r.id==1)r.collider->size={42,1,40};}),
        "Cannot edit shared navigation geometry.");
    play.advance(0,Vec2{},camera());
    require(testing::waitForNavigation(play)&&play.navigationBuildCount()>initialBuilds,"A world change did not rebuild the affected retained clearance tiles.");
    require(play.document().transient()&&authored.scene().find(1)->collider->size==Vec3{40,1,40}&&
        authored.scene().find(4)->movement->enabled&&authored.scene().find(5)->navigation->enabled,
        "Runtime component edits changed the authored actors or geometry.");
}

}
int main(){try{lifecycleAndEditing();flatPlayerPresentationPreservesRuntime();fixedTimingAndInterpolation();particlesUseRuntimeClock();playFromHereTransformsAndIsolation();playFromHereClearance();pauseAndExactSingleStep();pausedStepRetainsTriggerBatch();pauseAndStepContainDefinitionFailures();particlePreprocessIsReadyBeforePlayReturns();followCameraTransformLocks();directionalInputRespectsRoleAndComponentChanges();composedActorsShareNavigationAndKeepIndependentCommands();std::cout<<"Play runtime checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
