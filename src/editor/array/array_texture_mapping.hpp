////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/commands/document/scene_texture_mapping_plan.hpp"
#include "scene/scene_texture_mapping.hpp"
#include "scene/scene_transform.hpp"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace quader {

/** Resolves planned direct/occurrence charts in the preview mesh's coordinates. */
[[nodiscard]] inline std::optional<std::vector<QMat4>>
array_member_sampling(const SceneDocument &scene, EntityId member,
                      EntityId occurrence, std::span<const QMat4> planned,
                      bool plans_occurrence,
                      std::span<const QMat4> generated_member_worlds = {}) {
  std::vector<QMat4> result;
  result.reserve(planned.size());
  const TextureMappingComponent *mapping = scene.texture_mapping(member);
  const QMat4 direct = mapping != nullptr ? mapping->sampling_from_local : QMat4{};
  const bool member_edit = !plans_occurrence && occurrence != kInvalidEntityId;
  std::optional<QMat4> occurrence_from_world;
  if (member_edit) {
    if (generated_member_worlds.size() != planned.size()) return std::nullopt;
    occurrence_from_world = try_scene_matrix_inverse(scene.world_transform_matrix(occurrence));
    if (!occurrence_from_world.has_value()) return std::nullopt;
  }
  const auto *occurrence_mapping = scene.texture_mapping(occurrence);
  for (std::size_t index = 0; index < planned.size(); ++index) {
    const QMat4 &next = planned[index];
    if (member_edit) {
      const QMat4 definition_from_member = scene_transform_multiply(
          *occurrence_from_world, generated_member_worlds[index]);
      const auto effective = compose_texture_sampling_in_definition(
          next, definition_from_member,
          occurrence_mapping != nullptr ? occurrence_mapping->sampling_from_local : QMat4{});
      if (!effective.has_value()) return std::nullopt;
      result.push_back(*effective);
      continue;
    }
    const auto effective = plans_occurrence
        ? compose_texture_sampling_with_occurrence_mapping(
              scene, member, direct, occurrence, next)
        : compose_texture_sampling_from_local(scene, member, next, occurrence);
    if (!effective.has_value()) return std::nullopt;
    result.push_back(*effective);
  }
  return result;
}

/** Captures the shared texture-lock policy for one array source placement. */
class ArrayTextureMappingPlan final {
public:
  [[nodiscard]] static std::optional<ArrayTextureMappingPlan>
  capture(const SceneDocument &scene, EntityId source,
          TextureLockSettings settings,
          std::optional<QMat4> visible_source_world = std::nullopt) {
    const TransformComponent *local = scene.transform(source);
    auto plan = SceneTextureMappingPlan::capture(
        scene, std::span<const EntityId>(&source, 1u), settings);
    if (local == nullptr || !plan.has_value()) return std::nullopt;
    const EntityId parent = scene.parent(source);
    QMat4 authored_from_visible;
    if (visible_source_world.has_value()) {
      const auto inverse = try_scene_matrix_inverse(*visible_source_world);
      if (!inverse.has_value()) return std::nullopt;
      authored_from_visible = scene_transform_multiply(
          scene.world_transform_matrix(source), *inverse);
      if (scene_matrix_near_equal(authored_from_visible, QMat4{}))
        authored_from_visible = {};
    }
    return ArrayTextureMappingPlan{
        source, *local,
        parent == kInvalidEntityId ? QMat4{} : scene.world_transform_matrix(parent),
        authored_from_visible, std::move(*plan)};
  }

  [[nodiscard]] std::optional<std::vector<QMat4>>
  evaluate(std::span<const QMat4> worlds) const {
    std::vector<QMat4> mappings;
    mappings.reserve(worlds.size());
    for (const QMat4 &visible_world : worlds) {
      const QMat4 world = scene_transform_multiply(authored_from_visible_, visible_world);
      const auto local = try_scene_local_transform_preserving_world(
          parent_world_, world, before_);
      if (!local.has_value() ||
          !scene_matrix_near_equal(
              scene_transform_multiply(parent_world_,
                                       scene_local_transform_matrix(*local)),
              world)) return std::nullopt;
      const DocumentTransformEdit edit{.entity = source_, .before = before_,
                                       .after = *local};
      const auto evaluated = plan_.evaluate(
          std::span<const DocumentTransformEdit>(&edit, 1u));
      if (!evaluated.has_value()) return std::nullopt;
      const auto found = std::ranges::find(*evaluated, source_,
                                           &DocumentTextureMappingEdit::entity);
      if (found == evaluated->end()) return std::nullopt;
      mappings.push_back(found->after.has_value()
                             ? found->after->sampling_from_local : QMat4{});
    }
    return mappings;
  }

private:
  ArrayTextureMappingPlan(EntityId source, TransformComponent before,
                          QMat4 parent_world, QMat4 authored_from_visible,
                          SceneTextureMappingPlan plan)
      : source_(source), before_(before), parent_world_(parent_world),
        authored_from_visible_(authored_from_visible), plan_(std::move(plan)) {}

  EntityId source_;
  TransformComponent before_;
  QMat4 parent_world_;
  QMat4 authored_from_visible_;
  SceneTextureMappingPlan plan_;
};

/** Gates hierarchy capture independently of changing array placement samples. */
class ArrayTextureMappingCache final {
public:
  [[nodiscard]] const std::optional<ArrayTextureMappingPlan> &resolve(
      const SceneDocument &scene, EntityId source, TextureLockSettings settings,
      QMat4 visible_source_world) {
    if (!initialized_ || scene_revision_ != scene.revision() || source_ != source ||
        settings_.texture_lock != settings.texture_lock ||
        settings_.texture_lock_scale != settings.texture_lock_scale) {
      plan_ = ArrayTextureMappingPlan::capture(scene, source, settings,
                                              visible_source_world);
      source_ = source;
      scene_revision_ = scene.revision();
      settings_ = settings;
      initialized_ = true;
    }
    return plan_;
  }

private:
  EntityId source_ = kInvalidEntityId;
  std::uint64_t scene_revision_ = 0u;
  TextureLockSettings settings_;
  bool initialized_ = false;
  std::optional<ArrayTextureMappingPlan> plan_;
};

} // namespace quader
