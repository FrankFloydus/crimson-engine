#include "navigation_fixture.h"
#include "runtime/PlaySession.h"
#include "runtime/GameInput.h"
#include "scene/SceneExtraction.h"
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
using crimson::testing::startWithNavigation;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
constexpr EntityId Player=2,Follow=3;
constexpr float Ground=-.1180819273f;
void fixture(SceneDocument& document) {
    require(document.edit("Click fixture",[](auto& records,auto& properties){
        EntityRecord floor;floor.id=1;floor.name="Floor";floor.local=math::translation({0,Ground-.5f,0});
        floor.collider=SceneCollider{{20,1,20}};records.push_back(floor);
        EntityRecord player;player.id=Player;player.name="Player";player.actor.emplace();player.controller.emplace();player.motor.emplace();player.navigation.emplace();player.playerInput.emplace();records.push_back(player);
        EntityRecord follow;follow.id=Follow;follow.name="Camera";follow.camera.emplace();
        follow.camera->verticalFov=60;follow.camera->farPlane=2035;
        follow.cameraFollow.emplace();follow.cameraFollow->target=Player;follow.cameraFollow->offset={5.222835f,6.438390f,4.948770f};
        // The authored three-quarter orientation and offset are deliberately
        // different: rays must use the actual camera basis, not look at Player.
        follow.local=*math::inverse(math::lookAt({8,10,8},{0,0,0},{0,1,0}));
        follow.cameraFollow->worldBasis=follow.local;
        follow.cameraFollow->worldBasis[12]=follow.cameraFollow->worldBasis[13]=follow.cameraFollow->worldBasis[14]=0;
        records.push_back(follow);properties.playerCharacter=Player;properties.activeCamera=Follow;
    }),"Could not create click fixture.");
}
Vec2 visiblePixel(const Camera& camera,Vec3 world,const ViewportRegion& image,std::uint32_t width,std::uint32_t height) {
    // Project with the renderer's physical framebuffer dimensions, then map
    // its pixel to the actual desktop image rectangle used by ImGui.
    const auto projected=math::project(world,camera.projectionMatrix(static_cast<float>(width)/height,ClipDepth::ZeroToOne)*camera.viewMatrix(),
        {0,0,static_cast<float>(width),static_cast<float>(height)},ClipDepth::ZeroToOne);
    require(projected.has_value(),"Fixture point is behind the camera.");
    return {image.x+(*projected)[0]/width*image.width,image.y+(*projected)[1]/height*image.height};
}
void displayedImageDestination() {
    struct Layout {ViewportRegion image;std::uint32_t width,height;};
    // Saved detached Game dimensions, another monitor with negative origin,
    // and portrait/docked logical images with rounded physical pixel extents.
    const std::array<Layout,4> layouts{{
        {{458,143,1914,1101,true,false,9},1914,1101},
        {{-1900,150,956.3f,600.7f,true,false,10},1434,901},
        {{2600,580,600.3f,900.7f,true,false,11},1201,1801},
        {{390,72,1170,638,true,false,12},1170,638}
    }};
    for(const auto& layout:layouts) {
        const auto root=std::filesystem::temp_directory_path()/"crimson-game-click-unused";
        SceneDocument authored(root);fixture(authored);PlaySession play(root);
        require(startWithNavigation(play,authored),"Could not start click fixture.");
        GameInput input;input.setViewport(layout.image,true,false);input.acquire();
        auto displayed=*resolveGameCamera(play.presentationScene());
        input.setProjection(displayed,layout.width,layout.height);
        const Vec3 destination{2,Ground,-1.5f};
        const auto pixel=visiblePixel(displayed,destination,layout.image,layout.width,layout.height);
        SDL_Event click{};click.type=SDL_EVENT_MOUSE_BUTTON_DOWN;click.button.windowID=layout.image.windowId;
        click.button.button=SDL_BUTTON_LEFT;click.button.x=pixel[0];click.button.y=pixel[1];
        require(input.handleEvent(click),"Click was outside the displayed image.");
        // Motion after the press must not move its endpoint; UI receives that
        // later position, just as in the native event + ImGui handoff.
        const auto later=visiblePixel(displayed,{-4,Ground,-4},layout.image,layout.width,layout.height);
        input.submitUiPointer(later,true);
        // An edit after the image was presented must not change the old click
        // ray. This is the same boundary as follow/resize/camera-switch events.
        auto unrelatedCamera=displayed;unrelatedCamera.position[0]+=25;unrelatedCamera.target[0]+=25;
        play.advance(PlaySession::FixedStep,input,unrelatedCamera);
        require(!play.remainingPath().empty() && play.remainingPath().size()<=play.path().size(),
            "The clicked route did not expose its remaining navigation corners.");
        for(int frame=0;frame<240;++frame) {
            displayed=*resolveGameCamera(play.presentationScene());
            input.setProjection(displayed,layout.width,layout.height);
            input.submitUiPointer(later,false);
            play.advance(PlaySession::FixedStep,input,displayed);
        }
        const auto final=math::transformPoint(*play.document().scene().worldTransform(Player),{});
        require(std::hypot(final[0]-destination[0],final[2]-destination[2])<.06f,
            "Character did not stop at the original clicked world point in the moving detached view.");
        require(play.remainingPath().empty(),"The arrived route retained stale navigation lines.");
        const auto pose=*play.document().scene().worldTransform(Player);
        input.submitUiPointer(visiblePixel(displayed,{-3,Ground,2},layout.image,layout.width,layout.height),false);
        for(int frame=0;frame<30;++frame)play.advance(PlaySession::FixedStep,input,displayed);
        const auto idle=*play.document().scene().worldTransform(Player);
        require(std::abs(pose[0]-idle[0])<1e-5f && std::abs(pose[8]-idle[8])<1e-5f,
            "Mouse motion rotated an idle character after arrival.");
        require(authored.scene().worldTransform(Player)->at(0,3)==0 && authored.dirty(),
            "Click movement modified the dirty authored document.");
    }
}
}
int main(){try{displayedImageDestination();std::cout<<"Game click projection and arrival checks passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
