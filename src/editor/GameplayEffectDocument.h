#pragma once

#include "assets/GameplayEffectAsset.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {

// Independent asset history. Only committed immutable generations are published
// for future applications; a property gesture can always be canceled.
class GameplayEffectDocument {
public:
    using Mutation = std::function<void(GameplayEffectAsset&)>;
    static constexpr size_t MaximumHistoryEntries = 256;
    explicit GameplayEffectDocument(std::filesystem::path projectRoot);
    bool isOpen() const { return open_; }
    const std::string& assetId() const { return working_.id; }
    const std::filesystem::path& projectRoot() const { return root_; }
    const GameplayEffectAsset& definition() const { return working_; }
    const GameplayEffectAsset& savedDefinition() const { return saved_; }
    const std::shared_ptr<const GameplayEffectAsset>& committed() const { return committed_; }
    uint64_t revision() const { return revision_; }
    uint64_t previewRevision() const { return previewRevision_; }
    const std::string& status() const { return status_; }
    bool dirty() const;
    bool transactionActive() const { return transaction_.has_value(); }
    bool canUndo() const { return cursor_ != 0 && !transaction_; }
    bool canRedo() const { return cursor_ < history_.size() && !transaction_; }
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool editable() const { return open_ && !isEngineAssetId(working_.id); }
    bool create(const std::string& name = "New Gameplay Effect");
    bool load(const std::string& id);
    bool loadSnapshot(const GameplayEffectAsset&);
    bool save(const std::string& name = {}, bool saveAs = false);
    bool discard();
    void close(); // Caller resolves Save / Discard / Cancel before closing.
    bool externalFileChanged() const;
    bool begin(const std::string& label);
    bool update(const Mutation& mutation);
    bool commit();
    void cancel();
    bool edit(const std::string& label, const Mutation& mutation);
    bool undo();
    bool redo();
private:
    struct Command {
        std::string label;
        GameplayEffectAsset before, after;
        uint64_t beforeState{}, afterState{};
    };
    struct Transaction {
        std::string label;
        GameplayEffectAsset before;
        bool invalid{};
    };
    std::filesystem::path root_;
    GameplayEffectAsset working_, saved_;
    std::shared_ptr<const GameplayEffectAsset> committed_;
    std::vector<Command> history_;
    size_t cursor_{};
    std::optional<Transaction> transaction_;
    std::string status_;
    uint64_t revision_{}, previewRevision_{}, state_{1}, savedState_{1}, nextState_{2}, fingerprint_{};
    bool open_{},persisted_{};
    void install(GameplayEffectAsset asset, bool saved);
    void publish();
};

} // namespace crimson
