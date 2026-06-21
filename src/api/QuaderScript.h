#pragma once

#include "api/QuaderApiResult.h"
#include "api/QuaderApiSnapshot.h"
#include "api/QuaderApiTypes.h"

#if QUADER_HAS_MODELING_SDK
#include <quader_poly/document.h>
#endif

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

class QuaderApi;

struct QuaderScriptStepResult {
    std::string label;
    QuaderApiStatus status;
};

struct QuaderScriptResult {
    bool ok = true;
    std::vector<QuaderScriptStepResult> steps;
    std::unordered_map<std::string, QuaderModelingObjectRef> objects;
    QuaderApiSnapshot finalSnapshot;
};

class QuaderScript final {
public:
    QuaderScript(QuaderApi& api, std::string label = {});

    QuaderScript& createBox(std::string alias, ModelingBounds bounds, std::string name = "Box");

#if QUADER_HAS_MODELING_SDK
    QuaderScript& createMesh(std::string alias, quader_poly::Document document, QuaderCreateMeshArgs args = {});
#endif

    QuaderScript& select(std::string alias);
    QuaderScript& selectVertex(std::string alias, QuaderElementId vertex);
    QuaderScript& selectEdge(std::string alias, QuaderElementId a, QuaderElementId b);
    QuaderScript& selectFace(std::string alias, QuaderElementId face);
    QuaderScript& clearSelection();
    QuaderScript& setSelectionMode(ModelingSelectionMode mode);

    QuaderScript& moveSelection(Vec3 delta);
    QuaderScript& rotateSelection(Vec3 eulerRadians, Vec3 pivot = {});
    QuaderScript& scaleSelection(Vec3 scale, Vec3 pivot = {});
    QuaderScript& executeTool(std::string toolId);
    QuaderScript& executeAction(std::string actionId);
    QuaderScript& assignMaterialSlot(std::uint32_t materialSlot);
    QuaderScript& undo();
    QuaderScript& redo();

    [[nodiscard]] QuaderScriptResult run();

private:
    using StepFunction = std::function<QuaderApiStatus(QuaderScriptResult&)>;

    struct Step {
        std::string label;
        StepFunction execute;
    };

    QuaderScript& addStep(std::string label, StepFunction execute);
    [[nodiscard]] QuaderApiStatus missingAliasStatus(std::string_view alias) const;

    QuaderApi& api_;
    std::string label_;
    std::vector<Step> steps_;
};

} // namespace quader
