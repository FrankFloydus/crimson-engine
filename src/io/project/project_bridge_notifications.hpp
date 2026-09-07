////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "io/project/project_file_system.hpp"

#include <cstdint>

namespace quader {

class ProjectMaterialCatalog;
struct ProjectMaterialCatalogEntry;
struct QdrProjectWriteSnapshot;

/**
 * Publishes advisory engine-bridge save records after the owning scene/material
 * transaction commits. A failed notification never rolls back an authored save.
 */
class ProjectBridgeNotifications final {
public:
  [[nodiscard]] static ProjectIoResult<void> publish_scene(
      const ProjectFileSystem &file_system, const ProjectId &project_id,
      const SceneId &scene_id, const ProjectRelativePath &path,
      std::uint64_t source_byte_count, std::uint64_t source_content_hash,
      const QdrProjectWriteSnapshot &snapshot,
      const ProjectMaterialCatalog &materials);

  [[nodiscard]] static ProjectIoResult<void> publish_material(
      const ProjectFileSystem &file_system, const ProjectId &project_id,
      const ProjectMaterialCatalogEntry &material);
};

} // namespace quader
