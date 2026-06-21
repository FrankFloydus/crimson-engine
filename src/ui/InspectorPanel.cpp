#include "ui/InspectorPanel.h"

#include "document/DocumentCommandService.h"
#include "modeling/ModelingSessionService.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"
#include "scene/TransformPivot.h"
#include "ui/UIContext.h"
#include "ui/UILayout.h"
#include "ui/UIStyleTokens.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float RadToDeg = 57.29577951308232f;
constexpr float DegToRad = 0.017453292519943295f;
constexpr float kTransformLabelColumnWidth = 66.0f;
constexpr float kAxisTextColumnWidth = 12.0f;
constexpr float kMinAxisInputWidth = 34.0f;

void copyNameToBuffer(const NameComponent* name, std::array<char, 128>& buffer) {
    buffer.fill('\0');
    if (!name) {
        return;
    }
    std::snprintf(buffer.data(), buffer.size(), "%s", name->name.c_str());
}

Vec3 componentScaleRatio(Vec3 next, Vec3 previous) {
    auto ratio = [](float nextValue, float previousValue) {
        const float denominator = std::abs(previousValue) <= 0.000001f ? 1.0f : previousValue;
        return nextValue / denominator;
    };
    return {ratio(next.x, previous.x), ratio(next.y, previous.y), ratio(next.z, previous.z)};
}

Vec3 multiplyComponents(Vec3 left, Vec3 right) {
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

Vec3 clampScale(Vec3 scale) {
    return {
            std::max(scale.x, 0.001f),
            std::max(scale.y, 0.001f),
            std::max(scale.z, 0.001f),
    };
}

const char* selectionModeLabel(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return "Vertex";
    case ModelingSelectionMode::Edge:
        return "Edge";
    case ModelingSelectionMode::Face:
        return "Face";
    case ModelingSelectionMode::Object:
        return "Object";
    }
    return "Object";
}

const char* activeSelectionKindLabel(ModelingActiveSelectionKind kind) {
    switch (kind) {
    case ModelingActiveSelectionKind::None:
        return "None";
    case ModelingActiveSelectionKind::Object:
        return "Object";
    case ModelingActiveSelectionKind::Vertex:
        return "Vertex";
    case ModelingActiveSelectionKind::Edge:
        return "Edge";
    case ModelingActiveSelectionKind::Face:
        return "Face";
    case ModelingActiveSelectionKind::Light:
        return "Light";
    }
    return "None";
}

bool axisDragFloat(const char* axis,
        ImVec4 color,
        float* value,
        bool mixed,
        float speed,
        float minValue,
        float maxValue,
        float inputWidth) {
    ImGui::PushID(axis);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(axis);
    ImGui::PopStyleColor();
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::SetNextItemWidth(std::max(inputWidth, kMinAxisInputWidth));
    bool changed = false;
    if (mixed) {
        char mixedText[] = "Mixed";
        ImGui::BeginDisabled();
        ImGui::InputText("##value", mixedText, sizeof(mixedText), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
    } else {
        changed = ImGui::DragFloat("##value", value, speed, minValue, maxValue, "%.3f");
    }
    ImGui::PopID();
    return changed;
}

bool axisDragFloat3(const char* label,
        float values[3],
        const std::array<bool, 3>& mixed,
        const UIStyleTokens& style,
        float speed,
        float minValue = 0.0f,
        float maxValue = 0.0f) {
    bool changed = false;
    ImGui::PushID(label);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float axisChromeWidth = kAxisTextColumnWidth + ImGui::GetStyle().ItemInnerSpacing.x;
    const float axisColumnWidth =
            std::max((availableWidth - kTransformLabelColumnWidth) / 3.0f, axisChromeWidth + kMinAxisInputWidth);
    const float inputWidth = axisColumnWidth - axisChromeWidth;
    if (ImGui::BeginTable("##AxisDragFloat3", 4,
                ImGuiTableFlags_SizingFixedFit |
                        ImGuiTableFlags_NoSavedSettings |
                        ImGuiTableFlags_NoPadOuterX)) {
        ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, kTransformLabelColumnWidth);
        ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthFixed, axisColumnWidth);
        ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthFixed, axisColumnWidth);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthFixed, axisColumnWidth);
        ImGui::TableNextRow();

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);

        ImGui::TableSetColumnIndex(1);
        changed = axisDragFloat("X", style.axisX, &values[0], mixed[0], speed, minValue, maxValue, inputWidth) || changed;
        ImGui::TableSetColumnIndex(2);
        changed = axisDragFloat("Y", style.axisY, &values[1], mixed[1], speed, minValue, maxValue, inputWidth) || changed;
        ImGui::TableSetColumnIndex(3);
        changed = axisDragFloat("Z", style.axisZ, &values[2], mixed[2], speed, minValue, maxValue, inputWidth) || changed;

        ImGui::EndTable();
    }
    ImGui::PopID();
    return changed;
}

