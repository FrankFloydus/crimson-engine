#include "crimson-renderer/ShadowUpdateScheduler.h"

#include <algorithm>
#include <array>
#include <bit>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <type_traits>

namespace {
namespace updates=crimson::shadow_updates;
using Candidate=updates::Candidate;
void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }
std::array<Candidate,updates::Capacity> full(bool critical=false) {
    std::array<Candidate,updates::Capacity> result{};
    for(std::size_t i=0;i<result.size();++i)result[i]={1000+i,static_cast<std::uint8_t>(i),true,critical,float(i*i),0,0};
    return result;
}
void hardBudgetsAndAtomicFaces() {
    const auto candidates=full();
    const auto standard=updates::select(candidates);
    check(bool(standard) && standard.count==4 && standard.faceCount==24 && standard.deferredCount==12,"Default point-shadow update budget changed.");
    check(standard.selectedMask==15 && standard.dirtyCount==16,"Default updates did not prefer the closest four lights.");
    for(unsigned lights=0;lights<=20;++lights)for(unsigned faces=0;faces<=100;++faces) {
        const auto selection=updates::select(candidates,{lights,faces,4});
        check(bool(selection),"A valid budget failed selection.");
        check(selection.count==std::min({lights,faces/6,16u}),"Scheduler violated the combined light/face budget.");
        check(selection.faceCount==selection.count*6 && selection.faceCount<=faces,"A partially updated cube escaped the whole-light contract.");
        check(std::popcount(selection.selectedMask)==selection.count,"Selection mask and update count disagree.");
        check(selection.dirtyCount==selection.count+selection.deferredCount,"Deferred update accounting lost a dirty light.");
        for(unsigned i=0;i<selection.count;++i)check(selection.selected(selection.order[i]),"Ordered update does not belong to the selected mask.");
    }
    auto clean=candidates;for(auto& candidate:clean)candidate.dirty=false;
    check(updates::select(clean).count==0 && updates::select(clean).dirtyCount==0,"Clean cached maps consumed update work.");
    check(updates::select({}).count==0 && bool(updates::select({})),"An empty world cannot be scheduled.");
    const auto unlimited=updates::select(candidates,{UINT32_MAX,UINT32_MAX,4});
    check(unlimited.count==16 && unlimited.faceCount==96,"A large budget overflowed fixed resident capacity.");
    const auto critical=updates::select(full(true),{1,5,4});
    check(!critical.count && critical.criticalDeferred==16,"Critical maps bypassed a budget smaller than six faces.");
}

void priorityAndPersistentOrdering() {
    auto candidates=full();candidates[15].critical=true;
    auto selection=updates::select(candidates,{1,6,4});
    check(selection.selected(15),"A missing far projection lost priority to a young cached map.");
    candidates[14].framesDeferred=4;
    selection=updates::select(candidates,{1,6,4});
    check(selection.selected(14),"A critical newcomer starved an aged dirty map.");
    candidates[13].framesDeferred=5;
    selection=updates::select(candidates,{1,6,4});
    check(selection.selected(13),"Older deferred work lost fairness priority.");
    candidates[12].framesDeferred=5;candidates[12].lastUpdatedFrame=1;candidates[13].lastUpdatedFrame=2;
    check(updates::select(candidates,{1,6,4}).selected(12),"Equal-age fairness did not prefer the least recently updated map.");
    for(auto& candidate:candidates){candidate.distanceSquared=1;candidate.framesDeferred=0;candidate.lastUpdatedFrame=0;candidate.critical=false;}
    candidates[0].lightId=9000;candidates[15].lightId=1;
    const auto expected=updates::select(candidates);
    check(expected.order[0]==15 && !expected.selected(0),"Tie-breaking used source or atlas order instead of persistent light identity.");
    std::mt19937 random(0x53484144);
    for(unsigned iteration=0;iteration<100;++iteration) {
        std::shuffle(candidates.begin(),candidates.end(),random);
        const auto actual=updates::select(candidates);
        check(actual.selectedMask==expected.selectedMask && actual.order==expected.order,"Source reordering changed point-shadow update decisions.");
    }
    candidates=full();candidates[0].distanceSquared=std::numeric_limits<float>::infinity();
    check(bool(updates::select(candidates)) && !updates::select(candidates).selected(0),"Unknown/remote positive-infinite distance did not receive the lowest distance priority.");
    candidates[0].framesDeferred=std::numeric_limits<std::uint32_t>::max();
    check(updates::select(candidates,{1,6,4}).selected(0),"Saturated age overflowed fairness priority.");
}
void mixedProjectionBudgets() {
    auto candidates=full(true);
    for(unsigned i=0;i<candidates.size();++i)candidates[i].faceCount=i%2?1:6;
    const auto selection=updates::select(candidates,{16,10,4});
    check(selection&&selection.faceCount==10&&selection.count==5,
        "Mixed local shadows did not charge one Spot projection and six Point faces.");
    check(selection.selected(0)&&selection.selected(1)&&selection.selected(3)&&selection.selected(5)&&selection.selected(7),
        "A six-face Point prevented lower-cost complete Spot updates from using the remaining budget.");
    candidates[0].faceCount=0;
    check(updates::select(candidates).error==updates::Error::InvalidFaceCount,"Zero-projection update passed validation.");
    candidates[0].faceCount=7;
    check(updates::select(candidates).error==updates::Error::InvalidFaceCount,"Unbounded projection update passed validation.");
}

