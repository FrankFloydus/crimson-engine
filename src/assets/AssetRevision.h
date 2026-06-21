#pragma once

#include <compare>
#include <cstdint>

namespace quader {

template <typename Tag>
struct TypedAssetRevision {
    std::uint64_t value = 0;

    constexpr bool valid() const { return value != 0; }
    constexpr explicit operator bool() const { return valid(); }
    constexpr auto operator<=>(const TypedAssetRevision&) const = default;
};

template <typename Tag>
constexpr TypedAssetRevision<Tag> nextRevision(TypedAssetRevision<Tag> revision) {
    return TypedAssetRevision<Tag>{revision.value == 0 ? 1 : revision.value + 1};
}

struct MeshAssetRevisionTag {};
struct MaterialAssetRevisionTag {};
struct TextureAssetRevisionTag {};

using MeshAssetRevision = TypedAssetRevision<MeshAssetRevisionTag>;
using MaterialAssetRevision = TypedAssetRevision<MaterialAssetRevisionTag>;
using TextureAssetRevision = TypedAssetRevision<TextureAssetRevisionTag>;

} // namespace quader
