////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <mesh/polygon/document.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace quader_editor_core {

/**
 * Enumerates EditorOperationId values used by the modeling layer.
 */
enum class EditorOperationId {
  Bridge,
  EdgeBevel,
  Thicken,
  SliceQuad,
  MergeByDistance,
  HalfCut,
  RemoveDoubles,
  BevelVertices,
  DuplicateMeshes,
  ConnectVertices,
  DissolveVertices,
  ConnectEdges,
  SplitEdges,
  DissolveEdges,
  MergeEdges,
  CollapseEdges,
  FillHole,
  CombineFaces,
  CollapseFaces,
  RadialAlign,
  FlipFaceNormals,
  ShadeFacesSmooth,
  ShadeFacesFlat,
  HardenEdgeNormals,
  SoftenEdgeNormals,
  CombineMeshes,
  ExtractFaces,
  DetachFaces,
  SnapVerticesToActive,
  SnapEdgesToActive,
  MergeVerticesToActive,
  MergeVerticesToCenter,
  RecalculateFaceNormals,
  AssignMaterial,
  DeleteSelection,
};

inline constexpr double kDefaultCommandGridWorldSize = 1.0;

/**
 * Represents a Command Grid Fact value used by the modeling runtime domain.
 */
struct CommandGridFact {
  double value = 0.0;
  bool valid = false;
};

/**
 * Represents a Command Distance Fact value used by the modeling runtime domain.
 */
struct CommandDistanceFact {
  double value = 0.0;
  bool valid = false;
};

/**
 * Represents a Command QVec3 Fact value used by the modeling runtime domain.
 */
struct CommandVec3Fact {
  quader::QVec3 value{};
  bool valid = false;
};

/**
 * Represents an Int Operation Option Schema value used by the modeling runtime
 * domain.
 */
struct IntOperationOptionSchema {
  std::string_view id;
  std::string_view label;
  int default_value = 0;
  int minimum = 0;
  int maximum = 0;
  int step = 1;
  bool hard_limits = true;
};

/**
 * Represents a Float Operation Option Schema value used by the modeling runtime
 * domain.
 */
struct FloatOperationOptionSchema {
  std::string_view id;
  std::string_view label;
  double default_value = 0.0;
  double minimum = 0.0;
  double maximum = 0.0;
  double step = 0.01;
  bool hard_limits = true;
};

/**
 * Represents a Bool Operation Option Schema value used by the modeling runtime
 * domain.
 */
struct BoolOperationOptionSchema {
  std::string_view id;
  std::string_view label;
  bool default_value = false;
  bool visible = true;
};

/**
 * Enumerates EditorOperationOptionValueKind values used by the modeling runtime
 * domain.
 */
enum class EditorOperationOptionValueKind {
  Integer,
  Floating,
  Boolean,
};

/**
 * Stores flattened operation option metadata owned by the operation schema
 * layer.
 */
struct EditorOperationOptionDescriptor {
  EditorOperationId operation = EditorOperationId::Bridge;
  std::string_view operation_id;
  std::string_view operation_label;
  std::string_view preference_group_id;
  std::string_view preference_id;
  std::string_view option_id;
  std::string_view label;
  EditorOperationOptionValueKind value_kind =
      EditorOperationOptionValueKind::Integer;
  int default_integer = 0;
  int minimum_integer = 0;
  int maximum_integer = 0;
  int integer_step = 1;
  double default_number = 0.0;
  double minimum_number = 0.0;
  double maximum_number = 0.0;
  double number_step = 0.01;
  bool default_bool = false;
  bool visible = true;
  bool hard_limits = true;
};

/**
 * Represents a Bridge Operation Schema value used by the modeling runtime
 * domain.
 */
struct BridgeOperationSchema {
  EditorOperationId id = EditorOperationId::Bridge;
  std::string_view operation_id = "bridge";
  std::string_view label = "Bridge";
  IntOperationOptionSchema steps;
};

