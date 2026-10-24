#pragma once
#include "scene/Scene.h"
#include "assets/ModelAsset.h"
#include <stop_token>

namespace crimson {
// Captured by the document owner before detached import preparation. Payloads
// and model generations are immutable; workers never access the live scene.
struct QuaderPaintInput {
    std::string sourceKey,modelId;
    std::shared_ptr<const ModelAsset> model;
    std::optional<SceneMeshPaint> paint;
};
struct QuaderPaintReplacement {
    std::string sourceKey,beforeModelId;
    std::optional<SceneMeshPaint> before,after;
    // Error is measured in normalized material weight, across all four layers.
    float maxError{},rmsError{};
};
QuaderPaintReplacement migrateQuaderPaint(const QuaderPaintInput&,const ModelAsset& after,std::stop_token={});
}
