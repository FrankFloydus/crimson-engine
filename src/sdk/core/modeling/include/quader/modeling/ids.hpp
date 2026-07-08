////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <compare>
#include <cstdint>

namespace quader::modeling {

/**
 * Stores one typed generational SDK identifier.
 */
template <typename Tag> struct Id {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;

  [[nodiscard]] constexpr bool valid() const noexcept {
    return index != 0 && generation != 0;
  }

  friend constexpr auto operator<=>(const Id &, const Id &) = default;
};

template <typename Tag>
[[nodiscard]] constexpr Id<Tag> make_id(std::uint32_t index,
                                         std::uint32_t generation = 1) {
  return Id<Tag>{index, generation};
}

struct VertexTag;
struct EdgeTag;
struct FaceTag;
struct ObjectTag;
struct MaterialTag;

using VertexId = Id<VertexTag>;
using EdgeId = Id<EdgeTag>;
using FaceId = Id<FaceTag>;
using ObjectId = Id<ObjectTag>;
using MaterialId = Id<MaterialTag>;

/**
 * Canonical undirected edge identity. The endpoint order is stable.
 */
struct EdgeKey {
  VertexId a{};
  VertexId b{};

  [[nodiscard]] constexpr bool valid() const noexcept {
    return a.valid() && b.valid() && a != b;
  }

  friend constexpr auto operator<=>(const EdgeKey &, const EdgeKey &) =
      default;
};

[[nodiscard]] constexpr EdgeKey make_edge_key(VertexId first,
                                              VertexId second) {
  return second < first ? EdgeKey{second, first} : EdgeKey{first, second};
}

} // namespace quader::modeling
