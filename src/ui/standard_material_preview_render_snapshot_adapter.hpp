////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "render/material_evaluation_snapshot.hpp"

namespace quader {

struct QuaderStandardMaterialDocumentInfo;
struct QuaderStandardMaterialPreviewInfo;

/**
 * Adapts public material-editor presentation DTOs to a renderer-neutral draft
 * preview. Preview pixels are accepted atomically only when their session,
 * material identity, and draft revision match the retained document.
 */
[[nodiscard]] RenderMaterialEvaluationSnapshot
standard_material_preview_render_snapshot(
    const QuaderStandardMaterialDocumentInfo &document,
    const QuaderStandardMaterialPreviewInfo *preview = nullptr);

} // namespace quader
