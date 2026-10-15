#pragma once

#include "shaders/point_light_record.sh"
#include <cstdint>

namespace crimson::point_lighting {
inline constexpr std::uint32_t pointLightRecordRows(std::uint32_t capacity,std::uint32_t width) {
    return width?static_cast<std::uint32_t>((std::uint64_t(capacity)*CC_POINT_LIGHT_RECORD_TEXELS+width-1)/width):0;
}
}
