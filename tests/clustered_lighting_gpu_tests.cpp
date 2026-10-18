#include "crimson-renderer/ClusteredLightingGpu.h"
#include "GpuTestBackend.h"
#include "crimson-renderer/ClusteredLighting.h"
#include "crimson-renderer/Picking.h"
#include "crimson-renderer/PointLightRecord.h"
#include <bgfx/platform.h>
#include <SDL3/SDL.h>
#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include "cs_cluster_lights.h"

namespace {
using namespace crimson;
constexpr bgfx::ViewId ComputeView=0,ReadView=1;
void check(bool success,const std::string& message){if(!success)throw std::runtime_error(message);}
struct Sdl {
    Sdl(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}
    ~Sdl(){SDL_Quit();}
};
struct Graphics {
    explicit Graphics(SDL_Window* window) {
        auto* hwnd=SDL_GetPointerProperty(SDL_GetWindowProperties(window),SDL_PROP_WINDOW_WIN32_HWND_POINTER,nullptr);
        check(hwnd!=nullptr,std::string("SDL did not provide a hidden HWND: ")+SDL_GetError());
        bgfx::renderFrame();
        bgfx::Init init;init.type=selectedTestBgfxRenderer();init.platformData.nwh=hwnd;
        init.resolution.width=32;init.resolution.height=32;init.resolution.reset=BGFX_RESET_NONE;
        check(bgfx::init(init),"Could not initialize the selected hidden clustered-light backend.");
        check(bgfx::getRendererType()==init.type,"Clustered-light checks initialized a different graphics backend.");
    }
    ~Graphics(){bgfx::shutdown();}
};
struct Program {
    bgfx::ProgramHandle handle{BGFX_INVALID_HANDLE};
    Program() {
        const auto shader=bgfx::createShader(bgfx::copy(cc_cs_cluster_lights,sizeof cc_cs_cluster_lights));
        check(bgfx::isValid(shader),"Could not create clustered-light compute shader.");
        handle=bgfx::createProgram(shader,true);
        check(bgfx::isValid(handle),"Could not create clustered-light compute program.");
    }
    ~Program(){if(bgfx::isValid(handle))bgfx::destroy(handle);}
};
struct Texture {
    bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};
    ~Texture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}
};
using Readback=std::vector<Vec4>;

