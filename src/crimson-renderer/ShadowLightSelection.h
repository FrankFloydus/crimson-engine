#pragma once

#include "RenderTypes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace crimson {

// Direct-light storage and shadow residency are separate budgets. All callers
// use this selection for both caster preparation and source-to-shadow mapping.
inline constexpr std::uint32_t PointShadowLightBudget=16;

struct PointShadowLightSelection {
    std::array<std::uint32_t,PointShadowLightBudget> sourceIndices{};
    std::uint32_t count{};
    std::size_t eligibleCount{},unadmittedCount{};

    std::span<const std::uint32_t> selectedSources() const noexcept {
        return std::span<const std::uint32_t>(sourceIndices).first(count);
    }
    std::int32_t slotForSource(std::uint32_t source) const noexcept {
        for(std::uint32_t slot=0;slot<count;++slot)
            if(sourceIndices[slot]==source)return static_cast<std::int32_t>(slot);
        return -1;
    }
};

// With a frozen world-frame priority origin, admit the strongest spatial
// influences first: squared center distance normalized by squared radius.
// Scene/Game preparation and shader mapping must use that same origin. A
// missing/nonfinite origin preserves the original source-order behavior.
inline PointShadowLightSelection selectPointShadowLights(std::span<const PointLight> lights,
    std::optional<Vec3> priorityOrigin={}) noexcept {
    PointShadowLightSelection result;
    const bool prioritize=priorityOrigin && math::finite(*priorityOrigin);
    std::array<double,PointShadowLightBudget> scores{};
    for(std::size_t source=0;source<lights.size();++source) {
        const auto& light=lights[source];
        if(!light.enabled || !light.castShadows || !std::isfinite(light.radius) || light.radius<=0 ||
           !std::isfinite(light.intensity) || light.intensity<=0 || !math::finite(light.position))continue;
        ++result.eligibleCount;
        if(!prioritize) {
            if(result.count<PointShadowLightBudget)result.sourceIndices[result.count++]=static_cast<std::uint32_t>(source);
            continue;
        }
        // Widen before subtraction/squaring. Finite float positions/radii can
        // overflow float arithmetic, but their complete score range fits double.
        double distanceSquared=0;
        for(unsigned axis=0;axis<3;++axis) {
            const double delta=double(light.position[axis])-double((*priorityOrigin)[axis]);
            distanceSquared+=delta*delta;
        }
        const double radius=light.radius;
        const double score=distanceSquared/(radius*radius);
        const auto id=light.entityId?light.entityId:static_cast<std::uint64_t>(source)+1;
        std::uint32_t destination=0;
        for(;destination<result.count;++destination) {
            if(score<scores[destination])break;
            if(score!=scores[destination])continue;
            const auto previous=result.sourceIndices[destination];
            const auto previousId=lights[previous].entityId?lights[previous].entityId:std::uint64_t(previous)+1;
            if(id<previousId || (id==previousId && source<previous))break;
        }
        if(destination==PointShadowLightBudget)continue;
        const auto last=result.count<PointShadowLightBudget?result.count:PointShadowLightBudget-1;
        for(auto slot=last;slot>destination;--slot) {
            result.sourceIndices[slot]=result.sourceIndices[slot-1];scores[slot]=scores[slot-1];
        }
        result.sourceIndices[destination]=static_cast<std::uint32_t>(source);scores[destination]=score;
        if(result.count<PointShadowLightBudget)++result.count;
    }
    result.unadmittedCount=result.eligibleCount-result.count;
    return result;
}

} // namespace crimson
