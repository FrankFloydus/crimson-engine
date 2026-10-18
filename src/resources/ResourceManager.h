#pragma once
#include "AssetRef.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <stdexcept>
#include <string>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace crimson {
struct ModelAsset; struct ModelGeometryLevel; struct DecodedImage; struct MaterialAsset;
struct SceneSnapshot; struct PreparedEnvironment; struct ShaderBinary; struct ModelImportStage; struct ImageImportStage;struct PreviewDependencies;struct DeletionValidation;struct CollisionMesh;using StaticTriangleMesh=CollisionMesh;struct CollisionOverlayGeometry;
struct PlayerOcclusionGeometry; struct NavigationTileData;
namespace streaming { struct ModelStream; struct TextureMipCache; }
enum class ResourceKind { Model, Geometry, Image, Material, Scene, Environment, Shader, ModelStream, TextureMips, ModelImport, ImageImport, PreviewMetadata, DeletionValidation, StaticCollision, CollisionOverlay, PlayerOcclusion, NavigationTile };
enum class ResourceState { Unloaded, Queued, Loading, Preparing, Ready, Failed };
enum class ResourcePriority : unsigned { Selected=0, Placement=1, Visible=2, Preview=3, Background=4 };
enum class DependencyValidation { Content, FileState };
template<class T> struct ResourceTraits;
#define CRIMSON_RESOURCE_TRAIT(Type, Kind) template<> struct ResourceTraits<Type> { static constexpr ResourceKind kind=ResourceKind::Kind; };
CRIMSON_RESOURCE_TRAIT(ModelAsset,Model)
CRIMSON_RESOURCE_TRAIT(ModelGeometryLevel,Geometry)
CRIMSON_RESOURCE_TRAIT(DecodedImage,Image)
CRIMSON_RESOURCE_TRAIT(MaterialAsset,Material)
CRIMSON_RESOURCE_TRAIT(SceneSnapshot,Scene)
CRIMSON_RESOURCE_TRAIT(PreparedEnvironment,Environment)
CRIMSON_RESOURCE_TRAIT(ShaderBinary,Shader)
CRIMSON_RESOURCE_TRAIT(streaming::ModelStream,ModelStream)
CRIMSON_RESOURCE_TRAIT(streaming::TextureMipCache,TextureMips)
CRIMSON_RESOURCE_TRAIT(ModelImportStage,ModelImport)
CRIMSON_RESOURCE_TRAIT(ImageImportStage,ImageImport)
CRIMSON_RESOURCE_TRAIT(PreviewDependencies,PreviewMetadata)
CRIMSON_RESOURCE_TRAIT(DeletionValidation,DeletionValidation)
CRIMSON_RESOURCE_TRAIT(StaticTriangleMesh,StaticCollision)
CRIMSON_RESOURCE_TRAIT(CollisionOverlayGeometry,CollisionOverlay)
CRIMSON_RESOURCE_TRAIT(PlayerOcclusionGeometry,PlayerOcclusion)
CRIMSON_RESOURCE_TRAIT(NavigationTileData,NavigationTile)
#undef CRIMSON_RESOURCE_TRAIT

struct ResourceConfig {
    std::size_t workers{2}, maximumQueued{128}, cpuRetentionBytes{512ull*1024*1024};
    // Soft target for streamable model/texture assets, not a total VRAM cap.
    // Fixed shadow/environment/post-process targets are accounted separately.
    std::size_t gpuRetentionBytes{512ull*1024*1024}, thumbnailBytes{64ull*1024*1024};
    std::chrono::milliseconds idleGrace{5000};
};
struct ResourceRequestOptions {
    std::string variant;
    ResourcePriority priority{ResourcePriority::Visible};
    std::vector<std::filesystem::path> dependencies;
    std::vector<std::string> assetDependencies;
    bool forceReload{};
    bool retainLoader{true}; // One-shot staging/cache jobs must release captured payloads.
    bool cancelWhenUnused{}; // Discard superseded derived work even for one-shot loaders.
    DependencyValidation dependencyValidation{DependencyValidation::Content};
};
template<class T> struct ResourceLoadResult {
    std::shared_ptr<const T> data;
    std::size_t bytes{};
    std::uint64_t fingerprint{};
    std::vector<std::filesystem::path> dependencies;
};
struct ResourcePublication {
    std::string id, variant;
    ResourceKind kind{};
    std::uint64_t revision{}, fingerprint{};
};
struct ResourceStats {
    std::size_t records{}, liveInterests{}, cpuBytes{}, queued{}, running{};
    std::uint64_t loads{}, cacheHits{}, failures{}, staleResults{}, evictions{};
};
std::uint64_t fingerprintFiles(const std::vector<std::filesystem::path>&);
std::uint64_t fingerprintFileStates(const std::vector<std::filesystem::path>&);
std::string resourcePathKey(const std::filesystem::path&);

