#include "ParticleDocument.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
uint64_t fileFingerprint(const std::filesystem::path& root,const std::string& id) {
    std::ifstream input(particleAssetPath(root,id),std::ios::binary);
    if (!input) throw std::runtime_error("Particle file is unavailable.");
    uint64_t hash=14695981039346656037ull;
    char bytes[4096];
    while (input.read(bytes,sizeof(bytes)) || input.gcount())
        for (std::streamsize i=0;i<input.gcount();++i) { hash^=static_cast<unsigned char>(bytes[i]); hash*=1099511628211ull; }
    if (input.bad()) throw std::runtime_error("Could not fingerprint the complete particle file.");
    return hash;
}
}

ParticleEffectDocument::ParticleEffectDocument(std::filesystem::path projectRoot)
    :root_(std::filesystem::absolute(std::move(projectRoot)).lexically_normal()) {}
bool ParticleEffectDocument::dirty() const {
    return open_ && (state_!=savedState_ || (transaction_ && working_!=transaction_->before));
}
std::string ParticleEffectDocument::undoLabel() const { return canUndo()?history_[cursor_-1].label:std::string{}; }
std::string ParticleEffectDocument::redoLabel() const { return canRedo()?history_[cursor_].label:std::string{}; }
std::vector<std::string> ParticleEffectDocument::historicalMaterialReferences() const {
    std::vector<std::string> references;
    const auto append=[&](const ParticleEffectAsset& definition) {
        const auto& id=definition.emitter.material.id;
        if (!id.empty() && std::find(references.begin(),references.end(),id)==references.end()) references.push_back(id);
    };
    for (const auto& command:history_) { append(command.before);append(command.after); }
    if (transaction_) append(transaction_->before);
    return references;
}
void ParticleEffectDocument::publish() {
    committed_=std::make_shared<const ParticleEffectAsset>(working_);
    ++revision_;
}
void ParticleEffectDocument::install(ParticleEffectAsset asset,bool saved) {
    validateParticleAsset(asset);
    auto prepared=std::make_shared<const ParticleEffectAsset>(asset);
    const auto fingerprint=saved?fileFingerprint(root_,asset.id):0;
    working_=std::move(asset); saved_=working_; committed_=std::move(prepared);
    history_.clear(); cursor_=0; transaction_.reset();
    state_=nextState_++; savedState_=saved?state_:0; fingerprint_=fingerprint;
    open_=true; ++revision_; ++previewRevision_;
}
bool ParticleEffectDocument::create(const std::string& name) {
    if (transaction_) { status_="Finish the current particle edit before creating another effect."; return false; }
    try { install(createParticleAsset(root_,name),true); status_="Created "+working_.name+"."; return true; }
    catch (const std::exception& error) { status_="Could not create particle effect: "+std::string(error.what()); return false; }
}
bool ParticleEffectDocument::load(const std::string& id) {
    if (transaction_) { status_="Finish the current particle edit before opening another effect."; return false; }
    try { auto candidate=loadParticleAsset(root_,id); install(std::move(candidate),true); status_="Opened "+working_.name+"."; return true; }
    catch (const std::exception& error) { status_="Could not open particle effect: "+std::string(error.what()); return false; }
}
bool ParticleEffectDocument::save(const std::string& name,bool saveAs) {
    if (!open_) { status_="No particle document is open."; return false; }
    if (transaction_ && !commit()) return false;
    if (!saveAs && !name.empty() && name!=working_.name)
        if (!edit("Rename particle effect",[&](auto& asset){asset.name=name;})) return false;
    try {
        auto saved=saveAs?saveParticleAssetAs(root_,working_,name):working_;
        if (!saveAs) saveParticleAsset(root_,saved);
        const auto fingerprint=fileFingerprint(root_,saved.id);
        if (saveAs) {
            // History belongs to the new document; undo cannot restore the old
            // managed file ID and accidentally overwrite its source asset.
            for (auto& command:history_) { command.before.id=saved.id; command.after.id=saved.id; }
            if (saved.name!=working_.name) {
                auto before=working_; before.id=saved.id;
                Command command{"Rename particle effect",std::move(before),saved,state_,nextState_++};
                history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
                history_.push_back(std::move(command));
                if (history_.size()>MaximumHistoryEntries) history_.erase(history_.begin());
                cursor_=history_.size(); state_=history_.back().afterState;
            }
            working_=saved;
            publish(); ++previewRevision_;
        }
        saved_=working_; savedState_=state_; fingerprint_=fingerprint;
        status_="Saved "+working_.name+".";
        return true;
    } catch (const std::exception& error) {
        status_="Could not save particle effect: "+std::string(error.what());
        return false;
    }
}
bool ParticleEffectDocument::discard() {
    if (!open_) return false;
    transaction_.reset(); working_=saved_; history_.clear(); cursor_=0;
    state_=nextState_++; savedState_=state_;
    publish(); ++previewRevision_; status_="Discarded unsaved particle edits.";
    return true;
}
void ParticleEffectDocument::close() {
    open_=false; transaction_.reset(); history_.clear(); cursor_=0;
    committed_.reset(); working_=defaultParticleEffect(); saved_=working_;
    ++revision_; ++previewRevision_; status_.clear();
}
bool ParticleEffectDocument::externalFileChanged() const {
    if (!open_ || working_.id.empty()) return false;
    try { return fileFingerprint(root_,working_.id)!=fingerprint_; }
    catch (...) { return true; }
}
bool ParticleEffectDocument::begin(const std::string& label) {
    if (!open_ || transaction_) return false;
    transaction_=Transaction{label,working_,false};
    return true;
}
bool ParticleEffectDocument::update(const Mutation& mutation) {
    if (!transaction_) return false;
    try {
        auto candidate=working_; mutation(candidate);
        if (candidate.id!=transaction_->before.id) throw std::runtime_error("A particle edit cannot change its managed identity; use Save As.");
        if (candidate.emitter.id!=transaction_->before.emitter.id) throw std::runtime_error("A particle edit cannot change its persistent emitter identity.");
        validateParticleAsset(candidate);
        transaction_->invalid=false;
        if (candidate!=working_) { working_=std::move(candidate); ++previewRevision_; }
        status_.clear(); return true;
    } catch (const std::exception& error) {
        transaction_->invalid=true;
        status_=error.what();
        return false;
    }
}
bool ParticleEffectDocument::commit() {
    if (!transaction_) return false;
    if (transaction_->invalid) return false;
    if (working_==transaction_->before) { transaction_.reset(); return true; }
    try {
        validateParticleAsset(working_);
        auto snapshot=std::make_shared<const ParticleEffectAsset>(working_);
        Command command{transaction_->label,transaction_->before,working_,state_,nextState_++};
        history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
        history_.push_back(std::move(command));
        if (history_.size()>MaximumHistoryEntries) history_.erase(history_.begin());
        cursor_=history_.size(); state_=history_.back().afterState;
        committed_=std::move(snapshot); ++revision_;
        transaction_.reset(); status_.clear(); return true;
    } catch (const std::exception& error) { status_=error.what(); return false; }
}
void ParticleEffectDocument::cancel() {
    if (!transaction_) return;
    // Preview owners restore these values live; cancellation does not rewind time.
    working_=std::move(transaction_->before); transaction_.reset();
    ++previewRevision_; status_.clear();
}
bool ParticleEffectDocument::edit(const std::string& label,const Mutation& mutation) {
    if (!begin(label)) return false;
    if (!update(mutation)) {
        const auto diagnostic=status_; cancel(); status_=diagnostic; return false;
    }
    return commit();
}
bool ParticleEffectDocument::undo() {
    if (!canUndo()) return false;
    const auto& command=history_[cursor_-1];
    working_=command.before; state_=command.beforeState; --cursor_;
    publish(); ++previewRevision_; status_="Undid "+command.label+"."; return true;
}
bool ParticleEffectDocument::redo() {
    if (!canRedo()) return false;
    const auto& command=history_[cursor_];
    working_=command.after; state_=command.afterState; ++cursor_;
    publish(); ++previewRevision_; status_="Redid "+command.label+"."; return true;
}

} // namespace crimson
