////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <editor/modeling/runtime/selection_types.hpp>

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>
#include <vector>

namespace quader_editor_core {

SelectionEditOperation selection_edit_operation_from_id(int operation);
int selection_edit_operation_id(SelectionEditOperation operation);
SelectionEditOperation selection_edit_operation_from_modifiers(bool ctrl_pressed, bool shift_pressed);
bool selection_edit_operation_preserves_existing(SelectionEditOperation operation);

/**
 * Applies the editor's deterministic selection set algebra to two ordered-key
 * domains. Inputs are canonicalized here so individual selection owners do not
 * grow their own subtly different Add/Remove/Toggle implementations.
 */
template <typename Key>
[[nodiscard]] std::vector<Key> apply_selection_edit_operation(
    std::vector<Key> baseline, std::vector<Key> hits,
    SelectionEditOperation operation) {
  const auto canonicalize = [](std::vector<Key> &keys) {
    std::sort(keys.begin(), keys.end());
    keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
  };
  canonicalize(baseline);
  canonicalize(hits);
  if (operation == SelectionEditOperation::Replace) {
    return hits;
  }

  std::vector<Key> selected;
  selected.reserve(baseline.size() + hits.size());
  if (operation == SelectionEditOperation::Add) {
    std::set_union(baseline.begin(), baseline.end(), hits.begin(), hits.end(),
                   std::back_inserter(selected));
  } else if (operation == SelectionEditOperation::Remove) {
    std::set_difference(baseline.begin(), baseline.end(), hits.begin(),
                        hits.end(), std::back_inserter(selected));
  } else {
    std::set_symmetric_difference(
        baseline.begin(), baseline.end(), hits.begin(), hits.end(),
        std::back_inserter(selected));
  }
  return selected;
}

template <typename Key> struct OrderedSelectionEditResult {
  std::vector<Key> selected;
  std::optional<Key> active;
};

/**
 * Resolves the active member after applying the shared set edit. A direct hit
 * activates the clicked member when it survives the operation. Rectangle edits
 * omit direct_hit and therefore preserve the baseline active member when
 * possible, matching component selection throughout the editor.
 */
template <typename Key>
[[nodiscard]] OrderedSelectionEditResult<Key> resolve_selection_edit_operation(
    std::vector<Key> baseline, std::vector<Key> hits,
    std::optional<Key> baseline_active, SelectionEditOperation operation,
    std::optional<Key> direct_hit = std::nullopt) {
  OrderedSelectionEditResult<Key> result{
      .selected = apply_selection_edit_operation(std::move(baseline),
                                                 std::move(hits), operation),
  };
  const auto selected_contains = [&result](const Key &key) {
    return std::binary_search(result.selected.begin(), result.selected.end(),
                              key);
  };
  if (direct_hit && selected_contains(*direct_hit)) {
    result.active = *direct_hit;
  } else if (operation != SelectionEditOperation::Replace && baseline_active &&
             selected_contains(*baseline_active)) {
    result.active = *baseline_active;
  } else if (!result.selected.empty()) {
    result.active = result.selected.back();
  }
  return result;
}

} // namespace quader_editor_core
