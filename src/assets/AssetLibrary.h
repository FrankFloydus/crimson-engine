#pragma once

#include "crimson-renderer/Renderer.h"
#include "ModelAsset.h"
#include "ModelMaterialOverrides.h"
#include "BrowserCatalog.h"
#include "ContentStorage.h"
#include "resources/AssetId.h"
#include "MaterialAsset.h"
#include "ParticleAsset.h"
#include "NavigationAgentProfile.h"
#include "GameplayEffectAsset.h"
#include "AnimationAsset.h"
#include "AssetReferenceManager.h"
#include "resources/ResourceManager.h"
#include "resources/Paths.h"
#include "scene/PrimitiveMesh.h"
#include <filesystem>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>

struct SDL_Window;

namespace crimson {
class SceneDocument;
class AsyncImports;
class TextureStreamer;
struct PreparedTexture;
class MaterialGraphPrograms;
struct MaterialGraphResizeBinding;

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
    // Material preparation can be nested (world, thumbnail, material preview).
    // Each view admits SSR only for its own effective Environment settings.
    class ReflectionDemandScope {
    public:
        ReflectionDemandScope(const AssetLibrary& owner,bool enabled):owner_(owner),previous_(owner.reflectionDemand_) {owner_.reflectionDemand_=enabled;}
        ~ReflectionDemandScope(){owner_.reflectionDemand_=previous_;}
        ReflectionDemandScope(const ReflectionDemandScope&)=delete;
        ReflectionDemandScope& operator=(const ReflectionDemandScope&)=delete;
    private:
        const AssetLibrary& owner_;
        bool previous_{};
    };
    explicit AssetLibrary(std::filesystem::path projectRoot,ResourceManager* resources=nullptr);
    ~AssetLibrary();
    AssetLibrary(const AssetLibrary&) = delete;
    AssetLibrary& operator=(const AssetLibrary&) = delete;

