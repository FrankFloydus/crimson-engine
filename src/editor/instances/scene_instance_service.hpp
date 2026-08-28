////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/document_command_scene_helpers.hpp"
#include "editor/commands/document/scene_command.hpp"
#include "editor/commands/document_command_service.hpp"
#include "editor/history/modeling_memento_command.hpp"
#include "editor/instances/instance_edit_context.hpp"
#include "editor/modeling/services/modeling_session_service.hpp"
#include "editor/projection/mesh_projection_service.hpp"
#include "editor/selection/scene_selection_application.hpp"
#include "scene/scene_bounds.hpp"
#include "scene/scene_entity_naming.hpp"
#include "scene/scene_transform.hpp"
#include "scene/scene_texture_mapping.hpp"

#include <algorithm>
#include <exception>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace quader {

/**
 * Reports one instance-domain mutation.
 */
struct SceneInstanceMutationResult {
  bool ok = false;
  bool changed = false;
  std::string message;
  EntityId occurrence = kInvalidEntityId;
  EntityId definition = kInvalidEntityId;
  std::vector<EntityId> entities;

  /// Adapts the domain mutation status for command/history API boundaries.
  [[nodiscard]] CommandResult command_result() const {
    return {
        .ok = ok,
        .changed = changed,
        .message = message,
    };
  }
};

/**
 * Stores the scene-owned data needed by the public instance-edit breadcrumb.
 */
struct SceneInstanceEditState final {
  bool active = false;
  EntityId occurrence = kInvalidEntityId;
  EntityId definition = kInvalidEntityId;
  std::string scene_name;
  std::string instance_name;
};

namespace scene_instance_detail {

/// Stores one member's placement before and after an instance mutation.
struct MemberPlacement final {
  EntityId entity = kInvalidEntityId;
  DocumentEntityPlacement before;
  DocumentEntityPlacement after;
};

/// Identifies a mesh within a member hierarchy before its occurrence is removed.
struct FlattenTextureMapping final {
  std::size_t member_index = 0;
  std::size_t descendant_index = 0;
  QMat4 sampling_from_local;
};

inline CommandResult restore_placements(
    SceneDocument &scene, std::span<const MemberPlacement> placements,
    bool use_after) {
  for (const MemberPlacement &placement : placements) {
    const DocumentEntityPlacement &target =
        use_after ? placement.after : placement.before;
    const CommandResult result = document_command_apply_placement(scene, target);
    if (!result.ok) {
      return result;
    }
  }
  return command_changed_result();
}

inline std::optional<DocumentEntitySnapshot>
root_only_snapshot(const SceneDocument &scene, EntityId entity) {
  std::optional<DocumentEntitySnapshot> snapshot =
      document_command_capture_entity_snapshot(scene, entity);
  if (snapshot.has_value()) {
    snapshot->children.clear();
  }
  return snapshot;
}

inline EntityId restore_root(SceneDocument &scene,
                             const DocumentEntitySnapshot &snapshot) {
  const EntityId parent =
      snapshot.original_parent != kInvalidEntityId &&
              scene.exists(snapshot.original_parent)
          ? snapshot.original_parent
          : kInvalidEntityId;
  return document_command_create_entity_from_snapshot(
      scene, snapshot, parent, snapshot.name, true, true);
}

/// Creates one definition plus its first occurrence as one history item.
class CreateSceneInstanceCommand final : public SceneCommand {
public:
  CreateSceneInstanceCommand(SceneDocument &scene, std::string occurrence_name,
                             std::string definition_name,
                             EntityId occurrence_parent,
                             TransformComponent occurrence_transform,
                             std::vector<MemberPlacement> members)
      : SceneCommand(scene, "Create Instance"),
        occurrence_name_(std::move(occurrence_name)),
        definition_name_(std::move(definition_name)),
        occurrence_parent_(occurrence_parent),
        occurrence_transform_(occurrence_transform),
        members_(std::move(members)),
        before_selection_(document_command_capture_selection(scene)) {}

  [[nodiscard]] EntityId occurrence() const { return occurrence_; }
  [[nodiscard]] EntityId definition() const { return definition_; }

  CommandResult undo() override {
    if (occurrence_ == kInvalidEntityId || definition_ == kInvalidEntityId) {
      return command_invalid_result("Instance has not been created.");
    }
    if (scene().exists(occurrence_) && !scene().destroy_entity(occurrence_)) {
      return command_invalid_result("Instance occurrence could not be removed.");
    }
    const CommandResult restored = restore_placements(scene(), members_, false);
    if (!restored.ok) {
      if (occurrence_snapshot_.has_value()) {
        occurrence_ = restore_root(scene(), *occurrence_snapshot_);
      }
      return restored;
    }
    if (scene().exists(definition_) && !scene().destroy_entity(definition_)) {
      return command_invalid_result("Instance definition could not be removed.");
    }
    document_command_apply_selection(scene(), before_selection_);
    return command_changed_result();
  }

