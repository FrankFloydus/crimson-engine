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

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {

class QuaderApi;

/**
 * Stores the Quader Script Step Result data contract used by the public Quader API surface.
 */
struct QuaderScriptStepResult {
    std::string label;
    QuaderApiStatus status;
};

/**
 * Stores the Quader Script Result data contract used by the public Quader API surface.
 */
struct QuaderScriptResult : QuaderApiStatus {
    QuaderScriptResult() { ok = true; }

    std::vector<QuaderScriptStepResult> steps;
    std::unordered_map<std::string, QuaderModelingObjectRef> objects;
    QuaderApiSnapshot finalSnapshot;
};

/**
 * Owns Quader Script behavior within the public Quader API surface.
 */
class QuaderScript final {
public:
    QuaderScript(QuaderApi& api, std::string label = {});

    QuaderScript& createBox(std::string alias, QuaderBounds bounds, std::string name = "Box");

    QuaderScript& createMesh(std::string alias, QuaderPolygonDocument document, QuaderCreateMeshArgs args = {});

    QuaderScript& select(std::string alias);
    QuaderScript& selectVertex(std::string alias, QuaderElementId vertex);
    QuaderScript& selectEdge(std::string alias, QuaderElementId a, QuaderElementId b);
    QuaderScript& selectFace(std::string alias, QuaderElementId face);
    QuaderScript& clearSelection();
    QuaderScript& setSelectionMode(QuaderSelectionMode mode);

    QuaderScript& moveSelection(QVec3 delta);
    QuaderScript& rotateSelection(QVec3 eulerRadians, QVec3 pivot = {});
    QuaderScript& scaleSelection(QVec3 scale, QVec3 pivot = {});
    QuaderScript& executeTool(std::string toolId);
    QuaderScript& assignMaterialSlot(std::uint32_t materialSlot);
    QuaderScript& undo();
    QuaderScript& redo();

    [[nodiscard]] QuaderScriptResult run();

private:
    using StepFunction = std::function<QuaderApiStatus(QuaderScriptResult&)>;

    /**
     * Represents a Step value used by the public Quader API surface.
     */
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
