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

class QuaderQueryApi;
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
    QuaderRenderApi(QuaderApiSession session, QuaderQueryApi& query);

    [[nodiscard]] QuaderOverlayFrame selection_overlay_frame() const;
    [[nodiscard]] std::vector<QuaderToolPreviewMesh>
    tool_preview_meshes() const;

  private:
    friend class QuaderRenderApiRuntimeAccess;

    [[nodiscard]] QuaderApiVoidResult
    runtime_status(std::string message = {}) const;

    QuaderApiSession session_;
    QuaderQueryApi& query_;
};

} // namespace quader
