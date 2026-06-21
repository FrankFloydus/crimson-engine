#include "quader/ui/OutlinerPanel.h"

#include "quader/modeling/ModelingSessionService.h"
#include "quader/scene/SceneBounds.h"
#include "quader/scene/SceneDocument.h"
#include "quader/scene/SceneEntityNaming.h"
#include "quader/ui/UIContext.h"
#include "quader/ui/UILayout.h"
#include "quader/ui/UIStyleTokens.h"

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
    context.document.clearSelection();
    for (const EntityId entity : desiredSelection) {
        context.document.setSelected(entity, true);
    }
    if (activeSelection != InvalidEntityId) {
        context.document.setActiveSelection(activeSelection);
    }
    context.modeling.syncObjectSelectionFromScene();
}

void selectEntityRange(SceneDocument& document,
        const std::vector<EntityRecord>& records,
        EntityId anchor,
        EntityId clicked) {
    document.clearSelection();
    if (anchor == InvalidEntityId || anchor == clicked) {
        document.setSelected(clicked, true);
        document.setActiveSelection(clicked);
        return;
    }

    bool selecting = false;
    for (const EntityRecord& record : records) {
        const bool endpoint = record.id == anchor || record.id == clicked;
        if (endpoint && !selecting) {
            selecting = true;
            document.setSelected(record.id, true);
            continue;
        }

        if (selecting) {
            document.setSelected(record.id, true);
        }

        if (endpoint) {
            document.setActiveSelection(clicked);
            return;
        }
    }

    document.clearSelection();
    document.setSelected(clicked, true);
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

void selectOnly(SceneDocument& document, const std::vector<EntityId>& entities) {
    document.clearSelection();
    EntityId active = InvalidEntityId;
    for (const EntityId entity : entities) {
        if (document.exists(entity)) {
            document.setSelected(entity, true);
            active = entity;
        }
    }
    if (active != InvalidEntityId) {
        document.setActiveSelection(active);
    }
}

EntityId commonParent(const SceneDocument& document, const std::vector<EntityId>& entities) {
    if (entities.empty()) {
        return InvalidEntityId;
    }
    const EntityId parent = document.parent(entities.front());
    for (const EntityId entity : entities) {
        if (document.parent(entity) != parent) {
            return InvalidEntityId;
        }
    }
    return parent;
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

    const EntityId group = document.createEmpty(SceneEntityNaming::nextNameForKind(document, SceneEntityKind::Empty));
    document.setTransform(group, groupTransform);
    const EntityId parent = commonParent(document, roots);
    if (parent != InvalidEntityId) {
        document.setParent(group, parent, true);
    }
    for (const EntityId child : roots) {
        document.setParent(child, group, true);
    }

    selectOnly(document, {group});
    syncSelectionToModeling(context);
}

void ungroupEntity(UIContext& context, EntityId group) {
    SceneDocument& document = context.document;
    if (!document.exists(group) || document.kind(group) != EntityKind::Empty || !document.hasChildren(group)) {
        return;
    }

    const EntityId parent = document.parent(group);
    const std::vector<EntityId> children = document.children(group);
    for (const EntityId child : children) {
        document.setParent(child, parent, true);
    }
    document.destroyEntity(group);

    selectOnly(document, children);
    syncSelectionToModeling(context);
}

void ungroupSelection(UIContext& context) {
    const std::vector<EntityId> roots = selectedRootEntities(context.document);
    for (const EntityId entity : roots) {
        if (context.document.exists(entity) &&
                context.document.kind(entity) == EntityKind::Empty &&
                context.document.hasChildren(entity)) {
            ungroupEntity(context, entity);
        }
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
        selectOnly(document, modelingEntities);
        syncSelectionToModeling(context);
        (void)context.modeling.deleteSelection();
    }

    for (const EntityId root : selectedRootEntities(document)) {
        if (document.exists(root)) {
            document.destroyEntity(root);
        }
    }
    for (const EntityId entity : hierarchyEntities) {
        if (document.exists(entity) && document.modelingObject(entity) == nullptr) {
            document.destroyEntity(entity);
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
    selectOnly(context.document, sourceMeshes);
    syncSelectionToModeling(context);
    const ModelingCommandResult duplicateResult =
            context.modeling.executeInstantOperation(ModelingInstantOperationId::DuplicateMeshes);
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

void copyCommonEntityState(SceneDocument& document, EntityId source, EntityId clone, bool renameClone = true) {
    if (renameClone) {
        const NameComponent* name = document.name(source);
        if (name != nullptr) {
            document.renameEntity(clone, SceneEntityNaming::nextName(document, name->name));
        }
    }
    if (const TransformComponent* transform = document.transform(source)) {
        document.setTransform(clone, *transform);
    }
    if (const VisibilityComponent* visibility = document.visibility(source)) {
        document.setVisible(clone, visibility->visible);
    }
    if (const LockComponent* lock = document.lock(source)) {
        document.setLocked(clone, lock->locked);
    }
    document.setEntityKind(clone, document.kind(source));
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
            copyCommonEntityState(document, source, clone);
        } else {
            const NameComponent* name = document.name(source);
            const std::string baseName = name ? name->name : "Entity";
            clone = document.createEntity(SceneEntityNaming::nextName(document, baseName), document.kind(source));
            copyCommonEntityState(document, source, clone, false);
        }

        for (const EntityId child : document.children(source)) {
            const EntityId childClone = cloneEntity(child);
            if (childClone != InvalidEntityId) {
                document.setParent(childClone, clone, false);
            }
        }
        return clone;
    };

    std::vector<EntityId> rootClones;
    for (const EntityId root : roots) {
        const EntityId clone = cloneEntity(root);
        if (clone != InvalidEntityId) {
            document.setParent(clone, document.parent(root), false);
            rootClones.push_back(clone);
        }
    }

    selectOnly(document, rootClones);
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

void OutlinerPanel::draw(UIContext& context) {
    SceneDocument& document = context.document;
    setNextWindowMinWidth(context.style.sidebarMinWidth);
    if (!ImGui::Begin(title(), nullptr, context.style.panelWindowFlags)) {
        ImGui::End();
        return;
    }
    if (ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows) && panelFocusInputActive()) {
        context.requestPanelFocus(id());
    }

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

    auto handleSelection = [&](const EntityRecord& record) {
        if (io.KeyShift) {
            selectEntityRange(document, records, outlinerAnchor_, record.id);
        } else if (io.KeyCtrl || io.KeySuper) {
            document.setSelected(record.id, !containsEntity(selected, record.id));
        } else {
            document.clearSelection();
            document.setSelected(record.id, true);
        }
        outlinerAnchor_ = record.id;
        selectionChanged = true;
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
        const bool rowLeftClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left) && !ImGui::IsItemToggledOpen();
        const bool rowRightClicked = ImGui::IsItemClicked(ImGuiMouseButton_Right);
        ImGui::OpenPopupOnItemClick("EntityContext", ImGuiPopupFlags_MouseButtonRight);
        if (rowLeftClicked) {
            handleSelection(record);
        }

        const ImTextureID icon = iconFor(context.icons, record.kind);
        ImGui::SameLine();
        if (icon != ImTextureID_Invalid) {
            ImGui::Image(icon, {16.0f, 16.0f});
            ImGui::SameLine();
        }
        ImGui::TextUnformatted(record.name.c_str());

        if (rowRightClicked) {
            if (!record.selected) {
                document.clearSelection();
                document.setSelected(record.id, true);
                selectionChanged = true;
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

    ImGui::End();
}

} // namespace quader