/**
 * Represents an Edge Bevel Operation Schema value used by the modeling runtime
 * domain.
 */
struct EdgeBevelOperationSchema {
  EditorOperationId id = EditorOperationId::EdgeBevel;
  std::string_view operation_id = "edge_bevel";
  std::string_view label = "Edge Bevel";
  FloatOperationOptionSchema width;
  FloatOperationOptionSchema profile;
  IntOperationOptionSchema segments;
  quader_poly::BevelProfileType default_profile_type =
      quader_poly::BevelProfileType::Offset;
};

/**
 * Represents a Thicken Operation Schema value used by the modeling runtime
 * domain.
 */
struct ThickenOperationSchema {
  EditorOperationId id = EditorOperationId::Thicken;
  std::string_view operation_id = "thicken";
  std::string_view label = "Thicken";
  FloatOperationOptionSchema thickness;
  BoolOperationOptionSchema from_center;
};

/**
 * Represents a Slice Quad Operation Schema value used by the modeling runtime
 * domain.
 */
struct SliceQuadOperationSchema {
  EditorOperationId id = EditorOperationId::SliceQuad;
  std::string_view operation_id = "slice_quad";
  std::string_view label = "Slice Quad";
  IntOperationOptionSchema x_slices;
  IntOperationOptionSchema y_slices;
};

/**
 * Represents a Merge By Distance Operation Schema value used by the modeling
 * runtime domain.
 */
struct MergeByDistanceOperationSchema {
  EditorOperationId id = EditorOperationId::MergeByDistance;
  std::string_view operation_id = "merge_by_distance";
  std::string_view label = "Merge by Distance";
  FloatOperationOptionSchema tolerance;
};

/**
 * Enumerates the local mesh axis used by the centered Half-Cut plane.
 */
enum class HalfCutAxis {
  X = 0,
  Y = 1,
  Z = 2,
};

/**
 * Represents a Half-Cut Operation Schema value used by the modeling runtime
 * domain.
 */
struct HalfCutOperationSchema {
  EditorOperationId id = EditorOperationId::HalfCut;
  std::string_view operation_id = "half_cut";
  std::string_view label = "Half-Cut";
  IntOperationOptionSchema axis;
  BoolOperationOptionSchema separate_parts;
  BoolOperationOptionSchema fill_parts;
};

/**
 * Represents a Bevel Vertices Operation Schema value used by the modeling
 * runtime domain.
 */
struct BevelVerticesOperationSchema {
  EditorOperationId id = EditorOperationId::BevelVertices;
  std::string_view operation_id = "bevel_vertices";
  std::string_view label = "Bevel Vertices";
};

/**
 * Represents a Duplicate Meshes Operation Schema value used by the modeling
 * runtime domain.
 */
struct DuplicateMeshesOperationSchema {
  EditorOperationId id = EditorOperationId::DuplicateMeshes;
  std::string_view operation_id = "duplicate_meshes";
  std::string_view label = "Duplicate Meshes";
};

/**
 * Represents a Bridge Operation Settings value used by the modeling runtime
 * domain.
 */
struct BridgeOperationSettings {
  int steps = 1;
};

/**
 * Represents an Edge Bevel Operation Settings value used by the modeling
 * runtime domain.
 */
struct EdgeBevelOperationSettings {
  double width = 1.0;
  double profile = 0.5;
  int profile_type = static_cast<int>(quader_poly::BevelProfileType::Offset);
  int segments = 1;
};

/**
 * Represents a Thicken Operation Settings value used by the modeling runtime
 * domain.
 */
struct ThickenOperationSettings {
  double thickness = 0.25;
  bool from_center = false;
};

/**
 * Represents a Slice Quad Operation Settings value used by the modeling runtime
 * domain.
 */
struct SliceQuadOperationSettings {
  int x_slices = 1;
  int y_slices = 1;
};

/**
 * Represents a Merge By Distance Operation Settings value used by the modeling
 * runtime domain.
 */
struct MergeByDistanceOperationSettings {
  double tolerance = 0.125;
};

