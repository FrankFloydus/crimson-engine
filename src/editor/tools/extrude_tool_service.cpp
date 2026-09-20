////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/tools/extrude_tool_service.hpp"

#include "editor/history/editor_history_service.hpp"
#include "editor/history/extrude_history_command.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "scene/scene_document.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"

#include <atomic>
#include <cmath>
#include <limits>
#include <string>
#include <utility>

namespace quader {
namespace {

bool valid_options(const ExtrudeSessionDefaults &options) {
  return std::isfinite(options.amount) &&
         std::abs(options.amount /
                  viewport_host_defaults().map_units_per_world_unit) <=
             std::numeric_limits<float>::max() &&
         (options.mode == modeling::FaceExtrudeMode::Region ||
          options.mode == modeling::FaceExtrudeMode::AlongNormals ||
          options.mode == modeling::FaceExtrudeMode::Individual);
}

bool same_options(const ExtrudeSessionDefaults &a,
                  const ExtrudeSessionDefaults &b) {
  return a.mode == b.mode && a.amount == b.amount;
}

bool same_direction(const std::optional<QVec3> &a,
                     const std::optional<QVec3> &b) {
  return a.has_value() == b.has_value() &&
         (!a || (a->x == b->x && a->y == b->y && a->z == b->z));
}

double to_world_amount(double displayed) {
  return displayed / viewport_host_defaults().map_units_per_world_unit;
}

std::string next_extrusion_key() {
  static std::atomic<std::uint64_t> next{0};
  return "extrude:" + std::to_string(++next);
}

} // namespace

struct ExtrudeToolService::Impl final {
  /// Pins the authoritative owners without enumerating geometry or bindings.
  struct Stamp final {
    std::uint64_t scene_content = 0;
    std::uint64_t scene_selection = 0;
    std::uint64_t modeling_content = 0;
    std::uint64_t modeling_selection = 0;
    std::uint64_t object_bindings = 0;

    friend bool operator==(const Stamp &, const Stamp &) = default;
  };

  /// Immutable source and the exact committed head that may still be adjusted.
  struct Adjustment final {
    ModelingFaceExtrusionSource source;
    std::optional<QVec3> world_direction;
    std::string merge_key;
    Stamp stamp;
    std::uint64_t history_revision = 0;
    QVec3 origin;
    QVec3 direction;
    ExtrudeSessionDefaults options;
  };

  /// Provisional geometry whose cancellation restores the committed document.
  struct Drag final {
    ModelingFaceExtrusionSource source;
    ModelingSessionMemento before;
    std::optional<QVec3> world_direction;
    std::optional<QVec3> original_world_direction;
    double amount = 0.0;
    ExtrudeHandleState handle;
    ExtrudeSessionDefaults options;
    std::optional<Adjustment> previous_adjustment;
    std::optional<Adjustment> cancellation_adjustment;
    QVec3 source_origin;
    QVec3 source_direction;
    QVec3 visual_direction;
    QVec3 initial_pivot;
  };

  [[nodiscard]] static std::optional<QVec3> canonical_drag_direction(
      const Drag &drag, std::optional<QVec3> direction) {
    if (drag.options.mode == modeling::FaceExtrudeMode::Region && direction &&
        same_direction(direction, std::optional<QVec3>{
            drag.original_world_direction.value_or(drag.source_direction)})) {
      // An explicit constraint returning to the original direction is the same
      // operation request, including its absent default-direction override.
      return drag.original_world_direction;
    }
    return direction;
  }

  Impl(SceneDocument &scene_value, ModelingSessionService &modeling_value,
       EditorHistoryService &history_value, ExtrudeSessionDefaults &defaults_value)
      : scene(scene_value), modeling(modeling_value), history(history_value),
        defaults(defaults_value), observed_defaults(defaults_value) {}

  [[nodiscard]] Stamp stamp() const {
    return {scene.content_revision(), scene.selection_revision(),
            modeling.content_revision(), modeling.selection_revision(),
            modeling.modeling_object_map_revision()};
  }

