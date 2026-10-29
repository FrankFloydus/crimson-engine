#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/RenderPassPlan.h"
#include <crimson/renderer/core/Smaa.hpp>
#include "resources/ResourceManager.h"
#include "GpuTestBackend.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace crimson;
void check(bool condition,const std::string& message) {if(!condition)throw std::runtime_error(message);}
struct Sdl {Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~Sdl(){SDL_Quit();}};
struct Texture {
    bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};
    ~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}
};
float halfFloat(std::uint16_t h) {
    const auto exponent=(h>>10)&31u,mantissa=h&1023u;
    const float magnitude=exponent?std::ldexp(1.f+mantissa/1024.f,int(exponent)-15):std::ldexp(float(mantissa),-24);
    return (h&32768u)?-magnitude:magnitude;
}
std::shared_ptr<ModelAsset> diagonalQuad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="smaa-quad";ModelPrimitive primitive;
    primitive.uvMask=3;primitive.tangentUv=0;
    constexpr std::array<Vec3,4> corners{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    for(const auto point:corners) {
        ModelVertex vertex;vertex.position=point;vertex.normal={0,0,1};vertex.tangent={1,0,0,-1};
        primitive.vertices.push_back(vertex);math::include(asset->bounds,point);
    }
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    asset->materials.emplace_back();return asset;
}
void exercise(SDL_Window* window) {
    // Readback buffers survive Renderer, including an exceptional teardown.
    std::vector<std::shared_ptr<std::vector<std::uint8_t>>> retained;
    ResourceManager resources;Renderer renderer(native_renderer_context(window,nullptr,selectedTestGraphicsBackend()));
    TestGpuContext context;std::uint32_t frame{};
    bgfx::reset(320,240,BGFX_RESET_NONE);renderer.setGridVisible(false);
    const auto tick=[&]{resources.pump();renderer.advanceStreaming();frame=renderer.advance_frame();context.flush();};
    const auto read=[&](bgfx::TextureHandle source,std::uint16_t w,std::uint16_t h,bgfx::TextureFormat::Enum format,unsigned bytesPerPixel) {
        Texture staging{bgfx::createTexture2D(w,h,false,1,format,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"SMAA staging allocation failed.");
        auto bytes=std::make_shared<std::vector<std::uint8_t>>(std::size_t(w)*h*bytesPerPixel);retained.push_back(bytes);
        bgfx::blit(DiagnosticReadbackView,staging.handle,0,0,source,0,0,w,h);
        const auto ready=bgfx::readTexture(staging.handle,bytes->data());check(ready!=UINT32_MAX,"SMAA readback unsupported.");
        for(unsigned tries=0;tries<120;++tries){tick();if(picking::frameReady(frame,ready))return *bytes;}
        renderer.drainPending(frame);throw std::runtime_error("SMAA readback timed out.");
    };
    {
        shared::core::Smaa smaa;
        const auto filter=[&](std::uint16_t w,std::uint16_t h,const std::vector<std::uint8_t>& pixels) {
            check(smaa.prepare(w,h).ok(),"SMAA scratch preparation failed.");
            Texture source{bgfx::createTexture2D(w,h,false,1,bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP,bgfx::copy(pixels.data(),std::uint32_t(pixels.size())))};
            check(bgfx::isValid(source.handle),"SMAA input allocation failed.");
            smaa.record_edges(145,source.handle);smaa.record_weights(146);
            smaa.record_blend(147,source.handle,smaa.output_framebuffer());
            return read(smaa.output_texture(),w,h,bgfx::TextureFormat::RGBA16F,8);
        };
        constexpr std::array<std::uint8_t,4> flat{55,120,180,96};
        for(const auto extent:std::array<std::array<std::uint16_t,2>,3>{{{65,49},{1,1},{37,23}}}) {
            const auto w=extent[0],h=extent[1];std::vector<std::uint8_t> pixels(std::size_t(w)*h*4);
            for(std::size_t i=0;i<pixels.size();++i)pixels[i]=flat[i%4];
            const auto output=filter(w,h,pixels);
            for(std::size_t i=0;i<output.size()/2;++i) {
                std::uint16_t value;std::memcpy(&value,output.data()+2*i,2);
                check(std::abs(halfFloat(value)-flat[i%4]/255.f)<.0015f,"SMAA changed flat linear color or alpha.");
            }
        }
        const auto generation=smaa.generation();const auto handle=smaa.output_texture();
        check(!smaa.prepare(0,23).ok()&&smaa.generation()==generation&&smaa.output_texture().idx==handle.idx,
            "Invalid SMAA resize replaced the accepted resources.");
        constexpr std::uint16_t W=65,H=49;std::vector<std::uint8_t> pixels(W*H*4);
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
            const auto pixel=(y*W+x)*4;const auto value=x>=8+y/2?255u:0u;
            for(unsigned c=0;c<3;++c)pixels[pixel+c]=std::uint8_t(value);pixels[pixel+3]=255;
        }
        const auto output=filter(W,H,pixels);unsigned partial{};
        for(unsigned y=0;y<H;++y)for(unsigned x=0;x<W;++x) {
            const auto pixel=y*W+x;std::uint16_t value;std::memcpy(&value,output.data()+pixel*8,2);const auto red=halfFloat(value);
            if(red>.01f&&red<.99f)++partial;
            if(x+3<8+y/2||x>11+y/2)check(std::abs(red-pixels[pixel*4]/255.f)<.0015f,"SMAA blurred flat pixels away from an edge.");
        }
        check(partial>20,"SMAA did not reconstruct diagonal edge coverage.");
    }
    // Exercise the actual rich pass chain with independent Scene/Game settings.
    renderer.resizeViewport(161,121);renderer.resizeGameViewport(161,121);
    ViewportAppearance appearance;appearance.backgroundSrgb={};renderer.setAppearance(appearance);
    PreparedEnvironment environment;environment.settings={2,8,8,16,16,32};environment.key=0x534d4141;
    environment.data=ibl::bake({4,2,std::vector<Vec3>(8,Vec3{})},environment.settings);
    check(renderer.publishEnvironment(environment),renderer.environmentStatus());
    auto mesh=diagonalQuad();check(renderer.prepareModel(*mesh),renderer.modelStatus());
    auto surfaces=mesh->materials;surfaces[0].baseColor={1,1,1,1};
    RenderModelInstance instance;instance.entityId=1;instance.model.asset=mesh;instance.materials=&surfaces;instance.shading=MaterialShading::Unlit;
    instance.model.transform=math::rotation(math::axisAngle({0,0,1},.37f));
    SceneSettings settings;settings.camera.position={0,0,4};settings.camera.target={};settings.camera.nearPlane=.1f;settings.camera.farPlane=32;
    settings.environment.enabled=false;settings.environment.background.show=false;settings.environment.toneMapping=ToneMapping::Linear;
    settings.environment.ambientLight.source=AmbientLightSource::Disabled;
    SceneSettings gameSettings=settings;RenderScene scene{settings,{}},game{gameSettings,{}};
    scene.sceneMode=game.sceneMode=true;scene.instances=game.instances=std::span(&instance,1);
    const auto capture=[&] {
        renderer.render(scene);renderer.renderGame(game);
        auto first=read(renderer.viewportTexture(),std::uint16_t(renderer.width()),std::uint16_t(renderer.height()),bgfx::TextureFormat::RGBA8,4);
        auto second=read(renderer.gameViewportTexture(),std::uint16_t(renderer.width()),std::uint16_t(renderer.height()),bgfx::TextureFormat::RGBA8,4);
        return std::array{std::move(first),std::move(second)};
    };
    const auto off=capture();check(off[0]==off[1],"Identical Scene/Game Off views diverged.");
    gameSettings.antiAliasing=AntiAliasing::Smaa;const auto mixed=capture();
    check(mixed[0]==off[0],"Game SMAA changed Scene's Off output.");
    unsigned changed{};for(std::size_t i=0;i<off[1].size();i+=4)if(mixed[1][i]!=off[1][i])++changed;
    check(changed>30,"Rich SMAA did not filter the scene silhouette.");
    settings.antiAliasing=AntiAliasing::Smaa;const auto both=capture();
    check(both[0]==both[1],"Independent Scene/Game SMAA targets diverged.");
    check(capture()==both,"Spatial SMAA changed on an identical consecutive frame.");
    settings.antiAliasing=gameSettings.antiAliasing=AntiAliasing::Off;
    check(capture()==off,"Switching SMAA Off failed to restore the exact baseline.");
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::RawData);
    renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::RawData);const auto rawOff=capture();
    settings.antiAliasing=gameSettings.antiAliasing=AntiAliasing::Smaa;
    check(capture()==rawOff,"SMAA modified RawData inspection.");
    renderer.setDebugView(RenderViewPurpose::Scene,RenderDebugView::Lit);renderer.setDebugView(RenderViewPurpose::Game,RenderDebugView::Lit);
    renderer.resizeViewport(83,57);renderer.resizeGameViewport(83,57);const auto resized=capture();
    check(resized[0]==resized[1]&&resized[0].size()==83u*57u*4u,"SMAA failed odd-extent resize/view isolation.");
    resources.stop();renderer.drainPending(frame);
    std::cout<<"SMAA linear/alpha preservation, diagonal coverage, transactional resize, Scene/Game isolation, Off restoration and RawData bypass passed on "<<renderer.backendName()<<".\n";
}
}
int main() {
    try {Sdl sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
        SDL_CreateWindow("Crimson SMAA regression",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());exercise(window.get());return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
