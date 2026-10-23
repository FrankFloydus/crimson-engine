#include "editor/AssetPreviews.h"
#include "editor/AssetPreviewCache.h"
#include "editor/AssetSourceSnapshot.h"
#include "quader_bundle_fixture.h"
#include "resources/ThumbnailDemand.h"
#include "resources/ResourceManager.h"
#include <array>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void fairRequestScheduling() {
    using namespace crimson;
    const std::vector<std::string> visible{"expensive-pending","material-b","material-c","material-d"};
    PreviewRequestSchedule schedule;
    std::vector<std::string> visited;
    // Every request can consume the entire frame budget while still waiting
    // for shader/image preparation. All cards must nevertheless make progress.
    for(std::size_t frame=0;frame<visible.size();++frame)
        schedule.run(visible,[&](const auto& id){visited.push_back(id);},[]{return false;});
    require(visited==visible,"A pending thumbnail starved other visible material previews.");
    visited.clear();
    schedule.run(visible,[&](const auto& id){visited.push_back(id);},[]{return true;});
    require(visited==visible,"A warm thumbnail batch repeated or skipped a visible card.");
    schedule.next=3;
    const std::vector<std::string> scrolled{"new-material","last-material"};
    visited.clear();
    for(std::size_t frame=0;frame<scrolled.size();++frame)
        schedule.run(scrolled,[&](const auto& id){visited.push_back(id);},[]{return false;});
    std::sort(visited.begin(),visited.end());auto expected=scrolled;std::sort(expected.begin(),expected.end());
    require(visited==expected,"Changing browser demand left material previews unreachable.");
    schedule.run({},[](const auto&){},[]{return true;});
    require(schedule.next==0,"An empty preview demand retained an invalid scheduling cursor.");
}
template<class Function> void rejects(Function&& action,const char* message) {
    bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,message);
}
void deletedSourceObservers() {
    using namespace crimson;
    namespace fs=std::filesystem;
    const auto root=fs::temp_directory_path()/("crimson-observer-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto cleanup=[&] {
        std::error_code error;
        const auto absolute=fs::weakly_canonical(root,error),temporary=fs::weakly_canonical(fs::temp_directory_path(),error);
        if(!error && absolute.parent_path()==temporary && absolute.filename().string().starts_with("crimson-observer-"))fs::remove_all(absolute,error);
    };
    try {
        const auto fixture=bundle_test::writeFixture(root);
        const auto textureId=newAssetId(),materialId=newAssetId(),survivorId=newAssetId();
        const auto texture=root/"Content/Quader.png",material=root/"Content/Quader.crimson-material",survivor=root/"Content/Keep.crimson-material";
        writeFileAtomically(texture,"image bytes");writeFileAtomically(material,"{}");writeFileAtomically(survivor,"{}");
        const std::array<StoredAssetPath,3> added{{{textureId,texture},{materialId,material},{survivorId,survivor}}};
        registerAssetPaths(root,added);
        const std::vector<AssetEntry> initial{{fixture.modelId,"Quader mesh",AssetKind::Model},{textureId,"Texture",AssetKind::Texture},
            {materialId,"Material",AssetKind::Material},{survivorId,"Keep",AssetKind::Material}};
        AssetCatalogSnapshot catalog;
        require(catalog.update(root,initial).added.size()==4,"The source observer failed to capture the initial catalog.");
        const AssetSourceSnapshot oldSources(root);
        std::vector<StoredAssetPath> dependencies{{fixture.bundle.id,fixture.file},{materialId,material},{textureId,texture}};
        std::vector<std::string> removedIds{materialId,textureId};
        for(const auto& id:fixture.bundle.models.front().memberIds) {
            dependencies.push_back({id,resolveAssetPath(root,id)});removedIds.push_back(id);
        }
        for(const auto& dependency:dependencies)require(oldSources.matches(dependency.id,dependency.file),"A current bundle dependency was rejected.");
        const auto modelFile=resolveAssetPath(root,fixture.modelId);
        auto emptyBundle=fixture.bundle;emptyBundle.models.clear();
        writeFileAtomically(fixture.file,encodeQuaderSceneBundle(emptyBundle));
        const auto index=prepareAssetPathRegistration(root,{},removedIds);
        for(const auto& dependency:dependencies)if(dependency.id!=fixture.bundle.id)fs::remove(dependency.file);
        writeFileAtomically(root/".crimson/asset-locations.json",index);refreshContentStorage(root);
        rejects([&]{(void)resolveAssetPath(root,fixture.modelId);},"The regression fixture did not unregister the deleted model identity.");
        const std::vector<AssetEntry> surviving{{survivorId,"Keep",AssetKind::Material}};
        const auto changes=catalog.update(root,surviving);
        require(changes.added.empty() && changes.removed.size()==3,"Final deletion did not evict exactly the removed catalog assets.");
        const auto removedModel=std::find_if(changes.removed.begin(),changes.removed.end(),[&](const auto& source){return source.id==fixture.modelId;});
        require(removedModel!=changes.removed.end() && removedModel->kind==AssetKind::Model && removedModel->file==modelFile,
            "A removed model was resolved after unregister instead of retaining its literal source path.");
        for(const auto& source:changes.removed)require(source.file==oldSources.find(source.id),"A deletion acknowledgement lost its predelete path.");
        const AssetSourceSnapshot afterDelete(root);
        for(const auto& dependency:dependencies)require(afterDelete.matches(dependency.id,dependency.file)==(dependency.id==fixture.bundle.id),
            "A retained thumbnail did not invalidate a removed model/member/material/texture dependency.");
        require(afterDelete.matches(survivorId,survivor),"Deleting bundled sources invalidated an unrelated surviving source.");
        require(catalog.update(root,surviving).removed.empty(),"A later frame repeated catalog removals.");
        const auto returned=root/"Content/Returned.crimson-material",moved=root/"Content/Moved.crimson-material";
        writeFileAtomically(returned,"{}");registerAssetPath(root,materialId,returned);
        const std::vector<AssetEntry> restored{{materialId,"Returned",AssetKind::Material},{survivorId,"Keep",AssetKind::Material}};
        const auto returnedChanges=catalog.update(root,restored);
        require(returnedChanges.added.size()==1 && returnedChanges.added.front().file==returned,"Re-registration retained the deleted source path.");
        fs::rename(returned,moved);registerAssetPath(root,materialId,moved);
        const AssetSourceSnapshot relocated(root);
        require(relocated.matches(materialId,moved) && !relocated.matches(materialId,returned),"Relocation did not invalidate the former preview source location.");
        require(catalog.update(root,restored).added.empty(),"Relocation changed the stable catalog identity.");
        auto invalid=restored;invalid.push_back({newAssetId(),"Invalid live asset",AssetKind::Texture});
        rejects([&]{(void)catalog.update(root,invalid);},"A malformed live catalog identity was silently treated as a deleted source.");
        const auto removedAgain=catalog.update(root,surviving);
        require(removedAgain.removed.size()==1 && removedAgain.removed.front().file==moved,
            "A failed live catalog update or relocation corrupted the source observer's accepted snapshot.");
        cleanup();
    }catch(...){cleanup();throw;}
}
void persistentCache() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-preview-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"Content"/"Props");
    const auto folder=root/".crimson"/"cache"/"previews",source=root/"Content"/"Props"/"model.gltf",shader=root/"preview.sc";
    const auto animation=root/"Content"/"Props"/"walk.crimson-animation",animationSource=root/"Content"/"Props"/"walk-source.gltf";
    const auto write=[](const auto& file,const char* value){std::ofstream output(file,std::ios::binary|std::ios::trunc);output<<value;};
    try {
        write(source,"model source");write(shader,"shader source");write(animation,"saved root lock settings");write(animationSource,"retained animation tracks");
        crimson::CachedAssetPreview preview;preview.signature=42;preview.shaderSignature=101;preview.policyFingerprint=202;preview.edge=32;
        preview.files={source,shader,animation,animationSource};preview.materials={{"surface",303}};preview.animations={{"walk",404}};preview.decals={{"wall-stain",505}};
        preview.locations={{"model",source},{"walk",animation},{"walk-source",animationSource}};
        for(const auto& file:preview.files)preview.sourceFingerprints.push_back(crimson::fingerprintFiles({file}));
        preview.image=std::make_shared<crimson::DecodedImage>(crimson::DecodedImage{32,32,std::vector<std::uint8_t>(32*32*4,127)});
        crimson::writeAssetPreviewCache(root,folder,"model",preview,true);
        const auto read=[&]{return crimson::readAssetPreviewCache(root,folder,"model",32,101,202);};
        auto cached=read();
        require(cached.signature==42 && cached.image->rgba==preview.image->rgba && cached.files==preview.files && cached.materials.front().fingerprint==303,
            "A warm persistent preview did not restore its pixels and source descriptors.");
        require(cached.locations.front().id=="model" && cached.locations.front().file==source,"Persistent previews lost stable source locations.");
        require(cached.animations.size()==1&&cached.animations.front().id=="walk"&&cached.animations.front().fingerprint==404,
            "Persistent scene previews lost their authored Animation versions.");
        require(cached.decals.size()==1&&cached.decals.front().id=="wall-stain"&&cached.decals.front().fingerprint==505,
            "Persistent scene previews lost their authored decal versions.");
        {
            std::ifstream metadata(std::filesystem::path(crimson::assetPreviewImagePath(folder,"model",42).wstring()+L".json"));
            const auto record=nlohmann::json::parse(metadata);
            require(record.at("version")==crimson::AssetPreviewImageVersion,"Preview image writer and warm reader use different versions.");
        }
        const auto animationStamp=std::filesystem::last_write_time(animation),tracksStamp=std::filesystem::last_write_time(animationSource);
        write(animation,"changed root lock settings");std::filesystem::last_write_time(animation,animationStamp+std::chrono::seconds{2});
        rejects(read,"Changed Animation root settings reused a stale scene preview.");
        write(animation,"saved root lock settings");std::filesystem::last_write_time(animation,animationStamp);
        write(animationSource,"different animation tracks");std::filesystem::last_write_time(animationSource,tracksStamp+std::chrono::seconds{2});
        rejects(read,"Changed retained animation tracks reused a stale scene preview.");
        write(animationSource,"retained animation tracks");std::filesystem::last_write_time(animationSource,tracksStamp);
        require(read().animations.front().fingerprint==404,"Restoring Animation dependencies did not restore the valid cache.");
        auto animationless=preview;animationless.animations.clear();
        crimson::writeAssetPreviewCache(root,folder,"no-animation",animationless,true);
        const auto noAnimationIndex=crimson::assetPreviewIndexPath(folder,"no-animation",32);
        {
            std::ifstream input(noAnimationIndex);auto record=nlohmann::json::parse(input);record.erase("animations");input.close();
            std::ofstream output(noAnimationIndex,std::ios::binary|std::ios::trunc);output<<record.dump();
        }
        require(crimson::readAssetPreviewCache(root,folder,"no-animation",32,101,202).animations.empty(),
            "An animationless descriptor required the optional Animation version list.");
        auto staleRender=preview;staleRender.sourceFingerprints={1,2};
        rejects([&]{crimson::writeAssetPreviewCache(root,folder,"stale-render",staleRender,true);},"A source version changed after rendering was advertised as a current preview.");
        const auto stamp=std::filesystem::last_write_time(source);
        std::filesystem::last_write_time(source,stamp+std::chrono::seconds{2});
        require(read().signature==42,"An unchanged source with a new timestamp needlessly invalidated its PNG.");
        write(source,"changed data");std::filesystem::last_write_time(source,stamp+std::chrono::seconds{4});
        rejects(read,"A changed model source reused a stale persistent preview.");
        auto missingRenderVersions=preview;missingRenderVersions.sourceFingerprints.clear();
        rejects([&]{crimson::writeAssetPreviewCache(root,folder,"missing-render-versions",missingRenderVersions,true);},
            "Restored old pixels certified current source bytes without their render-time versions.");
        require(!std::filesystem::exists(crimson::assetPreviewIndexPath(folder,"missing-render-versions",32)),
            "An unverified restored PNG published a usable persistent descriptor.");
        write(source,"model source");std::filesystem::last_write_time(source,stamp);
        rejects([&]{(void)crimson::readAssetPreviewCache(root,folder,"model",32,102,202);},"Changed renderer shaders reused a stale preview.");
        rejects([&]{(void)crimson::readAssetPreviewCache(root,folder,"model",32,101,203);},"Changed reference policy reused a stale preview.");
        rejects([&]{(void)crimson::readAssetPreviewCache(root,folder,"model",64,101,202);},"A different card size reused an incompatible preview.");
        std::stop_source cancellation;cancellation.request_stop();
        rejects([&]{(void)crimson::readAssetPreviewCache(root,folder,"model",32,101,202,cancellation.get_token());},"Canceled preview disk work continued.");
        crimson::writeAssetPreviewCache(root,folder,"unsaved",preview,false);
        require(std::filesystem::is_regular_file(crimson::assetPreviewImagePath(folder,"unsaved",42)) &&
            !std::filesystem::exists(crimson::assetPreviewIndexPath(folder,"unsaved",32)),"An unsaved preview was advertised as a saved warm-start asset.");
        write(crimson::assetPreviewImagePath(folder,"model",42),"corrupted PNG");
        rejects(read,"A corrupted derived PNG was accepted.");
        std::filesystem::remove_all(root);
    }catch(...){std::filesystem::remove_all(root);throw;}
}
}
int main() {
    try {
        crimson::PreviewDependencies sources;
        sources.files={"assets/textures/albedo.png","assets/textures/normal.png"};
        sources.sourceFingerprints={11,22};
        require(sources.sourceMatches("assets/textures/albedo.png",11),"Current texture source was rejected.");
        require(sources.sourceMatches("assets/textures/folder/../normal.png",22),"Normalized source identity was rejected.");
        require(!sources.sourceMatches("assets/textures/albedo.png",10),"Old GPU pixels matched a newly hashed source.");
        require(!sources.sourceMatches("assets/textures/missing.png",11),"Missing source matched another image's fingerprint.");
        require(!sources.sourceMatches("assets/textures/albedo.png",0),"An unpublished GPU source was accepted.");
        sources.sourceFingerprints.pop_back();
        require(!sources.sourceMatches("assets/textures/albedo.png",11),"Incomplete source metadata was accepted.");
        crimson::PreviewCacheState cache;
        require(cache.needsDiskRead(31),"A cold preview did not check its persistent PNG.");
        require(!cache.needsDiskRead(31),"A pending/missing PNG scheduled duplicate reads.");
        cache.published=31;
        require(cache.needsDiskRead(31),"A GPU-evicted preview failed to retry its completed PNG.");
        require(!cache.needsDiskRead(31),"An eviction retry scheduled duplicate reads.");
        require(cache.needsDiskRead(32),"A changed source did not check its own PNG signature.");
        require(crimson::thumbnailPixelEdge(128,1)==128,"Default cards should not allocate 512-pixel thumbnails.");
        require(crimson::thumbnailPixelEdge(128,1.5f)==192,"Thumbnail demand ignored viewport DPI.");
        require(crimson::thumbnailPixelEdge(512,2)==512,"A 512-pixel preference must remain supported within renderer limits.");
        crimson::PreviewRetryState retry;
        const auto now=crimson::PreviewRetryState::Clock::time_point{};
        require(retry.ready(now),"A new thumbnail should be immediately eligible.");
        retry.failed(now);
        require(!retry.ready(now+std::chrono::milliseconds{999}) && retry.ready(now+std::chrono::seconds{1}),"Failed thumbnails must back off instead of retrying each frame or failing forever.");
        retry.failed(now+std::chrono::seconds{1});
        require(!retry.ready(now+std::chrono::seconds{2}) && retry.ready(now+std::chrono::seconds{3}),"Thumbnail retry delay must increase after repeated failures.");
        for(unsigned i=0;i<8;++i)retry.failed(now);
        require(!retry.ready(now+std::chrono::seconds{29}) && retry.ready(now+std::chrono::seconds{30}),"Thumbnail retry delay must remain bounded.");
        retry.reset();require(retry.ready(now),"A source edit or successful publication must reset retry backoff.");
        fairRequestScheduling();persistentCache();deletedSourceObservers();
        std::cout << "Preview source coherence passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