/**
 * Represents a Half-Cut Operation Settings value used by the modeling runtime
 * domain.
 */
struct HalfCutOperationSettings {
  HalfCutAxis axis = HalfCutAxis::X;
  bool separate_parts = false;
  bool fill_parts = true;
};

/**
 * Represents a Bevel Vertices Operation Settings value used by the modeling
 * runtime domain.
 */
struct BevelVerticesOperationSettings {
  CommandGridFact focused_grid;
  CommandDistanceFact explicit_distance;
};

/**
 * Represents a Duplicate Meshes Operation Settings value used by the modeling
 * runtime domain.
 */
struct DuplicateMeshesOperationSettings {
  CommandGridFact focused_grid;
  CommandVec3Fact explicit_offset;
};

/**
 * Represents a Recalculate Face Normals Operation Settings value used by the
 * modeling runtime domain.
 */
struct RecalculateFaceNormalsOperationSettings {
  bool outside = true;
};

/**
 * Represents an Assign Material Operation Settings value used by the modeling
 * runtime domain.
 */
struct AssignMaterialOperationSettings {
  std::uint32_t material_slot = 0;
  bool explicit_target = false;
  int object_id = -1;
  quader_poly::ElementId face_id = quader_poly::kInvalidElementId;
};

/**
 * Represents a Selection Operation Settings value used by the modeling runtime
 * domain.
 */
struct SelectionOperationSettings {};

inline constexpr BridgeOperationSchema bridge_operation_schema() {
  return {
      EditorOperationId::Bridge,
      "bridge",
      "Bridge",
      {"steps", "Steps", 1, 1, 64, 1, true},
  };
}

inline constexpr EdgeBevelOperationSchema edge_bevel_operation_schema() {
  return {
      EditorOperationId::EdgeBevel,
      "edge_bevel",
      "Edge Bevel",
      {"width", "Width", 1.0, 0.0625, 16384.0, 1.0, true},
      {"profile", "Profile", 0.5, 0.0, 1.0, 0.01, true},
      {"segments", "Segments", 1, 1, 16, 1, true},
      quader_poly::BevelProfileType::Offset,
  };
}

inline constexpr ThickenOperationSchema thicken_operation_schema() {
  return {
      EditorOperationId::Thicken,
      "thicken",
      "Thicken",
      {"thickness", "Thickness", 0.25, -4.0, 4.0, 0.01, false},
      {"from_center", "From Center", false, false},
  };
}

inline constexpr SliceQuadOperationSchema slice_quad_operation_schema() {
  return {
      EditorOperationId::SliceQuad,
      "slice_quad",
      "Slice Quad",
      {"x_slices", "X", 1, 0, 64, 1, true},
      {"y_slices", "Y", 1, 0, 64, 1, true},
  };
}

inline constexpr MergeByDistanceOperationSchema
merge_by_distance_operation_schema() {
  return {
      EditorOperationId::MergeByDistance,
      "merge_by_distance",
      "Merge by Distance",
      {"tolerance", "Tolerance", 0.125, 0.0, 128.0, 0.001, true},
  };
}

inline constexpr HalfCutOperationSchema half_cut_operation_schema() {
  return {
      EditorOperationId::HalfCut,
      "half_cut",
      "Half-Cut",
      {"axis", "Axis", static_cast<int>(HalfCutAxis::X),
       static_cast<int>(HalfCutAxis::X), static_cast<int>(HalfCutAxis::Z), 1,
       true},
      {"separate_parts", "Separate Parts", false, true},
      {"fill_parts", "Fill Parts", true, true},
  };
}

inline constexpr BevelVerticesOperationSchema
bevel_vertices_operation_schema() {
  return {
      EditorOperationId::BevelVertices,
      "bevel_vertices",
      "Bevel Vertices",
  };
}

inline constexpr DuplicateMeshesOperationSchema
duplicate_meshes_operation_schema() {
  return {
      EditorOperationId::DuplicateMeshes,
      "duplicate_meshes",
      "Duplicate Meshes",
  };
}

