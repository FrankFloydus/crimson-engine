////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_api_types.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace quader {

class QuaderApi;
class QuaderRenderApiRuntimeAccess;

/**
 * Stores the result of submitting editor-owned viewport overlay payloads to the render bridge.
 */
struct QuaderRenderOverlaySubmission {
    bool visible = false;
};

/**
 * Exposes Quader Render API operations through the public Quader API surface.
 */
class QuaderRenderApi final {
public:
    QuaderRenderApi(QuaderApiSession session, QuaderApi& api);

    /** Rebinds this host facade after the active scene session changes. */
    void rebind_session(QuaderApiSession session,
                        std::uint64_t scene_session_id = 1u,
                        std::uint64_t activation_generation = 1u);

    [[nodiscard]] QuaderOverlayFrame selection_overlay_frame() const;
    [[nodiscard]] std::vector<QuaderToolPreviewMesh>
    tool_preview_meshes() const;

  private:
    friend class QuaderRenderApiRuntimeAccess;

    [[nodiscard]] QuaderApiVoidResult
    runtime_status(std::string message = {}) const;

    QuaderApiSession session_;
    QuaderApi* api_ = nullptr;
    const void* native_array_preview_sync_bridge_ = nullptr;
    std::uint64_t native_array_preview_bridge_submission_revision_ = 0;
    std::uint64_t native_array_preview_revision_ = 0;
    std::uint64_t native_array_preview_asset_revision_ = 0;
    std::uint64_t native_array_preview_local_visibility_revision_ = 0;
    std::uint8_t native_array_preview_kind_ = 0;
    const void *transform_preview_sync_bridge_ = nullptr;
    std::uint64_t transform_preview_bridge_submission_revision_ = 0;
    std::uint64_t transform_preview_revision_ = 0;
    std::uint64_t transform_preview_asset_revision_ = 0;
    std::uint64_t transform_preview_local_visibility_revision_ = 0;
    const void *generated_tool_preview_sync_bridge_ = nullptr;
    std::uint64_t generated_tool_preview_bridge_submission_revision_ = 0;
    std::uint64_t generated_tool_preview_signature_ = 0;
    std::uint64_t generated_tool_preview_asset_revision_ = 0;
    std::uint64_t generated_tool_preview_local_visibility_revision_ = 0;
    std::uint64_t scene_session_id_ = 1u;
    std::uint64_t activation_generation_ = 1u;
    std::uint64_t scene_binding_revision_ = 1;
};

} // namespace quader
