#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <optional>
#include <stdexcept>
#include <vector>

namespace crimson::picking {
using Pixel = std::array<std::uint8_t,4>;

inline std::array<float,4> color(std::uint32_t index) {
    std::array<float,4> result{};
    for (std::size_t channel=0;channel<4;++channel)
        result[channel] = static_cast<float>((index >> (channel*8)) & 255)/255.0f;
    return result;
}
inline std::uint32_t decode(const Pixel& bytes) {
    return static_cast<std::uint32_t>(bytes[0]) | (static_cast<std::uint32_t>(bytes[1]) << 8) |
        (static_cast<std::uint32_t>(bytes[2]) << 16) | (static_cast<std::uint32_t>(bytes[3]) << 24);
}
// bgfx frame numbers wrap at 32 bits; a readback is only a few frames ahead.
inline bool frameReady(std::uint32_t current, std::uint32_t ready) {
    return current-ready < UINT32_C(0x80000000);
}

class EntityIds {
public:
    struct Draw {
        std::uint64_t entityId{};
        std::uint32_t materialSlot{UINT32_MAX};
    };
    std::uint32_t add(std::uint64_t entity,std::uint32_t materialSlot=UINT32_MAX) {
        if (entities_.size() >= std::numeric_limits<std::uint32_t>::max())
            throw std::length_error("Too many draws for viewport picking.");
        const auto result = static_cast<std::uint32_t>(entities_.size());
        entities_.push_back({entity,materialSlot});
        return result;
    }
    std::optional<std::uint64_t> resolve(const Pixel& pixel) const {
        if(const auto draw=resolveDraw(pixel))return draw->entityId;
        return {};
    }
    std::optional<Draw> resolveDraw(const Pixel& pixel) const {
        const auto index = decode(pixel);
        if (index >= entities_.size()) return {};
        return entities_[index];
    }
private:
    std::vector<Draw> entities_{{}}; // Clear/background is never an entity or a material slot.
};
} // namespace crimson::picking