  CommandResult redo() override {
    const CommandResult roots = create_or_restore_roots();
    if (!roots.ok) {
      return roots;
    }

    for (MemberPlacement &member : members_) {
      member.after.parent = definition_;
    }

    std::size_t moved = 0;
    for (; moved < members_.size(); ++moved) {
      const CommandResult applied =
          document_command_apply_placement(scene(), members_[moved].after);
      if (!applied.ok) {
        break;
      }
    }
    if (moved != members_.size()) {
      if (scene().exists(occurrence_)) {
        (void)scene().destroy_entity(occurrence_);
      }
      for (std::size_t index = moved; index > 0; --index) {
        (void)document_command_apply_placement(scene(),
                                               members_[index - 1u].before);
      }
      if (scene().exists(definition_)) {
        (void)scene().destroy_entity(definition_);
      }
      document_command_apply_selection(scene(), before_selection_);
      return command_invalid_result(
          "Instance members could not be moved into the definition.");
    }

    document_command_select_only(scene(), {occurrence_});
    return command_changed_result();
  }

private:
  CommandResult create_or_restore_roots() {
    if (definition_snapshot_.has_value() && occurrence_snapshot_.has_value()) {
      definition_ = restore_root(scene(), *definition_snapshot_);
      if (definition_ == kInvalidEntityId) {
        return command_invalid_result("Instance definition could not be restored.");
      }
      occurrence_ = restore_root(scene(), *occurrence_snapshot_);
      if (occurrence_ == kInvalidEntityId) {
        (void)scene().destroy_entity(definition_);
        return command_invalid_result("Instance occurrence could not be restored.");
      }
      return command_changed_result();
    }

    definition_ =
        scene().create_entity(definition_name_, EntityKind::Empty);
    if (definition_ == kInvalidEntityId ||
        !scene().set_parent(definition_, scene().scene_root(), false) ||
        !scene().set_instance_definition(definition_)) {
      destroy_roots();
      return command_invalid_result("Instance definition could not be created.");
    }
    (void)scene().set_visible(definition_, false);
    (void)scene().set_selectable(definition_, false);

    occurrence_ =
        scene().create_entity(occurrence_name_, EntityKind::Instance);
    if (occurrence_ == kInvalidEntityId ||
        !scene().set_parent(occurrence_, occurrence_parent_, false)) {
      destroy_roots();
      return command_invalid_result("Instance occurrence could not be created.");
    }
    scene().set_transform(occurrence_, occurrence_transform_);
    if (!scene().set_instance_occurrence(
            occurrence_, InstanceOccurrenceComponent{.definition = definition_})) {
      destroy_roots();
      return command_invalid_result("Instance occurrence could not be created.");
    }

    definition_snapshot_ = root_only_snapshot(scene(), definition_);
    occurrence_snapshot_ = root_only_snapshot(scene(), occurrence_);
    if (!definition_snapshot_.has_value() ||
        !occurrence_snapshot_.has_value()) {
      destroy_roots();
      return command_invalid_result("Instance roots could not be captured.");
    }
    return command_changed_result();
  }

  void destroy_roots() {
    if (occurrence_ != kInvalidEntityId && scene().exists(occurrence_)) {
      (void)scene().destroy_entity(occurrence_);
    }
    if (definition_ != kInvalidEntityId && scene().exists(definition_)) {
      (void)scene().destroy_entity(definition_);
    }
  }

  std::string occurrence_name_;
  std::string definition_name_;
  EntityId occurrence_parent_ = kInvalidEntityId;
  TransformComponent occurrence_transform_;
  std::vector<MemberPlacement> members_;
  DocumentSelectionState before_selection_;
  EntityId occurrence_ = kInvalidEntityId;
  EntityId definition_ = kInvalidEntityId;
  std::optional<DocumentEntitySnapshot> occurrence_snapshot_;
  std::optional<DocumentEntitySnapshot> definition_snapshot_;
};

/// Flattens the final occurrence by returning its canonical members.
class FlattenLastSceneInstanceCommand final : public SceneCommand {
public:
  FlattenLastSceneInstanceCommand(SceneDocument &scene, EntityId occurrence,
                                  EntityId definition,
                                  std::vector<MemberPlacement> members)
      : SceneCommand(scene, "Flatten Instance"), occurrence_(occurrence),
        definition_(definition), members_(std::move(members)),
        before_selection_(document_command_capture_selection(scene)),
        occurrence_snapshot_(root_only_snapshot(scene, occurrence)),
        definition_snapshot_(root_only_snapshot(scene, definition)) {}

  CommandResult undo() override {
    if (!definition_snapshot_.has_value() ||
        !occurrence_snapshot_.has_value() ||
        restore_root(scene(), *definition_snapshot_) == kInvalidEntityId) {
      return command_invalid_result("Instance definition could not be restored.");
    }
    if (restore_root(scene(), *occurrence_snapshot_) == kInvalidEntityId) {
      (void)scene().destroy_entity(definition_);
      return command_invalid_result("Instance occurrence could not be restored.");
    }
    const CommandResult restored = restore_placements(scene(), members_, false);
    if (!restored.ok) {
      return restored;
    }
    document_command_apply_selection(scene(), before_selection_);
    return command_changed_result();
  }

