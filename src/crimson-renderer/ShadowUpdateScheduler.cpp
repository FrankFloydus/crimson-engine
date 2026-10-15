#include "ShadowUpdateScheduler.h"

#include <algorithm>
#include <cmath>

namespace crimson::shadow_updates {
namespace {
bool before(const Candidate& left,const Candidate& right,const Budget& budget) noexcept {
    const bool leftAged=left.framesDeferred>=budget.fairnessAge;
    const bool rightAged=right.framesDeferred>=budget.fairnessAge;
    if(leftAged!=rightAged)return leftAged;
    if(leftAged) {
        if(left.framesDeferred!=right.framesDeferred)return left.framesDeferred>right.framesDeferred;
        if(left.lastUpdatedFrame!=right.lastUpdatedFrame)return left.lastUpdatedFrame<right.lastUpdatedFrame;
    }
    if(left.critical!=right.critical)return left.critical;
    if(left.distanceSquared!=right.distanceSquared)return left.distanceSquared<right.distanceSquared;
    if(left.framesDeferred!=right.framesDeferred)return left.framesDeferred>right.framesDeferred;
    if(left.lastUpdatedFrame!=right.lastUpdatedFrame)return left.lastUpdatedFrame<right.lastUpdatedFrame;
    return left.lightId<right.lightId;
}
Selection failure(Error error) noexcept { Selection result;result.error=error;return result; }
}

Selection select(std::span<const Candidate> candidates,const Budget& budget) noexcept {
    if(candidates.size()>Capacity)return failure(Error::TooManyCandidates);
    std::array<std::uint8_t,Capacity> pending{};
    Selection result;
    for(std::size_t index=0;index<candidates.size();++index) {
        const auto& candidate=candidates[index];
        if(!candidate.lightId)return failure(Error::InvalidLightId);
        if(candidate.slot>=Capacity)return failure(Error::InvalidSlot);
        if(std::isnan(candidate.distanceSquared) || candidate.distanceSquared<0)return failure(Error::InvalidDistance);
        for(std::size_t previous=0;previous<index;++previous) {
            if(candidates[previous].lightId==candidate.lightId)return failure(Error::DuplicateLightId);
            if(candidates[previous].slot==candidate.slot)return failure(Error::DuplicateSlot);
        }
        if(!candidate.dirty)continue;
        // At most sixteen candidates: insertion sorting avoids heap allocation
        // and has a small, explicit bound independent of world light count.
        std::size_t position=result.dirtyCount;
        while(position && before(candidate,candidates[pending[position-1]],budget)) {
            pending[position]=pending[position-1];--position;
        }
        pending[position]=static_cast<std::uint8_t>(index);++result.dirtyCount;
    }
    const auto limit=std::min({budget.maxLights,budget.maxFaces/FacesPerLight,static_cast<std::uint32_t>(Capacity)});
    result.count=static_cast<std::uint8_t>(std::min<std::uint32_t>(result.dirtyCount,limit));
    result.faceCount=static_cast<std::uint8_t>(result.count*FacesPerLight);
    result.deferredCount=static_cast<std::uint8_t>(result.dirtyCount-result.count);
    for(std::uint8_t index=0;index<result.dirtyCount;++index) {
        const auto& candidate=candidates[pending[index]];
        if(index<result.count) {
            result.order[index]=candidate.slot;
            result.selectedMask|=static_cast<std::uint16_t>(std::uint16_t{1}<<candidate.slot);
        } else if(candidate.critical)++result.criticalDeferred;
    }
    return result;
}

const char* errorText(Error error) noexcept {
    switch(error) {
    case Error::None:return "";
    case Error::TooManyCandidates:return "Point-shadow update candidates exceed the sixteen resident slots.";
    case Error::InvalidLightId:return "Point-shadow update candidate has no persistent light identity.";
    case Error::InvalidSlot:return "Point-shadow update candidate has an invalid atlas slot.";
    case Error::DuplicateLightId:return "Point-shadow update candidates repeat a light identity.";
    case Error::DuplicateSlot:return "Point-shadow update candidates overlap an atlas slot.";
    case Error::InvalidDistance:return "Point-shadow update candidate has an invalid focus distance.";
    }
    return "Unknown point-shadow scheduling error.";
}
} // namespace crimson::shadow_updates
