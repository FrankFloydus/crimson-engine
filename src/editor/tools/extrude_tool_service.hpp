////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "math/types.hpp"

#include <quader/modeling/types.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace quader {

class SceneDocument;
class ModelingSessionService;
class EditorHistoryService;
struct ModelingPickSelectRequest;

inline constexpr std::string_view kExtrudeToolId = "extrude";
inline constexpr std::string_view kExtrudeTypeSetting = "extrude.type";
inline constexpr std::string_view kExtrudeAmountSetting = "extrude.amount";
inline constexpr std::string_view kExtrudeApplySetting = "extrude.apply";

/// Extrusion option values in displayed map units; only mode is shared across scenes.
struct ExtrudeSessionDefaults final {
  modeling::FaceExtrudeMode mode = modeling::FaceExtrudeMode::Region;
  double amount = 0.0;
};

/// Selection-derived world-space handle retained until its owner revisions change.
struct ExtrudeHandleState final {
  bool active = false;
  QVec3 world_position = {};
  QVec3 world_direction = {0.0F, 1.0F, 0.0F};
  std::uint64_t revision = 0;
  modeling::FaceExtrudeMode mode = modeling::FaceExtrudeMode::Region;
  bool adjustment_available = false;
  /// Current cap pivot; the direction stays tied to the original operation.
  QVec3 adjustment_origin = {};
  QVec3 adjustment_direction = {0.0F, 1.0F, 0.0F};
  double amount_world = 0.0;
  bool dragging = false;
  bool adjusting = false;
  bool new_hovered = false;
  bool adjustment_hovered = false;
};

/// Observable work counts for unchanged-frame regression coverage.
struct ExtrudeToolWorkStats final {
  std::uint64_t source_capture_count = 0;
  std::uint64_t geometry_update_count = 0;
};

/// Owns persistent extrusion activation and one pending operation at a time.
class ExtrudeToolService final {
public:
  ExtrudeToolService(SceneDocument &scene, ModelingSessionService &modeling,
                     EditorHistoryService &history,
                     ExtrudeSessionDefaults &defaults);
  ~ExtrudeToolService();
  ExtrudeToolService(const ExtrudeToolService &) = delete;
  ExtrudeToolService &operator=(const ExtrudeToolService &) = delete;

  [[nodiscard]] bool active() const;
  [[nodiscard]] bool dragging() const;
  [[nodiscard]] bool preview_active() const;
  [[nodiscard]] bool adjusting() const;
  /// Distinguishes a rejected sample from a retired or compensated preview.
  [[nodiscard]] bool can_update_drag() const;
  [[nodiscard]] bool has_selected_faces() const;
  [[nodiscard]] bool adjustable() const;
  [[nodiscard]] modeling::FaceExtrudeMode mode() const;
  [[nodiscard]] double amount() const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const;
  [[nodiscard]] const ExtrudeHandleState &handle_state() const;
  [[nodiscard]] bool
  selected_face_hit(const ModelingPickSelectRequest &request) const;
  [[nodiscard]] ExtrudeToolWorkStats work_stats_for_tests() const;

  CommandResult activate();
  /// Chooses a preset while retaining the current pending operation, if any.
  CommandResult activate(modeling::FaceExtrudeMode requested_mode);
  /// Tool activation captures selected faces until explicit Apply or Cancel.
  CommandResult activate_preview();
  CommandResult activate_preview(modeling::FaceExtrudeMode requested_mode);
  /// Locks the first selected faces in a zero-amount pending operation.
  CommandResult begin_selection_preview();
  CommandResult commit_preview();
  CommandResult extrude();
  CommandResult refresh();
  CommandResult cancel();
  CommandResult set_mode(modeling::FaceExtrudeMode mode);
  CommandResult set_amount(double displayed_amount);
  CommandResult set_options(const ExtrudeSessionDefaults &options);
  CommandResult reset_settings();

  CommandResult begin_drag(std::optional<QVec3> world_direction = {});
  CommandResult begin_adjust_drag(std::optional<QVec3> world_direction = {});
  CommandResult update_drag(double displayed_amount,
                            std::optional<QVec3> world_direction = {});
  CommandResult update_drag_world_amount(
      double world_amount, std::optional<QVec3> world_direction = {});
  /// Ends pointer input while keeping its geometry pending for Apply or Cancel.
  CommandResult release_drag();
  CommandResult end_drag();
  CommandResult cancel_drag();

private:
  CommandResult activate_impl(modeling::FaceExtrudeMode requested_mode);
  CommandResult start_preview(const ExtrudeSessionDefaults &options);
  CommandResult cancel_preview();
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace quader