  [[nodiscard]] bool adjustment_current() const {
    return active && !drag && adjustment.has_value() &&
           adjustment->stamp == stamp() &&
           modeling.face_extrusion_source_current(adjustment->source) &&
           history.can_merge_with_latest(adjustment->merge_key,
                                          adjustment->history_revision);
  }

  void retire_stale_adjustment() {
    if (adjustment && !adjustment_current()) {
      adjustment.reset();
      ++settings_revision;
      ++handle_revision;
    }
  }

  void accept_adjustment(ModelingFaceExtrusionSource source,
                         std::optional<QVec3> direction, std::string key,
                         QVec3 origin, QVec3 normal,
                         const ExtrudeSessionDefaults &options) {
    adjustment = Adjustment{std::move(source), direction, std::move(key),
                            stamp(), history.revision(), origin, normal, options};
    if (!adjustment_current()) {
      adjustment.reset();
    }
    ++settings_revision;
    ++handle_revision;
  }

  void restore_adjustment_after_compensation(const Adjustment &previous) {
    if (!adjustment_current() &&
        history.can_merge_with_latest(previous.merge_key,
                                       previous.history_revision) &&
        modeling.face_extrusion_source_current(previous.source)) {
      accept_adjustment(previous.source, previous.world_direction,
                         previous.merge_key, previous.origin,
                         previous.direction, previous.options);
    }
  }

  [[nodiscard]] const ModelingFaceExtrusionSourceResult &target() const {
    const Stamp current = stamp();
    if (!cached_stamp || *cached_stamp != current ||
        (cached_target.source.valid() &&
         !modeling.face_extrusion_source_current(cached_target.source))) {
      cached_stamp = current;
      cached_target = modeling.capture_face_extrusion_source();
      ++work.source_capture_count;
      ++handle_revision;
    }
    return cached_target;
  }

  void observe_defaults() const {
    if (!same_options(observed_defaults, defaults)) {
      observed_defaults = defaults;
      ++settings_revision;
      ++handle_revision;
    }
  }

