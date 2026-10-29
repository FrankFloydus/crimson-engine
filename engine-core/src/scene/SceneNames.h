#pragma once

#include "Scene.h"
#include <string_view>
#include <unordered_map>

namespace crimson {
// Duplicate names continue the largest case-sensitive scene-wide numeric
// family, with at least three digits. Batch allocations reserve their results.
class SceneDuplicateNameAllocator {
public:
    explicit SceneDuplicateNameAllocator(std::span<const EntityRecord> records);
    void reserve(std::string_view name);
    std::string next(std::string_view source);
private:
    std::unordered_map<std::string,std::string> maxima_;
};
}
