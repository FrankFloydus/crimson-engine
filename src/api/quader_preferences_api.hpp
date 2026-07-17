////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api_types.hpp"

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
    QuaderApiVoidResult set_shortcut(std::string_view command_id,
                                     std::string shortcut);
    QuaderApiVoidResult set_shortcut(QuaderCommandId command_id,
                                     std::string shortcut);
    QuaderApiVoidResult remove_shortcut(std::string_view command_id);
    QuaderApiVoidResult remove_shortcut(QuaderCommandId command_id);
    QuaderApiVoidResult reset_shortcut(std::string_view command_id);
    QuaderApiVoidResult reset_shortcut(QuaderCommandId command_id);
    QuaderApiVoidResult set_snapping_enabled(bool enabled);
    [[nodiscard]] bool snapping_enabled() const;
    QuaderApiVoidResult set_grid_visible(bool visible);
    [[nodiscard]] bool grid_visible() const;
    QuaderApiVoidResult set_gizmos_visible(bool visible);
    [[nodiscard]] bool gizmos_visible() const;
    QuaderApiVoidResult set_mesh_grid_visible(bool visible);
    [[nodiscard]] bool mesh_grid_visible() const;
    QuaderApiVoidResult set_overlays_visible(bool visible);
    [[nodiscard]] bool overlays_visible() const;
    QuaderApiVoidResult set_grid_snap_enabled(bool enabled);
    [[nodiscard]] bool grid_snap_enabled() const;
    QuaderApiVoidResult set_vertex_snap_enabled(bool enabled);
    [[nodiscard]] bool vertex_snap_enabled() const;
    QuaderApiVoidResult set_rotation_snap_enabled(bool enabled);
    [[nodiscard]] bool rotation_snap_enabled() const;
    QuaderApiVoidResult set_rotation_snap_degrees(double degrees);
    [[nodiscard]] double rotation_snap_degrees() const;
    QuaderApiVoidResult set_navigation_gizmo_visible(bool visible);
    [[nodiscard]] bool navigation_gizmo_visible() const;
    QuaderApiVoidResult save();

private:
    [[nodiscard]] QuaderApiVoidResult unavailable(std::string message) const;

    QuaderPreferencesRuntimeHooks hooks_;
    QuaderCommandsApi* commands_ = nullptr;
};

} // namespace quader
