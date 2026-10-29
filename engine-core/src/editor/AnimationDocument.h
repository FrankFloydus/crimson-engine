#pragma once

#include "assets/AnimationAsset.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {

// Authored clips have independent history. Preview time and camera never enter
// this document, and incomplete property gestures publish no shared generation.
class AnimationDocument {
public:
    using Mutation=std::function<void(AnimationAsset&)>;
    static constexpr std::size_t MaximumHistoryEntries=256;
    explicit AnimationDocument(std::filesystem::path projectRoot);
    bool isOpen() const { return open_; }
    bool editable() const { return open_; }
    const std::string& assetId() const { return working_.id; }
    const std::filesystem::path& projectRoot() const { return root_; }
    const AnimationAsset& definition() const { return working_; }
    const AnimationAsset& savedDefinition() const { return saved_; }
    const std::shared_ptr<const AnimationAsset>& committed() const { return committed_; }
    std::uint64_t revision() const { return revision_; }
    std::uint64_t previewRevision() const { return previewRevision_; }
    const std::string& status() const { return status_; }
    bool dirty() const;
    bool transactionActive() const { return transaction_.has_value(); }
    bool canUndo() const { return cursor_!=0&&!transaction_; }
    bool canRedo() const { return cursor_<history_.size()&&!transaction_; }
    std::string undoLabel() const;
    std::string redoLabel() const;
    bool load(const std::string& id);
    bool save(const std::string& name={},bool saveAs=false);
    bool discard();
    void close();
    bool externalFileChanged() const;
    bool begin(const std::string& label);
    bool update(const Mutation&);
    bool commit();
    void cancel();
    bool edit(const std::string& label,const Mutation&);
    bool undo();
    bool redo();
    std::vector<std::string> modelReferences(bool history=false) const;
private:
    struct Command {
        std::string label;
        AnimationAsset before,after;
        std::uint64_t beforeState{},afterState{};
    };
    struct Transaction { std::string label;AnimationAsset before;bool invalid{}; };
    std::filesystem::path root_;
    AnimationAsset working_,saved_;
    std::shared_ptr<const AnimationAsset> committed_;
    std::vector<Command> history_;
    std::size_t cursor_{};
    std::optional<Transaction> transaction_;
    std::string status_;
    std::uint64_t revision_{},previewRevision_{},state_{1},savedState_{1},nextState_{2},fingerprint_{};
    bool open_{};
    void install(AnimationAsset);
    void publish();
};
}
