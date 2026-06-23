////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/InspectorPanel.h"

#include "api/QuaderEditorApi.h"
#include "core/LocalizationService.h"
#include "editor/commands/document/DocumentCommandEdits.h"
#include "editor/modeling/services/ModelingSessionService.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"
#include "scene/TransformPivot.h"
#include "ui/UIContext.h"
#include "ui/UIStyleTokens.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr float RadToDeg = 57.29577951308232f;
constexpr float DegToRad = 0.017453292519943295f;
constexpr float kInspectorLabelColumnWidth = 96.0f;
constexpr float kTransformLabelColumnWidth = 74.0f;
constexpr float kAxisTextColumnWidth = 12.0f;
constexpr float kMinAxisInputWidth = 58.0f;

void copyNameToBuffer(const NameComponent* name, std::array<char, 128>& buffer) {
    buffer.fill('\0');
    if (!name) {
        return;
    }
    std::snprintf(buffer.data(), buffer.size(), "%s", name->name.c_str());
}

QVec3 componentScaleRatio(QVec3 next, QVec3 previous) {
    auto ratio = [](float nextValue, float previousValue) {
        const float denominator = std::abs(previousValue) <= 0.000001f ? 1.0f : previousValue;
        return nextValue / denominator;
    };
    return {ratio(next.x, previous.x), ratio(next.y, previous.y), ratio(next.z, previous.z)};
}

QVec3 multiplyComponents(QVec3 left, QVec3 right) {
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

QVec3 clampScale(QVec3 scale) {
    return {
            std::max(scale.x, 0.001f),
            std::max(scale.y, 0.001f),
            std::max(scale.z, 0.001f),
    };
}

const char* selectionModeKey(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return "selection.mode.vertex";
    case ModelingSelectionMode::Edge:
        return "selection.mode.edge";
    case ModelingSelectionMode::Face:
        return "selection.mode.face";
    case ModelingSelectionMode::Object:
        return "selection.mode.object";
    case ModelingSelectionMode::Group:
        return "selection.mode.group";
    }
    return "selection.mode.object";
}

const char* selectionModeFallback(ModelingSelectionMode mode) {
    switch (mode) {
    case ModelingSelectionMode::Vertex:
        return "Vertex";
    case ModelingSelectionMode::Edge:
        return "Edge";
    case ModelingSelectionMode::Face:
        return "Face";
    case ModelingSelectionMode::Object:
        return "Object";
    case ModelingSelectionMode::Group:
        return "Group";
    }
    return "Object";
}

bool componentSelectionMode(ModelingSelectionMode mode) {
    return mode == ModelingSelectionMode::Vertex ||
            mode == ModelingSelectionMode::Edge ||
            mode == ModelingSelectionMode::Face;
}

const char* activeSelectionKindKey(ModelingActiveSelectionKind kind) {
    switch (kind) {
    case ModelingActiveSelectionKind::None:
        return "selection.active.none";
    case ModelingActiveSelectionKind::Object:
        return "selection.active.object";
    case ModelingActiveSelectionKind::Vertex:
        return "selection.active.vertex";
    case ModelingActiveSelectionKind::Edge:
        return "selection.active.edge";
    case ModelingActiveSelectionKind::Face:
        return "selection.active.face";
    case ModelingActiveSelectionKind::Light:
        return "selection.active.light";
    }
    return "selection.active.none";
}

const char* activeSelectionKindFallback(ModelingActiveSelectionKind kind) {
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
        const char* mixedLabel,
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
        std::array<char, 64> mixedBuffer{};
        std::snprintf(mixedBuffer.data(), mixedBuffer.size(), "%s", mixedLabel);
        ImGui::BeginDisabled();
        ImGui::InputText("##value", mixedBuffer.data(), mixedBuffer.size(), ImGuiInputTextFlags_ReadOnly);
        ImGui::EndDisabled();
    } else {
        changed = ImGui::DragFloat("##value", value, speed, minValue, maxValue, "%.3f");
    }
    ImGui::PopID();
    return changed;
}

