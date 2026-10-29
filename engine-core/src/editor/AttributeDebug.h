#pragma once
#include "scene/Scene.h"
#include <cstdint>
#include <string>
#include <vector>

namespace crimson {
class PlaySession;
struct AttributeDebugRow {std::string name;double current{};};

// Transient Game-view targeting only. Authored data and editor selection are
// never modified; callers reset this controller at Play/Stop transitions.
class AttributeDebug {
public:
    void reset();
    void toggle(const PlaySession&);
    void update(const PlaySession&);
    bool enabled() const{return enabled_;}
    EntityId target() const{return target_;}
    void cycle(const PlaySession&,bool backwards);
    std::vector<AttributeDebugRow> rows(const PlaySession&) const;
private:
    const PlaySession* play_{};
    std::uint64_t generation_{};
    EntityId target_{};
    bool enabled_{};
};
}
