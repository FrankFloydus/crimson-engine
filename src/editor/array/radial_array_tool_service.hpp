////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "assets/asset_revision.hpp"
#include "assets/material_reference.hpp"
#include "core/command_result.hpp"
#include "core/types.hpp"
#include "editor/array/instance_array_source.hpp"
#include "editor/array/array_texture_mapping.hpp"
#include "editor/modeling/radial_array_mode.hpp"
#include "math/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class AssetRegistry;
class EditorHistoryService;
class EditorSelectionService;
class ModelingSessionService;
class SceneInstanceService;
class SceneDocument;
class SceneStructuralLifecycleService;

inline constexpr std::string_view kRadialArrayToolId = "radial_array";
inline constexpr std::string_view kRadialArrayModeSetting = "radial_array.mode";
inline constexpr std::string_view kRadialArrayCreateInstancesSetting =
    "radial_array.create_instances";
inline constexpr std::string_view kRadialArrayDegreesSetting =
    "radial_array.degrees";
inline constexpr std::string_view kRadialArrayCopiesSetting =
    "radial_array.copies";
inline constexpr std::string_view kRadialArrayRepeatsSetting =
    "radial_array.repeats";
inline constexpr std::string_view kRadialArrayRepeatSpacingSetting =
    "radial_array.repeat_spacing";

inline constexpr double kRadialArrayDefaultDegrees = 360.0;
inline constexpr double kRadialArrayMinimumDegrees = 0.0;
inline constexpr double kRadialArrayMaximumDegrees = 360.0;
inline constexpr std::uint32_t kRadialArrayDefaultCopies = 8u;
inline constexpr std::uint32_t kRadialArrayMinimumCopies = 2u;
inline constexpr std::uint32_t kRadialArrayMaximumCopies = 128u;
inline constexpr std::uint32_t kRadialArrayDefaultRepeats = 1u;
inline constexpr std::uint32_t kRadialArrayMinimumRepeats = 1u;
inline constexpr std::uint32_t kRadialArrayMaximumRepeats = 512u;
inline constexpr double kRadialArrayMinimumRepeatSpacing = 0.0;
inline constexpr double kRadialArrayMaximumRepeatSpacing = 16384.0;
inline constexpr std::size_t kRadialArrayMaximumTotalElements = 1024u;

/** Keeps authored options while leaving placement and automatic spacing local. */
struct RadialArraySessionDefaults final {
  RadialArrayMode mode = RadialArrayMode::Total;
  bool create_instances = false;
  double degrees = kRadialArrayDefaultDegrees;
  std::uint32_t copies = kRadialArrayDefaultCopies;
  std::uint32_t repeats = kRadialArrayDefaultRepeats;
  std::optional<double> repeat_spacing;

  friend bool operator==(const RadialArraySessionDefaults &,
                         const RadialArraySessionDefaults &) = default;
};

/**
 * Exposes the renderer-neutral center marker used during placement/preview.
 */
struct RadialArrayCenterState {
  bool visible = false;
  bool locked = false;
  QVec3 world_position = {};
  QVec3 world_normal = {0.0f, 1.0f, 0.0f};
  std::uint64_t revision = 0;
};

/**
 * Describes one source mesh and transform-only generated preview instances.
 */
struct RadialArrayPreviewSource {
  EntityId source_entity = kInvalidEntityId;
  /// Public occurrence used for visibility and member sampling composition.
  EntityId visibility_entity = kInvalidEntityId;
  /// Canonical scene entity that owns this preview mesh and material.
  EntityId material_entity = kInvalidEntityId;
  AssetId mesh = kInvalidAssetId;
  MaterialReference material;
  std::vector<QMat4> generated_world_transforms;
  std::vector<QMat4> generated_sampling_from_local;
};

/**
 * Exposes all generated preview instances for one live Radial Array session.
 */
struct RadialArrayPreviewState {
  bool active = false;
  std::vector<RadialArrayPreviewSource> sources;
  std::uint64_t revision = 0;
};

/**
 * Owns one non-destructive multi-source Radial Array session.
 */
class RadialArrayToolService final {
public:
  RadialArrayToolService(SceneDocument &scene, AssetRegistry &assets,
                         EditorHistoryService &history,
                         ModelingSessionService &modeling,
                         EditorSelectionService &selection,
                         SceneInstanceService &instances,
                         SceneStructuralLifecycleService &structural_lifecycle,
                         RadialArraySessionDefaults *defaults = nullptr);

  [[nodiscard]] bool active() const;
  [[nodiscard]] bool has_session() const { return session_.has_value(); }
  [[nodiscard]] bool can_activate() const;
  [[nodiscard]] std::uint64_t tool_settings_revision() const {
    return tool_settings_revision_counter_;
  }
  [[nodiscard]] bool center_locked() const;
  [[nodiscard]] bool has_valid_center() const;
  [[nodiscard]] RadialArrayMode mode() const;
  [[nodiscard]] bool create_instances() const;
  [[nodiscard]] double degrees() const;
  [[nodiscard]] std::uint32_t copies() const;
  [[nodiscard]] std::uint32_t repeats() const;
  [[nodiscard]] double repeat_spacing() const;
  [[nodiscard]] std::uint32_t maximum_copies() const;
  [[nodiscard]] std::uint32_t maximum_repeats() const;
  [[nodiscard]] RadialArrayCenterState center_state() const;
  /// The returned view remains valid until this service is mutated.
  [[nodiscard]] const RadialArrayPreviewState &preview_state() const;

