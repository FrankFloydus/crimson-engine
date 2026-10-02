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
class QuaderUIApi;
class QuaderSelectionDiagnosticsApi;
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

/** Identifies Apply/Cancel intent resolved for a settings or dialog draft. */
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
  /** Preserves unmatched, pending, consumed-prefix and resolved lifecycle input. */
  enum class Kind {
    NoMatch,
    PendingChord,
    Consumed,
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
  bool outliner_focused = false;
  /** Ordinary main-editor panel input, including an embedded material browser. */
  bool editor_focused = false;
  /** Asset item browsing owns clipboard commands separately from the scene. */
  bool asset_browser_focused = false;
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
                      QuaderPreferencesApi& preferences, QuaderUIApi &ui,
                      QuaderSelectionDiagnosticsApi &selection_diagnostics,
                      QuaderDevToolsApi& dev_tools);
#else
    QuaderCommandsApi(QuaderApi& api, QuaderProjectApi& project,
                      QuaderSceneDocumentsApi &scene_documents,
                      QuaderViewportApi& viewports, QuaderToolsApi& tools,
                      QuaderPreferencesApi& preferences, QuaderUIApi &ui,
                      QuaderSelectionDiagnosticsApi &selection_diagnostics);
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
    /** Probes the asset browser's canonical shortcuts before Qt activation. */
    [[nodiscard]] QuaderViewportKeybindingDispatch
    resolve_asset_browser_input_binding(const QuaderInputChord &chord,
                                        bool text_input_active) const;
    /**
     * Probes browser toggling across editor owners without advancing a sequence.
     * Other eligible targets retain their canonical precedence.
     * Known browser prefixes may reserve mismatches only within their owner.
     */
    [[nodiscard]] QuaderViewportKeybindingDispatch
    resolve_asset_browser_toggle_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool viewport_focused, bool text_input_active,
        QuaderViewportKeybindingContext input_context = {},
        bool continue_pending = true,
        bool reserve_pending_mismatch = false) const;
    /**
     * Probes one edge without consuming sequence state and returns an enabled
     * global command when it is an exact match. Native hosts use this to run
     * global commands synchronously before a viewport queues the same edge.
     */
    [[nodiscard]] std::optional<std::string>
    resolve_immediate_global_command(
        const QuaderInputChord &chord, const ViewportToolState &state,
        bool text_input_active,
        QuaderViewportKeybindingContext input_context = {}) const;
    /** Resolves Apply/Cancel for an active tool or operation settings editor. */
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    dispatch_native_tool_settings_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state);
    /// Probes settings lifecycle shortcuts without consuming their sequence.
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    resolve_native_tool_settings_input_binding(
        const QuaderInputChord &chord, const ViewportToolState &state) const;
    /// Dialog-owned intents applied by the caller to its own draft.
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    resolve_modal_lifecycle_input_binding(const QuaderInputChord &chord) const;
    [[nodiscard]] QuaderNativeToolSettingsDispatch
    dispatch_modal_lifecycle_input_binding(const QuaderInputChord &chord);
    void reset_pending_modal_lifecycle_input_binding();
    QuaderApiVoidResult execute_native_tool_settings_intent(
        QuaderNativeToolSettingsIntent intent);
    void reset_pending_keybinding_chord();
    /// Clears editor prefixes while preserving a dialog's input ownership.
    void reset_pending_non_modal_keybinding_chord();
    /** Clears viewport prefixes without canceling another editor panel's sequence. */
    void reset_pending_viewport_keybinding_chord();
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
