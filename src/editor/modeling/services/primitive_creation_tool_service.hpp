////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/material_reference.hpp"
#include "core/command_result.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class EditorHistoryService;

inline constexpr std::string_view kCreatePrimitiveToolId = "create_primitive";

inline constexpr std::string_view kPrimitiveTypeSetting = "primitive.type";
inline constexpr std::string_view kPrimitiveFlipFacesSetting =
    "primitive.flip_faces";
inline constexpr std::string_view kBoxSubdivisionsXSetting =
    "primitive.box.subdivisions_x";
inline constexpr std::string_view kBoxSubdivisionsYSetting =
    "primitive.box.subdivisions_y";
inline constexpr std::string_view kBoxSubdivisionsZSetting =
    "primitive.box.subdivisions_z";
inline constexpr std::string_view kCylinderSideCountSetting =
    "primitive.cylinder.side_count";
inline constexpr std::string_view kCylinderCapsSetting =
    "primitive.cylinder.caps";
inline constexpr std::string_view kConeSideCountSetting =
    "primitive.cone.side_count";
inline constexpr std::string_view kConeCapsSetting = "primitive.cone.caps";
inline constexpr std::string_view kSphereRadialSegmentsSetting =
    "primitive.sphere.radial_segments";
inline constexpr std::string_view kSphereRingsSetting =
    "primitive.sphere.rings";
inline constexpr std::string_view kSphereHemisphereSetting =
    "primitive.sphere.hemisphere";
inline constexpr std::string_view kTorusMajorSegmentsSetting =
    "primitive.torus.major_segments";
inline constexpr std::string_view kTorusMinorSegmentsSetting =
    "primitive.torus.minor_segments";
inline constexpr std::string_view kTorusTubeRadiusRatioSetting =
    "primitive.torus.tube_radius_ratio";

inline constexpr int kPrimitiveMinimumSubdivisionCount = 1;
inline constexpr int kPrimitiveMaximumSubdivisionCount = 128;
inline constexpr int kCylinderMinimumSideCount = 3;
inline constexpr int kCylinderMaximumSideCount = 128;
inline constexpr int kConeMinimumSideCount = 3;
inline constexpr int kConeMaximumSideCount = 128;
inline constexpr int kSphereMinimumRadialSegments = 3;
inline constexpr int kSphereMaximumRadialSegments = 128;
inline constexpr int kSphereMinimumRings = 2;
inline constexpr int kSphereMaximumRings = 128;
inline constexpr int kTorusMinimumMajorSegments = 3;
inline constexpr int kTorusMaximumMajorSegments = 128;
inline constexpr int kTorusMinimumMinorSegments = 3;
inline constexpr int kTorusMaximumMinorSegments = 128;
inline constexpr double kTorusMinimumTubeRadiusRatio = 0.01;
inline constexpr double kTorusMaximumTubeRadiusRatio = 0.49;

/// Identifies the authored primitive generated from one footprint.
enum class PrimitiveCreationKind {
  Box = 0,
  Cylinder = 1,
  Sphere = 2,
  Cone = 3,
  Torus = 4,
};

/// Returns the canonical scene-name stem for a generated primitive kind.
[[nodiscard]] std::string_view
primitive_creation_default_name(PrimitiveCreationKind kind);

/// Stores persistent Box settings used by new and still-live Box primitives.
struct BoxPrimitiveCreationSettings {
  int subdivisions_x = 1;
  int subdivisions_y = 1;
  int subdivisions_z = 1;
  bool flip_faces = false;
};

/// Stores persistent Cylinder settings used by new and still-live Cylinders.
struct CylinderPrimitiveCreationSettings {
  int side_count = 32;
  bool caps = true;
  bool flip_faces = false;
};

/// Stores persistent Cone settings used by new and still-live Cones.
struct ConePrimitiveCreationSettings {
  int side_count = 32;
  bool caps = true;
  bool flip_faces = false;
};

