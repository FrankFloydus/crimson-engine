#pragma once

#include "api/QuaderApiSnapshot.h"
#include "assets/MeshAsset.h"

#include <optional>
#include <vector>

namespace quader {

class ProjectSession;

class QuaderQueryApi final {
public:
    explicit QuaderQueryApi(ProjectSession& session);

    [[nodiscard]] std::vector<QuaderEntitySnapshot> entities() const;
    [[nodiscard]] std::vector<QuaderEntitySnapshot> entityTree() const;
    [[nodiscard]] std::optional<QuaderEntitySnapshot> entity(EntityId id) const;
    [[nodiscard]] std::optional<QuaderMeshSnapshot> mesh(AssetId id) const;
    [[nodiscard]] std::vector<ModelingObjectRecord> modelingObjects() const;
    [[nodiscard]] std::optional<QuaderModelingObjectRef> modelingObjectForEntity(EntityId entity) const;
    [[nodiscard]] std::optional<QuaderModelingObjectRef> modelingObjectForSdkId(ModelingObjectId sdkObjectId) const;
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
};

} // namespace quader
