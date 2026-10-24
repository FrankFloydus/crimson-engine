#pragma once

#include "assets/RagdollAsset.h"
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {

// Authored rigs have independent history. Preview time and camera never enter
// this document, and incomplete property gestures publish no shared generation.
class RagdollDocument {
public:
    using Mutation=std::function<void(RagdollAsset&)>;
    static constexpr std::size_t MaximumHistoryEntries=256;
    explicit RagdollDocument(std::filesystem::path projectRoot);
    bool isOpen() const { return open_; }
    bool editable() const { return open_; }
    const std::string& assetId() const { return working_.id; }
    const std::filesystem::path& projectRoot() const { return root_; }
    const RagdollAsset& definition() const { return working_; }
    const RagdollAsset& savedDefinition() const { return saved_; }
    const std::shared_ptr<const RagdollAsset>& committed() const { return committed_; }
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
    bool create(RagdollAsset);
    bool save(const std::string& name={},bool saveAs=false,bool overwriteExternal=false);
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
    bool rebindSource(const ModelAsset&,std::uint32_t skin,const std::vector<std::uint32_t>& mappedNodes);
    std::vector<std::string> modelReferences(bool history=false) const;
private:
    struct Command {
        std::string label;
        RagdollAsset before,after;
        std::uint64_t beforeState{},afterState{};
    };
    struct Transaction { std::string label;RagdollAsset before;bool invalid{}; };
    std::filesystem::path root_;
    RagdollAsset working_,saved_;
    std::shared_ptr<const RagdollAsset> committed_;
    std::vector<Command> history_;
    std::size_t cursor_{};
    std::optional<Transaction> transaction_;
    std::string status_;
    std::uint64_t revision_{},previewRevision_{},state_{1},savedState_{1},nextState_{2},fingerprint_{};
    bool open_{},hasSaved_{},allowSourceChange_{};
    void install(RagdollAsset);
    void publish();
};
}