struct SelectedTransformPivot {
    EntityId entity = InvalidEntityId;
    TransformComponent transform;
    Vec3 localPivot = {};
};

struct BoolAggregate {
    bool hasValue = false;
    bool allTrue = true;
    bool anyTrue = false;

    bool mixed() const {
        return anyTrue && !allTrue;
    }
};

BoolAggregate aggregateVisibility(const SceneDocument& document, const std::vector<EntityId>& selected) {
    BoolAggregate result;
    for (const EntityId id : selected) {
        if (const VisibilityComponent* visibility = document.visibility(id)) {
            result.hasValue = true;
            result.allTrue = result.allTrue && visibility->visible;
            result.anyTrue = result.anyTrue || visibility->visible;
        }
    }
    return result;
}

BoolAggregate aggregateLock(const SceneDocument& document, const std::vector<EntityId>& selected) {
    BoolAggregate result;
    for (const EntityId id : selected) {
        if (const LockComponent* lock = document.lock(id)) {
            result.hasValue = true;
            result.allTrue = result.allTrue && lock->locked;
            result.anyTrue = result.anyTrue || lock->locked;
        }
    }
    return result;
}

bool sameFloat(float a, float b) {
    return std::fabs(a - b) <= 0.0001f;
}

std::array<bool, 3> mixedPositionAxes(const SceneDocument& document,
        const std::vector<EntityId>& selected,
        const TransformComponent& primary) {
    std::array<bool, 3> mixed{false, false, false};
    for (const EntityId id : selected) {
        if (const TransformComponent* transform = document.transform(id)) {
            mixed[0] = mixed[0] || !sameFloat(transform->position.x, primary.position.x);
            mixed[1] = mixed[1] || !sameFloat(transform->position.y, primary.position.y);
            mixed[2] = mixed[2] || !sameFloat(transform->position.z, primary.position.z);
        }
    }
    return mixed;
}

std::array<bool, 3> mixedRotationAxes(const SceneDocument& document,
        const std::vector<EntityId>& selected,
        const TransformComponent& primary) {
    std::array<bool, 3> mixed{false, false, false};
    for (const EntityId id : selected) {
        if (const TransformComponent* transform = document.transform(id)) {
            mixed[0] = mixed[0] || !sameFloat(transform->rotationEuler.x, primary.rotationEuler.x);
            mixed[1] = mixed[1] || !sameFloat(transform->rotationEuler.y, primary.rotationEuler.y);
            mixed[2] = mixed[2] || !sameFloat(transform->rotationEuler.z, primary.rotationEuler.z);
        }
    }
    return mixed;
}

std::array<bool, 3> mixedScaleAxes(const SceneDocument& document,
        const std::vector<EntityId>& selected,
        const TransformComponent& primary) {
    std::array<bool, 3> mixed{false, false, false};
    for (const EntityId id : selected) {
        if (const TransformComponent* transform = document.transform(id)) {
            mixed[0] = mixed[0] || !sameFloat(transform->scale.x, primary.scale.x);
            mixed[1] = mixed[1] || !sameFloat(transform->scale.y, primary.scale.y);
            mixed[2] = mixed[2] || !sameFloat(transform->scale.z, primary.scale.z);
        }
    }
    return mixed;
}

} // namespace

