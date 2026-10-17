#include "CollisionLayersDocument.h"
#include "assets/AtomicFile.h"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>

namespace crimson {
namespace {
using Json=nlohmann::json;
constexpr std::size_t MaximumFileBytes=65536;
void validateNames(const CollisionLayerNames& names) {
    for(const auto& name:names)if(name.size()>128||std::any_of(name.begin(),name.end(),[](unsigned char byte){return byte<32||byte==127;}))
        throw std::runtime_error("Collision layer names support at most 128 UTF-8 bytes without control characters.");
}
std::uint64_t fingerprint(std::string_view text) {
    std::uint64_t result=14695981039346656037ull;
    for(const unsigned char byte:text){result^=byte;result*=1099511628211ull;}return result;
}
std::optional<std::string> readFile(const std::filesystem::path& file) {
    if(!std::filesystem::exists(file))return {};
    if(!std::filesystem::is_regular_file(file)||std::filesystem::file_size(file)>MaximumFileBytes)
        throw std::runtime_error("Collision layers require a regular JSON file no larger than 64 KiB.");
    std::ifstream input(file,std::ios::binary);if(!input)throw std::runtime_error("Cannot read collision layers.");
    std::string text{std::istreambuf_iterator<char>(input),{}};
    if(input.bad()||text.size()>MaximumFileBytes)throw std::runtime_error("Could not read the complete collision layer file.");
    return text;
}
std::optional<std::uint64_t> fileFingerprint(const std::filesystem::path& file) {
    const auto text=readFile(file);return text?std::optional{fingerprint(*text)}:std::nullopt;
}
CollisionLayerNames parseNames(const std::string& text) {
    const auto json=Json::parse(text);
    if(!json.is_object()||json.at("version")!=1||!json.at("names").is_array()||json.at("names").size()!=32)
        throw std::runtime_error("Collision layers require version 1 with 32 fixed names.");
    auto names=json.at("names").get<CollisionLayerNames>();validateNames(names);return names;
}
}
const CollisionLayerNames& defaultCollisionLayerNames() {
    static const CollisionLayerNames names{"World","Player","Enemy","Trigger","Character"};return names;
}
CollisionLayersDocument::CollisionLayersDocument(std::filesystem::path root)
    :file_(std::filesystem::absolute(std::move(root)).lexically_normal()/".crimson/collision-layers.json"),committed_(std::make_shared<const Names>(working_)) {
    reload(true);
}
std::string CollisionLayersDocument::name(unsigned slot)const {
    if(slot>=working_.size())throw std::out_of_range("Collision layer slot is outside the fixed 32 layers.");
    return working_[slot].empty()?"Layer "+std::to_string(slot+1):working_[slot];
}
bool CollisionLayersDocument::dirty()const {return state_!=savedState_||(transaction_&&working_!=transaction_->before);}
std::string CollisionLayersDocument::undoLabel()const {return canUndo()?history_[cursor_-1].label:std::string{};}
std::string CollisionLayersDocument::redoLabel()const {return canRedo()?history_[cursor_].label:std::string{};}
bool CollisionLayersDocument::begin(const std::string& label) {
    if(transaction_){status_="Finish the current collision layer edit first.";return false;}
    try{transaction_=Transaction{label,working_,false};status_.clear();return true;}
    catch(const std::exception& error){status_=error.what();return false;}
}
bool CollisionLayersDocument::update(const Mutation& mutation) {
    if(!transaction_)return false;
    try{auto candidate=working_;mutation(candidate);validateNames(candidate);working_=std::move(candidate);transaction_->invalid=false;status_.clear();return true;}
    catch(const std::exception& error){transaction_->invalid=true;status_=error.what();return false;}
}
bool CollisionLayersDocument::updateName(unsigned slot,const std::string& value) {
    return update([&](auto& names){if(slot>=names.size())throw std::out_of_range("Collision layer slot is outside the fixed 32 layers.");names[slot]=value;});
}
bool CollisionLayersDocument::commit() {
    if(!transaction_||transaction_->invalid)return false;
    if(working_==transaction_->before){transaction_.reset();return true;}
    try {
        auto snapshot=std::make_shared<const Names>(working_);auto commands=history_;
        commands.erase(commands.begin()+static_cast<std::ptrdiff_t>(cursor_),commands.end());
        commands.push_back({transaction_->label,transaction_->before,working_,state_,nextState_});
        if(commands.size()>MaximumHistoryEntries)commands.erase(commands.begin());
        history_=std::move(commands);cursor_=history_.size();state_=nextState_++;committed_=std::move(snapshot);++revision_;
        transaction_.reset();status_.clear();return true;
    }catch(const std::exception& error){status_=error.what();return false;}
}
void CollisionLayersDocument::cancel() {
    if(!transaction_)return;working_=std::move(transaction_->before);transaction_.reset();status_.clear();
}
bool CollisionLayersDocument::edit(const std::string& label,const Mutation& mutation) {
    if(!begin(label))return false;
    if(!update(mutation)){const auto error=status_;cancel();status_=error;return false;}return commit();
}
bool CollisionLayersDocument::setName(unsigned slot,const std::string& value) {
    if(slot<working_.size()&&working_[slot]==value&&!transaction_)return false;
    return edit("Rename collision layer",[&](auto& names){if(slot>=names.size())throw std::out_of_range("Collision layer slot is outside the fixed 32 layers.");names[slot]=value;});
}
bool CollisionLayersDocument::undo() {
    if(!canUndo())return false;
    try{auto candidate=history_[cursor_-1].before;auto snapshot=std::make_shared<const Names>(candidate);working_=std::move(candidate);state_=history_[--cursor_].beforeState;committed_=std::move(snapshot);++revision_;status_.clear();return true;}
    catch(const std::exception& error){status_=error.what();return false;}
}
bool CollisionLayersDocument::redo() {
    if(!canRedo())return false;
    try{auto candidate=history_[cursor_].after;auto snapshot=std::make_shared<const Names>(candidate);working_=std::move(candidate);state_=history_[cursor_++].afterState;committed_=std::move(snapshot);++revision_;status_.clear();return true;}
    catch(const std::exception& error){status_=error.what();return false;}
}
bool CollisionLayersDocument::externalFileChanged()const {
    try{return fileFingerprint(file_)!=fingerprint_;}catch(...){return true;}
}
bool CollisionLayersDocument::checkExternalConflict() {
    if(!externalFileChanged())return false;
    status_="Collision layers changed outside the editor. Reload them or explicitly overwrite the external file.";return true;
}
bool CollisionLayersDocument::reload(bool discardUnsaved) {
    if(!discardUnsaved&&(dirty()||transaction_)){status_="Resolve unsaved collision layer edits before reloading.";return false;}
    try {
        const auto text=readFile(file_);auto candidate=text?parseNames(*text):defaultCollisionLayerNames();
        auto snapshot=std::make_shared<const Names>(candidate);const auto nextFingerprint=text?std::optional{fingerprint(*text)}:std::nullopt;
        working_=std::move(candidate);committed_=std::move(snapshot);fingerprint_=nextFingerprint;
        transaction_.reset();history_.clear();cursor_=0;state_=nextState_++;savedState_=state_;++revision_;status_.clear();return true;
    }catch(const std::exception& error){status_="Could not load collision layers: "+std::string(error.what());return false;}
}
bool CollisionLayersDocument::save(bool overwriteExternal) {
    if(transaction_&&!commit())return false;
    try {
        validateNames(working_);const auto text=Json{{"version",1},{"names",working_}}.dump(2)+'\n';
        if(!overwriteExternal&&fileFingerprint(file_)!=fingerprint_)throw std::runtime_error("Collision layers changed outside the editor; reload or explicitly overwrite the external file.");
        writeFileAtomically(file_,text);fingerprint_=fingerprint(text);savedState_=state_;status_="Saved collision layers.";return true;
    }catch(const std::exception& error){status_="Could not save collision layers: "+std::string(error.what());return false;}
}
}
