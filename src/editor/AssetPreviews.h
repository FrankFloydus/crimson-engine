#pragma once
#include <algorithm>
#include <chrono>
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
    std::optional<uint64_t> sourceFingerprint(const std::filesystem::path& source) const {
        if (files.size()!=sourceFingerprints.size()) return std::nullopt;
        const auto normalized=source.lexically_normal();
        for (size_t i=0;i<files.size();++i)
            if (files[i].lexically_normal()==normalized) return sourceFingerprints[i];
        return std::nullopt;
    }
    bool sourceMatches(const std::filesystem::path& source,uint64_t gpuFingerprint) const {
        const auto expected=sourceFingerprint(source);
        return gpuFingerprint && expected && *expected==gpuFingerprint;
    }
};
struct AssetPreviewStats {
    std::uint64_t requests{},validations{},residentHits{},diskRequests{},diskHits{},renderRequests{},invalidations{};
};
struct PreviewValidationKey {
    std::uint64_t catalog{},policy{},rendererShader{},materialShader{};
    std::uint32_t edge{};
    bool operator==(const PreviewValidationKey&) const = default;
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
    AssetPreviewStats stats() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace crimson
