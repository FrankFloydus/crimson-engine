////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_session.hpp"
#include "api/quader_editor_api_types.hpp"
#include "api/quader_viewport_input_types.hpp"

#include <cstdint>

namespace quader {

/**
 * Exposes Quader Tools API operations through the public Quader API surface.
 */
class QuaderToolsApi final {
public:
    explicit QuaderToolsApi(QuaderApiSession session);

    void set_runtime_hooks(QuaderToolsRuntimeHooks hooks);

    QuaderApiVoidResult activate(std::string_view tool_id);
    QuaderApiVoidResult activate(ViewportToolId tool);
    [[nodiscard]] bool can_activate(std::string_view tool_id) const;
    [[nodiscard]] bool can_activate(ViewportToolId tool) const;
    [[nodiscard]] QuaderToolSettingsTarget active_settings() const;
    [[nodiscard]] std::uint64_t active_settings_revision() const;
    [[nodiscard]] std::uint64_t active_tool_revision() const;
    QuaderApiVoidResult set_setting(std::string_view tool_id,
                                    std::string_view name, double value);
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
    QuaderApiVoidResult reset_active_settings();
    QuaderApiVoidResult
    accept_active_primitive(std::string name,
                            AssetId default_material = kInvalidAssetId,
                            bool snap_enabled = true);
    [[nodiscard]] bool has_active_primitive_creation() const;
    [[nodiscard]] std::string active_primitive_type() const;
    [[nodiscard]] std::string active_primitive_default_name() const;
    QuaderApiVoidResult commit_active_tool_preview();
    QuaderApiVoidResult cancel_active_tool_preview();
    QuaderApiVoidResult cancel_modal_tool();

    [[nodiscard]] std::string active_tool() const;

    /** Rebinds this host facade after the active scene session changes. */
    void rebind_session(QuaderApiSession session);

  private:
    QuaderApiSession session_;
    QuaderToolsRuntimeHooks hooks_;
    mutable const void *active_settings_source_identity_ = nullptr;
    mutable std::uint64_t active_settings_source_revision_ = 0;
    mutable std::uint64_t active_settings_revision_ = 0;
    mutable std::uint8_t active_settings_source_kind_ = 0;
    mutable bool active_settings_source_reset_availability_token_ = false;
    mutable bool active_settings_source_valid_ = false;
    mutable std::string active_tool_id_;
    mutable std::uint64_t active_tool_revision_ = 0;
    mutable bool active_tool_valid_ = false;
};

} // namespace quader
