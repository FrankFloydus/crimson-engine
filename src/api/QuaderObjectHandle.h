#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiSnapshot.h"
#include "api/QuaderApiTypes.h"

#include <optional>
#include <string>

namespace quader {

class QuaderApi;

class QuaderObjectHandle final {
public:
    QuaderObjectHandle(QuaderApi& api, QuaderModelingObjectRef ref);

    [[nodiscard]] QuaderModelingObjectRef ref() const;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] std::optional<QuaderEntitySnapshot> entity() const;
    [[nodiscard]] std::optional<MeshAsset> compiledMesh() const;

    QuaderApiVoidResult select();
    QuaderApiVoidResult rename(std::string name);
    QuaderApiVoidResult setTransform(TransformComponent transform);
    QuaderApiVoidResult move(Vec3 delta);
    QuaderApiVoidResult rotateEuler(Vec3 eulerRadians, Vec3 pivot = {});
    QuaderApiVoidResult scale(Vec3 scale, Vec3 pivot = {});

    QuaderApiVoidResult assignMaterial(AssetId material);
    QuaderApiVoidResult assignMaterialSlot(std::uint32_t materialSlot);
    QuaderApiVoidResult selectVertex(
            QuaderElementId vertex,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectEdge(
            QuaderElementId a,
            QuaderElementId b,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectFace(
            QuaderElementId face,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiResult<ModelingHoverSummary> hoverVertex(QuaderElementId vertex);
    QuaderApiResult<ModelingHoverSummary> hoverEdge(QuaderElementId a, QuaderElementId b);
    QuaderApiResult<ModelingHoverSummary> hoverFace(QuaderElementId face);

private:
    QuaderApi* api_ = nullptr;
    QuaderModelingObjectRef ref_;
};

} // namespace quader