  CommandResult redo() override {
    if (!scene().destroy_entity(occurrence_)) {
      return command_invalid_result("Instance occurrence could not be removed.");
    }
    const CommandResult placed = restore_placements(scene(), members_, true);
    if (!placed.ok) {
      (void)restore_placements(scene(), members_, false);
      if (occurrence_snapshot_.has_value()) {
        occurrence_ = restore_root(scene(), *occurrence_snapshot_);
      }
      return placed;
    }
    if (!scene().destroy_entity(definition_)) {
      (void)restore_placements(scene(), members_, false);
      if (occurrence_snapshot_.has_value()) {
        occurrence_ = restore_root(scene(), *occurrence_snapshot_);
      }
      return command_invalid_result("Instance definition could not be removed.");
    }
    std::vector<EntityId> flattened;
    flattened.reserve(members_.size());
    for (const MemberPlacement &member : members_) {
      flattened.push_back(member.entity);
    }
    document_command_select_only(scene(), flattened);
    return command_changed_result();
  }

private:
  EntityId occurrence_ = kInvalidEntityId;
  EntityId definition_ = kInvalidEntityId;
  std::vector<MemberPlacement> members_;
  DocumentSelectionState before_selection_;
  std::optional<DocumentEntitySnapshot> occurrence_snapshot_;
  std::optional<DocumentEntitySnapshot> definition_snapshot_;
};

/// Places independent modeling duplicates and removes one linked occurrence.
class FinalizeFlattenSceneInstanceCommand final : public SceneCommand {
public:
  FinalizeFlattenSceneInstanceCommand(SceneDocument &scene,
                                      EntityId occurrence,
                                      std::vector<MemberPlacement> duplicates)
      : SceneCommand(scene, "Flatten Instance"), occurrence_(occurrence),
        duplicates_(std::move(duplicates)),
        before_selection_(document_command_capture_selection(scene)),
        occurrence_snapshot_(root_only_snapshot(scene, occurrence)) {}

  CommandResult undo() override {
    if (!occurrence_snapshot_.has_value() ||
        restore_root(scene(), *occurrence_snapshot_) == kInvalidEntityId) {
      return command_invalid_result("Instance occurrence could not be restored.");
    }
    const CommandResult restored =
        restore_placements(scene(), duplicates_, false);
    if (!restored.ok) {
      return restored;
    }
    document_command_apply_selection(scene(), before_selection_);
    return command_changed_result();
  }

  CommandResult redo() override {
    const CommandResult placed = restore_placements(scene(), duplicates_, true);
    if (!placed.ok) {
      (void)restore_placements(scene(), duplicates_, false);
      return placed;
    }
    if (!scene().destroy_entity(occurrence_)) {
      (void)restore_placements(scene(), duplicates_, false);
      return command_invalid_result("Instance occurrence could not be removed.");
    }
    std::vector<EntityId> flattened;
    flattened.reserve(duplicates_.size());
    for (const MemberPlacement &duplicate : duplicates_) {
      flattened.push_back(duplicate.entity);
    }
    document_command_select_only(scene(), flattened);
    return command_changed_result();
  }

private:
  EntityId occurrence_ = kInvalidEntityId;
  std::vector<MemberPlacement> duplicates_;
  DocumentSelectionState before_selection_;
  std::optional<DocumentEntitySnapshot> occurrence_snapshot_;
};

} // namespace scene_instance_detail

/**
 * Owns scene-local definition/occurrence policy and transient edit scope.
 */
class SceneInstanceService final {
public:
  SceneInstanceService(SceneDocument &scene,
                       const AssetRegistry &assets,
                       DocumentCommandService &document_commands,
                       EditorHistoryService &history,
                       ModelingSessionService &modeling,
                       InstanceEditContext &edit_context,
                       const std::string &scene_name)
      : scene_(scene), assets_(assets), document_commands_(document_commands),
        history_(history), modeling_(modeling), edit_context_(edit_context),
        scene_name_(scene_name) {}

  [[nodiscard]] bool can_create_instance() const {
    return prepare_create().has_value();
  }

  [[nodiscard]] bool can_flatten_instance() const {
    return can_flatten_selected_instances();
  }

  [[nodiscard]] bool can_flatten_selected_instances() const {
    sanitize_edit_context();
    if (edit_context_.active() ||
        modeling_.selection_mode() != ModelingSelectionMode::Object ||
        modeling_.can_cancel_active_tool_or_preview()) {
      return false;
    }
    const std::optional<std::vector<EntityId>> occurrences =
        selected_occurrences();
    if (!occurrences.has_value()) {
      return false;
    }
    for (const EntityId occurrence : *occurrences) {
      const InstanceOccurrenceComponent *component =
          scene_.instance_occurrence(occurrence);
      if (component == nullptr) {
        return false;
      }
      const std::vector<EntityId> members =
          scene_.children(component->definition);
      if (members.empty()) {
        return false;
      }
      for (const EntityId member : members) {
        if (!flatten_placement(occurrence, member, member).has_value()) {
          return false;
        }
      }
    }
    return true;
  }

  [[nodiscard]] bool can_enter_instance_edit(EntityId occurrence) const {
    return !valid_edit_context() && valid_occurrence(occurrence) &&
           scene_.is_effectively_visible(occurrence) &&
           scene_entity_selectable_for_user_selection(scene_, occurrence) &&
           modeling_.selection_mode() == ModelingSelectionMode::Object &&
           !modeling_.can_cancel_active_tool_or_preview();
  }

  [[nodiscard]] bool can_exit_instance_edit() const {
    return valid_edit_context();
  }

  SceneInstanceMutationResult create_instance() {
    const std::optional<CreatePlan> plan = prepare_create();
    if (!plan.has_value()) {
      return invalid("Select one or more visible, unlocked, unnested meshes "
                     "or mesh groups with a common parent in Object mode.");
    }

    auto command =
        std::make_unique<scene_instance_detail::CreateSceneInstanceCommand>(
            scene_, plan->occurrence_name, plan->definition_name,
            plan->occurrence_parent, plan->occurrence_transform,
            plan->members);
    auto *created = command.get();
    const CommandResult status = history_.execute(std::move(command));
    if (!status.ok || !status.changed) {
      return from_status(status);
    }
    modeling_.project_object_selection_from_scene(true);
    return {
        .ok = true,
        .changed = true,
        .message = "Instance created.",
        .occurrence = created->occurrence(),
        .definition = created->definition(),
    };
  }

