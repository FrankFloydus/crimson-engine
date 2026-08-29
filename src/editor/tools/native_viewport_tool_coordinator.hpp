////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "core/types.hpp"

#include <string>
#include <string_view>

namespace quader {

class EditorSelectionService;
class CurveToolService;
class LinearArrayToolService;
class MaterialReference;
class ModelingSessionService;
class PivotToolService;
class PrimitiveCreationToolService;
class RadialArrayToolService;

/**
 * Routes native editor-owned viewport tools through one lifecycle boundary.
 */
class NativeViewportToolCoordinator final {
public:
  NativeViewportToolCoordinator(PivotToolService &pivot,
                                LinearArrayToolService &linear_array,
                                RadialArrayToolService &radial_array,
                                PrimitiveCreationToolService &primitives,
                                EditorSelectionService &selection,
                                ModelingSessionService &modeling,
                                CurveToolService &curve);

  [[nodiscard]] bool handles(std::string_view tool_id) const;
  [[nodiscard]] bool can_activate(std::string_view tool_id) const;
  [[nodiscard]] bool has_pending_tool() const;
  /// Distinguishes uncommitted gestures from an idle persistent curve editor.
  [[nodiscard]] bool has_pending_history_preview() const;
  [[nodiscard]] bool blocks_modeling_content_mutation() const;
  [[nodiscard]] bool blocks_document_mutation(EntityId entity) const;
  [[nodiscard]] std::string_view active_tool_id() const;

  CommandResult activate(std::string_view tool_id, double grid_world_size);
  CommandResult cancel_competing_state_for_tool_activation();
  CommandResult commit_active();
  CommandResult cancel_active();
  CommandResult accept_active_primitive(std::string name,
                                        MaterialReference material,
                                        bool snap_enabled);
  void reset_transient_state();

private:
  CommandResult cancel_prior_native_state();

  PivotToolService &pivot_;
  LinearArrayToolService &linear_array_;
  RadialArrayToolService &radial_array_;
  PrimitiveCreationToolService &primitives_;
  EditorSelectionService &selection_;
  ModelingSessionService &modeling_;
  CurveToolService &curve_;
};

} // namespace quader
