////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiSnapshot.h"
#include "assets/MeshAsset.h"

#include <optional>
#include <vector>

namespace quader {

class ProjectSession;
class EditorHistoryService;

/**
 * Exposes Quader Query API operations through the public Quader API surface.
 */
class QuaderQueryApi final {
public:
    QuaderQueryApi(ProjectSession& session, EditorHistoryService& history);

    [[nodiscard]] std::vector<QuaderEntitySnapshot> entities() const;
    [[nodiscard]] std::vector<QuaderEntitySnapshot> entityTree() const;
    [[nodiscard]] std::optional<QuaderEntitySnapshot> entity(EntityId id) const;
    [[nodiscard]] std::optional<QuaderMeshSnapshot> mesh(AssetId id) const;
    [[nodiscard]] std::vector<ModelingObjectRecord> modelingObjects() const;
    [[nodiscard]] std::optional<QuaderModelingObjectRef> modelingObjectForEntity(EntityId entity) const;
    [[nodiscard]] std::optional<QuaderModelingObjectRef> modelingObjectForId(ModelingObjectId modelingObjectId) const;
    [[nodiscard]] std::optional<MeshAsset> compiledObjectMesh(QuaderModelingObjectRef object) const;
    [[nodiscard]] std::optional<Vec3> selectionCenter() const;
    [[nodiscard]] ModelingSelectionSummary selection() const;
    [[nodiscard]] ModelingToolStatus toolStatus() const;
    [[nodiscard]] QuaderAssetSnapshot assets() const;
    [[nodiscard]] QuaderHistorySnapshot history() const;
    [[nodiscard]] ViewportOverlayFrame selectionOverlay() const;
    [[nodiscard]] QuaderRevisionStamp revisions() const;
    [[nodiscard]] QuaderApiSnapshot snapshot() const;

private:
    ProjectSession& session_;
    EditorHistoryService& history_;
};

} // namespace quader
