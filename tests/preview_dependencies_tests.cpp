#include "editor/AssetPreviews.h"
#include "editor/AssetPreviewCache.h"
#include "resources/ThumbnailDemand.h"
#include "resources/ResourceManager.h"
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class Function> void rejects(Function&& action,const char* message) {
    bool failed=false;try{action();}catch(const std::exception&){failed=true;}require(failed,message);
}
void persistentCache() {
    const auto root=std::filesystem::temp_directory_path()/("crimson-preview-cache-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root/"Content"/"Props");
    const auto folder=root/".crimson"/"cache"/"previews",source=root/"Content"/"Props"/"model.gltf",shader=root/"preview.sc";
    const auto write=[](const auto& file,const char* value){std::ofstream output(file,std::ios::binary|std::ios::trunc);output<<value;};
    try {
        write(source,"model source");write(shader,"shader source");
        crimson::CachedAssetPreview preview;preview.signature=42;preview.shaderSignature=101;preview.policyFingerprint=202;preview.edge=32;
        preview.files={source,shader};preview.materials={{"surface",303}};preview.locations={{"model",source}};
        preview.sourceFingerprints={crimson::fingerprintFiles({source}),crimson::fingerprintFiles({shader})};
        preview.image=std::make_shared<crimson::DecodedImage>(crimson::DecodedImage{32,32,std::vector<std::uint8_t>(32*32*4,127)});
        crimson::writeAssetPreviewCache(root,folder,"model",preview,true);
        const auto read=[&]{return crimson::readAssetPreviewCache(root,folder,"model",32,101,202);};
        auto cached=read();
        require(cached.signature==42 && cached.image->rgba==preview.image->rgba && cached.files==preview.files && cached.materials.front().fingerprint==303,
            "A warm persistent preview did not restore its pixels and source descriptors.");
        require(cached.locations.front().id=="model" && cached.locations.front().file==source,"Persistent previews lost stable source locations.");
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
        persistentCache();
        std::cout << "Preview source coherence passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