  SceneDocument &scene;
  ModelingSessionService &modeling;
  EditorHistoryService &history;
  ExtrudeSessionDefaults &defaults;
  bool active = false;
  std::optional<Adjustment> adjustment;
  std::optional<Drag> drag;
  mutable std::optional<Stamp> cached_stamp;
  mutable ModelingFaceExtrusionSourceResult cached_target;
  mutable ExtrudeSessionDefaults observed_defaults;
  mutable std::uint64_t settings_revision = 1;
  mutable std::uint64_t handle_revision = 1;
  mutable ExtrudeHandleState handle;
  mutable ExtrudeToolWorkStats work;
};

ExtrudeToolService::ExtrudeToolService(SceneDocument &scene,
                                     ModelingSessionService &modeling,
                                     EditorHistoryService &history,
                                     ExtrudeSessionDefaults &defaults)
    : impl_(std::make_unique<Impl>(scene, modeling, history, defaults)) {}

ExtrudeToolService::~ExtrudeToolService() = default;

bool ExtrudeToolService::active() const { return impl_->active; }
bool ExtrudeToolService::dragging() const { return impl_->drag.has_value(); }
bool ExtrudeToolService::adjusting() const {
  return dragging() && impl_->drag->previous_adjustment.has_value();
}
bool ExtrudeToolService::can_update_drag() const {
  return dragging() && impl_->modeling.face_extrusion_preview_active() &&
         impl_->modeling.face_extrusion_source_current(impl_->drag->source);
}
bool ExtrudeToolService::adjustable() const {
  return impl_->adjustment_current();
}
modeling::FaceExtrudeMode ExtrudeToolService::mode() const {
  return dragging() ? impl_->drag->options.mode : impl_->defaults.mode;
}
double ExtrudeToolService::amount() const {
  return dragging() ? impl_->drag->amount : impl_->defaults.amount;
}

bool ExtrudeToolService::has_selected_faces() const {
  return impl_->modeling.selection_mode() == ModelingSelectionMode::Face &&
         impl_->modeling.selection_summary().selected_face_count != 0;
}

std::uint64_t ExtrudeToolService::tool_settings_revision() const {
  impl_->observe_defaults();
  return impl_->settings_revision;
}

const ExtrudeHandleState &ExtrudeToolService::handle_state() const {
  if (!active()) {
    impl_->handle = {.revision = impl_->handle_revision,
                     .mode = mode()};
    return impl_->handle;
  }
  if (dragging()) {
    auto &drag = *impl_->drag;
    auto &handle = drag.handle;
    handle.revision = impl_->handle_revision;
    handle.amount_world = to_world_amount(drag.amount);
    const auto direction = drag.options.mode == modeling::FaceExtrudeMode::Region
                               ? drag.world_direction.value_or(drag.visual_direction)
                               : drag.visual_direction;
    handle.adjustment_direction = direction;
    handle.world_direction = direction;
    handle.world_position = drag.initial_pivot;
    if (drag.options.mode == modeling::FaceExtrudeMode::Region) {
      const auto geometry_direction =
          drag.world_direction.value_or(drag.source_direction);
      handle.world_position = drag.source_origin + geometry_direction *
          static_cast<float>(to_world_amount(drag.amount));
    }
    handle.adjustment_origin = handle.world_position;
    handle.dragging = true;
    handle.adjusting = adjusting();
    return handle;
  }
  const ModelingFaceExtrusionSourceResult &target = impl_->target();
  impl_->handle = {.active = target.ok && target.source.valid(),
                   .world_position = target.center_world,
                   .world_direction = target.handle_direction_world,
                   .revision = impl_->handle_revision,
                   .mode = mode(),
                   .adjustment_available = adjustable(),
                   .amount_world = to_world_amount(amount())};
  if (impl_->handle.adjustment_available) {
    impl_->handle.adjustment_origin = target.center_world;
    impl_->handle.adjustment_direction = impl_->adjustment->direction;
    if (mode() == modeling::FaceExtrudeMode::Region) {
      impl_->handle.world_direction = impl_->adjustment->direction;
    }
    impl_->handle.amount_world = to_world_amount(impl_->adjustment->options.amount);
  }
  return impl_->handle;
}

bool ExtrudeToolService::selected_face_hit(
    const ModelingPickSelectRequest &request) const {
  return active() && impl_->modeling.selected_face_extrusion_hit(request);
}

ExtrudeToolWorkStats ExtrudeToolService::work_stats_for_tests() const {
  return impl_->work;
}

CommandResult ExtrudeToolService::activate() {
  return activate(impl_->defaults.mode);
}

CommandResult ExtrudeToolService::activate(
    modeling::FaceExtrudeMode requested_mode) {
  const ExtrudeSessionDefaults options{requested_mode, impl_->defaults.amount};
  if (!valid_options(options)) {
    return command_invalid_result("Invalid extrusion settings.");
  }
  if (active()) {
    return start_extrusion(options);
  }
  // Reject invalid candidates before changing selection or viewport-tool state.
  if (has_selected_faces() && options.amount != 0.0) {
    const auto &target = impl_->target();
    if (!target.ok) {
      return command_invalid_result(target.message);
    }
    const auto validated = impl_->modeling.validate_face_extrusion(
        target.source, options.mode, to_world_amount(options.amount));
    if (!validated.ok) {
      return modeling_memento_command_result(validated);
    }
  }
  const auto previous_tool = impl_->modeling.tool_status().active_tool;
  CommandResult result = modeling_memento_command_result(
      impl_->modeling.activate_viewport_tool(ModelingViewportTool::Select));
  if (!result.ok) {
    return result;
  }
  result = modeling_memento_command_result(
      impl_->modeling.set_selection_mode(ModelingSelectionMode::Face));
  if (!result.ok) {
    return result;
  }
  impl_->active = true;
  ++impl_->settings_revision;
  ++impl_->handle_revision;
  result = start_extrusion(options);
  if (result.ok) {
    result.changed = true;
  } else {
    impl_->active = false;
    impl_->adjustment.reset();
    (void)impl_->modeling.activate_viewport_tool(previous_tool);
    ++impl_->settings_revision;
    ++impl_->handle_revision;
  }
  return result;
}

CommandResult ExtrudeToolService::refresh() {
  if (!active()) {
    return command_unchanged_result();
  }
  if (impl_->modeling.selection_mode() != ModelingSelectionMode::Face ||
      impl_->modeling.tool_status().active_tool != ModelingViewportTool::Select) {
    return cancel();
  }
  if (!dragging()) {
    impl_->retire_stale_adjustment();
  }
  impl_->observe_defaults();
  return command_unchanged_result();
}

CommandResult ExtrudeToolService::extrude() {
  return start_extrusion(impl_->defaults);
}

CommandResult ExtrudeToolService::start_extrusion(
    const ExtrudeSessionDefaults &requested_options) {
  // Callers may pass the defaults object itself; retain the requested value
  // across the geometry publication and preference update.
  const ExtrudeSessionDefaults options = requested_options;
  if (!active()) {
    return command_invalid_result("The Extrude tool is not active.");
  }
  if (dragging()) {
    return command_invalid_result("Finish the extrusion drag first.");
  }
  const CommandResult refreshed = refresh();
  if (!refreshed.ok || !active()) {
    return refreshed;
  }
  if (!valid_options(options)) {
    return command_invalid_result("Invalid extrusion settings.");
  }
  if (!has_selected_faces() || options.amount == 0.0) {
    const bool changed = !same_options(options, impl_->defaults) ||
                         impl_->adjustment.has_value();
    retire_adjustment();
    impl_->defaults = options;
    impl_->observe_defaults();
    return changed ? command_changed_result() : command_unchanged_result();
  }
  const ModelingFaceExtrusionSourceResult target = impl_->target();
  if (!target.ok) {
    return command_invalid_result(target.message);
  }
  if (!target.source.valid()) {
    return command_unchanged_result();
  }
  std::string key = next_extrusion_key();
  ++impl_->work.geometry_update_count;
  CommandResult result = impl_->history.execute(
      std::make_unique<ExtrudeHistoryCommand>(
          impl_->modeling, target.source, options.mode,
          to_world_amount(options.amount), std::nullopt, key),
      {.merge_key = key});
  if (result.ok && result.changed) {
    impl_->defaults = options;
    impl_->observe_defaults();
    impl_->accept_adjustment(target.source, std::nullopt, std::move(key),
                              target.center_world, target.direction_world,
                              options);
  }
  return result;
}

CommandResult ExtrudeToolService::set_options(
    const ExtrudeSessionDefaults &options) {
  if (!valid_options(options)) {
    return command_invalid_result("Invalid extrusion settings.");
  }
  if (dragging()) {
    return command_invalid_result("Finish the extrusion drag first.");
  }
  if (same_options(options, impl_->defaults)) {
    return command_unchanged_result();
  }
  impl_->retire_stale_adjustment();
  if (impl_->adjustment) {
    const Impl::Adjustment current = *impl_->adjustment;
    ++impl_->work.geometry_update_count;
    CommandResult applied = impl_->history.execute(
        std::make_unique<ExtrudeHistoryCommand>(
            impl_->modeling, current.source, options.mode,
            to_world_amount(options.amount), current.world_direction,
            current.merge_key),
        {.merge_key = current.merge_key});
    if (!applied.ok) {
      return applied;
    }
    impl_->accept_adjustment(current.source, current.world_direction,
                              current.merge_key, current.origin,
                              current.direction, options);
  }
  impl_->defaults = options;
  impl_->observe_defaults();
  return command_changed_result();
}

CommandResult ExtrudeToolService::set_mode(modeling::FaceExtrudeMode value) {
  ExtrudeSessionDefaults next = impl_->defaults;
  next.mode = value;
  return set_options(next);
}

CommandResult ExtrudeToolService::set_amount(double displayed_amount) {
  ExtrudeSessionDefaults next = impl_->defaults;
  next.amount = displayed_amount;
  return set_options(next);
}

CommandResult ExtrudeToolService::reset_settings() { return set_options({}); }

CommandResult ExtrudeToolService::begin_drag(
    std::optional<QVec3> world_direction) {
  if (!active() || dragging()) {
    return command_invalid_result("The Extrude tool cannot start a drag.");
  }
  impl_->retire_stale_adjustment();
  const ModelingFaceExtrusionSourceResult target = impl_->target();
  if (!target.ok || !target.source.valid()) {
    return command_invalid_result(target.ok ? "Select faces to extrude."
                                             : target.message);
  }
  Impl::Drag drag{.source = target.source,
                  .before = impl_->modeling.capture_history_memento(),
                  .world_direction = world_direction,
                  .original_world_direction = world_direction,
                  .handle = handle_state(),
                  .options = impl_->defaults,
                  .cancellation_adjustment = impl_->adjustment,
                  .source_origin = target.center_world,
                  .source_direction = target.direction_world,
                  .visual_direction = target.handle_direction_world,
                  .initial_pivot = target.center_world};
  drag.handle.adjustment_available = false;
  drag.handle.adjustment_origin = target.center_world;
  drag.handle.adjustment_direction =
      drag.world_direction.value_or(target.handle_direction_world);
  CommandResult result = modeling_memento_command_result(
      impl_->modeling.begin_face_extrusion_preview(
          target.source, drag.cancellation_adjustment
                             ? drag.cancellation_adjustment->source
                             : ModelingFaceExtrusionSource{}));
  if (!result.ok) {
    if (drag.cancellation_adjustment) {
      impl_->restore_adjustment_after_compensation(
          *drag.cancellation_adjustment);
    }
    return result;
  }
  impl_->drag = std::move(drag);
  impl_->adjustment.reset();
  ++impl_->settings_revision;
  ++impl_->handle_revision;
  return command_changed_result();
}

CommandResult ExtrudeToolService::begin_adjust_drag(
    std::optional<QVec3> world_direction) {
  if (!adjustable()) {
    return command_invalid_result("No extrusion result is available to adjust.");
  }
  const Impl::Adjustment previous = *impl_->adjustment;
  Impl::Drag drag{
      .source = previous.source,
      .before = impl_->modeling.capture_history_memento(),
      .world_direction = world_direction.has_value()
                             ? world_direction : previous.world_direction,
      .original_world_direction = previous.world_direction,
      .amount = previous.options.amount,
      .handle = handle_state(),
      .options = previous.options,
      .previous_adjustment = previous,
      .cancellation_adjustment = previous,
      .source_origin = previous.origin,
      .source_direction = previous.direction,
      .visual_direction = handle_state().world_direction,
      .initial_pivot = handle_state().adjustment_origin};
  drag.world_direction =
      Impl::canonical_drag_direction(drag, drag.world_direction);
  CommandResult result = modeling_memento_command_result(
      impl_->modeling.begin_face_extrusion_adjustment_preview(previous.source));
  if (!result.ok) {
    impl_->restore_adjustment_after_compensation(previous);
    return result;
  }
  impl_->drag = std::move(drag);
  impl_->adjustment.reset();
  ++impl_->settings_revision;
  ++impl_->handle_revision;
  return command_changed_result();
}

CommandResult ExtrudeToolService::update_drag(
    double displayed_amount, std::optional<QVec3> world_direction) {
  if (!dragging() || !std::isfinite(displayed_amount)) {
    return command_invalid_result("Invalid extrusion drag amount.");
  }
  Impl::Drag &drag = *impl_->drag;
  const std::optional<QVec3> direction = Impl::canonical_drag_direction(
      drag, world_direction.has_value() ? world_direction : drag.world_direction);
  if (drag.amount == displayed_amount &&
      same_direction(direction, drag.world_direction)) {
    return command_unchanged_result();
  }
  ++impl_->work.geometry_update_count;
  CommandResult result = modeling_memento_command_result(
      impl_->modeling.update_face_extrusion_preview(
          drag.source, mode(), to_world_amount(displayed_amount), direction));
  if (result.ok) {
    drag.amount = displayed_amount;
    drag.world_direction = direction;
    ++impl_->settings_revision;
    ++impl_->handle_revision;
  }
  return result;
}

CommandResult ExtrudeToolService::update_drag_world_amount(
    double world_amount, std::optional<QVec3> world_direction) {
  return update_drag(
      world_amount * viewport_host_defaults().map_units_per_world_unit,
      world_direction);
}

CommandResult ExtrudeToolService::end_drag() {
  if (!dragging()) {
    return command_unchanged_result();
  }
  const Impl::Drag drag = *impl_->drag;
  const bool adjusting_result = drag.previous_adjustment.has_value();
  if ((!adjusting_result && drag.amount == 0.0) ||
      (adjusting_result && drag.amount == drag.previous_adjustment->options.amount &&
       same_direction(drag.world_direction,
                      drag.previous_adjustment->world_direction))) {
    return cancel_drag();
  }
  if (adjusting_result &&
      !impl_->history.can_merge_with_latest(
          drag.previous_adjustment->merge_key,
          drag.previous_adjustment->history_revision)) {
    return command_invalid_result("The extrusion history changed during the drag.");
  }
  std::string key = adjusting_result ? drag.previous_adjustment->merge_key
                                    : next_extrusion_key();
  CommandResult result = impl_->history.execute(
      std::make_unique<ExtrudeHistoryCommand>(
          impl_->modeling, drag.source, drag.options.mode, to_world_amount(drag.amount),
          drag.world_direction, key, drag.before, true),
      {.merge_key = key});
  if (!result.ok) {
    return result;
  }
  impl_->drag.reset();
  if (result.changed) {
    impl_->defaults = {drag.options.mode, drag.amount};
    impl_->observe_defaults();
    impl_->accept_adjustment(
        drag.source, drag.world_direction, std::move(key),
        drag.source_origin,
        drag.options.mode == modeling::FaceExtrudeMode::Region
            ? drag.world_direction.value_or(drag.source_direction)
            : drag.source_direction,
        impl_->defaults);
  } else if (drag.cancellation_adjustment &&
             impl_->history.can_merge_with_latest(
                 drag.cancellation_adjustment->merge_key,
                 drag.cancellation_adjustment->history_revision)) {
    const auto &previous = *drag.cancellation_adjustment;
    impl_->accept_adjustment(previous.source, previous.world_direction,
                              previous.merge_key, previous.origin,
                              previous.direction, previous.options);
  }
  ++impl_->settings_revision;
  return command_changed_result();
}

CommandResult ExtrudeToolService::cancel_drag() {
  if (!dragging()) {
    return command_unchanged_result();
  }
  const auto previous = impl_->drag->cancellation_adjustment;
  CommandResult result = modeling_memento_command_result(
      impl_->modeling.cancel_face_extrusion_preview());
  if (!result.ok) {
    return result;
  }
  impl_->drag.reset();
  if (previous && impl_->history.can_merge_with_latest(
                      previous->merge_key, previous->history_revision)) {
    impl_->accept_adjustment(previous->source, previous->world_direction,
                              previous->merge_key, previous->origin,
                              previous->direction, previous->options);
  }
  ++impl_->settings_revision;
  ++impl_->handle_revision;
  return command_changed_result();
}

void ExtrudeToolService::retire_adjustment() {
  if (impl_->adjustment) {
    impl_->adjustment.reset();
    ++impl_->settings_revision;
    ++impl_->handle_revision;
  }
}

CommandResult ExtrudeToolService::cancel() {
  if (!active()) {
    return command_unchanged_result();
  }
  CommandResult result = cancel_drag();
  if (!result.ok) {
    return result;
  }
  retire_adjustment();
  impl_->active = false;
  ++impl_->settings_revision;
  ++impl_->handle_revision;
  return command_changed_result();
}

} // namespace quader
