////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <cstdint>
#include <span>
#include <string_view>

namespace quader::test {

/**
 * Names the minimum deterministic editor setup used to exercise one command.
 *
 * Fixtures deliberately describe user-visible state, rather than command
 * implementation details. A command-specific fixture builder may add topology
 * needed by that command, but it must preserve the minimum state documented
 * here.
 */
enum class CanonicalCommandFixture : std::uint8_t {
  /// A fresh editor with no user-created entity or pending interaction.
  EmptyEditor,
  /// A writable temporary project with one clean, active scene document.
  ProjectWithScene,
  /// A writable temporary project with two clean, open scene documents.
  TwoScenes,
  /// One visible, unlocked mesh selected in Object mode.
  ObjectSelection,
  /// Two visible, unlocked sibling meshes selected in Object mode; one active.
  ObjectPairSelection,
  /// Three spatially separated sibling meshes selected in Object mode; one active.
  ObjectSetSelection,
  /// Two exact-geometry sibling meshes in Object mode with only one selected.
  IdenticalObjectsOneSelected,
  /// One selected Object-mode mesh containing at least one face with >4 sides.
  NgonObjectSelection,
  /// One visible, unlocked group containing mesh members selected in Group mode.
  GroupSelection,
  /// One valid instance occurrence selected in Object mode outside edit scope.
  InstanceSelection,
  /// A valid instance definition edit scope with one editable member selected.
  InstanceEdit,
  /// One vertex selected on a connected, non-trivial mesh in Vertex mode.
  SingleVertexSelection,
  /// Two vertices selected on one mesh in Vertex mode; one is active.
  VertexPairSelection,
  /// At least three non-collinear vertices selected on one mesh in Vertex mode.
  VertexSetSelection,
  /// One manifold edge selected on a loop-capable mesh in Edge mode.
  SingleEdgeSelection,
  /// Two compatible edges selected on one mesh in Edge mode; one is active.
  EdgePairSelection,
  /// Every edge of one open boundary selected on a single mesh in Edge mode.
  BoundaryLoopSelection,
  /// One face selected on a mesh with adjacent and parallel faces in Face mode.
  SingleFaceSelection,
  /// Two compatible faces selected on one mesh in Face mode; one is active.
  FacePairSelection,
  /// One hidden mesh exists while Object mode remains active.
  HiddenObject,
  /// One selected mesh belongs to at least one removable scene layer.
  LayeredObjectSelection,
  /// The internal modeling clipboard contains one copied mesh selection.
  ClipboardObject,
  /// One committed, undoable modeling mutation is at the history head.
  UndoHistory,
  /// One committed modeling mutation has just been undone and can be redone.
  RedoHistory,
  /// One repeatable modeling operation has completed and remains applicable.
  RepeatHistory,
  /// One valid modeling operation preview is active and has a visible result.
  ModelingPreview,
  /// The primitive-creation tool owns an active, committable preview session.
  PrimitivePreview,
  /// A focused viewport pane exists with no modal interaction or navigation.
  Viewport,
  /// The owning OS/UI/runtime hook is attached to a deterministic test double.
  ExternalRuntime,
  /// An isolated asymmetric face with authored UVs and signed texture settings.
  TextureFaceSelection,
  /// One edge of an open, asymmetric authored-UV quad selected in Edge mode.
  UvPeelEdgeSelection,
  /// One visible, unlocked, nondegenerate Curve selected in Object mode.
  CurveSelection,
  /// One explicitly Hard manifold edge selected between Smooth faces.
  AuthoredHardEdgeSelection,
  /// One visible, unlocked selected mesh parented beneath another mesh.
  ParentedObjectSelection,
};

/**
 * Identifies observable state domains a command is allowed to mutate.
 */
enum class ObservableDomain : std::uint64_t {
  None = 0,
  Document = 1ULL << 0U,
  ModelingContent = 1ULL << 1U,
  Selection = 1ULL << 2U,
  Overlay = 1ULL << 3U,
  Assets = 1ULL << 4U,
  History = 1ULL << 5U,
  Tool = 1ULL << 6U,
  Viewport = 1ULL << 7U,
  Preferences = 1ULL << 8U,
  Revisions = 1ULL << 9U,
  Project = 1ULL << 10U,
  SceneDocuments = 1ULL << 11U,
  SceneGraph = 1ULL << 12U,
  Visibility = 1ULL << 13U,
  Layers = 1ULL << 14U,
  Clipboard = 1ULL << 15U,
  Instances = 1ULL << 16U,
  ExternalRuntime = 1ULL << 17U,
};

[[nodiscard]] constexpr ObservableDomain
operator|(ObservableDomain lhs, ObservableDomain rhs) {
  return static_cast<ObservableDomain>(static_cast<std::uint64_t>(lhs) |
                                       static_cast<std::uint64_t>(rhs));
}

[[nodiscard]] constexpr ObservableDomain
operator&(ObservableDomain lhs, ObservableDomain rhs) {
  return static_cast<ObservableDomain>(static_cast<std::uint64_t>(lhs) &
                                       static_cast<std::uint64_t>(rhs));
}

constexpr ObservableDomain &operator|=(ObservableDomain &lhs,
                                       ObservableDomain rhs) {
  lhs = lhs | rhs;
  return lhs;
}

[[nodiscard]] constexpr bool
has_observable_domain(ObservableDomain domains, ObservableDomain domain) {
  return (domains & domain) == domain;
}

/**
 * States the required result of executing a command on its canonical fixture.
 */
enum class CommandOutcomePolicy : std::uint8_t {
  /// Execution must succeed and report a user-observable change.
  ChangeRequired,
  /// Execution must succeed; a legitimate no-op result is accepted.
  SuccessRequired,
  /// The attached runtime or topology may legitimately reject execution.
  MayReject,
};

/**
 * States the history/repeat contract associated with a successful command.
 */
enum class CommandUndoRepeatPolicy : std::uint8_t {
  None,
  HistoryUndo,
  HistoryRedo,
  HistoryRepeat,
  Undoable,
  UndoableAndRepeatable,
  BeginPreview,
  CommitPreview,
  CancelPreview,
};

/**
 * Marks exceptional execution characteristics which generic contract runners
 * must handle explicitly rather than silently weakening every command check.
 */
enum class CommandBehaviorFlags : std::uint8_t {
  None = 0,
  ExternalRuntimeHookOnly = 1U << 0U,
  MayRejectWhenAvailable = 1U << 1U,
  DebugOnly = 1U << 2U,
  GeneratedCommandId = 1U << 3U,
};

[[nodiscard]] constexpr CommandBehaviorFlags
operator|(CommandBehaviorFlags lhs, CommandBehaviorFlags rhs) {
  return static_cast<CommandBehaviorFlags>(
      static_cast<std::uint8_t>(lhs) | static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr CommandBehaviorFlags
operator&(CommandBehaviorFlags lhs, CommandBehaviorFlags rhs) {
  return static_cast<CommandBehaviorFlags>(
      static_cast<std::uint8_t>(lhs) & static_cast<std::uint8_t>(rhs));
}

[[nodiscard]] constexpr bool has_command_behavior_flag(
    CommandBehaviorFlags flags, CommandBehaviorFlags flag) {
  return (flags & flag) == flag;
}

/**
 * Defines one explicit, test-owned behavior contract for a public command.
 */
struct CommandBehaviorContract {
  std::string_view command_id;
  CanonicalCommandFixture fixture = CanonicalCommandFixture::EmptyEditor;
  ObservableDomain observable_domains = ObservableDomain::None;
  CommandOutcomePolicy outcome = CommandOutcomePolicy::SuccessRequired;
  CommandUndoRepeatPolicy undo_repeat = CommandUndoRepeatPolicy::None;
  CommandBehaviorFlags flags = CommandBehaviorFlags::None;
};

/**
 * Returns the explicit behavior contract catalog for every visible command.
 */
[[nodiscard]] std::span<const CommandBehaviorContract>
command_behavior_contracts();

/**
 * Finds the explicit contract for a canonical command id.
 */
[[nodiscard]] const CommandBehaviorContract *
find_command_behavior_contract(std::string_view command_id);

} // namespace quader::test
