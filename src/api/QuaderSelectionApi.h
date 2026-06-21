#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiTypes.h"

#include <span>
#include <vector>

namespace quader {

class ProjectSession;
class QuaderApiMutationRunner;

class QuaderSelectionApi final {
public:
    QuaderSelectionApi(ProjectSession& session, QuaderApiMutationRunner& mutations);

    QuaderApiVoidResult selectObjects(std::span<const EntityId> entities, EntityId active = InvalidEntityId);
    QuaderApiVoidResult selectObject(
            QuaderModelingObjectRef object,
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
    QuaderApiVoidResult clear();
    QuaderApiVoidResult selectAll();
    QuaderApiVoidResult invert();

    QuaderApiResult<ModelingSelectionSummary> pickSelect(const ModelingPickSelectRequest& request);
    QuaderApiResult<ModelingHoverSummary> hover(const ModelingHoverRequest& request);
    QuaderApiResult<ModelingHoverSummary> hoverVertex(QuaderModelingObjectRef object, QuaderElementId vertex);
    QuaderApiResult<ModelingHoverSummary> hoverEdge(QuaderModelingObjectRef object, QuaderElementId a, QuaderElementId b);
    QuaderApiResult<ModelingHoverSummary> hoverFace(QuaderModelingObjectRef object, QuaderElementId face);
    QuaderApiVoidResult clearHover();

    [[nodiscard]] ModelingSelectionSummary summary() const;
    [[nodiscard]] ModelingHoverSummary hoverSummary() const;

private:
    [[nodiscard]] std::vector<EntityId> editedObjectSelection(
            QuaderModelingObjectRef object,
            QuaderSelectionEdit edit) const;
    [[nodiscard]] static ModelingSelectionEdit toModelingEdit(QuaderSelectionEdit edit);
    [[nodiscard]] QuaderApiResult<ModelingHoverSummary> directHover(const ModelingDirectHoverRequest& request);

    ProjectSession& session_;
    QuaderApiMutationRunner& mutations_;
};

} // namespace quader
