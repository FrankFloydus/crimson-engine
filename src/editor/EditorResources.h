#pragma once
#include "assets/ModelAsset.h"
#include "assets/ModelMaterialOverrides.h"
#include "crimson-renderer/Renderer.h"
#include "resources/ResourceManager.h"
#include <chrono>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace crimson {
class AssetLibrary;
struct ExtractedScene;
struct ExtractedModel;
struct ModelPose;
struct PreparedEditorModel {
    std::shared_ptr<const ModelAsset> asset;
    std::vector<ModelMaterial> materials;
    std::vector<ModelMaterialTextures> textures;
    uint64_t revision{},signature{};
    bool ready{};
    bool refined{}; // Requested detail is resident (including pressure policy).
    std::shared_ptr<const void> pinned;
};
// One world-wide caster set shared by Scene and Game. Its immutable packets
// keep alpha textures and model versions alive through queued shadow draws.
struct PreparedEditorShadows {
    std::vector<RenderModelInstance> instances;
    std::vector<std::shared_ptr<const PreparedEditorModel>> models;
    std::vector<Aabb> receiverBounds;
    bool ready{true};
    bool refined{true};
};
// Session-level resource preparation. It schedules CPU payloads, requests
// bounded uploads and publishes coherent immutable render bindings.
class EditorResources final {
public:
    EditorResources(AssetLibrary&,Renderer&,ResourceManager&,std::filesystem::path projectRoot);
    void beginFrame();
    void setMaterialContext(MaterialEvaluationContext context){materialContext_=context;}
    std::shared_ptr<const ModelPose> pose(const ExtractedModel&,const std::shared_ptr<const ModelAsset>&);
    Aabb modelBounds(const ExtractedModel&,const std::shared_ptr<const ModelAsset>&);
    void endFrame();
    std::shared_ptr<const PreparedEditorModel> prepare(const std::string& id,const Mat4& world,const Camera&,
        uint32_t viewportHeight,bool selected,ResourcePriority priority=ResourcePriority::Visible,float viewportAspect=0,
        std::span<const AssetRef<MaterialAsset>> materialOverrides={},float shadowTexelWorld=0,const ExtractedModel* presentation=nullptr);
    std::shared_ptr<const PreparedEditorShadows> prepareShadows(const ExtractedScene&,std::uint16_t resolution,
        ResourcePriority priority=ResourcePriority::Visible,std::span<const ShadowCamera> cameras={},std::span<const std::uint64_t> focusEntities={},std::optional<Vec3> priorityOrigin={},std::span<const Aabb> extraReceiverBounds={});
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
        std::string sourceId;
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
    struct PoseEntry {
        std::weak_ptr<const ModelAsset> source;
        std::shared_ptr<const ModelPose> value;
        std::int32_t clip{-1};
        double seconds{};
        bool loop{},wanted{};
    };
    std::unordered_map<std::uint64_t,PoseEntry> poses_;
    std::vector<std::shared_ptr<const PreparedEditorModel>> framePins_;
    std::vector<std::string> externalModels_;
    std::string status_;
    MaterialEvaluationContext materialContext_{};
    void requestGeometry(Entry&,const ModelAsset&,uint64_t revision,uint32_t primitive,uint32_t level,ResourcePriority);
};
} // namespace crimson
