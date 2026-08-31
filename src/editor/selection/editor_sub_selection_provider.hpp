////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/types.hpp"
#include "editor/modeling/modeling_result_types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace quader {

/** Identifies an editor-owned component selection projected beside mesh selection. */
enum class EditorSubSelectionKind {
  None,
  CurveControl,
};

/** Gives generic meaning to one provider-owned stable element identity. */
enum class EditorSubSelectionElementRole {
  Primary,
  SecondaryA,
  SecondaryB,
};

/** Identifies one active provider-owned element without leaking its authored type. */
struct EditorSubSelectionTarget {
  std::uint32_t index = 0;
  std::uint32_t generation = 0;
  EditorSubSelectionElementRole role =
      EditorSubSelectionElementRole::Primary;

  [[nodiscard]] bool operator==(
      const EditorSubSelectionTarget &) const = default;
};

/**
 * Stores the cheap retained facts required by the canonical selection owner.
 * Providers return an empty snapshot when their owner is stale or no longer in
 * the current selection scope.
 */
struct EditorSubSelectionSnapshot {
  EditorSubSelectionKind kind = EditorSubSelectionKind::None;
  bool participates_in_bulk_selection = false;
  EntityId entity = kInvalidEntityId;
  int modeling_object_id = -1;
  std::uint32_t selected_count = 0;
  std::optional<EditorSubSelectionTarget> active;
  std::uint64_t revision = 0;

  [[nodiscard]] bool operator==(
      const EditorSubSelectionSnapshot &) const = default;
};

/**
 * Pins the complete provider-owned selection for one deferred edit.
 * owner_revision and presentation_revision let providers restore observable
 * stamps exactly when a later owner in an atomic edit fails.
 */
struct EditorSubSelectionBaseline {
  EditorSubSelectionKind kind = EditorSubSelectionKind::None;
  EntityId entity = kInvalidEntityId;
  int modeling_object_id = -1;
  std::vector<EditorSubSelectionTarget> selected;
  std::optional<EditorSubSelectionTarget> active;
  std::optional<EditorSubSelectionTarget> hover;
  std::uint64_t revision = 0;
  // Provider-owned stamps required to compensate a failed multi-owner edit
  // without publishing transient presentation churn. Providers that do not
  // expose separate owner/presentation revisions may leave these at zero.
  std::uint64_t owner_revision = 0;
  std::uint64_t presentation_revision = 0;

  [[nodiscard]] bool valid() const noexcept {
    return kind != EditorSubSelectionKind::None;
  }

  [[nodiscard]] bool operator==(
      const EditorSubSelectionBaseline &) const = default;
};

/**
 * Enumerates canonical replace, additive, subtractive, and toggle edits for
 * provider selections.
 */
enum class EditorSubSelectionEdit {
  Replace,
  Add,
  Remove,
  Toggle,
};

/**
 * Projects an editor-owned component selection through EditorSelectionService.
 * Geometry, validation, history, and preview policy remain with the provider.
 */
class EditorSubSelectionProvider {
public:
  virtual ~EditorSubSelectionProvider() = default;

  [[nodiscard]] virtual EditorSubSelectionSnapshot
  sub_selection_snapshot() const = 0;
  [[nodiscard]] virtual EditorSubSelectionBaseline
  capture_sub_selection_baseline() const = 0;
  virtual void synchronize_sub_selection_scope() = 0;
  virtual ModelingCommandResult edit_sub_elements(
      std::span<const EditorSubSelectionTarget> hits,
      EditorSubSelectionEdit edit,
      const EditorSubSelectionBaseline *baseline,
      std::optional<EditorSubSelectionTarget> direct_hit) = 0;
  /** Restores an edit rollback point, including its exact revision stamps. */
  virtual ModelingCommandResult restore_sub_selection_baseline(
      const EditorSubSelectionBaseline &baseline) = 0;
  virtual ModelingCommandResult preview_sub_element_hover(
      std::optional<EditorSubSelectionTarget> target) = 0;
  virtual ModelingCommandResult select_all_sub_elements() = 0;
  virtual ModelingCommandResult invert_sub_elements() = 0;
  virtual ModelingCommandResult clear_sub_elements() = 0;
  virtual ModelingCommandResult delete_selected_sub_elements() = 0;
};

} // namespace quader
