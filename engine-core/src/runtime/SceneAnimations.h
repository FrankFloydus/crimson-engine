#pragma once
#include "scene/SceneExtraction.h"
#include "assets/AnimationAsset.h"
#include <functional>
#include <unordered_map>

namespace crimson {
// Playback clocks belong to the presented scene instance, not the shared model
// asset or document. Edits/saves do not advance or serialize these clocks.
class SceneAnimations {
public:
    using Query=std::function<std::shared_ptr<const AnimationAsset>(const std::string&)>;
    void advance(ExtractedScene&,std::uint64_t documentGeneration,double seconds,const Query& query={});
    void clear();
private:
    struct State {
        std::string asset;
        SceneModelAnimation settings;
        std::shared_ptr<const AnimationAsset> definition;
        double seconds{};
        bool seen{};
    };
    std::unordered_map<EntityId,State> states_;
    std::uint64_t generation_{};
};
}
