#include "MaterialDocument.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace crimson {
namespace {
bool equal(const MaterialAsset& a,const MaterialAsset& b) { return serializeMaterialAsset(a)==serializeMaterialAsset(b); }
uint64_t fileFingerprint(const std::filesystem::path& root,const std::string& id) {
    if(id.starts_with("builtin:"))return 0;
    std::ifstream input(materialAssetPath(root,id),std::ios::binary);
    if (!input) throw std::runtime_error("Material file is unavailable.");
    uint64_t hash=14695981039346656037ull;
    char bytes[4096];
    while (input.read(bytes,sizeof(bytes)) || input.gcount())
        for (std::streamsize i=0;i<input.gcount();++i) { hash^=static_cast<unsigned char>(bytes[i]); hash*=1099511628211ull; }
    if (input.bad()) throw std::runtime_error("Could not fingerprint the complete material file.");
    return hash;
}
}

MaterialDocument::MaterialDocument(std::filesystem::path projectRoot)
    :root_(std::filesystem::absolute(std::move(projectRoot)).lexically_normal()) {}
bool MaterialDocument::dirty() const {
    return open_ && (state_!=savedState_ || (transaction_ && !equal(working_,transaction_->before)));
}
bool MaterialDocument::editable() const {
    return open_ && working_.originModel.empty() && !working_.id.starts_with("builtin:") &&
        (particleMaterialCompatible(working_) || modelMaterialCompatible(working_));
}
std::vector<std::string> MaterialDocument::historicalTextureReferences() const {
    std::vector<std::string> result;
    const auto append=[&](const MaterialAsset& a) {
        for (const auto* t:{&a.surface.baseColorTexture,&a.surface.emissiveTexture,&a.surface.metallicRoughnessTexture,&a.surface.normalTexture,&a.surface.occlusionTexture})
            if (!t->image.id.empty() && std::find(result.begin(),result.end(),t->image.id)==result.end()) result.push_back(t->image.id);
    };
    append(saved_);
    if(transaction_)append(transaction_->before);
    for(const auto& command:history_){append(command.before);append(command.after);}
    return result;
}
std::string MaterialDocument::undoLabel() const { return canUndo()?history_[cursor_-1].label:std::string{}; }
std::string MaterialDocument::redoLabel() const { return canRedo()?history_[cursor_].label:std::string{}; }
void MaterialDocument::publish() {
    committed_=std::make_shared<const MaterialAsset>(working_);
    ++revision_;
}
void MaterialDocument::install(MaterialAsset asset,bool saved) {
    validateMaterialAsset(asset);
    auto prepared=std::make_shared<const MaterialAsset>(asset);
    const auto fingerprint=saved?fileFingerprint(root_,asset.id):0;
    working_=std::move(asset); saved_=working_; committed_=std::move(prepared);
    history_.clear(); cursor_=0; transaction_.reset();
    state_=nextState_++; savedState_=saved?state_:0; fingerprint_=fingerprint;
    open_=true; ++revision_; ++previewRevision_;
}
bool MaterialDocument::create(const std::string& name) {
    if (transaction_) { status_="Finish the current material edit before creating another material."; return false; }
    try { install(createMaterialAsset(root_,name),true); status_="Created "+working_.name+"."; return true; }
    catch (const std::exception& error) { status_="Could not create material: "+std::string(error.what()); return false; }
}
bool MaterialDocument::load(const std::string& id) {
    if (transaction_) { status_="Finish the current material edit before opening another material."; return false; }
    try { auto candidate=loadMaterialAsset(root_,id); install(std::move(candidate),true); status_="Opened "+working_.name+"."; return true; }
    catch (const std::exception& error) { status_="Could not open material: "+std::string(error.what()); return false; }
}
bool MaterialDocument::save(const std::string& name,bool saveAs) {
    if (!open_) { status_="No material document is open."; return false; }
    if ((!saveAs && !editable()) || (saveAs && !particleMaterialCompatible(working_) && !modelMaterialCompatible(working_))) { status_="This material is read-only."; return false; }
    if (transaction_ && !commit()) return false;
    if (!saveAs && !name.empty() && name!=working_.name)
        if (!edit("Rename material",[&](auto& asset){asset.name=name;})) return false;
    try {
        auto saved=saveAs?copyMaterialAsset(root_,working_,name):working_;
        if (!saveAs) saveMaterialAsset(root_,saved);
        const auto fingerprint=fileFingerprint(root_,saved.id);
        if (saveAs) {
            // History belongs to the new document; undo cannot restore the old
            // managed file ID and accidentally overwrite its source asset.
            for (auto& command:history_) { command.before.id=saved.id; command.after.id=saved.id; command.before.originModel.clear(); command.after.originModel.clear(); }
            if (saved.name!=working_.name) {
                auto before=working_; before.id=saved.id;before.originModel.clear();
                Command command{"Rename material",std::move(before),saved,state_,nextState_++};
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
        status_="Could not save material: "+std::string(error.what());
        return false;
    }
}
bool MaterialDocument::discard() {
    if (!open_) return false;
    transaction_.reset(); working_=saved_; history_.clear(); cursor_=0;
    state_=nextState_++; savedState_=state_;
    publish(); ++previewRevision_; status_="Discarded unsaved material edits.";
    return true;
}
void MaterialDocument::close() {
    open_=false; transaction_.reset(); history_.clear(); cursor_=0;
    committed_.reset(); working_=defaultParticleMaterialAsset(); saved_=working_;
    ++revision_; ++previewRevision_; status_.clear();
}
bool MaterialDocument::externalFileChanged() const {
    if (!open_ || working_.id.empty()) return false;
    try { return fileFingerprint(root_,working_.id)!=fingerprint_; }
    catch (...) { return true; }
}
bool MaterialDocument::begin(const std::string& label) {
    if (!editable() || transaction_) return false;
    transaction_=Transaction{label,working_,false};
    return true;
}
bool MaterialDocument::update(const Mutation& mutation) {
    if (!transaction_) return false;
    try {
        auto candidate=working_; mutation(candidate);
        if (candidate.id!=transaction_->before.id) throw std::runtime_error("A material edit cannot change its managed identity; use Save As.");
        if (candidate.originModel!=transaction_->before.originModel) throw std::runtime_error("A material edit cannot change its imported origin.");
        if (!particleMaterialCompatible(candidate) && !modelMaterialCompatible(candidate)) throw std::runtime_error("The material shading and blend modes are incompatible.");
        candidate.surface.name=candidate.name;
        validateMaterialAsset(candidate);
        transaction_->invalid=false;
        if (!equal(candidate,working_)) { working_=std::move(candidate); ++previewRevision_; }
        status_.clear(); return true;
    } catch (const std::exception& error) {
        transaction_->invalid=true;
        status_=error.what();
        return false;
    }
}
bool MaterialDocument::commit() {
    if (!transaction_) return false;
    if (transaction_->invalid) return false;
    if (equal(working_,transaction_->before)) { transaction_.reset(); return true; }
    try {
        validateMaterialAsset(working_);
        auto snapshot=std::make_shared<const MaterialAsset>(working_);
        Command command{transaction_->label,transaction_->before,working_,state_,nextState_++};
        history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
        history_.push_back(std::move(command));
        if (history_.size()>MaximumHistoryEntries) history_.erase(history_.begin());
        cursor_=history_.size(); state_=history_.back().afterState;
        committed_=std::move(snapshot); ++revision_;
        transaction_.reset(); status_.clear(); return true;
    } catch (const std::exception& error) { status_=error.what(); return false; }
}
void MaterialDocument::cancel() {
    if (!transaction_) return;
    // Cancellation preserves the last committed shared revision.
    working_=std::move(transaction_->before); transaction_.reset();
    ++previewRevision_; status_.clear();
}
bool MaterialDocument::edit(const std::string& label,const Mutation& mutation) {
    if (!begin(label)) return false;
    if (!update(mutation)) {
        const auto diagnostic=status_; cancel(); status_=diagnostic; return false;
    }
    return commit();
}
bool MaterialDocument::undo() {
    if (!canUndo()) return false;
    const auto& command=history_[cursor_-1];
    working_=command.before; state_=command.beforeState; --cursor_;
    publish(); ++previewRevision_; status_="Undid "+command.label+"."; return true;
}
bool MaterialDocument::redo() {
    if (!canRedo()) return false;
    const auto& command=history_[cursor_];
    working_=command.after; state_=command.afterState; ++cursor_;
    publish(); ++previewRevision_; status_="Redid "+command.label+"."; return true;
}

} // namespace crimson
