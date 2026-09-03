////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace quader {

/** Stable user-facing command domains used by menus and command discovery. */
enum class CommandPresentationDomain {
  File,
  Edit,
  Select,
  Create,
  Object,
  Mesh,
  Vertex,
  Edge,
  Face,
  UV,
  View,
  Window,
  Help,
  Internal,
};

/** Stable semantic groups within a user-facing command domain. */
enum class CommandPresentationGroup {
  Project,
  SceneDocument,
  History,
  Clipboard,
  Selection,
  Instances,
  Grouping,
  Materials,
  Visibility,
  Creation,
  Transform,
  Arrangement,
  Duplication,
  Topology,
  Merge,
  Normals,
  Shading,
  Texture,
  Cleanup,
  Tools,
  Camera,
  Display,
  Layout,
  Grid,
  Preview,
  Discovery,
  Internal,
};

/** Describes how invoking a command affects the current interaction. */
enum class CommandPresentationActivation {
  Immediate,
  Toggle,
  Choice,
  InteractiveTool,
  Preview,
  PreviewCommit,
  PreviewCancel,
};

/** Presentation-only warning severity. Command execution remains authoritative. */
enum class CommandPresentationDanger {
  None,
  Caution,
  Destructive,
};

/**
 * Makes command-palette inclusion or exemption deliberate for every command.
 */
enum class CommandPalettePolicy {
  Searchable,
  ContextOnly,
  SelfExempt,
  Internal,
};

/** Declares surfaces where a command is intended to be projected. */
enum class CommandPresentationSurface : std::uint32_t {
  None = 0u,
  MainMenu = 1u << 0u,
  ViewportContextMenu = 1u << 1u,
  HierarchyContextMenu = 1u << 2u,
  LayersContextMenu = 1u << 3u,
  Toolbar = 1u << 4u,
  ToolSettings = 1u << 5u,
  CommandPalette = 1u << 6u,
};

[[nodiscard]] constexpr CommandPresentationSurface
operator|(CommandPresentationSurface left,
          CommandPresentationSurface right) noexcept {
  return static_cast<CommandPresentationSurface>(
      static_cast<std::uint32_t>(left) |
      static_cast<std::uint32_t>(right));
}

[[nodiscard]] constexpr CommandPresentationSurface
operator&(CommandPresentationSurface left,
          CommandPresentationSurface right) noexcept {
  return static_cast<CommandPresentationSurface>(
      static_cast<std::uint32_t>(left) &
      static_cast<std::uint32_t>(right));
}

/**
 * Describes placement and discovery for one stable command ID. This catalog
 * never evaluates availability and never owns command labels or execution.
 */
struct CommandPresentationDescriptor final {
  QuaderCommandId command_id;
  CommandPresentationDomain domain = CommandPresentationDomain::Internal;
  CommandPresentationGroup group = CommandPresentationGroup::Internal;
  /** Stable display order within the command's domain. */
  std::uint32_t rank = 0u;
  CommandPresentationSurface surfaces = CommandPresentationSurface::None;
  CommandPalettePolicy palette_policy = CommandPalettePolicy::Internal;
  CommandPresentationActivation activation =
      CommandPresentationActivation::Immediate;
  CommandPresentationDanger danger = CommandPresentationDanger::None;
  std::span<const std::string_view> aliases;
  std::span<const std::string_view> keywords;
  std::string_view settings_route;
  std::string_view help_route;
};

[[nodiscard]] constexpr bool has_command_presentation_surface(
    const CommandPresentationDescriptor &descriptor,
    CommandPresentationSurface surface) noexcept {
  return (descriptor.surfaces & surface) != CommandPresentationSurface::None;
}

/** Returns all typed command presentation records in stable display order. */
[[nodiscard]] std::span<const CommandPresentationDescriptor>
command_presentation_catalog() noexcept;

/** Performs a narrow stable-ID lookup without evaluating command availability. */
[[nodiscard]] const CommandPresentationDescriptor *
find_command_presentation(std::string_view command_id) noexcept;

[[nodiscard]] inline const CommandPresentationDescriptor *
find_command_presentation(QuaderCommandId command_id) noexcept {
  return find_command_presentation(command_id.string());
}

/** Stable identifiers and English fallbacks for category breadcrumbs. */
[[nodiscard]] std::string_view
command_presentation_domain_id(CommandPresentationDomain domain) noexcept;
[[nodiscard]] std::string_view
command_presentation_domain_label(CommandPresentationDomain domain) noexcept;
[[nodiscard]] std::string_view
command_presentation_group_id(CommandPresentationGroup group) noexcept;
[[nodiscard]] std::string_view
command_presentation_group_label(CommandPresentationGroup group) noexcept;

} // namespace quader