namespace resource_detail {
struct Accounting { std::atomic_size_t bytes{}; };
template<class T> struct Version {
    std::shared_ptr<const T> data;
    std::uint64_t revision{}, fingerprint{};
    std::size_t bytes{};
    std::shared_ptr<Accounting> accounting;
    Version(ResourceLoadResult<T> result,std::uint64_t rev,std::shared_ptr<Accounting> owner)
        : data(std::move(result.data)),revision(rev),fingerprint(result.fingerprint),bytes(result.bytes),accounting(std::move(owner)) {
        accounting->bytes.fetch_add(bytes,std::memory_order_relaxed);
    }
    ~Version() { accounting->bytes.fetch_sub(bytes,std::memory_order_relaxed); }
};
struct Header {
    mutable std::mutex mutex;
    std::string id,variant,diagnostic;
    ResourceKind kind{};
    ResourceState state{ResourceState::Unloaded};
    ResourceRequestOptions options;
    std::uint64_t epoch{1},job{},readyRevision{};
    std::atomic_size_t interests{};
    std::atomic<ResourcePriority> priority{ResourcePriority::Visible};
    std::chrono::steady_clock::time_point used{std::chrono::steady_clock::now()};
    std::stop_source cancellation;
    bool scheduled{},deleted{},derived{};
    std::function<void()> schedule,evict,clearLoader;
    std::function<bool()> canEvict,hasPayload,hasLoader;
};
template<class T> struct Record : Header {
    std::shared_ptr<const Version<T>> current;
    std::function<ResourceLoadResult<T>(std::stop_token)> loader;
};
template<class T> struct Store { std::unordered_map<std::string,std::shared_ptr<Record<T>>> records; };
}

template<class T> class ResourceSnapshot {
public:
    ResourceSnapshot() = default;
    explicit operator bool() const noexcept { return version_ && version_->data; }
    const T* operator->() const noexcept { return version_ ? version_->data.get() : nullptr; }
    const T& operator*() const { return *version_->data; }
    std::shared_ptr<const T> shared() const { return version_ ? std::shared_ptr<const T>{version_,version_->data.get()} : nullptr; }
    std::uint64_t revision() const noexcept { return version_ ? version_->revision : 0; }
    std::uint64_t fingerprint() const noexcept { return version_ ? version_->fingerprint : 0; }
    std::size_t bytes() const noexcept { return version_ ? version_->bytes : 0; }
private:
    template<class> friend class ResourceHandle;
    std::shared_ptr<const resource_detail::Version<T>> version_;
};
template<class T> class ResourceHandle {
public:
    ResourceHandle() = default;
    ResourceHandle(const ResourceHandle& other) : record_(other.record_) { retain(); }
    ResourceHandle(ResourceHandle&& other) noexcept : record_(std::move(other.record_)) {}
    ResourceHandle& operator=(ResourceHandle other) noexcept { record_.swap(other.record_); return *this; }
    ~ResourceHandle() { if(record_) record_->interests.fetch_sub(1,std::memory_order_relaxed); }
    explicit operator bool() const noexcept { return bool(record_); }
    ResourceSnapshot<T> snapshot() const {
        ResourceSnapshot<T> result;
        if(!record_) return result;
        std::lock_guard lock(record_->mutex);
        record_->used=std::chrono::steady_clock::now();
        result.version_=record_->current;
        return result;
    }
    ResourceState state() const { if(!record_) return ResourceState::Unloaded; std::lock_guard lock(record_->mutex); return record_->state; }
    std::string diagnostic() const { if(!record_) return {}; std::lock_guard lock(record_->mutex); return record_->diagnostic; }
    std::vector<std::filesystem::path> dependencies() const { if(!record_)return {};std::lock_guard lock(record_->mutex);return record_->options.dependencies; }
    AssetRef<T> reference() const { return record_ && !record_->derived ? AssetRef<T>{record_->id} : AssetRef<T>{}; }
private:
    friend class ResourceManager;
    std::shared_ptr<resource_detail::Record<T>> record_;
    explicit ResourceHandle(std::shared_ptr<resource_detail::Record<T>> record) : record_(std::move(record)) { retain(); }
    void retain() { if(record_) record_->interests.fetch_add(1,std::memory_order_relaxed); }
};

