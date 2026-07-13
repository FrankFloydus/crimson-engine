////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "editor/commands/document/document_command_result.hpp"
#include "editor/history/scene_transform_preview_transaction.hpp"
#include "scene/components.hpp"

#include <optional>
#include <string>
#include <vector>

namespace quader {

class EditorHistoryService;
class DocumentCommandService;
class QuaderEditorApi;
class SceneDocument;
class AssetRegistry;

/**
 * Stores a captured scene transform target for a viewport transform drag.
 */
struct SceneTransformDragTarget {
  EntityId entity = kInvalidEntityId;
  TransformComponent transform;
  TransformComponent translation_preview_transform;
  QVec3 local_pivot = {};
  QMat4 world_transform;
  QVec3 parent_space_world_x = {1.0f, 0.0f, 0.0f};
  QVec3 parent_space_world_y = {0.0f, 1.0f, 0.0f};
  QVec3 parent_space_world_z = {0.0f, 0.0f, 1.0f};
  bool parent_translation_basis_valid = true;
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

    [[nodiscard]] bool begin(SceneDocument &document, QVec3 world_pivot,
                             bool start_preview,
                             std::string label = "Transform Selection");
    [[nodiscard]] bool begin_move_selected_to_primary_position_preview(
        SceneDocument &document, EntityId primary,
        std::string label = "Transform Selection");
    [[nodiscard]] bool begin_rotate_selected_to_primary_euler_preview(
        SceneDocument &document, const AssetRegistry &assets, EntityId primary,
        std::string label = "Transform Selection");
    [[nodiscard]] bool begin_scale_selected_to_primary_scale_preview(
        SceneDocument &document, const AssetRegistry &assets, EntityId primary,
        std::string label = "Transform Selection");
    [[nodiscard]] bool preview_active() const;
    [[nodiscard]] const std::vector<SceneTransformDragTarget>& targets() const;

    DocumentCommandResult preview_transform(EntityId entity,
                                            TransformComponent transform);
    DocumentCommandResult preview_selected_to_primary_position(
        SceneDocument &document, EntityId primary, QVec3 next_primary_position);
    DocumentCommandResult preview_selected_to_primary_euler(
        SceneDocument &document, EntityId primary,
        QVec3 next_primary_euler_radians);
    DocumentCommandResult preview_selected_to_primary_scale(
        SceneDocument &document, EntityId primary, QVec3 next_primary_scale);
    DocumentCommandResult
    preview_world_transform_delta(SceneDocument &document,
                                  const QMat4 &world_transform_delta);
    DocumentCommandResult
    preview_world_translation_delta(SceneDocument &document,
                                    QVec3 world_translation_delta);
    DocumentCommandResult accept();
    DocumentCommandResult commit(EditorHistoryService& history);
    QuaderApiVoidResult commit(QuaderEditorApi& editor);
    DocumentCommandResult cancel();
    void reset();

    QuaderApiVoidResult move_selected_to_primary_position(
        QuaderEditorApi &editor, SceneDocument &document, EntityId primary,
        QVec3 next_primary_position, std::string label = "Transform Selection");
    QuaderApiVoidResult rotate_selected_to_primary_euler_around_selection_pivot(
        QuaderEditorApi &editor, SceneDocument &document,
        const AssetRegistry &assets, EntityId primary,
        QVec3 next_primary_euler_radians,
        std::string label = "Transform Selection");
    QuaderApiVoidResult scale_selected_to_primary_scale_around_selection_pivot(
        QuaderEditorApi &editor, SceneDocument &document,
        const AssetRegistry &assets, EntityId primary, QVec3 next_primary_scale,
        std::string label = "Transform Selection");
    DocumentCommandResult
    move_selected_by_world_delta(DocumentCommandService &document_commands,
                                 SceneDocument &document, QVec3 world_delta,
                                 std::string label = "Move Selection");
    DocumentCommandResult transform_selected_by_world_delta(
        DocumentCommandService &document_commands, SceneDocument &document,
        const QMat4 &world_transform_delta,
        std::string label = "Transform Selection");

  private:
    /// Distinguishes an empty selection from an unsafe transform capture.
    enum class CaptureTargetsStatus {
      Empty,
      Captured,
      Invalid,
    };

    [[nodiscard]] CaptureTargetsStatus
    capture_targets(SceneDocument &document, QVec3 world_pivot);
    [[nodiscard]] const TransformComponent *
    captured_primary_transform(const SceneDocument &document,
                               EntityId primary) const;

    std::optional<SceneTransformPreviewTransaction> preview_;
    std::vector<SceneTransformDragTarget> targets_;
    std::string label_ = "Transform Selection";
    QVec3 world_pivot_ = {};
};

} // namespace quader
