////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/modeling/runtime/modeling_object_runtime_map.hpp"

#include <cstdint>
#include <unordered_map>

#include <editor/modeling/runtime/session.hpp>

namespace quader {

class SceneDocument;

/**
 * Coordinates Scene Modeling Sync Service responsibilities for the modeling application service layer.
 */
class SceneModelingSyncService {
public:
  explicit SceneModelingSyncService(ModelingObjectRuntimeMap &object_map);

  [[nodiscard]] ModelingObjectRuntimeMap &object_map();
  [[nodiscard]] const ModelingObjectRuntimeMap &object_map() const;
  [[nodiscard]] bool scene_objects_sync_valid() const;
  [[nodiscard]] std::uint64_t synced_modeling_content_revision() const;

  void invalidate_scene_objects();
  void mark_scene_objects_synced(std::uint64_t content_revision);
  void remember_projected_mesh_signature(int modeling_object_id,
                                         std::uint64_t signature);
  void forget_projected_mesh_signature(int modeling_object_id);
  [[nodiscard]] bool
  projected_mesh_signature_known(int modeling_object_id) const;
  [[nodiscard]] bool
  projected_mesh_signature_matches(int modeling_object_id,
                                   std::uint64_t signature) const;

  [[nodiscard]] bool scene_objects_synced_for_current_modeling_content(
      const quader_editor_core::EditorSession &session,
      const SceneDocument &scene) const;

private:
  ModelingObjectRuntimeMap &object_map_;
  bool scene_objects_sync_valid_ = false;
  std::uint64_t synced_modeling_content_revision_ = 0;
  std::unordered_map<int, std::uint64_t> projected_mesh_signatures_by_object_;
};

} // namespace quader
