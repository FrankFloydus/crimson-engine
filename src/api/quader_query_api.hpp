////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_session.hpp"
#include "api/quader_api_snapshot.hpp"

#include <optional>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/// Identifies the active document incarnation without exposing runtime owners.
struct QuaderSceneDocumentBinding final {
    SceneSessionId scene_session_id;
    std::uint64_t document_binding_generation = 0u;
    friend bool operator==(const QuaderSceneDocumentBinding &,
                           const QuaderSceneDocumentBinding &) = default;
};

/**
 * Exposes Quader Query API operations through the public Quader API surface.
 */
class QuaderQueryApi final {
public:
    QuaderQueryApi(QuaderApiSession session, QuaderApiMutationRunner& mutations);

    [[nodiscard]] QuaderSceneSettings scene_settings() const;
    [[nodiscard]] std::vector<QuaderEntitySnapshot> entities() const;
    [[nodiscard]] std::vector<QuaderEntitySnapshot> entity_tree() const;
    [[nodiscard]] std::optional<QuaderEntitySnapshot> entity(EntityId id) const;
    [[nodiscard]] std::optional<QuaderMeshSnapshot> mesh(AssetId id) const;
    [[nodiscard]] std::optional<QuaderMaterialAsset> material(AssetId id) const;
    [[nodiscard]] std::optional<QuaderTextureAsset> texture(AssetId id) const;
    [[nodiscard]] std::vector<QuaderModelingObjectRecord>
    modeling_objects() const;
    [[nodiscard]] std::optional<QuaderModelingObjectRef>
    modeling_object_for_entity(EntityId entity) const;
    [[nodiscard]] std::optional<QuaderModelingObjectRef>
    modeling_object_for_id(QuaderModelingObjectId modeling_object_id) const;
    [[nodiscard]] std::optional<QuaderMeshAsset>
    compiled_object_mesh(QuaderModelingObjectRef object) const;
    /// Returns the nearest pickable mesh depth without changing hover or selection.
    [[nodiscard]] std::optional<double>
    mesh_surface_distance(const QuaderRay &ray) const;
    /// Resolves a mesh vertex landmark and surface depth without changing state.
    [[nodiscard]] QuaderMeshSnapQueryResult
    mesh_snap_query(const QuaderRay &ray, double vertex_radius,
                    bool exclude_current_selection = true) const;
    /// Resolves only the mesh vertex candidate. Viewport callers combine this
    /// with the session-owned render-surface query for canonical occlusion.
    [[nodiscard]] std::optional<QuaderMeshVertexSnapTarget>
    mesh_vertex_snap_target(const QuaderRay &ray, double vertex_radius,
                            bool exclude_current_selection = true,
                            std::optional<double> maximum_ray_distance =
                                std::nullopt) const;
    [[nodiscard]] std::optional<QVec3> selection_center() const;
    [[nodiscard]] std::optional<QVec3> selection_transform_pivot() const;
    [[nodiscard]] std::optional<QuaderTransform3>
    selection_transform_orientation() const;
    [[nodiscard]] QuaderSelectionSummary selection() const;
    [[nodiscard]] QuaderToolStatus tool_status() const;
    [[nodiscard]] QuaderAssetSnapshot assets() const;
    [[nodiscard]] QuaderHistorySnapshot history() const;
    [[nodiscard]] QuaderOverlayFrame selection_overlay() const;
    /// Cheap identity fence for retained document controls and nested events.
    [[nodiscard]] QuaderSceneDocumentBinding document_binding() const;
    [[nodiscard]] QuaderRevisionStamp revisions() const;
    [[nodiscard]] QuaderApiSnapshot snapshot() const;

private:
    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
