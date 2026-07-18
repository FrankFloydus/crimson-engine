////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "api/quader_api_types.hpp"

#include <initializer_list>
#include <memory>
#include <span>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/**
 * Stores an opaque selection baseline for one deferred viewport box-selection
 * commit.
 */
class QuaderSelectionBoxBaseline final {
public:
    QuaderSelectionBoxBaseline();
    ~QuaderSelectionBoxBaseline();
    QuaderSelectionBoxBaseline(const QuaderSelectionBoxBaseline&) noexcept = default;
    QuaderSelectionBoxBaseline& operator=(const QuaderSelectionBoxBaseline&) noexcept = default;
    QuaderSelectionBoxBaseline(QuaderSelectionBoxBaseline&&) noexcept = default;
    QuaderSelectionBoxBaseline& operator=(QuaderSelectionBoxBaseline&&) noexcept = default;

    [[nodiscard]] bool valid() const;

private:
    friend class QuaderSelectionApi;

    class Impl;
    explicit QuaderSelectionBoxBaseline(std::shared_ptr<const Impl> impl);

    std::shared_ptr<const Impl> impl_;
};

/**
 * Exposes Quader Selection API operations through the public Quader API surface.
 */
class QuaderSelectionApi final {
public:
    QuaderSelectionApi(QuaderApiSession session, QuaderApiMutationRunner& mutations);

    QuaderApiVoidResult select_objects(std::span<const EntityId> entities,
                                       EntityId active = kInvalidEntityId);
    QuaderApiVoidResult select_objects(std::initializer_list<EntityId> entities,
                                       EntityId active = kInvalidEntityId);
    QuaderApiVoidResult select_objects_for_inspection(
        std::span<const EntityId> entities,
        EntityId active = kInvalidEntityId);
    QuaderApiVoidResult select_objects_for_inspection(
        std::initializer_list<EntityId> entities,
        EntityId active = kInvalidEntityId);
    QuaderApiVoidResult
    select_object(EntityId entity,
                  QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_object(QuaderModelingObjectRef object,
                  QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_objects(std::initializer_list<QuaderModelingObjectRef> objects,
                   QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_components(const QuaderComponentSelectionArgs &args);
    QuaderApiVoidResult
    select_vertex(QuaderModelingObjectRef object, QuaderElementId vertex,
                  QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_edge(QuaderModelingObjectRef object, QuaderElementId a,
                QuaderElementId b,
                QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_face(QuaderModelingObjectRef object, QuaderElementId face,
                QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_vertices(QuaderModelingObjectRef object,
                    std::initializer_list<QuaderElementId> vertices,
                    QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult
    select_faces(QuaderModelingObjectRef object,
                 std::initializer_list<QuaderElementId> faces,
                 QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult clear();
    QuaderApiVoidResult select_all();
    QuaderApiVoidResult select_inverse();
    QuaderApiVoidResult select_edge_direction();
    QuaderApiVoidResult select_edge_loops();
    QuaderApiVoidResult select_edge_ring();
    QuaderApiVoidResult select_faces_with_same_material();
    QuaderApiVoidResult select_coplanar_faces();
    QuaderApiVoidResult select_face_normal();
    QuaderApiVoidResult select_mesh_ngons();
    QuaderApiVoidResult select_similar_meshes();
    QuaderApiVoidResult grow();
    QuaderApiVoidResult shrink();
    QuaderApiVoidResult set_mode(QuaderSelectionMode mode);

    QuaderApiResult<QuaderSelectionSummary>
    pick_select(const QuaderPickSelectRequest &request);
    [[nodiscard]] QuaderSelectionBoxBaseline
    capture_box_selection_baseline() const;
    QuaderApiResult<QuaderSelectionSummary>
    box_select(const QuaderBoxSelectRequest &request);
    QuaderApiResult<QuaderSelectionSummary>
    box_select(const QuaderBoxSelectRequest &request,
               const QuaderSelectionBoxBaseline &baseline);
    QuaderApiResult<QuaderHoverSummary> hover(const QuaderHoverRequest& request);
    QuaderApiResult<QuaderHoverSummary>
    hover_vertex(QuaderModelingObjectRef object, QuaderElementId vertex);
    QuaderApiResult<QuaderHoverSummary>
    hover_edge(QuaderModelingObjectRef object, QuaderElementId a,
               QuaderElementId b);
    QuaderApiResult<QuaderHoverSummary>
    hover_face(QuaderModelingObjectRef object, QuaderElementId face);
    QuaderApiVoidResult clear_hover();

    [[nodiscard]] QuaderSelectionSummary summary() const;
    [[nodiscard]] QuaderHoverSummary hover_summary() const;
    [[nodiscard]] QuaderHoverSummary
    preview_hover(const QuaderHoverRequest &request) const;

  private:
    [[nodiscard]] QuaderApiResult<QuaderHoverSummary>
    direct_hover(QuaderModelingObjectRef object, QuaderActiveSelectionKind kind,
                 QuaderElementId vertex, QuaderElementId edge_a,
                 QuaderElementId edge_b, QuaderElementId face);

    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
