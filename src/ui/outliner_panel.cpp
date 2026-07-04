////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/outliner_panel.hpp"

#include "api/quader_editor_api.hpp"
#include "core/localization_service.hpp"
#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "scene/scene_document.hpp"
#include "ui/ui_context.hpp"
#include "ui/ui_style_tokens.hpp"

#include <imgui.h>

#include <algorithm>
#include <functional>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace quader {
namespace {

/**
 * Enumerates PendingOutlinerAction values used by the ui layer.
 */
enum class PendingOutlinerAction {
    None,
    Delete,
    Duplicate,
    Group,
    Ungroup,
    UngroupSelection,
};

bool contains_entity(const std::vector<EntityId> &ids, EntityId id) {
  return std::find(ids.begin(), ids.end(), id) != ids.end();
}

std::vector<EntityId>
entity_range_selection(const std::vector<EntityRecord> &records,
                       EntityId anchor, EntityId clicked) {
  if (anchor == kInvalidEntityId || anchor == clicked) {
    return {clicked};
  }

  std::vector<EntityId> selection;
  bool selecting = false;
  for (const EntityRecord &record : records) {
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

bool select_only(UIContext &context, const std::vector<EntityId> &entities,
                 EntityId active = kInvalidEntityId) {
  QuaderApiVoidResult result = context.intent.editor.selection().select_objects(
      std::span<const EntityId>(entities.data(), entities.size()), active);
  return result.ok && result.changed;
}

bool select_current_scene_selection(UIContext &context) {
  const SceneDocument &document = context.scene.document;
  const std::vector<EntityId> selected = document.selected_entities();
  return select_only(context, selected, document.primary_selection());
}

void group_selection(UIContext &context) {
  const SceneDocument &document = context.scene.document;
  const std::vector<EntityId> roots =
      document_command_selected_root_entities(document);
  if (roots.empty()) {
    return;
  }

  const QuaderApiVoidResult result =
      context.intent.editor.commands().execute(quader_command::kGroupSelection);
  if (result.ok && result.changed) {
    (void)select_current_scene_selection(context);
  }
}

void ungroup_entity(UIContext &context, EntityId group) {
  const SceneDocument &document = context.scene.document;
  if (!document.exists(group) || document.kind(group) != EntityKind::Empty ||
      !document.has_children(group)) {
    return;
  }

  if (!contains_entity(document.selected_entities(), group)) {
    (void)select_only(context, {group}, group);
  }
  QuaderApiVoidResult result =
      context.intent.editor.commands().execute(quader_command::kUngroupSelection);
  if (result.ok && result.changed) {
    (void)select_current_scene_selection(context);
  }
}

void ungroup_selection(UIContext &context) {
  QuaderApiVoidResult result =
      context.intent.editor.commands().execute(quader_command::kUngroupSelection);
  if (result.ok && result.changed) {
    (void)select_current_scene_selection(context);
  }
}

void delete_selection(UIContext &context) {
  const SceneDocument &document = context.scene.document;
  const std::vector<EntityId> roots =
      document_command_selected_root_entities(document);
  if (roots.empty()) {
    return;
  }

  const QuaderSelectionMode previous_mode =
      context.intent.editor.selection().summary().mode;
  if (previous_mode != QuaderSelectionMode::Object &&
      previous_mode != QuaderSelectionMode::Group) {
    (void)context.intent.editor.selection().set_mode(QuaderSelectionMode::Object);
  }
  (void)context.intent.editor.commands().execute(quader_command::kDeleteSelection);
}

void duplicate_selection(UIContext &context) {
  (void)context.intent.editor.commands().execute(quader_command::kDuplicateSelection);
}

ImTextureID icon_for(const UIIconSet &icons, EntityKind kind) {
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

void draw_outliner_text(ImDrawList *draw_list, ImVec2 position, ImU32 color,
                        const std::string &text, bool italic,
                        const ImVec4 &clip_rect) {
  const int vertex_start = draw_list->VtxBuffer.Size;
  draw_list->AddText(ImGui::GetFont(), ImGui::GetFontSize(), position, color,
                     text.c_str(), text.c_str() + text.size(), 0.0f,
                     &clip_rect);
  if (!italic) {
    return;
  }

  const float baseline = position.y + ImGui::GetTextLineHeight();
  for (int index = vertex_start; index < draw_list->VtxBuffer.Size; ++index) {
    ImDrawVert &vertex = draw_list->VtxBuffer[index];
    vertex.pos.x += (baseline - vertex.pos.y) * 0.16f;
  }
}

void draw_hierarchy_guides(ImDrawList *draw_list, ImVec2 row_min,
                           ImVec2 row_max, float content_x, int depth,
                           float indent,
                           const std::vector<bool> &ancestor_has_next,
                           bool has_next_sibling, ImU32 color) {
  if (depth <= 0) {
    return;
  }

  const float row_center_y = (row_min.y + row_max.y) * 0.5f;
  for (std::size_t level = 0; level < ancestor_has_next.size(); ++level) {
    if (!ancestor_has_next[level]) {
      continue;
    }
    const float line_x =
        row_min.x -
        static_cast<float>(depth - static_cast<int>(level)) * indent +
        indent * 0.5f;
    draw_list->AddLine({line_x, row_min.y - 1.0f},
                       {line_x, row_max.y + 1.0f}, color, 1.0f);
  }
  const float branch_x = row_min.x - indent * 0.5f;
  const float branch_bottom = has_next_sibling ? row_max.y + 1.0f
                                               : row_center_y;
  draw_list->AddLine({branch_x, row_min.y - 1.0f},
                     {branch_x, branch_bottom}, color, 1.0f);
  draw_list->AddLine({branch_x, row_center_y}, {content_x - 3.0f, row_center_y},
                     color, 1.0f);
}

} // namespace

/**
 * Stores the flattened scene tree projection used to draw and interact with the outliner rows.
 */
struct OutlinerPanel::TreeCache {
    bool valid = false;
    std::uint64_t scene_revision = 0;
    std::vector<EntityRecord> records;
    std::unordered_map<EntityId, std::size_t> record_index_by_entity;
    std::unordered_map<EntityId, std::vector<EntityId>> children_by_entity;
    std::vector<EntityId> roots;
    std::vector<EntityId> selected;
    EntityId primary_selection = kInvalidEntityId;
};

OutlinerPanel::OutlinerPanel() = default;

OutlinerPanel::~OutlinerPanel() = default;

float OutlinerPanel::minimum_width(const UIContext& context) const {
    // UIDockPanel applies setNextWindowMinWidth(context.style.sidebarMinWidth) before opening the window.
    return context.style.sidebar_min_width;
}

ImVec2 OutlinerPanel::panel_window_padding(const UIContext& context) const {
  return {context.style.window_padding.x, context.style.window_padding.y};
}

ImGuiWindowFlags OutlinerPanel::window_flags(const UIContext& context) const {
  return context.style.panel_window_flags;
}

void OutlinerPanel::draw_content(UIContext& context) {
    const SceneDocument& document = context.scene.document;

    const TreeCache &tree = refresh_tree_cache(document);
    const ImGuiIO& io = ImGui::GetIO();
    PendingOutlinerAction pending_action = PendingOutlinerAction::None;
    EntityId pending_target = kInvalidEntityId;
    constexpr float kOutlinerRowLeftPadding = 3.0f;
    constexpr float kOutlinerTreeIndent = 14.0f;
    constexpr float kOutlinerIconSize = 16.0f;
    constexpr float kOutlinerStatusIconSize = 14.0f;
    constexpr float kOutlinerIconTextGap = 4.0f;
    const ImGuiStyle& style = ImGui::GetStyle();
    ImGui::PushStyleVar(ImGuiStyleVar_IndentSpacing, kOutlinerTreeIndent);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2{kOutlinerRowLeftPadding, style.FramePadding.y});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2{kOutlinerIconTextGap, style.ItemSpacing.y});

    auto handle_selection = [&](const EntityRecord &record) {
      EntityId active_selection = record.id;
      std::vector<EntityId> next_selection;
      if (io.KeyShift) {
        next_selection =
            entity_range_selection(tree.records, outliner_anchor_, record.id);
      } else if (io.KeyCtrl || io.KeySuper) {
        next_selection = tree.selected;
        if (contains_entity(next_selection, record.id)) {
          next_selection.erase(std::remove(next_selection.begin(),
                                           next_selection.end(), record.id),
                               next_selection.end());
          active_selection = tree.primary_selection == record.id
                                 ? kInvalidEntityId
                                 : tree.primary_selection;
        } else {
          next_selection.push_back(record.id);
        }
      } else {
        next_selection = {record.id};
      }
      outliner_anchor_ = record.id;
      (void)select_only(context, next_selection, active_selection);
    };

    std::function<void(EntityId, const std::vector<bool> &, bool)> draw_node =
        [&](EntityId entity, const std::vector<bool> &ancestor_has_next,
            bool has_next_sibling) {
      const auto record_it = tree.record_index_by_entity.find(entity);
      if (record_it == tree.record_index_by_entity.end() ||
          record_it->second >= tree.records.size()) {
        return;
      }
      const EntityRecord &record = tree.records[record_it->second];
      ImGui::PushID(static_cast<int>(record.id));

      const bool subdued =
          !record.effectively_visible || record.effectively_locked;
      if (subdued) {
        ImGui::PushStyleColor(ImGuiCol_Text,
                              ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      }

      ImGuiTreeNodeFlags tree_flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                      ImGuiTreeNodeFlags_OpenOnDoubleClick |
                                      ImGuiTreeNodeFlags_SpanAvailWidth;
      if (record.selected) {
        tree_flags |= ImGuiTreeNodeFlags_Selected;
      }
      if (!record.has_children) {
        tree_flags |=
            ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
      } else {
        tree_flags |= ImGuiTreeNodeFlags_DefaultOpen;
      }

      const bool open = ImGui::TreeNodeEx("##TreeNode", tree_flags);
      const ImVec2 row_min = ImGui::GetItemRectMin();
      const ImVec2 row_max = ImGui::GetItemRectMax();
      const bool row_visible = ImGui::IsRectVisible(row_min, row_max);
      const bool row_left_clicked =
          ImGui::IsItemClicked(ImGuiMouseButton_Left) &&
          !ImGui::IsItemToggledOpen();
      const bool row_right_clicked =
          ImGui::IsItemClicked(ImGuiMouseButton_Right);
      ImGui::OpenPopupOnItemClick("EntityContext",
                                  ImGuiPopupFlags_MouseButtonRight);
      if (row_left_clicked) {
        handle_selection(record);
      }

      if (row_visible) {
        const ImTextureID icon = icon_for(context.icons, record.kind);
        const float row_height =
            std::max(row_max.y - row_min.y, ImGui::GetTextLineHeight());
        const float content_x = row_min.x + ImGui::GetTreeNodeToLabelSpacing();
        float text_x = content_x;
        ImDrawList *draw_list = ImGui::GetWindowDrawList();
        const ImU32 guide_color = ImGui::GetColorU32(ImGuiCol_Border, 0.75f);
        draw_hierarchy_guides(draw_list, row_min, row_max, content_x,
                              record.depth, kOutlinerTreeIndent,
                              ancestor_has_next, has_next_sibling,
                              guide_color);
        if (icon != ImTextureID_Invalid) {
          const ImVec2 icon_min{
              content_x,
              row_min.y +
                  std::max((row_height - kOutlinerIconSize) * 0.5f, 0.0f)};
          const ImVec2 icon_max{icon_min.x + kOutlinerIconSize,
                                icon_min.y + kOutlinerIconSize};
          draw_list->AddImage(icon, icon_min, icon_max);
          text_x += kOutlinerIconSize + kOutlinerIconTextGap;
        }
        const ImVec2 text_pos{
            text_x,
            row_min.y +
                std::max((row_height - ImGui::GetTextLineHeight()) * 0.5f,
                         0.0f)};
        float status_right = row_max.x - kOutlinerRowLeftPadding;
        auto draw_status_icon = [&](ImTextureID status_icon) {
          if (status_icon == ImTextureID_Invalid) {
            return;
          }
          const ImVec2 icon_max{
              status_right,
              row_min.y +
                  std::max((row_height + kOutlinerStatusIconSize) * 0.5f,
                           kOutlinerStatusIconSize)};
          const ImVec2 icon_min{icon_max.x - kOutlinerStatusIconSize,
                                icon_max.y - kOutlinerStatusIconSize};
          draw_list->AddImage(status_icon, icon_min, icon_max);
          status_right = icon_min.x - kOutlinerIconTextGap;
        };
        if (!record.effectively_visible) {
          draw_status_icon(context.icons.hide);
        }
        if (record.effectively_locked) {
          draw_status_icon(context.icons.lock);
        }
        const ImVec4 text_clip{
            text_x, row_min.y,
            std::max(text_x, status_right - kOutlinerIconTextGap), row_max.y};
        draw_outliner_text(draw_list, text_pos,
                           ImGui::GetColorU32(ImGuiCol_Text), record.name,
                           subdued, text_clip);
      }

      if (row_right_clicked) {
        if (!record.selected) {
          (void)select_only(context, {record.id}, record.id);
        }
      }
      if (ImGui::BeginPopup("EntityContext")) {
        const bool can_mutate_hierarchy = !record.is_scene_root;
        const std::string delete_label = context.localization.imgui_label(
            "ui.outliner.delete", "ui.outliner.delete", "Delete");
        if (ImGui::MenuItem(delete_label.c_str(), nullptr, false,
                            can_mutate_hierarchy)) {
          pending_action = PendingOutlinerAction::Delete;
          pending_target = record.id;
        }
        const std::string duplicate_label = context.localization.imgui_label(
            "ui.outliner.duplicate", "ui.outliner.duplicate", "Duplicate");
        if (ImGui::MenuItem(duplicate_label.c_str(), nullptr, false,
                            can_mutate_hierarchy)) {
          pending_action = PendingOutlinerAction::Duplicate;
          pending_target = record.id;
        }
        const std::string group_label = context.localization.imgui_label(
            "ui.outliner.group", "ui.outliner.group", "Group");
        if (ImGui::MenuItem(group_label.c_str(), nullptr, false,
                            can_mutate_hierarchy)) {
          pending_action = PendingOutlinerAction::Group;
          pending_target = record.id;
        }
        const bool can_ungroup = can_mutate_hierarchy &&
                                 record.kind == EntityKind::Empty &&
                                 record.has_children;
        const std::string ungroup_label = context.localization.imgui_label(
            "ui.outliner.ungroup", "ui.outliner.ungroup", "Ungroup");
        if (ImGui::MenuItem(ungroup_label.c_str(), nullptr, false,
                            can_ungroup)) {
          pending_action = PendingOutlinerAction::Ungroup;
          pending_target = record.id;
        }
        ImGui::EndPopup();
      }

      if (subdued) {
        ImGui::PopStyleColor();
      }

      if (open && record.has_children) {
        if (const auto children_it = tree.children_by_entity.find(record.id);
            children_it != tree.children_by_entity.end()) {
          std::vector<bool> child_ancestors = ancestor_has_next;
          if (record.depth > 0) {
            child_ancestors.push_back(has_next_sibling);
          }
          const std::vector<EntityId> &children = children_it->second;
          for (std::size_t index = 0; index < children.size(); ++index) {
            draw_node(children[index], child_ancestors,
                      index + 1u < children.size());
          }
        }
        ImGui::TreePop();
      }
      ImGui::PopID();
    };

    const std::vector<bool> root_ancestors;
    for (std::size_t index = 0; index < tree.roots.size(); ++index) {
      draw_node(tree.roots[index], root_ancestors,
                index + 1u < tree.roots.size());
    }

    ImGui::PopStyleVar(3);

    switch (pending_action) {
    case PendingOutlinerAction::Delete:
      delete_selection(context);
      break;
    case PendingOutlinerAction::Duplicate:
      duplicate_selection(context);
      break;
    case PendingOutlinerAction::Group:
      group_selection(context);
      break;
    case PendingOutlinerAction::Ungroup:
      ungroup_entity(context, pending_target);
      break;
    case PendingOutlinerAction::UngroupSelection:
      ungroup_selection(context);
      break;
    case PendingOutlinerAction::None:
        break;
    }
}

const OutlinerPanel::TreeCache &
OutlinerPanel::refresh_tree_cache(const SceneDocument &document) {
  if (!tree_cache_) {
    tree_cache_ = std::make_unique<TreeCache>();
  }
  TreeCache &cache = *tree_cache_;
  if (cache.valid && cache.scene_revision == document.revision()) {
    return cache;
  }

  cache.scene_revision = document.revision();
  cache.records = document.entity_records_tree_order();
  cache.record_index_by_entity.clear();
  cache.record_index_by_entity.reserve(cache.records.size());
  cache.children_by_entity.clear();
  cache.children_by_entity.reserve(cache.records.size());
  for (std::size_t index = 0; index < cache.records.size(); ++index) {
    const EntityRecord &record = cache.records[index];
    cache.record_index_by_entity.emplace(record.id, index);
    if (record.parent != kInvalidEntityId) {
      cache.children_by_entity[record.parent].push_back(record.id);
    }
  }
  cache.roots = document.root_entities();
  cache.selected = document.selected_entities();
  cache.primary_selection = document.primary_selection();
  cache.valid = true;
  return cache;
}

} // namespace quader
