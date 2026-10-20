#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "resources/ResourceManager.h"
#include "scene/SceneDocument.h"
#include "scene/SceneExtraction.h"
#include "scene/SceneOutlinerRows.h"
#include "assets/BuiltinGeometry.h"
#include "GpuTestBackend.h"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {
using namespace crimson;
void check(bool condition,const std::string& message) {
    if(!condition)throw std::runtime_error(message);
}
struct Sdl {
    Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}
    ~Sdl(){SDL_Quit();}
};
struct Texture {
    bgfx::TextureHandle value{BGFX_INVALID_HANDLE};
    ~Texture(){if(bgfx::isValid(value))bgfx::destroy(value);}
};
bool sameWorld(const Mat4& actual,const Mat4& expected) {
    if(!math::isAffine(actual)||math::handedness(actual)==0)return false;
    for(unsigned i=0;i<16;++i)
        if(!std::isfinite(actual[i])||std::abs(actual[i]-expected[i])>1e-5f)return false;
    return true;
}
void visible(const DecodedImage& image,const std::string& label) {
    unsigned litPixels{};double energy{};
    for(std::size_t pixel=0;pixel<image.rgba.size()/4;++pixel) {
        const auto offset=pixel*4;
        const auto intensity=std::max({image.rgba[offset],image.rgba[offset+1],image.rgba[offset+2]});
        litPixels+=intensity>12;
        energy+=intensity;
    }
    check(litPixels>100&&energy>10000,label+": default PBR Box has no measurable visible, point-lit surface (pixels="+
        std::to_string(litPixels)+", energy="+std::to_string(energy)+").");
}
void equivalent(const DecodedImage& actual,const DecodedImage& expected,const std::string& label) {
    check(actual.width==expected.width&&actual.height==expected.height&&actual.rgba.size()==expected.rgba.size(),
        label+": image dimensions changed.");
    unsigned maximum{};
    for(std::size_t i=0;i<actual.rgba.size();++i)
        maximum=std::max(maximum,unsigned(std::abs(int(actual.rgba[i])-int(expected.rgba[i]))));
    check(maximum<=2,label+": parenting changed visible geometry/ordinary point-light response (maximum RGBA error="+
        std::to_string(maximum)+").");
}
void exercise(SDL_Window* window) {
    ResourceManager resources;
    Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    TestGpuContext context;
    bgfx::reset(320,240,BGFX_RESET_NONE);
    renderer.setGridVisible(false);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    constexpr std::uint16_t Width=161,Height=121;
    renderer.resizeViewport(Width,Height);renderer.resizeGameViewport(Width,Height);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x504c41594552424f;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    const auto mesh=builtinBoxGeometry();
    check(renderer.prepareModel(*mesh),renderer.modelStatus());
    std::vector<ModelMaterialTextures> textures(mesh->materials.size());
    SceneSettings settings;
    settings.camera.position={3,2.4f,4};settings.camera.target={0,0,0};
    settings.camera.nearPlane=.1f;settings.camera.farPlane=30;
    settings.environment.enabled=false;settings.environment.background.show=false;
    settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    settings.environment.toneMapping=ToneMapping::Linear;
    std::uint32_t frame{};
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle target) {
        Texture staging{bgfx::createTexture2D(Width,Height,false,1,bgfx::TextureFormat::RGBA8,
            BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.value),"Player parenting readback allocation failed.");
        DecodedImage image{Width,Height,std::vector<std::uint8_t>(std::size_t(Width)*Height*4)};
        bgfx::blit(DiagnosticReadbackView,staging.value,0,0,target,0,0,Width,Height);
        const auto ready=bgfx::readTexture(staging.value,image.rgba.data());
        check(ready!=UINT32_MAX,"Player parenting readback is unsupported.");
        for(unsigned i=0;i<120;++i){tick();if(picking::frameReady(frame,ready))return image;}
        renderer.drainPending(frame);throw std::runtime_error("Player parenting readback timed out.");
    };
    const auto runCase=[&](const std::string& label,const Mat4& playerLocal,const Mat4& boxLocal) {
        // SceneDocument's constructor and entity authoring are in-memory. This
        // project path is never created, loaded or saved by this regression.
        SceneDocument document(std::filesystem::temp_directory_path()/"crimson-parenting-gpu-in-memory");
        const auto player=document.createPlayer(),box=document.createBox(),light=document.createPointLight();
        check(player&&box&&light,document.status());
        auto playerRecord=*document.scene().find(player);playerRecord.local=playerLocal;
        check(document.updateEntity(player,playerRecord),document.status());
        auto boxRecord=*document.scene().find(box);boxRecord.local=boxLocal;
        check(document.updateEntity(box,boxRecord),document.status());
        auto lightRecord=*document.scene().find(light);lightRecord.local=math::translation({2,3,4});
        check(document.updateEntity(light,lightRecord),document.status());
        const auto originalWorld=document.scene().worldTransform(box);
        check(bool(originalWorld)&&sameWorld(*originalWorld,boxLocal),label+": invalid original world transform.");
        const auto capture=[&](bool attached) {
            auto extracted=extractScene(document.scene());
            check(extracted.models.size()==1&&extracted.models.front().id==box&&extracted.models.front().visible,
                label+": extraction omitted the visible Box.");
            const auto& source=extracted.models.front();
            check(sameWorld(source.world,*originalWorld),label+": extracted world transform moved or became singular.");
            check(source.actorOwner==(attached?player:NoEntity),label+": wrong extracted Actor ownership.");
            check(source.lightingCategory==(attached?lighting_scope::Character:lighting_scope::World),
                label+": the regression did not exercise World-to-Character lighting classification.");
            check(extracted.points.size()==1&&extracted.points.front().enabled,
                label+": default all-scope point light was omitted or replaced by a derived rig light.");
            RenderModelInstance instance;instance.entityId=source.id;instance.model.asset=mesh;
            instance.model.transform=source.world;instance.materials=&mesh->materials;instance.textures=&textures;
            instance.forceFullDetail=true;instance.lightingScopes=source.lightingScopes;
            instance.lightingCategory=source.lightingCategory;instance.actorOwner=source.actorOwner;
            instance.castShadows=source.castShadows;instance.receiveShadows=source.receiveShadows;
            RenderScene scene{settings,{}};scene.sceneMode=true;scene.instances={&instance,1};
            scene.pointLights=extracted.points;scene.directionalLights=extracted.directional;
            const auto draw=[&]{renderer.render(scene);renderer.renderGame(scene);};
            for(unsigned i=0;i<4;++i){draw();tick();}
            draw();return std::array{read(renderer.viewportTexture()),read(renderer.gameViewportTexture())};
        };
        const auto before=capture(false);
        visible(before[0],label+" Scene before parenting");visible(before[1],label+" Game before parenting");
        document.select({box});check(document.reparentSelection(player),document.status());
        check(document.scene().find(box)->parent==player&&document.selection().primary==box,
            label+": reparenting lost the actual parent or the dragged Box selection.");
        const auto rows=sceneOutlinerRows(document.scene());
        const auto row=std::find_if(rows.begin(),rows.end(),[&](const auto& value){return value.entity==box;});
        check(row!=rows.end()&&row->parent==player,label+": Box disappeared from the actual Player child rows.");
        const auto after=capture(true);
        for(unsigned view=0;view<2;++view) {
            visible(after[view],label+" after parenting");
            equivalent(after[view],before[view],label+(view?" Game":" Scene"));
        }
        check(document.undo(),document.status());
        const auto undone=capture(false);
        check(document.scene().find(box)->parent==NoEntity,label+": undo did not detach the Box.");
        for(unsigned view=0;view<2;++view)equivalent(undone[view],before[view],label+" undo");
        check(document.redo(),document.status());
        const auto redone=capture(true);
        for(unsigned view=0;view<2;++view)equivalent(redone[view],before[view],label+" redo");
    };
    runCase("default Player and Box",Mat4{},Mat4{});
    Mat4 shear;shear[4]=.2f;shear[8]=-.12f;
    runCase("translated Player and reflected affine Box",
        math::translation({2,0,-1})*math::eulerRotationXYZ({0,.6f,0}),
        math::translation({.1f,.15f,0})*math::eulerRotationXYZ({.1f,.2f,0})*shear*math::scaling({-1.1f,.9f,1.2f}));
    resources.stop();renderer.drainPending(frame);
    std::cout<<"Player parenting kept actual Box rows, selection, nonsingular world transforms and visible default PBR point-lit pixels in Scene/Game through undo/redo on "
        <<renderer.backendName()<<".\n";
}
}
int main() {
    try {
        Sdl sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson Player parenting regression",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get());return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
