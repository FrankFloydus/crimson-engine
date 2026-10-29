#include "crimson-renderer/Picking.h"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
crimson::picking::Pixel rasterized(std::uint32_t id) {
    const auto color = crimson::picking::color(id);
    crimson::picking::Pixel pixel{};
    for (std::size_t channel=0;channel<4;++channel)
        pixel[channel] = static_cast<std::uint8_t>(std::lround(color[channel]*255));
    return pixel;
}
}

int main() {
    try {
        using namespace crimson::picking;
        for (const auto index : {0u,1u,255u,256u,65535u,65536u,0x01000000u,0xffffffffu})
            require(decode(rasterized(index)) == index,"Picking byte channels lost a dense ID.");
        EntityIds ids;
        const auto huge = UINT64_C(0xfedcba9876543210);
        const auto first = ids.add(huge), second = ids.add(9);
        require(first != second,"Distinct scene instances share a picking ID.");
        require(ids.resolve(rasterized(first)) == huge,"Persistent entity IDs must retain all 64 bits.");
        require(ids.resolve(rasterized(second)) == 9,"Picking resolved the wrong instance.");
        require(ids.resolve(Pixel{}) == 0,"Cleared pixels must select background.");
        require(!ids.resolve(rasterized(second+1)),"Out-of-range GPU IDs must be rejected.");
        EntityIds reopened;
        const auto reused = reopened.add(42);
        require(reopened.resolve(rasterized(reused)) == 42 && ids.resolve(rasterized(first)) == huge,
                "Pending readbacks must retain their own entity lookup.");
        EntityIds slots;
        const auto firstSlot=slots.add(huge,0),secondSlot=slots.add(huge,3),otherEntity=slots.add(42,3);
        require(firstSlot!=secondSlot&&secondSlot!=otherEntity,"Material slots or scene instances shared a dense draw ID.");
        const auto firstHit=slots.resolveDraw(rasterized(firstSlot)),secondHit=slots.resolveDraw(rasterized(secondSlot));
        require(firstHit&&firstHit->entityId==huge&&firstHit->materialSlot==0,"Material slot zero was confused with a background hit.");
        require(secondHit&&secondHit->entityId==huge&&secondHit->materialSlot==3,"Picking lost the material slot within a repeated model instance.");
        const auto otherHit=slots.resolveDraw(rasterized(otherEntity)),background=slots.resolveDraw(Pixel{});
        require(otherHit&&otherHit->entityId==42&&otherHit->materialSlot==3,"Picking confused equivalent slots on different entities.");
        require(background&&background->entityId==0&&background->materialSlot==UINT32_MAX,"Background picking returned an assignable material slot.");
        require(!slots.resolveDraw(rasterized(otherEntity+1)),"An invalid readback resolved to an assignable material slot.");
        require(slots.resolve(rasterized(secondSlot))==huge,"Slot-aware picking changed ordinary entity-selection resolution.");
        require(!frameReady(10,11) && frameReady(11,11) && frameReady(12,11),"Readback became visible before its ready frame.");
        require(!frameReady(0xfffffffeu,1) && frameReady(1,1) && frameReady(2,0xffffffffu),"Readback frame wrap was handled incorrectly.");
        std::cout << "Render scene picking checks passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