  SceneInstanceMutationResult flatten_instance() {
    return flatten_selected_instances();
  }

  SceneInstanceMutationResult flatten_selected_instances() {
    const std::optional<std::vector<EntityId>> selected =
        selected_occurrences();
    if (!selected.has_value() || !can_flatten_selected_instances()) {
      return invalid("Select one or more unlocked instances in Object mode "
                     "with no active preview or instance edit scope.");
    }

    EditorTransaction transaction =
        history_.begin_transaction("Flatten Instance");
    if (!transaction.active()) {
      return invalid("Cannot flatten while another editor transaction is active.");
    }
    const ModelingSessionMemento before_modeling =
        modeling_.capture_history_memento();
    const auto rollback = [&](CommandResult failure) {
      const CommandResult canceled = transaction.cancel();
      const ModelingCommandResult restored =
          modeling_.restore_history_memento(before_modeling);
      if (!canceled.ok) {
        return from_status(canceled);
      }
      if (!restored.ok) {
        return invalid(restored.message.empty()
                           ? "Modeling state could not be restored."
                           : restored.message);
      }
      return from_status(failure);
    };

    bool changed_modeling = false;
    bool definitions_are_common = true;
    EntityId common_definition = kInvalidEntityId;
    std::vector<EntityId> flattened_entities;
    for (const EntityId occurrence : *selected) {
      const InstanceOccurrenceComponent *component =
          scene_.instance_occurrence(occurrence);
      if (component == nullptr) {
        return rollback(command_invalid_result(
            "Selected entity is no longer an instance occurrence."));
      }
      const EntityId definition = component->definition;
      std::optional<std::string> occurrence_name;
      if (const NameComponent *name = scene_.name(occurrence)) {
        occurrence_name = name->name;
      }
      if (common_definition == kInvalidEntityId) {
        common_definition = definition;
      } else if (common_definition != definition) {
        definitions_are_common = false;
      }

      const std::vector<EntityId> remaining =
          scene_.instance_occurrences_for_definition(definition);
      const auto texture_mappings =
          capture_flatten_texture_mappings(occurrence, definition);
      if (!texture_mappings.has_value()) {
        return rollback(command_invalid_result(
            "Instance texture mapping could not be resolved."));
      }
      const bool adopts_canonical_members = remaining.size() == 1u;
      SceneInstanceMutationResult flattened =
          adopts_canonical_members
              ? flatten_last_occurrence(occurrence, definition)
              : flatten_linked_occurrence_in_transaction(occurrence,
                                                         definition);
      if (!flattened.ok || !flattened.changed) {
        return rollback(CommandResult{.ok = flattened.ok,
                                      .changed = flattened.changed,
                                      .message = flattened.message});
      }
      const ModelingCommandResult baked =
          bake_flatten_texture_mappings(flattened.entities, *texture_mappings);
      if (!baked.ok) {
        return rollback(modeling_memento_command_result(baked));
      }
      if (occurrence_name.has_value() && flattened.entities.size() == 1u) {
        const CommandResult renamed = document_commands_.rename_entity(
            flattened.entities.front(), *occurrence_name);
        if (!renamed.ok) {
          return rollback(renamed);
        }
      }
      changed_modeling = changed_modeling || !adopts_canonical_members ||
                         baked.changed;
      flattened_entities.insert(flattened_entities.end(),
                                 flattened.entities.begin(),
                                flattened.entities.end());
    }

    if (flattened_entities.empty()) {
      return rollback(command_invalid_result(
          "Flatten did not produce any ordinary scene entities."));
    }
    const CommandResult selected_outputs = document_commands_.select_entities(
        flattened_entities, flattened_entities.front(),
        SceneSelectionApplicationMode::UndoableCommand);
    if (!selected_outputs.ok) {
      return rollback(selected_outputs);
    }
    modeling_.project_object_selection_from_scene(true);

    if (changed_modeling) {
      const CommandResult recorded =
          transaction.record_executed_before_existing(
              std::make_unique<RecordedModelingMementoCommand>(
                  modeling_, "Flatten Instance", before_modeling,
                  modeling_.capture_history_memento(), true));
      if (!recorded.ok) {
        return rollback(recorded);
      }
    }

    const CommandResult committed = transaction.commit();
    if (!committed.ok || !committed.changed) {
      return from_status(committed);
    }
    return {
        .ok = true,
        .changed = true,
        .message = selected->size() == 1u ? "Instance flattened."
                                         : "Instances flattened.",
        .definition = definitions_are_common ? common_definition
                                             : kInvalidEntityId,
        .entities = std::move(flattened_entities),
    };
  }

  SceneInstanceMutationResult enter_instance_edit(EntityId occurrence) {
    sanitize_edit_context();
    if (!can_enter_instance_edit(occurrence)) {
      return invalid("Instance edit requires one valid, unlocked occurrence "
                     "in Object mode with no active preview.");
    }
    const InstanceOccurrenceComponent *component =
        scene_.instance_occurrence(occurrence);
    edit_context_.set(occurrence, component->definition);
    (void)apply_scene_selection(
        scene_, SceneSelectionState{}, SceneSelectionApplicationMode::RuntimeSync);
    modeling_.project_object_selection_from_scene(true);
    return {
        .ok = true,
        .changed = true,
        .message = "Instance edit entered.",
        .occurrence = occurrence,
        .definition = component->definition,
    };
  }

