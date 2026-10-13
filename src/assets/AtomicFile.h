#pragma once

#include <filesystem>
#include <string_view>

namespace crimson {

// Write a complete sibling temporary file, then replace the destination atomically.
// Throws on failure; an existing destination is retained until publication succeeds.
void writeFileAtomically(const std::filesystem::path& path, std::string_view contents);

} // namespace crimson
