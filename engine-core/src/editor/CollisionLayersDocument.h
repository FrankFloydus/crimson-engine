#pragma once
#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace crimson {
using CollisionLayerNames=std::array<std::string,32>;
const CollisionLayerNames& defaultCollisionLayerNames();
class CollisionLayersDocument {
public:
    using Names=CollisionLayerNames;
    using Mutation=std::function<void(Names&)>;
    static constexpr std::size_t MaximumHistoryEntries=256;
    explicit CollisionLayersDocument(std::filesystem::path projectRoot);
    const Names& names()const{return working_;}
    const std::shared_ptr<const Names>& committed()const{return committed_;}
    std::string name(unsigned slot)const;
    const std::filesystem::path& filePath()const{return file_;}
    std::uint64_t revision()const{return revision_;}
    bool dirty()const;
    bool transactionActive()const{return transaction_.has_value();}
    const std::string& status()const{return status_;}
    bool canUndo()const{return cursor_!=0&&!transaction_;}
    bool canRedo()const{return cursor_<history_.size()&&!transaction_;}
    std::string undoLabel()const;
    std::string redoLabel()const;
    bool setName(unsigned slot,const std::string& name);
    bool begin(const std::string& label="Rename collision layer");
    bool update(const Mutation&);
    bool updateName(unsigned slot,const std::string& name);
    bool commit();
    void cancel();
    bool edit(const std::string& label,const Mutation&);
    bool undo();
    bool redo();
    bool save(bool overwriteExternal=false);
    bool reload(bool discardUnsaved=false);
    bool discard(){return reload(true);}
    bool externalFileChanged()const;
    // Reports changed external state; never replaces dirty working values.
    bool checkExternalConflict();
private:
    struct Command {std::string label;Names before,after;std::uint64_t beforeState{},afterState{};};
    struct Transaction {std::string label;Names before;bool invalid{};};
    std::filesystem::path file_;
    Names working_=defaultCollisionLayerNames();
    std::shared_ptr<const Names> committed_;
    std::vector<Command> history_;
    std::optional<Transaction> transaction_;
    std::optional<std::uint64_t> fingerprint_;
    std::size_t cursor_{};
    std::uint64_t revision_{},state_{1},savedState_{1},nextState_{2};
    std::string status_;
};
}
