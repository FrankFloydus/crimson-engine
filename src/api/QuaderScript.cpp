////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/QuaderScript.h"

#include "api/QuaderApi.h"
#include "api/QuaderApiMutationRunner.h"
#include "editor/modeling/services/ModelingSessionService.h"

#include <memory>
#include <optional>
#include <utility>

namespace quader {
namespace {

void mergeScriptStepStatus(QuaderScriptResult& result, const QuaderApiStatus& status) {
    if (status.changed) {
        result.changed = true;
    }
    if (status.mutationKind != QuaderMutationKind::None) {
        result.mutationKind = status.mutationKind;
    }
    result.revisions = status.revisions;
}

std::string scriptTransactionLabel(std::string_view label) {
    return label.empty() ? std::string("Script") : std::string(label);
}

} // namespace

QuaderScript::QuaderScript(QuaderApi& api, std::string label)
    : api_(api)
    , label_(std::move(label)) {}

QuaderScript& QuaderScript::createBox(std::string alias, QuaderBounds bounds, std::string name) {
    const std::string stepLabel = "createBox(" + alias + ")";
    return addStep(stepLabel, [this, alias = std::move(alias), bounds, name = std::move(name)](
                                  QuaderScriptResult& result) mutable {
        const QuaderApiResult<QuaderModelingObjectRef> created = api_.modeling().createBox(bounds, std::move(name));
        if (created.ok) {
            result.objects[alias] = created.value;
        }
        return quaderApiStatusFromResult(created);
    });
}

QuaderScript& QuaderScript::createMesh(std::string alias, QuaderPolygonDocument document, QuaderCreateMeshArgs args) {
    auto storedDocument = std::make_shared<std::optional<QuaderPolygonDocument>>(std::move(document));
    const std::string stepLabel = "createMesh(" + alias + ")";
    return addStep(stepLabel,
            [this, alias = std::move(alias), storedDocument, args = std::move(args)](
                    QuaderScriptResult& result) mutable {
                if (!storedDocument->has_value()) {
                    QuaderApiStatus status;
                    status.ok = false;
                    status.changed = false;
                    status.message = "Script mesh document was already consumed.";
                    status.revisions = api_.revisions();
                    return status;
                }

                const QuaderApiResult<QuaderModelingObjectRef> created =
                        api_.modeling().createMesh(std::move(**storedDocument), args);
                storedDocument->reset();
                if (created.ok) {
                    result.objects[alias] = created.value;
                }
                return quaderApiStatusFromResult(created);
            });
}

QuaderScript& QuaderScript::select(std::string alias) {
    const std::string stepLabel = "select(" + alias + ")";
    return addStep(stepLabel, [this, alias = std::move(alias)](QuaderScriptResult& result) {
        const auto found = result.objects.find(alias);
        if (found == result.objects.end()) {
            return missingAliasStatus(alias);
        }
        return quaderApiStatusFromResult(api_.selection().selectObject(found->second));
    });
}

QuaderScript& QuaderScript::selectVertex(std::string alias, QuaderElementId vertex) {
    const std::string stepLabel = "selectVertex(" + alias + ")";
    return addStep(stepLabel, [this, alias = std::move(alias), vertex](
                                  QuaderScriptResult& result) {
        const auto found = result.objects.find(alias);
        if (found == result.objects.end()) {
            return missingAliasStatus(alias);
        }
        return quaderApiStatusFromResult(api_.selection().selectVertex(found->second, vertex));
    });
}

QuaderScript& QuaderScript::selectEdge(std::string alias, QuaderElementId a, QuaderElementId b) {
    const std::string stepLabel = "selectEdge(" + alias + ")";
    return addStep(stepLabel, [this, alias = std::move(alias), a, b](
                                QuaderScriptResult& result) {
        const auto found = result.objects.find(alias);
        if (found == result.objects.end()) {
            return missingAliasStatus(alias);
        }
        return quaderApiStatusFromResult(api_.selection().selectEdge(found->second, a, b));
    });
}

QuaderScript& QuaderScript::selectFace(std::string alias, QuaderElementId face) {
    const std::string stepLabel = "selectFace(" + alias + ")";
    return addStep(stepLabel, [this, alias = std::move(alias), face](
                                QuaderScriptResult& result) {
        const auto found = result.objects.find(alias);
        if (found == result.objects.end()) {
            return missingAliasStatus(alias);
        }
        return quaderApiStatusFromResult(api_.selection().selectFace(found->second, face));
    });
}

QuaderScript& QuaderScript::clearSelection() {
    return addStep("clearSelection", [this](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.selection().clear());
    });
}

