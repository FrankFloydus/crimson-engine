////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api_types.hpp"
#include "api/quader_keybinding_types.hpp"
#include "api/quader_viewport_input_types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class QuaderApi;
enum class QuaderSelectionMode;
#ifndef NDEBUG
class QuaderDevToolsApi;
#endif
class QuaderProjectApi;
class QuaderSceneDocumentsApi;
class QuaderPreferencesApi;
class QuaderToolsApi;
class QuaderViewportApi;

/**
 * Stores the result of dispatching one viewport-scoped keybinding.
 */
struct QuaderViewportKeybindingDispatch {
    /**
     * Enumerates viewport keybinding dispatch result kinds used by the public command layer.
     */
    enum class Kind {
        NoMatch,
        PendingChord,
        ViewportInputAction,
        ViewportShortcut,
        Command,
    };

    Kind kind = Kind::NoMatch;
    ViewportInputActionKind action = ViewportInputActionKind::None;
    ViewportShortcut shortcut = {};
    std::string command_id;
};

/** Identifies editor intent resolved from a focused native Tool Settings edit. */
enum class QuaderNativeToolSettingsIntent {
  None,
  Apply,
  Cancel,
};

/**
 * Preserves the shared keybinding sequence result without exposing a concrete
 * native tool or owner-specific command to Qt.
 */
struct QuaderNativeToolSettingsDispatch final {
  /** Classifies a native Tool Settings dispatch as unmatched, pending, or resolved. */
  enum class Kind {
    NoMatch,
    PendingChord,
    Intent,
  };

  Kind kind = Kind::NoMatch;
  QuaderNativeToolSettingsIntent intent =
      QuaderNativeToolSettingsIntent::None;
};

/**
 * Stores transient viewport state exposed to keybinding when-expressions.
 */
struct QuaderViewportKeybindingContext {
  bool viewport_hovered = false;
  bool orthographic_view = false;
  bool navigation_active = false;
  bool fly_navigation_active = false;
  bool orbit_navigation_active = false;
  bool pan_navigation_active = false;
  bool modal_tool_active = false;
  bool active_tool_owns_viewport = false;
  bool active_transform_drag = false;
  bool operation_preview_active = false;
  bool curve_controls_editable = false;
};

/**
 * Public command and keybinding gateway for editor intent.
 *
 * C++ callers should prefer typed command IDs from quader::quader_command, for
 * example editor.commands().execute(quader::quader_command::kEditUndo), and
 * use command_id_for_modeling_action() or command_id_for_tool_activation()
 * when bridging user-facing modeling actions or tools to stable command IDs.
 * The string overloads are the command-palette and scripting representation;
 * they expect canonical IDs and must not be used to reintroduce legacy aliases.
 *
 * Shortcut mutation is part of the public API contract so UI, automation, and
 * future scripting surfaces do not parse or persist keybinding storage directly.
 */
class QuaderCommandsApi final {
public:
#ifndef NDEBUG
    QuaderCommandsApi(QuaderApi& api, QuaderProjectApi& project,
                      QuaderSceneDocumentsApi &scene_documents,
                      QuaderViewportApi& viewports, QuaderToolsApi& tools,
                      QuaderPreferencesApi& preferences,
                      QuaderDevToolsApi& dev_tools);
#else
    QuaderCommandsApi(QuaderApi& api, QuaderProjectApi& project,
                      QuaderSceneDocumentsApi &scene_documents,
                      QuaderViewportApi& viewports, QuaderToolsApi& tools,
                      QuaderPreferencesApi& preferences);
#endif
    ~QuaderCommandsApi();

    QuaderCommandsApi(const QuaderCommandsApi&) = delete;
    QuaderCommandsApi& operator=(const QuaderCommandsApi&) = delete;

