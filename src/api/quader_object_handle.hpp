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
    [[nodiscard]] std::optional<QuaderMeshAsset> compiled_mesh() const;

    QuaderApiVoidResult select();
    QuaderApiVoidResult rename(std::string name);
    QuaderApiVoidResult destroy();
    QuaderApiResult<QuaderEntityRef> duplicate();
    QuaderApiVoidResult set_transform(QuaderTransform transform);
    QuaderApiVoidResult set_visible(bool visible);
    QuaderApiVoidResult set_locked(bool locked);
    QuaderApiVoidResult move(QVec3 delta);
    QuaderApiVoidResult rotate_euler(QVec3 euler_radians, QVec3 pivot = {});
    QuaderApiVoidResult scale(QVec3 scale, QVec3 pivot = {});

    QuaderApiVoidResult assign_material(AssetId material);
    QuaderApiVoidResult assign_material_slot(std::uint32_t material_slot);
    QuaderApiVoidResult
    select_vertex(QuaderElementId vertex,
                  QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_edge(QuaderElementId a, QuaderElementId b,
                QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_face(QuaderElementId face,
                QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiResult<QuaderHoverSummary> hover_vertex(QuaderElementId vertex);
    QuaderApiResult<QuaderHoverSummary> hover_edge(QuaderElementId a,
                                                   QuaderElementId b);
    QuaderApiResult<QuaderHoverSummary> hover_face(QuaderElementId face);

  private:
    [[nodiscard]] bool bound_to_active_scene() const;
    [[nodiscard]] QuaderApiVoidResult unavailable_status() const;

    QuaderApi* api_ = nullptr;
    QuaderModelingObjectRef ref_;
    SceneSessionId scene_session_id_;
};

} // namespace quader
