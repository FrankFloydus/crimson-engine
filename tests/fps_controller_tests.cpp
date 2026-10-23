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
        player.movement.emplace();player.playerInput.emplace();player.local=math::translation({0,.05f,0});records.push_back(player);
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
void playerRootRotationDefinesCameraForward() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-runtime";
    SceneDocument authored(root);fixture(authored);
    constexpr double quarterTurn=std::numbers::pi/2;
    require(authored.edit("Rotate Player",[](auto& records,auto&){
        for(auto& record:records)if(record.id==2)
            record.local=math::translation({0,.05f,0})*math::rotation(math::axisAngle({0,1,0},quarterTurn));
    }),"Could not rotate the authored Player.");
    auto settings=defaultPlayerCameraTemplate();settings.offset[1]=1.73f;settings.lens.verticalFov=86;
    require(updateCameraAuthoring(authored,3,settings),"Could not author a rotated Player's eye height and FOV.");
    const auto authoredView=resolveGameCamera(authored.scene());
    require(authoredView&&math::length(math::normalized(authoredView->target-authoredView->position)-Vec3{-1,0,0})<1e-5,
        "The authored FPS camera did not face the rotated Player's forward direction.");
    PlaySession play(root);require(play.start(authored),"Could not start a rotated FPS Player.");GameInput input;
    play.advance(0,input,Camera{});
    const auto initial=resolveGameCamera(play.presentationScene());
    require(initial&&math::length(math::normalized(initial->target-initial->position)-Vec3{-1,0,0})<1e-5&&play.tickCount()==0,
        "Play ignored the Player's root rotation before input acquisition.");
    require(std::abs(initial->position[1]-position(play.presentationScene())[1]-settings.offset[1])<1e-5&&
        std::abs(initial->fov-settings.lens.verticalFov)<1e-5,"Root rotation lost authored eye height or FOV.");
    activate(input);ticks(play,input,20);key(input,SDL_SCANCODE_W);ticks(play,input,60);
    require(position(play.document().scene())[0]<-4.4f&&std::abs(position(play.document().scene())[2])<.001f,
        "W moved along the old camera direction instead of the rotated Player's forward direction.");
    key(input,SDL_SCANCODE_W,false);ticks(play,input,90);
    look(input,-450,0);const auto beforeLook=play.tickCount();play.advance(0,input,Camera{});
    const auto turned=resolveGameCamera(play.presentationScene());
    require(turned&&math::normalized(turned->target-turned->position)[2]>.7f&&play.tickCount()==beforeLook,
        "Rotated Player mouse look waited for a physics tick or applied root yaw twice.");
    key(input,SDL_SCANCODE_W);
    require(play.document().edit("Rotate Player during Play",[](auto& records,auto&){
        for(auto& record:records)if(record.id==2)
            record.local=math::translation(math::transformPoint(record.local,{}))*math::rotation(math::axisAngle({0,1,0},-.6));
    }),"Could not rotate the Player during Play.");
    play.advance(0,input,Camera{});
    const auto edited=resolveGameCamera(play.presentationScene());
    const Vec3 editedForward{static_cast<float>(std::sin(.6)),0,static_cast<float>(-std::cos(.6))};
    require(!input.ownsKeyboard()&&input.movement()==Vec2{},"Player rotation retained captured movement input.");
    require(edited&&math::length(math::normalized(edited->target-edited->position)-editedForward)<1e-5,
        "Live Player rotation retained the old camera yaw or transient look.");
    require(play.document().edit("Rotate Player without Game input",[](auto& records,auto&){
        for(auto& record:records)if(record.id==2)
            record.local=math::translation(math::transformPoint(record.local,{}))*math::rotation(math::axisAngle({0,1,0},-quarterTurn));
    }),"Could not rotate the Player with Game input released.");
    play.advance(0,input,Camera{});
    const auto released=resolveGameCamera(play.presentationScene());
    require(released&&math::length(math::normalized(released->target-released->position)-Vec3{1,0,0})<1e-5&&
        std::abs(released->position[1]-position(play.presentationScene())[1]-settings.offset[1])<1e-5&&
        std::abs(released->fov-settings.lens.verticalFov)<1e-5,
        "Player rotation with input released lost camera forward, eye height or FOV.");
    require(authored.scene().find(2)->local==math::translation({0,.05f,0})*math::rotation(math::axisAngle({0,1,0},quarterTurn)),
        "Live root rotation changed the authored Player.");
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
void staircaseFixture(SceneDocument& document) {
    fixture(document);
    require(document.edit("Add staircase",[](auto& records,auto&){
        // Adjacent solid treads exercise both Jolt's walk-up correction and its
        // grounded step-down snap. The wide final tread leaves room to brake.
        for(int i=0;i<6;++i) {
            const float left=1.f+1.2f*i,height=.18f*(i+1);
            const float width=i==5?8.f:1.2f;
            EntityRecord step;step.id=10+i;step.name="Stair tread";
            step.local=math::translation({left+width*.5f,height*.5f,0});
            step.collider=SceneCollider{{width,height,4}};records.push_back(step);
        }
    }),"Could not add the FPS staircase fixture.");
}
double eyeResidual(const PlaySession& play,double eye=defaultPlayerCameraTemplate().offset[1]) {
    const auto camera=resolveGameCamera(play.presentationScene());
    require(camera.has_value(),"Stair camera disappeared during Play.");
    return camera->position[1]-position(play.presentationScene())[1]-eye;
}
void reachFirstSmoothedStep(PlaySession& play,GameInput& input) {
    ticks(play,input,20);key(input,SDL_SCANCODE_D);
    for(int i=0;i<60;++i) {
        ticks(play,input,1);
        if(eyeResidual(play)<-.015)return;
    }
    require(false,"Stair ascent did not produce a camera-only smoothing offset.");
}
void staircaseAscentDescentAndSettling() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-stairs";
    SceneDocument authored(root);staircaseFixture(authored);
    PlaySession play(root);require(play.start(authored),"Could not start the FPS staircase fixture.");
    GameInput input;activate(input);ticks(play,input,20);
    const double initialEye=resolveGameCamera(play.presentationScene())->position[1];
    double previousActor=position(play.presentationScene())[1],previousEye=initialEye;
    double largestAscent=0,largestEyeAscent=0;int rises=0;bool reachedStairs=false;
    key(input,SDL_SCANCODE_D);
    for(int i=0;i<180;++i) {
        ticks(play,input,1);
        const auto actor=position(play.presentationScene());
        const double eye=resolveGameCamera(play.presentationScene())->position[1];
        if(actor[1]>previousActor+.04)++rises;
        largestAscent=std::max(largestAscent,actor[1]-previousActor);
        largestEyeAscent=std::max(largestEyeAscent,eye-previousEye);
        reachedStairs=reachedStairs || actor[1]>.1f;
        if(!reachedStairs)require(eye>=initialEye-1e-4,"Stair smoothing dipped the eye before the first ascent.");
        require(eyeResidual(play)<=1e-3,"Ascending stair smoothing overshot the authored eye height.");
        previousActor=actor[1];previousEye=eye;
        if(actor[0]>9)break;
    }
    require(rises>=5 && position(play.document().scene())[1]>1.f,"The staircase fixture did not traverse multiple ascending treads.");
    require(largestAscent>.08 && largestEyeAscent<largestAscent*.7,"Stair ascent still moved the eye by the capsule's abrupt rise.");
    key(input,SDL_SCANCODE_D,false);ticks(play,input,120);
    require(std::abs(eyeResidual(play))<1e-4,"Stair ascent left the idle eye below its authored offset.");

    previousActor=position(play.presentationScene())[1];previousEye=resolveGameCamera(play.presentationScene())->position[1];
    double largestDescent=0,largestEyeDescent=0;int drops=0;
    key(input,SDL_SCANCODE_A);
    for(int i=0;i<200;++i) {
        ticks(play,input,1);
        const auto actor=position(play.presentationScene());
        const double eye=resolveGameCamera(play.presentationScene())->position[1];
        if(actor[1]<previousActor-.04)++drops;
        largestDescent=std::max(largestDescent,previousActor-actor[1]);
        largestEyeDescent=std::max(largestEyeDescent,previousEye-eye);
        require(eyeResidual(play)>=-1e-3,"Descending stair smoothing overshot the authored eye height.");
        previousActor=actor[1];previousEye=eye;
        if(actor[0]<-.5f)break;
    }
    require(drops>=5 && position(play.document().scene())[1]<.1f,"The staircase fixture did not traverse multiple descending treads.");
    require(largestDescent>.08 && largestEyeDescent<largestDescent*.7,"Stair descent still moved the eye by the capsule's abrupt drop.");
    key(input,SDL_SCANCODE_A,false);ticks(play,input,120);
    require(std::abs(eyeResidual(play))<1e-4,"Stair descent left the idle eye above its authored offset.");
    require(!play.document().dirty() && play.document().history().size()==0,"Camera stair smoothing authored runtime scene data.");
}
void staircaseRenderTimingAndJumpIsolation() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-stairs";
    SceneDocument authored(root);staircaseFixture(authored);
    PlaySession slow(root),fast(root);require(slow.start(authored)&&fast.start(authored),"Could not start stair timing fixtures.");
    GameInput slowInput,fastInput;activate(slowInput);activate(fastInput);ticks(slow,slowInput,20);ticks(fast,fastInput,20);
    key(slowInput,SDL_SCANCODE_D);key(fastInput,SDL_SCANCODE_D);
    // Compare the same quarter-tick presentation phase after each 60 Hz or
    // 240 Hz render interval, including the first discontinuous stair rise.
    slow.advance(Tick/4,slowInput,Camera{});fast.advance(Tick/4,fastInput,Camera{});
    bool smoothed=false;
    for(int i=0;i<105;++i) {
        slow.advance(Tick,slowInput,Camera{});
        for(int frame=0;frame<4;++frame)fast.advance(Tick/4,fastInput,Camera{});
        require(std::abs(slow.interpolation()-fast.interpolation())<1e-9,"Stair timing fixtures compared different presentation phases.");
        require(std::abs(resolveGameCamera(slow.presentationScene())->position[1]-resolveGameCamera(fast.presentationScene())->position[1])<1e-5,
            "Stair camera smoothing depended on the render rate.");
        require(math::length(position(slow.document().scene())-position(fast.document().scene()))<1e-5,
            "Camera smoothing altered fixed-tick stair movement.");
        smoothed=smoothed || eyeResidual(slow)<-.015;
    }
    require(smoothed,"Render timing fixtures did not reach a smoothed stair transition.");

    SceneDocument flat(root);fixture(flat);PlaySession jump(root);require(jump.start(flat),"Could not start flat jump camera fixture.");
    GameInput jumpInput;activate(jumpInput);ticks(jump,jumpInput,20);
    key(jumpInput,SDL_SCANCODE_SPACE);ticks(jump,jumpInput,1);key(jumpInput,SDL_SCANCODE_SPACE,false);
    for(int i=0;i<150;++i) {
        jump.advance(Tick/2,jumpInput,Camera{});
        require(std::abs(eyeResidual(jump))<1e-5,"Stair smoothing delayed a flat-ground jump or landing.");
    }
    PlaySession stairJump(root);require(stairJump.start(authored),"Could not start stair jump camera fixture.");
    GameInput stairInput;activate(stairInput);reachFirstSmoothedStep(stairJump,stairInput);
    // Fixed-tick calls present the previous pose at alpha=0. Publish almost
    // all of the already accepted walking tick before measuring the residual,
    // so its final stair correction cannot be mistaken for a jump correction.
    const auto beforePresentation=stairJump.tickCount();
    stairJump.advance(Tick*.9999,stairInput,Camera{});
    require(stairJump.tickCount()==beforePresentation,"Stair jump setup unexpectedly advanced the physics clock.");
    const double residual=eyeResidual(stairJump),startHeight=position(stairJump.document().scene())[1];
    key(stairInput,SDL_SCANCODE_D,false);key(stairInput,SDL_SCANCODE_SPACE);
    ticks(stairJump,stairInput,1);key(stairInput,SDL_SCANCODE_SPACE,false);
    require(position(stairJump.document().scene())[1]>startHeight+.025,"Stair jump fixture did not leave support on its queued jump.");
    double highest=startHeight;
    for(int i=0;i<32;++i) {
        ticks(stairJump,stairInput,1);highest=std::max(highest,static_cast<double>(position(stairJump.document().scene())[1]));
        require(eyeResidual(stairJump)>=residual-.003,"Jumping from stairs fed ballistic motion into camera step smoothing.");
    }
    require(highest-startHeight>.8,"Jumping from a stair interrupted the normal jump arc.");
    require(std::abs(eyeResidual(stairJump))<.01,"Residual stair smoothing did not settle naturally during the jump.");
}
void staircaseCameraResets() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-stairs";
    SceneDocument authored(root);staircaseFixture(authored);
    for(int change=0;change<5;++change) {
        PlaySession play(root);require(play.start(authored),"Could not start a stair reset fixture.");
        GameInput input;activate(input);reachFirstSmoothedStep(play,input);double eye=defaultPlayerCameraTemplate().offset[1];
        if(change==0) {
            require(play.document().edit("Teleport on stairs",[](auto& records,auto&){
                for(auto& record:records)if(record.id==2){record.local[12]+=3;record.local[13]+=2;}
            }),"Could not teleport the stair player.");
        } else if(change==1) {
            auto settings=defaultPlayerCameraTemplate();settings.offset[1]=1.42f;eye=settings.offset[1];
            require(updateCameraAuthoring(play.document(),3,settings),"Could not edit a camera during a stair transition.");
        } else if(change==2) {
            require(play.document().edit("Replace stair camera",[](auto& records,auto& properties){
                auto camera=*std::find_if(records.begin(),records.end(),[](const auto& record){return record.id==3;});
                camera.id=4;records.push_back(camera);properties.activeCamera=4;
            }),"Could not replace the active stair camera.");
        } else if(change==3)play.advance(0,input,Camera{},false);
        else require(play.document().beginTransform("Begin stair player transform",{2}),"Could not lock the stair player for an editor gesture.");
        play.advance(0,input,Camera{});
        require(std::abs(eyeResidual(play,eye))<1e-5,"A camera edit, teleport, replacement, suspension or transform lock retained stale stair smoothing.");
        if(change==4)play.document().cancel();
    }
}
void staircaseAuthoredResponseAndDisable() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-stairs";
    SceneDocument fastAuthored(root),slowAuthored(root),disabledAuthored(root);
    const auto authorTime=[](SceneDocument& document,float seconds) {
        staircaseFixture(document);
        require(document.edit("Set stair smoothing",[seconds](auto& records,auto&){
            for(auto& record:records)if(record.id==2)record.movement->stairSmoothingTime=seconds;
        }),"Could not author a stair smoothing response.");
    };
    authorTime(fastAuthored,.05f);authorTime(slowAuthored,.3f);authorTime(disabledAuthored,0);
    PlaySession fast(root),slow(root),disabled(root);
    require(fast.start(fastAuthored)&&slow.start(slowAuthored)&&disabled.start(disabledAuthored),
        "Could not start authored stair response fixtures.");
    GameInput fastInput,slowInput,disabledInput;activate(fastInput);activate(slowInput);activate(disabledInput);
    ticks(fast,fastInput,20);ticks(slow,slowInput,20);ticks(disabled,disabledInput,20);
    key(fastInput,SDL_SCANCODE_D);key(slowInput,SDL_SCANCODE_D);key(disabledInput,SDL_SCANCODE_D);
    const auto advanceTogether=[&] {
        ticks(fast,fastInput,1);ticks(slow,slowInput,1);ticks(disabled,disabledInput,1);
        require(math::length(position(fast.document().scene())-position(slow.document().scene()))<1e-5 &&
            math::length(position(fast.document().scene())-position(disabled.document().scene()))<1e-5,
            "Authored camera stair smoothing changed physical player movement.");
        require(std::abs(eyeResidual(disabled))<1e-5,"Zero stair smoothing still produced a camera offset.");
    };
    bool stepped=false;
    for(int i=0;i<60;++i) {
        advanceTogether();
        if(position(fast.document().scene())[1]>.1f){stepped=true;break;}
    }
    require(stepped,"Authored stair response fixtures did not reach the first tread.");
    key(fastInput,SDL_SCANCODE_D,false);key(slowInput,SDL_SCANCODE_D,false);key(disabledInput,SDL_SCANCODE_D,false);
    for(int i=0;i<8;++i)advanceTogether();
    const double fastLag=std::abs(eyeResidual(fast)),slowLag=std::abs(eyeResidual(slow));
    require(slowLag>.03 && fastLag<slowLag*.25,
        "A longer authored stair smoothing time did not retain more lag or settle more slowly.");
    for(int i=0;i<90;++i)advanceTogether();
    require(std::abs(eyeResidual(slow))<.001,"The slower authored stair response failed to settle at the eye height.");
    key(fastInput,SDL_SCANCODE_D);key(slowInput,SDL_SCANCODE_D);key(disabledInput,SDL_SCANCODE_D);
    for(int i=0;i<110;++i)advanceTogether();
    require(position(disabled.document().scene())[1]>1.f,"Disabled stair smoothing fixture did not cross the remaining treads.");
}
void staircaseLiveResponseEditing() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-stairs";
    SceneDocument authored(root);staircaseFixture(authored);
    PlaySession play(root);require(play.start(authored),"Could not start the live stair response fixture.");
    GameInput input;activate(input);reachFirstSmoothedStep(play,input);
    look(input,0,120);play.advance(Tick*.9999,input,Camera{});
    const auto beforeEdit=resolveGameCamera(play.presentationScene());
    const auto forward=math::normalized(beforeEdit->target-beforeEdit->position);
    require(forward[1]<-.15f,"Live stair response fixture did not establish an independent mouse look.");
    const auto editTime=[&](float seconds) {
        require(play.document().edit("Adjust stair smoothing",[seconds](auto& records,auto&){
            for(auto& record:records)if(record.id==2)record.movement->stairSmoothingTime=seconds;
        }),"Could not edit stair smoothing during Play.");
    };
    const auto beforeTicks=play.tickCount();const auto beforeActor=position(play.document().scene());
    editTime(.2f);play.advance(0,input,Camera{});
    require(play.tickCount()==beforeTicks && position(play.document().scene())==beforeActor && input.ownsKeyboard(),
        "A live stair response edit advanced physics, moved the capsule or released gameplay input.");
    require(eyeResidual(play)<-.01,"A positive live stair response edit discarded the pending camera correction.");
    auto camera=resolveGameCamera(play.presentationScene());
    require(math::length(math::normalized(camera->target-camera->position)-forward)<1e-5,
        "A positive live stair response edit reset mouse look.");

    editTime(0);play.advance(0,input,Camera{});
    require(play.tickCount()==beforeTicks && position(play.document().scene())==beforeActor && input.ownsKeyboard(),
        "Disabling stair smoothing advanced physics, moved the capsule or released gameplay input.");
    require(std::abs(eyeResidual(play))<1e-5,"Disabling stair smoothing without a fixed tick retained pending camera lag.");
    camera=resolveGameCamera(play.presentationScene());
    require(math::length(math::normalized(camera->target-camera->position)-forward)<1e-5,
        "Disabling stair smoothing reset mouse look.");
    const double firstHeight=position(play.document().scene())[1];bool disabledStep=false;
    for(int i=0;i<40;++i) {
        ticks(play,input,1);
        require(std::abs(eyeResidual(play))<1e-5,"A later step regenerated camera lag while stair smoothing was disabled.");
        if(position(play.document().scene())[1]>firstHeight+.1){disabledStep=true;break;}
    }
    require(disabledStep,"Live stair response fixture did not cross a tread with smoothing disabled.");
    require(play.document().undo(),"Could not undo disabling stair smoothing.");
    play.advance(0,input,Camera{});
    require(play.document().scene().find(2)->movement->stairSmoothingTime==.2f && input.ownsKeyboard(),
        "Undo did not restore the authored stair response and preserve gameplay input ownership.");
    require(std::abs(eyeResidual(play))<1e-5,"Reenabling stair smoothing restored stale camera lag.");
    bool resumed=false;
    for(int i=0;i<45;++i){ticks(play,input,1);if(eyeResidual(play)<-.015){resumed=true;break;}}
    require(resumed,"Undo did not reenable camera smoothing for subsequent stairs.");
    camera=resolveGameCamera(play.presentationScene());
    require(math::length(math::normalized(camera->target-camera->position)-forward)<1e-5,
        "Editing and undoing stair smoothing reset the player's view direction.");
}
void compactStaircaseCameraContinuity() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-compact-stairs";
    for(bool triangles:{false,true})for(double speed:{5.,8.}) {
        SceneDocument authored(root);fixture(authored);
        require(authored.edit("Compact staircase",[triangles](auto& records,auto&){
            for(auto& record:records)if(record.id==2)record.movement->stairSmoothingTime=.15f;
            // Match ordinary close-spaced architectural stairs. The rounded
            // capsule finishes each ascent in its normal sweep after Jolt's
            // stair helper; separate triangle meshes also match Quader imports.
            for(int i=0;i<8;++i) {
                const float width=i==7?6.f:.375f,height=.25f;
                EntityRecord step;step.id=10+i;step.name="Compact stair tread";
                step.local=math::translation({1.f+.375f*i+width*.5f,.25f*i+height*.5f,0});
                step.collider=SceneCollider{{width,height,4}};
                if(triangles) {
                    step.collider->shape=ColliderShape::Trimesh;
                    step.collider->mesh=makeCollisionMesh({
                        {-width*.5f,-height*.5f,-2},{width*.5f,-height*.5f,-2},
                        {width*.5f,height*.5f,-2},{-width*.5f,height*.5f,-2},
                        {-width*.5f,-height*.5f,2},{width*.5f,-height*.5f,2},
                        {width*.5f,height*.5f,2},{-width*.5f,height*.5f,2}},
                        {0,3,2,0,2,1,4,5,6,4,6,7,0,4,7,0,7,3,
                         1,2,6,1,6,5,0,1,5,0,5,4,3,7,6,3,6,2});
                }
                records.push_back(step);
            }
        }),"Could not author compact staircase.");
        PlaySession play(root),coarse(root);require(play.start(authored)&&coarse.start(authored),"Could not start compact staircase.");
        require(play.setAttributeBase(2,stats::MovementSpeed,speed),"Could not set compact stair walking speed.");
        require(coarse.setAttributeBase(2,stats::MovementSpeed,speed),"Could not set compact stair comparison speed.");
        GameInput input,coarseInput;activate(input);activate(coarseInput);
        ticks(play,input,20);ticks(coarse,coarseInput,20);key(input,SDL_SCANCODE_D);key(coarseInput,SDL_SCANCODE_D);
        constexpr double Frame=Tick/4;
        play.advance(Frame,input,Camera{});coarse.advance(Frame,coarseInput,Camera{});
        double previousEye=resolveGameCamera(play.presentationScene())->position[1],previousVelocity=0;
        double largestVelocityChange=0,largestLag=0;bool reachedTop=false;
        for(int frame=0;frame<1000;++frame) {
            play.advance(Frame,input,Camera{});
            if(frame%4==3) {
                coarse.advance(Tick,coarseInput,Camera{});
                require(std::abs(play.interpolation()-coarse.interpolation())<1e-9,
                    "Compact stairs compared different render phases.");
                require(std::abs(resolveGameCamera(play.presentationScene())->position[1]-
                    resolveGameCamera(coarse.presentationScene())->position[1])<1e-5,
                    "Compact stair camera changed between 60 and 240 Hz rendering.");
                require(math::length(position(play.document().scene())-position(coarse.document().scene()))<1e-5,
                    "Compact stair render timing changed physical movement.");
            }
            const auto actor=position(play.presentationScene());
            const double eye=resolveGameCamera(play.presentationScene())->position[1];
            const double velocity=(eye-previousEye)/Frame;
            largestVelocityChange=std::max(largestVelocityChange,std::abs(velocity-previousVelocity));
            largestLag=std::max(largestLag,-eyeResidual(play));
            require(velocity>-.02,"Compact stair ascent made the eye move backward.");
            previousEye=eye;previousVelocity=velocity;
            if(actor[0]>4.2f){reachedTop=actor[1]>1.95f;break;}
        }
        require(reachedTop,"Compact staircase was not traversed.");
        // Bound the change of eye velocity, not only the biggest single rise:
        // a missed 3 cm sweep produces a visible spike between small frames.
        require(largestVelocityChange<.18,"Compact stairs still produce abrupt camera velocity spikes.");
        if(speed==8.)require(largestLag>.55,"Fast compact stairs clipped camera lag at the former fixed bound.");
        key(input,SDL_SCANCODE_D,false);ticks(play,input,150);
        require(std::abs(eyeResidual(play))<1e-4,"Compact stair camera failed to settle after stopping.");
    }
}
void staircaseStrafingAndDiagonalInput() {
    const auto root=std::filesystem::temp_directory_path()/"crimson-fps-stair-strafing";
    SceneDocument authored(root);fixture(authored);
    require(authored.edit("Add strafing staircase",[](auto& records,auto&){
        for(int i=0;i<8;++i) {
            const float width=i==7?8.f:.375f;
            EntityRecord step;step.id=10+i;step.name="Strafing stair tread";
            step.local=math::translation({1.f+.375f*i+width*.5f,.25f*i+.125f,0});
            step.collider=SceneCollider{{width,.25f,80}};records.push_back(step);
        }
    }),"Could not author strafing staircase.");
    struct Approach {SDL_Scancode first,second;float lookX;int lateral;};
    // Pure A/D must work while looking across the stairs. W+A/W+D and
    // oblique pure strafing must also retain a valid upward stair attempt.
    const Approach approaches[]{
        {SDL_SCANCODE_D,SDL_SCANCODE_UNKNOWN,0,0},
        {SDL_SCANCODE_A,SDL_SCANCODE_UNKNOWN,1800,0},
        {SDL_SCANCODE_W,SDL_SCANCODE_D,900,1},
        {SDL_SCANCODE_W,SDL_SCANCODE_A,900,-1},
        {SDL_SCANCODE_D,SDL_SCANCODE_UNKNOWN,450,1},
        {SDL_SCANCODE_A,SDL_SCANCODE_UNKNOWN,1350,-1}};
    for(const auto& approach:approaches) {
        PlaySession play(root);require(play.start(authored),"Could not start strafing staircase.");
        GameInput input;activate(input);ticks(play,input,20);
        look(input,approach.lookX,0);play.advance(0,input,Camera{});
        const auto initialCamera=resolveGameCamera(play.presentationScene());
        const auto forward=math::normalized(initialCamera->target-initialCamera->position);
        key(input,approach.first);
        if(approach.second!=SDL_SCANCODE_UNKNOWN)key(input,approach.second);
        bool reachedTop=false,smoothed=false;
        for(int tick=0;tick<240;++tick) {
            ticks(play,input,1);const auto actor=position(play.document().scene());
            smoothed=smoothed || eyeResidual(play)<-.015;
            if(actor[0]>4.25f){reachedTop=actor[1]>1.95f;break;}
        }
        require(reachedTop,"A/D strafing or diagonal WASD movement stalled on a climbable stair riser.");
        require(smoothed,"Strafing stair ascent bypassed camera smoothing.");
        if(approach.lateral)require(approach.lateral*position(play.document().scene())[2]>1.f,
            "Diagonal stair traversal discarded the requested lateral movement.");
        key(input,approach.first,false);
        if(approach.second!=SDL_SCANCODE_UNKNOWN)key(input,approach.second,false);
        ticks(play,input,120);
        require(std::abs(eyeResidual(play))<1e-4,"Strafing stair smoothing failed to settle after stopping.");
        const auto camera=resolveGameCamera(play.presentationScene());
        require(math::length(math::normalized(camera->target-camera->position)-forward)<1e-5,
            "Stair traversal changed mouse-look direction to face the stairs.");
        require(!play.document().dirty() && play.document().history().size()==0,
            "Strafing stair traversal changed authored scene data.");
    }
}
}
int main(){try{movementJumpAndOwnership();lookCameraAuthoringAndEdits();playerRootRotationDefinesCameraForward();diagonalAndFrameRateIndependence();cancellationAndCameraLifetime();
    staircaseAscentDescentAndSettling();staircaseRenderTimingAndJumpIsolation();staircaseCameraResets();
    staircaseAuthoredResponseAndDisable();staircaseLiveResponseEditing();compactStaircaseCameraContinuity();staircaseStrafingAndDiagonalInput();
    std::cout<<"FPS controller checks passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
