#pragma once

#include "scene/Scene.h"
#include <functional>
#include <string>
#include <vector>

namespace crimson {
class SceneDocument;

// Buffered presentation and gesture baseline for the primary entity's local
// transform. Scene matrices remain authoritative; idle drawing never edits them.
struct InspectorTransformState {
    EntityId entity{};
    uint64_t documentGeneration{},sceneGeneration{},sceneRevision{};
    std::vector<EntityId> selection;
    math::AffineTransformFields fields{},captured{};
    Vec3 degrees{},capturedDegrees{};
    Mat4 shownMatrix{},baseline{};
    uint32_t activeItem{};
    unsigned activeField{};
    bool valid{},editing{},refresh{},scaleLinked{},capturedScaleLinked{};
    std::string error;
};

// The containing Inspector shares one property transaction with name/light
// controls. These callbacks transfer that explicit ownership at field boundaries.
struct InspectorTransformTransactions {
    std::function<bool(unsigned field,const char* label)> begin;
    std::function<void(unsigned field)> finish;
};
void drawInspectorTransform(SceneDocument&,InspectorTransformState&,const EntityRecord&,
                            const InspectorTransformTransactions&);
bool finishInspectorTransform(SceneDocument&,InspectorTransformState&);
} // namespace crimson
