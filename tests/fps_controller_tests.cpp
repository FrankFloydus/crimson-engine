#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include "scene/CameraAuthoring.h"
#include "scene/SceneExtraction.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>

namespace {
using namespace crimson;
const ViewportRegion Viewport{0,0,800,600,true,false,9};
constexpr double Tick=PlaySession::FixedStep;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Vec3 position(const Scene& scene){return math::transformPoint(*scene.worldTransform(2),{});}
void fixture(SceneDocument& document) {
    require(document.edit("FPS fixture",[](auto& records,auto& properties){
        records.clear();properties={};
        EntityRecord floor;floor.id=1;floor.name="Floor";floor.collider=SceneCollider{{100,1,100}};
        floor.local=math::translation({0,-.5f,0});records.push_back(floor);
        EntityRecord player;player.id=2;player.name="Player";player.actor.emplace();player.controller.emplace();
        player.motor.emplace();player.playerInput.emplace();player.local=math::translation({0,.05f,0});records.push_back(player);
        const auto settings=defaultPlayerCameraTemplate();
        EntityRecord camera;camera.id=3;camera.name="Camera";camera.parent=2;camera.camera=settings.lens;
        camera.cameraFollow=SceneCameraFollow{2,settings.offset,settings.worldBasis};records.push_back(camera);
        properties.playerCharacter=2;properties.activeCamera=3;
    }),"Could not create FPS fixture.");
}
void key(GameInput& input,SDL_Scancode code,bool down=true,bool repeat=false) {
    SDL_Event event{};event.type=down?SDL_EVENT_KEY_DOWN:SDL_EVENT_KEY_UP;
    event.key.windowID=Viewport.windowId;event.key.scancode=code;event.key.repeat=repeat;
    require(input.handleEvent(event),"FPS key was not consumed by Game.");
}
void look(GameInput& input,float x,float y) {
    SDL_Event event{};event.type=SDL_EVENT_MOUSE_MOTION;event.motion.windowID=Viewport.windowId;
    event.motion.xrel=x;event.motion.yrel=y;event.motion.x=400;event.motion.y=300;
    require(input.handleEvent(event),"FPS look was not consumed by Game.");
}
void activate(GameInput& input){input.setViewport(Viewport,true,false);input.acquire();}
void ticks(PlaySession& play,GameInput& input,int count) {for(int i=0;i<count;++i)play.advance(Tick,input,Camera{});}
void movementJumpAndOwnership() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-runtime";
    SceneDocument authored(root);fixture(authored);const auto original=authored.scene().records();
    const auto history=authored.history().size();PlaySession play(root);require(play.start(authored),"FPS needed a navigation mesh to start.");
    GameInput input;activate(input);ticks(play,input,20);
    key(input,SDL_SCANCODE_D);ticks(play,input,60);
    require(position(play.document().scene())[0]>4.4f,"WASD did not accelerate to walking speed without navigation.");
    require(play.routeQueryCount()==0&&!play.destination(),"WASD created a pathfinding request.");
    const auto tilted=resolveGameCamera(play.presentationScene());
    require(tilted&&tilted->up[0]>.02f,"Lateral movement did not bank the camera.");
    key(input,SDL_SCANCODE_D,false);ticks(play,input,100);
    const auto upright=resolveGameCamera(play.presentationScene());
    require(upright&&std::abs(upright->up[0])<.001f,"Strafe tilt did not settle after releasing movement.");
    const auto grounded=position(play.document().scene());
    key(input,SDL_SCANCODE_SPACE);play.advance(0,input,Camera{});
    require(position(play.document().scene())==grounded,"Jump ran outside the fixed clock.");
    double peak=grounded[1];
    for(int i=0;i<100;++i){key(input,SDL_SCANCODE_SPACE,true,true);ticks(play,input,1);peak=std::max(peak,static_cast<double>(position(play.document().scene())[1]));}
    require(peak-grounded[1]>.9&&peak-grounded[1]<1.3,"Jump did not reach its authored height.");
    require(std::abs(position(play.document().scene())[1]-grounded[1])<.05f,"Holding Space caused another jump or prevented landing.");
    key(input,SDL_SCANCODE_SPACE,false);
    key(input,SDL_SCANCODE_W);ticks(play,input,5);input.cancel();play.advance(0,input,Camera{});
    const auto released=position(play.document().scene());ticks(play,input,30);
    require(math::length(position(play.document().scene())-released)<.01,"Releasing Game replayed held movement.");
    require(!play.document().dirty()&&play.document().history().size()==0,"FPS simulation wrote scene history.");
    play.stop();require(authored.history().size()==history,"Play altered authored history.");
    const auto after=authored.scene().records();require(after.size()==original.size(),"Play changed authored entities.");
    for(std::size_t i=0;i<after.size();++i)require(sameEntity(after[i],original[i]),"FPS changed authored data.");
}
void lookCameraAuthoringAndEdits() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-runtime";
    SceneDocument authored(root);fixture(authored);
    auto settings=defaultPlayerCameraTemplate();settings.offset={.1f,1.57f,.04f};settings.lens.verticalFov=83;
    require(updateCameraAuthoring(authored,3,settings),"Could not author the FPS eye position.");
    PlaySession play(root);require(play.start(authored),"Could not start FPS look fixture.");GameInput input;activate(input);
    play.advance(0,input,Camera{});const auto before=resolveGameCamera(play.presentationScene());
    require(before&&math::length(before->position-position(play.presentationScene())-settings.offset)<1e-5&&
        std::abs(before->fov-83)<1e-5,"Play discarded authored eye position or FOV.");
    look(input,900,0);play.advance(0,input,Camera{});
    const auto turned=resolveGameCamera(play.presentationScene());
    require(turned&&math::normalized(turned->target-turned->position)[0]>.999f&&play.tickCount()==0,
        "Mouse look was delayed until a fixed tick or scaled by frame time.");
    look(input,0,100000);play.advance(0,input,Camera{});
    const auto down=resolveGameCamera(play.presentationScene());
    const auto forward=math::normalized(down->target-down->position);
    require(forward[1]<-.99f&&std::hypot(forward[0],forward[2])>.01,"Pitch clamp allowed the camera to flip.");
    ticks(play,input,20);key(input,SDL_SCANCODE_W);ticks(play,input,60);
    require(position(play.document().scene())[0]>4.4f&&position(play.document().scene())[1]<.1f,
        "Movement used camera pitch or ignored mouse yaw.");
    const auto record=play.document().scene().find(3);
    require(record->cameraFollow->offset==settings.offset&&record->cameraFollow->worldBasis==settings.worldBasis,
        "Runtime look overwrote authored Camera Follow.");
    require(play.document().edit("Rename unrelated entity",[](auto& records,auto&){records.front().name="Renamed floor";}),"Could not rename runtime floor.");
    play.advance(0,input,Camera{});
    require(math::normalized(resolveGameCamera(play.presentationScene())->target-resolveGameCamera(play.presentationScene())->position)[1]<-.99f,
        "Unrelated scene edit reset mouse look.");
    require(play.document().edit("Teleport",[](auto& records,auto&){for(auto& record:records)if(record.id==2)record.local[12]+=2;}),"Could not teleport FPS player.");
    play.advance(Tick,input,Camera{});require(!input.ownsKeyboard()&&input.movement()==Vec2{},"Teleport retained captured WASD input.");
    settings.offset={0,1.4f,0};settings.worldBasis=math::eulerRotationXYZ({0,.3f,0});
    require(updateCameraAuthoring(play.document(),3,settings),"Could not adjust FPS camera in Play.");
    play.advance(0,input,Camera{});
    const auto edited=resolveGameCamera(play.presentationScene());
    require(edited&&std::abs(edited->position[1]-position(play.presentationScene())[1]-1.4f)<1e-4,
        "Runtime camera did not accept an editor adjustment.");
}
void diagonalAndFrameRateIndependence() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-runtime";SceneDocument authored(root);fixture(authored);
    PlaySession a(root),b(root),c(root);require(a.start(authored)&&b.start(authored)&&c.start(authored),"Could not start FPS timing worlds.");
    GameInput ia,ib,ic;activate(ia);activate(ib);activate(ic);ticks(a,ia,20);ticks(b,ib,20);ticks(c,ic,20);
    key(ia,SDL_SCANCODE_W);key(ib,SDL_SCANCODE_W);key(ic,SDL_SCANCODE_W);key(ic,SDL_SCANCODE_D);
    ticks(a,ia,60);ticks(c,ic,60);
    for(int i=0;i<240;++i)b.advance(Tick/4,ib,Camera{});
    require(math::length(position(a.document().scene())-position(b.document().scene()))<1e-5,"Render rate changed FPS movement.");
    const auto straight=position(a.document().scene()),diagonal=position(c.document().scene());
    require(std::abs(std::hypot(straight[0],straight[2])-std::hypot(diagonal[0],diagonal[2]))<.02,
        "Diagonal input exceeded the walking speed.");
}
void cancellationAndCameraLifetime() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-runtime";SceneDocument authored(root);fixture(authored);
    PlaySession play(root);require(play.start(authored),"Could not start FPS cancellation fixture.");GameInput input;activate(input);ticks(play,input,20);
    key(input,SDL_SCANCODE_SPACE);ticks(play,input,1);key(input,SDL_SCANCODE_SPACE,false);
    double previous=position(play.document().scene())[1];bool buffered=false;
    for(int i=0;i<100;++i) {
        ticks(play,input,1);const double height=position(play.document().scene())[1];
        if(height<previous&&height>.06&&height<.3) {
            key(input,SDL_SCANCODE_SPACE);ticks(play,input,1);
            input.cancel();play.advance(0,input,Camera{});input.acquire();
            const double beforeLanding=position(play.document().scene())[1];
            for(int step=0;step<30;++step){ticks(play,input,1);require(position(play.document().scene())[1]<beforeLanding+.05,
                "An airborne jump buffer survived cancellation and immediate reacquisition.");}
            buffered=true;break;
        }
        previous=height;
    }
    require(buffered,"Cancellation fixture did not reach its descending jump window.");
    require(play.document().edit("Remove active camera",[](auto& records,auto& properties){
        std::erase_if(records,[](const auto& record){return record.id==3;});properties.activeCamera=NoEntity;
    }),"Could not remove the FPS camera.");
    play.advance(0,input,Camera{},false);play.advance(0,input,Camera{},true);const auto before=play.tickCount();
    ticks(play,input,1);require(play.tickCount()==before+1,"Removing a camera during suspension stopped Play.");
}
}
int main(){try{movementJumpAndOwnership();lookCameraAuthoringAndEdits();diagonalAndFrameRateIndependence();cancellationAndCameraLifetime();
    std::cout<<"FPS controller checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
