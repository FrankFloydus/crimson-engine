////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_snapshot.hpp"
#include "api/quader_api_types.hpp"

#include <optional>
#include <string>

namespace quader {

class QuaderApi;

/**
 * Owns Quader Object Handle behavior within the public Quader API surface.
 */
class QuaderObjectHandle final {
public:
    QuaderObjectHandle(QuaderApi& api, QuaderModelingObjectRef ref);

    [[nodiscard]] QuaderModelingObjectRef ref() const;
    [[nodiscard]] bool valid() const;
    [[nodiscard]] std::optional<QuaderEntitySnapshot> entity() const;
    [[nodiscard]] std::optional<QuaderMeshAsset> compiledMesh() const;

    QuaderApiVoidResult select();
    QuaderApiVoidResult rename(std::string name);
    QuaderApiVoidResult destroy();
    QuaderApiResult<QuaderEntityRef> duplicate();
    QuaderApiVoidResult setTransform(QuaderTransform transform);
    QuaderApiVoidResult setVisible(bool visible);
    QuaderApiVoidResult setLocked(bool locked);
    QuaderApiVoidResult move(QVec3 delta);
    QuaderApiVoidResult rotateEuler(QVec3 eulerRadians, QVec3 pivot = {});
    QuaderApiVoidResult scale(QVec3 scale, QVec3 pivot = {});

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
    QuaderApiResult<QuaderHoverSummary> hoverVertex(QuaderElementId vertex);
    QuaderApiResult<QuaderHoverSummary> hoverEdge(QuaderElementId a, QuaderElementId b);
    QuaderApiResult<QuaderHoverSummary> hoverFace(QuaderElementId face);

private:
    QuaderApi* api_ = nullptr;
    QuaderModelingObjectRef ref_;
};

} // namespace quader
