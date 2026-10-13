#include "crimson-renderer/ModelGpuCache.h"
#include "resources/TextureStreamer.h"
#include "assets/AtomicFile.h"
#include <bgfx/platform.h>
#include <lodepng.h>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
using namespace crimson;
void check(bool success,const char* message) { if (!success) throw std::runtime_error(message); }
void cleanup(const std::filesystem::path& root) {
    const auto resolved=std::filesystem::weakly_canonical(root),temporary=std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
    check(resolved.parent_path()==temporary && resolved.filename().string().starts_with("crimson-gpu-streaming-"),"GPU test cleanup escaped its disposable directory.");
    std::filesystem::remove_all(resolved);
}
void writePng(const std::filesystem::path& file,unsigned width,unsigned height,unsigned red) {
    std::vector<std::uint8_t> pixels(static_cast<std::size_t>(width)*height*4);
    for (std::size_t i=0;i<pixels.size();i+=4) {pixels[i]=static_cast<std::uint8_t>(red);pixels[i+1]=64;pixels[i+2]=192;pixels[i+3]=255;}
    std::vector<std::uint8_t> encoded;
    check(lodepng::encode(encoded,pixels,width,height)==0,"Could not create isolated streaming fixture.");
    writeFileAtomically(file,{reinterpret_cast<const char*>(encoded.data()),encoded.size()});
}
void textures(const std::filesystem::path& root) {
    ResourceConfig config;config.cpuRetentionBytes=0;config.idleGrace=std::chrono::milliseconds{0};
    ResourceManager manager(config);
    const auto file=root/"source.png";writePng(file,256,128,70);
    {
        TextureStreamer streamer(manager,root);
        const auto frame=[&](unsigned edge) {
            manager.pump();streamer.beginFrame();
            auto result=streamer.request("texture-fixture",file,streaming::TextureRole::Color,true,edge);
            streamer.advance(8192,2);bgfx::frame();
            std::this_thread::sleep_for(std::chrono::milliseconds{1});return result;
        };
        const auto until=[&](auto predicate,unsigned edge) {
            const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};PreparedTexture result;
            do {result=frame(edge);if(!result.diagnostic.empty()) throw std::runtime_error(result.diagnostic);if(predicate(result))return result;} while(std::chrono::steady_clock::now()<deadline);
            throw std::runtime_error("Streaming texture failed to reach requested residency.");
        };
        auto coarse=until([](const auto& result){return bgfx::isValid(result.handle);},256);
        check(coarse.width<=64 && coarse.sourceWidth==256 && coarse.sourceHeight==128,"Cold texture must publish a coarse allocation with original metadata.");
        auto detailed=until([](const auto& result){return result.width==256;},256);
        check(detailed.handle.idx!=coarse.handle.idx && streamer.residentBytes()>detailed.width*detailed.height*4,"Replacement allocations and pinned retired versions must be counted.");
        const auto old=detailed.handle;const auto fingerprint=detailed.sourceFingerprint;
        writePng(file,128,64,210);manager.invalidatePath(file);streamer.reload("texture-fixture");
        const auto preserved=streamer.request("texture-fixture",file,streaming::TextureRole::Color,true,128);
        check(preserved.handle.idx==old.idx,"Hot reload must retain the current texture while workers prepare replacement data.");
        auto reloaded=until([&](const auto& result){return result.width==128 && result.sourceFingerprint!=fingerprint;},128);
        check(reloaded.sourceWidth==128 && detailed.handle.idx==old.idx,"Reload must publish new metadata and preserve old reader leases.");
        coarse={};detailed={};reloaded={};
        streamer.beginFrame();streamer.trim(0);
        // 'preserved' is still a lease: trimming must keep its allocation accounted.
        check(streamer.residentBytes()>0,"A leased retired texture must survive cache eviction.");
    }
    manager.pump();manager.trim();
    check(manager.stats().cpuBytes==0,"Completed mip jobs must not retain captured source pixels or CPU mip interests.");
    for(int i=0;i<3;++i) bgfx::frame();
}
void uploadBudget(const std::filesystem::path& root) {
    ResourceManager manager;TextureStreamer textures(manager,root);
    const auto file=root/"budget.png";writePng(file,16,8,160);
    const auto request=[&] {return textures.request("budget-texture",file,streaming::TextureRole::Data,false,16);};
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    do {
        manager.pump();textures.beginFrame();const auto prepared=request();
        check(!bgfx::isValid(prepared.handle),"A row larger than the byte budget must remain unpublished.");
        textures.advance(63,2);bgfx::frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(!textures.residentBytes() && std::chrono::steady_clock::now()<deadline);
    check(textures.residentBytes()>0,"Byte-budget texture fixture did not finish CPU preparation.");
    for(unsigned i=0;i<5;++i) {
        manager.pump();textures.beginFrame();(void)request();textures.advance(63,2);bgfx::frame();
        check(!bgfx::isValid(request().handle),"Texture uploads must defer a complete row instead of exceeding the remaining byte cap.");
    }
    for(unsigned row=0;row<8;++row) {
        textures.beginFrame();(void)request();textures.advance(64,2);bgfx::frame();
        const auto prepared=request();
        check(bgfx::isValid(prepared.handle)==(row==7),"A 64-byte budget must upload at most one 64-byte texture row per advance.");
    }
    manager.stop();
}
void models(const std::filesystem::path& root) {
    ModelAsset asset;asset.id="model-fixture";ModelPrimitive primitive;primitive.uvMask=1;
    constexpr unsigned side=16;
    for(unsigned y=0;y<=side;++y)for(unsigned x=0;x<=side;++x) {
        ModelVertex vertex;vertex.position={static_cast<float>(x),0,static_cast<float>(y)};vertex.uv0={static_cast<float>(x)/side,static_cast<float>(y)/side};
        primitive.vertices.push_back(vertex);math::include(asset.bounds,vertex.position);
    }
    for(unsigned y=0;y<side;++y)for(unsigned x=0;x<side;++x) {const auto a=y*(side+1)+x,b=a+1,c=a+side+1,d=c+1;primitive.indices.insert(primitive.indices.end(),{a,c,b,b,c,d});}
    asset.primitives.push_back(std::move(primitive));
    const auto descriptor=streaming::buildModelCache(asset,root);
    const auto last=static_cast<std::uint32_t>(descriptor.primitives[0].levels.size()-1);
    auto coarse=streaming::loadModelLod(descriptor.primitives[0].levels[last],0,last);
    ModelGpuCache cache;cache.configure(asset.id,1,descriptor);check(cache.queue(asset.id,1,coarse),"Coarse mesh queue failed.");
    check(!cache.ready(asset.id,1),"Uninitialized model buffers must not become visible before upload completes.");
    for(unsigned i=0;i<5;++i) {cache.advance(sizeof(ModelVertex)-1,2);bgfx::frame();}
    check(!cache.ready(asset.id,1),"Mesh upload must defer a vertex that exceeds the remaining byte budget.");
    for(unsigned i=0;i<1000 && !cache.ready(asset.id,1);++i) {cache.advance(1024,2);bgfx::frame();}
    check(cache.ready(asset.id,1),"Chunked coarse mesh upload did not complete.");
    auto resident=cache.resident(asset.id,1,0,0);check(resident && resident->level==last,"Unavailable detail must use an initialized coarse LOD.");
    const auto bytes=resident->bytes;cache.evict(asset.id);check(cache.bytes()==bytes,"Pinned retired mesh buffers must remain accounted.");
    resident.reset();check(cache.bytes()==0,"Unleased retired mesh buffers should release their GPU allocation.");
    for(int i=0;i<3;++i)bgfx::frame();
}
}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-gpu-streaming-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    bool initialized=false;
    try {
        std::filesystem::create_directories(root);bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
        check(bgfx::init(init),"Could not initialize noninteractive Noop graphics checks.");initialized=true;
        textures(root);uploadBudget(root);models(root);bgfx::shutdown();initialized=false;cleanup(root);
        std::cout<<"Noop texture/mesh streaming lifetime checks passed.\n";return 0;
    } catch(const std::exception& error) {
        if(initialized)bgfx::shutdown();try { cleanup(root); } catch (...) {} std::cerr<<error.what()<<'\n';return 1;
    }
}