QuaderScript& QuaderScript::setSelectionMode(QuaderSelectionMode mode) {
    return addStep("setSelectionMode", [this, mode](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.selection().setMode(mode));
    });
}

QuaderScript& QuaderScript::moveSelection(QVec3 delta) {
    return addStep("moveSelection", [this, delta](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.modeling().moveSelection(delta));
    });
}

QuaderScript& QuaderScript::rotateSelection(QVec3 eulerRadians, QVec3 pivot) {
    return addStep("rotateSelection", [this, eulerRadians, pivot](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.modeling().rotateSelection(eulerRadians, pivot));
    });
}

QuaderScript& QuaderScript::scaleSelection(QVec3 scale, QVec3 pivot) {
    return addStep("scaleSelection", [this, scale, pivot](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.modeling().scaleSelection(scale, pivot));
    });
}

QuaderScript& QuaderScript::executeTool(std::string toolId) {
    return addStep("executeTool(" + toolId + ")", [this, toolId = std::move(toolId)](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.modeling().executeTool(toolId));
    });
}

QuaderScript& QuaderScript::assignMaterialSlot(std::uint32_t materialSlot) {
    return addStep("assignMaterialSlot", [this, materialSlot](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.modeling().assignMaterialSlotToSelection(materialSlot));
    });
}

QuaderScript& QuaderScript::undo() {
    return addStep("undo", [this](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.history().undo());
    });
}

QuaderScript& QuaderScript::redo() {
    return addStep("redo", [this](QuaderScriptResult&) {
        return quaderApiStatusFromResult(api_.history().redo());
    });
}

QuaderScriptResult QuaderScript::run() {
    QuaderScriptResult result;
    QuaderApiMutationRunner& mutations = api_.mutations();
    ModelingSessionMemento before = mutations.captureModelingMemento();
    EditorTransaction transaction = mutations.beginTransaction(scriptTransactionLabel(label_));
    if (!transaction.active()) {
        result.ok = false;
        result.changed = false;
        result.message = "Cannot run script while another editor transaction is active.";
        result.revisions = api_.revisions();
        result.finalSnapshot = api_.snapshot();
        return result;
    }

    for (const Step& step : steps_) {
        QuaderApiStatus status = quaderApiStatusFromResult(step.execute(result));
        result.steps.push_back({
                .label = step.label,
                .status = status,
        });

        if (!status.ok) {
            result.ok = false;
            result.message = status.message;
            mergeScriptStepStatus(result, status);
            break;
        }
        mergeScriptStepStatus(result, status);
    }

    if (!result.ok) {
        const CommandResult canceled = transaction.cancel();
        const QuaderApiVoidResult restored = mutations.restoreModelingMemento(before, "Rollback Script");
        result.objects.clear();
        result.changed = false;
        result.mutationKind = QuaderMutationKind::None;
        result.revisions = restored.revisions;
        if (!canceled.ok || !restored.ok) {
            if (!result.message.empty()) {
                result.message += "; ";
            }
            result.message += !canceled.ok ? canceled.message : restored.message;
        }
        result.finalSnapshot = api_.snapshot();
        return result;
    }

    ModelingSessionMemento after = mutations.captureModelingMemento();
    const CommandResult discarded = transaction.discard();
    if (!discarded.ok) {
        result.ok = false;
        result.changed = false;
        result.mutationKind = QuaderMutationKind::None;
        result.message = discarded.message;
        result.revisions = api_.revisions();
        result.finalSnapshot = api_.snapshot();
        return result;
    }

    if (result.changed) {
        const QuaderApiVoidResult recorded = mutations.recordModelingMementoTransaction(
                scriptTransactionLabel(label_),
                std::move(before),
                std::move(after));
        copyQuaderApiStatus(result, recorded);
    } else {
        result.revisions = api_.revisions();
    }

    result.finalSnapshot = api_.snapshot();
    return result;
}

QuaderScript& QuaderScript::addStep(std::string label, StepFunction execute) {
    steps_.push_back({
            .label = std::move(label),
            .execute = std::move(execute),
    });
    return *this;
}

QuaderApiStatus QuaderScript::missingAliasStatus(std::string_view alias) const {
    QuaderApiStatus status;
    status.ok = false;
    status.changed = false;
    status.message = "Unknown script object alias: " + std::string(alias);
    status.revisions = api_.revisions();
    return status;
}

} // namespace quader
