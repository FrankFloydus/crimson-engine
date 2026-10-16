#include "ResourceManager.h"
#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <stdexcept>

namespace crimson {
std::string resourcePathKey(const std::filesystem::path& path) {
    auto bytes=std::filesystem::absolute(path).lexically_normal().generic_u8string();
    std::string result(reinterpret_cast<const char*>(bytes.data()),bytes.size());
    std::transform(result.begin(),result.end(),result.begin(),[](unsigned char value){return static_cast<char>(std::tolower(value));});
    return result;
}
std::uint64_t fingerprintFiles(const std::vector<std::filesystem::path>& files) {
    std::uint64_t hash=14695981039346656037ull;
    const auto append=[&](const char* bytes,std::size_t count) { for(std::size_t i=0;i<count;++i) { hash^=static_cast<unsigned char>(bytes[i]); hash*=1099511628211ull; } };
    std::array<char,64*1024> buffer{};
    for(const auto& file:files) {
        const auto name=resourcePathKey(file); append(name.data(),name.size());
        std::ifstream input(file,std::ios::binary);
        if(!input) throw std::runtime_error("Resource dependency is unavailable: "+name);
        while(input) { input.read(buffer.data(),buffer.size()); append(buffer.data(),static_cast<std::size_t>(input.gcount())); }
        if(!input.eof()) throw std::runtime_error("Could not read resource dependency: "+name);
    }
    return hash;
}
std::uint64_t fingerprintFileStates(const std::vector<std::filesystem::path>& files) {
    uint64_t hash=14695981039346656037ull;
    const auto append=[&](std::string_view value){for(const auto c:value){hash^=static_cast<unsigned char>(c);hash*=1099511628211ull;}hash^=0;hash*=1099511628211ull;};
    for(const auto& file:files){const auto name=resourcePathKey(file);if(!std::filesystem::is_regular_file(file))throw std::runtime_error("Resource dependency is unavailable: "+name);
        append(name);append(std::to_string(std::filesystem::file_size(file)));append(std::to_string(std::filesystem::last_write_time(file).time_since_epoch().count()));}
    return hash;
}
ResourceManager::ResourceManager(ResourceConfig config) : config_(config) {
    config_.workers=std::clamp<std::size_t>(config_.workers,1,16);
    config_.maximumQueued=std::max<std::size_t>(1,config_.maximumQueued);
    for(std::size_t i=0;i<config_.workers;++i) workers_.emplace_back([this](std::stop_token token){worker(token);});
}
ResourceManager::~ResourceManager() { stop(); }
bool ResourceManager::enqueue(Task task) {
    std::lock_guard lock(queueMutex_);
    if(stopped_.load())return false;
    if(tasks_.size()>=config_.maximumQueued){
        const auto priority=[](const auto& queued){const auto record=queued.record.lock();return record?record->priority.load(std::memory_order_relaxed):queued.priority;};
        const auto worst=std::max_element(tasks_.begin(),tasks_.end(),[&](const auto& a,const auto& b){const auto ap=priority(a),bp=priority(b);return ap==bp?a.sequence<b.sequence:ap<bp;});
        if(worst==tasks_.end()||priority(task)>=priority(*worst))return false;
        const auto incoming=task.record.lock(),evicted=worst->record.lock();
        if(evicted&&evicted!=incoming){
            // schedule holds the incoming record lock. Never invert record/queue
            // locks if another reader briefly owns the preempted record.
            std::unique_lock stateLock(evicted->mutex,std::try_to_lock);if(!stateLock.owns_lock())return false;
            if(evicted->epoch==worst->epoch&&evicted->job==worst->job){evicted->cancellation.request_stop();evicted->scheduled=false;evicted->state=ResourceState::Queued;}
        }
        tasks_.erase(worst);
    }
    tasks_.push_back(std::move(task)); condition_.notify_one(); return true;
}
void ResourceManager::complete(std::function<void()> operation) {
    std::lock_guard lock(completionMutex_); completions_.push_back(std::move(operation));
}
void ResourceManager::worker(std::stop_token token) {
    while(!token.stop_requested()) {
        Task task{};
        {
            std::unique_lock lock(queueMutex_);
            condition_.wait(lock,token,[this]{return stopped_.load() || !tasks_.empty();});
            if(token.stop_requested() || stopped_.load()) return;
            const auto best=std::min_element(tasks_.begin(),tasks_.end(),[](const auto& a,const auto& b){
                const auto ar=a.record.lock(),br=b.record.lock();
                const auto ap=ar?ar->priority.load(std::memory_order_relaxed):a.priority,bp=br?br->priority.load(std::memory_order_relaxed):b.priority;
                return ap==bp ? a.sequence<b.sequence : ap<bp;
            });
            task=std::move(*best); tasks_.erase(best); running_.fetch_add(1);
        }
        task.run(); running_.fetch_sub(1);
    }
}
void ResourceManager::pump() {
    std::deque<std::function<void()>> ready;
    { std::lock_guard lock(completionMutex_); ready.swap(completions_); }
    for(auto& operation:ready) operation();
    {
        std::lock_guard lock(storesMutex_);
        auto ordered=headers_;std::stable_sort(ordered.begin(),ordered.end(),[](const auto& a,const auto& b){return a->priority.load(std::memory_order_relaxed)<b->priority.load(std::memory_order_relaxed);});
        for(const auto& record:ordered) if(record->schedule) record->schedule();
    }
    trim();
}
void ResourceManager::invalidate(const std::string& id) {
    std::lock_guard lock(storesMutex_);
    std::unordered_set<std::string> affected{id};bool expanded=true;
    while(expanded){expanded=false;for(const auto& record:headers_){std::lock_guard stateLock(record->mutex);
        if(record->derived||record->deleted||affected.contains(record->id))continue;
        if(std::any_of(record->options.assetDependencies.begin(),record->options.assetDependencies.end(),[&](const auto& dependency){return affected.contains(dependency);}))if(affected.insert(record->id).second)expanded=true;}}
    for(const auto& record:headers_) {
        std::lock_guard stateLock(record->mutex);
        if(record->derived || record->deleted || !affected.contains(record->id)) continue;
        record->cancellation.request_stop(); record->epoch=nextRevision_.fetch_add(1); record->scheduled=false; record->state=ResourceState::Queued;
    }
}
void ResourceManager::invalidatePath(const std::filesystem::path& path) {
    const auto key=resourcePathKey(path);
    std::lock_guard lock(storesMutex_);
    std::unordered_set<std::string> affected;
    for(const auto& record:headers_){std::lock_guard stateLock(record->mutex);if(!record->derived&&!record->deleted&&std::any_of(record->options.dependencies.begin(),record->options.dependencies.end(),[&](const auto& dependency){return resourcePathKey(dependency)==key;}))affected.insert(record->id);}
    bool expanded=true;while(expanded){expanded=false;for(const auto& record:headers_){std::lock_guard stateLock(record->mutex);if(record->derived||record->deleted||affected.contains(record->id))continue;
        if(std::any_of(record->options.assetDependencies.begin(),record->options.assetDependencies.end(),[&](const auto& dependency){return affected.contains(dependency);}))if(affected.insert(record->id).second)expanded=true;}}
    for(const auto& record:headers_) {
        std::lock_guard stateLock(record->mutex);
        if(record->derived || record->deleted || !affected.contains(record->id)) continue;
        record->cancellation.request_stop(); record->epoch=nextRevision_.fetch_add(1); record->scheduled=false; record->state=ResourceState::Queued;
    }
}
void ResourceManager::forget(const std::string& id) {
    std::lock_guard lock(storesMutex_);
    if(!id.starts_with("import:"))forgottenIds_.insert(id);
    for(const auto& record:headers_) if(!record->derived&&record->id==id) {
        std::lock_guard stateLock(record->mutex);
        record->cancellation.request_stop(); record->epoch=nextRevision_.fetch_add(1); record->deleted=true; record->scheduled=false;
        if(record->evict) record->evict();
        if(record->clearLoader)record->clearLoader();
    }
}
void ResourceManager::trim() {
    std::lock_guard lock(storesMutex_);
    std::vector<std::pair<std::chrono::steady_clock::time_point,std::shared_ptr<resource_detail::Header>>> candidates;
    for(const auto& record:headers_) { std::lock_guard stateLock(record->mutex); candidates.emplace_back(record->used,record); }
    std::sort(candidates.begin(),candidates.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    const auto now=std::chrono::steady_clock::now();
    for(const auto& [used,record]:candidates) {
        (void)used;
        std::lock_guard stateLock(record->mutex);
        if(record->interests.load() || record->scheduled || (record->state!=ResourceState::Ready&&record->state!=ResourceState::Failed) || !record->canEvict || !record->canEvict()) continue;
        if(accounting_->bytes.load()<=config_.cpuRetentionBytes && now-record->used<config_.idleGrace) continue;
        if(record->evict) { record->evict(); evictions_.fetch_add(1); }
    }
    // Synthetic import jobs have no durable identity. Drop their retired record
    // metadata once consumers let go; immutable version readers remain valid.
    const auto removable=[&](const auto& record){std::lock_guard stateLock(record->mutex);return record->interests.load()==0&&!record->scheduled&&
        (record->deleted||((!record->hasPayload||!record->hasPayload())&&now-record->used>=config_.idleGrace));};
    std::apply([&](auto&... stores){(std::erase_if(stores.records,[&](const auto& item){return removable(item.second);}),...);},stores_);
    std::erase_if(headers_,removable);
}
void ResourceManager::stop() {
    if(stopped_.exchange(true)) return;
    {
        std::lock_guard lock(storesMutex_);
        for(const auto& record:headers_) { std::lock_guard stateLock(record->mutex); record->cancellation.request_stop(); record->schedule={};if(record->clearLoader)record->clearLoader(); }
    }
    { std::lock_guard lock(queueMutex_); tasks_.clear(); }
    for(auto& thread:workers_) thread.request_stop();
    condition_.notify_all(); workers_.clear();
    { std::lock_guard lock(completionMutex_); completions_.clear(); }
}
bool ResourceManager::idle() const {
    std::scoped_lock lock(storesMutex_,queueMutex_,completionMutex_);
    if(!tasks_.empty()||!completions_.empty()||running_.load()!=0)return false;
    if(stopped_.load())return true;
    for(const auto& record:headers_){std::lock_guard stateLock(record->mutex);if(!record->deleted&&record->interests.load()!=0&&record->state==ResourceState::Queued&&record->hasLoader&&record->hasLoader())return false;}
    return true;
}
ResourceStats ResourceManager::stats() const {
    ResourceStats result;
    { std::lock_guard lock(storesMutex_); result.records=headers_.size(); for(const auto& record:headers_) result.liveInterests+=record->interests.load(); }
    { std::lock_guard lock(queueMutex_); result.queued=tasks_.size(); }
    result.running=running_.load(); result.cpuBytes=accounting_->bytes.load(); result.loads=loads_.load(); result.cacheHits=cacheHits_.load();
    result.failures=failures_.load(); result.staleResults=staleResults_.load(); result.evictions=evictions_.load(); return result;
}
std::vector<ResourcePublication> ResourceManager::takePublications() { auto result=std::move(publications_); publications_.clear(); return result; }
}
