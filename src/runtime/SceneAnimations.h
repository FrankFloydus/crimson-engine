#pragma once
#include "scene/SceneExtraction.h"
#include <unordered_map>

namespace crimson {
// Playback clocks belong to the presented scene instance, not the shared model
// asset or document. Edits/saves do not advance or serialize these clocks.
class SceneAnimations {
public:
    void advance(ExtractedScene&,std::uint64_t documentGeneration,double seconds);
    void clear();
private:
    struct State {
        std::string asset;
        SceneModelAnimation settings;
        double seconds{};
        bool seen{};
    };
    std::unordered_map<EntityId,State> states_;
    std::uint64_t generation_{};
};
}
