#include "crimson-renderer/Renderer.h"
#include "crimson-renderer/Environment.h"
#include "crimson-renderer/ImGuiBgfx.h"
#include "crimson-renderer/Picking.h"
#include "resources/TextureStreamer.h"
#include "resources/ShaderReloadSession.h"
#include "assets/AtomicFile.h"
#include "assets/MaterialAsset.h"
#include "assets/MaterialPreviewGeometry.h"
#include <SDL3/SDL.h>
#include <imgui_impl_sdl3.h>
#include <lodepng.h>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <limits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unordered_map>

namespace {
using namespace crimson;
void check(bool success,const std::string& text) {if(!success)throw std::runtime_error(text);}
void phase(const char* text){std::fprintf(stderr,"Native check: %s\n",text);std::fflush(stderr);}
void cleanup(const std::filesystem::path& root) {
    const auto resolved=std::filesystem::weakly_canonical(root),temporary=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    if(resolved.parent_path()!=temporary || !resolved.filename().string().starts_with("crimson-native-resources-"))throw std::runtime_error("Native test cleanup target escaped its disposable directory.");
    std::filesystem::remove_all(resolved);
}
struct SdlOwner {SdlOwner(){check(SDL_Init(SDL_INIT_VIDEO|SDL_INIT_EVENTS),SDL_GetError());}~SdlOwner(){SDL_Quit();}};
struct ImGuiOwner {
    explicit ImGuiOwner(SDL_Window* window) {
        ImGui::CreateContext();auto& io=ImGui::GetIO();io.IniFilename=nullptr;
        io.ConfigFlags|=ImGuiConfigFlags_DockingEnable|ImGuiConfigFlags_ViewportsEnable;
        if(!ImGui_ImplSDL3_InitForD3D(window)){ImGui::DestroyContext();throw std::runtime_error("Native ImGui SDL3 backend initialization failed.");}
    }
    ~ImGuiOwner(){ImGui_ImplSDL3_Shutdown();ImGui::DestroyContext();}
};
struct ScreenshotCallback final : bgfx::CallbackI {
    std::unordered_map<std::string,DecodedImage> images;
    std::string error;
    void fatal(const char* file,std::uint16_t line,bgfx::Fatal::Enum code,const char* message) override {
        error=message;std::fprintf(stderr,"Native bgfx error: %s (%s:%u)\n",message,file,line);
        if(code!=bgfx::Fatal::DebugCheck)std::abort();
    }
    void traceVargs(const char*,std::uint16_t,const char*,va_list) override {}
    void profilerBegin(const char*,std::uint32_t,const char*,std::uint16_t) override {}
    void profilerBeginLiteral(const char*,std::uint32_t,const char*,std::uint16_t) override {}
    void profilerEnd() override {}
    std::uint32_t cacheReadSize(std::uint64_t) override {return 0;}
    bool cacheRead(std::uint64_t,void*,std::uint32_t) override {return false;}
    void cacheWrite(std::uint64_t,const void*,std::uint32_t) override {}
    void screenShot(const char* name,std::uint32_t width,std::uint32_t height,std::uint32_t pitch,const void* data,std::uint32_t size,bool flip) override {
        if(!data || !width || !height || std::uint64_t(width)*4>pitch || std::uint64_t(pitch)*(height-1)+std::uint64_t(width)*4>size) {
            error="Invalid native viewport screenshot buffer.";return;
        }
        DecodedImage image{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)};
        const auto* pixels=static_cast<const std::uint8_t*>(data);
        for(std::uint32_t y=0;y<height;++y)for(std::uint32_t x=0;x<width;++x) {
            const auto* source=pixels+std::size_t(flip ? height-1-y : y)*pitch+x*4;
            auto* target=image.rgba.data()+(std::size_t(y)*width+x)*4;
            target[0]=source[2];target[1]=source[1];target[2]=source[0];target[3]=source[3];
        }
        images[name]=std::move(image);
    }
    void captureBegin(std::uint32_t,std::uint32_t,std::uint32_t,bgfx::TextureFormat::Enum,bool) override {}
    void captureEnd() override {}
    void captureFrame(const void*,std::uint32_t) override {}
};
void createImage(const std::filesystem::path& file) {
    constexpr unsigned width=256,height=256;std::vector<std::uint8_t> pixels(width*height*4);
    for(unsigned y=0;y<height;++y)for(unsigned x=0;x<width;++x){const auto offset=(y*width+x)*4;pixels[offset]=180;pixels[offset+1]=70;pixels[offset+2]=40;pixels[offset+3]=x<width/2 ? 0:255;}
    std::vector<std::uint8_t> encoded;check(lodepng::encode(encoded,pixels,width,height)==0,"Could not encode native fixture image.");
    writeFileAtomically(file,{reinterpret_cast<const char*>(encoded.data()),encoded.size()});
}
PreparedEnvironment environment() {
    PreparedEnvironment result;result.settings={2,8,8,16,16,32};result.key=12345;result.status="Native test constant environment.";
    ibl::Image source{4,2,std::vector<Vec3>(8,Vec3{.25f,.25f,.25f})};result.data=ibl::bake(source,result.settings);return result;
}
std::shared_ptr<ModelAsset> quad() {
    auto asset=std::make_shared<ModelAsset>();asset->id="native-model";asset->name="Native resource fixture";
    ModelPrimitive primitive;primitive.uvMask=3;primitive.tangentUv=0;
    const std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
    const std::array<Vec2,4> coordinates{{{0,1},{1,1},{1,0},{0,0}}};
    for(std::size_t i=0;i<4;++i) {
        ModelVertex vertex;vertex.position=positions[i];vertex.normal={0,0,1};vertex.uv0=vertex.uv1=coordinates[i];primitive.vertices.push_back(vertex);math::include(asset->bounds,vertex.position);
    }
    primitive.indices={0,1,2,0,2,3};asset->primitives.push_back(std::move(primitive));asset->draws.push_back({0,{}});
    ModelMaterial material;material.metallic=0;material.roughness=1;asset->materials.push_back(material);return asset;
}
double component(const DecodedImage& image,unsigned x,unsigned y,unsigned channel) {return image.rgba[(static_cast<std::size_t>(y)*image.width+x)*4+channel];}

