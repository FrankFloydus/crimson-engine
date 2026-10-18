#include "GameplayTags.h"
#include <algorithm>
#include <stdexcept>

namespace crimson {
GameplayTag::GameplayTag(std::string_view name):name_(name) {
    if(name.empty()||name.size()>256||name.front()=='.'||name.back()=='.')throw std::invalid_argument("Gameplay tag requires a nonempty dotted name.");
    bool separator=false;key_.reserve(name.size());
    for(const unsigned char value:name) {
        if(value=='.'){if(separator)throw std::invalid_argument("Gameplay tag contains an empty segment.");separator=true;key_.push_back('.');continue;}
        const bool letter=(value>='A'&&value<='Z')||(value>='a'&&value<='z');
        if(!letter&&!(value>='0'&&value<='9')&&value!='_'&&value!='-')throw std::invalid_argument("Gameplay tag segments accept letters, digits, underscores and hyphens.");
        separator=false;key_.push_back(value>='A'&&value<='Z'?static_cast<char>(value-'A'+'a'):static_cast<char>(value));
    }
}
bool GameplayTag::matches(const GameplayTag& parent) const {
    return valid()&&parent.valid()&&key_.starts_with(parent.key_)&&(key_.size()==parent.key_.size()||key_[parent.key_.size()]=='.');
}
bool GameplayTag::matchesAny(const GameplayTagContainer& queries,bool exact) const {
    return std::any_of(queries.tags().begin(),queries.tags().end(),[&](const auto& query){return exact?matchesExact(query):matches(query);});
}
GameplayTag GameplayTag::parent() const {
    const auto separator=name_.rfind('.');return separator==std::string::npos?GameplayTag{}:GameplayTag{std::string_view(name_).substr(0,separator)};
}
unsigned GameplayTag::commonDepth(const GameplayTag& other) const {
    if(!valid()||!other.valid())return 0;unsigned depth=0;std::size_t first=0,second=0;
    while(first<key_.size()&&second<other.key_.size()) {
        const auto endA=key_.find('.',first),endB=other.key_.find('.',second);
        const auto a=std::string_view(key_).substr(first,endA==std::string::npos?key_.size()-first:endA-first);
        const auto b=std::string_view(other.key_).substr(second,endB==std::string::npos?other.key_.size()-second:endB-second);
        if(a!=b)break;++depth;if(endA==std::string::npos||endB==std::string::npos)break;first=endA+1;second=endB+1;
    }
    return depth;
}
GameplayTagRegistry::GameplayTagRegistry(std::vector<GameplayTag> tags) {
    for(auto tag:tags) {
        if(!tag.valid())throw std::invalid_argument("Native gameplay tags cannot be empty.");
        for(;tag.valid();tag=tag.parent()) {
            const auto where=std::lower_bound(tags_.begin(),tags_.end(),tag);
            if(where==tags_.end()||*where!=tag)tags_.insert(where,tag);
        }
    }
}
std::optional<GameplayTag> GameplayTagRegistry::request(std::string_view name) const {
    try{const GameplayTag tag{name};const auto found=std::lower_bound(tags_.begin(),tags_.end(),tag);if(found!=tags_.end()&&*found==tag)return *found;}catch(const std::invalid_argument&){}
    return {};
}
bool GameplayTagRegistry::contains(const GameplayTag& tag) const{return tag.valid()&&std::binary_search(tags_.begin(),tags_.end(),tag);}
GameplayTagContainer::GameplayTagContainer(std::vector<GameplayTag> tags){for(const auto& tag:tags)add(tag);}
bool GameplayTagContainer::add(const GameplayTag& tag) {
    if(!tag.valid())return false;const auto where=std::lower_bound(tags_.begin(),tags_.end(),tag);if(where!=tags_.end()&&*where==tag)return false;tags_.insert(where,tag);return true;
}
bool GameplayTagContainer::remove(const GameplayTag& tag) {
    const auto where=std::lower_bound(tags_.begin(),tags_.end(),tag);if(where==tags_.end()||*where!=tag)return false;tags_.erase(where);return true;
}
bool GameplayTagContainer::addLeaf(const GameplayTag& tag) {
    if(!tag.valid())return false;
    if(has(tag,true))return true;
    if(std::any_of(tags_.begin(),tags_.end(),[&](const auto& value){return value.matches(tag);}))return false;
    std::erase_if(tags_,[&](const auto& value){return tag.matches(value);});return add(tag);
}
void GameplayTagContainer::append(const GameplayTagContainer& other){for(const auto& tag:other.tags_)add(tag);}
bool GameplayTagContainer::has(const GameplayTag& tag,bool exact) const {
    if(!tag.valid())return false;return exact?std::binary_search(tags_.begin(),tags_.end(),tag):std::any_of(tags_.begin(),tags_.end(),[&](const auto& value){return value.matches(tag);});
}
bool GameplayTagContainer::hasAny(const GameplayTagContainer& other,bool exact) const{return std::any_of(other.tags_.begin(),other.tags_.end(),[&](const auto& tag){return has(tag,exact);});}
bool GameplayTagContainer::hasAll(const GameplayTagContainer& other,bool exact) const{return std::all_of(other.tags_.begin(),other.tags_.end(),[&](const auto& tag){return has(tag,exact);});}
GameplayTagContainer GameplayTagContainer::filter(const GameplayTagContainer& other,bool exact) const {
    GameplayTagContainer result;for(const auto& tag:tags_)if(std::any_of(other.tags_.begin(),other.tags_.end(),[&](const auto& query){return exact?tag==query:tag.matches(query);}))result.add(tag);return result;
}
GameplayTagContainer GameplayTagContainer::parents() const {
    GameplayTagContainer result;for(auto tag:tags_)for(;tag.valid();tag=tag.parent())result.add(tag);return result;
}
bool GameplayTagQuery::matches(const GameplayTagContainer& value) const {
    switch(kind) {
    case GameplayTagQueryKind::AnyTags:return value.hasAny(tags);
    case GameplayTagQueryKind::AllTags:return value.hasAll(tags);
    case GameplayTagQueryKind::NoTags:return !value.hasAny(tags);
    case GameplayTagQueryKind::AnyTagsExact:return value.hasAny(tags,true);
    case GameplayTagQueryKind::AllTagsExact:return value.hasAll(tags,true);
    case GameplayTagQueryKind::AnyExpressions:return std::any_of(expressions.begin(),expressions.end(),[&](const auto& query){return query.matches(value);});
    case GameplayTagQueryKind::AllExpressions:return std::all_of(expressions.begin(),expressions.end(),[&](const auto& query){return query.matches(value);});
    case GameplayTagQueryKind::NoExpressions:return std::none_of(expressions.begin(),expressions.end(),[&](const auto& query){return query.matches(value);});
    default:return false;
    }
}
void validateGameplayTags(const GameplayTagContainer& container,const GameplayTagRegistry& registry) {
    for(const auto& tag:container.tags())if(!registry.contains(tag))throw std::invalid_argument("Unregistered gameplay tag: "+tag.name());
}
const GameplayTagRegistry& nativeGameplayTags() {
    static const GameplayTagRegistry value({stats::Health,stats::MaxHealth,stats::Mana,stats::MaxMana,stats::MovementSpeed,stats::Vitality,stats::Spirit,stats::HealthRegen,stats::ManaRegen});return value;
}
}
