////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string_view>

namespace quader {

/// Persistent UUID for a reusable project asset.
class ProjectAssetId final {
public:
  ProjectAssetId() = default;

  [[nodiscard]] static ProjectAssetId generate();
  [[nodiscard]] static std::optional<ProjectAssetId>
  parse(std::string_view text);
  [[nodiscard]] static ProjectAssetId
  from_bytes(const std::array<std::byte, 16> &bytes);

  [[nodiscard]] bool valid() const noexcept { return value_[0] != '\0'; }
  [[nodiscard]] std::string_view str() const noexcept {
    return valid() ? std::string_view(value_.data(), 36u) : std::string_view{};
  }
  [[nodiscard]] std::array<std::byte, 16> bytes() const;

  friend bool operator==(const ProjectAssetId &,
                         const ProjectAssetId &) = default;
  friend auto operator<=>(const ProjectAssetId &,
                          const ProjectAssetId &) = default;

private:
  explicit ProjectAssetId(std::string_view normalized);

  std::array<char, 37> value_{};
};

/// Hashes canonical project-asset UUID values for unordered containers.
struct ProjectAssetIdHash final {
  [[nodiscard]] std::size_t
  operator()(const ProjectAssetId &id) const noexcept {
    return std::hash<std::string_view>{}(id.str());
  }
};

} // namespace quader
