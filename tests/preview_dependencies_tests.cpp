#include "editor/AssetPreviews.h"
#include "resources/ThumbnailDemand.h"
#include <iostream>
#include <stdexcept>

namespace {
void require(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
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
        std::cout << "Preview source coherence passed.\n"; return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
