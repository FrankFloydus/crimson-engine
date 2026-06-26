////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderSelectionApi.h"
#include "api/QuaderApiSessionInternalAccess.h"

#include "api/QuaderApiTypeConversions.h"
#include "api/QuaderApiMutationRunner.h"
#include "scene/document/ProjectSession.h"
#include "editor/selection/EditorSelectionService.h"

#include <memory>
#include <utility>

namespace quader {
namespace {

ProjectSession& projectSession(QuaderApiSession session) {
    return QuaderApiSessionInternalAccess::projectSession(session);
}


QuaderApiResult<QuaderSelectionSummary> selectionResultFromStatus(
        QuaderApiVoidResult status,
        ModelingSelectionSummary value) {
    return quaderApiResultFromStatus(status, toApiSelectionSummary(value));
}

QuaderApiResult<QuaderHoverSummary> hoverResultFromStatus(
        QuaderApiVoidResult status,
        ModelingHoverSummary value) {
    return quaderApiResultFromStatus(status, toApiHoverSummary(value));
}

EditorSelectionEdit toEditorSelectionEdit(QuaderSelectionEdit edit) {
    switch (edit) {
    case QuaderSelectionEdit::Add:
        return EditorSelectionEdit::Add;
    case QuaderSelectionEdit::Remove:
        return EditorSelectionEdit::Remove;
    case QuaderSelectionEdit::Toggle:
        return EditorSelectionEdit::Toggle;
    case QuaderSelectionEdit::Replace:
        return EditorSelectionEdit::Replace;
    }
    return EditorSelectionEdit::Replace;
}

ModelingSelectionEdit toModelingEdit(QuaderSelectionEdit edit) {
    switch (edit) {
    case QuaderSelectionEdit::Add:
        return ModelingSelectionEdit::Add;
    case QuaderSelectionEdit::Remove:
        return ModelingSelectionEdit::Remove;
    case QuaderSelectionEdit::Toggle:
        return ModelingSelectionEdit::Toggle;
    case QuaderSelectionEdit::Replace:
        return ModelingSelectionEdit::Replace;
    }
    return ModelingSelectionEdit::Replace;
}

} // namespace

/**
 * Stores QuaderSelectionBoxBaseline implementation data outside the public API header.
 */
class QuaderSelectionBoxBaseline::Impl final {
public:
    explicit Impl(EditorSelectionBoxBaseline baseline)
        : baseline(std::move(baseline)) {}

