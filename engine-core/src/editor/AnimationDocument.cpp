#include "AnimationDocument.h"
#include <algorithm>
#include <fstream>
#include <stdexcept>

namespace crimson {
namespace {
std::uint64_t fileFingerprint(const std::filesystem::path& root,const std::string& id) {
    std::ifstream input(animationAssetPath(root,id),std::ios::binary);
    if(!input)throw std::runtime_error("Animation file is unavailable.");
    std::uint64_t result=14695981039346656037ull;
    char bytes[4096];
    while(input.read(bytes,sizeof(bytes))||input.gcount())
        for(std::streamsize i=0;i<input.gcount();++i){result^=static_cast<unsigned char>(bytes[i]);result*=1099511628211ull;}
    if(input.bad())throw std::runtime_error("Could not read the complete Animation file.");
    return result;
}
}
AnimationDocument::AnimationDocument(std::filesystem::path projectRoot)
    :root_(std::filesystem::absolute(std::move(projectRoot)).lexically_normal()) {}
bool AnimationDocument::dirty() const {return open_&&(state_!=savedState_||(transaction_&&working_!=transaction_->before));}
std::string AnimationDocument::undoLabel() const {return canUndo()?history_[cursor_-1].label:std::string{};}
std::string AnimationDocument::redoLabel() const {return canRedo()?history_[cursor_].label:std::string{};}
void AnimationDocument::publish(){committed_=std::make_shared<const AnimationAsset>(working_);++revision_;}
void AnimationDocument::install(AnimationAsset asset) {
    validateAnimationAsset(asset);
    const auto fingerprint=fileFingerprint(root_,asset.id);
    auto snapshot=std::make_shared<const AnimationAsset>(asset);
    working_=std::move(asset);saved_=working_;committed_=std::move(snapshot);
    transaction_.reset();history_.clear();cursor_=0;state_=nextState_++;savedState_=state_;fingerprint_=fingerprint;
    open_=true;++revision_;++previewRevision_;
}
bool AnimationDocument::load(const std::string& id) {
    if(transaction_){status_="Finish the current Animation edit before opening another animation.";return false;}
    try{install(loadAnimationAsset(root_,id));status_.clear();return true;}
    catch(const std::exception& error){status_="Could not open Animation: "+std::string(error.what());return false;}
}
bool AnimationDocument::save(const std::string& name,bool saveAs) {
    if(!open_){status_="No Animation document is open.";return false;}
    if(transaction_&&!commit())return false;
    if(!saveAs&&!name.empty()&&name!=working_.name)
        if(!edit("Rename animation",[&](auto& value){value.name=name;}))return false;
    try {
        auto saved=saveAs?copyAnimationAsset(root_,working_,name):working_;
        if(!saveAs)saveAnimationAsset(root_,saved);
        const auto fingerprint=fileFingerprint(root_,saved.id);
        if(saveAs) {
            for(auto& command:history_){command.before.id=saved.id;command.after.id=saved.id;}
            if(saved.name!=working_.name) {
                auto before=working_;before.id=saved.id;
                Command command{"Rename animation",std::move(before),saved,state_,nextState_++};
                history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());
                history_.push_back(std::move(command));
                if(history_.size()>MaximumHistoryEntries)history_.erase(history_.begin());
                cursor_=history_.size();state_=history_.back().afterState;
            }
            working_=saved;publish();++previewRevision_;
        }
        saved_=working_;savedState_=state_;fingerprint_=fingerprint;status_.clear();return true;
    }catch(const std::exception& error){status_="Could not save Animation: "+std::string(error.what());return false;}
}
bool AnimationDocument::discard() {
    if(!open_)return false;
    transaction_.reset();working_=saved_;history_.clear();cursor_=0;state_=nextState_++;savedState_=state_;
    publish();++previewRevision_;status_.clear();return true;
}
void AnimationDocument::close() {
    open_=false;transaction_.reset();history_.clear();cursor_=0;working_={};saved_={};committed_.reset();
    ++revision_;++previewRevision_;status_.clear();
}
bool AnimationDocument::externalFileChanged() const {
    if(!open_)return false;
    try{return fileFingerprint(root_,working_.id)!=fingerprint_;}catch(...){return true;}
}
bool AnimationDocument::begin(const std::string& label) {
    if(!open_||transaction_)return false;
    transaction_=Transaction{label,working_,false};return true;
}
bool AnimationDocument::update(const Mutation& mutation) {
    if(!transaction_)return false;
    try {
        auto candidate=working_;mutation(candidate);
        const auto& before=transaction_->before;
        if(candidate.id!=before.id)throw std::runtime_error("An Animation edit cannot change its identity; use Save As.");
        if(candidate.sourceModel!=before.sourceModel||candidate.sourceClip!=before.sourceClip||
           candidate.sourceClipIdentity!=before.sourceClipIdentity||candidate.bindingSignature!=before.bindingSignature)
            throw std::runtime_error("Changing an Animation's imported source requires an explicit source update.");
        validateAnimationAsset(candidate);transaction_->invalid=false;
        if(candidate!=working_){working_=std::move(candidate);++previewRevision_;}
        status_.clear();return true;
    }catch(const std::exception& error){transaction_->invalid=true;status_=error.what();return false;}
}
bool AnimationDocument::commit() {
    if(!transaction_||transaction_->invalid)return false;
    if(working_==transaction_->before){transaction_.reset();return true;}
    try {
        validateAnimationAsset(working_);
        auto snapshot=std::make_shared<const AnimationAsset>(working_);
        Command command{transaction_->label,transaction_->before,working_,state_,nextState_++};
        history_.erase(history_.begin()+static_cast<std::ptrdiff_t>(cursor_),history_.end());history_.push_back(std::move(command));
        if(history_.size()>MaximumHistoryEntries)history_.erase(history_.begin());
        cursor_=history_.size();state_=history_.back().afterState;committed_=std::move(snapshot);++revision_;
        transaction_.reset();status_.clear();return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
void AnimationDocument::cancel() {
    if(!transaction_)return;
    working_=std::move(transaction_->before);transaction_.reset();++previewRevision_;status_.clear();
}
bool AnimationDocument::edit(const std::string& label,const Mutation& mutation) {
    if(!begin(label))return false;
    if(!update(mutation)){const auto message=status_;cancel();status_=message;return false;}
    return commit();
}
bool AnimationDocument::undo() {
    if(!canUndo())return false;
    const auto& command=history_[cursor_-1];working_=command.before;state_=command.beforeState;--cursor_;
    publish();++previewRevision_;status_.clear();return true;
}
bool AnimationDocument::redo() {
    if(!canRedo())return false;
    const auto& command=history_[cursor_];working_=command.after;state_=command.afterState;++cursor_;
    publish();++previewRevision_;status_.clear();return true;
}
std::vector<std::string> AnimationDocument::modelReferences(bool history) const {
    std::vector<std::string> result;
    const auto add=[&](const AnimationAsset& value){if(!value.sourceModel.id.empty())result.push_back(value.sourceModel.id);};
    if(open_){if(history){add(saved_);for(const auto& command:history_){add(command.before);add(command.after);}if(transaction_)add(transaction_->before);}else add(working_);}
    std::sort(result.begin(),result.end());result.erase(std::unique(result.begin(),result.end()),result.end());return result;
}
}
