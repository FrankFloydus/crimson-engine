#pragma once

#include "Scene.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {

struct SceneSelection {
    std::vector<EntityId> ids;
    EntityId primary{};
};
struct SceneAssetEntry { std::string id, name; };
struct ModelPlacement { std::string assetId, name; Mat4 local{}; };
struct SceneSnapshot {
    std::string id, name;
    std::vector<EntityRecord> records;
    SceneProperties properties;
    std::optional<Camera> previewCamera;
    uint64_t fingerprint{};
};
std::filesystem::path sceneAssetPath(const std::filesystem::path& projectRoot, const std::string& id);
SceneSnapshot readSceneSnapshot(const std::filesystem::path& projectRoot, const std::string& id);
std::string serializeSceneSnapshot(const SceneSnapshot& snapshot);
void saveSceneSnapshot(const std::filesystem::path& projectRoot, const std::string& id, const SceneSnapshot& snapshot);
uint64_t sceneFileFingerprint(const std::filesystem::path& projectRoot, const std::string& id);
uint64_t sceneTextFingerprint(std::string_view text) noexcept;
std::vector<SceneAssetEntry> scanSceneAssets(const std::filesystem::path& projectRoot,
                                           std::vector<std::string>* diagnostics = nullptr);
std::string availableSceneName(const std::filesystem::path& projectRoot,const std::string& base);
bool deleteSceneAsset(const std::filesystem::path& projectRoot, const std::string& id, std::string& error);

struct EntityChange {
    EntityId id{};
    std::optional<EntityRecord> before, after;
    uint32_t fields{};
};
struct SceneCommand {
    std::string label;
    std::vector<EntityChange> entities;
    std::optional<SceneProperties> beforeProperties, afterProperties;
    uint32_t propertyFields{};
    SceneSelection beforeSelection, afterSelection;
    uint64_t beforeState{}, afterState{};
};

class UndoRedoManager {
public:
    static constexpr size_t MaximumEntries = 256;
    bool canUndo() const { return cursor_ != 0; }
    bool canRedo() const { return cursor_ < commands_.size(); }
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool dirty() const { return state_ != savedState_; }
    size_t size() const { return commands_.size(); }
private:
    friend class SceneDocument;
    std::vector<SceneCommand> commands_;
    size_t cursor_{};
    uint64_t state_{1}, savedState_{}, nextState_{2};
    void reset(bool saved);
    void push(SceneCommand command);
};

class SceneDocument {
public:
    class PreparedExternalChange {
    public:
        PreparedExternalChange(PreparedExternalChange&&) noexcept=default;
        PreparedExternalChange& operator=(PreparedExternalChange&&) noexcept=default;
        PreparedExternalChange(const PreparedExternalChange&)=delete;
        PreparedExternalChange& operator=(const PreparedExternalChange&)=delete;
    private:
        friend class SceneDocument;
        PreparedExternalChange()=default;
        std::unique_ptr<Scene> scene;
        SceneSelection selection;
        const SceneDocument* owner{};
        std::string expectedId,status,name;
        uint64_t expectedGeneration{},expectedRevision{},expectedState{},expectedSavedState{},expectedFingerprint{},savedFingerprint{};
        bool wasDirty{},newDocument{};
    };
    using Mutation = std::function<void(std::vector<EntityRecord>&, SceneProperties&)>;
    explicit SceneDocument(std::filesystem::path projectRoot);
    const Scene& scene() const { return scene_; }
    const UndoRedoManager& history() const { return history_; }
    const SceneSelection& selection() const { return selection_; }
    void select(std::vector<EntityId> ids, EntityId primary = NoEntity);
    const std::string& assetId() const { return assetId_; }
    const std::filesystem::path& projectRoot() const { return projectRoot_; }
    const std::string& displayName() const { return name_; }
    const std::string& status() const { return status_; }
    bool dirty() const;
    bool transactionActive() const { return transaction_.has_value(); }
    uint64_t transactionId() const { return transaction_?transaction_->id:0; }
    bool transient() const { return transient_; }
    bool resetTransientFrom(const SceneDocument& source);
    bool applyRuntimePoses(std::span<const ScenePoseUpdate> poses);
    bool beginTransform(const std::string& label, std::vector<EntityId> roots);
    void setTransformEditRoots(std::vector<EntityId> roots);
    const std::vector<EntityId>& transformEditRoots() const;

