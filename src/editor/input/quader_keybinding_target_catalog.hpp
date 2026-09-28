////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_keybinding_types.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

/**
 * Stores stable presentation metadata independently from effective resolver
 * rules so removed bindings remain discoverable.
 */
struct QuaderKeybindingTargetMetadata final {
  std::string action_id;
  std::string target_id;
  std::string label;
  std::string localization_key;
  QuaderKeybindingContextKind context = QuaderKeybindingContextKind::Global;
  std::string scope_id;
  std::string scope_label;
  std::string scope_localization_key;
  std::uint32_t selection_mode_mask = 0u;
  bool selection_mode_scoped = false;
  std::string when;
  std::string default_shortcut;
};

/**
 * Owns keybinding target presentation, scope, and alias-group policy. Effective
 * shortcut resolution and persistence remain owned by QuaderKeybindingService.
 */
class QuaderKeybindingTargetCatalog final {
public:
  void register_target(std::string target_id, std::string label,
                       std::string localization_key,
                       QuaderKeybindingContextKind context, std::string when,
                       std::string default_shortcut = {},
                       std::string action_id = {});
  void set_action_id(std::string_view target_id, std::string action_id);
  void set_selection_modes(std::string_view target_id,
                           std::uint32_t selection_mode_mask,
                           std::string_view viewport_when,
                           std::string_view alternate_when = {});
  void finalize_scope_metadata();

  [[nodiscard]] const QuaderKeybindingTargetMetadata *
  find(std::string_view target_id) const;
  [[nodiscard]] const std::vector<QuaderKeybindingTargetMetadata> &
  entries() const noexcept;

private:
  std::vector<QuaderKeybindingTargetMetadata> entries_;
};

} // namespace quader