    const std::vector<TextureAsset>& assets() const { return assets_; }
    const std::vector<AssetEntry>& catalog() const { return catalog_; }
    std::uint64_t catalogRevision() const { return catalogRevision_; }
    const TextureAsset* find(const std::string& id) const;
    std::shared_ptr<const ModelAsset> findModel(const std::string& id,const std::optional<PrimitiveMeshSettings>& primitive={}) const;
    std::shared_ptr<const MaterialAsset> findMaterial(const std::string& id) const;
    std::uint64_t materialRevision(const std::string& id) const;
    std::string materialDiagnostic(const std::string& id) const;
    std::shared_ptr<const PreparedParticleMaterial> preparedParticleMaterial(const std::string& id) const;
    ModelMaterialTextures materialPreviewBindings(const MaterialAsset& material,std::uint32_t textureEdge=512) const;
    // Numeric Value views reuse the canonical data image; RGB views encode its
    // linear color for display. The transient lease never creates an asset.
    PreparedTexture grungePreview(const MaterialGrungeNode&,std::uint32_t edge=1024,
        MaterialValueType type=MaterialValueType::Scalar) const;
    PreparedTexture materialNodeImagePreview(const std::string& documentId,const std::shared_ptr<const MaterialGraph>&,
        MaterialGraphPreviewTarget,unsigned edge=128) const;
    void releaseMaterialNodeImagePreviews(const std::string& documentId) const;
    std::unique_ptr<MaterialGraphPrograms> createMaterialNodePreviewPrograms() const;
    ModelMaterialTextures materialNodePreviewBindings(MaterialGraphPrograms&,const MaterialAsset&,MaterialGraphPreviewTarget,std::uint32_t textureEdge=512) const;
    void publishMaterial(std::shared_ptr<const MaterialAsset>);
    void publishManagedModels(std::span<const std::shared_ptr<const ModelAsset>> models);
    // Retire removed source meshes from pickers immediately; reclaim their
    // payload only when saved scenes, this document and its history release it.
    void collectUnusedQuaderModels(const SceneDocument& active);
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
    std::shared_ptr<const NavigationAgentProfile> findNavigationAgentProfile(const std::string& id) const;
    std::uint64_t navigationAgentProfileRevision(const std::string& id) const;
    std::string navigationAgentProfileDiagnostic(const std::string& id) const;
    void publishNavigationAgentProfile(std::shared_ptr<const NavigationAgentProfile>);
    void refreshNavigationAgentProfiles();
    std::shared_ptr<const NavigationAgentProfile> createNavigationAgentProfile(const std::string& name="New Navigation Agent Profile");
    std::shared_ptr<const NavigationAgentProfile> copyNavigationAgentProfile(const std::string& id,const std::string& name={});
    void protectNavigationAgentProfileDocument(std::string id){navigationProfileDocumentId_=std::move(id);}
    std::shared_ptr<const GameplayEffectAsset> findGameplayEffect(const std::string& id) const;
    std::uint64_t gameplayEffectRevision(const std::string& id) const;
    std::string gameplayEffectDiagnostic(const std::string& id) const;
    void publishGameplayEffect(std::shared_ptr<const GameplayEffectAsset>);
    void refreshGameplayEffects();
    std::shared_ptr<const GameplayEffectAsset> createGameplayEffect(const std::string& name="New Gameplay Effect");
    std::shared_ptr<const GameplayEffectAsset> copyGameplayEffect(const std::string& id,const std::string& name={});
    void protectGameplayEffectDocument(std::string id){gameplayEffectDocumentId_=std::move(id);}
    std::shared_ptr<const AnimationAsset> findAnimation(const std::string& id) const;
    std::uint64_t animationRevision(const std::string& id) const;
    std::string animationDiagnostic(const std::string& id) const;
    void publishAnimation(std::shared_ptr<const AnimationAsset>);
    void refreshAnimations();
    std::shared_ptr<const AnimationAsset> copyAnimation(const std::string& id,const std::string& name={});
    void protectAnimationDocument(std::string id){animationDocumentId_=std::move(id);}
    void setAnimationDocumentReferences(std::vector<std::string> active,std::vector<std::string> history) {
        if(active!=animationDocumentReferences_||history!=animationDocumentHistory_)++catalogRevision_;
        animationDocumentReferences_=std::move(active);animationDocumentHistory_=std::move(history);
    }
    ResourceManager* resources() const { return resources_; }
    void setMaterialGraphPrograms(MaterialGraphPrograms* programs) { graphPrograms_=programs; }
    std::vector<std::filesystem::path> materialShaderDependencies() const;
    std::uint64_t materialShaderRevision() const;
    std::uint64_t modelRevision(const std::string& id) const;
    std::uint64_t modelFingerprint(const std::string& id) const;
    std::vector<std::filesystem::path> modelDependencies(const std::string& id) const;
    std::shared_ptr<const ModelAsset> peekModel(const std::string& id,const std::optional<PrimitiveMeshSettings>& primitive={}) const;
    bool primitiveModelAlive(const std::string& geometryId) const;
    std::shared_ptr<const MaterialAsset> peekMaterial(const std::string& id) const;
    bool modelLoading(const std::string& id) const;
    std::string modelDiagnostic(const std::string& id) const;
    void updateResources(std::uint32_t bgfxFrame);
    void onFileChanged(const std::filesystem::path& path);
    void noteAssetWrite(const std::string& id) { ownWrites_.push_back(resolveAssetPath(projectRoot_,id)); }
    std::vector<std::filesystem::path> takeOwnWrites() { auto result=std::move(ownWrites_);ownWrites_.clear();return result; }
    bool modelBindingsReady(const std::vector<ModelMaterialTextures>&) const;
    bool modelMaterialsReady(const ModelAsset&,std::span<const AssetRef<MaterialAsset>> materialOverrides={}) const;
    std::string modelMaterialsDiagnostic(const ModelAsset&,std::span<const AssetRef<MaterialAsset>> materialOverrides={}) const;
    void rebuildModelCache(const std::string& id);
    void setTextureDemand(std::uint32_t edge,bool nativeQuality=false);
    void setThumbnailDemand(std::uint32_t edge) { thumbnailDemand_=edge; }
    std::size_t textureResidentBytes() const;
    void trimTextureCache(std::size_t bytes);
    ModelResolvers modelResolvers() const;
    std::vector<ModelMaterial> resolvedModelMaterials(const ModelAsset& model,std::span<const AssetRef<MaterialAsset>> materialOverrides={}) const;
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
    void select(const std::string& id) { browserSelection_.set(id.starts_with("folder:")?id:canonicalAssetId(id)); }
    BrowserCatalog& browserCatalog();
    std::string displayName(const std::string& id) const;
    BrowserSelection& browserSelection() { return browserSelection_; }
    const std::string& status() const { return status_; }
    void refreshScenes();
    bool deleteScene(const std::string& id);

