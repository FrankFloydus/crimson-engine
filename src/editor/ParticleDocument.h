#pragma once

#include "assets/ParticleAsset.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {

// This history owns effect definitions, independently of scene history. An
// active transaction changes only isolated preview; commit publishes a revision.
class ParticleEffectDocument {
public:
    using Mutation = std::function<void(ParticleEffectAsset&)>;
    static constexpr size_t MaximumHistoryEntries = 256;
    explicit ParticleEffectDocument(std::filesystem::path projectRoot);
    bool isOpen() const { return open_; }
    const std::string& assetId() const { return working_.id; }
    const ParticleEffectAsset& definition() const { return working_; }
    const std::shared_ptr<const ParticleEffectAsset>& committed() const { return committed_; }
    uint64_t revision() const { return revision_; }
    uint64_t previewRevision() const { return previewRevision_; }
    const std::string& status() const { return status_; }
    bool dirty() const;
    bool transactionActive() const { return transaction_.has_value(); }
    bool canUndo() const { return cursor_ != 0 && !transaction_; }
    bool canRedo() const { return cursor_ < history_.size() && !transaction_; }
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool create(const std::string& name = "New Particle Effect");
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
        ParticleEffectAsset before, after;
        uint64_t beforeState{}, afterState{};
    };
    struct Transaction {
        std::string label;
        ParticleEffectAsset before;
        bool invalid{};
    };
    std::filesystem::path root_;
    ParticleEffectAsset working_, saved_;
    std::shared_ptr<const ParticleEffectAsset> committed_;
    std::vector<Command> history_;
    size_t cursor_{};
    std::optional<Transaction> transaction_;
    std::string status_;
    uint64_t revision_{}, previewRevision_{}, state_{1}, savedState_{1}, nextState_{2}, fingerprint_{};
    bool open_{};
    void install(ParticleEffectAsset asset, bool saved);
    void publish();
};

} // namespace crimson
