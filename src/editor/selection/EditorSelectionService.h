////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "core/CommandResult.h"
#include "core/Types.h"
#include "editor/commands/document/DocumentCommandResult.h"
#include "editor/modeling/ModelingTypes.h"
#include "scene/Components.h"

#include <span>
#include <vector>

namespace quader {

class AssetRegistry;
class DocumentCommandService;
class ModelingSessionService;
class SceneDocument;

/**
 * Enumerates EditorSelectionTargetKind values used by the editor layer.
 */
enum class EditorSelectionTargetKind {
    None,
    Object,
    Group,
    Vertex,
    Edge,
    Face,
};

/**
 * Represents an Editor Selection Target value used by the editor selection authority.
 */
struct EditorSelectionTarget {
    EditorSelectionTargetKind kind = EditorSelectionTargetKind::None;
    EntityId entity = InvalidEntityId;
    int modelingObjectId = InvalidModelingObjectId;
    int vertex = -1;
    int edgeA = -1;
    int edgeB = -1;
    int face = -1;
};

/**
 * Stores the Editor Selection State data contract used by the editor selection authority.
 */
struct EditorSelectionState {
    ModelingSelectionMode mode = ModelingSelectionMode::Object;
    ModelingSelectionSummary summary;
    EditorSelectionTarget target;
    std::vector<EntityId> sceneSelection;
    EntityId activeEntity = InvalidEntityId;
};

/**
 * Coordinates Editor Selection Service responsibilities for the editor selection authority.
 */
class EditorSelectionService final {
public:
    EditorSelectionService(
            SceneDocument& scene,
            DocumentCommandService& documentCommands,
            AssetRegistry& assets,
            ModelingSessionService& modeling);

    [[nodiscard]] EditorSelectionState state() const;
    [[nodiscard]] EditorSelectionTarget target() const;
    [[nodiscard]] ModelingSelectionSummary summary() const;
    [[nodiscard]] ModelingHoverSummary hoverSummary() const;
    [[nodiscard]] ModelingSelectionMode mode() const;

    DocumentCommandResult setSceneSelection(std::span<const EntityId> entities, EntityId active = InvalidEntityId);
    ModelingCommandResult setObjectSelection(std::span<const EntityId> entities, EntityId active = InvalidEntityId);
    ModelingCommandResult setComponentSelection(const ModelingComponentSelectionRequest& request);
    ModelingCommandResult setMode(ModelingSelectionMode mode);
    ModelingCommandResult selectAll();
    ModelingCommandResult invert();
    ModelingCommandResult clear();

    [[nodiscard]] ModelingSelectionSummary pickSelect(const ModelingPickSelectRequest& request);
    ModelingCommandResult updateHover(const ModelingHoverRequest& request);
    ModelingCommandResult setHover(const ModelingDirectHoverRequest& request);
    [[nodiscard]] ModelingHoverSummary previewHover(const ModelingHoverRequest& request) const;
    ModelingCommandResult clearHover();

private:
    [[nodiscard]] EditorSelectionTarget targetFromSummary(const ModelingSelectionSummary& summary) const;
    [[nodiscard]] ModelingSelectionSummary scenePickSelection(
            EntityId targetEntity,
            ModelingSelectionMode mode,
            const ModelingPickSelectRequest& request);
    [[nodiscard]] EntityId pickMeshEntityByBounds(const ModelingRay& ray) const;
    [[nodiscard]] EntityId nearestGroupAncestor(EntityId entity) const;
    [[nodiscard]] ModelingSelectionSummary sceneSummaryFor(ModelingSelectionMode mode) const;

    SceneDocument& scene_;
    DocumentCommandService& documentCommands_;
    AssetRegistry& assets_;
    ModelingSessionService& modeling_;
};

} // namespace quader