  SceneInstanceMutationResult exit_instance_edit() {
    sanitize_edit_context();
    if (!edit_context_.active()) {
      return invalid("No instance edit scope is active.");
    }
    const ModelingCommandResult cleared = modeling_.clear_selection();
    if (!cleared.ok) {
      return invalid(cleared.message.empty()
                         ? "Instance component selection could not be cleared."
                         : cleared.message);
    }
    const ModelingCommandResult object_mode =
        modeling_.set_selection_mode(ModelingSelectionMode::Object);
    if (!object_mode.ok) {
      return invalid(object_mode.message.empty()
                         ? "Instance edit could not return to Object mode."
                         : object_mode.message);
    }
    const EntityId occurrence = edit_context_.occurrence();
    const EntityId definition = edit_context_.definition();
    edit_context_.clear();
    (void)apply_scene_selection(
        scene_, make_scene_selection(scene_, std::span<const EntityId>{&occurrence,
                                                                       1u},
                                     occurrence),
        SceneSelectionApplicationMode::RuntimeSync);
    modeling_.project_object_selection_from_scene(true);
    return {
        .ok = true,
        .changed = true,
        .message = "Instance edit exited.",
        .occurrence = occurrence,
        .definition = definition,
    };
  }

  /** Returns the sanitized scope without allocating presentation names. */
  [[nodiscard]] InstanceEditContext instance_edit_scope() const {
    sanitize_edit_context();
    return edit_context_;
  }

  [[nodiscard]] SceneInstanceEditState instance_edit_state() const {
    sanitize_edit_context();
    SceneInstanceEditState state;
    state.active = edit_context_.active();
    state.occurrence = edit_context_.occurrence();
    state.definition = edit_context_.definition();
    state.scene_name = scene_name_;
    if (state.active) {
      if (const NameComponent *name = scene_.name(state.occurrence)) {
        state.instance_name = name->name;
      }
    }
    return state;
  }

  [[nodiscard]] std::uint64_t instance_edit_revision() const {
    sanitize_edit_context();
    return edit_context_.revision();
  }

  [[nodiscard]] bool is_active_definition(EntityId definition) const {
    sanitize_edit_context();
    return edit_context_.active() && edit_context_.definition() == definition;
  }

  [[nodiscard]] bool is_in_active_definition_scope(EntityId entity) const {
    sanitize_edit_context();
    return edit_context_.active() &&
           scene_.containing_instance_definition(entity) ==
               edit_context_.definition() &&
           entity != edit_context_.definition();
  }

  [[nodiscard]] bool is_selectable_in_current_scope(EntityId entity) const {
    sanitize_edit_context();
    if (edit_context_.active()) {
      return modeling_.selection_mode() == ModelingSelectionMode::Object &&
             is_in_active_definition_scope(entity) &&
             !scene_.is_effectively_locked(entity) &&
             (scene_.visibility(entity) == nullptr ||
              scene_.visibility(entity)->selectable);
    }
    if (scene_.instance_occurrence(entity) != nullptr &&
        modeling_.selection_mode() != ModelingSelectionMode::Object) {
      return false;
    }
    return !scene_.is_instance_definition_entity(entity) &&
           !scene_.is_instance_definition_member(entity) &&
           scene_entity_selectable_for_user_selection(scene_, entity);
  }

  [[nodiscard]] EntityId resolve_selection_target(
      EntityId hit_entity,
      EntityId rendered_occurrence = kInvalidEntityId) const {
    sanitize_edit_context();
    if (!scene_.exists(hit_entity)) {
      return kInvalidEntityId;
    }
    if (edit_context_.active()) {
      return is_in_active_definition_scope(hit_entity) ? hit_entity
                                                       : kInvalidEntityId;
    }
    if (!scene_.is_instance_definition_member(hit_entity)) {
      return scene_.is_instance_definition_entity(hit_entity)
                 ? kInvalidEntityId
                 : hit_entity;
    }

    const EntityId definition =
        scene_.containing_instance_definition(hit_entity);
    if (valid_occurrence(rendered_occurrence)) {
      const InstanceOccurrenceComponent *occurrence =
          scene_.instance_occurrence(rendered_occurrence);
      if (occurrence != nullptr && occurrence->definition == definition) {
        return rendered_occurrence;
      }
    }
    const std::vector<EntityId> occurrences =
        scene_.instance_occurrences_for_definition(definition);
    return occurrences.empty() ? kInvalidEntityId : occurrences.front();
  }

  [[nodiscard]] std::optional<QMat4> effective_member_world_transform(
      EntityId member,
      EntityId occurrence = kInvalidEntityId) const {
    const EntityId definition = scene_.containing_instance_definition(member);
    if (definition == kInvalidEntityId || definition == member) {
      return std::nullopt;
    }
    sanitize_edit_context();
    if (occurrence == kInvalidEntityId && edit_context_.active() &&
        edit_context_.definition() == definition) {
      occurrence = edit_context_.occurrence();
    }
    if (!valid_occurrence(occurrence)) {
      return std::nullopt;
    }
    const InstanceOccurrenceComponent *component =
        scene_.instance_occurrence(occurrence);
    if (component == nullptr || component->definition != definition) {
      return std::nullopt;
    }
    const std::optional<QMat4> inverse_definition =
        try_scene_matrix_inverse(scene_.world_transform_matrix(definition));
    if (!inverse_definition.has_value()) {
      return std::nullopt;
    }
    const QMat4 member_relative = scene_transform_multiply(
        *inverse_definition, scene_.world_transform_matrix(member));
    return scene_transform_multiply(scene_.world_transform_matrix(occurrence),
                                    member_relative);
  }

