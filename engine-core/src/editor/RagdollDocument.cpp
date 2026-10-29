#include "RagdollDocument.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
std::uint64_t fileFingerprint(const std::filesystem::path& root,const std::string& id) {
    std::ifstream input(ragdollAssetPath(root,id),std::ios::binary);
    if(!input)throw std::runtime_error("Ragdoll file is unavailable.");
    std::uint64_t result=14695981039346656037ull;
    char bytes[4096];
    while(input.read(bytes,sizeof(bytes))||input.gcount())
        for(std::streamsize i=0;i<input.gcount();++i){result^=static_cast<unsigned char>(bytes[i]);result*=1099511628211ull;}
    if(input.bad())throw std::runtime_error("Could not read the complete Ragdoll file.");
    return result;
}
}
RagdollDocument::RagdollDocument(std::filesystem::path projectRoot)
    :root_(std::filesystem::absolute(std::move(projectRoot)).lexically_normal()) {}
bool RagdollDocument::dirty() const {return open_&&(state_!=savedState_||(transaction_&&working_!=transaction_->before));}
std::string RagdollDocument::undoLabel() const {return canUndo()?history_[cursor_-1].label:std::string{};}
std::string RagdollDocument::redoLabel() const {return canRedo()?history_[cursor_].label:std::string{};}
void RagdollDocument::publish(){committed_=std::make_shared<const RagdollAsset>(working_);++revision_;}
void RagdollDocument::install(RagdollAsset asset) {
    validateRagdollAsset(asset);
    const auto fingerprint=fileFingerprint(root_,asset.id);
    auto snapshot=std::make_shared<const RagdollAsset>(asset);
    working_=std::move(asset);saved_=working_;committed_=std::move(snapshot);
    transaction_.reset();history_.clear();cursor_=0;state_=nextState_++;savedState_=state_;fingerprint_=fingerprint;
    open_=true;hasSaved_=true;++revision_;++previewRevision_;
}
bool RagdollDocument::create(RagdollAsset asset) {
    if(transaction_){status_="Finish the current Ragdoll edit before creating another ragdoll.";return false;}
    try{validateRagdollAsset(asset);working_=std::move(asset);saved_=working_;transaction_.reset();history_.clear();cursor_=0;
        state_=nextState_++;savedState_=0;fingerprint_=0;open_=true;hasSaved_=false;publish();++previewRevision_;status_.clear();return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
bool RagdollDocument::load(const std::string& id) {
    if(transaction_){status_="Finish the current Ragdoll edit before opening another ragdoll.";return false;}
    try{install(loadRagdollAsset(root_,id));status_.clear();return true;}
    catch(const std::exception& error){status_="Could not open Ragdoll: "+std::string(error.what());return false;}
}
bool RagdollDocument::save(const std::string& name,bool saveAs,bool overwriteExternal) {
    if(!open_){status_="No Ragdoll document is open.";return false;}
    if(!saveAs&&!overwriteExternal&&hasSaved_&&externalFileChanged()){status_="Ragdoll changed on disk; reload it, explicitly overwrite it, or use Save As before saving.";return false;}
    if(transaction_&&!commit())return false;
    if(!saveAs&&!name.empty()&&name!=working_.name)
        if(!edit("Rename ragdoll",[&](auto& value){value.name=name;}))return false;
    try {
        auto saved=saveAs?copyRagdollAsset(root_,working_,name):working_;
        if(!saveAs)saveRagdollAsset(root_,saved);
        const auto fingerprint=fileFingerprint(root_,saved.id);
        if(saveAs) {
            for(auto& command:history_){command.before.id=saved.id;command.after.id=saved.id;}
            if(saved.name!=working_.name) {
                auto before=working_;before.id=saved.id;
                Command command{"Rename ragdoll",std::move(before),saved,state_,nextState_++};
                history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
                history_.push_back(std::move(command));
                if(history_.size()>MaximumHistoryEntries)history_.erase(history_.begin());
                cursor_=history_.size();state_=history_.back().afterState;
            }
            working_=saved;publish();++previewRevision_;
        }
        saved_=working_;savedState_=state_;fingerprint_=fingerprint;hasSaved_=true;status_.clear();return true;
    }catch(const std::exception& error){status_="Could not save Ragdoll: "+std::string(error.what());return false;}
}
bool RagdollDocument::discard() {
    if(!open_||!hasSaved_)return false;
    transaction_.reset();working_=saved_;history_.clear();cursor_=0;state_=nextState_++;savedState_=state_;
    publish();++previewRevision_;status_.clear();return true;
}
void RagdollDocument::close() {
    open_=false;hasSaved_=false;transaction_.reset();history_.clear();cursor_=0;working_={};saved_={};committed_.reset();
    ++revision_;++previewRevision_;status_.clear();
}
bool RagdollDocument::externalFileChanged() const {
    if(!open_||!hasSaved_)return false;
    try{return fileFingerprint(root_,working_.id)!=fingerprint_;}catch(...){return true;}
}
bool RagdollDocument::begin(const std::string& label) {
    if(!open_||transaction_)return false;
    transaction_=Transaction{label,working_,false,nextTransaction_++};return true;
}
bool RagdollDocument::update(const Mutation& mutation) {
    if(!transaction_)return false;
    try {
        auto candidate=working_;mutation(candidate);
        const auto& before=transaction_->before;
        if(candidate.id!=before.id)throw std::runtime_error("A Ragdoll edit cannot change its identity; use Save As.");
        if(!allowSourceChange_&&(candidate.sourceModel!=before.sourceModel||candidate.skin!=before.skin||candidate.bindingSignature!=before.bindingSignature))
            throw std::runtime_error("Changing a Ragdoll's imported source requires an explicit source update.");
        validateRagdollAsset(candidate);transaction_->invalid=false;
        if(candidate!=working_){working_=std::move(candidate);++previewRevision_;}
        status_.clear();return true;
    }catch(const std::exception& error){transaction_->invalid=true;status_=error.what();return false;}
}
bool RagdollDocument::rebindSource(const ModelAsset& model,std::uint32_t skin,const std::vector<std::uint32_t>& mappedNodes) {
    if(!open_||transaction_)return false;
    auto candidate=working_;
    try{rebindRagdollSource(candidate,model,skin,mappedNodes);}catch(const std::exception& error){status_=error.what();return false;}
    allowSourceChange_=true;const bool success=edit("Remap ragdoll source",[&](auto& value){value=candidate;});allowSourceChange_=false;return success;
}
bool RagdollDocument::commit() {
    if(!transaction_||transaction_->invalid)return false;
    if(working_==transaction_->before){transaction_.reset();return true;}
    try {
        validateRagdollAsset(working_);
        auto snapshot=std::make_shared<const RagdollAsset>(working_);
        Command command{transaction_->label,transaction_->before,working_,state_,nextState_++};
        history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());history_.push_back(std::move(command));
        if(history_.size()>MaximumHistoryEntries)history_.erase(history_.begin());
        cursor_=history_.size();state_=history_.back().afterState;committed_=std::move(snapshot);++revision_;
        transaction_.reset();status_.clear();return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
void RagdollDocument::cancel() {
    if(!transaction_)return;
    working_=std::move(transaction_->before);transaction_.reset();++previewRevision_;status_.clear();
}
bool RagdollDocument::edit(const std::string& label,const Mutation& mutation) {
    if(!begin(label))return false;
    if(!update(mutation)){const auto message=status_;cancel();status_=message;return false;}
    return commit();
}
bool RagdollDocument::undo() {
    if(!canUndo())return false;
    const auto& command=history_[cursor_-1];working_=command.before;state_=command.beforeState;--cursor_;
    publish();++previewRevision_;status_.clear();return true;
}
bool RagdollDocument::redo() {
    if(!canRedo())return false;
    const auto& command=history_[cursor_];working_=command.after;state_=command.afterState;++cursor_;
    publish();++previewRevision_;status_.clear();return true;
}
std::vector<std::string> RagdollDocument::modelReferences(bool history) const {
    std::vector<std::string> result;
    const auto add=[&](const RagdollAsset& value){if(!value.sourceModel.id.empty())result.push_back(value.sourceModel.id);};
    if(open_){add(working_);if(history){add(saved_);for(const auto& command:history_){add(command.before);add(command.after);}if(transaction_)add(transaction_->before);}}
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
}