inline constexpr EditorOperationOptionDescriptor operation_option_descriptor(
    EditorOperationId operation, std::string_view operation_id,
    std::string_view operation_label, std::string_view preference_group_id,
    std::string_view preference_id, const IntOperationOptionSchema &schema) {
  return {
      operation,
      operation_id,
      operation_label,
      preference_group_id,
      preference_id,
      schema.id,
      schema.label,
      EditorOperationOptionValueKind::Integer,
      schema.default_value,
      schema.minimum,
      schema.maximum,
      schema.step,
      0.0,
      0.0,
      0.0,
      0.01,
      false,
      true,
      schema.hard_limits,
  };
}

inline constexpr EditorOperationOptionDescriptor operation_option_descriptor(
    EditorOperationId operation, std::string_view operation_id,
    std::string_view operation_label, std::string_view preference_group_id,
    std::string_view preference_id, const FloatOperationOptionSchema &schema) {
  return {
      operation,
      operation_id,
      operation_label,
      preference_group_id,
      preference_id,
      schema.id,
      schema.label,
      EditorOperationOptionValueKind::Floating,
      0,
      0,
      0,
      1,
      schema.default_value,
      schema.minimum,
      schema.maximum,
      schema.step,
      false,
      true,
      schema.hard_limits,
  };
}

inline constexpr EditorOperationOptionDescriptor operation_option_descriptor(
    EditorOperationId operation, std::string_view operation_id,
    std::string_view operation_label, std::string_view preference_group_id,
    std::string_view preference_id, const BoolOperationOptionSchema &schema) {
  return {
      operation,
      operation_id,
      operation_label,
      preference_group_id,
      preference_id,
      schema.id,
      schema.label,
      EditorOperationOptionValueKind::Boolean,
      0,
      0,
      0,
      1,
      0.0,
      0.0,
      0.0,
      0.01,
      schema.default_value,
      schema.visible,
      false,
  };
}

inline std::span<const EditorOperationOptionDescriptor>
operation_option_descriptor_catalog() {
  constexpr BridgeOperationSchema kBridge = bridge_operation_schema();
  constexpr EdgeBevelOperationSchema kEdgeBevel = edge_bevel_operation_schema();
  constexpr ThickenOperationSchema kThicken = thicken_operation_schema();
  constexpr SliceQuadOperationSchema kSliceQuad = slice_quad_operation_schema();
  constexpr MergeByDistanceOperationSchema kMergeByDistance =
      merge_by_distance_operation_schema();
  constexpr HalfCutOperationSchema kHalfCut = half_cut_operation_schema();

  static constexpr std::array<EditorOperationOptionDescriptor, 12> kDescriptors{{
      operation_option_descriptor(kBridge.id, kBridge.operation_id,
                                  kBridge.label, "operation.bridge",
                                  "operation.bridge.steps", kBridge.steps),
      operation_option_descriptor(kEdgeBevel.id, kEdgeBevel.operation_id,
                                  kEdgeBevel.label, "operation.edge_bevel",
                                  "operation.edge_bevel.width",
                                  kEdgeBevel.width),
      operation_option_descriptor(kEdgeBevel.id, kEdgeBevel.operation_id,
                                  kEdgeBevel.label, "operation.edge_bevel",
                                  "operation.edge_bevel.profile",
                                  kEdgeBevel.profile),
      operation_option_descriptor(kEdgeBevel.id, kEdgeBevel.operation_id,
                                  kEdgeBevel.label, "operation.edge_bevel",
                                  "operation.edge_bevel.segments",
                                  kEdgeBevel.segments),
      operation_option_descriptor(kThicken.id, kThicken.operation_id,
                                  kThicken.label, "operation.thicken",
                                  "operation.thicken.thickness",
                                  kThicken.thickness),
      operation_option_descriptor(kThicken.id, kThicken.operation_id,
                                  kThicken.label, "operation.thicken",
                                  "operation.thicken.from_center",
                                  kThicken.from_center),
      operation_option_descriptor(kSliceQuad.id, kSliceQuad.operation_id,
                                  kSliceQuad.label, "operation.slice_quad",
                                  "operation.slice_quad.x_slices",
                                  kSliceQuad.x_slices),
      operation_option_descriptor(kSliceQuad.id, kSliceQuad.operation_id,
                                  kSliceQuad.label, "operation.slice_quad",
                                  "operation.slice_quad.y_slices",
                                  kSliceQuad.y_slices),
      operation_option_descriptor(
          kMergeByDistance.id, kMergeByDistance.operation_id,
          kMergeByDistance.label, "operation.merge_by_distance",
          "operation.merge_by_distance.tolerance", kMergeByDistance.tolerance),
      operation_option_descriptor(kHalfCut.id, kHalfCut.operation_id,
                                  kHalfCut.label, "operation.half_cut",
                                  "operation.half_cut.axis", kHalfCut.axis),
      operation_option_descriptor(
          kHalfCut.id, kHalfCut.operation_id, kHalfCut.label,
          "operation.half_cut", "operation.half_cut.separate_parts",
          kHalfCut.separate_parts),
      operation_option_descriptor(
          kHalfCut.id, kHalfCut.operation_id, kHalfCut.label,
          "operation.half_cut", "operation.half_cut.fill_parts",
          kHalfCut.fill_parts),
  }};
  return kDescriptors;
}

