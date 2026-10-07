////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_types.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_editor_api_types.hpp"
#include "api/quader_operation_input_hint_types.hpp"
#include "api/quader_primitive_creation_types.hpp"
#include "api/quader_settings_presentation_types.hpp"
#include "api/quader_viewport_input_types.hpp"

#include <cstdint>
#include <array>
#include <optional>
#include <string>
#include <vector>

namespace quader {

/// Selects the geometry mode when starting a new dedicated extrusion.
enum class QuaderExtrudeMode {
  Region,
  AlongNormals,
  Individual,
};

/**
 * Describes optional history behavior for one numeric Tool Settings edit.
 *
 * Reuse one nonempty merge key only for owner updates produced by the same
 * continuous drag or text-entry gesture. Discrete edits should leave it
 * empty so each accepted change remains an independent history step. Tool
 * owners without mergeable document history ignore this option.
 */
struct QuaderToolSettingEditOptions {
  std::string merge_key;
};

/**
 * Exposes Quader Tools API operations through the public Quader API surface.
 */
class QuaderToolsApi final {
public:
    explicit QuaderToolsApi(QuaderApiSession session);

    void set_runtime_hooks(QuaderToolsRuntimeHooks hooks);

    /// A genuine tool transition accepts pending work; explicit Cancel rolls it back.
    QuaderApiVoidResult activate(std::string_view tool_id);
    QuaderApiVoidResult activate(ViewportToolId tool);
    /// Joins the single editing runtime without restarting its shared operation.
    QuaderApiVoidResult acquire_pane_tool(ViewportPaneId pane);
    /// Editing eligibility is independent of the pane hosting Tool Settings.
    [[nodiscard]] bool pane_owns_executing_tool(ViewportPaneId pane) const;
    /// Called after an accepted tool lifecycle ends, never from frame refresh.
    void synchronize_pane_tool_after_execution(ViewportPaneId pane);
    /// Starts a new extrusion with this mode while retaining the remembered amount.
    QuaderApiVoidResult activate_extrude(QuaderExtrudeMode mode);
    [[nodiscard]] bool can_activate(std::string_view tool_id) const;
    [[nodiscard]] bool can_activate(ViewportToolId tool) const;
    /** Starts the requested shape, finishing the current valid shape on a kind switch. */
    QuaderApiVoidResult
    begin_primitive_creation(QuaderPrimitiveCreationShape shape);
    /** Enters Curve creation without attaching to an already selected Curve. */
    QuaderApiVoidResult begin_new_curve_creation();
    [[nodiscard]] bool can_begin_new_curve_creation() const;
    /** Arms one-shot viewport placement for a Point or Directional light. */
    QuaderApiVoidResult begin_light_creation(QuaderLightType type);
    [[nodiscard]] QuaderToolSettingsTarget active_settings() const;
    [[nodiscard]] std::uint64_t active_settings_revision() const;
    /// Array-only presentation choice; no preview/layout evaluation. No value
    /// means ordinary selection dimensions own the viewport presentation.
    [[nodiscard]] std::optional<bool> active_array_bounding_box_dimensions() const;
    /// Active lifecycle settings stay with the origin; other panes read parameters.
    [[nodiscard]] QuaderToolSettingsTarget settings_for_pane(ViewportPaneId pane) const;
    [[nodiscard]] std::uint64_t settings_revision(ViewportPaneId pane) const;
    [[nodiscard]] std::uint64_t settings_presentation_serial(ViewportPaneId pane) const;
    [[nodiscard]] std::uint64_t selected_tool_revision(ViewportPaneId pane) const;
    /// Borrowed until the next operation start or session replacement; no descriptor work.
    [[nodiscard]] const QuaderSettingsPresentationOrigin &
    active_settings_presentation_origin() const;
    /// Keeps an existing operation's origin unless an accepted start replaces it.
    void record_active_settings_origin(ViewportPaneId pane,
                                        bool new_operation = false);
    [[nodiscard]] std::uint64_t active_tool_revision() const;
    /// Scalar tool/preview gate for command availability; no settings or overlay projection.
    [[nodiscard]] std::uint64_t availability_revision() const;
    /// Cheap gate for owner-selected operation hints, including lifecycle/rebind.
    [[nodiscard]] std::uint64_t active_input_hints_revision(const QuaderOperationInputContext &context = {}) const;
    /// Resolve only after the hint revision changes; key mappings remain command-owned.
    [[nodiscard]] std::vector<QuaderOperationInputHint> active_input_hints(const QuaderOperationInputContext &context = {}) const;
    QuaderApiVoidResult set_setting(std::string_view tool_id,
                                    std::string_view name, double value,
                                    QuaderToolSettingEditOptions options = {});
    QuaderApiVoidResult set_setting(std::string_view tool_id,
                                    std::string_view name, bool value);
    QuaderApiVoidResult set_setting(std::string_view tool_id,
                                    std::string_view name,
                                    std::string_view value);
    QuaderApiVoidResult set_setting(std::string_view tool_id,
                                    std::string_view name,
                                    const char *value);
    QuaderApiVoidResult execute_action(std::string_view tool_id,
                                       std::string_view action_id);
    QuaderApiVoidResult set_setting(ViewportPaneId pane, std::string_view tool_id,
                                    std::string_view name, double value,
                                    QuaderToolSettingEditOptions options = {});
    QuaderApiVoidResult set_setting(ViewportPaneId pane, std::string_view tool_id,
                                    std::string_view name, bool value);
    QuaderApiVoidResult set_setting(ViewportPaneId pane, std::string_view tool_id,
                                    std::string_view name, std::string_view value);
    QuaderApiVoidResult set_setting(ViewportPaneId pane, std::string_view tool_id,
                                    std::string_view name, const char *value);
    QuaderApiVoidResult execute_action(ViewportPaneId pane, std::string_view tool_id,
                                       std::string_view action_id);
    QuaderApiVoidResult reset_active_settings();
    QuaderApiVoidResult
    accept_active_primitive(std::string name,
                            AssetId default_material = kInvalidAssetId,
                            bool snap_enabled = true);
    /**
     * Accepts the pending footprint with an explicit material for this shape.
     * Missing, deleted or invalid references use the project's Default Material.
     * Scene-only sessions retain their ordinary default when no material resolves.
     */
    QuaderApiVoidResult
    accept_active_primitive(std::string name, QuaderMaterialReference material,
                            bool snap_enabled = true);
    [[nodiscard]] bool has_active_primitive_creation() const;
    /// Active shape, or the remembered application choice; initially Box.
    [[nodiscard]] QuaderPrimitiveCreationShape preferred_primitive_shape() const;
    [[nodiscard]] std::string active_primitive_type() const;
    [[nodiscard]] std::string active_primitive_default_name() const;
    [[nodiscard]] std::optional<QuaderPrimitiveCreationState>
    live_primitive_creation() const;
    QuaderApiVoidResult begin_primitive_resize(
        QuaderPrimitiveResizeControl control,
        QVec3 radial_axis = {1.0f, 0.0f, 0.0f});
    /// Signed displacement from the starting face or ellipse-ray intersection.
    QuaderApiVoidResult update_primitive_resize(double displacement);
    QuaderApiVoidResult end_primitive_resize();
    QuaderApiVoidResult cancel_primitive_resize();
    QuaderApiVoidResult toggle_primitive_caps();
    /// Toggles Sphere hemisphere creation, including the pending footprint.
    QuaderApiVoidResult toggle_primitive_hemisphere();
    /// Box-only compatibility interface.
    [[nodiscard]] std::optional<QuaderPrimitiveCubeState>
    live_primitive_cube() const;
    QuaderApiVoidResult
    begin_primitive_cube_resize(QuaderPrimitiveCubeAxis axis, bool positive);
    /// Signed local-axis displacement from the gesture's starting bounds.
    QuaderApiVoidResult update_primitive_cube_resize(double displacement);
    QuaderApiVoidResult end_primitive_cube_resize();
    QuaderApiVoidResult cancel_primitive_cube_resize();
    QuaderApiVoidResult flip_primitive_faces();
    QuaderApiVoidResult commit_active_tool_preview();
    /// Accepts one active polygon modeling tool as one editor undo/redo step.
    QuaderApiVoidResult commit_modeling_tool(ViewportToolId tool);
    QuaderApiVoidResult cancel_active_tool_preview();
    QuaderApiVoidResult cancel_modal_tool();
    [[nodiscard]] bool
    active_native_tool_lifecycle(ViewportToolId tool) const;

