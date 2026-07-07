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

/**
 * Exposes Quader Query API operations through the public Quader API surface.
 */
class QuaderQueryApi final {
public:
    QuaderQueryApi(QuaderApiSession session, QuaderApiMutationRunner& mutations);

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
    [[nodiscard]] std::optional<QVec3> selection_center() const;
    [[nodiscard]] std::optional<QVec3> selection_transform_pivot() const;
    [[nodiscard]] QuaderSelectionSummary selection() const;
    [[nodiscard]] QuaderToolStatus tool_status() const;
    [[nodiscard]] QuaderAssetSnapshot assets() const;
    [[nodiscard]] QuaderHistorySnapshot history() const;
    [[nodiscard]] QuaderOverlayFrame selection_overlay() const;
    [[nodiscard]] QuaderRevisionStamp revisions() const;
    [[nodiscard]] QuaderApiSnapshot snapshot() const;

private:
    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
