#pragma once

#include "Scene.h"
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace crimson {

struct SceneSelection {
    std::vector<EntityId> ids;
    EntityId primary{};
};
struct SceneAssetEntry { std::string id, name; };
struct ModelPlacement { std::string assetId, name; Mat4 local{}; };
std::vector<SceneAssetEntry> scanSceneAssets(const std::filesystem::path& projectRoot,
                                           std::vector<std::string>* diagnostics = nullptr);
bool deleteSceneAsset(const std::filesystem::path& projectRoot, const std::string& id, std::string& error);

struct EntityChange {
    EntityId id{};
    std::optional<EntityRecord> before, after;
};
struct SceneCommand {
    std::string label;
    std::vector<EntityChange> entities;
    std::optional<SceneProperties> beforeProperties, afterProperties;
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
    using Mutation = std::function<void(std::vector<EntityRecord>&, SceneProperties&)>;
    explicit SceneDocument(std::filesystem::path projectRoot);
    const Scene& scene() const { return scene_; }
    const UndoRedoManager& history() const { return history_; }
    const SceneSelection& selection() const { return selection_; }
    void select(std::vector<EntityId> ids, EntityId primary = NoEntity);
    const std::string& assetId() const { return assetId_; }
    const std::string& displayName() const { return name_; }
    const std::string& status() const { return status_; }
    bool dirty() const;
    bool transactionActive() const { return transaction_.has_value(); }

    void newScene();
    bool load(const std::string& id);
    bool save(const std::string& name = {}, bool saveAs = false);
    bool edit(const std::string& label, const Mutation& mutation);
    bool begin(const std::string& label);
    bool update(const Mutation& mutation);
    bool commit();
    void cancel();
    bool undo();
    bool redo();
    EntityId createEmpty(const std::string& name = "Entity");
    EntityId createModel(const std::string& assetId, const std::string& name, const Mat4& local = Mat4{});
    std::vector<EntityId> createModels(const std::vector<ModelPlacement>& models);
    EntityId createDirectionalLight(const std::string& name = "Directional Light");
    EntityId createPointLight(const std::string& name = "Point Light");
    bool updateEntity(EntityId id, const EntityRecord& record, const std::string& label = "Edit entity");
    bool eraseSelection();
    bool duplicateSelection();
    bool reparentSelection(EntityId parent);
    bool groupSelection(const std::string& name = "Group");
    bool ungroupSelection();
    bool setProperties(const SceneProperties& properties, const std::string& label = "Edit scene settings");
private:
    struct Transaction {
        std::string label;
        std::vector<EntityRecord> before;
        SceneProperties properties;
        SceneSelection selection;
    };
    std::filesystem::path projectRoot_;
    Scene scene_;
    UndoRedoManager history_;
    SceneSelection selection_;
    std::string assetId_, name_{"Untitled"}, status_;
    std::optional<Transaction> transaction_;
    EntityId create(EntityRecord record, const std::string& label);
    bool applyHistory(const SceneCommand& command, bool forward);
    void sanitizeSelection();
};

} // namespace crimson
