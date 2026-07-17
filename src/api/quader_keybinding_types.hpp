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
};

/**
 * Describes one semantic action/context row in the public keybinding catalog.
 * Resolver aliases and alternate shortcuts are retained in their vectors;
 * fully unmapped actions remain represented by one entry with no shortcuts.
 */
struct QuaderKeybindingCatalogEntry {
  std::string action_id;
  std::string target_id;
  std::vector<std::string> target_ids;
  std::string label;
  std::string localization_key;
  QuaderKeybindingContextKind context = QuaderKeybindingContextKind::Global;
  std::string when;
  std::string shortcut;
  std::vector<std::string> shortcuts;
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
