////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiResult.h"
#include "editor/commands/document/DocumentCommandResult.h"
#include "editor/history/SceneTransformPreviewTransaction.h"
#include "scene/Components.h"

#include <optional>
#include <string>
#include <vector>

namespace quader {

class QuaderEditorApi;
class SceneDocument;

/**
 * Stores a captured scene transform target for a viewport transform drag.
 */
struct SceneTransformDragTarget {
    EntityId entity = InvalidEntityId;
    TransformComponent transform;
    Vec3 localPivot = {};
    Mat4 worldTransform;
};

/**
 * Owns scene transform drag preview and commit policy for viewport gizmo drags.
 */
class SceneTransformDragService final {
public:
    SceneTransformDragService();
    ~SceneTransformDragService();

    SceneTransformDragService(const SceneTransformDragService&) = delete;
    SceneTransformDragService& operator=(const SceneTransformDragService&) = delete;
    SceneTransformDragService(SceneTransformDragService&&) noexcept;
    SceneTransformDragService& operator=(SceneTransformDragService&&) noexcept;

    [[nodiscard]] bool begin(
            SceneDocument& document,
            Vec3 worldPivot,
            bool startPreview,
            std::string label = "Transform Selection");
    [[nodiscard]] bool previewActive() const;
    [[nodiscard]] const std::vector<SceneTransformDragTarget>& targets() const;

    DocumentCommandResult previewTransform(EntityId entity, TransformComponent transform);
    QuaderApiVoidResult commit(QuaderEditorApi& editor);
    DocumentCommandResult cancel();
    void reset();

private:
    std::optional<SceneTransformPreviewTransaction> preview_;
    std::vector<SceneTransformDragTarget> targets_;
    std::string label_ = "Transform Selection";
};

} // namespace quader
