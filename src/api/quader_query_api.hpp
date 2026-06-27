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

/**
 * Exposes Quader Query API operations through the public Quader API surface.
 */
class QuaderQueryApi final {
public:
    explicit QuaderQueryApi(QuaderApiSession session);

    [[nodiscard]] std::vector<QuaderEntitySnapshot> entities() const;
    [[nodiscard]] std::vector<QuaderEntitySnapshot> entityTree() const;
    [[nodiscard]] std::optional<QuaderEntitySnapshot> entity(EntityId id) const;
    [[nodiscard]] std::optional<QuaderMeshSnapshot> mesh(AssetId id) const;
    [[nodiscard]] std::vector<QuaderModelingObjectRecord> modelingObjects() const;
    [[nodiscard]] std::optional<QuaderModelingObjectRef> modelingObjectForEntity(EntityId entity) const;
    [[nodiscard]] std::optional<QuaderModelingObjectRef> modelingObjectForId(
            QuaderModelingObjectId modelingObjectId) const;
    [[nodiscard]] std::optional<QuaderMeshAsset> compiledObjectMesh(QuaderModelingObjectRef object) const;
    [[nodiscard]] std::optional<QVec3> selectionCenter() const;
    [[nodiscard]] QuaderSelectionSummary selection() const;
    [[nodiscard]] QuaderToolStatus toolStatus() const;
    [[nodiscard]] QuaderAssetSnapshot assets() const;
    [[nodiscard]] QuaderHistorySnapshot history() const;
    [[nodiscard]] QuaderOverlayFrame selectionOverlay() const;
    [[nodiscard]] QuaderRevisionStamp revisions() const;
    [[nodiscard]] QuaderApiSnapshot snapshot() const;

private:
    QuaderApiSession session_;
};

} // namespace quader
