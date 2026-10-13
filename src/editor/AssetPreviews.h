#pragma once
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
class Renderer;
class EditorResources;
struct PreviewDependencies {
    uint64_t fingerprint{};
    std::vector<std::filesystem::path> files;
    std::vector<uint64_t> sourceFingerprints;
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
// Derived thumbnail requests use isolated renderer targets/environment. Pump
// before ImGui::NewFrame, using the previous frame's visible browser identities.
class AssetPreviews final {
public:
    AssetPreviews(std::filesystem::path projectRoot,AssetLibrary&,ResourceManager&,Renderer&,EditorResources&);
    ~AssetPreviews();
    AssetPreviews(const AssetPreviews&)=delete;
    AssetPreviews& operator=(const AssetPreviews&)=delete;
    void pump(uint32_t bgfxFrame,std::span<const std::string> visibleIds);
    void requestSceneSave(const std::string& id);
    void invalidate(const std::string& id);
    const std::string& status() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace crimson