  CommandResult activate(double fallback_grid_size);
  CommandResult refresh();
  CommandResult cancel();
  CommandResult apply();
  CommandResult update_center_candidate(QVec3 world_position,
                                        QVec3 world_normal,
                                        double fallback_grid_size);
  CommandResult clear_center_candidate();
  CommandResult lock_center();
  CommandResult set_degrees(double degrees);
  CommandResult set_mode(RadialArrayMode mode);
  CommandResult set_create_instances(bool enabled);
  CommandResult set_copies(std::uint32_t copies);
  CommandResult adjust_copies(int delta);
  CommandResult set_repeats(std::uint32_t repeats);
  CommandResult adjust_repeats(int delta);
  CommandResult set_repeat_spacing(double spacing);
  CommandResult reset_settings();

private:
  /// Captures every source invariant required to reject a stale session.
  struct SourceSnapshot {
    EntityId entity = kInvalidEntityId;
    int modeling_object_id = -1;
    std::string uuid;
    EntityId parent = kInvalidEntityId;
    AssetId mesh = kInvalidAssetId;
    MaterialReference material;
    MeshAssetRevision mesh_revision;
    MaterialAssetRevision material_revision;
    QMat4 world_transform;
    QVec3 visible_pivot_world = {};
    EntityId instance_definition = kInvalidEntityId;
    std::vector<InstanceArrayPreviewMeshSnapshot> instance_meshes;
    mutable ArrayTextureMappingCache texture_mapping_cache;
  };

  /// Stores one center-placement and live-preview transaction.
  struct Session {
    std::vector<SourceSnapshot> sources;
    EntityId primary_source = kInvalidEntityId;
    std::uint64_t selection_revision = 0;
    double fallback_grid_size = 1.0;
    double degrees = kRadialArrayDefaultDegrees;
    std::uint32_t copies = kRadialArrayDefaultCopies;
    std::uint32_t repeats = kRadialArrayDefaultRepeats;
    double repeat_spacing = 1.0;
    bool repeat_spacing_edited = false;
    bool candidate_valid = false;
    bool center_locked = false;
    QVec3 center_world = {};
    QVec3 plane_normal_world = {0.0f, 1.0f, 0.0f};
    std::uint64_t generation = 0;
    std::uint64_t revision = 0;
    bool instance_occurrences = false;
    RadialArrayMode mode = RadialArrayMode::Total;
    bool create_instances = false;
  };

  [[nodiscard]] bool
  valid_source_selection(std::vector<EntityId> &ordered_sources,
                         EntityId &primary_source,
                         bool &instance_occurrences) const;
  [[nodiscard]] bool can_activate_preflight() const;
  [[nodiscard]] bool session_sources_are_valid(const Session &session) const;
  [[nodiscard]] bool
  compute_session_sources_are_valid(const Session &session) const;
  [[nodiscard]] static std::uint32_t
  maximum_copies_for(std::size_t source_count, std::uint32_t repeats);
  [[nodiscard]] static std::uint32_t
  maximum_repeats_for(std::size_t source_count, std::uint32_t copies);
  [[nodiscard]] static double
  automatic_repeat_spacing(const Session &session);
  [[nodiscard]] std::optional<QVec3>
  source_visible_pivot(EntityId entity) const;
  [[nodiscard]] std::uint64_t issue_revision();
  void issue_tool_settings_revision();
  void clear_preview_cache();
  CommandResult apply_instance_array(const Session &session);

  SceneDocument &scene_;
  AssetRegistry &assets_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  EditorSelectionService &selection_;
  SceneInstanceService &instances_;
  SceneStructuralLifecycleService &structural_lifecycle_;
  RadialArraySessionDefaults local_defaults_;
  RadialArraySessionDefaults &defaults_;
  std::optional<Session> session_;
  std::uint64_t revision_counter_ = 0;
  std::uint64_t tool_settings_revision_counter_ = 0;
  mutable std::uint64_t validated_session_generation_ = 0;
  mutable std::uint64_t validated_scene_revision_ = 0;
  mutable std::uint64_t validated_asset_revision_ = 0;
  mutable std::uint64_t validated_object_map_revision_ = 0;
  mutable int validated_selection_mode_ = -1;
  mutable bool cached_session_sources_valid_ = false;
  mutable std::uint64_t can_activate_scene_revision_ = 0;
  mutable int can_activate_selection_mode_ = -1;
  mutable bool can_activate_cached_value_ = false;
  mutable bool can_activate_cache_valid_ = false;
  mutable std::uint64_t cached_preview_revision_ = 0;
  mutable std::uint64_t cached_preview_scene_revision_ = 0;
  mutable std::uint64_t preview_generation_counter_ = 0;
  mutable TextureLockSettings cached_preview_texture_locks_;
  mutable RadialArrayPreviewState cached_preview_;
};

} // namespace quader
