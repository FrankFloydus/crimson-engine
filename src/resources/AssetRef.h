#pragma once
#include <string>
#include <utility>

namespace crimson {
// Authored identity is independent of whether any payload is loaded.
template<class T> struct AssetRef {
    std::string id;
    AssetRef() = default;
    explicit AssetRef(std::string value) : id(std::move(value)) {}
    explicit AssetRef(const char* value) : id(value ? value : "") {}
    bool empty() const noexcept { return id.empty(); }
    bool operator==(const AssetRef&) const = default;
};
}