bool axisDragFloat3(const char* stableId,
        const char* label,
        float values[3],
        const std::array<bool, 3>& mixed,
        const char* mixedLabel,
        const UIStyleTokens& style,
        float speed,
        float minValue = 0.0f,
        float maxValue = 0.0f) {
    bool changed = false;
    ImGui::PushID(stableId);
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
        changed = axisDragFloat("X", style.axisX, &values[0], mixed[0], mixedLabel, speed, minValue, maxValue, inputWidth) || changed;
        ImGui::TableSetColumnIndex(2);
        changed = axisDragFloat("Y", style.axisY, &values[1], mixed[1], mixedLabel, speed, minValue, maxValue, inputWidth) || changed;
        ImGui::TableSetColumnIndex(3);
        changed = axisDragFloat("Z", style.axisZ, &values[2], mixed[2], mixedLabel, speed, minValue, maxValue, inputWidth) || changed;

        ImGui::EndTable();
    }
    ImGui::PopID();
    return changed;
}

bool beginLabeledControlTable(const char* stableId) {
    ImGui::PushID(stableId);
    return ImGui::BeginTable("##InspectorLabeledControl", 2,
            ImGuiTableFlags_SizingFixedFit |
                    ImGuiTableFlags_NoSavedSettings |
                    ImGuiTableFlags_NoPadOuterX);
}

void setupLabeledControlColumns() {
    ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthFixed, kInspectorLabelColumnWidth);
    ImGui::TableSetupColumn(
            "Value",
            ImGuiTableColumnFlags_WidthFixed,
            std::max(ImGui::GetContentRegionAvail().x - kInspectorLabelColumnWidth, kMinAxisInputWidth));
    ImGui::TableNextRow();
}

void endLabeledControlTable() {
    ImGui::EndTable();
    ImGui::PopID();
}

bool inputTextWithLeftLabel(const char* stableId, const char* label, char* buffer, std::size_t bufferSize, ImGuiInputTextFlags flags) {
    bool changed = false;
    if (beginLabeledControlTable(stableId)) {
        setupLabeledControlColumns();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        changed = ImGui::InputText("##value", buffer, bufferSize, flags);
        endLabeledControlTable();
    } else {
        ImGui::PopID();
    }
    return changed;
}

bool checkboxWithLeftLabel(const char* stableId, const char* label, bool* value) {
    bool changed = false;
    if (beginLabeledControlTable(stableId)) {
        setupLabeledControlColumns();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        changed = ImGui::Checkbox("##value", value);
        endLabeledControlTable();
    } else {
        ImGui::PopID();
    }
    return changed;
}

