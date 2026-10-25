#include "MaterialDocument.h"
#include "resources/AssetId.h"
#include <fstream>
#include <algorithm>
#include <stdexcept>

namespace crimson {
namespace {
uint64_t fileFingerprint(const std::filesystem::path& root,const std::string& id) {
    if(isEngineAssetId(id))return 0;
    std::ifstream input(materialAssetPath(root,id),std::ios::binary);
    if (!input) throw std::runtime_error("Material file is unavailable.");
    uint64_t hash=14695981039346656037ull;
    char bytes[4096];
    while (input.read(bytes,sizeof(bytes)) || input.gcount())
        for (std::streamsize i=0;i<input.gcount();++i) { hash^=static_cast<unsigned char>(bytes[i]); hash*=1099511628211ull; }
    if (input.bad()) throw std::runtime_error("Could not fingerprint the complete material file.");
    return hash;
}
std::optional<uint64_t> matchingFileFingerprint(const std::filesystem::path& root,const std::string& id,const std::string& expected) {
    if(isEngineAssetId(id))return {};
    std::ifstream input(materialAssetPath(root,id),std::ios::binary);
    if(!input)return {};
    std::string bytes(expected.size(),'\0');
    input.read(bytes.data(),static_cast<std::streamsize>(bytes.size()));
    if(input.gcount()!=static_cast<std::streamsize>(bytes.size()) || bytes!=expected || input.peek()!=std::char_traits<char>::eof() || input.bad())return {};
    uint64_t hash=14695981039346656037ull;
    for(const unsigned char byte:bytes){hash^=byte;hash*=1099511628211ull;}
    return hash;
}
}

MaterialDocument::MaterialDocument(std::filesystem::path projectRoot)
    :root_(std::filesystem::absolute(std::move(projectRoot)).lexically_normal()) {}
bool MaterialDocument::dirty() const {
    return open_ && (state_!=savedState_ || (transaction_ && transaction_->currentDefinition!=transaction_->beforeDefinition));
}
bool MaterialDocument::editable() const {
    // Imported model sidecars are the authored material, with a stable shared
    // identity. Their model origin is provenance, not an edit restriction.
    return open_ && !isEngineAssetId(working_.id) &&
        (particleMaterialCompatible(working_) || modelMaterialCompatible(working_) || decalMaterialCompatible(working_));
}
std::vector<std::string> MaterialDocument::historicalTextureReferences() const {
    std::vector<std::string> result;
    const auto append=[&](const MaterialAsset& a) {
        for(const auto& id:materialTextureReferences(a))if(std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);
    };
    append(saved_);
    if(transaction_)append(transaction_->before);
    for(const auto& command:history_){append(command.before);append(command.after);}
    return result;
}
std::vector<std::string> MaterialDocument::historicalMaterialReferences() const {
    std::vector<std::string> result;
    const auto append=[&](const MaterialAsset& material) {
        for(const auto& id:materialMaterialReferences(material))if(std::find(result.begin(),result.end(),id)==result.end())result.push_back(id);
    };
    append(saved_);if(transaction_)append(transaction_->before);
    for(const auto& command:history_){append(command.before);append(command.after);}return result;
}
bool MaterialDocument::reconcileTextureReferences(const TextureReferenceResolver& resolve,const MaterialReferenceResolver& resolveMaterial) {
    if(!open_)return true;
    try {
        auto working=working_,saved=saved_;
        auto history=history_;auto transaction=transaction_;
        auto committed=committed_;
        const auto rewrite=[&](MaterialAsset& material) {
            const bool changed=rewriteMaterialAssetReferences(material,resolve,resolveMaterial);
            if(changed)validateMaterialAsset(material);
            return changed;
        };
        const bool workingChanged=rewrite(working),savedChanged=rewrite(saved);
        bool changed=workingChanged||savedChanged,committedChanged=false;
        for(auto& command:history){changed|=rewrite(command.before);changed|=rewrite(command.after);}
        if(transaction) {
            const bool beforeChanged=rewrite(transaction->before);changed|=beforeChanged;
            if(beforeChanged)transaction->beforeDefinition=serializeMaterialAsset(transaction->before);
            if(workingChanged)transaction->currentDefinition=serializeMaterialAsset(working);
        }
        if(committed) {
            auto candidate=*committed;
            committedChanged=rewrite(candidate);changed|=committedChanged;
            if(committedChanged)committed=std::make_shared<const MaterialAsset>(std::move(candidate));
        }
        if(!changed)return true;
        auto fingerprint=fingerprint_;
        if(savedChanged) {
            // Accept only the exact saved rewrite performed by asset deletion.
            // An unrelated external edit must still produce a save conflict.
            try {if(const auto matched=matchingFileFingerprint(root_,saved.id,serializeMaterialAsset(saved)))fingerprint=*matched;}
            catch(const std::exception&) { /* An unavailable file remains an external conflict. */ }
        }
        // Every allocation, resolver call and validation completed above. These
        // swaps install the complete replacement while retaining history tokens.
        std::swap(working_,working);std::swap(saved_,saved);history_.swap(history);transaction_.swap(transaction);committed_.swap(committed);
        fingerprint_=fingerprint;
        if(workingChanged)++previewRevision_;
        if(committedChanged)++revision_;
        if(!transaction_ || !transaction_->invalid)status_.clear();return true;
    } catch(const std::exception& error) {
        status_="Could not reconcile deleted material textures: "+std::string(error.what());return false;
    }
}
std::string MaterialDocument::undoLabel() const { return canUndo()?history_[cursor_-1].label:std::string{}; }
std::string MaterialDocument::redoLabel() const { return canRedo()?history_[cursor_].label:std::string{}; }
void MaterialDocument::publish() {
    committed_=std::make_shared<const MaterialAsset>(working_);
    ++revision_;
}
void MaterialDocument::install(MaterialAsset asset,bool saved) {
    validateMaterialAsset(asset);
    canonicalizeMaterialAsset(asset);
    auto prepared=std::make_shared<const MaterialAsset>(asset);
    const auto fingerprint=saved?fileFingerprint(root_,asset.id):0;
    working_=std::move(asset); saved_=working_; committed_=std::move(prepared);
    history_.clear(); cursor_=0; transaction_.reset();
    state_=nextState_++; savedState_=saved?state_:0; fingerprint_=fingerprint;
    open_=true;staged_=false; ++revision_; ++previewRevision_;
}
bool MaterialDocument::create(const std::string& name) {
    if (transaction_) { status_="Finish the current material edit before creating another material."; return false; }
    try { install(createMaterialAsset(root_,name),true); status_="Created "+working_.name+"."; return true; }
    catch (const std::exception& error) { status_="Could not create material: "+std::string(error.what()); return false; }
}
bool MaterialDocument::createBlend(const std::string& name) {
    if(transaction_){status_="Finish the current material edit before creating another material.";return false;}
    try {install(createBlendMaterialAsset(root_,name),true);status_="Created "+working_.name+".";return true;}
    catch(const std::exception& error){status_="Could not create Blend material: "+std::string(error.what());return false;}
}
bool MaterialDocument::load(const std::string& id) {
    if (transaction_) { status_="Finish the current material edit before opening another material."; return false; }
    try { auto candidate=loadMaterialAsset(root_,id); install(std::move(candidate),true); status_="Opened "+working_.name+"."; return true; }
    catch (const std::exception& error) { status_="Could not open material: "+std::string(error.what()); return false; }
}
bool MaterialDocument::loadStaged(const MaterialAsset& material) {
    if(transaction_){status_="Finish the current material edit before opening another material.";return false;}
    try {install(material,false);savedState_=state_;staged_=true;status_.clear();return true;}
    catch(const std::exception& error){status_="Could not open material: "+std::string(error.what());return false;}
}
bool MaterialDocument::save(const std::string& name,bool saveAs) {
    if (!open_) { status_="No material document is open."; return false; }
    if ((!saveAs && !editable()) || (saveAs && !particleMaterialCompatible(working_) && !modelMaterialCompatible(working_) && !decalMaterialCompatible(working_))) { status_="This material is read-only."; return false; }
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
        saved_=working_; savedState_=state_; fingerprint_=fingerprint;staged_=false;
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
    open_=false;staged_=false; transaction_.reset(); history_.clear(); cursor_=0;
    committed_.reset(); working_=defaultParticleMaterialAsset(); saved_=working_;
    ++revision_; ++previewRevision_; status_.clear();
}
bool MaterialDocument::externalFileChanged() const {
    if (!open_ || working_.id.empty()) return false;
    if(staged_) {
        try {const auto file=materialAssetPath(root_,working_.id);if(!std::filesystem::exists(file))return false;
            return serializeMaterialAsset(loadMaterialAsset(root_,working_.id))!=serializeMaterialAsset(saved_);
        }catch(...){return true;}
    }
    try { return fileFingerprint(root_,working_.id)!=fingerprint_; }
    catch (...) { return true; }
}
bool MaterialDocument::begin(const std::string& label) {
    if (!editable() || transaction_) return false;
    const auto definition=serializeMaterialAsset(working_);
    transaction_=Transaction{label,working_,definition,definition,false};
    return true;
}
bool MaterialDocument::update(const Mutation& mutation) {
    if (!transaction_) return false;
    try {
        auto candidate=working_; mutation(candidate);
        if (candidate.id!=transaction_->before.id) throw std::runtime_error("A material edit cannot change its managed identity; use Save As.");
        if (candidate.originModel!=transaction_->before.originModel) throw std::runtime_error("A material edit cannot change its imported origin.");
        validateMaterialAsset(candidate);
        canonicalizeMaterialAsset(candidate);
        // Canonicalization has already validated the shared mesh/sprite graph.
        // Consumer compatibility performs that same validation again.
        auto definition=serializeMaterialAsset(candidate);
        transaction_->invalid=false;
        if (definition!=transaction_->currentDefinition) {
            working_=std::move(candidate);transaction_->currentDefinition=std::move(definition);++previewRevision_;
        }
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
    if (transaction_->currentDefinition==transaction_->beforeDefinition) { transaction_.reset(); return true; }
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
