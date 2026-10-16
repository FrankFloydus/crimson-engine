#pragma once
#include <string>
#include <string_view>

namespace crimson {
// Public asset IDs use canonical RFC UUID text. The UUID implementation stays
// private to storage; paths and imported source names are not reference IDs.
bool isAssetUuid(std::string_view value);
std::string newAssetId();
// Legacy readers convert old IDs deterministically once; empty stays empty.
std::string canonicalAssetId(std::string_view value);
std::string derivedAssetId(std::string_view owner, std::string_view role);
bool isEngineAssetId(std::string_view value);
} // namespace crimson