void exercise(std::vector<std::shared_ptr<Readback>>& outstandingDestinations) {
    Program program;ClusteredLightingGpu lighting(program.handle);
    std::uint32_t frame{};
    const auto tick=[&]{frame=bgfx::frame();lighting.advance(frame);};
    const auto expectDispatch=[&](std::uint32_t expected,const char* reason) {
        // bgfx reports completed submission counters with a frame of latency.
        // Drain the bounded queue and count the one submitted workload once.
        std::uint32_t dispatched{};
        for(unsigned drain=0;drain<4;++drain){tick();dispatched+=bgfx::getStats()->numCompute;}
        check(dispatched==expected,std::string(reason)+" Observed "+std::to_string(dispatched)+" dispatches.");
    };
    const auto read=[&] {
        const auto width=lighting.atlasWidth(),height=lighting.atlasHeight();
        Texture staging{bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA32F,
            BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK)};
        check(bgfx::isValid(staging.handle),"Could not allocate RGBA32F cluster-mask readback.");
        auto pixels=std::make_shared<Readback>(std::size_t(width)*height);
        // Keep every destination alive through bgfx shutdown, including a
        // timeout or another assertion while an asynchronous read is pending.
        outstandingDestinations.push_back(pixels);
        bgfx::blit(ReadView,staging.handle,0,0,lighting.texture(),0,0,width,height);
        const auto ready=bgfx::readTexture(staging.handle,pixels->data());
        check(ready!=UINT32_MAX,"RGBA32F cluster-mask readback is unsupported.");
        for(unsigned attempt=0;attempt<120;++attempt) {
            tick();if(picking::frameReady(frame,ready))return pixels;
        }
        throw std::runtime_error("RGBA32F cluster-mask readback timed out.");
    };
    Camera camera;camera.position={0,0,5};camera.target={};camera.fov=60;
    camera.fovAxis=CameraFovAxis::Vertical;camera.nearPlane=.1f;camera.farPlane=50;
    std::vector<PointLight> lights(ClusteredLightingGpu::MaxLights);
    std::vector<std::int32_t> shadows(lights.size(),-1);
    for(std::size_t i=0;i<lights.size();++i) {
        auto& light=lights[i];light.position={};light.color={.2f,.4f,.6f};light.intensity=1;
        light.enabled=true;light.radius=1000;light.entityId=i+1;light.castShadows=false;
    }
    // A 3x3 LUT formerly made a three-texel atlas row. Padded record uploads
    // must not overlap masks, including after the five-texel response extension.
    std::vector<float> brdf(3*3*2);
    for(std::size_t i=0;i<brdf.size();++i)brdf[i]=static_cast<float>(i+1)/32.f;
    const auto prepare=[&](std::span<const PointLight> values,std::span<const std::int32_t> indices,
        std::uint16_t width=32,std::uint16_t height=32) {
        check(lighting.prepare(camera,width,height,values,indices,brdf,ComputeView),lighting.status());
    };
    const auto verify=[&](const Readback& pixels) {
        const auto atlasWidth=lighting.atlasWidth();
        check(atlasWidth>=3 && std::has_single_bit(static_cast<std::uint32_t>(atlasWidth)),
            "Odd BRDF size produced a non-power-of-two atlas width.");
        for(std::size_t y=0;y<3;++y)for(std::size_t x=0;x<3;++x)for(std::size_t channel=0;channel<2;++channel)
            check(std::bit_cast<std::uint32_t>(pixels[y*atlasWidth+x][channel])==
                std::bit_cast<std::uint32_t>(brdf[(y*3+x)*2+channel]),"Cluster atlas changed an odd-sized BRDF lookup value.");
        const auto& stats=lighting.stats();
        check(stats.activeLights==4096 && stats.wordCount==128 && stats.clusterCount==24,
            "Full-capacity mask fixture lost active lights or clusters.");
        const auto begin=lighting.maskBaseTexel();
        const auto maskTexels=std::size_t(stats.clusterCount)*(stats.wordCount/4);
        check(std::size_t(begin)+maskTexels<=pixels.size(),"Cluster mask region escaped its atlas.");
        for(std::size_t texel=0;texel<maskTexels;++texel)for(std::size_t channel=0;channel<4;++channel)
            check(std::bit_cast<std::uint32_t>(pixels[begin+texel][channel])==UINT32_MAX,
                "Cluster mask lost bits through row padding or uint/float transport.");
    };
    prepare(lights,shadows);
    expectDispatch(1,"Cold clustered-light data did not dispatch culling exactly once.");
    const auto first=read();verify(*first);
    const auto lightBase=3u*lighting.atlasWidth();
    check(lighting.maskBaseTexel()==lightBase+
        point_lighting::pointLightRecordRows(ClusteredLightingGpu::MaxLights,lighting.atlasWidth())*lighting.atlasWidth(),
        "Light records and masks disagree about their padded reservation.");
    for(const auto source:{0u,1u,17u,4095u}) {
        const auto firstRecord=lightBase+source*CC_POINT_LIGHT_RECORD_TEXELS;
        check((*first)[firstRecord+3][1]==0&&(*first)[firstRecord+3][0]==-1&&
            (*first)[firstRecord+3][2]==float(lights[source].roles.receiverMask),
            "Point response packing changed shadow or lighting role records.");
        check((*first)[firstRecord+4]==Vec4{0,1,4,1},"Ordinary point-light response defaults changed.");
        check((*first)[firstRecord+5]==Vec4{2,0,0,0},"Default attenuation was not transported.");
    }
    const auto oldRed=(*first)[lightBase+1][0];

    lights[0].color[0]=.8f;lighting.setComputeProgram(program.handle);
    prepare(lights,shadows);
    expectDispatch(0,"A color-only light update or unchanged program handle reculled static masks.");
    const auto recolored=read();verify(*recolored);
    check((*recolored)[lightBase+1][0]>oldRed,"Color-only light update did not reach the fragment light records.");
    const auto firstMasks=lighting.maskBaseTexel();
    for(std::size_t i=0;i<32;++i)for(std::size_t channel=0;channel<4;++channel)
        check(std::bit_cast<std::uint32_t>((*first)[firstMasks+i][channel])==
            std::bit_cast<std::uint32_t>((*recolored)[firstMasks+i][channel]),
            "A color-only record upload corrupted the first cluster's cached masks.");

    lights[0].nearFalloffRadius=1.25f;lights[0].specularStrength=.35f;
    lights.back().nearFalloffRadius=2;lights.back().specularStrength=0;
    prepare(lights,shadows);expectDispatch(0,"A near-falloff/specular-only update unnecessarily reculled static masks.");
    const auto softened=read();verify(*softened);
    check((*softened)[lightBase+4]==Vec4{1.5625f,.35f,4,1}&&
        (*softened)[lightBase+(lights.size()-1)*CC_POINT_LIGHT_RECORD_TEXELS+4]==Vec4{4,0,4,1},
        "Point response edits did not update the first and last packed source records.");
    const auto maskTexels=std::size_t(lighting.stats().clusterCount)*(lighting.stats().wordCount/4);
    for(std::size_t i=0;i<maskTexels;++i)for(std::size_t channel=0;channel<4;++channel)
        check(std::bit_cast<std::uint32_t>((*softened)[firstMasks+i][channel])==
            std::bit_cast<std::uint32_t>((*recolored)[firstMasks+i][channel]),
            "A point response edit corrupted or changed spatial light masks.");
    lights[0].specularStrength=.75f;prepare(lights,shadows);expectDispatch(0,"Specular-only edit invalidated spatial light masks.");
    check((*read())[lightBase+4]==Vec4{1.5625f,.75f,4,1},"Specular-only edit was omitted from the cached light snapshot.");
    {
        const auto defaultEnergy=read();const auto defaultBytes=lighting.residentBytes();
        lights.front().indirectEnergy=0;lights.back().indirectEnergy=4;
        prepare(lights,shadows);expectDispatch(0,"A GI-energy-only edit unnecessarily reculled spatial light masks.");
        const auto editedEnergy=read();
        check((*editedEnergy)[lightBase+5]==Vec4{2,-1,0,0}&&
            (*editedEnergy)[lightBase+(lights.size()-1)*CC_POINT_LIGHT_RECORD_TEXELS+5]==Vec4{2,3,0,0},
            "GI energy did not update the first/last records using delta-from-one encoding.");
        for(std::size_t texel=0;texel<editedEnergy->size();++texel)for(std::size_t channel=0;channel<4;++channel) {
            const bool energyValue=channel==1&&(texel==lightBase+5||texel==lightBase+(lights.size()-1)*CC_POINT_LIGHT_RECORD_TEXELS+5);
            if(!energyValue)check(std::bit_cast<std::uint32_t>((*editedEnergy)[texel][channel])==
                std::bit_cast<std::uint32_t>((*defaultEnergy)[texel][channel]),
                "A GI energy edit changed direct response, spatial masks, BRDF data or reserved record values.");
        }
        check(lighting.residentBytes()==defaultBytes,"GI energy changed clustered light allocation size.");
        lights.front().indirectEnergy=std::numeric_limits<float>::quiet_NaN();lights.back().indirectEnergy=std::numeric_limits<float>::infinity();
        prepare(lights,shadows);expectDispatch(0,"Nonfinite GI energy changed spatial culling.");
        const auto finiteEnergy=read();
        check((*finiteEnergy)[lightBase+5]==Vec4{2,0,0,0}&&
            (*finiteEnergy)[lightBase+(lights.size()-1)*CC_POINT_LIGHT_RECORD_TEXELS+5]==Vec4{2,0,0,0},
            "Nonfinite transient GI energy failed to restore the default one multiplier.");
        lights.front().indirectEnergy=-100;lights.back().indirectEnergy=100;
        prepare(lights,shadows);expectDispatch(0,"GI energy bounds changed spatial culling.");
        const auto boundedEnergy=read();
        check((*boundedEnergy)[lightBase+5]==Vec4{2,-1,0,0}&&
            (*boundedEnergy)[lightBase+(lights.size()-1)*CC_POINT_LIGHT_RECORD_TEXELS+5]==Vec4{2,15,0,0},
            "Transient GI energy bounds escaped the renderer's zero-to-sixteen contract.");
        lights.front().indirectEnergy=1;lights.back().indirectEnergy=1;
        prepare(lights,shadows);expectDispatch(0,"Restoring default GI energy changed spatial masks.");
        const auto restoredEnergy=read();
        for(std::size_t texel=0;texel<restoredEnergy->size();++texel)for(std::size_t channel=0;channel<4;++channel)
            check(std::bit_cast<std::uint32_t>((*restoredEnergy)[texel][channel])==
                std::bit_cast<std::uint32_t>((*defaultEnergy)[texel][channel]),
                "Default GI energy did not restore the exact legacy atlas bytes.");
    }
    lights[0].attenuation=0;lights.back().attenuation=3;
    prepare(lights,shadows);expectDispatch(0,"Attenuation-only edit unnecessarily reculled spatial light masks.");
    const auto decay=read();
    check((*decay)[lightBase+5]==Vec4{0,0,0,0}&&
        (*decay)[lightBase+(lights.size()-1)*CC_POINT_LIGHT_RECORD_TEXELS+5]==Vec4{3,0,0,0},
        "Distance attenuation edits failed to reach first/last light records.");
    lights[0].attenuation=std::numeric_limits<float>::quiet_NaN();prepare(lights,shadows);
    expectDispatch(0,"Invalid attenuation changed culling spheres.");
    check((*read())[lightBase+5]==Vec4{2,0,0,0},"Nonfinite attenuation did not restore the inverse-square default.");
    lights[0].attenuation=-100;prepare(lights,shadows);
    check((*read())[lightBase+5]==Vec4{-10,0,0,0},"Negative attenuation limit was not enforced.");
    lights[0].attenuation=100;prepare(lights,shadows);
    check((*read())[lightBase+5]==Vec4{10,0,0,0},"Positive attenuation limit was not enforced.");
    lights[0].attenuation=2;lights.back().attenuation=2;
    lights[0].nearFalloffRadius=std::numeric_limits<float>::quiet_NaN();lights[0].specularStrength=std::numeric_limits<float>::quiet_NaN();
    prepare(lights,shadows);expectDispatch(0,"Invalid response sanitization changed spatial light masks.");
    check((*read())[lightBase+4]==Vec4{0,1,4,1},"Nonfinite point response values reached shaders instead of ordinary-light defaults.");
    lights[0].nearFalloffRadius=-1;lights[0].specularStrength=-1;
    prepare(lights,shadows);expectDispatch(0,"Negative response sanitization changed spatial light masks.");
    check((*read())[lightBase+4]==Vec4{0,0,4,1},"Negative point response values were not bounded.");
    lights[0].nearFalloffRadius=1e9f;lights[0].specularStrength=100;
    prepare(lights,shadows);expectDispatch(0,"Large response sanitization changed spatial light masks.");
    check((*read())[lightBase+4]==Vec4{1e10f,1,4,1},"Point response limits overflowed or exceeded their shader contract.");

    prepare(lights,shadows);
    expectDispatch(0,"Unchanged camera, lights, BRDF and configuration repeated culling.");
    prepare({},{});
    expectDispatch(0,"An empty light set dispatched unnecessary culling.");
    check(lighting.stats().activeLights==0 && lighting.stats().wordCount==0,"Empty light set retained stale active-light metadata.");
    prepare(lights,shadows);
    expectDispatch(1,"Restoring lights after an empty set reused stale masks.");verify(*read());

    camera.fov=75;prepare(lights,shadows);
    expectDispatch(1,"Changing camera FOV failed to invalidate clustered masks.");verify(*read());
    check(!lighting.prepare(camera,32,32,lights,shadows,{},ComputeView),"Incomplete BRDF data was accepted.");
    prepare(lights,shadows);
    expectDispatch(1,"Recovering from invalid BRDF data reused an incoherent publication.");verify(*read());

    prepare({},{},5120,2880);
    expectDispatch(0,"An empty adaptive grid dispatched unnecessary culling.");
    check(lighting.stats().clusterCount==80*45*24,"Large viewport did not adapt its tile size conservatively.");
    prepare(lights,shadows);
    expectDispatch(1,"Restoring the small viewport reused the adaptive grid's masks.");verify(*read());

    PointLight point;point.position={};point.radius=8;point.intensity=1000000;
    const std::int32_t noShadow=-1;
    const auto preparePoint=[&] {prepare(std::span{&point,1},std::span{&noShadow,1},256,256);};
    const auto lightBit=[&](const Readback& pixels,const Vec3& world) {
        clustered_lighting::Grid grid;
        check(clustered_lighting::build(grid,camera,256,256,std::span{&point,1}),"Finite point oracle failed.");
        const auto view=camera.viewMatrix();
        const auto pixel=math::project(world,camera.projectionMatrix(1,ClipDepth::ZeroToOne)*view,
            {0,0,256,256},ClipDepth::ZeroToOne);
        check(pixel&&(*pixel)[0]>=0&&(*pixel)[0]<256&&(*pixel)[1]>=0&&(*pixel)[1]<256,
            "Finite point mask sample left the viewport.");
        const auto cluster=grid.clusterForPixelDepth((*pixel)[0],(*pixel)[1],-math::transformPoint(view,world)[2]);
        return std::bit_cast<std::uint32_t>(pixels[lighting.maskBaseTexel()+cluster][0])&1u;
    };
    preparePoint();expectDispatch(1,"Finite point sphere did not update cluster assignment.");
    const auto finitePixels=read();const auto pointBase=3u*lighting.atlasWidth();
    check((*finitePixels)[pointBase+4]==Vec4{0,1,4,1}&&(*finitePixels)[pointBase][3]==1000000,
        "Finite point packing lost ordinary response defaults or migrated intensity range.");
    check(lightBit(*finitePixels,{})==1&&lightBit(*finitePixels,{0,0,-12})==0,
        "Finite point culling missed its source or retained distant clusters beyond its radius.");
    point.rangeFalloffExponent=1;point.edgeSoftness=.25f;point.sourceRadius=2;
    preparePoint();expectDispatch(0,"Falloff exponent, edge softness or source radius unnecessarily reculled static masks.");
    const auto tuned=read();
    check((*tuned)[pointBase+4]==Vec4{0,1,1,.25f}&&(*tuned)[pointBase+3][1]==2,
        "Falloff/source-radius-only edits were omitted from the cached GPU record.");
    check((*tuned)[pointBase+2][3]>8&&(*tuned)[pointBase+2][3]<8.001f,
        "Source Radius changed the authored finite influence sphere.");
    point.rangeFalloffExponent=std::numeric_limits<float>::quiet_NaN();
    point.edgeSoftness=std::numeric_limits<float>::infinity();point.sourceRadius=std::numeric_limits<float>::quiet_NaN();
    preparePoint();expectDispatch(0,"Nonfinite response sanitization changed spatial masks.");
    const auto sanitized=read();
    check((*sanitized)[pointBase+4]==Vec4{0,1,4,1}&&(*sanitized)[pointBase+3][1]==0,
        "Nonfinite range/source-radius values escaped ordinary defaults.");
    point.rangeFalloffExponent=100;point.edgeSoftness=-1;point.sourceRadius=1e9f;
    preparePoint();expectDispatch(0,"Response bound clamping changed spatial masks.");
    const auto bounded=read();
    check((*bounded)[pointBase+4]==Vec4{0,1,8,.05f}&&(*bounded)[pointBase+3][1]==100000,
        "Range/source-radius bounds were not enforced in GPU records.");
    point.position[1]=30;preparePoint();expectDispatch(1,"Moving only the finite point source reused stale masks.");
    check(lightBit(*read(),{})==0,"Moving the finite source did not clear its old illumination sphere.");

    constexpr std::uint16_t screenWidth=321,screenHeight=243;
    const auto withScreenEffects=[&](bool gi,bool ao) {
        return lighting.prepare(camera,screenWidth,screenHeight,{}, {},brdf,ComputeView,false,
            screenWidth,screenHeight,
            gi?screenWidth:0,gi?screenHeight:0,ao?screenWidth:0,ao?screenHeight:0);
    };
    check(withScreenEffects(true,true),lighting.status());
    const auto packed=(std::size_t(screenWidth)*screenHeight+3)/4;
    const auto pixels=std::size_t(screenWidth)*screenHeight;
    check(lighting.ssgiBaseTexel()>=lighting.occluderBaseTexel()+packed,
        "SSGI overlaps packed nearest-layer depth.");
    check(lighting.ssaoBaseTexel()==lighting.ssgiBaseTexel()+pixels&&
        lighting.ssaoBaseTexel()+packed<=std::size_t(lighting.atlasWidth())*lighting.atlasHeight(),
        "Odd-size SSGI and SSAO regions overlap or escape the atlas.");
    check(lighting.ssaoWidth()==screenWidth&&lighting.ssaoHeight()==screenHeight,
        "SSAO publication lost its odd viewport extent.");
    for(unsigned i=0;i<4;++i)tick();
    const auto combinedBytes=lighting.residentBytes();
    check(withScreenEffects(false,true),lighting.status());
    check(lighting.ssgiBaseTexel()==0&&lighting.ssaoBaseTexel()>=lighting.occluderBaseTexel()+packed,
        "Disabling SSGI left stale metadata or invalidated independent SSAO.");
    check(withScreenEffects(false,false),lighting.status());
    for(unsigned i=0;i<4;++i)tick();
    check(!lighting.ssaoWidth()&&!lighting.ssaoHeight()&&!lighting.ssaoBaseTexel()&&
        lighting.residentBytes()<combinedBytes,"Disabled screen effects retained their atlas allocation.");
    check(!lighting.prepare(camera,screenWidth,screenHeight,{}, {},brdf,ComputeView,false,0,0,0,0,screenWidth,0),
        "An incomplete SSAO target extent was accepted.");

}
}

int main() {
    try {
        Sdl sdl;
        std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("Crimson clustered-light GPU checks",32,32,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(bool(window),SDL_GetError());
        // This storage precedes the graphics lifetime so pending GPU writes
        // retain their destinations until shutdown also on exceptional exits.
        std::vector<std::shared_ptr<Readback>> outstandingDestinations;
        Graphics graphics(window.get());exercise(outstandingDestinations);
        std::cout<<"Clustered-light odd BRDF packing, mask bits, static reuse and recovery checks passed.\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
