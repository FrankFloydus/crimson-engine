////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_input_types.hpp"

#include <cstddef>
#include <string>
#include <vector>

namespace quader {

/**
 * Classifies the editor state in which a keybinding target is active.
 */
enum class QuaderKeybindingContextKind {
  Global,
  Viewport,
  ViewportNavigation,
  FlyNavigation,
  OperationPreview,
  ModalTool,
  LinearArrayTool,
  RadialArrayTool,
  Custom,
  CurveTool,
};

/**
 * Identifies one user-facing action row without exposing resolver aliases.
 */
struct QuaderKeybindingActionKey {
  std::string action_id;
  std::string scope_id;

  bool operator==(const QuaderKeybindingActionKey &) const = default;
};

/**
 * Describes where the effective mappings for one catalog action originate.
 */
enum class QuaderKeybindingSourceKind {
  Default,
  User,
};

/**
 * Selects how an action-level edit handles overlapping mappings.
 */
enum class QuaderKeybindingConflictPolicy {
  Reject,
  Replace,
};

/**
 * Classifies one mapping relationship found during assignment analysis.
 */
enum class QuaderKeybindingCollisionKind {
  CompatibleReuse,
  ExactConflict,
  PrefixConflict,
};

/**
 * Describes one action involved in a proposed mapping relationship.
 */
struct QuaderKeybindingCollision {
  QuaderKeybindingActionKey action;
  std::string target_id;
  std::string label;
  std::string localization_key;
  std::string shortcut;
  std::string when;
  QuaderKeybindingSourceKind source = QuaderKeybindingSourceKind::Default;
  QuaderKeybindingCollisionKind kind =
      QuaderKeybindingCollisionKind::ExactConflict;
};

/**
 * Stores the canonical result of analyzing one proposed mapping.
 */
struct QuaderKeybindingAssignmentAnalysis {
  bool ok = false;
  std::string message;
  std::string canonical_shortcut;
  std::vector<QuaderKeybindingCollision> compatible_reuses;
  std::vector<QuaderKeybindingCollision> conflicts;
};

/**
 * Describes one semantic action/scope row in the public keybinding catalog.
 * Resolver aliases remain internal targets; every row owns at most one
 * physical mapping and fully unmapped actions remain represented.
 */
struct QuaderKeybindingCatalogEntry {
  QuaderKeybindingActionKey key;
  std::string action_id;
  std::string target_id;
  std::vector<std::string> target_ids;
  std::string label;
  std::string localization_key;
  QuaderKeybindingContextKind context = QuaderKeybindingContextKind::Global;
  std::string scope_id;
  std::string scope_label;
  std::string scope_localization_key;
  std::string when;
  std::string shortcut;
  std::string default_shortcut;
  QuaderKeybindingSourceKind source = QuaderKeybindingSourceKind::Default;
  bool customized = false;
  bool unassigned = false;
};

/**
 * Stores a keybinding rule exposed by the public command API.
 */
struct QuaderKeybindingRule {
  std::string target_id;
  std::vector<QuaderInputChord> sequence;
  std::string shortcut;
  std::string when;
  std::string source = "default";
  int weight = 0;
  bool removal = false;
  std::size_t order = 0u;
};

/**
 * Stores keybinding conflict information for rebinding UI and tests.
 */
struct QuaderKeybindingConflict {
  std::string target_id;
  std::string shortcut;
  std::string when;
  std::string source;
};

} // namespace quader
