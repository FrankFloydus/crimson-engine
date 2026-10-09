////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include <string_view>

namespace quader {

/// Localization identity and English fallback for one Inspector label.
struct InspectorPropertyHelp {
  std::string_view key;
  std::string_view fallback;
};

/// Localized label help resolved only while building Inspector rows.
[[nodiscard]] inline InspectorPropertyHelp
qt_inspector_property_help(std::string_view stable_id) {
  if (stable_id.starts_with("material.")) {
    stable_id = "material";
  }
  constexpr std::string_view kTransformKinds[]{"location", "rotation", "scale"};
  for (const std::string_view kind : kTransformKinds) {
    constexpr std::string_view kPrefix = "transform.";
    if (stable_id.starts_with(kPrefix) &&
        stable_id.substr(kPrefix.size()).starts_with(kind) &&
        stable_id.size() > kPrefix.size() + kind.size() &&
        stable_id[kPrefix.size() + kind.size()] == '.') {
      stable_id = stable_id.substr(0, kPrefix.size() + kind.size());
      break;
    }
  }
  /// Associates a retained row identifier with its immutable help text.
  struct Entry {
    std::string_view id;
    InspectorPropertyHelp help;
  };
  static constexpr Entry kEntries[]{
      {"general", {"ui.inspector.help.general",
          "Object identity."}},
      {"visibility", {"ui.inspector.help.visibility",
          "Object visibility and selection in the viewport."}},
      {"transform", {"ui.inspector.help.transform",
          "Location, rotation and scale of the selected objects."}},
      {"material", {"ui.inspector.help.material",
          "Materials used by the selection. Use the row actions to edit or replace a material."}},
      {"export", {"ui.inspector.help.export",
          "Choose whether this mesh is included when exporting the scene."}},
      {"lighting", {"ui.inspector.help.lighting",
          "Control how the mesh is rendered and interacts with light."}},
      {"viewport", {"ui.inspector.help.viewport",
          "Control how the mesh is displayed in the viewport."}},
      {"physics", {"ui.inspector.help.physics",
          "Generate mesh collisions and control their viewport preview."}},
      {"light", {"ui.inspector.help.light",
          "Light shape, color, brightness, reach and shadows."}},
      {"components", {"ui.inspector.help.components",
          "Read-only counts of the components in the selected objects."}},
      {"summary.no_selection", {"ui.inspector.help.summary.no_selection",
          "Select an object or mesh component to inspect its properties."}},
      {"summary.mode", {"ui.inspector.help.summary.mode",
          "Current selection mode: objects, vertices, edges or faces."}},
      {"summary.active", {"ui.inspector.help.summary.active",
          "The kind of selection currently being inspected."}},
      {"summary.vertices", {"ui.inspector.help.summary.vertices",
          "Number of selected mesh vertices."}},
      {"summary.edges", {"ui.inspector.help.summary.edges",
          "Number of selected mesh edges."}},
      {"summary.faces", {"ui.inspector.help.summary.faces",
          "Number of selected mesh faces."}},
      {"general.name", {"ui.inspector.help.general.name",
          "Rename this object in the scene and Outliner."}},
      {"visibility.visible", {"ui.inspector.help.visibility.visible",
          "Show the selected objects in the viewport."}},
      {"visibility.selectable", {"ui.inspector.help.visibility.selectable",
          "Allow the selected objects to be picked in the viewport."}},
      {"transform.selection", {"ui.inspector.help.transform.selection",
          "Number of selected objects with an editable transform."}},
      {"transform.location", {"ui.inspector.help.transform.location",
          "Object location along X, Y and Z. A mixed value means the selected objects differ on that axis."}},
      {"transform.rotation", {"ui.inspector.help.transform.rotation",
          "Object rotation around X, Y and Z, in degrees. A mixed value means the selected objects differ on that axis."}},
      {"transform.scale", {"ui.inspector.help.transform.scale",
          "Object scale along X, Y and Z. A value of 1 keeps the original size; 2 doubles it."}},
      {"export.editor_only", {"ui.inspector.help.export.editor_only",
          "Keep this mesh in the editor but omit it from scene exports."}},
      {"lighting.cast_shadows", {"ui.inspector.help.lighting.cast_shadows",
          "Allow this mesh to cast shadows in rendered lighting."}},
      {"physics.generate_collisions", {"ui.inspector.help.physics.generate_collisions",
          "Generate a static collision shape from this mesh’s geometry."}},
      {"viewport.wireframe_on_shaded", {"ui.inspector.help.viewport.wireframe_on_shaded",
          "Show this mesh's wireframe when Show Wireframe is enabled in the viewport."}},
      {"viewport.custom_wireframe_color", {"ui.inspector.help.viewport.custom_wireframe_color",
          "Use a saved color for this mesh. Uncheck to inherit the viewport's wireframe color."}},
      {"viewport.wireframe_color", {"ui.inspector.help.viewport.wireframe_color",
          "Choose the custom wireframe color for the selected meshes."}},
      {"physics.display_collision", {"ui.inspector.help.physics.display_collision",
          "Show this mesh’s generated collision geometry in the viewport."}},
      {"physics.collision_status", {"ui.inspector.help.physics.collision_status",
          "Reports whether the mesh’s saved physics settings support collision generation."}},
      {"light.type", {"ui.inspector.help.light.type",
          "Choose a directional light, an omnidirectional point light or a focused spotlight."}},
      {"light.source_preset", {"ui.inspector.help.light.source_preset",
          "Apply a light-source preset to color and intensity. Shape, range and shadows stay unchanged."}},
      {"light.color_mode", {"ui.inspector.help.light.color_mode",
          "Choose between a color temperature in kelvin and a custom light color."}},
      {"light.color_temperature_kelvin", {"ui.inspector.help.light.color_temperature_kelvin",
          "Set the light color in kelvin. Lower temperatures look warmer; higher temperatures look cooler."}},
      {"light.color", {"ui.inspector.help.light.color",
          "Choose the emitted light color when Color mode is selected."}},
      {"light.intensity", {"ui.inspector.help.light.intensity",
          "Set the light’s brightness. Zero produces no light; larger values make it brighter."}},
      {"light.range", {"ui.inspector.help.light.range",
          "Set how far this point or spot light reaches in the scene."}},
      {"light.distance_attenuation", {"ui.inspector.help.light.distance_attenuation",
          "Control how the light’s brightness decreases with distance."}},
      {"light.cast_shadows", {"ui.inspector.help.light.cast_shadows",
          "Allow this light to cast shadows from objects in the scene."}},
      {"components.meshes", {"ui.inspector.help.components.meshes",
          "Number of selected objects that contain a mesh."}},
      {"components.lights", {"ui.inspector.help.components.lights",
          "Number of selected objects that contain a light."}},
  };
  for (const Entry &entry : kEntries) {
    if (entry.id == stable_id) {
      return entry.help;
    }
  }
  return {};
}

} // namespace quader
