////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "io/project/project_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace quader {

class QuaderEventsApi;
class QuaderProjectAssetsApiInternalAccess;

/// Selects the public editor and payload family for a project material.
enum class QuaderProjectMaterialKind : std::uint8_t {
  Standard,
  Graph,
};

/// Public catalog metadata for one validated project material.
struct QuaderProjectMaterialInfo {
  ProjectAssetId id;
  std::string name;
  QuaderProjectMaterialKind kind = QuaderProjectMaterialKind::Standard;
  std::string project_relative_path;
  std::uintmax_t qmat_size_bytes = 0u;
  std::array<float, 3> base_color{1.0f, 1.0f, 1.0f};
  std::uint64_t content_revision = 0u;
};

/// Immutable encoded albedo preview returned by the project-assets API.
struct QuaderProjectMaterialPreview {
  std::vector<std::byte> encoded_bytes;
  std::uint64_t content_revision = 0u;
};

/// Public project-asset intent surface; standalone sessions are unavailable.
class QuaderProjectAssetsApi final {
public:
  explicit QuaderProjectAssetsApi(QuaderApiSession session);

  [[nodiscard]] QuaderApiResult<std::vector<QuaderProjectMaterialInfo>>
  materials() const;
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialInfo>
  active_material() const;
  QuaderApiVoidResult set_active_material(ProjectAssetId material_id);
  [[nodiscard]] QuaderApiResult<QuaderProjectMaterialPreview>
  material_preview(ProjectAssetId material_id) const;

  void rebind_session(QuaderApiSession session) { session_ = session; }

private:
  void attach_events(QuaderEventsApi &events) { events_ = &events; }
  void publish_assets_changed();

  friend class QuaderProjectAssetsApiInternalAccess;
  QuaderApiSession session_;
  QuaderEventsApi *events_ = nullptr;
};

} // namespace quader
