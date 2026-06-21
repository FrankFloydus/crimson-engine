#include "api/QuaderSelectionApi.h"

#include "api/QuaderApiMutationRunner.h"
#include "document/ProjectSession.h"

#include <algorithm>
#include <utility>

namespace quader {
namespace {

QuaderApiResult<ModelingSelectionSummary> selectionResultFromStatus(
        QuaderApiVoidResult status,
        ModelingSelectionSummary value) {
    QuaderApiResult<ModelingSelectionSummary> result;
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = std::move(status.message);
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    result.value = value;
    return result;
}

QuaderApiResult<ModelingHoverSummary> hoverResultFromStatus(
        QuaderApiVoidResult status,
        ModelingHoverSummary value) {
    QuaderApiResult<ModelingHoverSummary> result;
    result.ok = status.ok;
    result.changed = status.changed;
    result.message = std::move(status.message);
    result.mutationKind = status.mutationKind;
    result.revisions = status.revisions;
    result.value = value;
    return result;
}

void appendUnique(std::vector<EntityId>& entities, EntityId entity) {
    if (entity != InvalidEntityId && std::find(entities.begin(), entities.end(), entity) == entities.end()) {
        entities.push_back(entity);
    }
}

} // namespace

QuaderSelectionApi::QuaderSelectionApi(ProjectSession& session, QuaderApiMutationRunner& mutations)
    : session_(session)
    , mutations_(mutations) {}

QuaderApiVoidResult QuaderSelectionApi::selectObjects(std::span<const EntityId> entities, EntityId active) {
    return mutations_.runModelingResult(
            session_.modeling().setObjectSelection(entities, active),
            "Select Objects");
}

QuaderApiVoidResult QuaderSelectionApi::selectObject(QuaderModelingObjectRef object, QuaderSelectionEdit edit) {
    std::vector<EntityId> entities = editedObjectSelection(object, edit);
    const EntityId active = entities.empty() ? InvalidEntityId : object.entity;
    return selectObjects(entities, active);
}

QuaderApiVoidResult QuaderSelectionApi::selectComponents(const QuaderComponentSelectionArgs& args) {
    ModelingComponentSelectionRequest request;
    request.sdkObjectId = args.object.sdkObjectId;
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
            session_.modeling().setComponentSelection(request),
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

QuaderApiVoidResult QuaderSelectionApi::clear() {
    return mutations_.runModelingResult(session_.modeling().clearSelection(), "Clear Selection");
}

QuaderApiVoidResult QuaderSelectionApi::selectAll() {
    return mutations_.runModelingResult(session_.modeling().selectAll(), "Select All");
}

QuaderApiVoidResult QuaderSelectionApi::invert() {
    return mutations_.runModelingResult(session_.modeling().invertSelection(), "Invert Selection");
}

QuaderApiResult<ModelingSelectionSummary> QuaderSelectionApi::pickSelect(const ModelingPickSelectRequest& request) {
    const ModelingSelectionSummary picked = session_.modeling().pickSelect(request);
    QuaderApiVoidResult status = mutations_.runModelingResult(
            ModelingCommandResult{.ok = true, .changed = true},
            "Pick Select");
    return selectionResultFromStatus(std::move(status), picked);
}

QuaderApiResult<ModelingHoverSummary> QuaderSelectionApi::hover(const ModelingHoverRequest& request) {
    QuaderApiVoidResult status = mutations_.runModelingResult(
            session_.modeling().updateSelectionHover(request),
            "Update Hover");
    return hoverResultFromStatus(std::move(status), session_.modeling().selectionHoverSummary());
}

QuaderApiResult<ModelingHoverSummary> QuaderSelectionApi::hoverVertex(
        QuaderModelingObjectRef object,
        QuaderElementId vertex) {
    return directHover(ModelingDirectHoverRequest{
            .sdkObjectId = object.sdkObjectId,
            .kind = ModelingActiveSelectionKind::Vertex,
            .vertexId = vertex,
    });
}

QuaderApiResult<ModelingHoverSummary> QuaderSelectionApi::hoverEdge(
        QuaderModelingObjectRef object,
        QuaderElementId a,
        QuaderElementId b) {
    return directHover(ModelingDirectHoverRequest{
            .sdkObjectId = object.sdkObjectId,
            .kind = ModelingActiveSelectionKind::Edge,
            .edge = {a, b},
    });
}

QuaderApiResult<ModelingHoverSummary> QuaderSelectionApi::hoverFace(
        QuaderModelingObjectRef object,
        QuaderElementId face) {
    return directHover(ModelingDirectHoverRequest{
            .sdkObjectId = object.sdkObjectId,
            .kind = ModelingActiveSelectionKind::Face,
            .faceId = face,
    });
}

QuaderApiVoidResult QuaderSelectionApi::clearHover() {
    return mutations_.runModelingResult(session_.modeling().clearSelectionHover(), "Clear Hover");
}

ModelingSelectionSummary QuaderSelectionApi::summary() const {
    return session_.modeling().selectionSummary();
}

ModelingHoverSummary QuaderSelectionApi::hoverSummary() const {
    return session_.modeling().selectionHoverSummary();
}

std::vector<EntityId> QuaderSelectionApi::editedObjectSelection(
        QuaderModelingObjectRef object,
        QuaderSelectionEdit edit) const {
    std::vector<EntityId> entities = session_.scene().selectedEntities();
    const auto found = std::find(entities.begin(), entities.end(), object.entity);
    const bool selected = found != entities.end();
    if (edit == QuaderSelectionEdit::Replace) {
        entities.clear();
        appendUnique(entities, object.entity);
        return entities;
    }
    if (edit == QuaderSelectionEdit::Add || (edit == QuaderSelectionEdit::Toggle && !selected)) {
        appendUnique(entities, object.entity);
        return entities;
    }
    if ((edit == QuaderSelectionEdit::Remove || edit == QuaderSelectionEdit::Toggle) && selected) {
        entities.erase(found);
    }
    return entities;
}

ModelingSelectionEdit QuaderSelectionApi::toModelingEdit(QuaderSelectionEdit edit) {
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

QuaderApiResult<ModelingHoverSummary> QuaderSelectionApi::directHover(const ModelingDirectHoverRequest& request) {
    QuaderApiVoidResult status = mutations_.runModelingResult(
            session_.modeling().setSelectionHover(request),
            "Update Hover");
    return hoverResultFromStatus(std::move(status), session_.modeling().selectionHoverSummary());
}

} // namespace quader
