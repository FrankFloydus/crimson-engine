#include "api/QuaderScript.h"

#include "api/QuaderApi.h"

#include <memory>
#include <optional>
#include <utility>

namespace quader {
namespace {

QuaderApiStatus statusFromResult(const QuaderApiStatus& status) {
    return status;
}

template <typename T>
QuaderApiStatus statusFromResult(const QuaderApiResult<T>& result) {
    QuaderApiStatus status;
    status.ok = result.ok;
    status.changed = result.changed;
    status.message = result.message;
    status.mutationKind = result.mutationKind;
    status.revisions = result.revisions;
    return status;
}

} // namespace

QuaderScript::QuaderScript(QuaderApi& api, std::string label)
    : api_(api)
    , label_(std::move(label)) {}

QuaderScript& QuaderScript::createBox(std::string alias, ModelingBounds bounds, std::string name) {
    const std::string stepLabel = "createBox(" + alias + ")";
    return addStep(stepLabel, [this, alias = std::move(alias), bounds, name = std::move(name)](
                                  QuaderScriptResult& result) mutable {
        const QuaderApiResult<QuaderModelingObjectRef> created = api_.modeling().createBox(bounds, std::move(name));
        if (created.ok) {
            result.objects[alias] = created.value;
        }
        return statusFromResult(created);
    });
}

#if QUADER_HAS_MODELING_SDK
QuaderScript& QuaderScript::createMesh(std::string alias, quader_poly::Document document, QuaderCreateMeshArgs args) {
    auto storedDocument = std::make_shared<std::optional<quader_poly::Document>>(std::move(document));
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
                return statusFromResult(created);
            });
}
#endif

QuaderScript& QuaderScript::select(std::string alias) {
    const std::string stepLabel = "select(" + alias + ")";
    return addStep(stepLabel, [this, alias = std::move(alias)](QuaderScriptResult& result) {
        const auto found = result.objects.find(alias);
        if (found == result.objects.end()) {
            return missingAliasStatus(alias);
        }
        return statusFromResult(api_.selection().selectObject(found->second));
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
        return statusFromResult(api_.selection().selectVertex(found->second, vertex));
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
        return statusFromResult(api_.selection().selectEdge(found->second, a, b));
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
        return statusFromResult(api_.selection().selectFace(found->second, face));
    });
}

QuaderScript& QuaderScript::clearSelection() {
    return addStep("clearSelection", [this](QuaderScriptResult&) {
        return statusFromResult(api_.selection().clear());
    });
}

QuaderScript& QuaderScript::setSelectionMode(ModelingSelectionMode mode) {
    return addStep("setSelectionMode", [this, mode](QuaderScriptResult&) {
        return statusFromResult(api_.modeling().setSelectionMode(mode));
    });
}

QuaderScript& QuaderScript::moveSelection(Vec3 delta) {
    return addStep("moveSelection", [this, delta](QuaderScriptResult&) {
        return statusFromResult(api_.modeling().moveSelection(delta));
    });
}

QuaderScript& QuaderScript::rotateSelection(Vec3 eulerRadians, Vec3 pivot) {
    return addStep("rotateSelection", [this, eulerRadians, pivot](QuaderScriptResult&) {
        return statusFromResult(api_.modeling().rotateSelection(eulerRadians, pivot));
    });
}

QuaderScript& QuaderScript::scaleSelection(Vec3 scale, Vec3 pivot) {
    return addStep("scaleSelection", [this, scale, pivot](QuaderScriptResult&) {
        return statusFromResult(api_.modeling().scaleSelection(scale, pivot));
    });
}

QuaderScript& QuaderScript::executeTool(std::string toolId) {
    return addStep("executeTool(" + toolId + ")", [this, toolId = std::move(toolId)](QuaderScriptResult&) {
        return statusFromResult(api_.modeling().executeTool(toolId));
    });
}

QuaderScript& QuaderScript::executeAction(std::string actionId) {
    return addStep("executeAction(" + actionId + ")", [this, actionId = std::move(actionId)](QuaderScriptResult&) {
        return statusFromResult(api_.modeling().executeAction(actionId));
    });
}

QuaderScript& QuaderScript::assignMaterialSlot(std::uint32_t materialSlot) {
    return addStep("assignMaterialSlot", [this, materialSlot](QuaderScriptResult&) {
        return statusFromResult(api_.modeling().assignMaterialSlotToSelection(materialSlot));
    });
}

QuaderScript& QuaderScript::undo() {
    return addStep("undo", [this](QuaderScriptResult&) {
        return statusFromResult(api_.history().undo());
    });
}

QuaderScript& QuaderScript::redo() {
    return addStep("redo", [this](QuaderScriptResult&) {
        return statusFromResult(api_.history().redo());
    });
}

QuaderScriptResult QuaderScript::run() {
    QuaderScriptResult result;
    for (const Step& step : steps_) {
        QuaderApiStatus status = step.execute(result);
        result.steps.push_back({
                .label = step.label,
                .status = status,
        });

        if (!status.ok) {
            result.ok = false;
            break;
        }
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
