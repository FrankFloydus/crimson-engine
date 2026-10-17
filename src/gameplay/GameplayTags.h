#pragma once
#include <compare>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace crimson {
class GameplayTagContainer;
// Native tag names are dotted identifiers. Identity and matching ignore ASCII
// case; a registry supplies the canonical spelling used for serialization/UI.
class GameplayTag {
public:
    GameplayTag()=default;
    explicit GameplayTag(std::string_view name);
    bool valid() const{return !key_.empty();}
    const std::string& name() const{return name_;}
    bool matches(const GameplayTag& parent) const;
    bool matchesAny(const GameplayTagContainer&,bool exact=false) const;
    GameplayTag parent() const;
    unsigned commonDepth(const GameplayTag&) const;
    bool matchesExact(const GameplayTag& other) const{return valid()&&other.valid()&&*this==other;}
    bool operator==(const GameplayTag& other) const{return key_==other.key_;}
    std::strong_ordering operator<=>(const GameplayTag& other) const{return key_<=>other.key_;}
private:
    std::string name_,key_;
};
class GameplayTagRegistry {
public:
    explicit GameplayTagRegistry(std::vector<GameplayTag> nativeTags={});
    std::optional<GameplayTag> request(std::string_view name) const;
    bool contains(const GameplayTag&) const;
    std::span<const GameplayTag> tags() const{return tags_;}
private:
    std::vector<GameplayTag> tags_;
};
class GameplayTagContainer {
public:
    GameplayTagContainer()=default;
    explicit GameplayTagContainer(std::vector<GameplayTag> tags);
    bool add(const GameplayTag&);
    bool remove(const GameplayTag&);
    bool addLeaf(const GameplayTag&);
    void append(const GameplayTagContainer&);
    bool has(const GameplayTag&,bool exact=false) const;
    bool hasAny(const GameplayTagContainer&,bool exact=false) const;
    bool hasAll(const GameplayTagContainer&,bool exact=false) const;
    GameplayTagContainer filter(const GameplayTagContainer&,bool exact=false) const;
    GameplayTagContainer parents() const;
    std::span<const GameplayTag> tags() const{return tags_;}
    bool empty() const{return tags_.empty();}
    bool operator==(const GameplayTagContainer&) const=default;
private:
    std::vector<GameplayTag> tags_;
};
enum class GameplayTagQueryKind { Empty,AnyTags,AllTags,NoTags,AnyTagsExact,AllTagsExact,AnyExpressions,AllExpressions,NoExpressions };
struct GameplayTagQuery {
    GameplayTagQueryKind kind{GameplayTagQueryKind::Empty};
    GameplayTagContainer tags;
    std::vector<GameplayTagQuery> expressions;
    bool matches(const GameplayTagContainer&) const;
    bool operator==(const GameplayTagQuery&) const=default;
};
void validateGameplayTags(const GameplayTagContainer&,const GameplayTagRegistry&);

namespace stats {
inline const GameplayTag Health{"Stats.Health"},MaxHealth{"Stats.MaxHealth"},Mana{"Stats.Mana"},MaxMana{"Stats.MaxMana"},
    MovementSpeed{"Stats.MovementSpeed"},Vitality{"Stats.Vitality"},Spirit{"Stats.Spirit"},HealthRegen{"Stats.HealthRegen"},ManaRegen{"Stats.ManaRegen"};
}
const GameplayTagRegistry& nativeGameplayTags();
}
