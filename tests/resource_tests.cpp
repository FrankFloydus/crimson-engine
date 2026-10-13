#include "resources/ResourceManager.h"
#include "resources/TextureMipCache.h"
#include "resources/FileWatcher.h"
#include <atomic>
#include <mutex>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

using namespace crimson;
namespace {
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
template<class Predicate> void await(ResourceManager& manager,Predicate predicate) {
    const auto end=std::chrono::steady_clock::now()+std::chrono::seconds{5};
    do { manager.pump(); if(predicate()) return; std::this_thread::sleep_for(std::chrono::milliseconds{1}); } while(std::chrono::steady_clock::now()<end);
    throw std::runtime_error("Resource worker did not finish.");
}
ResourceLoadResult<DecodedImage> image(unsigned value,std::uint64_t fingerprint=0) {
    auto data=std::make_shared<DecodedImage>(); data->width=1; data->height=1; data->rgba={static_cast<std::uint8_t>(value),0,0,255};
    return {data,data->rgba.size(),fingerprint,{}};
}
void handlesAndVersions() {
    ResourceManager manager;
    std::atomic_uint calls{}; std::atomic_uint value{10};
    const auto loader=[&](std::stop_token){++calls; return image(value.load(),value.load());};
    auto first=manager.request<DecodedImage>(AssetRef<DecodedImage>{"image"},loader);
    auto second=manager.request<DecodedImage>(AssetRef<DecodedImage>{"image"},loader);
    await(manager,[&]{return first.state()==ResourceState::Ready;});
    require(calls==1,"Duplicate consumers decoded twice.");
    auto old=first.snapshot(); require(old->rgba[0]==10,"Initial immutable snapshot is wrong.");
    auto copy=first; auto moved=std::move(copy);
    require(manager.stats().liveInterests==3,"RAII copy/move interest count is wrong.");
    value=20; manager.invalidate("image");
    await(manager,[&]{return first.snapshot().revision()==2;});
    require(first.snapshot()->rgba[0]==20 && old->rgba[0]==10,"Reload changed a reader's old data.");
    require(manager.stats().cpuBytes==8,"Both live immutable versions were not accounted.");
    old={}; require(manager.stats().cpuBytes==4,"Released old version remained allocated.");
    moved={}; second={}; first={}; require(manager.stats().liveInterests==0,"Handle destruction leaked interest.");
}
void failureAndStaleness() {
    ResourceManager manager; std::atomic_bool fail{},block{},entered{};
    const auto loader=[&](std::stop_token token) {
        entered=true; while(block.load() && !token.stop_requested()) std::this_thread::yield();
        if(fail.load()) throw std::runtime_error("expected corrupt source");
        return image(42);
    };
    auto handle=manager.request<DecodedImage>(AssetRef<DecodedImage>{"reload"},loader);
    await(manager,[&]{return handle.state()==ResourceState::Ready;});
    fail=true; manager.invalidate("reload"); await(manager,[&]{return handle.state()==ResourceState::Failed;});
    require(handle.snapshot() && handle.snapshot()->rgba[0]==42,"Failed reload destroyed working content.");
    const auto failures=manager.stats().failures; for(int i=0;i<10;++i) manager.pump();
    require(manager.stats().failures==failures,"Unchanged invalid source retried forever.");
    fail=false; entered=false; block=true; manager.invalidate("reload"); manager.pump();
    await(manager,[&]{return entered.load();}); manager.invalidate("reload"); block=false;
    await(manager,[&]{return handle.state()==ResourceState::Ready && handle.snapshot().revision()==4;});
    require(manager.stats().staleResults>0,"Superseded worker result was accepted.");
    auto pinned=handle.snapshot(); manager.forget("reload"); manager.pump();
    require(!handle.snapshot() && pinned->rgba[0]==42,"Deletion invalidated a pinned reader or retained new access.");
    require(!manager.request<DecodedImage>(AssetRef<DecodedImage>{"reload"},loader),"Deleted identity was silently resurrected.");
}
void retentionAndVariants() {
    ResourceConfig config; config.cpuRetentionBytes=0; config.idleGrace=std::chrono::milliseconds{0}; config.maximumQueued=1;
    ResourceManager manager(config);
    ResourceRequestOptions color; color.variant="color";
    ResourceRequestOptions normal; normal.variant="normal";
    auto a=manager.request<DecodedImage>(AssetRef<DecodedImage>{"shared"},[](std::stop_token){return image(5);},color);
    auto b=manager.request<DecodedImage>(AssetRef<DecodedImage>{"shared"},[](std::stop_token){return image(9);},normal);
    await(manager,[&]{return a.state()==ResourceState::Ready && b.state()==ResourceState::Ready;});
    require(a.snapshot()->rgba[0]==5 && b.snapshot()->rgba[0]==9,"Processing variants collided.");
    auto snapshot=a.snapshot(); auto shared=snapshot.shared(); a={}; snapshot={}; b={}; manager.trim();
    require(manager.stats().cpuBytes==4,"Eviction removed pinned bytes or retained unused bytes.");
    shared.reset(); manager.trim(); require(manager.stats().cpuBytes==0,"Unpinned cache did not trim under pressure.");
    require(manager.stats().evictions==2,"Evictions were not tracked.");
}
void dependencyAndWatcher() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-resource-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root); const auto source=root/"source.txt";
    try {
        {std::ofstream output(source); output<<"first";}
        ResourceManager manager; ResourceRequestOptions options; options.dependencies={source};
        auto handle=manager.request<DecodedImage>(AssetRef<DecodedImage>{"dependency"},[](std::stop_token){return image(1);},options);
        await(manager,[&]{return handle.state()==ResourceState::Ready;});
        const auto fingerprint=handle.snapshot().fingerprint(); require(fingerprint!=0,"Dependency fingerprint was omitted.");
        FileWatcher watcher({root},std::chrono::milliseconds{10}); std::this_thread::sleep_for(std::chrono::milliseconds{20});
        {std::ofstream output(source); output<<"changed";}
        std::vector<std::filesystem::path> changed;
        const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{3};
        while(changed.empty() && std::chrono::steady_clock::now()<deadline) { changed=watcher.poll(); std::this_thread::sleep_for(std::chrono::milliseconds{5}); }
        require(!changed.empty(),"Native file watcher did not report a source change.");
        manager.invalidatePath(source); await(manager,[&]{return handle.snapshot().revision()==2;});
        require(handle.snapshot().fingerprint()!=fingerprint,"Changed dependency retained its old fingerprint.");
        watcher.stop();
    } catch(...) { std::filesystem::remove_all(root); throw; }
    std::filesystem::remove_all(root);
}
void shutdown() {
    ResourceManager manager; std::atomic_bool entered{};
    auto handle=manager.request<DecodedImage>(AssetRef<DecodedImage>{"stop"},[&](std::stop_token token){entered=true; while(!token.stop_requested()) std::this_thread::yield(); return image(3);});
    await(manager,[&]{return entered.load();}); manager.stop();
    require(manager.idle(),"Shutdown left a worker or completion alive.");
    require(!manager.request<DecodedImage>(AssetRef<DecodedImage>{"new"},[](std::stop_token){return image(1);}),"Stopped manager admitted new work.");
}
void boundedQueueAndPriority() {
    ResourceConfig config;config.workers=1;config.maximumQueued=1;ResourceManager manager(config);
    std::atomic_bool entered{},release{};std::mutex orderMutex;std::vector<int> order;
    auto gate=manager.request<DecodedImage>(AssetRef<DecodedImage>{"gate"},[&](std::stop_token stop){entered=true;while(!release&&!stop.stop_requested())std::this_thread::yield();return image(1);});
    await(manager,[&]{return entered.load();});
    const auto load=[&](int value){return [&,value](std::stop_token){std::lock_guard lock(orderMutex);order.push_back(value);return image(static_cast<unsigned>(value));};};
    ResourceRequestOptions background;background.priority=ResourcePriority::Background;
    ResourceRequestOptions selected;selected.priority=ResourcePriority::Selected;
    auto lower=manager.request<DecodedImage>(AssetRef<DecodedImage>{"lower"},load(2),background);
    auto urgent=manager.request<DecodedImage>(AssetRef<DecodedImage>{"urgent"},load(3),selected);
    auto deferred=manager.request<DecodedImage>(AssetRef<DecodedImage>{"deferred"},load(4));
    require(manager.stats().queued<=1,"Bounded queue exceeded its configured capacity.");release=true;
    await(manager,[&]{return gate.state()==ResourceState::Ready&&lower.state()==ResourceState::Ready&&urgent.state()==ResourceState::Ready&&deferred.state()==ResourceState::Ready;});
    {std::lock_guard lock(orderMutex);require(order.size()==3&&order.front()==3,"Selected work did not preempt queued background work.");}
    require(manager.stats().queued<=1,"Deferred scheduling exceeded queue capacity.");
}
void seedLifetimeAndIdentityRevisions() {
    ResourceConfig config;config.cpuRetentionBytes=0;config.idleGrace=std::chrono::milliseconds{0};ResourceManager manager(config);
    auto payload=image(77);const std::weak_ptr<const DecodedImage> weak=payload.data;manager.seed<DecodedImage>(AssetRef<DecodedImage>{"seed"},std::move(payload));
    await(manager,[&]{return manager.stats().loads==1&&weak.expired();});
    require(manager.stats().cpuBytes==0,"One-shot seed retained its unaccounted captured pixels.");
    auto first=manager.request<DecodedImage>(AssetRef<DecodedImage>{"versioned"},[](std::stop_token){return image(5);});
    await(manager,[&]{return first.state()==ResourceState::Ready;});const auto old=first.snapshot().revision();first={};manager.trim();
    auto next=manager.request<DecodedImage>(AssetRef<DecodedImage>{"versioned"},[](std::stop_token){return image(6);});
    await(manager,[&]{return next.state()==ResourceState::Ready;});require(next.snapshot().revision()>old,"Evicted identity reused a prior source revision.");
    manager.forget("versioned");next={};manager.trim();require(!manager.request<DecodedImage>(AssetRef<DecodedImage>{"versioned"},[](std::stop_token){return image(9);}),"Pruned deleted identity was resurrected.");
    for(int i=0;i<32;++i){const auto id="import:test/"+std::to_string(i);auto job=manager.request<DecodedImage>(AssetRef<DecodedImage>{id},[](std::stop_token){return image(1);});manager.forget(id);}
    await(manager,[&]{return manager.idle();});manager.trim();require(manager.stats().records<=1,"Forgotten synthetic import records accumulated indefinitely.");
}
void dependencyClosureAndFailedRetention() {
    ResourceConfig config;config.cpuRetentionBytes=0;config.idleGrace=std::chrono::milliseconds{0};ResourceManager manager(config);
    std::atomic_uint callsA{},callsB{},callsC{};
    ResourceRequestOptions b;b.assetDependencies={"A"};ResourceRequestOptions c;c.assetDependencies={"B"};
    auto a=manager.request<DecodedImage>(AssetRef<DecodedImage>{"A"},[&](std::stop_token){++callsA;return image(1);});
    auto bh=manager.request<DecodedImage>(AssetRef<DecodedImage>{"B"},[&](std::stop_token){++callsB;return image(2);},b);
    auto ch=manager.request<DecodedImage>(AssetRef<DecodedImage>{"C"},[&](std::stop_token){++callsC;return image(3);},c);
    await(manager,[&]{return a.state()==ResourceState::Ready&&bh.state()==ResourceState::Ready&&ch.state()==ResourceState::Ready;});manager.invalidate("A");
    await(manager,[&]{return callsA==2&&callsB==2&&callsC==2&&manager.idle();});
    std::atomic_bool fail{};auto bad=manager.request<DecodedImage>(AssetRef<DecodedImage>{"bad"},[&](std::stop_token){if(fail)throw std::runtime_error("bad");return image(4);});
    await(manager,[&]{return bad.state()==ResourceState::Ready;});fail=true;manager.invalidate("bad");await(manager,[&]{return bad.state()==ResourceState::Failed;});
    bad={};manager.trim();require(manager.stats().cpuBytes==12,"Unpinned last-good bytes from a failed resource were never evicted.");
    a={};bh={};ch={};manager.trim();require(manager.stats().cpuBytes==0,"Dependency cache retained unpinned CPU data.");
}
void derivedFileStateValidation() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-file-state-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);const auto file=root/"trusted-cache.bin";
    try{
        {std::ofstream output(file,std::ios::binary);output<<"checked payload";}
        const auto first=fingerprintFileStates({file});ResourceManager manager;ResourceRequestOptions options;
        options.dependencies={file};options.dependencyValidation=DependencyValidation::FileState;
        std::atomic_bool block{},entered{};
        auto handle=manager.request<DecodedImage>(AssetRef<DecodedImage>{"mip-level"},[&](std::stop_token stop){entered=true;while(block&&!stop.stop_requested())std::this_thread::yield();return image(8,12345);},options);
        await(manager,[&]{return handle.state()==ResourceState::Ready;});require(handle.snapshot().fingerprint()==12345,"Metadata validation replaced the loader's payload content key.");
        const auto good=handle.snapshot();entered=false;block=true;manager.invalidate("mip-level");manager.pump();await(manager,[&]{return entered.load();});
        {std::ofstream output(file,std::ios::binary|std::ios::app);output<<"changed";}require(fingerprintFileStates({file})!=first,"File-state validation missed changed cache size/mtime.");
        block=false;await(manager,[&]{return handle.state()==ResourceState::Failed;});require(handle.snapshot().revision()==good.revision(),"Mid-read cache replacement destroyed the last-good version.");
    }catch(...){std::filesystem::remove_all(root);throw;}
    std::filesystem::remove_all(root);
}
}
int main() {
    try { handlesAndVersions(); failureAndStaleness(); retentionAndVariants(); dependencyAndWatcher(); shutdown();boundedQueueAndPriority();seedLifetimeAndIdentityRevisions();dependencyClosureAndFailedRetention();derivedFileStateValidation();std::cout<<"Resource lifecycle checks passed.\n"; }
    catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
