////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/command_result.hpp"
#include "editor/modeling/modeling_types.hpp"
#include "editor/transform/mesh_arrangement_service.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class ModelingSessionService;
class SceneDocument;

inline constexpr std::string_view kAlignToolId = "align";

/// Options for the next immediate arrangement action.
struct AlignToolSettings final {
  bool use_snapping = false;

  friend bool operator==(const AlignToolSettings &,
                         const AlignToolSettings &) = default;
};

/// Application-lifetime options, independent from scene content and preferences.
struct AlignToolSessionDefaults final {
  AlignToolSettings settings;
  std::uint64_t revision = 0;
};

/// Counts revision-gated eligibility work for retained presentation verification.
struct AlignToolWorkCounters final {
  std::uint64_t availability_query_count = 0;
  std::uint64_t context_validation_count = 0;
};

/// Keeps the arrangement panel active while each click authors its own history step.
class AlignToolService final {
public:
  AlignToolService(SceneDocument &scene, const AssetRegistry &assets,
                   DocumentCommandService &document_commands,
                   ModelingSessionService &modeling,
                   AlignToolSessionDefaults *defaults = nullptr,
                   std::function<void()> context_retired = {});

  [[nodiscard]] bool active() const;
  [[nodiscard]] bool can_activate() const;
  [[nodiscard]] bool can_distribute() const;
  [[nodiscard]] const AlignToolSettings &settings() const;
  [[nodiscard]] std::uint64_t settings_revision() const;
  [[nodiscard]] bool grid_alignment_unavailable() const {
    (void)active();
    return grid_alignment_unavailable_;
  }
  [[nodiscard]] std::string_view warning_message() const;
  [[nodiscard]] std::optional<double> snap_grid_size() const {
    (void)active();
    return snap_grid_size_;
  }
  [[nodiscard]] CommandResult preflight_activation() const;
  [[nodiscard]] const AlignToolWorkCounters &work_counters() const {
    return work_counters_;
  }

  CommandResult activate();
  CommandResult deactivate();
  CommandResult refresh();
  CommandResult set_alignment(MeshArrangementAxis axis,
                              MeshAlignmentMode alignment);
  CommandResult set_distribution(MeshArrangementAxis axis);
  CommandResult set_use_snapping(bool enabled);
  CommandResult reset_settings();

private:
  [[nodiscard]] MeshArrangementService arrangement() const;
  [[nodiscard]] MeshArrangementAvailability availability() const;
  CommandResult execute(const MeshArrangementIntent &intent, std::string label);
  void clear_warning() const;

  SceneDocument &scene_;
  const AssetRegistry &assets_;
  DocumentCommandService &document_commands_;
  ModelingSessionService &modeling_;
  AlignToolSessionDefaults local_defaults_;
  AlignToolSessionDefaults &defaults_;
  std::function<void()> context_retired_;
  mutable bool active_ = false;
  mutable bool invalidation_pending_ = false;
  mutable bool grid_alignment_unavailable_ = false;
  mutable std::optional<double> snap_grid_size_;
  mutable std::uint64_t availability_scene_revision_ = 0;
  mutable std::uint64_t availability_selection_revision_ = 0;
  mutable std::uint64_t availability_layer_revision_ = 0;
  mutable std::uint64_t availability_asset_revision_ = 0;
  mutable ModelingSelectionMode availability_selection_mode_ =
      ModelingSelectionMode::Object;
  mutable bool availability_valid_ = false;
  mutable MeshArrangementAvailability cached_availability_;
  mutable std::uint64_t settings_revision_ = 0;
  mutable std::uint64_t observed_defaults_revision_ = 0;
  mutable bool observed_active_ = false;
  mutable AlignToolWorkCounters work_counters_;
};

} // namespace quader
