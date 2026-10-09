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

#include <cstdint>

namespace quader {

class QuaderCommandsApi;

/**
 * Exposes Quader Preferences API operations through the public Quader API surface.
 */
class QuaderPreferencesApi final {
public:
    QuaderPreferencesApi();
    explicit QuaderPreferencesApi(QuaderCommandsApi& commands);

    void attach_commands(QuaderCommandsApi &commands);
    void set_runtime_hooks(QuaderPreferencesRuntimeHooks hooks);

    [[nodiscard]] std::string locale() const;
    QuaderApiVoidResult set_locale(std::string locale);
    [[nodiscard]] double interface_scale() const;
    QuaderApiVoidResult set_interface_scale(double scale);
    /** Requested display synchronization; unsupported drivers retain VSync. */
    [[nodiscard]] bool vsync_enabled() const;
    QuaderApiVoidResult set_vsync_enabled(bool enabled);
    [[nodiscard]] SessionPreferences session_settings() const;
    /// Persists and applies the group atomically; reducing history is irreversible.
    QuaderApiVoidResult set_session_settings(SessionPreferences settings);
    /// Shared appearance preference. Empty restores the packaged default.
    [[nodiscard]] std::filesystem::path default_material_texture() const;
    QuaderApiVoidResult set_default_material_texture(std::filesystem::path path);
    /** Cheap owner stamp; reads never enumerate or resolve shortcuts. */
    [[nodiscard]] std::uint64_t keybindings_revision() const noexcept;
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
    QuaderApiVoidResult set_snapping_enabled(bool enabled);
    [[nodiscard]] bool snapping_enabled() const;
    QuaderApiVoidResult set_backface_selection_enabled(bool enabled);
    [[nodiscard]] bool backface_selection_enabled() const;
    /// Global surface culling preference shared by every viewport.
    QuaderApiVoidResult set_backface_culling_enabled(bool enabled);
    [[nodiscard]] bool backface_culling_enabled() const;
    QuaderApiVoidResult set_grid_visible(bool visible);
    [[nodiscard]] bool grid_visible() const;
    QuaderApiVoidResult set_gizmos_visible(bool visible);
    [[nodiscard]] bool gizmos_visible() const;
    QuaderApiVoidResult set_mesh_grid_visible(bool visible);
    [[nodiscard]] bool mesh_grid_visible() const;
    QuaderApiVoidResult set_overlays_visible(bool visible);
    [[nodiscard]] bool overlays_visible() const;
    /// Controls selection dimension labels independently of hover measurements.
    QuaderApiVoidResult set_dimensions_visible(bool visible);
    [[nodiscard]] bool dimensions_visible() const;
    QuaderApiVoidResult set_collisions_visible(bool visible);
    [[nodiscard]] bool collisions_visible() const;
    QuaderApiVoidResult set_texture_lock_enabled(bool enabled);
    [[nodiscard]] bool texture_lock_enabled() const;
    QuaderApiVoidResult set_texture_lock_scale_enabled(bool enabled);
    [[nodiscard]] bool texture_lock_scale_enabled() const;
    QuaderApiVoidResult set_grid_snap_enabled(bool enabled);
    [[nodiscard]] bool grid_snap_enabled() const;
    QuaderApiVoidResult set_vertex_snap_enabled(bool enabled);
    [[nodiscard]] bool vertex_snap_enabled() const;
    QuaderApiVoidResult set_rotation_snap_enabled(bool enabled);
    [[nodiscard]] bool rotation_snap_enabled() const;
    QuaderApiVoidResult set_rotation_snap_degrees(double degrees);
    [[nodiscard]] double rotation_snap_degrees() const;
    QuaderApiVoidResult set_viewport_info_visible(bool visible);
    [[nodiscard]] bool viewport_info_visible() const;
    QuaderApiVoidResult save();

private:
    [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;

    QuaderPreferencesRuntimeHooks hooks_;
    QuaderCommandsApi* commands_ = nullptr;
};

} // namespace quader
