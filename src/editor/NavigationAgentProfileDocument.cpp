#include "NavigationAgentProfileDocument.h"
#include "resources/AssetId.h"
#include "assets/ContentStorage.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
uint64_t fileFingerprint(const std::filesystem::path& root,const std::string& id) {
    std::ifstream input(navigationAgentProfilePath(root,id),std::ios::binary);
    if (!input) throw std::runtime_error("Navigation profile file is unavailable.");
    uint64_t hash=14695981039346656037ull;
    char bytes[4096];
    while (input.read(bytes,sizeof(bytes)) || input.gcount())
        for (std::streamsize i=0;i<input.gcount();++i) { hash^=static_cast<unsigned char>(bytes[i]); hash*=1099511628211ull; }
    if (input.bad()) throw std::runtime_error("Could not fingerprint the complete navigation profile file.");
    return hash;
}
}

NavigationAgentProfileDocument::NavigationAgentProfileDocument(std::filesystem::path projectRoot)
    :root_(std::filesystem::absolute(std::move(projectRoot)).lexically_normal()) {}
bool NavigationAgentProfileDocument::dirty() const {
    return open_ && (state_!=savedState_ || (transaction_ && working_!=transaction_->before));
}
std::string NavigationAgentProfileDocument::undoLabel() const { return canUndo()?history_[cursor_-1].label:std::string{}; }
std::string NavigationAgentProfileDocument::redoLabel() const { return canRedo()?history_[cursor_].label:std::string{}; }
void NavigationAgentProfileDocument::publish() {
    committed_=std::make_shared<const NavigationAgentProfile>(working_);
    ++revision_;
}
void NavigationAgentProfileDocument::install(NavigationAgentProfile asset,bool saved) {
    validateNavigationAgentProfile(asset);
    asset.id=canonicalAssetId(asset.id);
    auto prepared=std::make_shared<const NavigationAgentProfile>(asset);
    const auto fingerprint=saved && asset.id!=DefaultNavigationAgentProfileId?fileFingerprint(root_,asset.id):0;
    working_=std::move(asset); saved_=working_; committed_=std::move(prepared);
    history_.clear(); cursor_=0; transaction_.reset();
    state_=nextState_++; savedState_=saved?state_:0; fingerprint_=fingerprint;
    open_=true;persisted_=saved; ++revision_; ++previewRevision_;
}
bool NavigationAgentProfileDocument::create(const std::string& name) {
    if (transaction_) { status_="Finish the current navigation profile edit before creating another profile."; return false; }
    try { install(createNavigationAgentProfile(root_,name),true); status_="Created "+working_.name+"."; return true; }
    catch (const std::exception& error) { status_="Could not create navigation profile: "+std::string(error.what()); return false; }
}
bool NavigationAgentProfileDocument::load(const std::string& id) {
    if (transaction_) { status_="Finish the current navigation profile edit before opening another profile."; return false; }
    try { auto candidate=id==DefaultNavigationAgentProfileId?*defaultNavigationAgentProfile():loadNavigationAgentProfile(root_,id); install(std::move(candidate),true); status_="Opened "+working_.name+"."; return true; }
    catch (const std::exception& error) { status_="Could not open navigation profile: "+std::string(error.what()); return false; }
}
bool NavigationAgentProfileDocument::loadSnapshot(const NavigationAgentProfile& profile) {
    if(transaction_)return false;
    try { install(profile,false);status_.clear();return true; }
    catch(const std::exception& error){status_=error.what();return false;}
}
bool NavigationAgentProfileDocument::save(const std::string& name,bool saveAs) {
    if (!open_) { status_="No navigation profile document is open."; return false; }
    if(!saveAs && !editable()) { status_="Default Agent is read-only; use Save As.";return false; }
    if (transaction_ && !commit()) return false;
    if (!saveAs && !name.empty() && name!=working_.name)
        if (!edit("Rename navigation profile",[&](auto& asset){asset.name=name;})) return false;
    try {
        auto saved=saveAs?copyNavigationAgentProfile(root_,working_,name):working_;
        if (!saveAs) saveNavigationAgentProfile(root_,saved);
        const auto fingerprint=fileFingerprint(root_,saved.id);
        if (saveAs) {
            // History belongs to the new document; undo cannot restore the old
            // managed file ID and accidentally overwrite its source asset.
            for (auto& command:history_) { command.before.id=saved.id; command.after.id=saved.id; }
            if (saved.name!=working_.name) {
                auto before=working_; before.id=saved.id;
                Command command{"Rename navigation profile",std::move(before),saved,state_,nextState_++};
                history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
                history_.push_back(std::move(command));
                if (history_.size()>MaximumHistoryEntries) history_.erase(history_.begin());
                cursor_=history_.size(); state_=history_.back().afterState;
            }
            working_=saved;
            publish(); ++previewRevision_;
        }
        saved_=working_; savedState_=state_; fingerprint_=fingerprint;persisted_=true;
        status_="Saved "+working_.name+".";
        return true;
    } catch (const std::exception& error) {
        status_="Could not save navigation profile: "+std::string(error.what());
        return false;
    }
}
bool NavigationAgentProfileDocument::discard() {
    if (!open_) return false;
    transaction_.reset(); working_=saved_; history_.clear(); cursor_=0;
    state_=nextState_++; savedState_=persisted_?state_:0;
    publish(); ++previewRevision_; status_="Discarded unsaved navigation profile edits.";
    return true;
}
void NavigationAgentProfileDocument::close() {
    open_=false; transaction_.reset(); history_.clear(); cursor_=0;
    committed_.reset(); working_=NavigationAgentProfile{}; saved_=working_;
    ++revision_; ++previewRevision_; status_.clear();
}
bool NavigationAgentProfileDocument::externalFileChanged() const {
    if (!open_ || working_.id.empty() || !editable()) return false;
    if(!persisted_) {
        // A scene Save may materialize this seed while its editor is open.
        // Equal original seed bytes are safe; independently edited or corrupt
        // materializations require the ordinary explicit conflict resolution.
        if(contentStorageReady(root_))for(const auto& entry:storedAssetPaths(root_))if(entry.id==working_.id) {
            try {return loadNavigationAgentProfile(root_,working_.id)!=saved_;}
            catch(...) {return true;}
        }
        return false;
    }
    try { return fileFingerprint(root_,working_.id)!=fingerprint_; }
    catch (...) { return true; }
}
bool NavigationAgentProfileDocument::begin(const std::string& label) {
    if (!editable() || transaction_) return false;
    transaction_=Transaction{label,working_,false};
    return true;
}
bool NavigationAgentProfileDocument::update(const Mutation& mutation) {
    if (!transaction_) return false;
    try {
        auto candidate=working_; mutation(candidate);
        if (candidate.id!=transaction_->before.id) throw std::runtime_error("A navigation profile edit cannot change its managed identity; use Save As.");
        validateNavigationAgentProfile(candidate);
        transaction_->invalid=false;
        if (candidate!=working_) { working_=std::move(candidate); ++previewRevision_; }
        status_.clear(); return true;
    } catch (const std::exception& error) {
        transaction_->invalid=true;
        status_=error.what();
        return false;
    }
}
bool NavigationAgentProfileDocument::commit() {
    if (!transaction_) return false;
    if (transaction_->invalid) return false;
    if (working_==transaction_->before) { transaction_.reset(); return true; }
    try {
        validateNavigationAgentProfile(working_);
        auto snapshot=std::make_shared<const NavigationAgentProfile>(working_);
        Command command{transaction_->label,transaction_->before,working_,state_,nextState_++};
        history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
        history_.push_back(std::move(command));
        if (history_.size()>MaximumHistoryEntries) history_.erase(history_.begin());
        cursor_=history_.size(); state_=history_.back().afterState;
        committed_=std::move(snapshot); ++revision_;
        transaction_.reset(); status_.clear(); return true;
    } catch (const std::exception& error) { status_=error.what(); return false; }
}
void NavigationAgentProfileDocument::cancel() {
    if (!transaction_) return;
    // Preview owners restore these values live; cancellation does not rewind time.
    working_=std::move(transaction_->before); transaction_.reset();
    ++previewRevision_; status_.clear();
}
bool NavigationAgentProfileDocument::edit(const std::string& label,const Mutation& mutation) {
    if (!begin(label)) return false;
    if (!update(mutation)) {
        const auto diagnostic=status_; cancel(); status_=diagnostic; return false;
    }
    return commit();
}
bool NavigationAgentProfileDocument::undo() {
    if (!canUndo()) return false;
    const auto& command=history_[cursor_-1];
    working_=command.before; state_=command.beforeState; --cursor_;
    publish(); ++previewRevision_; status_="Undid "+command.label+"."; return true;
}
bool NavigationAgentProfileDocument::redo() {
    if (!canRedo()) return false;
    const auto& command=history_[cursor_];
    working_=command.after; state_=command.afterState; ++cursor_;
    publish(); ++previewRevision_; status_="Redid "+command.label+"."; return true;
}

} // namespace crimson
