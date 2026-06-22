#include "ui/OutlinerPanel.h"

#include "api/QuaderEditorApi.h"
#include "core/LocalizationService.h"
#include "modeling/ModelingSessionService.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"
#include "scene/SceneEntityNaming.h"
#include "ui/UIContext.h"
#include "ui/UIStyleTokens.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace quader {
namespace {

enum class PendingOutlinerAction {
    None,
    Delete,
    Duplicate,
    Group,
    Ungroup,
    UngroupSelection,
};

bool containsEntity(const std::vector<EntityId>& ids, EntityId id) {
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}

void syncSelectionToModeling(UIContext& context) {
    const std::vector<EntityId> desiredSelection = context.document.selectedEntities();
    const EntityId activeSelection = context.document.primarySelection();
    const ModelingSelectionMode previousMode = context.modeling.selectionMode();
    if (previousMode != ModelingSelectionMode::Group) {
        (void)context.editor.modeling().setSelectionMode(ModelingSelectionMode::Object);
    }
    (void)context.editor.scene().select(std::span<const EntityId>(desiredSelection.data(), desiredSelection.size()), activeSelection);
    context.modeling.syncObjectSelectionFromScene();
    if (previousMode == ModelingSelectionMode::Group) {
        (void)context.editor.modeling().setSelectionMode(ModelingSelectionMode::Group);
    }
}

std::vector<EntityId> entityRangeSelection(
        const std::vector<EntityRecord>& records,
        EntityId anchor,
        EntityId clicked) {
    if (anchor == InvalidEntityId || anchor == clicked) {
        return {clicked};
    }

    std::vector<EntityId> selection;
    bool selecting = false;
    for (const EntityRecord& record : records) {
        const bool endpoint = record.id == anchor || record.id == clicked;
        if (endpoint && !selecting) {
            selecting = true;
            selection.push_back(record.id);
            continue;
        }

        if (selecting) {
            selection.push_back(record.id);
        }

        if (endpoint) {
            return selection;
        }
    }

    return {clicked};
}

std::vector<EntityId> selectedRootEntities(const SceneDocument& document) {
    const std::vector<EntityId> selected = document.selectedEntities();
    std::unordered_set<EntityId> selectedSet(selected.begin(), selected.end());
    std::vector<EntityId> roots;
    for (const EntityId entity : document.entityTreeOrder()) {
        if (!selectedSet.contains(entity)) {
            continue;
        }
        bool ancestorSelected = false;
        EntityId parent = document.parent(entity);
        while (parent != InvalidEntityId) {
            if (selectedSet.contains(parent)) {
                ancestorSelected = true;
                break;
            }
            parent = document.parent(parent);
        }
        if (!ancestorSelected) {
            roots.push_back(entity);
        }
    }
    return roots;
}

std::vector<EntityId> selectedHierarchyEntities(const SceneDocument& document) {
    std::vector<EntityId> result;
    std::unordered_set<EntityId> seen;
    for (const EntityId root : selectedRootEntities(document)) {
        for (const EntityId entity : document.descendants(root, true)) {
            if (seen.insert(entity).second) {
                result.push_back(entity);
            }
        }
    }
    return result;
}

bool selectOnly(UIContext& context, const std::vector<EntityId>& entities, EntityId active = InvalidEntityId) {
    QuaderApiVoidResult result = context.editor.scene().select(std::span<const EntityId>(entities.data(), entities.size()), active);
    return result.ok && result.changed;
}

void groupSelection(UIContext& context) {
    SceneDocument& document = context.document;
    const std::vector<EntityId> roots = selectedRootEntities(document);
    if (roots.empty()) {
        return;
    }

    TransformComponent groupTransform;
    if (const std::optional<SceneBounds> bounds = selectedWorldBounds(document, context.assets)) {
        groupTransform.position = boundsCenter(*bounds);
    }

    const QuaderApiResult<QuaderEntityRef> result = context.editor.scene().groupSelection({}, groupTransform);
    if (result.ok && result.changed) {
        syncSelectionToModeling(context);
    }
}

void ungroupEntity(UIContext& context, EntityId group) {
    SceneDocument& document = context.document;
    if (!document.exists(group) || document.kind(group) != EntityKind::Empty || !document.hasChildren(group)) {
        return;
    }

    if (!containsEntity(document.selectedEntities(), group)) {
        (void)selectOnly(context, {group}, group);
    }
    QuaderApiVoidResult result = context.editor.scene().ungroupSelection();
    if (result.ok && result.changed) {
        syncSelectionToModeling(context);
    }
}

void ungroupSelection(UIContext& context) {
    QuaderApiVoidResult result = context.editor.scene().ungroupSelection();
    if (result.ok && result.changed) {
        syncSelectionToModeling(context);
    }
}

void deleteSelection(UIContext& context) {
    SceneDocument& document = context.document;
    const std::vector<EntityId> hierarchyEntities = selectedHierarchyEntities(document);
    if (hierarchyEntities.empty()) {
        return;
    }

    std::vector<EntityId> modelingEntities;
    for (const EntityId entity : hierarchyEntities) {
        if (document.modelingObject(entity) != nullptr) {
            modelingEntities.push_back(entity);
        }
    }

    if (!modelingEntities.empty()) {
        (void)selectOnly(context, modelingEntities);
        syncSelectionToModeling(context);
        (void)context.editor.commands().execute("modeling.delete_selection");
    }

    for (const EntityId root : selectedRootEntities(document)) {
        if (document.exists(root)) {
            (void)context.editor.scene().destroy(root);
        }
    }
    for (const EntityId entity : hierarchyEntities) {
        if (document.exists(entity) && document.modelingObject(entity) == nullptr) {
            (void)context.editor.scene().destroy(entity);
        }
    }
    context.modeling.syncObjectSelectionFromScene();
}

std::vector<EntityId> duplicateModelingEntities(UIContext& context, const std::vector<EntityId>& sourceMeshes) {
    if (sourceMeshes.empty()) {
        return {};
    }

    const std::vector<EntityId> beforeEntities = context.document.entities();
    std::unordered_set<EntityId> beforeSet(beforeEntities.begin(), beforeEntities.end());
    (void)selectOnly(context, sourceMeshes);
    syncSelectionToModeling(context);
    const QuaderApiVoidResult duplicateResult = context.editor.commands().execute("modeling.duplicate_meshes");
    if (!duplicateResult.ok) {
        return {};
    }

    std::vector<EntityId> duplicates;
    for (const EntityId entity : context.document.entities()) {
        if (!beforeSet.contains(entity) && context.document.modelingObject(entity) != nullptr) {
            duplicates.push_back(entity);
        }
    }
    std::sort(duplicates.begin(), duplicates.end());
    return duplicates;
}

void copyCommonEntityState(UIContext& context, EntityId source, EntityId clone, bool renameClone = true) {
    SceneDocument& document = context.document;
    if (renameClone) {
        const NameComponent* name = document.name(source);
        if (name != nullptr) {
            (void)context.editor.scene().rename(clone, SceneEntityNaming::nextName(document, name->name));
        }
    }
    if (const TransformComponent* transform = document.transform(source)) {
        (void)context.editor.scene().setTransform(clone, *transform);
    }
    if (const VisibilityComponent* visibility = document.visibility(source)) {
        (void)context.editor.scene().setVisible(clone, visibility->visible);
    }
    if (const LockComponent* lock = document.lock(source)) {
        (void)context.editor.scene().setLocked(clone, lock->locked);
    }
}

void duplicateSelection(UIContext& context) {
    SceneDocument& document = context.document;
    const std::vector<EntityId> roots = selectedRootEntities(document);
    if (roots.empty()) {
        return;
    }

    std::vector<EntityId> sourceMeshes;
    for (const EntityId root : roots) {
        for (const EntityId entity : document.descendants(root, true)) {
            if (document.modelingObject(entity) != nullptr) {
                sourceMeshes.push_back(entity);
            }
        }
    }

    const std::vector<EntityId> duplicateMeshes = duplicateModelingEntities(context, sourceMeshes);
    std::unordered_map<EntityId, EntityId> cloneBySource;
    for (std::size_t index = 0; index < sourceMeshes.size() && index < duplicateMeshes.size(); ++index) {
        cloneBySource[sourceMeshes[index]] = duplicateMeshes[index];
    }

    std::function<EntityId(EntityId)> cloneEntity = [&](EntityId source) -> EntityId {
        EntityId clone = InvalidEntityId;
        if (document.modelingObject(source) != nullptr) {
            const auto mapped = cloneBySource.find(source);
            if (mapped == cloneBySource.end()) {
                return InvalidEntityId;
            }
            clone = mapped->second;
            copyCommonEntityState(context, source, clone);
        } else {
            const NameComponent* name = document.name(source);
            const std::string baseName = name ? name->name : "Entity";
            TransformComponent transform;
            if (const TransformComponent* sourceTransform = document.transform(source)) {
                transform = *sourceTransform;
            }
            const QuaderApiResult<QuaderEntityRef> created = context.editor.scene().createEntity(QuaderCreateEntityArgs{
                    .name = SceneEntityNaming::nextName(document, baseName),
                    .kind = document.kind(source),
                    .parent = InvalidEntityId,
                    .transform = transform,
            });
            if (!created.ok || !created.changed) {
                return InvalidEntityId;
            }
            clone = created.value.id;
            copyCommonEntityState(context, source, clone, false);
        }

        for (const EntityId child : document.children(source)) {
            const EntityId childClone = cloneEntity(child);
            if (childClone != InvalidEntityId) {
                (void)context.editor.scene().reparent(childClone, clone, false);
            }
        }
        return clone;
    };

    std::vector<EntityId> rootClones;
    for (const EntityId root : roots) {
        const EntityId clone = cloneEntity(root);
        if (clone != InvalidEntityId) {
            (void)context.editor.scene().reparent(clone, document.parent(root), false);
            rootClones.push_back(clone);
        }
    }

    (void)selectOnly(context, rootClones);
    syncSelectionToModeling(context);
}

ImTextureID iconFor(const UIIconSet& icons, EntityKind kind) {
    switch (kind) {
    case EntityKind::Empty:
        return icons.empty;
    case EntityKind::Mesh:
        return icons.mesh;
    case EntityKind::Light:
    case EntityKind::Generic:
        return ImTextureID_Invalid;
    }
    return ImTextureID_Invalid;
}

void drawOutlinerText(
        ImDrawList* drawList,
        ImVec2 position,
        ImU32 color,
        const std::string& text,
        bool italic,
        const ImVec4& clipRect) {
    const int vertexStart = drawList->VtxBuffer.Size;
    drawList->AddText(
            ImGui::GetFont(),
            ImGui::GetFontSize(),
            position,
            color,
            text.c_str(),
            text.c_str() + text.size(),
            0.0f,
            &clipRect);
    if (!italic) {
        return;
    }

    const float baseline = position.y + ImGui::GetTextLineHeight();
    for (int index = vertexStart; index < drawList->VtxBuffer.Size; ++index) {
        ImDrawVert& vertex = drawList->VtxBuffer[index];
        vertex.pos.x += (baseline - vertex.pos.y) * 0.16f;
    }
}

void drawHierarchyGuides(
        ImDrawList* drawList,
        ImVec2 rowMin,
        ImVec2 rowMax,
        float contentX,
        int depth,
        float indent,
        ImU32 color) {
    if (depth <= 0) {
        return;
    }

    const float rowCenterY = (rowMin.y + rowMax.y) * 0.5f;
    for (int level = 0; level < depth; ++level) {
        const float lineX = rowMin.x - static_cast<float>(depth - level) * indent + indent * 0.5f;
        drawList->AddLine({lineX, rowMin.y}, {lineX, rowMax.y}, color, 1.0f);
    }
    const float branchX = rowMin.x - indent * 0.5f;
    drawList->AddLine({branchX, rowCenterY}, {contentX - 3.0f, rowCenterY}, color, 1.0f);
}

} // namespace

