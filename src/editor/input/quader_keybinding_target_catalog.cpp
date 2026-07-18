////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/input/quader_keybinding_target_catalog.hpp"

#include <utility>

namespace quader {
namespace {

/**
 * Carries the stable label and context for one keybinding scope.
 */
struct KeybindingScopePresentation final {
  std::string id;
  std::string label;
  std::string localization_key;
  QuaderKeybindingContextKind context = QuaderKeybindingContextKind::Global;
};

constexpr std::uint32_t kSelectionModeVertex = 1u << 0u;
constexpr std::uint32_t kSelectionModeEdge = 1u << 1u;
constexpr std::uint32_t kSelectionModeFace = 1u << 2u;
constexpr std::uint32_t kSelectionModeObject = 1u << 3u;
constexpr std::uint32_t kSelectionModeGroup = 1u << 4u;
constexpr std::uint32_t kSelectionModeComponent =
    kSelectionModeVertex | kSelectionModeEdge | kSelectionModeFace;
constexpr std::uint32_t kSelectionModeAny =
    kSelectionModeComponent | kSelectionModeObject | kSelectionModeGroup;

KeybindingScopePresentation
scope_for_context(QuaderKeybindingContextKind context) {
  switch (context) {
  case QuaderKeybindingContextKind::Global:
    return {"global", "Global", "keybindings.scope.global", context};
  case QuaderKeybindingContextKind::Viewport:
    return {"viewport", "Viewport", "keybindings.scope.viewport", context};
  case QuaderKeybindingContextKind::ViewportNavigation:
    return {"viewport.navigation", "Viewport › Navigation",
            "keybindings.scope.viewport.navigation", context};
  case QuaderKeybindingContextKind::FlyNavigation:
    return {"viewport.navigation.fly", "Viewport › Fly Navigation",
            "keybindings.scope.viewport.navigation.fly", context};
  case QuaderKeybindingContextKind::OperationPreview:
    return {"viewport.operation_preview", "Viewport › Operation Preview",
            "keybindings.scope.viewport.operation_preview", context};
  case QuaderKeybindingContextKind::ModalTool:
    return {"viewport.tool.modal", "Viewport › Modal Tool",
            "keybindings.scope.viewport.modal_tool", context};
  case QuaderKeybindingContextKind::LinearArrayTool:
    return {"viewport.tool.linear_array", "Viewport › Linear Array",
            "keybindings.scope.viewport.tool.linear_array", context};
  case QuaderKeybindingContextKind::RadialArrayTool:
    return {"viewport.tool.radial_array", "Viewport › Radial Array",
            "keybindings.scope.viewport.tool.radial_array", context};
  case QuaderKeybindingContextKind::Custom:
    return {"custom", "Custom", "keybindings.scope.custom", context};
  }
  return {"custom", "Custom", "keybindings.scope.custom",
          QuaderKeybindingContextKind::Custom};
}

KeybindingScopePresentation
scope_for_selection_modes(std::uint32_t mask) {
  switch (mask) {
  case kSelectionModeVertex:
    return {"viewport.selection.vertex", "Viewport › Vertex",
            "keybindings.scope.viewport.selection.vertex",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeEdge:
    return {"viewport.selection.edge", "Viewport › Edge",
            "keybindings.scope.viewport.selection.edge",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeFace:
    return {"viewport.selection.face", "Viewport › Face",
            "keybindings.scope.viewport.selection.face",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeObject:
    return {"viewport.selection.object", "Viewport › Object",
            "keybindings.scope.viewport.selection.object",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeGroup:
    return {"viewport.selection.group", "Viewport › Group",
            "keybindings.scope.viewport.selection.group",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeComponent:
    return {"viewport.selection.component", "Viewport › Component",
            "keybindings.scope.viewport.selection.component",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeEdge | kSelectionModeFace:
    return {"viewport.selection.edge_or_face", "Viewport › Edge / Face",
            "keybindings.scope.viewport.selection.edge_or_face",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeObject | kSelectionModeFace:
    return {"viewport.selection.object_or_face", "Viewport › Object / Face",
            "keybindings.scope.viewport.selection.object_or_face",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeObject | kSelectionModeGroup:
    return {"viewport.selection.object_or_group", "Viewport › Object / Group",
            "keybindings.scope.viewport.selection.object_or_group",
            QuaderKeybindingContextKind::Viewport};
  case kSelectionModeAny:
    return {"viewport.selection.any", "Viewport › Any Selection Mode",
            "keybindings.scope.viewport.selection.any",
            QuaderKeybindingContextKind::Viewport};
  default:
    return {"viewport.selection", "Viewport › Mesh",
            "keybindings.scope.viewport.selection",
            QuaderKeybindingContextKind::Viewport};
  }
}

std::string selection_mode_when_clause(std::uint32_t mask) {
  std::string result;
  const auto append = [&](std::string_view identifier) {
    if (!result.empty()) {
      result.append(" || ");
    }
    result.append(identifier);
  };
  if ((mask & kSelectionModeVertex) != 0u) {
    append("selectionMode.vertex");
  }
  if ((mask & kSelectionModeEdge) != 0u) {
    append("selectionMode.edge");
  }
  if ((mask & kSelectionModeFace) != 0u) {
    append("selectionMode.face");
  }
  if ((mask & kSelectionModeObject) != 0u) {
    append("selectionMode.object");
  }
  if ((mask & kSelectionModeGroup) != 0u) {
    append("selectionMode.group");
  }
  if (result.find(" || ") != std::string::npos) {
    result = "(" + result + ")";
  }
  return result;
}

} // namespace

void QuaderKeybindingTargetCatalog::register_target(
    std::string target_id, std::string label, std::string localization_key,
    QuaderKeybindingContextKind context, std::string when,
    std::string default_shortcut, std::string action_id) {
  const KeybindingScopePresentation scope = scope_for_context(context);
  for (QuaderKeybindingTargetMetadata &metadata : entries_) {
    if (metadata.target_id != target_id) {
      continue;
    }
    metadata.label = std::move(label);
    metadata.localization_key = std::move(localization_key);
    metadata.context = context;
    if (!metadata.selection_mode_scoped) {
      metadata.scope_id = scope.id;
      metadata.scope_label = scope.label;
      metadata.scope_localization_key = scope.localization_key;
      metadata.when = std::move(when);
    }
    if (!default_shortcut.empty()) {
      metadata.default_shortcut = std::move(default_shortcut);
    }
    if (!action_id.empty()) {
      metadata.action_id = std::move(action_id);
    }
    return;
  }
  if (action_id.empty()) {
    action_id = target_id;
  }
  entries_.push_back({
      .action_id = std::move(action_id),
      .target_id = std::move(target_id),
      .label = std::move(label),
      .localization_key = std::move(localization_key),
      .context = context,
      .scope_id = scope.id,
      .scope_label = scope.label,
      .scope_localization_key = scope.localization_key,
      .when = std::move(when),
      .default_shortcut = std::move(default_shortcut),
  });
}

void QuaderKeybindingTargetCatalog::set_action_id(
    std::string_view target_id, std::string action_id) {
  for (QuaderKeybindingTargetMetadata &metadata : entries_) {
    if (metadata.target_id == target_id) {
      metadata.action_id = std::move(action_id);
      return;
    }
  }
}

void QuaderKeybindingTargetCatalog::set_selection_modes(
    std::string_view target_id, std::uint32_t selection_mode_mask,
    std::string_view viewport_when) {
  if (selection_mode_mask == 0u) {
    return;
  }
  for (QuaderKeybindingTargetMetadata &metadata : entries_) {
    if (metadata.target_id != target_id) {
      continue;
    }
    const KeybindingScopePresentation scope =
        scope_for_selection_modes(selection_mode_mask);
    metadata.context = scope.context;
    metadata.scope_id = scope.id;
    metadata.scope_label = scope.label;
    metadata.scope_localization_key = scope.localization_key;
    metadata.selection_mode_mask = selection_mode_mask;
    metadata.selection_mode_scoped = true;
    metadata.when = viewport_when;
    const std::string clause = selection_mode_when_clause(selection_mode_mask);
    if (!clause.empty()) {
      metadata.when.append(" && ").append(clause);
    }
    return;
  }
}

void QuaderKeybindingTargetCatalog::finalize_scope_metadata() {
  for (QuaderKeybindingTargetMetadata &metadata : entries_) {
    if (metadata.scope_id.empty()) {
      const KeybindingScopePresentation scope =
          scope_for_context(metadata.context);
      metadata.scope_id = scope.id;
      metadata.scope_label = scope.label;
      metadata.scope_localization_key = scope.localization_key;
    }
  }

  for (QuaderKeybindingTargetMetadata &metadata : entries_) {
    if (!metadata.selection_mode_scoped) {
      continue;
    }
    std::uint32_t combined_mask = 0u;
    bool has_unscoped_alias = false;
    for (const QuaderKeybindingTargetMetadata &alias : entries_) {
      if (alias.action_id != metadata.action_id) {
        continue;
      }
      has_unscoped_alias = has_unscoped_alias || !alias.selection_mode_scoped;
      combined_mask |= alias.selection_mode_mask;
    }
    if (has_unscoped_alias || combined_mask == 0u) {
      continue;
    }
    const KeybindingScopePresentation scope =
        scope_for_selection_modes(combined_mask);
    for (QuaderKeybindingTargetMetadata &alias : entries_) {
      if (alias.action_id == metadata.action_id) {
        alias.context = scope.context;
        alias.scope_id = scope.id;
        alias.scope_label = scope.label;
        alias.scope_localization_key = scope.localization_key;
      }
    }
  }
}

const QuaderKeybindingTargetMetadata *
QuaderKeybindingTargetCatalog::find(std::string_view target_id) const {
  for (const QuaderKeybindingTargetMetadata &metadata : entries_) {
    if (metadata.target_id == target_id) {
      return &metadata;
    }
  }
  return nullptr;
}

const std::vector<QuaderKeybindingTargetMetadata> &
QuaderKeybindingTargetCatalog::entries() const noexcept {
  return entries_;
}

} // namespace quader
