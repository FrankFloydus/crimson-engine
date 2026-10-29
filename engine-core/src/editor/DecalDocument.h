#pragma once

#include "assets/DecalAsset.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {

// This history owns surface definitions, independently of scene history. An
// active transaction changes only isolated preview; commit publishes a revision.
class DecalDocument {
public:
    using Mutation = std::function<void(DecalAsset&)>;
    static constexpr size_t MaximumHistoryEntries = 256;
    explicit DecalDocument(std::filesystem::path projectRoot);
    bool isOpen() const { return open_; }
    const std::string& assetId() const { return working_.id; }
    const std::filesystem::path& projectRoot() const { return root_; }
    const DecalAsset& definition() const { return working_; }
    const DecalAsset& savedDefinition() const { return saved_; }
    const std::shared_ptr<const DecalAsset>& committed() const { return committed_; }
    uint64_t revision() const { return revision_; }
    uint64_t previewRevision() const { return previewRevision_; }
    const std::string& status() const { return status_; }
    bool dirty() const;
    bool editable() const;
    bool transactionActive() const { return transaction_.has_value(); }
    bool canUndo() const { return cursor_ != 0 && !transaction_; }
    bool canRedo() const { return cursor_ < history_.size() && !transaction_; }
    std::string undoLabel() const;
    std::string redoLabel() const;
    std::vector<std::string> historicalMaterialReferences() const;
    bool reconcileMaterialReferences(const std::function<std::string(const std::string&)>&);
    bool create(const std::string& name = "New Decal");
    bool load(const std::string& id);
    bool open(const std::string& id) { return load(id); }
    bool save(const std::string& name = {}, bool saveAs = false);
    bool saveAs(const std::string& name = {}) { return save(name,true); }
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
        DecalAsset before, after;
        uint64_t beforeState{}, afterState{};
    };
    struct Transaction {
        std::string label;
        DecalAsset before;
        bool invalid{};
    };
    std::filesystem::path root_;
    DecalAsset working_, saved_;
    std::shared_ptr<const DecalAsset> committed_;
    std::vector<Command> history_;
    size_t cursor_{};
    std::optional<Transaction> transaction_;
    std::string status_;
    uint64_t revision_{}, previewRevision_{}, state_{1}, savedState_{1}, nextState_{2}, fingerprint_{};
    bool open_{};
    void install(DecalAsset asset, bool saved);
    void publish();
};

} // namespace crimson