    EditorSelectionBoxBaseline baseline;
};

QuaderSelectionBoxBaseline::QuaderSelectionBoxBaseline() = default;
QuaderSelectionBoxBaseline::~QuaderSelectionBoxBaseline() = default;

QuaderSelectionBoxBaseline::QuaderSelectionBoxBaseline(std::shared_ptr<const Impl> impl)
    : impl_(std::move(impl)) {}

bool QuaderSelectionBoxBaseline::valid() const {
    return impl_ != nullptr;
}

QuaderSelectionApi::QuaderSelectionApi(QuaderApiSession session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiVoidResult QuaderSelectionApi::selectObjects(std::span<const EntityId> entities, EntityId active) {
    return mutations_.runDocumentResult(
            projectSession(session_).selection().setSceneSelection(entities, active),
            "Select Objects");
}

QuaderApiVoidResult QuaderSelectionApi::selectObjects(std::initializer_list<EntityId> entities, EntityId active) {
    return selectObjects(std::span<const EntityId>(entities.begin(), entities.size()), active);
}

QuaderApiVoidResult QuaderSelectionApi::selectObject(EntityId entity, QuaderSelectionEdit edit) {
    return mutations_.runDocumentResult(
            projectSession(session_).selection().editSceneSelection(entity, toEditorSelectionEdit(edit)),
            "Select Objects");
}

QuaderApiVoidResult QuaderSelectionApi::selectObject(QuaderModelingObjectRef object, QuaderSelectionEdit edit) {
    return selectObject(object.entity, edit);
}

QuaderApiVoidResult QuaderSelectionApi::selectObjects(
        std::initializer_list<QuaderModelingObjectRef> objects,
        QuaderSelectionEdit edit) {
    std::vector<EntityId> entities;
    entities.reserve(objects.size());
    for (const QuaderModelingObjectRef& object : objects) {
        entities.push_back(object.entity);
    }
    return mutations_.runDocumentResult(
            projectSession(session_).selection().editSceneSelection(entities, toEditorSelectionEdit(edit)),
            "Select Objects");
}

QuaderApiVoidResult QuaderSelectionApi::selectComponents(const QuaderComponentSelectionArgs& args) {
    ModelingComponentSelectionRequest request;
    request.modelingObjectId = args.object.modelingObjectId;
    request.edit = toModelingEdit(args.edit);
    switch (args.kind) {
    case QuaderComponentKind::Vertex:
        request.mode = ModelingSelectionMode::Vertex;
        request.vertices.reserve(args.vertices.size());
        for (const QuaderElementId vertex : args.vertices) {
            request.vertices.push_back(vertex);
        }
        if (args.active.kind == QuaderComponentKind::Vertex) {
            request.activeKind = ModelingActiveSelectionKind::Vertex;
            request.activeVertex = args.active.vertex;
        }
        break;
    case QuaderComponentKind::Edge:
        request.mode = ModelingSelectionMode::Edge;
        request.edges.reserve(args.edges.size());
        for (const auto& edge : args.edges) {
            request.edges.emplace_back(edge.first, edge.second);
        }
        if (args.active.kind == QuaderComponentKind::Edge) {
            request.activeKind = ModelingActiveSelectionKind::Edge;
            request.activeEdge = {args.active.edgeA, args.active.edgeB};
        }
        break;
    case QuaderComponentKind::Face:
        request.mode = ModelingSelectionMode::Face;
        request.faces.reserve(args.faces.size());
        for (const QuaderElementId face : args.faces) {
            request.faces.push_back(face);
        }
        if (args.active.kind == QuaderComponentKind::Face) {
            request.activeKind = ModelingActiveSelectionKind::Face;
            request.activeFace = args.active.face;
        }
        break;
    case QuaderComponentKind::Object:
        return selectObject(args.object, args.edit);
    }

    return mutations_.runModelingResult(
            projectSession(session_).selection().setComponentSelection(request),
            "Select Components");
}

QuaderApiVoidResult QuaderSelectionApi::selectVertex(
        QuaderModelingObjectRef object,
        QuaderElementId vertex,
        QuaderSelectionEdit edit) {
    return selectComponents(QuaderComponentSelectionArgs{
            .object = object,
            .kind = QuaderComponentKind::Vertex,
            .vertices = {vertex},
            .active = QuaderComponentRef{.object = object, .kind = QuaderComponentKind::Vertex, .vertex = vertex},
            .edit = edit,
    });
}

QuaderApiVoidResult QuaderSelectionApi::selectEdge(
        QuaderModelingObjectRef object,
        QuaderElementId a,
        QuaderElementId b,
        QuaderSelectionEdit edit) {
    return selectComponents(QuaderComponentSelectionArgs{
            .object = object,
            .kind = QuaderComponentKind::Edge,
            .edges = {{a, b}},
            .active = QuaderComponentRef{.object = object, .kind = QuaderComponentKind::Edge, .edgeA = a, .edgeB = b},
            .edit = edit,
    });
}

QuaderApiVoidResult QuaderSelectionApi::selectFace(
        QuaderModelingObjectRef object,
        QuaderElementId face,
        QuaderSelectionEdit edit) {
    return selectComponents(QuaderComponentSelectionArgs{
            .object = object,
            .kind = QuaderComponentKind::Face,
            .faces = {face},
            .active = QuaderComponentRef{.object = object, .kind = QuaderComponentKind::Face, .face = face},
            .edit = edit,
    });
}

QuaderApiVoidResult QuaderSelectionApi::selectVertices(
        QuaderModelingObjectRef object,
        std::initializer_list<QuaderElementId> vertices,
        QuaderSelectionEdit edit) {
    return selectComponents(QuaderComponentSelectionArgs{
            .object = object,
            .kind = QuaderComponentKind::Vertex,
            .vertices = std::vector<QuaderElementId>(vertices.begin(), vertices.end()),
            .active = vertices.size() == 0u ? QuaderComponentRef{} : QuaderComponentRef{
                    .object = object,
                    .kind = QuaderComponentKind::Vertex,
                    .vertex = *(vertices.end() - 1),
            },
            .edit = edit,
    });
}

QuaderApiVoidResult QuaderSelectionApi::selectFaces(
        QuaderModelingObjectRef object,
        std::initializer_list<QuaderElementId> faces,
        QuaderSelectionEdit edit) {
    return selectComponents(QuaderComponentSelectionArgs{
            .object = object,
            .kind = QuaderComponentKind::Face,
            .faces = std::vector<QuaderElementId>(faces.begin(), faces.end()),
            .active = faces.size() == 0u ? QuaderComponentRef{} : QuaderComponentRef{
                    .object = object,
                    .kind = QuaderComponentKind::Face,
                    .face = *(faces.end() - 1),
            },
            .edit = edit,
    });
}

QuaderApiVoidResult QuaderSelectionApi::clear() {
    return mutations_.runModelingResult(projectSession(session_).selection().clear(), "Clear Selection");
}

QuaderApiVoidResult QuaderSelectionApi::selectAll() {
    return mutations_.runModelingResult(projectSession(session_).selection().selectAll(), "Select All");
}

QuaderApiVoidResult QuaderSelectionApi::invert() {
    return mutations_.runModelingResult(projectSession(session_).selection().invert(), "Invert Selection");
}

QuaderApiVoidResult QuaderSelectionApi::setMode(QuaderSelectionMode mode) {
    return mutations_.runModelingResult(
            projectSession(session_).selection().setMode(toModelingSelectionMode(mode)),
            "Set Selection Mode");
}

QuaderApiResult<QuaderSelectionSummary> QuaderSelectionApi::pickSelect(const QuaderPickSelectRequest& request) {
    const ModelingSelectionSummary before = projectSession(session_).selection().summary();
    const ModelingSelectionSummary picked = projectSession(session_).selection().pickSelect(toModelingPickSelectRequest(request));
    const ModelingSelectionSummary after = projectSession(session_).selection().summary();
    const bool changed = before.contentRevision != after.contentRevision ||
            before.selectionRevision != after.selectionRevision;
    QuaderApiVoidResult status = mutations_.runModelingResult(
            ModelingCommandResult{.ok = true, .changed = changed},
            "Pick Select");
    return selectionResultFromStatus(std::move(status), picked);
}

QuaderSelectionBoxBaseline QuaderSelectionApi::captureBoxSelectionBaseline() const {
    return QuaderSelectionBoxBaseline(std::shared_ptr<const QuaderSelectionBoxBaseline::Impl>(
            new QuaderSelectionBoxBaseline::Impl(
                    projectSession(session_).selection().captureBoxSelectionBaseline())));
}

QuaderApiResult<QuaderSelectionSummary> QuaderSelectionApi::boxSelect(const QuaderBoxSelectRequest& request) {
    const ModelingSelectionSummary before = projectSession(session_).selection().summary();
    const ModelingSelectionSummary selected = projectSession(session_).selection().boxSelect(toModelingBoxSelectRequest(request));
    const ModelingSelectionSummary after = projectSession(session_).selection().summary();
    const bool changed = before.contentRevision != after.contentRevision ||
            before.selectionRevision != after.selectionRevision;
    QuaderApiVoidResult status = mutations_.runModelingResult(
            ModelingCommandResult{.ok = true, .changed = changed},
            "Box Select");
    return selectionResultFromStatus(std::move(status), selected);
}

QuaderApiResult<QuaderSelectionSummary> QuaderSelectionApi::boxSelect(
        const QuaderBoxSelectRequest& request,
        const QuaderSelectionBoxBaseline& baseline) {
    const ModelingSelectionSummary before = projectSession(session_).selection().summary();
    const ModelingSelectionSummary selected = baseline.impl_ != nullptr
            ? projectSession(session_).selection().boxSelect(toModelingBoxSelectRequest(request), baseline.impl_->baseline)
            : projectSession(session_).selection().boxSelect(toModelingBoxSelectRequest(request));
    const ModelingSelectionSummary after = projectSession(session_).selection().summary();
    const bool changed = before.contentRevision != after.contentRevision ||
            before.selectionRevision != after.selectionRevision;
    QuaderApiVoidResult status = mutations_.runModelingResult(
            ModelingCommandResult{.ok = true, .changed = changed},
            "Box Select");
    return selectionResultFromStatus(std::move(status), selected);
}

QuaderApiResult<QuaderHoverSummary> QuaderSelectionApi::hover(const QuaderHoverRequest& request) {
    QuaderApiVoidResult status = mutations_.runModelingResult(
            projectSession(session_).selection().updateHover(toModelingHoverRequest(request)),
            "Update Hover");
    return hoverResultFromStatus(std::move(status), projectSession(session_).selection().hoverSummary());
}

QuaderApiResult<QuaderHoverSummary> QuaderSelectionApi::hoverVertex(
        QuaderModelingObjectRef object,
        QuaderElementId vertex) {
    return directHover(object, QuaderActiveSelectionKind::Vertex, vertex, -1, -1, -1);
}

QuaderApiResult<QuaderHoverSummary> QuaderSelectionApi::hoverEdge(
        QuaderModelingObjectRef object,
        QuaderElementId a,
        QuaderElementId b) {
    return directHover(object, QuaderActiveSelectionKind::Edge, -1, a, b, -1);
}

QuaderApiResult<QuaderHoverSummary> QuaderSelectionApi::hoverFace(
        QuaderModelingObjectRef object,
        QuaderElementId face) {
    return directHover(object, QuaderActiveSelectionKind::Face, -1, -1, -1, face);
}

QuaderApiVoidResult QuaderSelectionApi::clearHover() {
    return mutations_.runModelingResult(projectSession(session_).selection().clearHover(), "Clear Hover");
}

QuaderSelectionSummary QuaderSelectionApi::summary() const {
    return toApiSelectionSummary(projectSession(session_).selection().summary());
}

QuaderHoverSummary QuaderSelectionApi::hoverSummary() const {
    return toApiHoverSummary(projectSession(session_).selection().hoverSummary());
}

QuaderHoverSummary QuaderSelectionApi::previewHover(const QuaderHoverRequest& request) const {
    return toApiHoverSummary(projectSession(session_).selection().previewHover(toModelingHoverRequest(request)));
}

QuaderApiResult<QuaderHoverSummary> QuaderSelectionApi::directHover(
        QuaderModelingObjectRef object,
        QuaderActiveSelectionKind kind,
        QuaderElementId vertex,
        QuaderElementId edgeA,
        QuaderElementId edgeB,
        QuaderElementId face) {
    ModelingDirectHoverRequest request;
    request.modelingObjectId = object.modelingObjectId;
    request.vertexId = vertex;
    request.edge = {edgeA, edgeB};
    request.faceId = face;
    switch (kind) {
    case QuaderActiveSelectionKind::Vertex:
        request.kind = ModelingActiveSelectionKind::Vertex;
        break;
    case QuaderActiveSelectionKind::Edge:
        request.kind = ModelingActiveSelectionKind::Edge;
        break;
    case QuaderActiveSelectionKind::Face:
        request.kind = ModelingActiveSelectionKind::Face;
        break;
    case QuaderActiveSelectionKind::None:
    case QuaderActiveSelectionKind::Object:
    case QuaderActiveSelectionKind::Light:
        request.kind = ModelingActiveSelectionKind::None;
        break;
    }
    QuaderApiVoidResult status = mutations_.runModelingResult(
            projectSession(session_).selection().setHover(request),
            "Update Hover");
    return hoverResultFromStatus(std::move(status), projectSession(session_).selection().hoverSummary());
}

} // namespace quader
