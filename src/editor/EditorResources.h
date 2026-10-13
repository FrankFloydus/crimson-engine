#pragma once
#include "assets/ModelAsset.h"
#include "crimson-renderer/Renderer.h"
#include "resources/ResourceManager.h"
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
class AssetLibrary;
struct PreparedEditorModel {
    std::shared_ptr<const ModelAsset> asset;
    std::vector<ModelMaterial> materials;
    std::vector<ModelMaterialTextures> textures;
    uint64_t revision{},signature{};
    bool ready{};
    bool refined{}; // Requested detail is resident (including pressure policy).
    std::shared_ptr<const void> pinned;
};
// Session-level resource preparation. It schedules CPU payloads, requests
// bounded uploads and publishes coherent immutable render bindings.
class EditorResources final {
public:
    EditorResources(AssetLibrary&,Renderer&,ResourceManager&,std::filesystem::path projectRoot);
    void beginFrame();
    void endFrame();
    std::shared_ptr<const PreparedEditorModel> prepare(const std::string& id,const Mat4& world,const Camera&,
        uint32_t viewportHeight,bool selected,ResourcePriority priority=ResourcePriority::Visible);
    void invalidate(const std::string& id);
    void clear();
    const std::string& status() const { return status_; }
    bool failed(const std::string& id) const;
    void retainExternalModelForFrame(const std::string& id) { externalModels_.push_back(id); }
private:
    struct Entry {
        std::shared_ptr<const PreparedEditorModel> current;
        uint64_t geometryRevision{};
        uint64_t repairRevision{};
        std::string geometryAssetId;
        std::unordered_map<std::string,ResourceHandle<ModelGeometryLevel>> geometry;
        std::chrono::steady_clock::time_point used;
        bool wanted{};
        bool failed{};
        std::string diagnostic;
    };
    AssetLibrary& assets_;
    Renderer& renderer_;
    ResourceManager& resources_;
    std::filesystem::path root_;
    std::unordered_map<std::string,Entry> entries_;
    std::vector<std::shared_ptr<const PreparedEditorModel>> framePins_;
    std::vector<std::string> externalModels_;
    std::string status_;
    void requestGeometry(Entry&,const ModelAsset&,uint64_t revision,uint32_t primitive,uint32_t level,ResourcePriority);
};
} // namespace crimson
