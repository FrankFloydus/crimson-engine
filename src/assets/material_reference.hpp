////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/project_asset_id.hpp"
#include "core/types.hpp"

#include <cstdint>

namespace quader {

/// Selects which persistence domain owns a material.
enum class MaterialReferenceKind : std::uint8_t {
  None,
  Embedded,
  Project,
};

/**
 * Stable logical material reference used by scene components.
 *
 * Embedded references retain the existing scene-local numeric AssetId
 * contract. Project references use a persistent UUID and are resolved by the
 * open project catalog. The two identities are deliberately never folded into
 * the same numeric namespace.
 */
class MaterialReference final {
public:
  MaterialReference() = default;

  // Kept implicit so existing aggregate MaterialComponent initializers that
  // pass an AssetId remain source-compatible.
  MaterialReference(AssetId embedded_asset) noexcept
      : kind_(embedded_asset == kInvalidAssetId
                  ? MaterialReferenceKind::None
                  : MaterialReferenceKind::Embedded),
        embedded_asset_(embedded_asset) {}

  [[nodiscard]] static MaterialReference
  project(ProjectAssetId project_asset) noexcept {
    MaterialReference result;
    if (project_asset.valid()) {
      result.kind_ = MaterialReferenceKind::Project;
      result.project_asset_ = project_asset;
    }
    return result;
  }

  [[nodiscard]] MaterialReferenceKind kind() const noexcept { return kind_; }
  [[nodiscard]] bool valid() const noexcept {
    return kind_ == MaterialReferenceKind::Embedded
               ? embedded_asset_ != kInvalidAssetId
               : kind_ == MaterialReferenceKind::Project &&
                     project_asset_.valid();
  }
  [[nodiscard]] bool embedded() const noexcept {
    return kind_ == MaterialReferenceKind::Embedded;
  }
  [[nodiscard]] bool project_owned() const noexcept {
    return kind_ == MaterialReferenceKind::Project;
  }
  [[nodiscard]] AssetId embedded_asset() const noexcept {
    return embedded() ? embedded_asset_ : kInvalidAssetId;
  }
  [[nodiscard]] const ProjectAssetId &project_asset() const noexcept {
    return project_asset_;
  }

  // Native embedded-material APIs continue to consume AssetId. Project
  // references intentionally convert to the invalid embedded sentinel.
  [[nodiscard]] operator AssetId() const noexcept { return embedded_asset(); }

  friend bool operator==(const MaterialReference &,
                         const MaterialReference &) = default;
  friend bool operator==(const MaterialReference &reference,
                         AssetId embedded_asset) noexcept {
    return reference.embedded_asset() == embedded_asset;
  }
  friend bool operator==(AssetId embedded_asset,
                         const MaterialReference &reference) noexcept {
    return reference == embedded_asset;
  }

private:
  MaterialReferenceKind kind_ = MaterialReferenceKind::None;
  AssetId embedded_asset_ = kInvalidAssetId;
  ProjectAssetId project_asset_;
};

} // namespace quader