template<class Tick>
DecodedImage readTarget(bgfx::TextureHandle source,std::uint16_t width,std::uint16_t height,bgfx::ViewId view,
    std::uint32_t& frame,Tick&& tick,std::vector<std::shared_ptr<DecodedImage>>& destinations) {
    check(bgfx::isValid(source),"Particle readback source is unavailable.");
    struct ReadbackTexture {
        bgfx::TextureHandle handle{BGFX_INVALID_HANDLE};
        ~ReadbackTexture(){if(bgfx::isValid(handle))bgfx::destroy(handle);}
    } staging;
    staging.handle=bgfx::createTexture2D(width,height,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_BLIT_DST|BGFX_TEXTURE_READ_BACK);
    check(bgfx::isValid(staging.handle),"Could not allocate particle pixel readback.");
    auto image=std::make_shared<DecodedImage>(DecodedImage{width,height,std::vector<std::uint8_t>(std::size_t(width)*height*4)});
    // This pool is declared before Renderer: even a timeout keeps pending CPU
    // destinations alive through bgfx shutdown rather than freeing a read target.
    destinations.push_back(image);
    bgfx::setViewName(view,"Native particle pixel readback");
    bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    bgfx::blit(view,staging.handle,0,0,source,0,0,width,height);
    const auto ready=bgfx::readTexture(staging.handle,image->rgba.data());
    check(ready!=UINT32_MAX,"Particle readback is unsupported by this adapter.");
    for(unsigned i=0;i<120;++i) {
        tick();
        if(picking::frameReady(frame,ready))return *image;
    }
    throw std::runtime_error("Particle pixel readback did not complete.");
}