void InspectorPanel::draw(UIContext& context) {
    SceneDocument& document = context.document;
    ModelingSessionService& modeling = context.modeling;

    setNextWindowMinWidth(context.style.sidebarMinWidth);
    if (!ImGui::Begin(title(), nullptr, context.style.panelWindowFlags)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && panelFocusInputActive()) {
        context.requestPanelFocus(id());
    }

    const std::vector<EntityId> selected = document.selectedEntities();
    const ModelingSelectionSummary selection = modeling.selectionSummary();
    if (selected.empty()) {
        if (selection.hasSelection) {
            ImGui::Text("Mode: %s", selectionModeLabel(selection.mode));
            ImGui::Text("Active: %s", activeSelectionKindLabel(selection.activeKind));
            ImGui::Text("Vertices: %u", selection.selectedVertexCount);
            ImGui::Text("Edges: %u", selection.selectedEdgeCount);
            ImGui::Text("Faces: %u", selection.selectedFaceCount);
        } else {
            ImGui::TextUnformatted("No selection");
        }
        ImGui::End();
        return;
    }

    EntityId primary = document.primarySelection();
    if (primary == InvalidEntityId) {
        primary = selected.front();
    }
    ImGui::Text("%llu selected", static_cast<unsigned long long>(selected.size()));
    ImGui::Text("Mode: %s", selectionModeLabel(selection.mode));
    if (selection.mode != ModelingSelectionMode::Object) {
        ImGui::Text("Active: %s", activeSelectionKindLabel(selection.activeKind));
        if (selection.selectedVertexCount > 0u) {
            ImGui::Text("Vertices: %u", selection.selectedVertexCount);
        }
        if (selection.selectedEdgeCount > 0u) {
            ImGui::Text("Edges: %u", selection.selectedEdgeCount);
        }
        if (selection.selectedFaceCount > 0u) {
            ImGui::Text("Faces: %u", selection.selectedFaceCount);
        }
    }

    if (selected.size() == 1u) {
        std::array<char, 128> nameBuffer;
        copyNameToBuffer(document.name(primary), nameBuffer);
        if (ImGui::InputText("Name", nameBuffer.data(), nameBuffer.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            (void)context.documentCommands.renameEntity(primary, nameBuffer.data());
        }
    }

    const BoolAggregate visibilityAggregate = aggregateVisibility(document, selected);
    if (visibilityAggregate.hasValue) {
        bool visible = visibilityAggregate.allTrue;
        if (ImGui::Checkbox("Visible", &visible)) {
            std::vector<DocumentVisibilityEdit> edits;
            edits.reserve(selected.size());
            for (const EntityId id : selected) {
                if (const VisibilityComponent* current = document.visibility(id)) {
                    edits.push_back(DocumentVisibilityEdit{
                            .entity = id,
                            .before = current->visible,
                            .after = visible,
                    });
                }
            }
            (void)context.documentCommands.setVisibility(std::move(edits));
        }
        if (visibilityAggregate.mixed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("Mixed");
        }
    }

    const BoolAggregate lockAggregate = aggregateLock(document, selected);
    if (lockAggregate.hasValue) {
        bool locked = lockAggregate.allTrue;
        if (ImGui::Checkbox("Locked", &locked)) {
            std::vector<DocumentLockEdit> edits;
            edits.reserve(selected.size());
            for (const EntityId id : selected) {
                if (const LockComponent* current = document.lock(id)) {
                    edits.push_back(DocumentLockEdit{
                            .entity = id,
                            .before = current->locked,
                            .after = locked,
                    });
                }
            }
            (void)context.documentCommands.setLocks(std::move(edits));
        }
        if (lockAggregate.mixed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("Mixed");
        }
    }

    TransformComponent* transform = document.transform(primary);
    if (transform) {
        std::size_t transformCount = 0;
        for (const EntityId id : selected) {
            if (document.transform(id)) {
                ++transformCount;
            }
        }

        ImGui::SeparatorText("Transform");
        if (selected.size() > 1u) {
            ImGui::TextDisabled("%llu spatial entities", static_cast<unsigned long long>(transformCount));
        }

        auto applyToSelectedTransforms = [&](auto mutate) {
            std::vector<DocumentTransformEdit> edits;
            edits.reserve(selected.size());
            for (const EntityId id : selected) {
                if (document.isEffectivelyLocked(id)) {
                    continue;
                }
                if (TransformComponent* current = document.transform(id)) {
                    const TransformComponent before = *current;
                    TransformComponent next = before;
                    mutate(next);
                    edits.push_back(DocumentTransformEdit{
                            .entity = id,
                            .before = before,
                            .after = next,
                    });
                }
            }
            (void)context.documentCommands.setTransforms(std::move(edits));
            transform = document.transform(primary);
        };

        auto selectedTransformPivots = [&](Vec3 worldPivot) {
            std::vector<SelectedTransformPivot> records;
            records.reserve(selected.size());
            for (const EntityId id : selected) {
                if (document.isEffectivelyLocked(id)) {
                    continue;
                }
                if (const TransformComponent* current = document.transform(id)) {
                    records.push_back(SelectedTransformPivot{
                            .entity = id,
                            .transform = *current,
                            .localPivot = localPivotForWorldPoint(*current, worldPivot),
                    });
                }
            }
            return records;
        };

        auto applyToSelectedTransformRecords = [&](Vec3 worldPivot, auto apply) {
            const std::vector<SelectedTransformPivot> records = selectedTransformPivots(worldPivot);
            std::vector<DocumentTransformEdit> edits;
            edits.reserve(records.size());
            for (const SelectedTransformPivot& record : records) {
                TransformComponent next = record.transform;
                apply(next, record.localPivot, worldPivot);
                edits.push_back(DocumentTransformEdit{
                        .entity = record.entity,
                        .before = record.transform,
                        .after = next,
                });
            }
            (void)context.documentCommands.setTransforms(std::move(edits));
            transform = document.transform(primary);
        };

        auto selectedTransformPivot = [&]() {
            const std::optional<SceneBounds> bounds = selectedWorldBounds(document, context.assets);
            return bounds ? boundsCenter(*bounds) : transform->position;
        };

        float position[3] = {transform->position.x, transform->position.y, transform->position.z};
        const std::array<bool, 3> positionMixed = mixedPositionAxes(document, selected, *transform);
        if (axisDragFloat3("Position", position, positionMixed, context.style, 0.05f)) {
            const Vec3 previous = transform->position;
            const Vec3 nextPosition{position[0], position[1], position[2]};
            const Vec3 delta = nextPosition - previous;
            applyToSelectedTransforms([&](TransformComponent& next) {
                next.position = next.position + delta;
            });
        }

        float rotation[3] = {
                transform->rotationEuler.x * RadToDeg,
                transform->rotationEuler.y * RadToDeg,
                transform->rotationEuler.z * RadToDeg,
        };
        const std::array<bool, 3> rotationMixed = mixedRotationAxes(document, selected, *transform);
        if (axisDragFloat3("Rotation", rotation, rotationMixed, context.style, 0.5f)) {
            const Vec3 previous = transform->rotationEuler;
            const Vec3 nextRotation{rotation[0] * DegToRad, rotation[1] * DegToRad, rotation[2] * DegToRad};
            const Vec3 delta = nextRotation - previous;
            applyToSelectedTransformRecords(selectedTransformPivot(), [&](TransformComponent& next, Vec3 localPivot, Vec3 worldPivot) {
                applyWorldRotationDeltaAroundPivot(next, delta, localPivot, worldPivot);
            });
        }

        float scale[3] = {transform->scale.x, transform->scale.y, transform->scale.z};
        const std::array<bool, 3> scaleMixed = mixedScaleAxes(document, selected, *transform);
        if (axisDragFloat3("Scale", scale, scaleMixed, context.style, 0.02f, 0.001f, 1000.0f)) {
            const Vec3 ratio = componentScaleRatio({scale[0], scale[1], scale[2]}, transform->scale);
            applyToSelectedTransformRecords(selectedTransformPivot(), [&](TransformComponent& next, Vec3 localPivot, Vec3 worldPivot) {
                next.scale = clampScale(multiplyComponents(next.scale, ratio));
                pinLocalPivotToWorldPoint(next, localPivot, worldPivot);
            });
        }
    }

    if (selected.size() > 1u) {
        std::size_t meshCount = 0;
        std::size_t lightCount = 0;
        for (const EntityId id : selected) {
            meshCount += document.mesh(id) ? 1u : 0u;
            lightCount += document.light(id) ? 1u : 0u;
        }

        ImGui::SeparatorText("Components");
        ImGui::Text("Meshes: %llu", static_cast<unsigned long long>(meshCount));
        ImGui::Text("Lights: %llu", static_cast<unsigned long long>(lightCount));
    }

    ImGui::End();
}

} // namespace quader
