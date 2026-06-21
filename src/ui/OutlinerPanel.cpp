#include "ui/OutlinerPanel.h"

#include "document/DocumentCommandService.h"
#include "modeling/ModelingSessionService.h"
#include "modeling/tools/ModelingCommandToolCatalog.h"
#include "scene/SceneBounds.h"
#include "scene/SceneDocument.h"
#include "scene/SceneEntityNaming.h"
#include "ui/UIContext.h"
#include "ui/UIStyleTokens.h"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <optional>
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
    (void)context.modeling.setSelectionMode(ModelingSelectionMode::Object);
    (void)context.documentCommands.selectEntities(desiredSelection, activeSelection);
    context.modeling.syncObjectSelectionFromScene();
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
    DocumentCommandResult result = context.documentCommands.selectEntities(entities, active);
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

    EntityId group = InvalidEntityId;
    DocumentCommandResult result = context.documentCommands.groupSelection({}, groupTransform, &group);
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
    DocumentCommandResult result = context.documentCommands.ungroupSelection();
    if (result.ok && result.changed) {
        syncSelectionToModeling(context);
    }
}

void ungroupSelection(UIContext& context) {
    DocumentCommandResult result = context.documentCommands.ungroupSelection();
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
        if (const CommandTool* deleteTool = findModelingCommandTool("delete_selection")) {
            (void)deleteTool->execute(context.modeling);
        } else {
            (void)context.modeling.deleteSelection();
        }
    }

    for (const EntityId root : selectedRootEntities(document)) {
        if (document.exists(root)) {
            (void)context.documentCommands.destroyEntity(root);
        }
    }
    for (const EntityId entity : hierarchyEntities) {
        if (document.exists(entity) && document.modelingObject(entity) == nullptr) {
            (void)context.documentCommands.destroyEntity(entity);
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
    const CommandTool* duplicateTool = findModelingCommandTool("duplicate_meshes");
    const ModelingCommandResult duplicateResult = duplicateTool != nullptr
            ? duplicateTool->execute(context.modeling)
            : ModelingCommandResult{.ok = false, .message = "Duplicate command tool is not registered."};
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
            (void)context.documentCommands.renameEntity(clone, SceneEntityNaming::nextName(document, name->name));
        }
    }
    if (const TransformComponent* transform = document.transform(source)) {
        (void)context.documentCommands.applyEntityTransform(clone, *transform);
    }
    if (const VisibilityComponent* visibility = document.visibility(source)) {
        (void)context.documentCommands.setVisible(clone, visibility->visible);
    }
    if (const LockComponent* lock = document.lock(source)) {
        (void)context.documentCommands.setLocked(clone, lock->locked);
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
            const DocumentCommandResult created = context.documentCommands.createEntity(
                    SceneEntityNaming::nextName(document, baseName),
                    document.kind(source),
                    InvalidEntityId,
                    transform,
                    &clone);
            if (!created.ok || !created.changed) {
                return InvalidEntityId;
            }
            copyCommonEntityState(context, source, clone, false);
        }

        for (const EntityId child : document.children(source)) {
            const EntityId childClone = cloneEntity(child);
            if (childClone != InvalidEntityId) {
                (void)context.documentCommands.reparentEntity(childClone, clone, false);
            }
        }
        return clone;
    };

    std::vector<EntityId> rootClones;
    for (const EntityId root : roots) {
        const EntityId clone = cloneEntity(root);
        if (clone != InvalidEntityId) {
            (void)context.documentCommands.reparentEntity(clone, document.parent(root), false);
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
        const DocumentCommandResult result = context.documentCommands.selectEntities(nextSelection, activeSelection);
        selectionChanged = selectionChanged || (result.ok && result.changed);
    };

    std::function<void(EntityId)> drawNode = [&](EntityId entity) {
        const auto recordIt = recordByEntity.find(entity);
        if (recordIt == recordByEntity.end()) {
            return;
        }
        const EntityRecord& record = recordIt->second;
        ImGui::PushID(static_cast<int>(record.id));

        if (!record.effectivelyVisible) {
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
        const float contentX = rowMin.x +
                (record.hasChildren ? ImGui::GetTreeNodeToLabelSpacing() : kOutlinerRowLeftPadding);
        float textX = contentX;
        ImDrawList* drawList = ImGui::GetWindowDrawList();
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
        drawList->AddText(textPos, ImGui::GetColorU32(ImGuiCol_Text), record.name.c_str());

        if (rowRightClicked) {
            if (!record.selected) {
                const DocumentCommandResult result = context.documentCommands.selectEntities({record.id}, record.id);
                selectionChanged = selectionChanged || (result.ok && result.changed);
            }
        }
        if (ImGui::BeginPopup("EntityContext")) {
            if (ImGui::MenuItem("Delete")) {
                pendingAction = PendingOutlinerAction::Delete;
                pendingTarget = record.id;
            }
            if (ImGui::MenuItem("Duplicate")) {
                pendingAction = PendingOutlinerAction::Duplicate;
                pendingTarget = record.id;
            }
            if (ImGui::MenuItem("Group")) {
                pendingAction = PendingOutlinerAction::Group;
                pendingTarget = record.id;
            }
            const bool canUngroup = record.kind == EntityKind::Empty && record.hasChildren;
            if (ImGui::MenuItem("Ungroup", nullptr, false, canUngroup)) {
                pendingAction = PendingOutlinerAction::Ungroup;
                pendingTarget = record.id;
            }
            ImGui::EndPopup();
        }

        if (!record.effectivelyVisible) {
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