template<class Copy>
void exerciseParticles(Renderer& renderer,Copy&& copy) {
    check(renderer.particleStatus().empty(),renderer.particleStatus());
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={0,0,0};settings.environment.enabled=false;
    ViewportAppearance appearance;appearance.backgroundSrgb={.2f,.4f,.6f};renderer.setAppearance(appearance);
    RenderScene scene{settings,{}};scene.sceneMode=true;
    const auto mainImage=[&] {
        renderer.render(scene);
        return copy(renderer.viewportTexture(),std::uint16_t(renderer.width()),std::uint16_t(renderer.height()),16);
    };
    settings.exposure=0;const auto background=mainImage();
    for(const float exposure:{1.f,100.f}) {
        settings.exposure=exposure;
        check(mainImage().rgba==background.rgba,"Empty particle viewport background changed with scene exposure.");
    }
    for(unsigned channel=0;channel<3;++channel)
        check(std::abs(component(background,64,64,channel)-appearance.backgroundSrgb[channel]*255)<=1,"Native particle viewport background has incorrect color encoding.");

    settings.exposure=1;
    std::array<RenderParticle,1> red{{{{0,0,0},1.5f,{8,0,0,1}}}};
    scene.particles=red;
    const auto additive=mainImage();
    check(component(additive,64,64,0)>component(background,64,64,0)+100,"Instanced additive particle was invisible over empty background.");
    for(unsigned channel=1;channel<3;++channel)
        check(std::abs(component(additive,64,64,channel)-component(background,64,64,channel))<=1,"Additive particle changed coverage or an unrelated color channel.");
    check(renderer.particleStats().submitted==1 && renderer.particleStats().invalid==0 && renderer.particleStats().dropped==0,"Starter particle was not submitted through the native instancing path.");
    red[0].color[3]=.01f;const auto faint=mainImage();
    check(component(faint,64,64,0)>component(background,64,64,0)+5,"Low-opacity additive particle was discarded by background coverage handling.");
    settings.exposure=0;
    check(mainImage().rgba==background.rgba,"Zero scene exposure must remove particle radiance while preserving the uncovered editor background.");
    settings.exposure=1;red[0].color[3]=1;

    auto occluder=quad();occluder->id="native-particle-occluder";occluder->materials[0].doubleSided=true;
    check(renderer.prepareModel(*occluder),renderer.modelStatus());
    RenderModelInstance occluderInstance{91,{occluder,{}},nullptr,0,true,false};
    scene.instances=std::span(&occluderInstance,1);scene.particles={};
    const auto opaque=mainImage();
    red[0].position[2]=-.5f;scene.particles=red;const auto behind=mainImage();
    for(unsigned channel=0;channel<3;++channel)
        check(component(behind,64,64,channel)==component(opaque,64,64,channel),"Particle behind opaque geometry ignored scene depth.");
    red[0].position[2]=.5f;const auto inFront=mainImage();
    check(component(inFront,64,64,0)>component(opaque,64,64,0)+100,"Particle in front of opaque geometry did not pass depth testing.");
    scene.instances={};red[0].position={0,0,0};renderer.evictModel(occluder->id);

    std::array<RenderParticle,1> green{{{{0,0,0},1.5f,{0,8,0,1}}}};
    renderer.resizeParticlePreview(128,128);
    const auto originalMain=renderer.viewportTexture();
    check(bgfx::isValid(renderer.particlePreviewTexture()) && renderer.particlePreviewTexture().idx!=originalMain.idx,"Particle preview must own a separate display target.");
    renderer.render(scene);renderer.renderParticlePreview(settings,green);
    const auto independentMain=copy(renderer.viewportTexture(),128,128,16);
    const auto independentPreview=copy(renderer.particlePreviewTexture(),128,128,17);
    check(component(independentMain,64,64,0)>200 && component(independentMain,64,64,1)<150,"Preview rendering overwrote main particle color.");
    check(component(independentPreview,64,64,1)>200 && component(independentPreview,64,64,0)<100,"Preview particle shader/target did not produce independent green pixels.");
    check(renderer.particlePreviewStats().submitted==1,"Independent preview did not submit its particle.");
    const auto originalPreview=renderer.particlePreviewTexture();
    renderer.resize(0,0);
    check(renderer.drawable() && renderer.viewportTexture().idx==originalMain.idx &&
        renderer.particlePreviewTexture().idx==originalPreview.idx,"Disabling the main backbuffer disabled or replaced detached viewport targets.");
    // Swap the colors so stale pixels cannot pass this detached-render check.
    scene.particles=green;renderer.render(scene);renderer.renderParticlePreview(settings,red);
    const auto detachedMain=copy(renderer.viewportTexture(),128,128,16);
    const auto detachedPreview=copy(renderer.particlePreviewTexture(),128,128,17);
    check(component(detachedMain,64,64,1)>200 && component(detachedMain,64,64,0)<100 && renderer.particleStats().submitted==1,
        "Offscreen scene rendering stopped while the main backbuffer was disabled.");
    check(component(detachedPreview,64,64,0)>200 && component(detachedPreview,64,64,1)<150 && renderer.particlePreviewStats().submitted==1,
        "Offscreen particle preview rendering stopped while the main backbuffer was disabled.");
    renderer.resize(320,240);scene.particles=red;
    check(renderer.viewportTexture().idx==originalMain.idx && renderer.particlePreviewTexture().idx==originalPreview.idx,
        "Restoring the main backbuffer replaced detached viewport targets.");
    renderer.resizeParticlePreview(96,64);
    check(renderer.viewportTexture().idx==originalMain.idx,"Resizing preview replaced the main viewport target.");
    renderer.render(scene);renderer.renderParticlePreview(settings,green);
    const auto resizedPreview=copy(renderer.particlePreviewTexture(),96,64,17);
    check(component(resizedPreview,48,32,1)>200,"Resized preview lost particle rendering.");
    const auto previewTarget=renderer.particlePreviewTexture();
    renderer.resizeViewport(160,96);
    check(renderer.particlePreviewTexture().idx==previewTarget.idx,"Resizing the main viewport replaced the preview target.");
    renderer.render(scene);renderer.renderParticlePreview(settings,green);
    const auto resizedMain=copy(renderer.viewportTexture(),160,96,16);
    check(component(resizedMain,80,48,0)>200,"Resized main viewport lost particle rendering.");
    renderer.releaseParticlePreview();
    check(!bgfx::isValid(renderer.particlePreviewTexture()),"Released particle preview retained its display target.");
    renderer.resizeViewport(128,128);
    check(component(mainImage(),64,64,0)>200,"Releasing the preview disturbed main particle rendering.");

    std::array<RenderParticle,2> invalid{red[0],red[0]};invalid[1].position[0]=std::numeric_limits<float>::quiet_NaN();scene.particles=invalid;
    check(component(mainImage(),64,64,0)>200,"An invalid presentation packet prevented valid particles from rendering.");
    check(renderer.particleStats().requested==2 && renderer.particleStats().submitted==1 && renderer.particleStats().invalid==1,"Invalid native particle packet was not diagnosed before upload.");
    std::vector<RenderParticle> overlap(512,red[0]);for(auto& particle:overlap)particle.color[0]=MaxParticleRadiance;scene.particles=overlap;
    check(component(mainImage(),64,64,0)>=254 && renderer.particleStats().invalid==0,"Supported HDR particle overlap overflowed or rejected authored maximum intensity.");
    std::vector<RenderParticle> bounded(MaxParticleDrawCount+1,red[0]);for(auto& particle:bounded)particle.size=.01f;scene.particles=bounded;
    (void)mainImage();const auto stats=renderer.particleStats();
    check(stats.requested==bounded.size() && stats.submitted<=MaxParticleDrawCount && stats.dropped>=1 &&
        stats.requested==stats.submitted+stats.invalid+stats.dropped,"Native particle capacity omission was not bounded and fully counted.");
    renderer.setAppearance({});
}