    QuaderApiVoidResult execute(std::string_view command_id);
    QuaderApiVoidResult execute(QuaderCommandId command_id);
    [[nodiscard]] std::optional<QuaderApiStatus> last_status() const;
    [[nodiscard]] std::uint64_t last_status_revision() const;
    [[nodiscard]] std::optional<QuaderCommandDescriptor>
    descriptor(std::string_view command_id) const;
    [[nodiscard]] std::optional<QuaderCommandDescriptor>
    descriptor(QuaderCommandId command_id) const;
    [[nodiscard]] std::vector<QuaderCommandDescriptor> available() const;
    [[nodiscard]] std::string
    command_id_for_modeling_action(std::string_view action_id) const;
    [[nodiscard]] std::string
    command_id_for_tool_activation(std::string_view tool_id) const;
    [[nodiscard]] std::string
    keybinding_target_id_for_selection_mode(QuaderSelectionMode mode) const;
    [[nodiscard]] std::string
    keybinding_target_id_for_grid_preset(int preset) const;
    [[nodiscard]] std::string shortcut_for(std::string_view command_id) const;
    [[nodiscard]] std::string shortcut_for(QuaderCommandId command_id) const;
    /** Cheap owner stamp for shortcut presentation, including storage reloads. */
    [[nodiscard]] std::uint64_t keybindings_revision() const noexcept;
    [[nodiscard]] std::vector<QuaderKeybindingRule> keybindings() const;
    [[nodiscard]] std::vector<QuaderKeybindingCatalogEntry>
    keybinding_catalog() const;
    [[nodiscard]] std::string
    format_input_mapping(const QuaderInputChord &chord) const;
    [[nodiscard]] std::vector<QuaderKeybindingConflict>
    conflicts_for_shortcut(std::string_view shortcut,
                           std::string_view when = {}) const;
    [[nodiscard]] QuaderKeybindingAssignmentAnalysis
    analyze_shortcut_assignment(const QuaderKeybindingActionKey &action,
                                std::string_view shortcut) const;
    [[nodiscard]] bool resolve_viewport_shortcut(
        ViewportKey key, ViewportModifiers modifiers,
        const ViewportToolState &state, bool viewport_focused,
        bool text_input_active, ViewportShortcut &shortcut,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] bool resolve_viewport_shortcut(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        ViewportShortcut &shortcut,
        QuaderViewportKeybindingContext input_context = {});
    /**
     * Performs a stateless single-chord semantic probe for held/released state
     * and deferred mouse ownership. Pressed and wheel input must use
     * dispatch_viewport_input_binding() so sequence state and target
     * precedence remain authoritative.
     */
    [[nodiscard]] bool resolve_viewport_input_action(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        ViewportInputActionKind &action,
        QuaderViewportKeybindingContext input_context = {});
    [[nodiscard]] QuaderViewportKeybindingDispatch dispatch_viewport_keybinding(
        ViewportKey key, ViewportModifiers modifiers,
        const ViewportToolState &state, bool viewport_focused,
        bool text_input_active,
        QuaderViewportKeybindingContext input_context = {});
    /** Resolves one edge through the shared stateful sequence dispatcher. */
    [[nodiscard]] QuaderViewportKeybindingDispatch
    dispatch_viewport_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        QuaderViewportKeybindingContext input_context = {});
    /** Resolves Apply/Cancel for a focused active native-tool settings editor. */
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    dispatch_native_tool_settings_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state);
    QuaderApiVoidResult execute_native_tool_settings_intent(
        QuaderNativeToolSettingsIntent intent);
    void reset_pending_keybinding_chord();
    [[nodiscard]] bool has_pending_keybinding_chord() const;
    QuaderApiVoidResult set_shortcut(std::string_view command_id,
                                     std::string shortcut);
    QuaderApiVoidResult set_shortcut(QuaderCommandId command_id,
                                     std::string shortcut);
    QuaderApiVoidResult remove_shortcut(std::string_view command_id);
    QuaderApiVoidResult remove_shortcut(QuaderCommandId command_id);
    QuaderApiVoidResult reset_shortcut(std::string_view command_id);
    QuaderApiVoidResult reset_shortcut(QuaderCommandId command_id);
    QuaderApiVoidResult set_action_shortcut(
        const QuaderKeybindingActionKey &action,
        std::string shortcut,
        QuaderKeybindingConflictPolicy policy =
            QuaderKeybindingConflictPolicy::Reject);
    QuaderApiVoidResult remove_action_shortcut(
        const QuaderKeybindingActionKey &action);
    QuaderApiVoidResult reset_action_shortcut(
        const QuaderKeybindingActionKey &action,
        QuaderKeybindingConflictPolicy policy =
            QuaderKeybindingConflictPolicy::Reject);
    QuaderApiVoidResult reset_all_shortcuts();
    QuaderApiVoidResult set_keybinding_storage_path(std::filesystem::path path);

  private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace quader