  [[nodiscard]] std::optional<QMat4>
  active_edit_display_from_scene() const {
    sanitize_edit_context();
    if (!edit_context_.active()) {
      return std::nullopt;
    }
    const std::optional<QMat4> inverse_definition =
        try_scene_matrix_inverse(
            scene_.world_transform_matrix(edit_context_.definition()));
    if (!inverse_definition.has_value()) {
      return std::nullopt;
    }
    return scene_transform_multiply(
        scene_.world_transform_matrix(edit_context_.occurrence()),
        *inverse_definition);
  }

  void clear_transient_state() { edit_context_.clear(); }

private:
  /// Captures validated names, placement, and members for instance creation.
  struct CreatePlan final {
    std::string occurrence_name;
    std::string definition_name;
    EntityId occurrence_parent = kInvalidEntityId;
    TransformComponent occurrence_transform;
    std::vector<scene_instance_detail::MemberPlacement> members;
  };

  [[nodiscard]] std::optional<CreatePlan> prepare_create() const {
    sanitize_edit_context();
    if (edit_context_.active() ||
        modeling_.selection_mode() != ModelingSelectionMode::Object ||
        modeling_.can_cancel_active_tool_or_preview()) {
      return std::nullopt;
    }
    const std::vector<EntityId> selected = scene_.selected_entities();
    if (selected.empty()) {
      return std::nullopt;
    }
    const EntityId common_parent = scene_.parent(selected.front());
    const QMat4 parent_world =
        common_parent == kInvalidEntityId
            ? scene_transform_identity_matrix()
            : scene_.world_transform_matrix(common_parent);
    const std::optional<QMat4> inverse_parent =
        try_scene_matrix_inverse(parent_world);
    const std::optional<SceneBounds> selected_bounds =
        selected_transform_world_bounds(scene_, assets_);
    if (!inverse_parent.has_value() || !selected_bounds.has_value()) {
      return std::nullopt;
    }

    CreatePlan plan;
    plan.occurrence_parent = common_parent;
    plan.occurrence_transform.position = transform_point(
        *inverse_parent, bounds_center(*selected_bounds));
    const QMat4 occurrence_world = scene_transform_multiply(
        parent_world,
        compose_transform(plan.occurrence_transform.spatial_transform()));
    if (selected.size() == 1u) {
      if (const NameComponent *source_name = scene_.name(selected.front())) {
        plan.occurrence_name = source_name->name;
      }
    }
    if (plan.occurrence_name.empty()) {
      plan.occurrence_name = SceneEntityNaming::next_name(scene_, "Instance");
    }
    plan.definition_name =
        SceneEntityNaming::next_name(scene_, plan.occurrence_name + " Definition");
    plan.members.reserve(selected.size());
    for (const EntityId entity : selected) {
      if (!valid_creation_hierarchy(entity) ||
          scene_.parent(entity) != common_parent) {
        return std::nullopt;
      }
      const TransformComponent *transform = scene_.transform(entity);
      if (transform == nullptr) {
        return std::nullopt;
      }
      const std::optional<TransformComponent> definition_local =
          try_scene_local_transform_preserving_world(
              occurrence_world, scene_.world_transform_matrix(entity),
              *transform);
      if (!definition_local.has_value()) {
        return std::nullopt;
      }
      plan.members.push_back(scene_instance_detail::MemberPlacement{
          .entity = entity,
          .before = DocumentEntityPlacement{
              .entity = entity,
              .parent = common_parent,
              .transform = *transform,
              .layer_id = scene_.entity_layer(entity),
          },
          .after = DocumentEntityPlacement{
              .entity = entity,
              .parent = kInvalidEntityId,
              .transform = *definition_local,
              .layer_id = kInvalidSceneLayerId,
          },
      });
    }
    // The command creates the definition later; use a sentinel here and patch
    // the parent immediately before execution through the command-owned id.
    return plan;
  }

  [[nodiscard]] bool valid_creation_hierarchy(EntityId root) const {
    if (!scene_.exists(root) ||
        (scene_.kind(root) != EntityKind::Mesh &&
         scene_.kind(root) != EntityKind::Empty)) {
      return false;
    }
    bool contains_mesh = false;
    for (const EntityId entity : scene_.descendants(root, true)) {
      if (!scene_.exists(entity) || scene_.is_effectively_locked(entity) ||
          !scene_.is_effectively_visible(entity) ||
          scene_.instance_occurrence(entity) != nullptr ||
          scene_.is_instance_definition_entity(entity) ||
          scene_.is_instance_definition_member(entity)) {
        return false;
      }
      switch (scene_.kind(entity)) {
      case EntityKind::Mesh:
        if (scene_.mesh(entity) == nullptr ||
            scene_.modeling_object(entity) == nullptr) {
          return false;
        }
        contains_mesh = true;
        break;
      case EntityKind::Empty:
        if (scene_.mesh(entity) != nullptr || scene_.light(entity) != nullptr) {
          return false;
        }
        break;
      case EntityKind::Light:
      case EntityKind::Generic:
      case EntityKind::Instance:
        return false;
      }
    }
    return contains_mesh;
  }

  [[nodiscard]] std::optional<std::vector<EntityId>>
  selected_occurrences() const {
    const std::vector<EntityId> selected = scene_.selected_entities();
    if (selected.empty()) {
      return std::nullopt;
    }
    for (const EntityId occurrence : selected) {
      if (!valid_occurrence(occurrence) ||
          scene_.is_effectively_locked(occurrence) ||
          !scene_.is_effectively_visible(occurrence)) {
        return std::nullopt;
      }
    }
    return selected;
  }

