#pragma once

#include "crimson-renderer/Renderer.h"
#include "ModelAsset.h"
#include "BrowserCatalog.h"
#include "MaterialAsset.h"
#include "ParticleAsset.h"
#include "AssetReferenceManager.h"
#include "resources/ResourceManager.h"
#include "resources/Paths.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct SDL_Window;

namespace crimson {
class SceneDocument;
class AsyncImports;
class TextureStreamer;
class MaterialGraphPrograms;

enum class MaterialSlot { Albedo, Roughness, Metallic, Normal };
struct TextureAsset {
    std::string id; // Project-relative UTF-8 path, e.g. assets/textures/stone.png.
    std::string name;
    uint32_t width{}, height{};
    bool hdr{false}; // Radiance HDR images are environment sources, not material data maps.
    bgfx::TextureHandle thumbnail{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle colorTexture{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle dataTexture{BGFX_INVALID_HANDLE};
    bool colorAttempted{false}, dataAttempted{false};
};
struct PreparedParticleMaterial {
    RenderParticleMaterial render;
    bool ready{false};
    bool current{false}; // A retained last-good packet is usable but not the requested revision.
    std::uint64_t signature{},sourceFingerprint{};
    std::string diagnostic;
};

class AssetLibrary {
public:
    explicit AssetLibrary(std::filesystem::path projectRoot,ResourceManager* resources=nullptr);
    ~AssetLibrary();
    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;

    const std::vector<TextureAsset>& assets() const { return assets_; }
    const std::vector<AssetEntry>& catalog() const { return catalog_; }
    const TextureAsset* find(const std::string& id) const;
    std::shared_ptr<const ModelAsset> findModel(const std::string& id) const;
    std::shared_ptr<const MaterialAsset> findMaterial(const std::string& id) const;
    std::uint64_t materialRevision(const std::string& id) const;
    std::string materialDiagnostic(const std::string& id) const;
    std::shared_ptr<const PreparedParticleMaterial> preparedParticleMaterial(const std::string& id) const;
    ModelMaterialTextures materialPreviewBindings(const MaterialAsset& material) const;
    std::unique_ptr<MaterialGraphPrograms> createMaterialNodePreviewPrograms() const;
    ModelMaterialTextures materialNodePreviewBindings(MaterialGraphPrograms&,const MaterialAsset&,MaterialGraphPreviewTarget) const;
    void publishMaterial(std::shared_ptr<const MaterialAsset>);
    void protectMaterialDocument(std::string id) { materialDocumentId_=std::move(id); }
    void refreshMaterials();
    std::shared_ptr<const MaterialAsset> createMaterial(const std::string& name="New Material");
    std::shared_ptr<const MaterialAsset> copyMaterial(const std::string& id,const std::string& name={});
    void setParticleDocumentReferences(std::vector<std::string> active,std::vector<std::string> history) {
        if(active!=particleDocumentReferences_||history!=particleDocumentHistory_)++catalogRevision_;
        particleDocumentReferences_=std::move(active);particleDocumentHistory_=std::move(history);
    }
    void setMaterialDocumentReferences(std::vector<std::string> active,std::vector<std::string> history) {
        if(active!=materialDocumentReferences_||history!=materialDocumentHistory_)++catalogRevision_;
        materialDocumentReferences_=std::move(active);materialDocumentHistory_=std::move(history);
    }
    const std::filesystem::path& projectRoot() const { return projectRoot_; }
    std::shared_ptr<const ParticleEffectAsset> findParticle(const std::string& id) const;
    std::uint64_t particleRevision(const std::string& id) const;
    std::string particleDiagnostic(const std::string& id) const;
    void publishParticle(std::shared_ptr<const ParticleEffectAsset>);
    void refreshParticles();
    void protectParticleDocument(std::string id) { particleDocumentId_=std::move(id); }
    ResourceManager* resources() const { return resources_; }
    void setMaterialGraphPrograms(MaterialGraphPrograms* programs) { graphPrograms_=programs; }
    std::vector<std::filesystem::path> materialShaderDependencies() const;
    std::uint64_t materialShaderRevision() const;
    std::uint64_t modelRevision(const std::string& id) const;
    std::uint64_t modelFingerprint(const std::string& id) const;
    bool modelLoading(const std::string& id) const;
    std::string modelDiagnostic(const std::string& id) const;
    void updateResources(std::uint32_t bgfxFrame);
    void onFileChanged(const std::filesystem::path& path);
    void noteAssetWrite(const std::string& id) { ownWrites_.push_back(projectRoot_/pathFromUtf8(id)); }
    std::vector<std::filesystem::path> takeOwnWrites() { auto result=std::move(ownWrites_);ownWrites_.clear();return result; }
    bool modelBindingsReady(const std::vector<ModelMaterialTextures>&) const;
    bool modelMaterialsReady(const ModelAsset&,const std::string& materialOverride={}) const;
    std::string modelMaterialsDiagnostic(const ModelAsset&,const std::string& materialOverride={}) const;
    void rebuildModelCache(const std::string& id);
    void setTextureDemand(std::uint32_t edge,bool nativeQuality=false);
    void setThumbnailDemand(std::uint32_t edge) { thumbnailDemand_=edge; }
    std::size_t textureResidentBytes() const;
    void trimTextureCache(std::size_t bytes);
    ModelResolvers modelResolvers() const;
    std::vector<ModelMaterial> resolvedModelMaterials(const ModelAsset& model,const std::string& materialOverride={}) const;
    AssetReferenceManager& references() { return references_; }
    const AssetReferenceManager& references() const { return references_; }
    AssetDeletionPlan planDeletion(const std::vector<std::string>& ids,const SceneDocument* active=nullptr);
    std::vector<AssetEntry> fallbackCandidates(const std::string& id) const;
    bool deleteAssets(const AssetDeletionPlan& plan,const std::unordered_map<std::string,std::string>& replacements={},SceneDocument* active=nullptr);
    bool deletionPending() const;
    bool pumpDeletion(SceneDocument* active);
    void cancelDeletion();
    const TextureAsset* selected() const { return find(selectedId()); }
    const std::string& selectedId() const { return browserSelection_.primary(); }
    void select(const std::string& id) { browserSelection_.set(id); }
    BrowserCatalog& browserCatalog();
    std::string displayName(const std::string& id) const;
    BrowserSelection& browserSelection() { return browserSelection_; }
    const std::string& status() const { return status_; }
    void refreshScenes();
    bool deleteScene(const std::string& id);

    void requestImport(SDL_Window* window, const std::string& destinationFolder = {});
    void processImports();
    const TextureAsset* importTexture(const std::filesystem::path& source);
    std::shared_ptr<const ModelAsset> importModel(const std::filesystem::path& source);
    size_t importFiles(const std::vector<std::filesystem::path>& sources, const std::string& destinationFolder = {});
    std::vector<ModelMaterialTextures> modelBindings(const ModelAsset& model,std::span<const ModelMaterial> materials={});
    std::string loadPreview();
    bool savePreview(const std::string& modelId);
    bool assign(Material& material, MaterialSlot slot, const std::string& id);
    void clear(Material& material, MaterialSlot slot);
    MaterialTextures bindings(const Material& material);
    void loadMaterial(Material& material);
    void saveMaterial(const Material& material);
    bool assignEnvironment(EnvironmentSettings& environment, const std::string& id,bool persistLegacy=true);
    void clearEnvironment(EnvironmentSettings& environment,bool persistLegacy=true);
    void loadEnvironment(EnvironmentSettings& environment);
    void saveEnvironment(const EnvironmentSettings& environment);

private:
    struct PendingDeletion;
    std::shared_ptr<PendingDeletion> pendingDeletion_;
    friend class AsyncImports;
    struct ImportQueue;
    std::shared_ptr<ImportQueue> imports_;
    std::filesystem::path projectRoot_;
    struct ParticleVersion { std::shared_ptr<const ParticleEffectAsset> definition;std::uint64_t revision{};std::string diagnostic; };
    std::unordered_map<std::string,ParticleVersion> particles_;
    std::uint64_t nextParticleRevision_{1};
    std::string particleDocumentId_;
    struct MaterialVersion { std::shared_ptr<const MaterialAsset> definition;std::uint64_t revision{};std::string diagnostic; };
    std::unordered_map<std::string,MaterialVersion> authoredMaterials_;
    mutable std::unordered_map<std::string,std::shared_ptr<const PreparedParticleMaterial>> preparedParticleMaterials_;
    std::uint64_t nextMaterialRevision_{1};
    std::string materialDocumentId_;
    std::vector<std::string> particleDocumentReferences_,particleDocumentHistory_,materialDocumentReferences_,materialDocumentHistory_;
    ResourceManager* resources_{};
    std::unique_ptr<AsyncImports> asyncImports_;
    std::unique_ptr<TextureStreamer> textureStreamer_;
    mutable std::unordered_map<std::string,ResourceHandle<ModelAsset>> modelResources_;
    mutable std::unordered_set<std::string> modelCacheRebuilds_;
    mutable std::unordered_map<std::string,ResourceHandle<MaterialAsset>> materialResources_;
    mutable std::unordered_map<std::string,std::chrono::steady_clock::time_point> modelInterest_,materialInterest_;
    std::unordered_map<std::string,ResourceHandle<ModelAsset>> materialCatalogResources_;
    std::uint32_t textureDemand_{512};
    std::uint32_t thumbnailDemand_{128};
    bool nativeTextures_{};
    std::uint64_t resourcePolicyRevision_{};
    std::unordered_map<std::string,std::uint64_t> registeredModelRevisions_;
    AssetReferenceManager references_;
    BrowserCatalog browserCatalog_;
    BrowserSelection browserSelection_;
    uint64_t catalogRevision_{1}, browserRevision_{};
    mutable std::vector<TextureAsset> assets_;
    std::vector<TextureAsset> retiredTextures_; // Released after the current ImGui frame has submitted.
    std::vector<std::shared_ptr<const ModelAsset>> models_;
    std::vector<std::shared_ptr<const MaterialAsset>> materials_;
    std::vector<AssetEntry> catalog_;
    std::string status_;
    std::vector<std::filesystem::path> ownWrites_;
    TextureAsset* findMutable(const std::string& id) const;
    TextureAsset makeAsset(const std::filesystem::path& path);
    bgfx::TextureHandle fullTexture(TextureAsset& asset, bool srgb) const;
    ModelMaterialTextures materialBindings(const ModelMaterial&) const;
    ModelMaterialTextures graphMaterialBindings(const std::shared_ptr<const MaterialGraph>&) const;
    ModelMaterialTextures preparedGraphBindings(MaterialGraphPrograms&,const MaterialGraphShader&,const MaterialGraph&) const;
    struct GraphShaderCache {std::weak_ptr<const MaterialGraph> source;std::shared_ptr<const MaterialGraphShader> shader;};
    mutable std::unordered_map<const MaterialGraph*,GraphShaderCache> graphShaderCache_;
    MaterialGraphPrograms* graphPrograms_{};
    void registerModel(std::shared_ptr<const ModelAsset> model);
    void addTexture(TextureAsset asset);
    void refreshReferenceIndex(const SceneDocument* active);
};

} // namespace crimson