float OutlinerPanel::minimumWidth(const UIContext& context) const {
    // UIDockPanel applies setNextWindowMinWidth(context.style.sidebarMinWidth) before opening the window.
    return context.style.sidebarMinWidth;
}

void OutlinerPanel::drawContent(UIContext& context) {
    SceneDocument& document = context.document;

    const std::vector<EntityRecord> records = document.entityRecordsTreeOrder();
    std::unordered_map<EntityId, EntityRecord> recordByEntity;
    recordByEntity.reserve(records.size());
    for (const EntityRecord& record : records) {
        recordByEntity.emplace(record.id, record);
    }

    const std::vector<EntityId> selected = document.selectedEntities();
    const ImGuiIO& io = ImGui::GetIO();
    bool selectionChanged = false;
    PendingOutlinerAction pendingAction = PendingOutlinerAction::None;
    EntityId pendingTarget = InvalidEntityId;
    constexpr float kOutlinerRowLeftPadding = 3.0f;
    constexpr float kOutlinerTreeIndent = 14.0f;
    constexpr float kOutlinerIconSize = 16.0f;
    constexpr float kOutlinerStatusIconSize = 14.0f;
    constexpr float kOutlinerIconTextGap = 4.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, kOutlinerTreeIndent);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{kOutlinerRowLeftPadding, style.FramePadding.y});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{kOutlinerIconTextGap, style.ItemSpacing.y});

    auto handleSelection = [&](const EntityRecord& record) {
        EntityId activeSelection = record.id;
        std::vector<EntityId> nextSelection;
        if (io.KeyShift) {
            nextSelection = entityRangeSelection(records, outlinerAnchor_, record.id);
        } else if (io.KeyCtrl || io.KeySuper) {
            nextSelection = selected;
            if (containsEntity(nextSelection, record.id)) {
                nextSelection.erase(std::remove(nextSelection.begin(), nextSelection.end(), record.id), nextSelection.end());
                activeSelection = document.primarySelection() == record.id ? InvalidEntityId : document.primarySelection();
            } else {
                nextSelection.push_back(record.id);
            }
        } else {
            nextSelection = {record.id};
        }
        outlinerAnchor_ = record.id;
        const QuaderApiVoidResult result =
                context.editor.scene().select(std::span<const EntityId>(nextSelection.data(), nextSelection.size()), activeSelection);
        selectionChanged = selectionChanged || (result.ok && result.changed);
    };

    std::function<void(EntityId)> drawNode = [&](EntityId entity) {
        const auto recordIt = recordByEntity.find(entity);
        if (recordIt == recordByEntity.end()) {
            return;
        }
        const EntityRecord& record = recordIt->second;
        ImGui::PushID(static_cast<int>(record.id));

        const bool subdued = !record.effectivelyVisible || record.effectivelyLocked;
        if (subdued) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        }

        ImGuiTreeNodeFlags treeFlags =
                ImGuiTreeNodeFlags_OpenOnArrow |
                ImGuiTreeNodeFlags_OpenOnDoubleClick |
                ImGuiTreeNodeFlags_SpanAvailWidth;
        if (record.selected) {
            treeFlags |= ImGuiTreeNodeFlags_Selected;
        }
        if (!record.hasChildren) {
            treeFlags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        }

        const bool open = ImGui::TreeNodeEx("##TreeNode", treeFlags);
        const ImVec2 rowMin = ImGui::GetItemRectMin();
        const ImVec2 rowMax = ImGui::GetItemRectMax();
        const bool rowLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
        const bool rowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ImGui::OpenPopupOnItemClick("EntityContext", ImGuiPopupFlags_MouseButtonRight);
        if (rowLeftClicked) {
            handleSelection(record);
        }

        const ImTextureID icon = iconFor(context.icons, record.kind);
        const float rowHeight = std::max(rowMax.y - rowMin.y, ImGui::GetTextLineHeight());
        const float contentX = rowMin.x + ImGui::GetTreeNodeToLabelSpacing();
        float textX = contentX;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        const ImU32 guideColor = ImGui::GetColorU32(ImGuiCol_Border, 0.75f);
        drawHierarchyGuides(drawList, rowMin, rowMax, contentX, record.depth, kOutlinerTreeIndent, guideColor);
        if (icon != ImTextureID_Invalid) {
            const ImVec2 iconMin{
                    contentX,
                    rowMin.y + std::max((rowHeight - kOutlinerIconSize) * 0.5f, 0.0f)};
            const ImVec2 iconMax{iconMin.x + kOutlinerIconSize, iconMin.y + kOutlinerIconSize};
            drawList->AddImage(icon, iconMin, iconMax);
            textX += kOutlinerIconSize + kOutlinerIconTextGap;
        }
        const ImVec2 textPos{
                textX,
                rowMin.y + std::max((rowHeight - ImGui::GetTextLineHeight()) * 0.5f, 0.0f)};
        float statusRight = rowMax.x - kOutlinerRowLeftPadding;
        auto drawStatusIcon = [&](ImTextureID statusIcon) {
            if (statusIcon == ImTextureID_Invalid) {
                return;
            }
            const ImVec2 iconMax{
                    statusRight,
                    rowMin.y + std::max((rowHeight + kOutlinerStatusIconSize) * 0.5f, kOutlinerStatusIconSize)};
            const ImVec2 iconMin{
                    iconMax.x - kOutlinerStatusIconSize,
                    iconMax.y - kOutlinerStatusIconSize};
            drawList->AddImage(statusIcon, iconMin, iconMax);
            statusRight = iconMin.x - kOutlinerIconTextGap;
        };
        if (!record.effectivelyVisible) {
            drawStatusIcon(context.icons.hide);
        }
        if (record.effectivelyLocked) {
            drawStatusIcon(context.icons.lock);
        }
        const ImVec4 textClip{
                textX,
                rowMin.y,
                std::max(textX, statusRight - kOutlinerIconTextGap),
                rowMax.y};
        drawOutlinerText(
                drawList,
                textPos,
                ImGui::GetColorU32(ImGuiCol_Text),
                record.name,
                subdued,
                textClip);

        if (rowRightClicked) {
            if (!record.selected) {
                const std::array<EntityId, 1> selection{record.id};
                const QuaderApiVoidResult result =
                        context.editor.scene().select(std::span<const EntityId>(selection.data(), selection.size()), record.id);
                selectionChanged = selectionChanged || (result.ok && result.changed);
            }
        }
        if (ImGui::BeginPopup("EntityContext")) {
            const std::string deleteLabel = context.localization.imguiLabel("ui.outliner.delete", "ui.outliner.delete", "Delete");
            if (ImGui::MenuItem(deleteLabel.c_str())) {
                pendingAction = PendingOutlinerAction::Delete;
                pendingTarget = record.id;
            }
            const std::string duplicateLabel = context.localization.imguiLabel("ui.outliner.duplicate", "ui.outliner.duplicate", "Duplicate");
            if (ImGui::MenuItem(duplicateLabel.c_str())) {
                pendingAction = PendingOutlinerAction::Duplicate;
                pendingTarget = record.id;
            }
            const std::string groupLabel = context.localization.imguiLabel("ui.outliner.group", "ui.outliner.group", "Group");
            if (ImGui::MenuItem(groupLabel.c_str())) {
                pendingAction = PendingOutlinerAction::Group;
                pendingTarget = record.id;
            }
            const bool canUngroup = record.kind == EntityKind::Empty && record.hasChildren;
            const std::string ungroupLabel = context.localization.imguiLabel("ui.outliner.ungroup", "ui.outliner.ungroup", "Ungroup");
            if (ImGui::MenuItem(ungroupLabel.c_str(), nullptr, false, canUngroup)) {
                pendingAction = PendingOutlinerAction::Ungroup;
                pendingTarget = record.id;
            }
            ImGui::EndPopup();
        }

        if (subdued) {
            ImGui::PopStyleColor();
        }

        if (open && record.hasChildren) {
            for (const EntityId child : document.children(record.id)) {
                drawNode(child);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    };

    for (const EntityId root : document.rootEntities()) {
        drawNode(root);
    }

    ImGui::PopStyleVar(3);

    if (selectionChanged) {
        syncSelectionToModeling(context);
    }

    if (!io.WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_G, false) &&
            (io.KeyCtrl || io.KeySuper)) {
        pendingAction = io.KeyShift ? PendingOutlinerAction::UngroupSelection : PendingOutlinerAction::Group;
        pendingTarget = document.primarySelection();
    }

    switch (pendingAction) {
    case PendingOutlinerAction::Delete:
        deleteSelection(context);
        break;
    case PendingOutlinerAction::Duplicate:
        duplicateSelection(context);
        break;
    case PendingOutlinerAction::Group:
        groupSelection(context);
        break;
    case PendingOutlinerAction::Ungroup:
        ungroupEntity(context, pendingTarget);
        break;
    case PendingOutlinerAction::UngroupSelection:
        ungroupSelection(context);
        break;
    case PendingOutlinerAction::None:
        break;
    }
}

} // namespace quader