  [[nodiscard]] bool valid_occurrence(EntityId occurrence) const {
    if (!scene_.exists(occurrence) ||
        scene_.kind(occurrence) != EntityKind::Instance) {
      return false;
    }
    const InstanceOccurrenceComponent *component =
        scene_.instance_occurrence(occurrence);
    return component != nullptr &&
           scene_.is_instance_definition_entity(component->definition) &&
           !scene_.has_children(occurrence);
  }

  [[nodiscard]] bool valid_edit_context() const {
    sanitize_edit_context();
    return edit_context_.active();
  }

  void sanitize_edit_context() const {
    if (sanitizing_edit_context_) {
      return;
    }
    if (!edit_context_.active()) {
      edit_context_.clear();
      return;
    }
    const EntityId occurrence_id = edit_context_.occurrence();
    bool valid = valid_occurrence(occurrence_id);
    const InstanceOccurrenceComponent *occurrence =
        valid ? scene_.instance_occurrence(occurrence_id) : nullptr;
    valid = valid && occurrence != nullptr &&
            occurrence->definition == edit_context_.definition();
    if (valid) {
      return;
    }

    // Undo, deletion, or document replacement can invalidate an occurrence
    // without routing through the breadcrumb command. Complete the transient
    // exit contract instead of merely dropping the IDs and leaving component
    // mode or a modal preview active.
    sanitizing_edit_context_ = true;
    edit_context_.clear();
    modeling_.clear_transient_state_for_document_replacement();
    (void)modeling_.clear_selection();
    (void)modeling_.set_selection_mode(ModelingSelectionMode::Object);
    if (valid_occurrence(occurrence_id)) {
      (void)apply_scene_selection(
          scene_, make_scene_selection(
                      scene_, std::span<const EntityId>{&occurrence_id, 1u},
                      occurrence_id),
          SceneSelectionApplicationMode::RuntimeSync);
    } else {
      (void)apply_scene_selection(scene_, SceneSelectionState{},
                                  SceneSelectionApplicationMode::RuntimeSync);
    }
    modeling_.project_object_selection_from_scene(true);
    sanitizing_edit_context_ = false;
  }

  [[nodiscard]] std::optional<QMat4> member_relative_transform(
      EntityId definition, EntityId member) const {
    const std::optional<QMat4> inverse_definition =
        try_scene_matrix_inverse(scene_.world_transform_matrix(definition));
    if (!inverse_definition.has_value()) {
      return std::nullopt;
    }
    return scene_transform_multiply(*inverse_definition,
                                    scene_.world_transform_matrix(member));
  }

  [[nodiscard]] std::optional<
      std::vector<scene_instance_detail::FlattenTextureMapping>>
  capture_flatten_texture_mappings(EntityId occurrence,
                                    EntityId definition) const {
    std::vector<scene_instance_detail::FlattenTextureMapping> mappings;
    const std::vector<EntityId> members = scene_.children(definition);
    for (std::size_t member_index = 0; member_index < members.size();
         ++member_index) {
      const std::vector<EntityId> hierarchy =
          scene_.descendants(members[member_index], true);
      for (std::size_t index = 0; index < hierarchy.size(); ++index) {
        const EntityId entity = hierarchy[index];
        if (scene_.mesh(entity) == nullptr) {
          continue;
        }
        const auto mapping =
            effective_texture_sampling_from_local(scene_, entity, occurrence);
        if (!mapping.has_value()) {
          return std::nullopt;
        }
        if (mapping->values != QMat4{}.values ||
            scene_.texture_mapping(entity) != nullptr) {
          mappings.push_back({.member_index = member_index,
                              .descendant_index = index,
                              .sampling_from_local = *mapping});
        }
      }
    }
    return mappings;
  }

  ModelingCommandResult bake_flatten_texture_mappings(
      std::span<const EntityId> members,
      std::span<const scene_instance_detail::FlattenTextureMapping> mappings) {
    bool changed = false;
    for (const auto &mapping : mappings) {
      if (mapping.member_index >= members.size()) {
        return {.ok = false, .message = "Flattened texture member is missing."};
      }
      const std::vector<EntityId> hierarchy =
          scene_.descendants(members[mapping.member_index], true);
      if (mapping.descendant_index >= hierarchy.size()) {
        return {.ok = false,
                .message = "Flattened texture hierarchy does not match."};
      }
      const EntityId entity = hierarchy[mapping.descendant_index];
      const ModelingObjectComponent *object = scene_.modeling_object(entity);
      if (object == nullptr) {
        return {.ok = false,
                .message = "Flattened texture mesh is not editable."};
      }
      const int object_id = object->object_id;
      const std::optional<MeshAsset> mesh = modeling_.compile_object_mesh(object_id);
      if (!mesh.has_value() || !mesh->authored_polygon_document) {
        return {.ok = false,
                .message = "Flattened texture topology is unavailable."};
      }
      quader_poly::Document document = *mesh->authored_polygon_document;
      try {
        if (MeshProjectionService::bake_polygon_texture_mapping(
                document, mapping.sampling_from_local)) {
          const ModelingCommandResult replaced =
              modeling_.replace_mesh_document(object_id, std::move(document));
          if (!replaced.ok) {
            return replaced;
          }
          changed = changed || replaced.changed;
        }
      } catch (const std::exception &error) {
        return {.ok = false, .message = error.what()};
      }
      if (scene_.texture_mapping(entity) != nullptr) {
        if (!scene_.clear_texture_mapping(entity)) {
          return {.ok = false,
                  .message = "Flattened texture placement could not be cleared."};
        }
        changed = true;
      }
    }
    return {.ok = true, .changed = changed};
  }

