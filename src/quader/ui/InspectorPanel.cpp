#include "quader/ui/InspectorPanel.h"

#include "quader/modeling/ModelingSessionService.h"
#include "quader/scene/SceneBounds.h"
#include "quader/scene/SceneDocument.h"
#include "quader/scene/TransformPivot.h"
#include "quader/ui/UIContext.h"
#include "quader/ui/UILayout.h"
#include "quader/ui/UIStyleTokens.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <optional>
#include <vector>

namespace quader {
namespace {

constexpr float RadToDeg = 57.29577951308232f;
constexpr float DegToRad = 0.017453292519943295f;
constexpr float kTransformLabelColumnWidth = 72.0f;
constexpr float kAxisTextColumnWidth = 16.0f;
constexpr float kMinAxisInputWidth = 76.0f;

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
    const bool changed = ImGui::DragFloat("##value", value, speed, minValue, maxValue, "%.3f");
    ImGui::PopID();
    return changed;
}

bool axisDragFloat3(const char* label,
        float values[3],
        const UIStyleTokens& style,
        float speed,
        float minValue = 0.0f,
        float maxValue = 0.0f) {
    bool changed = false;
    ImGui::PushID(label);
    const float availableWidth = ImGui::GetContentRegionAvail().x;
    const float axisChromeWidth = kAxisTextColumnWidth + ImGui::GetStyle().ItemInnerSpacing.x;
    const float minimumInlineWidth = kTransformLabelColumnWidth + (axisChromeWidth + kMinAxisInputWidth) * 3.0f;

    if (availableWidth < minimumInlineWidth) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);

        changed = axisDragFloat("X", style.axisX, &values[0], speed, minValue, maxValue,
                          uiFillWidthAtLeast(kMinAxisInputWidth) - axisChromeWidth) ||
                changed;
        changed = axisDragFloat("Y", style.axisY, &values[1], speed, minValue, maxValue,
                          uiFillWidthAtLeast(kMinAxisInputWidth) - axisChromeWidth) ||
                changed;
        changed = axisDragFloat("Z", style.axisZ, &values[2], speed, minValue, maxValue,
                          uiFillWidthAtLeast(kMinAxisInputWidth) - axisChromeWidth) ||
                changed;
        ImGui::PopID();
        return changed;
    }

    const float axisColumnWidth = (availableWidth - kTransformLabelColumnWidth) / 3.0f;
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
        changed = axisDragFloat("X", style.axisX, &values[0], speed, minValue, maxValue, inputWidth) || changed;
        ImGui::TableSetColumnIndex(2);
        changed = axisDragFloat("Y", style.axisY, &values[1], speed, minValue, maxValue, inputWidth) || changed;
        ImGui::TableSetColumnIndex(3);
        changed = axisDragFloat("Z", style.axisZ, &values[2], speed, minValue, maxValue, inputWidth) || changed;

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

} // namespace

void InspectorPanel::draw(UIContext& context) {
    SceneDocument& document = context.document;
    ModelingSessionService& modeling = context.modeling;

    if (!ImGui::Begin(title(), nullptr, context.style.panelWindowFlags)) {
        ImGui::End();
        return;
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
            document.renameEntity(primary, nameBuffer.data());
        }
    }

    bool hasVisibility = false;
    bool allVisible = true;
    for (const EntityId id : selected) {
        if (const VisibilityComponent* visibility = document.visibility(id)) {
            hasVisibility = true;
            allVisible = allVisible && visibility->visible;
        }
    }
    if (hasVisibility) {
        bool visible = allVisible;
        if (ImGui::Checkbox("Visible", &visible)) {
            for (const EntityId id : selected) {
                document.setVisible(id, visible);
            }
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
            for (const EntityId id : selected) {
                if (TransformComponent* current = document.transform(id)) {
                    TransformComponent next = *current;
                    mutate(next);
                    document.setTransform(id, next);
                }
            }
            transform = document.transform(primary);
        };

        auto selectedTransformPivots = [&](Vec3 worldPivot) {
            std::vector<SelectedTransformPivot> records;
            records.reserve(selected.size());
            for (const EntityId id : selected) {
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
            for (const SelectedTransformPivot& record : records) {
                TransformComponent next = record.transform;
                apply(next, record.localPivot, worldPivot);
                document.setTransform(record.entity, next);
            }
            transform = document.transform(primary);
        };

        auto selectedTransformPivot = [&]() {
            const std::optional<SceneBounds> bounds = selectedWorldBounds(document, context.assets);
            return bounds ? boundsCenter(*bounds) : transform->position;
        };

        float position[3] = {transform->position.x, transform->position.y, transform->position.z};
        if (axisDragFloat3("Position", position, context.style, 0.05f)) {
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
        if (axisDragFloat3("Rotation", rotation, context.style, 0.5f)) {
            const Vec3 previous = transform->rotationEuler;
            const Vec3 nextRotation{rotation[0] * DegToRad, rotation[1] * DegToRad, rotation[2] * DegToRad};
            const Vec3 delta = nextRotation - previous;
            applyToSelectedTransformRecords(selectedTransformPivot(), [&](TransformComponent& next, Vec3 localPivot, Vec3 worldPivot) {
                applyWorldRotationDeltaAroundPivot(next, delta, localPivot, worldPivot);
            });
        }

        float scale[3] = {transform->scale.x, transform->scale.y, transform->scale.z};
        if (axisDragFloat3("Scale", scale, context.style, 0.02f, 0.001f, 1000.0f)) {
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