inline bool operation_option_matches_setting_name(
    const EditorOperationOptionDescriptor &descriptor,
    std::string_view setting_name) {
  if (setting_name == descriptor.option_id ||
      setting_name == descriptor.preference_id) {
    return true;
  }
  if (descriptor.operation == EditorOperationId::Bridge &&
      descriptor.option_id == "steps") {
    return setting_name == "bridge_steps";
  }
  if (descriptor.operation == EditorOperationId::EdgeBevel &&
      descriptor.option_id == "width") {
    return setting_name == "amount" || setting_name == "edge_bevel_width";
  }
  if (descriptor.operation == EditorOperationId::EdgeBevel &&
      descriptor.option_id == "profile") {
    return setting_name == "edge_bevel_profile";
  }
  if (descriptor.operation == EditorOperationId::EdgeBevel &&
      descriptor.option_id == "segments") {
    return setting_name == "edge_bevel_segments";
  }
  if (descriptor.operation == EditorOperationId::Thicken &&
      descriptor.option_id == "thickness") {
    return setting_name == "amount";
  }
  if (descriptor.operation == EditorOperationId::MergeByDistance &&
      descriptor.option_id == "tolerance") {
    return setting_name == "distance";
  }
  return false;
}

inline std::optional<EditorOperationOptionDescriptor>
operation_option_for_setting(std::string_view operation_id,
                             std::string_view setting_name) {
  for (const EditorOperationOptionDescriptor &descriptor :
       operation_option_descriptor_catalog()) {
    if (!operation_id.empty() && descriptor.operation_id != operation_id) {
      continue;
    }
    if (operation_option_matches_setting_name(descriptor, setting_name)) {
      return descriptor;
    }
  }
  return std::nullopt;
}

inline int normalize_operation_int(const IntOperationOptionSchema &schema,
                                   int value) {
  if (!schema.hard_limits) {
    return value;
  }
  return std::clamp(value, schema.minimum, schema.maximum);
}

inline double
normalize_operation_float(const FloatOperationOptionSchema &schema,
                          double value) {
  if (!std::isfinite(value)) {
    return schema.default_value;
  }
  if (!schema.hard_limits) {
    return value;
  }
  return std::clamp(value, schema.minimum, schema.maximum);
}

inline double command_grid_world_size_or_default(CommandGridFact grid) {
  return grid.valid && std::isfinite(grid.value) && grid.value > 0.0
             ? grid.value
             : kDefaultCommandGridWorldSize;
}