class ResourceManager final {
public:
    explicit ResourceManager(ResourceConfig config={});
    ~ResourceManager();
    ResourceManager(const ResourceManager&)=delete;
    ResourceManager& operator=(const ResourceManager&)=delete;
    template<class T,class Loader> ResourceHandle<T> request(AssetRef<T> reference,Loader loader,ResourceRequestOptions options={}) {
        return requestKey<T>(std::move(reference.id),false,std::move(loader),std::move(options));
    }
    // Content-keyed transient CPU work has no authored reference or deletion
    // identity. It shares normal workers, immutable publication and accounting.
    template<class T,class Loader> ResourceHandle<T> requestDerived(std::string key,Loader loader,ResourceRequestOptions options={}) {
        return requestKey<T>(std::move(key),true,std::move(loader),std::move(options));
    }
private:
    template<class T,class Loader> ResourceHandle<T> requestKey(std::string id,bool derived,Loader loader,ResourceRequestOptions options) {
        if(id.empty() || stopped_.load()) return {};
        std::lock_guard storesLock(storesMutex_);
        if(stopped_.load() || (!derived&&forgottenIds_.contains(id)))return {};
        auto& store=std::get<resource_detail::Store<T>>(stores_).records;
        const auto key=std::string(1,derived?'\1':'\0')+id+'\0'+options.variant;
        auto& item=store[key];
        if(!item) {
            item=std::make_shared<resource_detail::Record<T>>(); item->id=std::move(id);item->derived=derived;item->epoch=nextRevision_.fetch_add(1);
            item->variant=options.variant; item->kind=ResourceTraits<T>::kind;
            const std::weak_ptr<resource_detail::Record<T>> weak=item;
            item->schedule=[this,weak] { if(auto record=weak.lock()) schedule(record); };
            item->evict=[weak] { if(auto record=weak.lock()) { record->current.reset(); record->state=record->diagnostic.empty()?ResourceState::Unloaded:ResourceState::Failed; } };
            item->canEvict=[weak] { if(auto record=weak.lock()) return record->current && record->current.use_count()==1; return false; };
            item->hasPayload=[weak] { if(auto record=weak.lock())return bool(record->current);return false; };
            item->hasLoader=[weak] { if(auto record=weak.lock())return bool(record->loader);return false; };
            item->clearLoader=[weak] { if(auto record=weak.lock()) record->loader={}; };
            headers_.push_back(item);
        }
        {
            std::lock_guard lock(item->mutex);
            if(item->deleted) return {};
            if(item->current&&item->readyRevision==item->epoch&&!options.forceReload&&!options.retainLoader)item->loader={};
            else item->loader=std::move(loader);
            item->options=std::move(options); item->used=std::chrono::steady_clock::now();
            item->priority.store(item->options.priority,std::memory_order_relaxed);
            if(item->options.forceReload) { item->cancellation.request_stop(); item->epoch=nextRevision_.fetch_add(1); item->scheduled=false; item->state=ResourceState::Queued; }
            if(item->current && item->readyRevision==item->epoch && !item->options.forceReload) cacheHits_.fetch_add(1);
        }
        ResourceHandle<T> handle{item};
        schedule(item);
        return handle;
    }
public:
    // Main-thread publication/preparation boundary, before building frame snapshots.
    void pump();
    template<class T> void seed(AssetRef<T> reference,ResourceLoadResult<T> value,ResourceRequestOptions options={}) {
        // Queue an already decoded import through the same publication/epoch checks.
        options.retainLoader=false;
        (void)request<T>(std::move(reference),[value=std::move(value)](std::stop_token){return value;},std::move(options));
    }
    void invalidate(const std::string& id);
    void invalidatePath(const std::filesystem::path&);
    // Retire every live variant without reserving the authored identity. A
    // later validated source publication may request a fresh generation.
    void discard(const std::string& id);
    // Explicit project deletion permanently rejects late ordinary requests.
    void forget(const std::string& id);
    void trim();
    void stop();
    bool idle() const;
    ResourceStats stats() const;
    std::vector<ResourcePublication> takePublications();
    const ResourceConfig& config() const noexcept { return config_; }
private:
    struct Task { ResourcePriority priority; std::uint64_t sequence; std::function<void()> run; std::weak_ptr<resource_detail::Header> record; std::uint64_t epoch{},job{}; };
    ResourceConfig config_;
    std::shared_ptr<resource_detail::Accounting> accounting_{std::make_shared<resource_detail::Accounting>()};
    using Stores=std::tuple<resource_detail::Store<ModelAsset>,resource_detail::Store<ModelGeometryLevel>,
        resource_detail::Store<DecodedImage>,resource_detail::Store<MaterialAsset>,resource_detail::Store<SceneSnapshot>,
        resource_detail::Store<PreparedEnvironment>,resource_detail::Store<ShaderBinary>,
        resource_detail::Store<streaming::ModelStream>,resource_detail::Store<streaming::TextureMipCache>,
        resource_detail::Store<ModelImportStage>,resource_detail::Store<ImageImportStage>,resource_detail::Store<PreviewDependencies>,resource_detail::Store<DeletionValidation>,resource_detail::Store<StaticTriangleMesh>,resource_detail::Store<CollisionOverlayGeometry>,resource_detail::Store<PlayerOcclusionGeometry>,resource_detail::Store<NavigationTileData>>;
    Stores stores_;
    mutable std::mutex storesMutex_,queueMutex_,completionMutex_;
    std::condition_variable_any condition_;
    std::vector<std::shared_ptr<resource_detail::Header>> headers_;
    std::unordered_set<std::string> forgottenIds_; // Persistent identities cannot be reintroduced by a late consumer.
    std::vector<Task> tasks_;
    std::deque<std::function<void()>> completions_;
    std::vector<ResourcePublication> publications_;
    std::vector<std::jthread> workers_;
    std::atomic_bool stopped_{};
    std::atomic_size_t running_{};
    std::atomic_uint64_t sequence_{},nextRevision_{1},loads_{},cacheHits_{},failures_{},staleResults_{},evictions_{};
    bool enqueue(Task);
    void complete(std::function<void()>);
    void worker(std::stop_token);
    void retire(const std::string& id,bool preventReload);
    template<class T> void schedule(const std::shared_ptr<resource_detail::Record<T>>& record) {
        std::lock_guard lock(record->mutex);
        if(stopped_.load() || record->deleted || record->scheduled || !record->loader || record->interests.load()==0 ||
            (record->current && record->readyRevision==record->epoch) || record->state==ResourceState::Failed) return;
        record->cancellation=std::stop_source{};
        const auto token=record->cancellation.get_token();
        const auto epoch=record->epoch,job=++record->job;
        auto loader=record->loader;
        auto dependencies=record->options.dependencies;
        const auto validation=record->options.dependencyValidation;
        const auto work=[this,record,loader=std::move(loader),dependencies=std::move(dependencies),validation,epoch,job,token] {
            {
                std::lock_guard stateLock(record->mutex);
                if(record->epoch!=epoch || record->job!=job || token.stop_requested()) { staleResults_.fetch_add(1); return; }
                if(record->interests.load()==0&&(record->options.retainLoader||record->options.cancelWhenUnused)) {
                    record->scheduled=false;record->state=record->current?ResourceState::Ready:ResourceState::Unloaded;
                    if(!record->options.retainLoader)record->loader={};return;
                }
                record->state=ResourceState::Loading;
            }
            std::shared_ptr<const resource_detail::Version<T>> version;
            std::vector<std::filesystem::path> discovered;
            std::string failure;
            try {
                if(token.stop_requested()) throw std::runtime_error("Resource load canceled.");
                const auto fingerprint=[&]{return validation==DependencyValidation::Content?fingerprintFiles(dependencies):fingerprintFileStates(dependencies);};
                const auto initial=dependencies.empty() ? 0 : fingerprint();
                auto result=loader(token);
                if(!result.data) throw std::runtime_error("Resource loader returned no data.");
                if(token.stop_requested()) throw std::runtime_error("Resource load canceled.");
                if(!dependencies.empty() && initial!=fingerprint())
                    throw std::runtime_error("Resource dependencies changed during loading.");
                if(!result.fingerprint) result.fingerprint=initial;
                discovered=std::move(result.dependencies);
                version=std::make_shared<resource_detail::Version<T>>(std::move(result),epoch,accounting_);
            } catch(const std::exception& error) { failure=error.what(); }
            catch(...) { failure="Unknown resource loader failure."; }
            {
                std::lock_guard stateLock(record->mutex);
                if(record->epoch==epoch && record->job==job) record->state=ResourceState::Preparing;
            }
            complete([this,record,version=std::move(version),discovered=std::move(discovered),failure=std::move(failure),epoch,job,token] {
                std::lock_guard publishLock(record->mutex);
                if(stopped_.load() || record->deleted || record->epoch!=epoch || record->job!=job || token.stop_requested()) { staleResults_.fetch_add(1); return; }
                record->scheduled=false;
                if(record->interests.load()==0&&(record->options.retainLoader||record->options.cancelWhenUnused)) {
                    record->state=record->current?ResourceState::Ready:ResourceState::Unloaded;
                    if(!record->options.retainLoader)record->loader={};return;
                }
                if(!version) { record->state=ResourceState::Failed; record->diagnostic=failure;if(!record->options.retainLoader)record->loader={};failures_.fetch_add(1); return; }
                record->current=version; record->readyRevision=epoch; record->state=ResourceState::Ready; record->diagnostic.clear();
                if(!record->options.retainLoader) record->loader={};
                if(!discovered.empty()) record->options.dependencies=std::move(discovered);
                publications_.push_back({record->id,record->variant,record->kind,epoch,version->fingerprint});
                loads_.fetch_add(1);
            });
        };
        record->state=ResourceState::Queued;
        record->scheduled=enqueue({record->options.priority,sequence_.fetch_add(1),work,record,epoch,job});
    }
};
}