    [[nodiscard]] std::string active_tool() const;

    /** Rebinds this host facade after the active scene session changes. */
    void rebind_session(QuaderApiSession session);

  private:
    QuaderApiVoidResult with_align_feedback(QuaderApiVoidResult result) const;
    QuaderApiVoidResult acquire_pane_settings(ViewportPaneId pane,
                                             std::string_view tool_id);
    /** Retains the pane's selected-tool settings projection without activating it. */
    struct PaneSettingsStamp {
      std::array<std::uint64_t, 7> source = {};
      std::uint64_t revision = 0;
      bool valid = false;
    };
    QuaderApiSession session_;
    QuaderToolsRuntimeHooks hooks_;
    mutable const void *active_settings_source_identity_ = nullptr;
    mutable std::uint64_t active_settings_source_revision_ = 0;
    mutable std::uint64_t active_settings_source_dependent_revision_ = 0;
    mutable std::uint64_t active_settings_revision_ = 0;
    mutable std::uint8_t active_settings_source_kind_ = 0;
    mutable bool active_settings_source_reset_availability_token_ = false;
    mutable bool active_settings_source_valid_ = false;
    mutable std::string active_tool_id_;
    mutable std::uint64_t active_tool_revision_ = 0;
    mutable bool active_tool_valid_ = false;
    mutable std::optional<std::array<std::uint64_t, 15>> availability_source_;
    mutable std::uint64_t availability_revision_ = 0;
    mutable std::array<std::uint64_t, 21> input_hints_source_ = {};
    mutable QuaderOperationInputContext input_hints_context_;
    mutable std::string input_hints_native_tool_id_;
    mutable std::uint64_t input_hints_revision_ = 0;
    mutable bool input_hints_source_valid_ = false;
    mutable std::optional<QuaderPrimitiveCreationState> primitive_creation_state_;
    mutable std::uint64_t primitive_creation_owner_revision_ = 0;
    mutable std::uint64_t primitive_creation_transform_revision_ = 0;
    mutable std::uint64_t primitive_creation_selection_revision_ = 0;
    mutable std::uint64_t primitive_creation_instance_revision_ = 0;
    mutable std::uint64_t primitive_creation_revision_ = 0;
    mutable std::array<PaneSettingsStamp, 4> pane_settings_stamps_;
};

} // namespace quader