inline float
bevel_vertices_distance_from_settings(BevelVerticesOperationSettings settings) {
  if (settings.explicit_distance.valid &&
      std::isfinite(settings.explicit_distance.value)) {
    return static_cast<float>(settings.explicit_distance.value);
  }
  return static_cast<float>(
      command_grid_world_size_or_default(settings.focused_grid));
}

inline quader::QVec3 duplicate_meshes_offset_from_settings(
    DuplicateMeshesOperationSettings settings) {
  if (settings.explicit_offset.valid &&
      std::isfinite(settings.explicit_offset.value.x) &&
      std::isfinite(settings.explicit_offset.value.y) &&
      std::isfinite(settings.explicit_offset.value.z)) {
    return settings.explicit_offset.value;
  }
  return {};
}

inline quader_poly::BevelProfileType
sanitize_edge_bevel_profile_type(int profile_type) {
  return profile_type == static_cast<int>(quader_poly::BevelProfileType::Offset)
             ? quader_poly::BevelProfileType::Offset
             : quader_poly::BevelProfileType::Offset;
}

inline BridgeOperationSettings
normalize_bridge_operation_settings(BridgeOperationSettings settings) {
  settings.steps =
      normalize_operation_int(bridge_operation_schema().steps, settings.steps);
  return settings;
}

inline EdgeBevelOperationSettings
normalize_edge_bevel_operation_settings(EdgeBevelOperationSettings settings) {
  const EdgeBevelOperationSchema schema = edge_bevel_operation_schema();
  settings.width = normalize_operation_float(schema.width, settings.width);
  settings.profile =
      normalize_operation_float(schema.profile, settings.profile);
  settings.profile_type =
      static_cast<int>(sanitize_edge_bevel_profile_type(settings.profile_type));
  settings.segments =
      normalize_operation_int(schema.segments, settings.segments);
  return settings;
}

inline ThickenOperationSettings
normalize_thicken_operation_settings(ThickenOperationSettings settings) {
  settings.thickness = normalize_operation_float(
      thicken_operation_schema().thickness, settings.thickness);
  return settings;
}

inline SliceQuadOperationSettings
normalize_slice_quad_operation_settings(SliceQuadOperationSettings settings) {
  const SliceQuadOperationSchema schema = slice_quad_operation_schema();
  settings.x_slices =
      normalize_operation_int(schema.x_slices, settings.x_slices);
  settings.y_slices =
      normalize_operation_int(schema.y_slices, settings.y_slices);
  return settings;
}

inline MergeByDistanceOperationSettings
normalize_merge_by_distance_operation_settings(
    MergeByDistanceOperationSettings settings) {
  settings.tolerance = normalize_operation_float(
      merge_by_distance_operation_schema().tolerance, settings.tolerance);
  return settings;
}

inline HalfCutAxis sanitize_half_cut_axis(int axis) {
  switch (axis) {
  case static_cast<int>(HalfCutAxis::X):
    return HalfCutAxis::X;
  case static_cast<int>(HalfCutAxis::Y):
    return HalfCutAxis::Y;
  case static_cast<int>(HalfCutAxis::Z):
    return HalfCutAxis::Z;
  default:
    return HalfCutAxis::X;
  }
}

inline HalfCutOperationSettings
normalize_half_cut_operation_settings(HalfCutOperationSettings settings) {
  settings.axis = sanitize_half_cut_axis(static_cast<int>(settings.axis));
  return settings;
}

inline quader_poly::EdgeBevelSettings
to_edge_bevel_settings(EdgeBevelOperationSettings settings) {
  settings = normalize_edge_bevel_operation_settings(settings);
  quader_poly::EdgeBevelSettings result;
  result.width = static_cast<float>(settings.width);
  result.profile = static_cast<float>(settings.profile);
  result.profile_type = sanitize_edge_bevel_profile_type(settings.profile_type);
  result.segments = settings.segments;
  return result;
}

} // namespace quader_editor_core