    void newScene();
    bool load(const std::string& id);
    bool openPreparedSnapshot(const SceneSnapshot& snapshot);
    bool save(const std::string& name = {}, bool saveAs = false, bool overwriteExternal = false);
    bool reloadFromDisk();
    bool applyReloadSnapshot(const SceneSnapshot& snapshot, bool preserveSelection = true);
    bool externalFileChanged() const;
    void setPreviewCamera(const Camera& camera) { previewCamera_=camera; }
    const std::optional<Camera>& previewCamera() const { return previewCamera_; }
    uint64_t loadedFingerprint() const { return loadedFingerprint_; }
    uint64_t documentGeneration() const { return documentGeneration_; }
    std::vector<std::string> historicalModelReferences() const;
    std::vector<std::string> historicalParticleReferences() const;
    std::vector<std::string> historicalMaterialReferences() const;
    // Global resource deletion is outside scene undo. Install its complete
    // validated candidate and remove history that could resurrect deleted IDs.
    bool applyExternalAssetChanges(const std::vector<EntityRecord>& records, const SceneProperties& properties,
                                   const std::string& label, bool wasDirty);
    PreparedExternalChange prepareExternalAssetChanges(const std::vector<EntityRecord>& records,const SceneProperties& properties,
                                                       const std::string& label,bool wasDirty,uint64_t savedFingerprint) const;
    PreparedExternalChange prepareNewSceneAfterAssetDeletion() const;
    bool canCommitExternalAssetChanges(const PreparedExternalChange& prepared) const noexcept;
    bool commitExternalAssetChanges(PreparedExternalChange&& prepared) noexcept;
    bool edit(const std::string& label, const Mutation& mutation);
    bool begin(const std::string& label);
    bool update(const Mutation& mutation);
    bool commit();
    void cancel();
    bool undo();
    bool redo();
    EntityId createEmpty(const std::string& name = "Entity");
    EntityId createQuader(const std::string& name = "Quader");
    // Generated records use temporary IDs/parent IDs and stable source keys.
    // Reimport preserves matching persistent identities and all manual siblings.
    using ModelMaterialRemap = std::function<ModelMaterialOverrides(const EntityRecord&,const EntityRecord&)>;
    bool replaceQuaderImport(EntityId owner,const QuaderLink&,const std::vector<EntityRecord>& generated,
        const ModelMaterialRemap& remapMaterials={});
    EntityId createModel(const std::string& assetId, const std::string& name, const Mat4& local = Mat4{});
    EntityId createParticle(const std::string& assetId, const std::string& name = "Particle Effect", const Mat4& local = Mat4{});
    std::vector<EntityId> createModels(const std::vector<ModelPlacement>& models);
    EntityId createDirectionalLight(const std::string& name = "Directional Light");
    EntityId createPointLight(const std::string& name = "Point Light");
    EntityId createEnvironment(const std::string& name = "Environment");
    EntityId createCharacter(const std::string& name = "Character");
    EntityId createCollider(const std::string& name = "Collider", EntityId parent = NoEntity);
    EntityId createCamera(const std::string& name = "Camera");
    EntityId createCamera(const Camera& view,float aspect,const std::string& name = "Camera");
    EntityId createBox(const std::string& name = "Box");
    bool setActiveEnvironment(EntityId id);
    bool setActiveCamera(EntityId id);
    bool setPlayerCharacter(EntityId id);
    bool setCameraFollowTarget(EntityId camera, EntityId target);
    bool setCameraFollow(EntityId camera, EntityId target) { return setCameraFollowTarget(camera,target); }
    bool updateEntity(EntityId id, const EntityRecord& record, const std::string& label = "Edit entity");
    bool eraseSelection();
    bool duplicateSelection();
    bool reparentSelection(EntityId parent);
    bool groupSelection(const std::string& name = "Group");
    bool ungroupSelection();
    bool setProperties(const SceneProperties& properties, const std::string& label = "Edit scene settings");
private:
    bool applyPreparedSnapshot(const SceneSnapshot&,bool preserveSelection,bool opening);
    struct Transaction {
        SceneCommand command;
        std::vector<EntityId> transformRoots;
        uint64_t id{};
    };
    std::filesystem::path projectRoot_;
    Scene scene_;
    UndoRedoManager history_;
    SceneSelection selection_;
    std::string assetId_, name_{"Untitled"}, status_;
    std::optional<Camera> previewCamera_;
    uint64_t loadedFingerprint_{};
    uint64_t documentGeneration_{};
    uint64_t transactionSerial_{};
    std::optional<Transaction> transaction_;
    bool transient_{};
    EntityId create(EntityRecord record, const std::string& label);
    bool applyHistory(const SceneCommand& command, bool forward);
    void sanitizeSelection();
};

} // namespace crimson