template<class Tick>
void exercisePlatformWindows(Renderer& renderer,ImGuiBgfx& tools,ScreenshotCallback& callback,std::uint32_t& frame,Tick&& tick,SDL_Window* mainWindow) {
    phase("platform setup");
    auto& io=ImGui::GetIO();auto& platform=ImGui::GetPlatformIO();
    check((io.BackendFlags & ImGuiBackendFlags_PlatformHasViewports) && (io.BackendFlags & ImGuiBackendFlags_RendererHasViewports),"Native multi-viewport backend capability was not advertised.");
    check(platform.Renderer_CreateWindow && platform.Renderer_DestroyWindow && platform.Renderer_SetWindowSize &&
        platform.Renderer_RenderWindow && platform.Renderer_SwapBuffers,"Native multi-viewport renderer callbacks are incomplete.");
    struct KeepHidden {
        ImGuiPlatformIO& platform;
        void (*show)(ImGuiViewport*);
        ~KeepHidden(){platform.Platform_ShowWindow=show;}
    } hidden{platform,platform.Platform_ShowWindow};
    // SDL creates platform windows hidden. Suppress only its later ShowWindow
    // callback so this regression never displays windows or steals user focus.
    platform.Platform_ShowWindow=[](ImGuiViewport*){};
    io.ConfigViewportsNoAutoMerge=true;
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={0,0,0};settings.environment.enabled=false;
    std::array<RenderParticle,1> red{{{{0,0,0},1.5f,{8,0,0,1}}}},green{{{{0,0,0},1.5f,{0,8,0,1}}}};
    RenderScene scene{settings,{}};scene.sceneMode=true;scene.particles=red;
    renderer.resizeParticlePreview(128,128);
    const auto originalMain=renderer.viewportTexture(),originalPreview=renderer.particlePreviewTexture();
    ImGuiID collapseViewportId{};
    void* collapseNativeWindow{};
    std::uintptr_t collapseChainIdentity{};
    const auto collapseDraw=[&](float height,bool collapsed) {
        SDL_Event event;while(SDL_PollEvent(&event))ImGui_ImplSDL3_ProcessEvent(&event);
        tools.newFrame(frame);ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
        const auto* mainViewport=ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({mainViewport->Pos.x+mainViewport->Size.x+80,mainViewport->Pos.y+60},ImGuiCond_Always);
        ImGui::SetNextWindowSize({192,height},ImGuiCond_Always);
        ImGui::SetNextWindowCollapsed(collapsed,ImGuiCond_Always);
        const bool visible=ImGui::Begin("Hidden native collapse regression",nullptr,ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
        auto* viewport=ImGui::GetWindowViewport();
        check(viewport->ID!=mainViewport->ID && (!collapseViewportId || viewport->ID==collapseViewportId),"Collapsing the native window replaced its ImGui viewport.");
        collapseViewportId=viewport->ID;
        check(ImGui::IsWindowCollapsed()==collapsed,"Native decorated regression did not apply its collapse state.");
        if(collapsed)check(viewport->Size.y>0 && viewport->Size.y<=ImGui::GetFrameHeight()+1,"Collapsed native window did not use title-only height.");
        if(visible) {
            ImGui::TextUnformatted("Decorated native collapse/expand");
            ImGui::Image(ImGuiBgfx::textureId(renderer.particlePreviewTexture()),{64,48});
        }
        ImGui::End();ImGui::Render();
        renderer.render(scene);if(visible)renderer.renderParticlePreview(settings,green);
        bgfx::setViewClear(ToolsView,BGFX_CLEAR_COLOR,0x202020ff);tools.render(ImGui::GetDrawData());
        ImGui::UpdatePlatformWindows();ImGui::RenderPlatformWindowsDefault();
        viewport=ImGui::FindViewportByID(collapseViewportId);
        if(const auto identity=tools.platformViewportSwapChainIdentity(collapseViewportId)) {
            check(!collapseChainIdentity || identity==collapseChainIdentity,"Collapse/expand replaced the persistent DXGI swap chain.");
            collapseChainIdentity=identity;
        }
        if(viewport && viewport->PlatformWindowCreated) {
            check(!collapseNativeWindow || viewport->PlatformHandleRaw==collapseNativeWindow,"Collapse/expand replaced the native HWND.");
            collapseNativeWindow=viewport->PlatformHandleRaw;
            const auto id=static_cast<SDL_WindowID>(reinterpret_cast<std::uintptr_t>(viewport->PlatformHandle));
            auto* secondary=SDL_GetWindowFromID(id);int width{},actualHeight{};
            check(secondary && (SDL_GetWindowFlags(secondary)&SDL_WINDOW_HIDDEN) && SDL_GetWindowSize(secondary,&width,&actualHeight),"Collapse regression displayed or lost its native window.");
            check(width==192 && actualHeight==static_cast<int>(viewport->Size.y),"Native collapse/expand size did not match its ImGui viewport.");
        }
        // Exactly the application's ordinary owner frame. No screenshot,
        // texture readback, or additional frame can flush away this lifecycle bug.
        tick();check(callback.error.empty(),callback.error);
    };
    phase("decorated collapse warmup");
    collapseDraw(128,false);collapseDraw(128,false);
    check(collapseNativeWindow!=nullptr,"Decorated collapse regression did not create its native window.");
    for(unsigned cycle=0;cycle<12;++cycle) {
        std::fprintf(stderr,"Native check: collapse cycle %u\n",cycle);std::fflush(stderr);
        const float height=128.f+static_cast<float>(cycle%3)*17.f;
        collapseDraw(height,false);collapseDraw(height,false);
        collapseDraw(height,true);collapseDraw(height,true);
    }
    collapseDraw(128,false);collapseDraw(128,false);
    check(bgfx::isValid(tools.platformViewportFramebuffer(collapseViewportId)),"Expanded native window did not regain its framebuffer.");
    check(collapseChainIdentity!=0,"Collapse/expand regression never created its persistent DXGI swap chain.");
    // Exercise close/reopen in one owner frame. The renderer's retirement
    // fence must outlive its deleted viewport resource even if Windows reuses
    // the native handle. No readback or screenshot separates these callbacks.
    auto* reopened=ImGui::FindViewportByID(collapseViewportId);
    phase("same-frame close/reopen");
    tools.newFrame(frame);
    const auto closedWindowId=static_cast<SDL_WindowID>(reinterpret_cast<std::uintptr_t>(reopened->PlatformHandle));
    platform.Renderer_DestroyWindow(reopened);platform.Platform_DestroyWindow(reopened);
    platform.Platform_CreateWindow(reopened);platform.Renderer_CreateWindow(reopened);
    check(!SDL_GetWindowFromID(closedWindowId) && reopened->PlatformHandleRaw && reopened->RendererUserData,
        "Native close/reopen did not replace platform and renderer ownership.");
    check(!bgfx::isValid(tools.platformViewportFramebuffer(collapseViewportId)),"Native close/reopen bypassed imported-texture preparation.");
    platform.Renderer_RenderWindow(reopened,nullptr);tick();check(callback.error.empty(),callback.error);
    tools.newFrame(frame);platform.Renderer_RenderWindow(reopened,nullptr);
    check(bgfx::isValid(tools.platformViewportFramebuffer(collapseViewportId)),"Native close/reopen did not recover after the retirement frame.");
    tick();check(callback.error.empty(),callback.error);
    ImGuiID viewportId{};
    const auto draw=[&](ImVec2 size) {
        SDL_Event event;while(SDL_PollEvent(&event))ImGui_ImplSDL3_ProcessEvent(&event);
        tools.newFrame(frame);ImGui_ImplSDL3_NewFrame();ImGui::NewFrame();
        const auto* mainViewport=ImGui::GetMainViewport();
        ImGui::SetNextWindowPos({mainViewport->Pos.x+mainViewport->Size.x+80,mainViewport->Pos.y+60},ImGuiCond_Always);
        ImGui::SetNextWindowSize(size,ImGuiCond_Always);
        ImGui::Begin("Hidden native platform regression",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoDocking|ImGuiWindowFlags_NoSavedSettings);
        auto* viewport=ImGui::GetWindowViewport();viewportId=viewport->ID;
        check(viewportId!=mainViewport->ID,"Native ImGui window did not acquire a secondary viewport.");
        const auto pos=ImGui::GetWindowPos();auto* list=ImGui::GetWindowDrawList();
        const auto width=ImGui::GetWindowSize().x,height=ImGui::GetWindowSize().y;
        list->PushClipRect({pos.x+16,pos.y+8},{pos.x+width*.5f-8,pos.y+height-8},true);
        list->AddCallback(platform.DrawCallback_SetSamplerNearest,nullptr);
        list->AddImage(ImGuiBgfx::textureId(renderer.particlePreviewTexture()),{pos.x+8,pos.y+8},{pos.x+width*.5f,pos.y+height-8});
        list->PopClipRect();
        list->AddCallback(platform.DrawCallback_ResetRenderState,nullptr);
        list->AddRectFilled({pos.x+width*.5f+8,pos.y+8},{pos.x+width-8,pos.y+height-8},IM_COL32(0,0,255,255));
        ImGui::End();ImGui::Render();
        renderer.render(scene);renderer.renderParticlePreview(settings,green);
        bgfx::setViewClear(ToolsView,BGFX_CLEAR_COLOR,0x202020ff);tools.render(ImGui::GetDrawData());
        ImGui::UpdatePlatformWindows();ImGui::RenderPlatformWindowsDefault();
        viewport=ImGui::FindViewportByID(viewportId);
        check(viewport && viewport->DrawData && viewport->DrawData->DisplayPos.x!=0,"Native secondary draw data lost its desktop origin.");
    };
    const auto capture=[&](const char* name) {
        const auto framebuffer=tools.platformViewportFramebuffer(viewportId);
        check(bgfx::isValid(framebuffer),"Native platform framebuffer is unavailable for capture.");
        check(tools.requestPlatformViewportCapture(viewportId,[&callback,key=std::string(name)](const DecodedImage& image){callback.images[key]=image;}),
            "Native platform capture request was rejected.");
        for(unsigned i=0;i<30;++i) {
            tick();check(callback.error.empty(),callback.error);
            if(const auto found=callback.images.find(name);found!=callback.images.end())return found->second;
        }
        throw std::runtime_error("Native secondary viewport screenshot did not complete.");
    };
    const auto checkPixels=[&](const DecodedImage& image,unsigned width,unsigned height) {
        check(image.width==width && image.height==height,"Native platform swap chain retained its old size.");
        check(component(image,width/4,height/2,1)>200 && component(image,width/4,height/2,0)<100,"Native secondary viewport sampled the main scene or lost its preview image.");
        check(component(image,width*3/4,height/2,2)>250 && component(image,width*3/4,height/2,0)<5,"Native secondary draw origin, clip, or reset callback targeted the wrong framebuffer.");
        check(component(image,12,height/2,1)<80,"Native secondary framebuffer clipping ignored DisplayPos.");
    };
    phase("native pixel warmup/capture");
    draw({128,96});tick();draw({128,96});tick();draw({128,96});
    checkPixels(capture("secondary-initial"),128,96);
    const auto originalChain=tools.platformViewportSwapChainIdentity(viewportId);
    check(originalChain!=0,"Native pixel regression has no persistent DXGI swap chain.");
    const auto* viewport=ImGui::FindViewportByID(viewportId);
    const auto secondaryWindowId=static_cast<SDL_WindowID>(reinterpret_cast<std::uintptr_t>(viewport->PlatformHandle));
    check(SDL_GetWindowFromID(secondaryWindowId) && (SDL_GetWindowFlags(SDL_GetWindowFromID(secondaryWindowId)) & SDL_WINDOW_HIDDEN),"Native regression displayed its secondary window.");
    phase("native pixel resize/capture");
    draw({192,128});
    check(tools.platformViewportSwapChainIdentity(viewportId)==originalChain,"Preparing a platform resize replaced its DXGI swap chain.");
    tick();draw({192,128});checkPixels(capture("secondary-resized"),192,128);
    check(tools.platformViewportSwapChainIdentity(viewportId)==originalChain,"Native resize replaced its persistent DXGI swap chain.");
    check(renderer.viewportTexture().idx==originalMain.idx && renderer.particlePreviewTexture().idx==originalPreview.idx,"Native platform resize replaced scene/preview targets.");
    phase("platform shutdown drain");tools.shutdown();
    check(!bgfx::isValid(tools.platformViewportFramebuffer(viewportId)) && !platform.Renderer_CreateWindow && !platform.Renderer_DestroyWindow &&
        !(io.BackendFlags & ImGuiBackendFlags_RendererHasViewports),"Native ImGui renderer shutdown retained platform resources or callbacks.");
    check(!SDL_GetWindowFromID(secondaryWindowId) && SDL_GetWindowFromID(SDL_GetWindowID(mainWindow))==mainWindow,"Native platform shutdown leaked its window or destroyed the main SDL owner.");
    renderer.releaseParticlePreview();
    for(unsigned i=0;i<8 && tools.hasPendingRetirements();++i)tick();
    check(!tools.hasPendingRetirements(),"Native imported textures survived the bounded shutdown drain.");
    check(callback.error.empty(),callback.error);
}

void exercise(const std::filesystem::path& root,const ShaderCompileConfig& shaderConfig,SDL_Window* window) {
    std::vector<std::shared_ptr<DecodedImage>> readbackDestinations;
    ScreenshotCallback screenshots;
    ResourceManager resources;Renderer renderer(window,&screenshots);ImGuiOwner context(window);
    ImGuiBgfx tools([&screenshots](const std::string& message){screenshots.error=message;});
    phase("resources initialized");
    renderer.resize(320,240);renderer.resizeViewport(128,128);
    const auto lighting=environment();check(renderer.publishEnvironment(lighting),renderer.environmentStatus());check(renderer.publishThumbnailEnvironment(lighting),renderer.thumbnailStatus());
    TextureStreamer textures(resources,root);const auto source=root/"source.png";createImage(source);
    std::uint32_t frame{};
    const auto tick=[&] {resources.pump();textures.advance(8192,2);renderer.advanceStreaming(8192,2);frame=bgfx::frame();tools.postFrame(frame);std::this_thread::sleep_for(std::chrono::milliseconds{1});};
    const auto copy=[&](bgfx::TextureHandle texture,std::uint16_t width,std::uint16_t height,bgfx::ViewId view) {
        return readTarget(texture,width,height,view,frame,tick,readbackDestinations);
    };
    PreparedTexture color,normal;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{20};
    do {
        textures.beginFrame();color=textures.request("native-image",source,streaming::TextureRole::Color,false,256,ResourcePriority::Selected);
        normal=textures.request(NeutralNormalTextureId,{},streaming::TextureRole::Normal,false,1,ResourcePriority::Selected);
        tick();
        if(!color.diagnostic.empty())throw std::runtime_error(color.diagnostic);if(!normal.diagnostic.empty())throw std::runtime_error(normal.diagnostic);
    }while((!color.complete || !normal.complete) && std::chrono::steady_clock::now()<deadline);
    check(color.complete && normal.complete,"Native texture streaming did not publish complete resources.");
    auto asset=quad();auto descriptor=streaming::buildModelCache(*asset,root);renderer.configureModelStream(asset->id,1,descriptor);
    check(renderer.queueModelLod(asset->id,1,streaming::loadModelLod(descriptor.primitives[0].levels[0])),"Native mesh streaming queue failed.");
    for(unsigned i=0;i<100 && !renderer.modelStreamReady(asset->id,1);++i)tick();
    check(renderer.modelStreamReady(asset->id,1),"Native mesh buffers were not published.");
    SceneSettings settings;settings.camera.position={0,0,3};settings.camera.target={0,0,0};settings.environment.enabled=false;
    DirectionalLight light;light.direction={0,0,-1};light.color={1,1,1};light.intensity=3;
    std::array<DirectionalLight,1> lights{light};std::vector<ModelMaterialTextures> bindings(1);
    bindings[0].handles[0]=color.handle;bindings[0].leases[0]=color.lease;bindings[0].flags[0]=BGFX_SAMPLER_MIN_POINT|BGFX_SAMPLER_MAG_POINT;
    std::vector<ModelMaterial> materials=asset->materials;
    RenderModelInstance instance{77,{asset,{}},&bindings,1,true,true,&materials};
    RenderScene scene{settings,{},nullptr,nullptr,std::span(&instance,1),lights,{},true};
    scene.pinned=std::make_shared<std::vector<ModelMaterialTextures>>(bindings);
    const auto capture=[&](const char* name) {
        check(renderer.renderThumbnail(name,1,scene,128,scene.pinned),"Native thumbnail request failed: "+renderer.thumbnailStatus());
        const auto limit=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        do {tick();if(auto result=renderer.pollThumbnail(frame))return result->image;}while(std::chrono::steady_clock::now()<limit);
        throw std::runtime_error("Native offscreen readback did not complete.");
    };
    const auto baseline=capture("opaque");check(component(baseline,64,64,0)>30,"Native model thumbnail did not contain visible lit geometry.");
    bindings[0].handles[2]=normal.handle;bindings[0].leases[2]=normal.lease;bindings[0].flags[2]=0;materials[0].normalTexture.image=AssetRef<DecodedImage>{NeutralNormalTextureId};
    scene.pinned=std::make_shared<std::vector<ModelMaterialTextures>>(bindings);
    const auto flat=capture("flat-normal");
    for(unsigned channel=0;channel<3;++channel)check(std::abs(component(flat,64,64,channel)-component(baseline,64,64,channel))<=1,"A neutral normal map changed native model lighting.");
    instance.model.transform=math::scaling({-1.1f,.8f,1.2f});
    const auto mirrored=capture("mirrored");
    for(unsigned channel=0;channel<3;++channel)check(std::abs(component(mirrored,64,64,channel)-component(flat,64,64,channel))<=2,"Mirrored/nonuniform model transforms changed neutral front-face lighting.");
    instance.model.transform={};materials[0].alphaMask=true;
    const auto mask=capture("cutout");check(component(mask,32,64,0)<30 && component(mask,96,64,0)>30,"Native cutout material did not discard its transparent region.");
    const auto pick=[&](unsigned x,std::uint64_t serial) {
        check(renderer.requestPick({1,1,serial,x,64,renderer.streamingSignature()}),renderer.pickingStatus());renderer.render(scene);
        for(unsigned i=0;i<50;++i){tick();if(auto result=renderer.pollPick(frame))return result->entityId;}
        throw std::runtime_error("Native entity picking readback did not complete.");
    };
    check(pick(32,1)==0 && pick(96,2)==77,"Native ID picking must match material cutout/depth coverage.");
    const auto sphere=materialPreviewGeometry(); const auto priorBytes=renderer.modelResidentBytes();
    const auto priorSignature=renderer.streamingSignature();
    renderer.configureModelStream(sphere->id,1,*sphere->stream);
    check(renderer.queueModelLod(sphere->id,1,materialPreviewGeometryLevel()),"Material preview sphere GPU queue failed.");
    for(unsigned i=0;i<100 && !renderer.modelStreamReady(sphere->id,1);++i)tick();
    check(renderer.modelStreamReady(sphere->id,1) && renderer.modelResidentBytes()>priorBytes,"Shared material sphere must publish through tracked GPU resources.");
    check(renderer.streamingSignature()==priorSignature,"Offscreen material geometry must not invalidate main-view picking.");
    const auto sphereBytes=renderer.modelResidentBytes();
    check(renderer.queueModelLod(sphere->id,1,materialPreviewGeometryLevel()) && renderer.modelResidentBytes()==sphereBytes,"Material thumbnails must share a single GPU sphere allocation.");
    const std::array<DirectionalLight,2> previewLights{{DirectionalLight{{-1,-1,-1},{1,1,1},1,true},DirectionalLight{{0,1,0},{.7f,.7f,.7f},1,true}}};
    scene.directionalLights=previewLights; settings.environment.enabled=true;
    settings.camera.position={0,0,3};settings.camera.target={0,0,0};settings.camera.up={0,1,0};
    settings.camera.fov=45;settings.camera.nearPlane=.1f;settings.camera.farPlane=10;
    instance.model={sphere,Mat4{}};instance.pickable=false;materials=sphere->materials;bindings={ModelMaterialTextures{}};
    materials[0].baseColor={.6f,.05f,.02f,1};
    scene.pinned=std::make_shared<std::vector<ModelMaterialTextures>>(bindings);
    const auto materialRed=capture("material-red");
    check(component(materialRed,64,64,0)>component(materialRed,64,64,2)+30,"Material sphere preview must display the assigned linear surface factors.");
    check(component(materialRed,0,0,0)<30 && component(materialRed,64,64,0)>30,"Material preview must frame a visible sphere on its fixed background.");
    bindings[0].handles[2]=normal.handle;bindings[0].leases[2]=normal.lease;bindings[0].flags[2]=0;
    materials[0].normalTexture.image=AssetRef<DecodedImage>{NeutralNormalTextureId};materials[0].normalTexture.texCoord=1;
    scene.pinned=std::make_shared<std::vector<ModelMaterialTextures>>(bindings);
    const auto materialNormal=capture("material-uv1-normal");
    for(unsigned channel=0;channel<3;++channel)check(std::abs(component(materialNormal,64,64,channel)-component(materialRed,64,64,channel))<=1,"Material sphere UV1 normal maps must use a correct tangent basis.");
    materials[0].baseColor={.02f,.6f,.05f,1};
    const auto materialGreen=capture("material-green");
    check(component(materialGreen,64,64,1)>component(materialGreen,64,64,0)+30 && materialGreen.rgba!=materialRed.rgba,"Material previews must use individual surfaces without reallocating sphere geometry.");
    check(renderer.modelResidentBytes()==sphereBytes,"Multiple material captures must retain shared geometry accounting.");
    phase("particle readbacks");exerciseParticles(renderer,copy);
    SceneSettings particleSettings;particleSettings.camera.position={0,0,3};particleSettings.camera.target={0,0,0};particleSettings.environment.enabled=false;
    std::array<RenderParticle,1> reloadParticle{{{{0,0,0},1.5f,{8,0,0,1}}}};
    RenderScene particleScene{particleSettings,{}};particleScene.sceneMode=true;particleScene.particles=reloadParticle;
    const auto particleCapture=[&] {
        renderer.render(particleScene);return copy(renderer.viewportTexture(),128,128,16);
    };
    const auto beforeReloadParticles=particleCapture();
    phase("shader reload");ShaderReloadSession shaders(resources,renderer,tools,shaderConfig);
    const auto shaderDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
    do {tick();shaders.pump();}while(shaders.pending() && std::chrono::steady_clock::now()<shaderDeadline);
    check(shaders.revision()==1,"Valid native shader candidates did not publish: "+shaders.status());
    const auto healthy=renderer.shaderSignature();const auto healthyPixels=capture("valid-reload");
    const auto healthyParticlePixels=particleCapture();
    check(healthyParticlePixels.rgba==beforeReloadParticles.rgba,"Valid hot reload changed native particle output.");
    const auto bad=shaderConfig.sourceDirectory/"fs_model.sc";std::ifstream input(bad);std::string text(std::istreambuf_iterator<char>{input},{});input.close();
    text+="\nTHIS_IS_NOT_VALID_SHADER_CODE\n";writeFileAtomically(bad,text);const std::array<std::filesystem::path,1> changed{bad};shaders.changed(changed);
    const auto failureDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{30};
    do {tick();shaders.pump();}while(shaders.pending() && std::chrono::steady_clock::now()<failureDeadline);
    check(!shaders.pending() && shaders.revision()==1 && renderer.shaderSignature()==healthy,"Failed shader reload changed published native programs.");
    const auto retained=capture("failed-reload-retained");check(retained.rgba==healthyPixels.rgba,"Failed shader reload changed the rendered native image.");
    check(particleCapture().rgba==healthyParticlePixels.rgba,"Failed hot reload changed the retained native particle program.");
    check(renderer.setThumbnail("cached",1,retained) && bgfx::isValid(renderer.thumbnailTexture("cached",1)),"Native thumbnail GPU cache publication failed.");
    renderer.evictThumbnail("cached");renderer.evictModel(asset->id);renderer.evictModel(sphere->id);resources.stop();frame=renderer.drainPending(frame);
    phase("persistent native surfaces");exercisePlatformWindows(renderer,tools,screenshots,frame,tick,window);
}
}
int main(int argc,char** argv) {
    if(argc!=4){std::cerr<<"Pass shaderc, source and include directories.\n";return 1;}
    const auto root=std::filesystem::temp_directory_path()/("crimson-native-resources-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        std::filesystem::create_directories(root);
        ShaderCompileConfig config{std::filesystem::path(argv[1]),root/"shaders",root/"includes",root/"cache"};
        std::filesystem::copy(std::filesystem::path(argv[2]),config.sourceDirectory,std::filesystem::copy_options::recursive);
        std::filesystem::copy(std::filesystem::path(argv[3]),config.includeDirectory,std::filesystem::copy_options::recursive);
        SdlOwner sdl;std::unique_ptr<SDL_Window,decltype(&SDL_DestroyWindow)> window(SDL_CreateWindow("Crimson native resource checks",320,240,SDL_WINDOW_HIDDEN),SDL_DestroyWindow);
        check(!!window,SDL_GetError());exercise(root,config,window.get());window.reset();cleanup(root);
        std::cout<<"Hidden native DX11 streaming/shader/normal/cutout/material-thumbnail/particle/platform-window checks passed.\n";return 0;
    }catch(const std::exception& exception){std::error_code ignored;if(std::filesystem::exists(root,ignored))try{cleanup(root);}catch(...){}std::cerr<<exception.what()<<'\n';return 1;}
}
