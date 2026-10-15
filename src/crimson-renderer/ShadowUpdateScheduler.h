#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace crimson::shadow_updates {

inline constexpr std::size_t Capacity=16;
inline constexpr std::uint32_t FacesPerLight=6;

// CPU-only scheduling metadata, independent of source extraction order. A slot
// is the persistent atlas allocation, not the index of this candidate span.
struct Candidate {
    std::uint64_t lightId{};
    std::uint8_t slot{};
    bool dirty{},critical{}; // Critical means no coherent projection is available.
    float distanceSquared{}; // Squared distance to the selected world-frame focus.
    std::uint32_t framesDeferred{};
    std::uint64_t lastUpdatedFrame{};
};
struct Budget {
    std::uint32_t maxLights{4},maxFaces{24};
    std::uint32_t fairnessAge{4};
};
enum class Error : std::uint8_t { None, TooManyCandidates, InvalidLightId, InvalidSlot, DuplicateLightId, DuplicateSlot, InvalidDistance };
struct Selection {
    std::uint16_t selectedMask{};
    std::array<std::uint8_t,Capacity> order{}; // Atlas slots, in scheduling order.
    std::uint8_t count{},faceCount{},dirtyCount{},deferredCount{},criticalDeferred{};
    Error error{Error::None};
    bool selected(std::uint8_t slot) const noexcept {
        return slot<Capacity && (selectedMask&(std::uint16_t{1}<<slot))!=0;
    }
    explicit operator bool() const noexcept { return error==Error::None; }
};

// Allocation-free, deterministic selection of whole six-face updates. The
// caller invokes this once per shadow world/frame and reuses its decision for
// Scene/Game. It owns age/history: increment pending dirty ages once per world
// frame (saturating), and reset an age only after all six faces are submitted.
//
// Age >= fairnessAge outranks critical/distance priority. This prevents a
// persistent dirty light from starving behind continuously arriving or moving
// near lights. Younger candidates prefer missing projections, then distance.
// Ties use persistent light identity, never span order. Positive infinity is a
// valid unknown/remote distance; NaN and negative distances fail closed.
//
// This selector never authorizes sampling a stale projection. An unscheduled
// new/moved light stays unshadowed; unchanged projections may retain their old
// coherent content while a caster-only update is pending.
Selection select(std::span<const Candidate> candidates,const Budget& budget={}) noexcept;
const char* errorText(Error error) noexcept;

} // namespace crimson::shadow_updates
