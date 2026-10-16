#pragma once

#include "assets/MaterialAsset.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {

// Material history is independent of scenes and particle effects. Only
// committed, valid revisions are published to shared users.
class MaterialDocument {
public:
    using Mutation = std::function<void(MaterialAsset&)>;
    using TextureReferenceResolver = std::function<std::string(const std::string&,MaterialTextureRole)>;
    static constexpr size_t MaximumHistoryEntries = 256;
    explicit MaterialDocument(std::filesystem::path projectRoot);
    bool isOpen() const { return open_; }
    const std::string& assetId() const { return working_.id; }
    const std::filesystem::path& projectRoot() const { return root_; }
    const MaterialAsset& definition() const { return working_; }
    const std::shared_ptr<const MaterialAsset>& committed() const { return committed_; }
    uint64_t revision() const { return revision_; }
    uint64_t previewRevision() const { return previewRevision_; }
    const std::string& status() const { return status_; }
    bool dirty() const;
    bool editable() const;
    std::vector<std::string> historicalTextureReferences() const;
    // Reconcile project deletion policy across every retained version without
    // changing history/dirty tokens or publishing an active gesture's values.
    // Returns true on success (including no changes), false with status on failure.
    bool reconcileTextureReferences(const TextureReferenceResolver&);
    bool transactionActive() const { return transaction_.has_value(); }
    bool canUndo() const { return cursor_ != 0 && !transaction_; }
    bool canRedo() const { return cursor_ < history_.size() && !transaction_; }
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool create(const std::string& name = "New Material");
    bool load(const std::string& id);
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
        MaterialAsset before, after;
        uint64_t beforeState{}, afterState{};
    };
    struct Transaction {
        std::string label;
        MaterialAsset before;
        bool invalid{};
    };
    std::filesystem::path root_;
    MaterialAsset working_, saved_;
    std::shared_ptr<const MaterialAsset> committed_;
    std::vector<Command> history_;
    size_t cursor_{};
    std::optional<Transaction> transaction_;
    std::string status_;
    uint64_t revision_{}, previewRevision_{}, state_{1}, savedState_{1}, nextState_{2}, fingerprint_{};
    bool open_{};
    void install(MaterialAsset asset, bool saved);
    void publish();
};

} // namespace crimson