  [[nodiscard]] std::optional<scene_instance_detail::MemberPlacement>
  flatten_placement(EntityId occurrence, EntityId member,
                    EntityId output_entity) const {
    const EntityId output_parent = scene_.parent(occurrence);
    const TransformComponent *current = scene_.transform(output_entity);
    const EntityId definition = scene_.containing_instance_definition(member);
    const std::optional<QMat4> relative =
        member_relative_transform(definition, member);
    if (current == nullptr || !relative.has_value()) {
      return std::nullopt;
    }
    const QMat4 desired_world = scene_transform_multiply(
        scene_.world_transform_matrix(occurrence), *relative);
    const QMat4 parent_world =
        output_parent == kInvalidEntityId
            ? scene_transform_identity_matrix()
            : scene_.world_transform_matrix(output_parent);
    const std::optional<TransformComponent> output_local =
        try_scene_local_transform_preserving_world(parent_world, desired_world,
                                                   *current);
    if (!output_local.has_value()) {
      return std::nullopt;
    }
    return scene_instance_detail::MemberPlacement{
        .entity = output_entity,
        .before = DocumentEntityPlacement{
            .entity = output_entity,
            .parent = scene_.parent(output_entity),
            .transform = *current,
        },
        .after = DocumentEntityPlacement{
            .entity = output_entity,
            .parent = output_parent,
            .transform = *output_local,
        },
    };
  }

  SceneInstanceMutationResult flatten_last_occurrence(EntityId occurrence,
                                                       EntityId definition) {
    const std::vector<EntityId> members = scene_.children(definition);
    if (members.empty()) {
      return invalid("Instance definition has no members.");
    }
    std::vector<scene_instance_detail::MemberPlacement> placements;
    placements.reserve(members.size());
    for (const EntityId member : members) {
      const std::optional<scene_instance_detail::MemberPlacement> placement =
          flatten_placement(occurrence, member, member);
      if (!placement.has_value()) {
        return invalid("Instance member placement cannot be flattened.");
      }
      placements.push_back(*placement);
    }
    auto command = std::make_unique<
        scene_instance_detail::FlattenLastSceneInstanceCommand>(
        scene_, occurrence, definition, placements);
    const CommandResult status = history_.execute(std::move(command));
    if (!status.ok || !status.changed) {
      return from_status(status);
    }
    modeling_.project_object_selection_from_scene(true);
    return {
        .ok = true,
        .changed = true,
        .message = "Instance flattened.",
        .definition = definition,
        .entities = members,
    };
  }

  SceneInstanceMutationResult flatten_linked_occurrence_in_transaction(
      EntityId occurrence, EntityId definition) {
    const std::vector<EntityId> members = scene_.children(definition);
    if (members.empty()) {
      return invalid("Instance definition has no members.");
    }

    CommandResult selected = document_commands_.select_entities(
        members, members.front(), SceneSelectionApplicationMode::UndoableCommand);
    if (!selected.ok) {
      return from_status(selected);
    }
    modeling_.project_object_selection_from_scene(true);
    const ModelingCommandResult duplicated =
        modeling_.duplicate_selected_entity_hierarchy_with_independent_meshes(
            document_commands_);
    if (!duplicated.ok || !duplicated.changed) {
      return {
          .ok = duplicated.ok,
          .changed = duplicated.changed,
          .message = duplicated.message,
      };
    }

    const std::vector<EntityId> duplicates = scene_.selected_entities();
    if (duplicates.size() != members.size()) {
      return invalid(
          "Flatten did not create one editable mesh for every member.");
    }
    std::vector<scene_instance_detail::MemberPlacement> placements;
    placements.reserve(duplicates.size());
    for (std::size_t index = 0; index < duplicates.size(); ++index) {
      const std::optional<scene_instance_detail::MemberPlacement> placement =
          flatten_placement(occurrence, members[index], duplicates[index]);
      if (!placement.has_value()) {
        return invalid("Flattened mesh placement could not be resolved.");
      }
      placements.push_back(*placement);
    }

    auto finalize = std::make_unique<
        scene_instance_detail::FinalizeFlattenSceneInstanceCommand>(
        scene_, occurrence, placements);
    const CommandResult finalized = history_.execute(std::move(finalize));
    if (!finalized.ok || !finalized.changed) {
      return from_status(finalized);
    }
    return {
        .ok = true,
        .changed = true,
        .message = "Instance flattened.",
        .definition = definition,
        .entities = duplicates,
    };
  }

  [[nodiscard]] static SceneInstanceMutationResult invalid(std::string message) {
    return {
        .ok = false,
        .changed = false,
        .message = std::move(message),
    };
  }

  [[nodiscard]] static SceneInstanceMutationResult
  from_status(const CommandResult &status) {
    return {
        .ok = status.ok,
        .changed = status.changed,
        .message = status.message,
    };
  }

  SceneDocument &scene_;
  const AssetRegistry &assets_;
  DocumentCommandService &document_commands_;
  EditorHistoryService &history_;
  ModelingSessionService &modeling_;
  InstanceEditContext &edit_context_;
  const std::string &scene_name_;
  mutable bool sanitizing_edit_context_ = false;
};

} // namespace quader