/// Stores persistent Sphere settings used by new and still-live Spheres.
struct SpherePrimitiveCreationSettings {
  int radial_segments = 32;
  int rings = 16;
  bool hemisphere = false;
  bool flip_faces = false;
};

/// Stores persistent Torus settings used by new and still-live Tori.
struct TorusPrimitiveCreationSettings {
  int major_segments = 32;
  int minor_segments = 12;
  double tube_radius_ratio = 0.2;
  bool flip_faces = false;
};

/**
 * Owns one modal multi-shape primitive transaction.
 *
 * Accepted shapes are projected live for feedback, while one pre-session
 * memento remains the rollback boundary. Apply records the complete before /
 * after pair as one editor history command; Cancel and per-shape failures
 * restore the pre-session state. The newest accepted primitive remains
 * live-editable only while its sole active selection revision is unchanged.
 */
class PrimitiveCreationToolService final {
public:
  PrimitiveCreationToolService(
      EditorHistoryService &history, ModelingSessionService &modeling,
      ModelingOperationPreviewCheckpointHooks checkpoint_hooks = {});

  [[nodiscard]] bool active() const { return session_.has_value(); }
  [[nodiscard]] bool can_apply() const;
  [[nodiscard]] std::size_t shape_count() const;
  [[nodiscard]] bool owns_created_object(int modeling_object_id) const;
  [[nodiscard]] std::optional<PrimitiveCreationKind> active_kind() const;
  [[nodiscard]] std::string_view active_tool_id() const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const {
    return tool_settings_revision_;
  }

  [[nodiscard]] const BoxPrimitiveCreationSettings &box_settings() const {
    return box_settings_;
  }
  [[nodiscard]] const CylinderPrimitiveCreationSettings &
  cylinder_settings() const {
    return cylinder_settings_;
  }
  [[nodiscard]] const ConePrimitiveCreationSettings &cone_settings() const {
    return cone_settings_;
  }
  [[nodiscard]] const SpherePrimitiveCreationSettings &sphere_settings() const {
    return sphere_settings_;
  }
  [[nodiscard]] const TorusPrimitiveCreationSettings &torus_settings() const {
    return torus_settings_;
  }

  CommandResult activate();
  CommandResult activate(PrimitiveCreationKind kind);
  CommandResult set_kind(PrimitiveCreationKind kind);
  CommandResult accept_preview(const ModelingBoxPreview &preview,
                               std::string name,
                               MaterialReference material = {});
  CommandResult apply();
  CommandResult cancel();
  CommandResult set_setting(std::string_view name, double value);

private:
  /// Captures the rollback boundary and accepted-shape count for one session.
  struct Session {
    PrimitiveCreationKind kind = PrimitiveCreationKind::Box;
    ModelingSessionMemento before;
    std::size_t accepted_shapes = 0;
    std::vector<int> created_object_ids;
  };

  /// Keeps the newest accepted primitive editable while its selection is
  /// unchanged.
  struct LivePrimitive {
    ModelingBoxPreview placement;
    int modeling_object_id = -1;
    std::uint64_t selection_revision = 0;
  };

  [[nodiscard]] CommandResult rollback_and_exit(std::string message,
                                                bool report_failure);
  [[nodiscard]] bool live_primitive_is_selected() const;
  [[nodiscard]] CommandResult update_live_primitive();
  void freeze_live_primitive_if_selection_changed();
  void finish_checkpoint(bool restored_by_cancel);

  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  ModelingOperationPreviewCheckpointHooks checkpoint_hooks_;
  BoxPrimitiveCreationSettings box_settings_;
  CylinderPrimitiveCreationSettings cylinder_settings_;
  ConePrimitiveCreationSettings cone_settings_;
  SpherePrimitiveCreationSettings sphere_settings_;
  TorusPrimitiveCreationSettings torus_settings_;
  PrimitiveCreationKind preferred_kind_ = PrimitiveCreationKind::Box;
  std::optional<Session> session_;
  std::optional<LivePrimitive> live_primitive_;
  std::uint64_t tool_settings_revision_ = 0;
};

} // namespace quader
