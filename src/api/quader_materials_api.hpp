////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "core/types.hpp"
#include "io/project/project_types.hpp"

#include <cstdint>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/// Selects which persisted identity domain owns one public material reference.
enum class QuaderMaterialReferenceKind : std::uint8_t {
  Embedded,
  Project,
};

/**
 * Public, owner-neutral material identity.
 *
 * Embedded references retain legacy scene-local AssetId values. Project
 * references retain stable project UUIDs and therefore cover every project
 * material family, including Standard and Graph materials, without exposing
 * native catalog or asset-registry types.
 */
struct QuaderMaterialReference final {
  QuaderMaterialReferenceKind kind = QuaderMaterialReferenceKind::Embedded;
  AssetId embedded_asset = kInvalidAssetId;
  ProjectAssetId project_asset;

  [[nodiscard]] static QuaderMaterialReference embedded(AssetId asset) {
    return {
        .kind = QuaderMaterialReferenceKind::Embedded,
        .embedded_asset = asset,
    };
  }

  [[nodiscard]] static QuaderMaterialReference project(ProjectAssetId asset) {
    return {
        .kind = QuaderMaterialReferenceKind::Project,
        .project_asset = asset,
    };
  }

  [[nodiscard]] bool valid() const noexcept {
    return kind == QuaderMaterialReferenceKind::Embedded
               ? embedded_asset != kInvalidAssetId
               : project_asset.valid();
  }

  [[nodiscard]] bool embedded() const noexcept {
    return kind == QuaderMaterialReferenceKind::Embedded;
  }

  [[nodiscard]] bool project_owned() const noexcept {
    return kind == QuaderMaterialReferenceKind::Project;
  }

  [[nodiscard]] bool
  operator==(const QuaderMaterialReference &) const = default;
};

/// Selects the semantic material-use universe queried or mutated by the API.
enum class QuaderMaterialUsageScope : std::uint8_t {
  Selection,
  ActiveScene,
};

/** Public material-reference intent surface for the active editor scene. */
class QuaderMaterialsApi final {
public:
  QuaderMaterialsApi(QuaderApiSession session,
                     QuaderApiMutationRunner &mutations);

  /** Returns distinct references resolved by live faces in the requested scope. */
  [[nodiscard]] QuaderApiResult<std::vector<QuaderMaterialReference>>
  used_materials(QuaderMaterialUsageScope scope) const;

  /** Rewrites matching references as one atomic, undoable editor operation. */
  QuaderApiVoidResult replace_material_references(
      QuaderMaterialReference source, QuaderMaterialReference replacement,
      QuaderMaterialUsageScope scope);

  void rebind_session(QuaderApiSession session) { session_ = session; }

private:
  QuaderApiSession session_;
  QuaderApiMutationRunner *mutations_ = nullptr;
};

} // namespace quader
