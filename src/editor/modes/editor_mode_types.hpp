////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_types.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace quader {

/** Editor contexts are independent of the component selection kind. */
enum class EditorMode { Object, Edit };

/** Stable authored identity plus its current scene/modeling projections. */
struct EditorModeParticipant final {
  EntityId entity = kInvalidEntityId;
  std::string uuid;
  int modeling_object_id = kInvalidModelingObjectId;

  [[nodiscard]] bool operator==(const EditorModeParticipant &) const = default;
};

/** Immutable borrowed presentation of one scene's current editor context. */
struct EditorModeSnapshot final {
  EditorMode mode = EditorMode::Object;
  std::uint64_t revision = 1u;
  std::uint64_t generation = 1u;
  ModelingSelectionMode component_kind = ModelingSelectionMode::Vertex;
  EntityId active_entity = kInvalidEntityId;
  /// Stable identity of the active object, retained across Object deselection.
  std::string active_uuid;
  std::vector<EditorModeParticipant> participants;
  std::vector<EntityId> object_results;

  [[nodiscard]] bool contains(EntityId entity) const noexcept;
  [[nodiscard]] bool operator==(const EditorModeSnapshot &) const = default;
};

} // namespace quader