template <typename Enum>
bool comboWithLeftLabel(const char* stableId,
        const char* label,
        Enum* value,
        const std::vector<std::pair<Enum, std::string>>& options) {
    bool changed = false;
    if (beginLabeledControlTable(stableId)) {
        setupLabeledControlColumns();
        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(label);
        ImGui::TableSetColumnIndex(1);
        const auto selected = std::find_if(options.begin(), options.end(), [&](const auto& option) {
            return option.first == *value;
        });
        const char* preview = selected == options.end() ? "" : selected->second.c_str();
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ImGui::BeginCombo("##value", preview)) {
            for (const auto& [optionValue, optionLabel] : options) {
                const bool isSelected = optionValue == *value;
                if (ImGui::Selectable(optionLabel.c_str(), isSelected)) {
                    *value = optionValue;
                    changed = true;
                }
                if (isSelected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        endLabeledControlTable();
    } else {
        ImGui::PopID();
    }
    return changed;
}

/**
 * Represents a Selected Transform Pivot value used by the ImGui editor presentation layer.
 */
struct SelectedTransformPivot {
    EntityId entity = InvalidEntityId;
    TransformComponent transform;
    QVec3 localPivot = {};
};

/**
 * Represents a Bool Aggregate value used by the ImGui editor presentation layer.
 */
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

float InspectorPanel::minimumWidth(const UIContext& context) const {
    // UIDockPanel applies setNextWindowMinWidth(context.style.sidebarMinWidth) before opening the window.
    return context.style.sidebarMinWidth;
}

void InspectorPanel::drawContent(UIContext& context) {
    // UIDockPanel routes hover input through context.requestPanelFocus(id()) before content draws.
    SceneDocument& document = context.document;
    ModelingSessionService& modeling = context.modeling;

    const std::vector<EntityId> selected = document.selectedEntities();
    const ModelingSelectionSummary selection = modeling.selectionSummary();
    LocalizationService& localization = context.localization;
    const std::string mixedLabel = localization.text("ui.inspector.mixed", "Mixed");
    const std::string modePrefix = localization.text("ui.inspector.mode", "Mode");
    const std::string activePrefix = localization.text("ui.inspector.active", "Active");
    const std::string verticesPrefix = localization.text("ui.inspector.vertices", "Vertices");
    const std::string edgesPrefix = localization.text("ui.inspector.edges", "Edges");
    const std::string facesPrefix = localization.text("ui.inspector.faces", "Faces");
    if (selected.empty()) {
        if (selection.hasSelection) {
            const std::string mode = localization.text(selectionModeKey(selection.mode), selectionModeFallback(selection.mode));
            const std::string active = localization.text(activeSelectionKindKey(selection.activeKind), activeSelectionKindFallback(selection.activeKind));
            ImGui::Text("%s: %s", modePrefix.c_str(), mode.c_str());
            ImGui::Text("%s: %s", activePrefix.c_str(), active.c_str());
            ImGui::Text("%s: %u", verticesPrefix.c_str(), selection.selectedVertexCount);
            ImGui::Text("%s: %u", edgesPrefix.c_str(), selection.selectedEdgeCount);
            ImGui::Text("%s: %u", facesPrefix.c_str(), selection.selectedFaceCount);
        } else {
            const std::string noSelection = localization.text("ui.inspector.no_selection", "No selection");
            ImGui::TextUnformatted(noSelection.c_str());
        }
        return;
    }

    EntityId primary = document.primarySelection();
    if (primary == InvalidEntityId) {
        primary = selected.front();
    }
    if (componentSelectionMode(selection.mode)) {
        const std::string active = localization.text(activeSelectionKindKey(selection.activeKind), activeSelectionKindFallback(selection.activeKind));
        ImGui::Text("%s: %s", activePrefix.c_str(), active.c_str());
        if (selection.selectedVertexCount > 0u) {
            ImGui::Text("%s: %u", verticesPrefix.c_str(), selection.selectedVertexCount);
        }
        if (selection.selectedEdgeCount > 0u) {
            ImGui::Text("%s: %u", edgesPrefix.c_str(), selection.selectedEdgeCount);
        }
        if (selection.selectedFaceCount > 0u) {
            ImGui::Text("%s: %u", facesPrefix.c_str(), selection.selectedFaceCount);
        }
    }

    if (selected.size() == 1u) {
        std::array<char, 128> nameBuffer;
        copyNameToBuffer(document.name(primary), nameBuffer);
        const std::string nameLabel = localization.text("ui.inspector.name", "Name");
        if (inputTextWithLeftLabel("name", nameLabel.c_str(), nameBuffer.data(), nameBuffer.size(),
                    ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll)) {
            (void)context.editor.scene().rename(primary, nameBuffer.data());
        }
    }

    const BoolAggregate visibilityAggregate = aggregateVisibility(document, selected);
    if (visibilityAggregate.hasValue) {
        bool hidden = !visibilityAggregate.allTrue;
        const std::string hiddenLabel = localization.text("ui.inspector.hidden", "Hidden");
        if (checkboxWithLeftLabel("hidden", hiddenLabel.c_str(), &hidden)) {
            std::vector<DocumentVisibilityEdit> edits;
            edits.reserve(selected.size());
            for (const EntityId id : selected) {
                if (const VisibilityComponent* current = document.visibility(id)) {
                    edits.push_back(DocumentVisibilityEdit{
                            .entity = id,
                            .before = current->visible,
                            .after = !hidden,
                    });
                }
            }
            (void)context.editor.scene().setVisibility(std::span<const DocumentVisibilityEdit>(edits.data(), edits.size()));
        }
        if (visibilityAggregate.mixed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mixedLabel.c_str());
        }
    }

    const BoolAggregate lockAggregate = aggregateLock(document, selected);
    if (lockAggregate.hasValue) {
        bool locked = lockAggregate.allTrue;
        const std::string lockedLabel = localization.text("ui.inspector.locked", "Locked");
        if (checkboxWithLeftLabel("locked", lockedLabel.c_str(), &locked)) {
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
            (void)context.editor.scene().setLocks(std::span<const DocumentLockEdit>(edits.data(), edits.size()));
        }
        if (lockAggregate.mixed()) {
            ImGui::SameLine();
            ImGui::TextDisabled("%s", mixedLabel.c_str());
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

        const std::string transformLabel = localization.text("ui.inspector.transform", "Transform");
        if (ImGui::CollapsingHeader(transformLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
        if (selected.size() > 1u) {
            const std::string spatialEntities = localization.format(
                    "ui.inspector.spatial_entities",
                    "{count} spatial entities",
                    {{"count", std::to_string(transformCount)}});
            ImGui::TextDisabled("%s", spatialEntities.c_str());
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
            (void)context.editor.scene().setTransforms(std::span<const DocumentTransformEdit>(edits.data(), edits.size()));
            transform = document.transform(primary);
        };

        auto selectedTransformPivots = [&](QVec3 worldPivot) {
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

        auto applyToSelectedTransformRecords = [&](QVec3 worldPivot, auto apply) {
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
            (void)context.editor.scene().setTransforms(std::span<const DocumentTransformEdit>(edits.data(), edits.size()));
            transform = document.transform(primary);
        };

        auto selectedTransformPivot = [&]() {
            const std::optional<SceneBounds> bounds = selectedWorldBounds(document, context.assets);
            return bounds ? boundsCenter(*bounds) : transform->position;
        };

        float position[3] = {transform->position.x, transform->position.y, transform->position.z};
        const std::array<bool, 3> positionMixed = mixedPositionAxes(document, selected, *transform);
        const std::string positionLabel = localization.text("ui.inspector.position", "Position");
        if (axisDragFloat3("position", positionLabel.c_str(), position, positionMixed, mixedLabel.c_str(), context.style, 0.05f)) {
            const QVec3 previous = transform->position;
            const QVec3 nextPosition{position[0], position[1], position[2]};
            const QVec3 delta = nextPosition - previous;
            applyToSelectedTransforms([&](TransformComponent& next) {
                next.position = next.position + delta;
            });
        }

        const QVec3 displayedRotation = positiveEulerRadians(transform->rotationEuler);
        float rotation[3] = {
                displayedRotation.x * RadToDeg,
                displayedRotation.y * RadToDeg,
                displayedRotation.z * RadToDeg,
        };
        const std::array<bool, 3> rotationMixed = mixedRotationAxes(document, selected, *transform);
        const std::string rotationLabel = localization.text("ui.inspector.rotation", "Rotation");
        if (axisDragFloat3("rotation", rotationLabel.c_str(), rotation, rotationMixed, mixedLabel.c_str(), context.style, 0.5f)) {
            const QVec3 previous = displayedRotation;
            const QVec3 nextRotation{rotation[0] * DegToRad, rotation[1] * DegToRad, rotation[2] * DegToRad};
            const QVec3 delta = nextRotation - previous;
            applyToSelectedTransformRecords(selectedTransformPivot(), [&](TransformComponent& next, QVec3 localPivot, QVec3 worldPivot) {
                applyWorldRotationDeltaAroundPivot(next, delta, localPivot, worldPivot);
            });
        }

        float scale[3] = {transform->scale.x, transform->scale.y, transform->scale.z};
        const std::array<bool, 3> scaleMixed = mixedScaleAxes(document, selected, *transform);
        const std::string scaleLabel = localization.text("ui.inspector.scale", "Scale");
        if (axisDragFloat3("scale", scaleLabel.c_str(), scale, scaleMixed, mixedLabel.c_str(), context.style, 0.02f, 0.001f, 1000.0f)) {
            const QVec3 ratio = componentScaleRatio({scale[0], scale[1], scale[2]}, transform->scale);
            applyToSelectedTransformRecords(selectedTransformPivot(), [&](TransformComponent& next, QVec3 localPivot, QVec3 worldPivot) {
                next.scale = clampScale(multiplyComponents(next.scale, ratio));
                pinLocalPivotToWorldPoint(next, localPivot, worldPivot);
            });
        }
        }
    }

    if (selected.size() == 1u) {
        if (const MeshComponent* currentMesh = document.mesh(primary)) {
            MeshComponent mesh = *currentMesh;
            auto applyMeshComponent = [&](MeshComponent next) {
                (void)context.editor.scene().setMeshComponent(primary, next);
            };

            const std::string exportLabel = localization.text("ui.inspector.export", "Export");
            if (ImGui::CollapsingHeader(exportLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                bool editorOnly = mesh.editorOnly;
                const std::string editorOnlyLabel = localization.text("ui.inspector.editor_only", "Editor Only");
                if (checkboxWithLeftLabel("editor_only", editorOnlyLabel.c_str(), &editorOnly)) {
                    mesh.editorOnly = editorOnly;
                    applyMeshComponent(mesh);
                }
            }

            const std::string lightingLabel = localization.text("ui.inspector.lighting", "Lighting");
            if (ImGui::CollapsingHeader(lightingLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                bool castShadows = mesh.castShadows;
                const std::string castShadowsLabel = localization.text("ui.inspector.cast_shadows", "Cast Shadows");
                if (checkboxWithLeftLabel("cast_shadows", castShadowsLabel.c_str(), &castShadows)) {
                    mesh.castShadows = castShadows;
                    applyMeshComponent(mesh);
                }

                bool doubleSided = mesh.doubleSided;
                const std::string doubleSidedLabel = localization.text("ui.inspector.double_sided", "Double sided");
                if (checkboxWithLeftLabel("double_sided", doubleSidedLabel.c_str(), &doubleSided)) {
                    mesh.doubleSided = doubleSided;
                    applyMeshComponent(mesh);
                }
            }

            const std::string physicsLabel = localization.text("ui.inspector.physics", "Physics");
            if (ImGui::CollapsingHeader(physicsLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
                bool generateCollisions = mesh.generateCollisions;
                const std::string generateCollisionsLabel = localization.text("ui.inspector.generate_collisions", "Generate Collisions");
                if (checkboxWithLeftLabel("generate_collisions", generateCollisionsLabel.c_str(), &generateCollisions)) {
                    mesh.generateCollisions = generateCollisions;
                    applyMeshComponent(mesh);
                }

                MeshCollisionType collisionType = mesh.collisionType;
                const std::string collisionTypeLabel = localization.text("ui.inspector.collision_type", "Collision Type");
                const std::vector<std::pair<MeshCollisionType, std::string>> collisionOptions{
                        {MeshCollisionType::Box, localization.text("ui.inspector.collision_type.box", "Box")},
                        {MeshCollisionType::Sphere, localization.text("ui.inspector.collision_type.sphere", "Sphere")},
                        {MeshCollisionType::Cylinder, localization.text("ui.inspector.collision_type.cylinder", "Cylinder")},
                        {MeshCollisionType::ConvexHull, localization.text("ui.inspector.collision_type.convex_hull", "Convex Hull")},
                        {MeshCollisionType::Mesh, localization.text("ui.inspector.collision_type.mesh", "Mesh")},
                };
                if (comboWithLeftLabel("collision_type", collisionTypeLabel.c_str(), &collisionType, collisionOptions)) {
                    mesh.collisionType = collisionType;
                    applyMeshComponent(mesh);
                }

                bool displayCollision = mesh.displayCollision;
                const std::string displayCollisionLabel = localization.text("ui.inspector.display_collision", "Display Collision");
                if (checkboxWithLeftLabel("display_collision", displayCollisionLabel.c_str(), &displayCollision)) {
                    mesh.displayCollision = displayCollision;
                    applyMeshComponent(mesh);
                }

                MeshPhysicsBodyType bodyType = mesh.bodyType;
                const std::string bodyTypeLabel = localization.text("ui.inspector.body_type", "Body type");
                const std::vector<std::pair<MeshPhysicsBodyType, std::string>> bodyOptions{
                        {MeshPhysicsBodyType::StaticBody, localization.text("ui.inspector.body_type.static_body", "Static Body")},
                        {MeshPhysicsBodyType::RigidBody, localization.text("ui.inspector.body_type.rigid_body", "Rigid Body")},
                };
                if (comboWithLeftLabel("body_type", bodyTypeLabel.c_str(), &bodyType, bodyOptions)) {
                    mesh.bodyType = bodyType;
                    applyMeshComponent(mesh);
                }
            }
        }
    }

    if (selected.size() > 1u) {
        std::size_t meshCount = 0;
        std::size_t lightCount = 0;
        for (const EntityId id : selected) {
            meshCount += document.mesh(id) ? 1u : 0u;
            lightCount += document.light(id) ? 1u : 0u;
        }

        const std::string componentsLabel = localization.text("ui.inspector.components", "Components");
        if (ImGui::CollapsingHeader(componentsLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            const std::string meshesLabel = localization.text("ui.inspector.meshes", "Meshes");
            const std::string lightsLabel = localization.text("ui.inspector.lights", "Lights");
            ImGui::Text("%s: %llu", meshesLabel.c_str(), static_cast<unsigned long long>(meshCount));
            ImGui::Text("%s: %llu", lightsLabel.c_str(), static_cast<unsigned long long>(lightCount));
        }
    }
}

} // namespace quader
