////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <compare>
#include <cstdint>

namespace quader {

template <typename Tag>
/**
 * Represents a Typed Asset Revision value used by the asset document and registry layer.
 */
struct TypedAssetRevision {
    std::uint64_t value = 0;

    constexpr bool valid() const { return value != 0; }
    constexpr explicit operator bool() const { return valid(); }
    constexpr auto operator<=>(const TypedAssetRevision&) const = default;
};

template <typename Tag>
constexpr TypedAssetRevision<Tag>
next_revision(TypedAssetRevision<Tag> revision) {
  return TypedAssetRevision<Tag>{revision.value == 0 ? 1 : revision.value + 1};
}

/**
 * Stores the Mesh Asset Revision Tag data contract used by the asset document and registry layer.
 */
struct MeshAssetRevisionTag {};
/**
 * Stores the Material Asset Revision Tag data contract used by the asset document and registry layer.
 */
struct MaterialAssetRevisionTag {};
/**
 * Stores the Texture Asset Revision Tag data contract used by the asset document and registry layer.
 */
struct TextureAssetRevisionTag {};

using MeshAssetRevision = TypedAssetRevision<MeshAssetRevisionTag>;
using MaterialAssetRevision = TypedAssetRevision<MaterialAssetRevisionTag>;
using TextureAssetRevision = TypedAssetRevision<TextureAssetRevisionTag>;

} // namespace quader
