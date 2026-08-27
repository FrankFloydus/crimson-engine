////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "scene/scene_document.hpp"
#include "scene/scene_transform.hpp"

#include <optional>

namespace quader {

/** A mapping is finite affine data; singular scales remain invalid. */
inline bool texture_mapping_is_valid(const TextureMappingComponent &mapping) {
  const QMat4 &matrix = mapping.sampling_from_local;
  return scene_matrix_is_finite(matrix) && matrix.values[3] == 0.0f &&
         matrix.values[7] == 0.0f && matrix.values[11] == 0.0f &&
         matrix.values[15] == 1.0f &&
         try_scene_matrix_inverse(matrix).has_value();
}

/** Composes a supplied member chart with its occurrence's sampling correction. */
inline std::optional<QMat4> compose_texture_sampling_from_local(
    const SceneDocument &scene, EntityId member,
    const QMat4 &direct, EntityId occurrence) {
  if (!scene.exists(member)) {
    return std::nullopt;
  }
  if (occurrence == kInvalidEntityId) {
    return direct;
  }
  const InstanceOccurrenceComponent *instance =
      scene.instance_occurrence(occurrence);
  if (instance == nullptr ||
      scene.containing_instance_definition(member) != instance->definition) {
    return std::nullopt;
  }
  const TextureMappingComponent *occurrence_mapping =
      scene.texture_mapping(occurrence);
  if (occurrence_mapping == nullptr) {
    return direct;
  }
  const std::optional<QMat4> definition_from_world =
      try_scene_matrix_inverse(scene.world_transform_matrix(instance->definition));
  if (!definition_from_world.has_value()) {
    return std::nullopt;
  }
  const QMat4 definition_from_member = scene_transform_multiply(
      *definition_from_world, scene.world_transform_matrix(member));
  const std::optional<QMat4> member_from_definition =
      try_scene_matrix_inverse(definition_from_member);
  if (!member_from_definition.has_value()) {
    return std::nullopt;
  }
  const QMat4 result = scene_transform_multiply(
      scene_transform_multiply(direct, *member_from_definition),
      scene_transform_multiply(occurrence_mapping->sampling_from_local,
                               definition_from_member));
  return scene_matrix_is_finite(result) ? std::optional<QMat4>{result}
                                        : std::nullopt;
}

/** Resolves ordinary or definition-member sampling in one scene owner. */
inline std::optional<QMat4> effective_texture_sampling_from_local(
    const SceneDocument &scene, EntityId member,
    EntityId occurrence = kInvalidEntityId) {
  const TextureMappingComponent *member_mapping = scene.texture_mapping(member);
  return compose_texture_sampling_from_local(
      scene, member,
      member_mapping != nullptr ? member_mapping->sampling_from_local : QMat4{},
      occurrence);
}

/** Solves a stored member chart for a desired effective occurrence chart. */
inline std::optional<TextureMappingComponent> texture_mapping_for_effective_sampling(
    const SceneDocument &scene, EntityId member,
    const QMat4 &desired_sampling_from_local,
    EntityId occurrence = kInvalidEntityId) {
  if (!texture_mapping_is_valid({desired_sampling_from_local})) {
    return std::nullopt;
  }
  // Resolve the same occurrence composition with an identity member chart.
  // Keep the forward evaluator's multiplication order unchanged.
  const std::optional<QMat4> correction = compose_texture_sampling_from_local(
      scene, member, QMat4{}, occurrence);
  if (!correction.has_value()) {
    return std::nullopt;
  }
  if (correction->values == QMat4{}.values) {
    return TextureMappingComponent{desired_sampling_from_local};
  }
  const std::optional<QMat4> correction_inverse =
      try_scene_matrix_inverse(*correction);
  if (!correction_inverse.has_value()) {
    return std::nullopt;
  }
  const TextureMappingComponent mapping{
      .sampling_from_local = scene_transform_multiply(
          desired_sampling_from_local, *correction_inverse)};
  return texture_mapping_is_valid(mapping)
             ? std::optional<TextureMappingComponent>{mapping} : std::nullopt;
}

} // namespace quader
