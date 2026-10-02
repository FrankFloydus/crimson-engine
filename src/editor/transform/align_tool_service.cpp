////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/transform/align_tool_service.hpp"

#include "assets/asset_registry.hpp"
#include "core/release_features.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/scene_document.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <algorithm>
#include <functional>
#include <utility>

namespace quader {
namespace {

std::optional<std::size_t> axis_index(MeshArrangementAxis axis) {
  switch (axis) {
  case MeshArrangementAxis::X: return 0u;
  case MeshArrangementAxis::Y: return 1u;
  case MeshArrangementAxis::Z: return 2u;
  }
  return std::nullopt;
}

std::optional<MeshArrangementMode> alignment_mode(MeshAlignmentMode alignment) {
  switch (alignment) {
  case MeshAlignmentMode::Minimum: return MeshArrangementMode::Minimum;
  case MeshAlignmentMode::Center: return MeshArrangementMode::Center;
  case MeshAlignmentMode::Maximum: return MeshArrangementMode::Maximum;
  }
  return std::nullopt;
}

MeshArrangementSnapRequest snap_request(bool enabled) {
  MeshArrangementSnapRequest request{.enabled = enabled};
  if (enabled) {
    for (const auto &preset : viewport_grid_preset_catalog()) {
      request.candidate_grid_sizes.push_back(preset.world_units);
    }
    std::ranges::sort(request.candidate_grid_sizes, std::greater<>{});
  }
  return request;
}

} // namespace

AlignToolService::AlignToolService(
    SceneDocument &scene, const AssetRegistry &assets,
    DocumentCommandService &document_commands, ModelingSessionService &modeling,
    AlignToolSessionDefaults *defaults, std::function<void()> context_retired)
    : scene_(scene), assets_(assets), document_commands_(document_commands),
      modeling_(modeling), defaults_(defaults != nullptr ? *defaults : local_defaults_),
      context_retired_(std::move(context_retired)) {}

MeshArrangementService AlignToolService::arrangement() const {
  const auto mode = modeling_.selection_mode();
  return MeshArrangementService(
      scene_, assets_,
      {.object_mode = mode == ModelingSelectionMode::Object ||
                      mode == ModelingSelectionMode::Group,
       .active_object = scene_.primary_selection()},
      document_commands_);
}

MeshArrangementAvailability AlignToolService::availability() const {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return {};
  }
  const auto mode = modeling_.selection_mode();
  if (!availability_valid_ ||
      availability_scene_revision_ != scene_.revision() ||
      availability_selection_revision_ != scene_.selection_revision() ||
      availability_layer_revision_ != scene_.layer_revision() ||
      availability_asset_revision_ != assets_.revision() ||
      availability_selection_mode_ != mode) {
    ++work_counters_.availability_query_count;
    bool context_retired = false;
    cached_availability_ = arrangement().availability();
    if (active_ && cached_availability_.can_align) {
      ++work_counters_.context_validation_count;
      if (!arrangement().capture_baseline().ok) {
        cached_availability_ = {};
      }
    }
    if (active_ && !cached_availability_.can_align) {
      active_ = false;
      invalidation_pending_ = true;
      clear_warning();
      context_retired = true;
    }
    availability_scene_revision_ = scene_.revision();
    availability_selection_revision_ = scene_.selection_revision();
    availability_layer_revision_ = scene_.layer_revision();
    availability_asset_revision_ = assets_.revision();
    availability_selection_mode_ = mode;
    availability_valid_ = true;
    ++settings_revision_;
    if (context_retired && context_retired_) {
      context_retired_();
    }
  }
  return cached_availability_;
}

bool AlignToolService::can_activate() const { return availability().can_align; }

bool AlignToolService::active() const {
  if (active_) {
    (void)availability();
  }
  return active_;
}

bool AlignToolService::can_distribute() const {
  return availability().can_distribute;
}

const AlignToolSettings &AlignToolService::settings() const {
  return defaults_.settings;
}

std::uint64_t AlignToolService::settings_revision() const {
  (void)availability();
  if (observed_defaults_revision_ != defaults_.revision ||
      observed_active_ != active_) {
    observed_defaults_revision_ = defaults_.revision;
    observed_active_ = active_;
    ++settings_revision_;
  }
  return settings_revision_;
}

std::string_view AlignToolService::warning_message() const {
  (void)active();
  return grid_alignment_unavailable_ ? mesh_arrangement_grid_alignment_warning()
                                     : std::string_view{};
}

void AlignToolService::clear_warning() const {
  grid_alignment_unavailable_ = false;
  snap_grid_size_.reset();
}

CommandResult AlignToolService::preflight_activation() const {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  const auto baseline = arrangement().capture_baseline();
  return baseline.ok ? command_unchanged_result()
                     : command_invalid_result(baseline.message);
}

CommandResult AlignToolService::activate() {
  if (active()) {
    return command_unchanged_result();
  }
  const auto preflight = preflight_activation();
  if (!preflight.ok) {
    return preflight;
  }
  active_ = true;
  invalidation_pending_ = false;
  clear_warning();
  return command_changed_result();
}

CommandResult AlignToolService::deactivate() {
  if (!active()) {
    return command_unchanged_result();
  }
  active_ = false;
  clear_warning();
  return command_changed_result();
}

CommandResult AlignToolService::refresh() {
  (void)active();
  return std::exchange(invalidation_pending_, false) ? command_changed_result()
                                                    : command_unchanged_result();
}

CommandResult AlignToolService::execute(const MeshArrangementIntent &intent,
                                      std::string label) {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (!active()) {
    return command_invalid_result("Align Tool is not active.");
  }
  const auto baseline = arrangement().capture_baseline();
  auto plan = MeshArrangementService::plan(
      baseline, intent, snap_request(settings().use_snapping));
  if (!plan.ok) {
    return command_invalid_result(plan.message);
  }
  const auto result = document_commands_.set_transforms(
      std::move(plan.edits), std::move(label), SceneTextureTransformKind::Translation);
  if (!result.ok) {
    return {.ok = false, .changed = result.changed, .message = result.message};
  }
  grid_alignment_unavailable_ = plan.grid_alignment_unavailable;
  snap_grid_size_ = plan.snap_grid_size;
  return {.ok = true, .changed = result.changed,
          .message = std::string(warning_message())};
}

CommandResult AlignToolService::set_alignment(MeshArrangementAxis axis,
                                             MeshAlignmentMode alignment) {
  const auto index = axis_index(axis);
  const auto mode = alignment_mode(alignment);
  if (!index || !mode) {
    return command_invalid_result("Align Tool needs a valid world axis and alignment.");
  }
  MeshArrangementIntent intent;
  intent.axes[*index] = *mode;
  return execute(intent, "Align Objects");
}

CommandResult AlignToolService::set_distribution(MeshArrangementAxis axis) {
  const auto index = axis_index(axis);
  if (!index) {
    return command_invalid_result("Align Tool needs a valid world axis.");
  }
  MeshArrangementIntent intent;
  intent.axes[*index] = MeshArrangementMode::Distribute;
  return execute(intent, "Distribute Objects");
}

CommandResult AlignToolService::set_use_snapping(bool enabled) {
  if (!release_has_feature(ReleaseFeature::Arrangement)) {
    return command_invalid_result(release_feature_unavailable_message());
  }
  if (settings().use_snapping == enabled) {
    return command_unchanged_result();
  }
  defaults_.settings.use_snapping = enabled;
  ++defaults_.revision;
  clear_warning();
  return command_changed_result();
}

CommandResult AlignToolService::reset_settings() {
  return set_use_snapping(false);
}

} // namespace quader
