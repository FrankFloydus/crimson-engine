#include "crimson-renderer/ModelGpuCache.h"
#include "resources/StreamingModel.h"
#include "crimson-renderer/ModelDeformationGpu.h"
#include "crimson-renderer/ThumbnailCache.h"
#include "editor/AssetPreviews.h"
#include "resources/ImageDecoder.h"
#include "resources/TextureStreamer.h"
#include "assets/AtomicFile.h"
#include "assets/MaterialAsset.h"
#include <bgfx/platform.h>
#include <lodepng.h>
#include <array>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
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
    auto file=root/"source.png";writePng(file,256,128,70);
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
        const auto movedFingerprint=reloaded.sourceFingerprint;const auto retainedHandle=reloaded.handle;
        const auto destination=root/"Content/Textures/Renamed.png";std::filesystem::create_directories(destination.parent_path());
        std::filesystem::rename(file,destination);file=destination;
        const auto moving=streamer.request("texture-fixture",file,streaming::TextureRole::Color,true,128);
        check(moving.handle.idx==retainedHandle.idx,"Moving a stable image ID discarded its last prepared texture.");
        auto moved=until([&](const auto& result){return result.complete&&result.sourceFingerprint!=movedFingerprint;},128);
        check(moved.sourceWidth==128&&reloaded.handle.idx==retainedHandle.idx,"Streaming a moved source failed to retarget or invalidated a reader lease.");
        coarse={};detailed={};reloaded={};moved={};
        streamer.beginFrame();streamer.trim(0);
        // 'preserved' is still a lease: trimming must keep its allocation accounted.
        check(streamer.residentBytes()>0,"A leased retired texture must survive cache eviction.");
    }
    manager.pump();manager.trim();
    check(manager.stats().cpuBytes==0,"Completed mip jobs must not retain captured source pixels or CPU mip interests.");
    for(int i=0;i<3;++i) bgfx::frame();
}
void repeatedBuiltinTextures(const std::filesystem::path& root) {
    ResourceManager manager;TextureStreamer streamer(manager,root);
    const auto request=[&] {
        return streamer.request(NeutralNormalTextureId,{},streaming::TextureRole::Normal,false,1,ResourcePriority::Selected);
    };
    PreparedTexture ready;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
    do {
        manager.pump();streamer.beginFrame();ready=request();(void)request();streamer.advance();bgfx::frame();
        check(ready.diagnostic.empty(),"Repeated built-in texture requests failed with an empty physical source.");
        if(ready.complete)break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(std::chrono::steady_clock::now()<deadline);
    check(ready.complete,"Built-in normal texture did not finish preparing.");
    const auto handle=ready.handle;const auto loads=manager.stats().loads;
    for(unsigned i=0;i<8;++i) {
        manager.pump();streamer.beginFrame();const auto reused=request();streamer.advance();bgfx::frame();
        check(reused.complete&&reused.handle.idx==handle.idx&&reused.diagnostic.empty(),
            "A repeated built-in texture request replaced its stable GPU allocation.");
    }
    check(manager.stats().loads==loads,"Repeated built-in requests queued unnecessary source work.");
    manager.stop();
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
void thumbnailImages(const std::filesystem::path& root) {
    constexpr unsigned edge=128,count=120;
    DecodedImage image{edge,edge,std::vector<std::uint8_t>(edge*edge*4,192)};
    ThumbnailCache cache;
    std::vector<bgfx::TextureHandle> handles;
    for(unsigned i=0;i<count;++i) {
        const auto id="browser-card-"+std::to_string(i);
        check(cache.publish(id,1,image),"Browser thumbnail publication failed.");handles.push_back(cache.find(id,1));
    }
    check(cache.bytes()==std::size_t(count)*edge*edge*4,"Thumbnail bytes must reflect actual card resolution.");
    for(unsigned i=0;i<5;++i) bgfx::frame();
    for(unsigned i=0;i<count;++i) {
        const auto id="browser-card-"+std::to_string(i);
        check(cache.find(id,1).idx==handles[i].idx,"Scrolling through more than 64 ordinary cards evicted a reusable thumbnail.");
        check(cache.publish(id,1,image) && cache.find(id,1).idx==handles[i].idx,"An unchanged thumbnail allocated a replacement GPU texture.");
    }
    check(!bgfx::isValid(cache.find("browser-card-0",2)),"A changed thumbnail signature reused stale GPU pixels.");
    const auto png=root/"persisted-thumbnail.png";writePng(png,edge,edge,40);
    ThumbnailCache bounded(std::size_t(edge)*edge*4*2);
    const auto pixels=decodeManagedImage(png);
    PreviewCacheState state;check(state.needsDiskRead(7),"Cold thumbnail skipped its persistent cache.");
    check(bounded.publish("persisted",7,pixels),"Persistent thumbnail publication failed.");state.published=7;
    check(bounded.publish("second",1,image),"Second bounded thumbnail failed.");
    (void)bounded.find("persisted",7);
    check(bounded.publish("third",1,image),"Third bounded thumbnail failed.");
    check(bgfx::isValid(bounded.find("persisted",7)) && !bgfx::isValid(bounded.find("second",1)),"Thumbnail eviction did not retain the recently visited card.");
    bounded.trim(0);check(bounded.bytes()==0,"Thumbnail trimming exceeded its requested budget.");
    check(state.needsDiskRead(7),"GPU eviction did not retry the already completed disk image.");
    check(bounded.publish("persisted",7,decodeManagedImage(png)) && bgfx::isValid(bounded.find("persisted",7)),"An evicted thumbnail could not restore from its PNG without rendering geometry.");
}
void thumbnailTextures(const std::filesystem::path& root) {
    constexpr unsigned width=128,height=64;
    ResourceConfig config;config.idleGrace=std::chrono::milliseconds{0};config.thumbnailBytes=width*height*4*2;
    ResourceManager manager(config);TextureStreamer streamer(manager,root);
    const auto file=root/"thumbnail-texture.png";writePng(file,width,height,180);
    const auto request=[&](const std::string& id){return streamer.request(id,file,streaming::TextureRole::Data,false,width,ResourcePriority::Preview,true);};
    const auto until=[&](const std::string& id) {
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
        do {
            manager.pump();streamer.beginFrame();auto prepared=request(id);streamer.advance();bgfx::frame();
            if(!prepared.diagnostic.empty())throw std::runtime_error(prepared.diagnostic);
            if(prepared.complete && prepared.width==width)return prepared;
            std::this_thread::sleep_for(std::chrono::milliseconds{1});
        }while(std::chrono::steady_clock::now()<deadline);
        throw std::runtime_error("Raw thumbnail texture did not become ready.");
    };
    auto first=until("thumbnail-first");const auto retained=first.handle;first={};
    const auto loads=manager.stats().loads;
    streamer.beginFrame();streamer.advance();streamer.trim(1024*1024);
    check(request("thumbnail-first").handle.idx==retained.idx,"An idle browser image reloaded despite fitting the thumbnail budget.");
    manager.pump();check(manager.stats().loads==loads,"Revisiting an idle thumbnail scheduled new disk work.");
    auto second=until("thumbnail-second");second={};
    auto third=until("thumbnail-third");
    streamer.beginFrame();streamer.trim(1024*1024);
    check(!bgfx::isValid(request("thumbnail-first").handle),"Raw thumbnail working set ignored its bounded LRU budget.");
    check(bgfx::isValid(request("thumbnail-second").handle) && bgfx::isValid(request("thumbnail-third").handle),"Raw thumbnail eviction removed a recent image.");
    streamer.beginFrame();streamer.trim(0);
    check(streamer.residentBytes()==std::size_t(width)*height*4,"A leased thumbnail must remain accounted under pressure.");
    third={};streamer.beginFrame();streamer.trim(0);
    check(streamer.residentBytes()==0,"An unleased thumbnail did not release under memory pressure.");
}
void generatedTextures(const std::filesystem::path& root) {
    ResourceConfig config;config.cpuRetentionBytes=0;config.idleGrace=std::chrono::milliseconds{0};ResourceManager manager(config);
    const auto generatedRoot=root/"generated-memory-only";TextureStreamer streamer(manager,generatedRoot);
    const auto mainThread=std::this_thread::get_id();std::atomic_uint calls{};std::atomic_bool mainThreadGeneration{};
    const auto generator=[&] {
        ++calls;if(std::this_thread::get_id()==mainThread)mainThreadGeneration=true;
        return DecodedImage{128,128,std::vector<std::uint8_t>(128*128*4,128)};
    };
    const auto request=[&](bool thumbnail){return streamer.requestGenerated("recipe-a",generator,128,!thumbnail,thumbnail?64u:128u,ResourcePriority::Selected,thumbnail);};
    PreparedTexture runtime,thumbnail;
    const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    do {
        manager.pump();streamer.beginFrame();runtime=request(false);thumbnail=request(true);
        if(!runtime.diagnostic.empty()||!thumbnail.diagnostic.empty())throw std::runtime_error(runtime.diagnostic+thumbnail.diagnostic);
        if(runtime.complete&&thumbnail.complete)break;
        streamer.advance(4096,2);bgfx::frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(std::chrono::steady_clock::now()<deadline);
    check(runtime.complete&&thumbnail.complete&&runtime.width==128&&thumbnail.width==64,"Generated runtime/thumbnail images did not reach coherent requested residency.");
    check(calls==1&&!mainThreadGeneration&&runtime.sourceFingerprint==thumbnail.sourceFingerprint&&runtime.signature!=thumbnail.signature&&runtime.handle.idx!=thumbnail.handle.idx,
        "Generated views regenerated on the main thread, duplicated source work or confused display/data identities.");
    check(runtime.sourceWidth==128&&runtime.sourceHeight==128&&thumbnail.sourceWidth==128&&!std::filesystem::exists(generatedRoot),
        "Generated image streaming created a disk/catalog cache or lost source metadata.");
    const auto old=runtime.handle;const auto fingerprint=runtime.sourceFingerprint;
    const auto revised=[&]{++calls;return DecodedImage{128,128,std::vector<std::uint8_t>(128*128*4,64)};};
    auto next=streamer.requestGenerated("recipe-b",revised,128,true,128);
    check(!bgfx::isValid(next.handle)&&!next.sourceReady&&runtime.handle.idx==old.idx,"Pending generated content replaced an existing immutable GPU lease.");
    const auto replacementDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};
    do {
        manager.pump();streamer.beginFrame();next=streamer.requestGenerated("recipe-b",revised,128,true,128);
        if(!next.diagnostic.empty())throw std::runtime_error(next.diagnostic);
        if(next.complete)break;
        streamer.advance();bgfx::frame();std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(std::chrono::steady_clock::now()<replacementDeadline);
    check(next.complete&&next.sourceFingerprint!=fingerprint&&next.handle.idx!=old.idx&&runtime.handle.idx==old.idx&&calls==2,
        "Changed generator recipes failed to publish a separate complete version while retaining old readers.");
    streamer.invalidate("recipe-a");manager.forget("recipe-a");
    check(request(false).handle.idx==old.idx,"Managed asset invalidation/deletion removed a generated texture with the same opaque key.");
    next={};thumbnail={};runtime={};streamer.beginFrame();streamer.advance(0,0);streamer.trim(0);
    manager.pump();manager.trim();
    check(streamer.residentBytes()==0&&manager.stats().cpuBytes==0,"Generated CPU mip payloads or GPU versions remained retained after eviction.");
    const auto malformed=[] {return DecodedImage{2,2,std::vector<std::uint8_t>(16)};};
    const auto failureDeadline=std::chrono::steady_clock::now()+std::chrono::seconds{10};PreparedTexture failure;
    do {
        manager.pump();streamer.beginFrame();failure=streamer.requestGenerated("bad-recipe",malformed,16,false,16);streamer.advance();
        if(!failure.diagnostic.empty())break;
        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }while(std::chrono::steady_clock::now()<failureDeadline);
    check(!failure.diagnostic.empty()&&!bgfx::isValid(failure.handle),"Generated dimension mismatch failed to produce a diagnostic without publishing partial pixels.");
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
    const auto bytes=resident->bytes;
    const auto configureAlias=[&](const std::string& id,std::uint64_t revision=1) {
        auto alias=descriptor;alias.id=id;cache.configure(id,revision,alias);
    };
    const std::string alias="shared-level-alias";
    configureAlias(alias);cache.markRendered(alias,1);
    const auto signature=cache.signature();
    check(cache.queue(alias,1,coarse) && cache.queue(alias,1,coarse) && cache.queue(alias,1,coarse),
        "Repeated shared-level admission failed.");
    check(!cache.ready(alias,1) && !cache.resident(alias,1,0,0) && cache.signature()==signature,
        "A shared cached level changed visible geometry or picking during frame preparation.");
    check(cache.bytes()==bytes,"Duplicate shared admissions allocated replacement GPU buffers.");
    cache.advance(0,0);
    check(!cache.ready(alias,1) && cache.signature()==signature,"Shared admission exceeded a zero time budget.");
    cache.advance(0,2);
    check(cache.ready(alias,1) && cache.resident(alias,1,0,0).get()==resident.get() && cache.signature()==signature+1,
        "Frame advance did not publish exactly one shared resident object and invalidate prior Scene picking.");
    check(cache.queue(alias,1,coarse),"Queuing an already admitted resident level failed.");
    cache.advance(0,2);
    check(cache.signature()==signature+1 && cache.bytes()==bytes,"An already resident shared level was admitted twice.");

    const std::string evicted="shared-level-evicted";
    configureAlias(evicted);check(cache.queue(evicted,1,coarse),"Eviction admission fixture failed.");
    cache.evict(evicted);cache.advance(0,2);
    check(!cache.ready(evicted,1) && !cache.descriptor(evicted,1),"Eviction resurrected a pending shared admission.");

    const std::string stale="shared-level-stale";
    configureAlias(stale);check(cache.queue(stale,1,coarse),"Stale admission fixture failed.");
    configureAlias(stale,2);cache.markRendered(stale,2);
    const std::array<std::string,2> live{asset.id,alias};
    cache.trim(std::numeric_limits<std::size_t>::max(),live);cache.advance(0,2);
    check(!cache.ready(stale,1) && !cache.descriptor(stale,1),"A superseded revision published its pending shared level.");

    const std::string trimmed="shared-level-trimmed";
    configureAlias(trimmed);check(cache.queue(trimmed,1,coarse),"Pressure trim admission fixture failed.");
    cache.trim(0,live);cache.advance(0,2);
    check(!cache.ready(trimmed,1) && !cache.descriptor(trimmed,1),"Pressure trimming resurrected an unused shared admission.");
    check(cache.ready(asset.id,1) && cache.ready(alias,1) && cache.bytes()==bytes,
        "Cancelling unused shared admissions released a live shared allocation.");
    const std::string retained="shared-level-retained";
    configureAlias(retained);check(cache.queue(retained,1,coarse),"Retained shared admission fixture failed.");
    const auto* exact=resident.get();
    cache.evict(alias);cache.evict(stale);cache.evict(asset.id);resident.reset();
    check(cache.bytes()==bytes && !cache.ready(retained,1),"A pending shared admission failed to retain its sole GPU allocation owner.");
    cache.advance(0,2);resident=cache.resident(retained,1,0,0);
    check(cache.ready(retained,1) && resident.get()==exact,"Shared admission changed or lost the exact resident object after source eviction.");
    cache.evict(retained);check(cache.bytes()==bytes,"Pinned retired mesh buffers must remain accounted.");
    resident.reset();check(cache.bytes()==0,"Unleased retired mesh buffers should release their GPU allocation.");
    for(int i=0;i<3;++i)bgfx::frame();
}
void modelReopenedRevisions(const std::filesystem::path& root) {
    ModelAsset asset;asset.id="model-scene-reopen-fixture";ModelPrimitive primitive;
    primitive.vertices.resize(3);primitive.vertices[1].position={1,0,0};primitive.vertices[2].position={0,1,0};primitive.indices={0,1,2};
    asset.primitives.push_back(primitive);
    const auto descriptor=streaming::buildModelCache(asset,root,false);
    const auto source=streaming::loadModelLod(descriptor.primitives[0].levels[0]);
    ModelGpuCache cache;cache.configure(asset.id,1,descriptor);
    check(cache.queue(asset.id,1,source),"Initial scene model could not queue its GPU upload.");
    for(unsigned i=0;i<16&&!cache.ready(asset.id,1);++i){cache.advance(1024*1024,10);bgfx::frame();}
    check(cache.ready(asset.id,1),"Initial scene model failed to publish before reopening.");
    cache.markRendered(asset.id,1);
    auto first=cache.resident(asset.id,1,0,0);const auto* original=first.get();const auto bytes=cache.bytes();
    const std::array<std::string,1> live{asset.id};
    constexpr auto NoPressure=std::numeric_limits<std::size_t>::max();

    // Closing a scene can release its CPU metadata. Reloading that same asset
    // receives a newer resource revision while the old GPU level is cached.
    // EditorResources trims at the end of preparation, before the next advance.
    cache.configure(asset.id,2,descriptor);check(cache.queue(asset.id,2,source),"Reopened scene failed to queue a shared GPU admission.");
    check(!cache.ready(asset.id,2)&&cache.bytes()==bytes,"Reopened scene shared geometry published early or allocated duplicate buffers.");
    cache.trim(NoPressure,live);
    check(cache.descriptor(asset.id,2)&&cache.ready(asset.id,1),"End-of-frame cleanup deleted a newer scene revision before its shared admission.");
    for(unsigned i=0;i<16&&!cache.ready(asset.id,2);++i)cache.advance(0,10);
    check(cache.ready(asset.id,2)&&cache.resident(asset.id,2,0,0).get()==original,
        "Reopened scene did not publish its reused GPU buffers at the next frame boundary.");
    cache.markRendered(asset.id,2);cache.markRendered(asset.id,1,false);first.reset();
    cache.trim(NoPressure,live);
    check(!cache.descriptor(asset.id,1)&&cache.ready(asset.id,2)&&cache.bytes()==bytes,
        "An older last-good view regressed the rendered revision or prevented superseded scene cleanup.");

    // A replacement whose content changed must survive the same preparation
    // boundary as an actual pending upload, not just a shared-buffer admission.
    asset.primitives[0].vertices[1].position={2,0,0};
    const auto changed=streaming::buildModelCache(asset,root,false);
    check(changed.primitives[0].levels[0].key!=descriptor.primitives[0].levels[0].key,"Scene replacement fixture did not change geometry identity.");
    const auto replacement=streaming::loadModelLod(changed.primitives[0].levels[0]);
    cache.configure(asset.id,3,changed);check(cache.queue(asset.id,3,replacement),"New scene revision failed to queue a fresh GPU upload.");
    check(!cache.ready(asset.id,3)&&cache.bytes()>bytes,"Replacement fixture failed to retain an incomplete new GPU allocation.");
    cache.trim(NoPressure,live);cache.advance(sizeof(ModelVertex)-1,10);
    check(cache.descriptor(asset.id,3)&&!cache.ready(asset.id,3)&&cache.ready(asset.id,2),
        "Preparation discarded the new upload or exposed it before the upload budget allowed a vertex.");
    cache.trim(NoPressure,live);
    for(unsigned i=0;i<16&&!cache.ready(asset.id,3);++i){cache.advance(512,10);bgfx::frame();}
    check(cache.ready(asset.id,3)&&cache.ready(asset.id,2),"A fresh scene revision could not complete while the last-good version remained drawable.");
    cache.markRendered(asset.id,3);cache.markRendered(asset.id,2,false);cache.trim(NoPressure,live);
    check(!cache.descriptor(asset.id,2)&&cache.ready(asset.id,3)&&cache.bytes()==bytes,
        "Publishing a newer scene failed to retire the superseded GPU allocation.");
    cache.evict(asset.id);check(cache.bytes()==0,"Scene readmission regression retained unleased GPU allocations.");
    for(unsigned i=0;i<3;++i)bgfx::frame();
}
void deformationUploadBudget(const std::filesystem::path& root) {
    ModelAsset asset;asset.id="deformation-budget-fixture";ModelPrimitive primitive;
    primitive.vertices.resize(3);primitive.vertices[1].position={1,0,0};primitive.vertices[2].position={0,1,0};primitive.indices={0,1,2};
    primitive.skinWeights.resize(3);for(auto& value:primitive.skinWeights)value.weights[0]=1;
    primitive.hasSkinning=true;primitive.morphTargets.resize(32);for(auto& target:primitive.morphTargets)target.positions.assign(3,Vec3{.1f,0,0});
    asset.primitives.push_back(primitive);const auto descriptor=streaming::buildModelCache(asset,root,false);
    const auto source=streaming::loadModelLod(descriptor.primitives[0].levels[0]);
    ModelGpuCache cache;cache.configure(asset.id,1,descriptor);check(cache.queue(asset.id,1,source),"Could not queue deformation upload fixture.");
    const auto baseBytes=3*sizeof(ModelVertex)+3*sizeof(std::uint16_t);
    const auto auxiliaryBytes=3*16*sizeof(float)+3*32*9*sizeof(float);
    check(cache.bytes()>=baseBytes+auxiliaryBytes&&!cache.ready(asset.id,1),"Pending auxiliary deformation allocations were uncounted or published early.");
    constexpr auto budget=sizeof(ModelVertex);unsigned frames{};
    while(!cache.ready(asset.id,1)&&frames<200){cache.advance(budget,10);++frames;bgfx::frame();}
    check(frames>=((baseBytes+auxiliaryBytes+budget-1)/budget)&&frames<200,
        "Auxiliary skin/morph uploads escaped the shared per-frame byte budget.");
    auto resident=cache.resident(asset.id,1,0,0);check(resident&&resident->deformation&&resident->deformation->ready(),
        "Published animated LOD contains incomplete deformation inputs.");
    const auto bytes=cache.bytes();cache.evict(asset.id);check(cache.bytes()==bytes,"Eviction lost accounting for a leased animated LOD.");
    resident.reset();check(cache.bytes()==0,"Animated LOD retained unleased auxiliary buffers after eviction.");
    for(unsigned i=0;i<3;++i)bgfx::frame();
}

}
int main() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-gpu-streaming-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    bool initialized=false;
    try {
        std::filesystem::create_directories(root);bgfx::renderFrame();bgfx::Init init;init.type=bgfx::RendererType::Noop;init.resolution.width=init.resolution.height=1;
        check(bgfx::init(init),"Could not initialize noninteractive Noop graphics checks.");initialized=true;
        textures(root);repeatedBuiltinTextures(root);uploadBudget(root);thumbnailImages(root);thumbnailTextures(root);generatedTextures(root);models(root);modelReopenedRevisions(root);deformationUploadBudget(root);bgfx::shutdown();initialized=false;cleanup(root);
        std::cout<<"Noop texture/mesh streaming lifetime checks passed.\n";return 0;
    } catch(const std::exception& error) {
        if(initialized)bgfx::shutdown();try { cleanup(root); } catch (...) {} std::cerr<<error.what()<<'\n';return 1;
    }
}
