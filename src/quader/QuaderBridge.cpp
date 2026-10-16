#include "QuaderBridge.h"
#include "SaveNotificationWatcher.h"
#include "assets/ModelImportIdentity.h"
#include "assets/ContentStorage.h"
#include "resources/AssetId.h"
#include "resources/Paths.h"
#include <algorithm>
#include <chrono>
#include <deque>
#include <fstream>
#include <future>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <unordered_set>

namespace crimson {
namespace {
using Clock=std::chrono::steady_clock;
std::string pathKey(const std::filesystem::path& path){const auto text=std::filesystem::absolute(path).lexically_normal().generic_u8string();std::string result(reinterpret_cast<const char*>(text.data()),text.size());
    std::transform(result.begin(),result.end(),result.begin(),[](unsigned char c){return c>='A'&&c<='Z'?static_cast<char>(c+'a'-'A'):static_cast<char>(c);});return result;}
template<class T> bool ready(const std::future<T>& future){return future.valid()&&future.wait_for(std::chrono::seconds{0})==std::future_status::ready;}
bool sameConfiguration(QuaderLink left,QuaderLink right){left.saveKey.clear();right.saveKey.clear();return left==right;}
std::optional<std::string> previousFile(const std::filesystem::path& root,const std::string& id){const auto path=checkedManagedAssetPath(root,id);if(!std::filesystem::exists(path))return std::nullopt;
    std::ifstream input(path,std::ios::binary|std::ios::ate);const auto size=input.tellg();if(!input||size<0||size>512ll*1024*1024)throw std::runtime_error("Cannot snapshot a previous Quader asset.");
    std::string data(static_cast<std::size_t>(size),'\0');input.seekg(0);if(!data.empty())input.read(data.data(),static_cast<std::streamsize>(data.size()));if(!input)throw std::runtime_error("Previous Quader asset changed while reading.");return data;}
std::string completedMessage(const QuaderImportResult& result){std::ostringstream out;out<<"Imported "<<result.meshes.size()<<" meshes and "<<result.lights.size()<<" lights.";
    if(result.skippedCollisions)out<<' '<<result.skippedCollisions<<" unsupported collisions skipped.";
    if(result.skippedSpots)out<<' '<<result.skippedSpots<<" spot lights skipped.";
    if(result.skippedLights)out<<' '<<result.skippedLights<<" lights exceeded scene limits.";
    if(!result.deferredMaterials.empty())out<<" Material refresh waits for the open material to be saved or closed.";return out.str();}
}
struct QuaderBridge::Impl {
    struct Request {EntityId entity{};QuaderLink link;std::uint64_t generation{},serial{};bool manual{};std::optional<QuaderCompletedSave> completed;std::string key;
        std::optional<QuaderLink> baseline;};
    struct State {
        std::string message,failedKey;std::set<std::string> consumed;std::deque<std::string> consumedOrder;std::vector<std::string> deferred;
        Clock::time_point retryAt{},nextCheck{};std::uint32_t failures{};bool seeded{};
    };
    struct Watch {std::unique_ptr<SaveNotificationWatcher> watcher;Clock::time_point due{},fallback{};bool pending{true};};
    struct Job {
        Request request;std::stop_source cancellation;std::future<std::shared_ptr<QuaderSourceScene>> reading;
        std::future<QuaderImportResult> preparing;std::shared_ptr<QuaderSourceScene> source;
        std::optional<QuaderImportResult> result;std::optional<QuaderCompletedSave> manualBaseline;std::vector<std::string> blockedMaterials;bool invalid{};
        std::uint64_t assetPolicyRevision{};
    };
    std::filesystem::path root;std::uint64_t generation{},serial{};
    std::map<EntityId,Request> requests;std::unordered_map<EntityId,State> states;std::map<std::string,Watch> watches;std::unique_ptr<Job> job;
    explicit Impl(std::filesystem::path project):root(std::move(project)){}
    ~Impl(){if(job)job->cancellation.request_stop();}
    void consume(State& state,const std::string& key){if(key.empty()||!state.consumed.insert(key).second)return;state.consumedOrder.push_back(key);
        while(state.consumedOrder.size()>512){state.consumed.erase(state.consumedOrder.front());state.consumedOrder.pop_front();}}
    void enqueue(Request request){request.serial=++serial;const auto entity=request.entity;requests[entity]=std::move(request);states[entity].message="Queued Quader import...";
        if(job&&job->request.entity==entity){job->invalid=true;job->cancellation.request_stop();}}
    void fail(const Request& request,const std::string& message){auto& state=states[request.entity];state.message=message;
        if(state.failedKey!=request.key){state.failedKey=request.key;state.failures=0;}++state.failures;
        state.retryAt=Clock::now()+std::chrono::milliseconds(500*(1u<<std::min(state.failures,3u)));}
    bool valid(const Request& request,std::span<const QuaderTarget> targets)const{
        if(request.generation!=generation)return false;const auto target=std::find_if(targets.begin(),targets.end(),[&](const auto& value){return value.entity==request.entity;});
        return target!=targets.end()&&(request.manual?(request.baseline&&sameConfiguration(target->link,*request.baseline)):
            (target->hasImported&&sameConfiguration(target->link,request.link)));
    }
    void start(){if(job||requests.empty())return;auto found=requests.begin();job=std::make_unique<Job>();job->request=std::move(found->second);requests.erase(found);
        auto request=job->request;const auto token=job->cancellation.get_token();states[request.entity].message="Reading Quader scene...";
        job->reading=std::async(std::launch::async,[request,token]{if(token.stop_requested())throw std::runtime_error("Quader import canceled.");auto source=readQuaderScene(pathFromUtf8(request.link.projectPath),request.link.sceneId);
            if(!request.link.projectId.empty()&&request.link.projectId!=source->project.id)throw std::runtime_error("The Quader project identity changed. Select the project again.");
            if(token.stop_requested())throw std::runtime_error("Quader import canceled.");return source;});}
    QuaderPrepareOptions options(const Job& current,const QuaderBridgeHooks& hooks){
        QuaderPrepareOptions options;options.projectRoot=root;options.scale=current.request.link.importScale;options.refreshMaterials=current.request.link.refreshMaterials;options.syncLights=current.request.link.syncLights;options.completed=current.request.completed;
        const auto reserved=reservedModelImportIds(root);options.reservedIds.insert(reserved.begin(),reserved.end());
        for(const auto& identity:current.source->materials){const auto id=quaderMaterialAssetId(current.source->project.id,current.source->id,identity);
            const auto replacement=hooks.resolveMaterial?hooks.resolveMaterial(id):id;options.materialOverrides.emplace(id,replacement);
            if(hooks.reservedAsset&&hooks.reservedAsset(id))options.reservedIds.insert(id);
            const auto importedId=availableQuaderAssetId(id,options.reservedIds);
            const auto published=canonicalAssetId(importedId);
            if(hooks.dirtyMaterial&&hooks.dirtyMaterial(published))options.protectedMaterials.insert(published);
        }
        if(hooks.resolveTexture)for(const auto& id:options.reservedIds)options.imageOverrides.emplace(id,std::array<std::string,2>{hooks.resolveTexture(id,false),hooks.resolveTexture(id,true)});
        return options;
    }
    bool currentCompletion(const Job& current){if(!current.request.completed)return true;
        const auto latest=readQuaderCompletedSave(pathFromUtf8(current.request.link.projectPath),current.source->project.id,current.source->id,true);
        return quaderSaveKey(latest,current.request.link.importScale,current.request.link.refreshMaterials,current.request.link.syncLights)==current.request.key;}
    bool currentMaterialInputs(const QuaderImportResult& result)const{
        for(const auto& input:result.materialInputs){const auto data=previousFile(root,input.id);
            const auto fingerprint=data?std::optional{modelImportDataDigest({reinterpret_cast<const std::uint8_t*>(data->data()),data->size()})}:std::nullopt;
            if(fingerprint!=input.digest)return false;}
        return true;
    }
    void pump(std::span<const QuaderTarget> targets,const QuaderBridgeHooks& hooks,bool suspended){
        if(!job)return;auto& current=*job;
        if(!valid(current.request,targets)&&!current.invalid){current.invalid=true;current.cancellation.request_stop();states[current.request.entity].message="Quader import canceled because its settings changed.";}
        try{
            if(current.invalid&&!current.reading.valid()&&!current.preparing.valid()){job.reset();return;}
            if(ready(current.reading)){current.source=current.reading.get();if(current.invalid){job.reset();return;}
                if(current.request.manual){try{auto save=readQuaderCompletedSave(pathFromUtf8(current.request.link.projectPath),current.source->project.id,current.source->id,true);
                    if(save.scene&&save.scene->fingerprint==current.source->fingerprint&&save.scene->relativePath==current.source->relativePath)current.manualBaseline=std::move(save);
                }catch(const std::exception&){/* Explicit import remains valid without completion records. */}}
            }
            if(current.source&&!current.preparing.valid()&&!current.result&&!current.invalid){
                if(!current.blockedMaterials.empty()&&hooks.dirtyMaterial&&std::any_of(current.blockedMaterials.begin(),current.blockedMaterials.end(),[&](const auto& id){return hooks.dirtyMaterial(id);}))return;
                auto settings=options(current,hooks);
                if(settings.refreshMaterials&&!settings.protectedMaterials.empty()){current.blockedMaterials.assign(settings.protectedMaterials.begin(),settings.protectedMaterials.end());states[current.request.entity].message="Material refresh waits for the open material to be saved or closed.";return;}
                current.assetPolicyRevision=hooks.assetPolicyRevision?hooks.assetPolicyRevision():0;
                current.blockedMaterials.clear();const auto source=current.source;const auto token=current.cancellation.get_token();states[current.request.entity].message="Preparing Quader meshes, collisions and materials...";
                current.preparing=std::async(std::launch::async,[source,settings=std::move(settings),token]{return prepareQuaderImport(*source,settings,token);});}
            if(ready(current.preparing)){current.result=current.preparing.get();if(current.invalid){job.reset();return;}
                if(current.manualBaseline)current.result->sourceKey=quaderSaveKey(*current.manualBaseline,current.request.link.importScale,current.request.link.refreshMaterials,current.request.link.syncLights);}
            if(!current.result||suspended)return;if(current.invalid){job.reset();return;}
            if(!currentCompletion(current)){states[current.request.entity].message="A newer Quader save superseded this import.";job.reset();return;}
            if(hooks.dirtyMaterial)for(const auto& file:current.result->files)if(file.id.ends_with(".crimson-material")&&hooks.dirtyMaterial(canonicalAssetId(file.id))){
                states[current.request.entity].message="Material refresh waits for the open material to be saved or closed.";return;}
            if((hooks.assetPolicyRevision&&hooks.assetPolicyRevision()!=current.assetPolicyRevision)||!currentMaterialInputs(*current.result)){
                current.result.reset();current.blockedMaterials.clear();states[current.request.entity].message="Crimson material assets changed; preparing the current versions...";return;}
            if(!hooks.applyImport)throw std::runtime_error("No Quader scene publication owner is available.");
            std::vector<AssetFileChange> rollback;rollback.reserve(current.result->files.size()+1);for(const auto& file:current.result->files)rollback.push_back({file.id,previousFile(root,file.id)});
            rollback.push_back({".crimson/asset-locations.json",previousFile(root,".crimson/asset-locations.json")});
            auto applied=current.request.link;applied.projectId=current.source->project.id;applied.sceneId=current.source->id;
            if(!current.request.manual)if(const auto target=std::find_if(targets.begin(),targets.end(),[&](const auto& value){return value.entity==current.request.entity;});target!=targets.end())applied.autoSync=target->link.autoSync;
            commitAssetTransaction(root,current.result->files);
            try{
                if(contentStorageReady(root)) {
                    std::vector<StoredAssetPath> paths;paths.reserve(current.result->files.size());
                    for(const auto& file:current.result->files)if(file.contents)paths.push_back({file.id,resolveAssetPath(root,file.id)});
                    registerAssetPaths(root,paths);
                }
                if(!hooks.applyImport(current.request.entity,applied,*current.result))throw std::runtime_error("The Quader scene import could not be published.");}
            catch(...){
                try{commitAssetTransaction(root,rollback);}
                catch(...){refreshContentStorage(root);throw;}
                refreshContentStorage(root);throw;
            }
            auto& state=states[current.request.entity];state.message=completedMessage(*current.result);state.deferred=current.result->deferredMaterials;state.failures=0;state.failedKey.clear();
            consume(state,current.result->sourceKey);consume(state,current.request.key);
            if(hooks.publishAssets){try{hooks.publishAssets(*current.result);}catch(const std::exception& error){state.message+=" Asset refresh: "+std::string(error.what());}}
            job.reset();
        }catch(const std::exception& error){if(!current.invalid)fail(current.request,error.what());job.reset();}
    }
};
QuaderBridge::QuaderBridge(std::filesystem::path projectRoot) {
    initializeContentStorage(projectRoot);
    impl_=std::make_unique<Impl>(std::move(projectRoot));
}
QuaderBridge::~QuaderBridge()=default;
void QuaderBridge::requestImport(EntityId entity,const QuaderLink& link,std::uint64_t generation){
    if(!entity)return;if(!impl_->generation)impl_->generation=generation;Impl::Request request;request.entity=entity;request.link=link;request.generation=generation;request.manual=true;
    request.key="manual:"+std::to_string(++impl_->serial);impl_->enqueue(std::move(request));}
void QuaderBridge::cancel(EntityId entity){impl_->requests.erase(entity);if(impl_->job&&impl_->job->request.entity==entity){impl_->job->invalid=true;impl_->job->cancellation.request_stop();}impl_->states[entity].message="Quader import canceled.";}
bool QuaderBridge::busy(EntityId entity)const{return impl_->requests.contains(entity)||(impl_->job&&impl_->job->request.entity==entity&&!impl_->job->invalid);}
std::string QuaderBridge::status(EntityId entity)const{const auto found=impl_->states.find(entity);return found==impl_->states.end()?std::string{}:found->second.message;}
void QuaderBridge::update(std::span<const QuaderTarget> targets,std::uint64_t generation,bool suspended,const QuaderBridgeHooks& hooks){
    auto& impl=*impl_;if(impl.generation!=generation){impl.generation=generation;impl.states.clear();impl.watches.clear();std::erase_if(impl.requests,[&](const auto& item){return item.second.generation!=generation;});
        if(impl.job&&impl.job->request.generation!=generation){impl.job->invalid=true;impl.job->cancellation.request_stop();}}
    std::unordered_set<EntityId> active;for(const auto& target:targets)active.insert(target.entity);
    std::erase_if(impl.requests,[&](const auto& item){return !active.contains(item.first);});std::erase_if(impl.states,[&](const auto& item){return !active.contains(item.first)&&(!impl.job||impl.job->request.entity!=item.first);});
    for(auto& [entity,request]:impl.requests)if(request.manual&&!request.baseline){
        const auto target=std::find_if(targets.begin(),targets.end(),[&](const auto& value){return value.entity==entity;});
        if(target!=targets.end())request.baseline=target->link;
    }
    impl.pump(targets,hooks,suspended);if(suspended)return;
    const auto now=Clock::now();std::set<std::string> projectKeys;std::set<std::string> changed;
    for(const auto& target:targets){if(!target.hasImported||target.link.projectPath.empty()||target.link.projectId.empty()||target.link.sceneId.empty())continue;
        const auto key=pathKey(pathFromUtf8(target.link.projectPath));projectKeys.insert(key);auto [found,inserted]=impl.watches.try_emplace(key);auto& watch=found->second;
        if(inserted){watch.watcher=std::make_unique<SaveNotificationWatcher>(pathFromUtf8(target.link.projectPath).parent_path());watch.due=now;watch.fallback=now;}
        if(watch.watcher->consume_changed()){watch.pending=true;watch.due=now+std::chrono::milliseconds{250};}
        if((watch.pending&&now>=watch.due)||now>=watch.fallback){changed.insert(key);watch.pending=false;watch.fallback=now+std::chrono::seconds{2};}}
    std::erase_if(impl.watches,[&](const auto& item){return !projectKeys.contains(item.first);});
    for(const auto& target:targets){auto& state=impl.states[target.entity];if(!state.seeded){state.seeded=true;impl.consume(state,target.consumedSaveKey);}
        if(!target.hasImported||target.link.projectPath.empty()||target.link.projectId.empty()||target.link.sceneId.empty()||busy(target.entity))continue;
        const bool deferred=!state.deferred.empty()&&(!hooks.dirtyMaterial||std::none_of(state.deferred.begin(),state.deferred.end(),[&](const auto& id){return hooks.dirtyMaterial(id);}));
        const auto project=pathKey(pathFromUtf8(target.link.projectPath));if(!deferred&&(!target.link.autoSync||(!changed.contains(project)&&now<state.nextCheck)))continue;
        state.nextCheck=now+std::chrono::seconds{2};
        try{auto save=readQuaderCompletedSave(pathFromUtf8(target.link.projectPath),target.link.projectId,target.link.sceneId,true);const auto key=quaderSaveKey(save,target.link.importScale,target.link.refreshMaterials,target.link.syncLights);
            if(key.empty()){if(state.message.empty())state.message="Save the linked scene in Quader to initialize automatic synchronization.";continue;}
            if(!deferred&&state.consumed.contains(key))continue;if(!deferred&&state.failedKey==key&&(state.failures>=3||now<state.retryAt))continue;
            Impl::Request request;request.entity=target.entity;request.link=target.link;request.generation=generation;request.completed=std::move(save);request.key=key;impl.enqueue(std::move(request));
            if(deferred)state.deferred.clear();
        }catch(const std::exception& error){state.message=error.what();}}
    impl.start();
}
} // namespace crimson
