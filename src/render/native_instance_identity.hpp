#pragma once

#include <cstdint>
#include <string_view>

namespace quader {
// Stable CPU provenance only; never depends on a transient renderer instance ID.
[[nodiscard]] inline std::uint64_t native_source_instance_id(
    std::string_view receiver_uuid, std::string_view member_uuid = {},
    std::uint32_t index_offset = 0, std::uint32_t index_count = 0) noexcept {
    std::uint64_t value = 14695981039346656037ull;
    const auto add = [&](unsigned char byte) { value ^= byte; value *= 1099511628211ull; };
    for (const unsigned char byte : receiver_uuid) add(byte);
    add(0);
    for (const unsigned char byte : member_uuid) add(byte);
    for (unsigned i = 0; i < 4; ++i) {
        add(static_cast<unsigned char>(index_offset >> (8 * i)));
        add(static_cast<unsigned char>(index_count >> (8 * i)));
    }
    return value ? value : 1;
}
} // namespace quader
