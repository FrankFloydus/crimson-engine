////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiSession.h"
#include "api/QuaderApiTypes.h"

#include <initializer_list>
#include <span>
#include <vector>

namespace quader {

class QuaderApiMutationRunner;

/**
 * Exposes Quader Selection API operations through the public Quader API surface.
 */
class QuaderSelectionApi final {
public:
    QuaderSelectionApi(QuaderApiSession session, QuaderApiMutationRunner& mutations);

    QuaderApiVoidResult selectObjects(std::span<const EntityId> entities, EntityId active = InvalidEntityId);
    QuaderApiVoidResult selectObjects(std::initializer_list<EntityId> entities, EntityId active = InvalidEntityId);
    QuaderApiVoidResult selectObject(
            EntityId entity,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectObject(
            QuaderModelingObjectRef object,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectObjects(
            std::initializer_list<QuaderModelingObjectRef> objects,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectComponents(const QuaderComponentSelectionArgs& args);
    QuaderApiVoidResult selectVertex(
            QuaderModelingObjectRef object,
            QuaderElementId vertex,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectEdge(
            QuaderModelingObjectRef object,
            QuaderElementId a,
            QuaderElementId b,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectFace(
            QuaderModelingObjectRef object,
            QuaderElementId face,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectVertices(
            QuaderModelingObjectRef object,
            std::initializer_list<QuaderElementId> vertices,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult selectFaces(
            QuaderModelingObjectRef object,
            std::initializer_list<QuaderElementId> faces,
            QuaderSelectionEdit edit = QuaderSelectionEdit::Replace);
    QuaderApiVoidResult clear();
    QuaderApiVoidResult selectAll();
    QuaderApiVoidResult invert();
    QuaderApiVoidResult setMode(QuaderSelectionMode mode);

    QuaderApiResult<QuaderSelectionSummary> pickSelect(const QuaderPickSelectRequest& request);
    QuaderApiResult<QuaderHoverSummary> hover(const QuaderHoverRequest& request);
    QuaderApiResult<QuaderHoverSummary> hoverVertex(QuaderModelingObjectRef object, QuaderElementId vertex);
    QuaderApiResult<QuaderHoverSummary> hoverEdge(QuaderModelingObjectRef object, QuaderElementId a, QuaderElementId b);
    QuaderApiResult<QuaderHoverSummary> hoverFace(QuaderModelingObjectRef object, QuaderElementId face);
    QuaderApiVoidResult clearHover();

    [[nodiscard]] QuaderSelectionSummary summary() const;
    [[nodiscard]] QuaderHoverSummary hoverSummary() const;
    [[nodiscard]] QuaderHoverSummary previewHover(const QuaderHoverRequest& request) const;

private:
    [[nodiscard]] QuaderApiResult<QuaderHoverSummary> directHover(
            QuaderModelingObjectRef object,
            QuaderActiveSelectionKind kind,
            QuaderElementId vertex,
            QuaderElementId edgeA,
            QuaderElementId edgeB,
            QuaderElementId face);

    QuaderApiSession session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
