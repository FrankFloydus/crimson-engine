#pragma once
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace crimson {
class AssetLibrary;
class ResourceManager;
namespace shared { class Renderer; } using Renderer=shared::Renderer;
class EditorResources;
struct CachedAssetPreview;
struct PreviewDependencies {
    uint64_t fingerprint{};
    std::vector<std::filesystem::path> files;
    std::vector<uint64_t> sourceFingerprints;
    std::shared_ptr<const CachedAssetPreview> cachedPreview;
    std::optional<uint64_t> sourceFingerprint(const std::filesystem::path& source) const;
    bool sourceMatches(const std::filesystem::path& source,uint64_t gpuFingerprint) const {
        const auto expected=sourceFingerprint(source);
        return gpuFingerprint && expected && *expected==gpuFingerprint;
    }
};
struct AssetPreviewStats {
    std::uint64_t requests{},validations{},residentHits{},diskRequests{},diskHits{},renderRequests{},invalidations{};
    std::uint64_t sourcePlanBuilds{},sourcePlanHits{};
    std::uint64_t decalPreparations{},decalPreparationFailures{};
};
struct PreviewValidationKey {
    std::uint64_t catalog{},policy{},rendererShader{},materialShader{};
    std::uint32_t edge{};
    bool operator==(const PreviewValidationKey&) const = default;
};
// Dependency descriptors are immutable for an exact signature and source
// location generation. Geometry/shader readiness retries retain that publication.
struct PreviewDescriptorState {
    std::uint64_t signature{},sourceLocations{};
    PreviewValidationKey validation;
    bool valid{};
    bool matches(std::uint64_t expected,const PreviewValidationKey& key,std::uint64_t locations) const {
        return valid&&signature==expected&&validation==key&&sourceLocations==locations;
    }
    void publish(std::uint64_t value,const PreviewValidationKey& key,std::uint64_t locations) {
        signature=value;validation=key;sourceLocations=locations;valid=true;
    }
    void invalidate(){valid=false;}
};
struct PreviewCacheState {
    std::uint64_t checked{},published{};
    bool needsDiskRead(std::uint64_t signature) {
        // A previously published image can be evicted while this CPU entry is
        // alive. Its completed PNG is still reusable on the next visible visit.
        if (published==signature) { checked=0;published=0; }
        if (checked==signature) return false;
        checked=signature;return true;
    }
};
struct PreviewRetryState {
    using Clock=std::chrono::steady_clock;
    Clock::time_point next{};
    unsigned failures{};
    bool ready(Clock::time_point now) const { return now>=next; }
    void failed(Clock::time_point now) {
        const auto exponent=std::min(failures,5u);
        if(failures<5)++failures;
        next=now+std::chrono::seconds{std::min(1u<<exponent,30u)};
    }
    void reset() { next={};failures=0; }
};
// Requests can remain pending while their images or shaders prepare. Resume
// after the last attempted card so one expensive pending asset cannot consume
// every frame's preparation budget ahead of the other visible cards.
struct PreviewRequestSchedule {
    std::size_t next{};
    template<class Request,class BudgetAvailable>
    void run(std::span<const std::string> ids,Request&& request,BudgetAvailable&& budgetAvailable) {
        if(ids.empty()) { next=0;return; }
        const auto first=next%ids.size();
        for(std::size_t attempted=0;attempted<ids.size();++attempted) {
            const auto index=(first+attempted)%ids.size();
            request(ids[index]);
            next=(index+1)%ids.size();
            if(!budgetAvailable())break;
        }
    }
};
// A retained scene plan may outlive its model metadata interests while image
// or shader work is pending. Resume cold metadata demand within the same card
// instead of hydrating its entire receiver population in one UI frame.
struct PreviewSceneModelDemand {
    std::size_t next{};
    template<class Request,class BudgetAvailable>
    bool refresh(std::size_t count,Request&& request,BudgetAvailable&& budgetAvailable) {
        if(next>=count)next=0;
        while(next<count) {
            if(!budgetAvailable())return false;
            request(next);++next;
        }
        next=0;return true;
    }
};
// Derived thumbnail requests use isolated renderer targets/environment. Pump
// before ImGui::NewFrame, using the previous frame's visible browser identities.
class AssetPreviews final {
public:
    AssetPreviews(std::filesystem::path projectRoot,AssetLibrary&,ResourceManager&,Renderer&,EditorResources&);
    ~AssetPreviews();
    AssetPreviews(const AssetPreviews&)=delete;
    AssetPreviews& operator=(const AssetPreviews&)=delete;
    void pump(uint32_t bgfxFrame,std::span<const std::string> visibleIds,std::uint32_t pixelEdge=128);
    void requestSceneSave(const std::string& id);
    void invalidate(const std::string& id);
    void invalidateFile(const std::filesystem::path& file);
    const std::string& status() const;
    // Read-only preparation stage/error for diagnostics; no UI or retry changes.
    std::string diagnostic(const std::string& id) const;
    AssetPreviewStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace crimson
