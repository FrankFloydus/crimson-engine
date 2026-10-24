#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include "scene/CameraAuthoring.h"
#include "scene/SceneExtraction.h"
#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void near(Vec3 value,Vec3 expected,const char* message){require(math::length(value-expected)<1e-3f,message);}
void key(GameInput& input,bool down=true,bool repeat=false) {
    SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    event.key.windowID=9;event.key.scancode=SDL_SCANCODE_F;event.key.repeat=repeat;
    require(input.handleEvent(event),"Owned F was not consumed.");
}
void fixture(SceneDocument& document) {
    require(document.edit("Trace fixture",[](auto& records,auto& properties){
        records.clear();properties={};
        EntityRecord floor;floor.id=1;floor.name="Floor";floor.collider=SceneCollider{{100,1,100}};
        floor.local=math::translation({0,-.5f,0});records.push_back(floor);
        EntityRecord player;player.id=2;player.name="Player";player.actor.emplace();player.controller.emplace();
        player.movement.emplace();player.playerInput.emplace();player.local=math::translation({0,.05f,0});records.push_back(player);
        const auto settings=defaultPlayerCameraTemplate();
        EntityRecord camera;camera.id=3;camera.name="Camera";camera.parent=2;camera.camera=settings.lens;
        camera.cameraFollow=SceneCameraFollow{2,settings.offset,settings.worldBasis};records.push_back(camera);
        EntityRecord target;target.id=4;target.name="Target";target.collider.emplace();
        target.collider->shape=ColliderShape::Sphere;target.collider->radius=1;
        target.collider->collision.layers=WorldCollisionLayer|EnemyCollisionLayer;
        target.local=math::translation({0,1.7f,-5});records.push_back(target);
        EntityRecord other;other.id=6;other.name="Other actor";other.actor.emplace();other.collider.emplace();
        other.collider->collision.layers=WorldCollisionLayer|CharacterCollisionLayer;
        other.local=math::translation({0,1.7f,-8});records.push_back(other);
        EntityRecord side=target;side.id=7;side.name="Side target";side.local=math::translation({5,1.7f,0});records.push_back(side);
        properties.playerCharacter=2;properties.activeCamera=3;
    }),"Could not author trace fixture.");
}
void playerCameraTraceAndLifetime() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-player-traces";
    SceneDocument authored(root);fixture(authored);const auto original=authored.scene().records();
    const auto history=authored.history().size();PlaySession play(root);require(play.start(authored),"Could not start trace fixture.");
    GameInput input;input.setViewport({0,0,800,600,true,false,9},true,false);input.acquire();
    play.advance(0,input,Camera{});const auto camera=resolveGameCamera(play.presentationScene());require(camera.has_value(),"Missing Player camera.");
    key(input);play.advance(0,input,Camera{});
    require(play.tickCount()==0,"The test trace required a physics tick.");
    auto records=play.traceVisualization().records();require(records.size()==1,"F did not record exactly one trace.");
    require(records[0].segments.size()==2 && records[0].contacts.size()==1,"Hit did not split the line or mark its contact.");
    const auto& clear=records[0].segments[0];const auto& blocked=records[0].segments[1];
    require(!clear.afterHit && blocked.afterHit,"Trace colors changed before the impact.");
    near(clear.start,camera->position,"Trace did not start at the eye camera.");
    near(clear.end,{0,1.7f,-4},"Player's own capsule blocked the trace or the target surface was missed.");
    near(blocked.start,clear.end,"Green and red segments did not meet at the hit.");
    near(blocked.end,camera->position+camera->forward()*10,"Trace did not retain the full configured length.");
    require(!play.document().dirty() && play.document().history().size()==0,"Trace visualization changed runtime scene/history.");
    key(input,true,true);play.advance(0,input,Camera{});
    require(play.traceVisualization().records().size()==1,"F key repeat generated another trace.");
    // Expiration advances on render frames, independently of fixed simulation.
    for(int i=0;i<49;++i)play.advance(.01,input,Camera{});
    require(play.traceVisualization().records().size()==1,"Default duration expired before half a second.");
    play.advance(.011,input,Camera{});require(play.traceVisualization().records().empty(),"Default duration did not hide the trace.");
    require(play.tickCount()==30,"The runtime stopped advancing while checking trace expiry.");
    require(play.document().edit("Select only Character layers",[](auto& values,auto&){
        for(auto& value:values)if(value.id==2)value.playerInput->traceTest.collisionMask=CharacterCollisionLayer;
    }),"Could not change trace mask.");
    key(input,false);key(input);play.advance(0,input,Camera{});
    records=play.traceVisualization().records();require(records.size()==1&&records[0].segments.size()==2,"The Character-only mask failed to detect another actor.");
    require(records[0].segments[0].end[2]<-7,"A trace mask selected an excluded target.");
    play.stop();require(play.traceVisualization().records().empty(),"Stop retained trace geometry.");
    const auto after=authored.scene().records();
    require(after.size()==original.size() && authored.history().size()==history,"Play traces changed authored document history.");
    for(std::size_t i=0;i<after.size();++i)require(sameEntity(after[i],original[i]),"Play traces changed authored scene data.");
}
void currentLookMaskAndOptionalDrawing() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-player-trace-look";
    SceneDocument authored(root);fixture(authored);PlaySession play(root);require(play.start(authored),"Could not start look fixture.");
    GameInput input;input.setViewport({0,0,800,600,true,false,9},true,false);input.acquire();play.advance(0,input,Camera{});
    SDL_Event motion{};motion.type=SDL_EVENT_MOUSE_MOTION;motion.motion.windowID=9;motion.motion.xrel=900;
    require(input.handleEvent(motion),"Mouse look was rejected.");key(input);play.advance(0,input,Camera{});
    auto records=play.traceVisualization().records();require(records.size()==1 && records[0].contacts.size()==1,"Same-frame camera look missed the side target.");
    require(records[0].segments[0].end[0]>3.99f && std::abs(records[0].segments[0].end[2])<1e-3,
        "Trace used last frame's view instead of current mouse look.");
    PhysicsTrace query;query.ray={{0,1.7f,0},{0,0,-1}};query.maxDistance=10;
    const auto count=records.size();const auto result=play.trace(query,PhysicsQueryFilter{EnemyCollisionLayer});
    require(result.valid&&result.hits.size()==1&&result.hits[0].entity==4,"Public Play trace ignored query layers.");
    require(play.traceVisualization().records().size()==count,"A query without visualization created a drawing.");
    require(play.trace(query,PhysicsQueryFilter{0}).hits.empty(),"A zero mask detected collision.");
    require(play.document().edit("Hide test traces",[](auto& values,auto&){
        for(auto& value:values)if(value.id==2)value.playerInput->traceTest.duration=0;
    }),"Could not change trace duration.");
    key(input,false);key(input);play.advance(0,input,Camera{});
    require(play.traceVisualization().records().size()==count,"Duration zero recorded visible geometry.");
    key(input,false);key(input);input.cancel();play.advance(0,input,Camera{});
    require(play.traceVisualization().records().size()==count,"A canceled queued F press fired a trace.");
    play.advance(0,input,Camera{},false);require(play.traceVisualization().records().empty(),"Suspension retained temporary traces.");
    play.stop();require(!play.trace(query).valid,"An inactive Play session queried a destroyed physics world.");
}
}
int main(){try{playerCameraTraceAndLifetime();currentLookMaskAndOptionalDrawing();std::cout<<"Player camera trace checks passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