void fairnessUnderContinuousChanges() {
    // All sixteen lights keep moving. After every update that light is dirty
    // again, so nearest/critical priority alone would starve the far twelve.
    auto candidates=full(true);std::array<unsigned,updates::Capacity> updatesSeen{},longestWait{};
    constexpr unsigned Frames=200;
    for(unsigned frame=0;frame<Frames;++frame) {
        const auto selection=updates::select(candidates);
        check(selection.count==4 && selection.faceCount==24,"Continuous motion exceeded the world-frame budget.");
        for(auto& candidate:candidates) {
            const auto slot=candidate.slot;
            if(selection.selected(slot)){++updatesSeen[slot];candidate.framesDeferred=0;candidate.lastUpdatedFrame=frame+1;}
            else {++candidate.framesDeferred;longestWait[slot]=std::max(longestWait[slot],candidate.framesDeferred);}
        }
        std::rotate(candidates.begin(),candidates.begin()+1,candidates.end());
    }
    for(unsigned slot=0;slot<updates::Capacity;++slot) {
        check(updatesSeen[slot]>10,"Continuously dirty resident map starved behind moving near lights.");
        check(longestWait[slot]<=7,"Bounded resident set waited unexpectedly long for a complete update.");
    }

    // Twelve near slots receive brand-new critical lights every frame. Four
    // far resident lights still have to complete pending caster-only refreshes.
    candidates=full();for(unsigned slot=0;slot<4;++slot)candidates[slot].distanceSquared=10000;
    std::array<bool,4> completed{};
    for(unsigned frame=0;frame<16;++frame) {
        for(unsigned slot=4;slot<16;++slot)candidates[slot]={10000+frame*16+slot,static_cast<std::uint8_t>(slot),true,true,0,0,0};
        const auto selection=updates::select(candidates);
        for(unsigned slot=0;slot<4;++slot)if(candidates[slot].dirty) {
            if(selection.selected(static_cast<std::uint8_t>(slot))){completed[slot]=true;candidates[slot].dirty=false;}
            else ++candidates[slot].framesDeferred;
        }
    }
    check(std::all_of(completed.begin(),completed.end(),[](bool value){return value;}),"New critical arrivals starved unchanged-projection content updates.");
}

void invalidInputsFailClosed() {
    const auto rejects=[](const auto& candidates,updates::Error error) {
        const auto selection=updates::select(candidates);
        check(selection.error==error && !selection && !selection.selectedMask && !selection.count,"Invalid scheduler input exposed a partial update plan.");
        check(*updates::errorText(error),"Invalid scheduler input has no diagnostic.");
    };
    auto candidates=full();candidates[4].lightId=0;rejects(candidates,updates::Error::InvalidLightId);
    candidates=full();candidates[4].slot=16;rejects(candidates,updates::Error::InvalidSlot);
    candidates=full();candidates[4].lightId=candidates[0].lightId;rejects(candidates,updates::Error::DuplicateLightId);
    candidates=full();candidates[4].slot=candidates[0].slot;rejects(candidates,updates::Error::DuplicateSlot);
    candidates=full();candidates[4].distanceSquared=-1;rejects(candidates,updates::Error::InvalidDistance);
    candidates=full();candidates[4].distanceSquared=std::numeric_limits<float>::quiet_NaN();rejects(candidates,updates::Error::InvalidDistance);
    candidates=full();candidates[4].distanceSquared=-std::numeric_limits<float>::infinity();rejects(candidates,updates::Error::InvalidDistance);
    std::array<Candidate,17> oversized{};rejects(oversized,updates::Error::TooManyCandidates);
}
}

int main() {
    try {
        static_assert(std::is_trivially_copyable_v<updates::Selection>);
        hardBudgetsAndAtomicFaces();priorityAndPersistentOrdering();mixedProjectionBudgets();fairnessUnderContinuousChanges();invalidInputsFailClosed();
        std::cout<<"Point-shadow update budget, atomic faces, priority, fairness, reordering and validation checks passed.\n";return 0;
    } catch(const std::exception& exception) {std::cerr<<exception.what()<<'\n';return 1;}
}
