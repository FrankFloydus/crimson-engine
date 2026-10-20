#include "resources/TextureStreamer.h"
#include <bgfx/platform.h>
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
void run() {
    ResourceConfig config;config.cpuRetentionBytes=0;config.idleGrace=std::chrono::milliseconds{0};ResourceManager manager(config);
    TextureStreamer streamer(manager,{});std::atomic_uint generations{};
    const auto generator=[&](std::stop_token token){++generations;FloatImage image{256,128,{}};image.rgba.resize(256*128*4);for(std::size_t i=0;i<image.rgba.size();i+=4){if(token.stop_requested())throw std::runtime_error("Canceled.");image.rgba[i]=.5009765625f;image.rgba[i+1]=2;image.rgba[i+2]=-.5f;image.rgba[i+3]=1;}return image;};
    const auto frame=[&](std::size_t budget) {
        manager.pump();streamer.beginFrame();auto result=streamer.requestGeneratedFloat("float-immutable-v1",generator,256,128,true,256,ResourcePriority::Selected);
        streamer.advance(budget,10);bgfx::frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});
        if(!result.diagnostic.empty())throw std::runtime_error(result.diagnostic);return result;
    };
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{15};PreparedTexture first,current;
    do{current=frame(8192);if(bgfx::isValid(current.handle)&&!bgfx::isValid(first.handle))first=current;}while(!current.complete&&std::chrono::steady_clock::now()<deadline);
    check(current.complete&&current.sourceReady&&current.width==256&&current.height==128,"Float stream failed to publish requested rectangular residency.");
    check(first.width<=64&&first.sourceWidth==256&&first.sourceHeight==128,"Float streaming did not publish coarse-first metadata.");
    check(generations.load()==1,"One immutable float generation ran more than once during refinement.");
    check(streamer.residentBytes()>=256*128*8,"Float GPU accounting used four bytes per pixel.");
    const auto loads=manager.stats().loads;const auto signature=current.signature;const auto handle=current.handle;
    const auto started=std::chrono::steady_clock::now();
    for(unsigned i=0;i<120;++i) {
        manager.pump();streamer.beginFrame();auto cached=streamer.cachedGeneratedFloat("float-immutable-v1",256,128,true,256,ResourcePriority::Selected);
        check(cached&&cached->complete&&cached->handle.idx==handle.idx&&cached->signature==signature,"Warm float cache lost its immutable GPU generation.");
        streamer.advance(8192,10);bgfx::frame();
    }
    check(manager.stats().loads==loads&&generations.load()==1,"Warm float cache rebaked or reloaded CPU image data.");
    std::cout<<"Warm 120 requests: "<<std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()<<" ms orchestration wall time; generation count="<<generations.load()<<", resource loads="<<loads<<".\n";
    first={};current={};streamer.beginFrame();streamer.advance(8192,10);streamer.trim(0);manager.pump();manager.trim();
    check(streamer.residentBytes()==0,"Unleased float GPU generations were not retired.");
}
void npotWarmCache(bool mipmapped) {
    ResourceConfig config;config.cpuRetentionBytes=0;config.idleGrace=std::chrono::milliseconds{0};ResourceManager manager(config);
    TextureStreamer streamer(manager,{});std::atomic_uint generations{};
    const auto generator=[&](std::stop_token token) {
        ++generations;FloatImage image{1000,500,{}};image.rgba.resize(1000*500*4);
        for(std::size_t i=0;i<image.rgba.size();i+=4) {
            if((i&65535)==0&&token.stop_requested())throw std::runtime_error("Canceled.");
            image.rgba[i]=.5f;image.rgba[i+1]=2;image.rgba[i+2]=-.5f;image.rgba[i+3]=1;
        }
        return image;
    };
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{15};PreparedTexture current;
    do {
        manager.pump();streamer.beginFrame();
        current=streamer.requestGeneratedFloat("float-npot-v1",generator,1000,500,mipmapped,512,ResourcePriority::Selected);
        if(!current.diagnostic.empty())throw std::runtime_error(current.diagnostic);
        streamer.advance(1024*1024,10);bgfx::frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});
    } while(!current.complete&&std::chrono::steady_clock::now()<deadline);
    check(current.complete&&current.sourceReady&&current.width==500&&current.height==250,
        "NPOT float stream did not settle at the floor-halved mip required by a 512-pixel demand.");
    const auto handle=current.handle;const auto signature=current.signature;
    // Drop CPU mip data during an actual demand gap, keeping the GPU generation.
    streamer.beginFrame();streamer.advance(8192,10);manager.pump();manager.trim();
    check(manager.stats().cpuBytes==0&&manager.stats().liveInterests==0,"NPOT demand gap retained the generated CPU descriptor.");
    const auto loads=manager.stats().loads;
    for(unsigned i=0;i<8;++i) {
        manager.pump();streamer.beginFrame();
        auto cached=streamer.cachedGeneratedFloat("float-npot-v1",1000,500,mipmapped,512,ResourcePriority::Selected);
        check(cached&&cached->complete&&cached->sourceReady&&cached->handle.idx==handle.idx&&cached->signature==signature,
            "NPOT warm lookup treated the correct 500-pixel mip as incomplete after descriptor release.");
        current=streamer.requestGeneratedFloat("float-npot-v1",generator,1000,500,mipmapped,512,ResourcePriority::Selected);
        check(current.complete&&current.handle.idx==handle.idx&&current.signature==signature,
            "NPOT generated request failed to retain the complete warm GPU generation.");
        streamer.advance(8192,10);bgfx::frame();
        check(manager.stats().liveInterests==0,"NPOT warm request unnecessarily reacquired a CPU descriptor.");
    }
    check(generations.load()==1&&manager.stats().loads==loads,"NPOT warm cache regenerated or reloaded an immutable float image.");
}
}
int main(){bool initialized=false;try{bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;check(bgfx::init(init),"Noop initialization failed.");initialized=true;run();npotWarmCache(true);npotWarmCache(false);for(unsigned i=0;i<3;++i)bgfx::frame();bgfx::shutdown();std::cout<<"Float GPU streaming cache/lifetime checks passed.\n";return 0;}catch(const std::exception& error){if(initialized)bgfx::shutdown();std::cerr<<error.what()<<'\n';return 1;}}