    void requestImport(SDL_Window* window, const std::string& destinationFolder = {});
    void processImports();
    const TextureAsset* importTexture(const std::filesystem::path& source,const std::string& destinationFolder = {});
    std::shared_ptr<const ModelAsset> importModel(const std::filesystem::path& source,const std::string& destinationFolder = {});
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
    struct NavigationProfileVersion {std::shared_ptr<const NavigationAgentProfile> definition;std::uint64_t revision{};std::string diagnostic;};
    std::unordered_map<std::string,NavigationProfileVersion> navigationProfiles_;
    std::uint64_t nextNavigationProfileRevision_{1};
    std::string navigationProfileDocumentId_;
    struct GameplayEffectVersion {std::shared_ptr<const GameplayEffectAsset> definition;std::uint64_t revision{};std::string diagnostic;};
    std::unordered_map<std::string,GameplayEffectVersion> gameplayEffects_;
    std::uint64_t nextGameplayEffectRevision_{1};
    std::string gameplayEffectDocumentId_;
    struct AnimationVersion {std::shared_ptr<const AnimationAsset> definition;std::uint64_t revision{};std::string diagnostic;};
    std::unordered_map<std::string,AnimationVersion> animations_;
    std::unordered_map<std::string,ResourceHandle<ModelAsset>> animationCatalogResources_;
    std::uint64_t nextAnimationRevision_{1};
    std::string animationDocumentId_;
    std::vector<std::string> animationDocumentReferences_,animationDocumentHistory_;
    struct MaterialVersion { std::shared_ptr<const MaterialAsset> definition;std::uint64_t revision{};std::string diagnostic; };
    std::unordered_map<std::string,MaterialVersion> authoredMaterials_;
    struct MaterialCompatibility {std::weak_ptr<const MaterialAsset> source;bool compatible{};};
    mutable std::unordered_map<std::string,std::shared_ptr<const PreparedParticleMaterial>> preparedParticleMaterials_;
    std::uint64_t nextMaterialRevision_{1};
    std::string materialDocumentId_;
    std::vector<std::string> particleDocumentReferences_,particleDocumentHistory_,materialDocumentReferences_,materialDocumentHistory_;
    ResourceManager* resources_{};
    std::unique_ptr<AsyncImports> asyncImports_;
    std::unique_ptr<TextureStreamer> textureStreamer_;
    struct ResizeImageSource {
        std::filesystem::path file;
        ResourceHandle<streaming::TextureMipCache> fingerprint;
        ResourceHandle<DecodedImage> pixels;
        bool reloadPixels{};
        std::chrono::steady_clock::time_point used;
    };
    mutable std::unordered_map<std::string,ResizeImageSource> resizeImageSources_;
    struct NodeImagePreview {
        std::string document,textureKey,sourceKey,diagnostic;
        std::array<unsigned,2> sourceExtent{};
        std::shared_ptr<const MaterialGraph> definition;
        std::vector<std::string> sources;
        // A warm material can release the shared source request later in the
        // same frame. Keep pending preview decodes wanted until their immutable
        // pixels have been captured by the generation job.
        std::unordered_map<std::string,ResourceHandle<DecodedImage>> pendingInputs;
        std::uint64_t valueKey{};
        std::shared_ptr<const PreparedTexture> current;
        bool pending{};
        bool displayPending{};
        std::chrono::steady_clock::time_point requested,used;
    };
    mutable std::unordered_map<std::string,NodeImagePreview> nodeImagePreviews_;
    mutable unsigned nodeImageStarts_{};
    mutable std::unordered_map<std::string,ResourceHandle<ModelAsset>> modelResources_;
    mutable std::unordered_map<std::string,std::weak_ptr<const ModelAsset>> primitiveModels_;
    std::shared_ptr<const ModelAsset> primitiveModel(const std::string& id,const PrimitiveMeshSettings&) const;
    mutable std::unordered_set<std::string> modelCacheRebuilds_;
    mutable std::unordered_map<std::string,ResourceHandle<MaterialAsset>> materialResources_;
    mutable std::unordered_map<std::string,std::chrono::steady_clock::time_point> modelInterest_,materialInterest_;
    std::unordered_map<std::string,ResourceHandle<ModelAsset>> materialCatalogResources_;
    bool materialCatalogDirty_{};
    std::uint32_t textureDemand_{512};
    std::uint32_t thumbnailDemand_{128};
    bool nativeTextures_{};
    std::uint64_t resourcePolicyRevision_{};
    std::uint64_t storageRevision_{};
    std::unordered_map<std::string,std::filesystem::path> storageLocations_;
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
    std::unordered_set<std::string> inactiveQuaderModels_;
    std::unordered_set<std::string> retainedQuaderModels_;
    std::vector<std::string> quaderDocumentModelReferences_;
    std::array<std::uint64_t,8> quaderCollectionState_{};
    bool quaderCollectionInitialized_{};
    std::string status_;
    std::vector<std::filesystem::path> ownWrites_;
    TextureAsset* findMutable(const std::string& id) const;
    TextureAsset makeAsset(const std::filesystem::path& path);
    bgfx::TextureHandle fullTexture(TextureAsset& asset, bool srgb) const;
    ModelMaterialTextures materialBindings(const ModelMaterial&) const;
    ModelMaterialTextures materialBindings(const ModelMaterial&,std::uint32_t textureEdge,ResourcePriority) const;
    ModelMaterialTextures graphMaterialBindings(const std::shared_ptr<const MaterialGraph>&,std::uint32_t textureEdge,ResourcePriority,bool useFixedModel=false) const;
    ModelMaterialTextures preparedGraphBindings(MaterialGraphPrograms&,const MaterialGraphShader&,const MaterialGraph&,std::uint32_t textureEdge,ResourcePriority) const;
    struct CachedImageRequestInfo {
        std::string key;
        std::array<unsigned,2> extent{};
        bool requested{},started{};
    };
    PreparedTexture preparedResizeTexture(const MaterialGraphResizeBinding&,std::uint32_t,ResourcePriority,
        std::unordered_map<std::string,std::uint64_t>&,CachedImageRequestInfo* = nullptr,bool allowStart=true) const;
    struct GraphShaderCache {
        std::weak_ptr<const MaterialGraph> source;
        std::shared_ptr<const MaterialGraphShader> shader;
        std::optional<std::uint8_t> requiredUvs;
        bool fixedSurfaceChecked{};
        std::optional<ModelMaterial> fixedSurface;
    };
    mutable std::unordered_map<const MaterialGraph*,GraphShaderCache> graphShaderCache_;
    bool materialCompatible(const std::shared_ptr<const MaterialAsset>&) const;
    GraphShaderCache& graphCache(const std::shared_ptr<const MaterialGraph>&) const;
    std::uint8_t requiredGraphUvs(const std::shared_ptr<const MaterialGraph>&) const;
    MaterialGraphPrograms* graphPrograms_{};
    mutable bool reflectionDemand_{};
    mutable std::unordered_map<const MaterialAsset*,MaterialCompatibility> materialCompatibility_;
    void ensureModelMaterials(const ModelAsset& model);
    void ensureModelAnimations(const ModelAsset& model);
    void registerModel(std::shared_ptr<const ModelAsset> model,bool refreshMaterialCatalog=true);
    std::vector<std::string> newModelImportAssets(const ModelAsset& model,bool reused) const;
    std::string placeImportedAssets(std::span<const std::string> ids,const std::string& destinationFolder);
    void addTexture(TextureAsset asset);
    void refreshReferenceIndex(const SceneDocument* active);
    void refreshQuaderModelCatalog();
};

} // namespace crimson
