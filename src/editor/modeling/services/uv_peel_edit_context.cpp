////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/modeling/services/uv_peel_edit_context.hpp"

#include <atomic>

namespace quader {
namespace {

std::uint64_t next_uv_peel_selection_token() {
  // Distinguish replaced services and independent scene sessions with equal IDs.
  static std::atomic<std::uint64_t> next{1};
  return next.fetch_add(1, std::memory_order_relaxed);
}

} // namespace

UvPeelEditContext::UvPeelEditContext(UvPeelSessionDefaults *defaults)
    : defaults_(defaults != nullptr ? *defaults : owned_defaults_) {}

ModelingUvPeelSettings UvPeelEditContext::synchronize(
    const ModelingSelectedUvPeelSnapshot &snapshot,
    std::uint64_t content_revision, std::uint64_t selection_revision,
    ModelingInstanceEditScope instance_scope) {
  if (!initialized_ || content_revision_ != content_revision ||
      selection_revision_ != selection_revision ||
      instance_scope_ != instance_scope ||
      active_modeling_object_id_ != snapshot.active_modeling_object_id ||
      selected_objects_ != snapshot.objects) {
    selected_objects_ = snapshot.objects;
    content_revision_ = content_revision;
    selection_revision_ = selection_revision;
    instance_scope_ = instance_scope;
    active_modeling_object_id_ = snapshot.active_modeling_object_id;
    selection_token_ = next_uv_peel_selection_token();
    initialized_ = true;
    ++revision_;
  }

  std::size_t count = 0;
  for (const auto &object : snapshot.objects) {
    count += object.edges.size();
  }
  const bool editable = snapshot.command.ok && count != 0u;
  return {
      .selected_edge_count = count,
      .editable = editable,
      .options = defaults_.options,
      .block_reason = editable ? ModelingUvPeelBlockReason::None
                              : ModelingUvPeelBlockReason::NoSelection,
      .disabled_reason = snapshot.command.message,
      .selection_token = selection_token_,
  };
}

ModelingCommandResult
UvPeelEditContext::set_options(const ModelingUvPeelOptions &options) {
  const auto validated = modeling::validate_uv_peel_options(options);
  if (!validated.ok()) {
    return {.ok = false, .message = validated.error_message()};
  }
  if (defaults_.options == validated.value()) {
    return {.ok = true, .changed = false};
  }
  defaults_.options = validated.value();
  ++defaults_.revision;
  return {.ok = true, .changed = true};
}

void UvPeelEditContext::restore_options(
    const UvPeelSessionDefaults &snapshot) {
  if (defaults_.options == snapshot.options) {
    return;
  }
  defaults_.options = snapshot.options;
  ++defaults_.revision;
}

void UvPeelEditContext::invalidate_selection() {
  initialized_ = false;
  selection_token_ = next_uv_peel_selection_token();
  ++revision_;
}

} // namespace quader
