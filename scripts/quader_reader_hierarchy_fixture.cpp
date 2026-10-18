// Generates the reader's deterministic native hierarchy regression fixture.
#include "assets/material_document.hpp"
#include "io/project/project_manifest.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "scene/document/scene_content_document.hpp"
#include "quader_bridge_reader.hpp"

#include <quader/modeling/neutral/polygon/document.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using namespace quader;

void require(bool value, const std::string &message) {
  if (!value) throw std::runtime_error(message);
}

void checked(int value, const QuaderBridgeError &error) {
  require(value != 0, error.message);
}

MeshAsset triangle() {
  MeshAsset mesh;
  mesh.name = "Hierarchy triangle";
  auto &geometry = mesh.edit_geometry();
  geometry.vertices = {
      {.position = {0, 0, 0}, .normal = {0, 0, 1}, .uv = {0, 0}},
      {.position = {1, 0, 0}, .normal = {0, 0, 1}, .uv = {1, 0}},
      {.position = {0, 1, 0}, .normal = {0, 0, 1}, .uv = {0, 1}}};
  geometry.indices = {0, 1, 2};
  auto polygon = std::make_shared<quader_poly::Document>();
  polygon->next_vertex_id = 4;
  polygon->next_face_id = 2;
  polygon->vertices = {{.id = 1, .position = {0, 0, 0}},
                       {.id = 2, .position = {1, 0, 0}},
                       {.id = 3, .position = {0, 1, 0}}};
  quader_poly::Face face;
  face.id = 1;
  face.vertices = {1, 2, 3};
  face.uvs = {{0, 0}, {1, 0}, {0, 1}};
  face.normal_shading = quader_poly::SurfaceShading::Authored;
  polygon->faces.push_back(std::move(face));
  mesh.has_authored_polygon_document = true;
  mesh.authored_polygon_document = std::move(polygon);
  return mesh;
}

void generate(const std::filesystem::path &root) {
  require(!std::filesystem::exists(root), "Fixture output directory already exists.");
  std::filesystem::create_directories(root);
  auto created = ProjectManifestStore::create_in_empty_directory(root);
  require(created.ok(), "Cannot create fixture workspace.");
  auto workspace = created.take_value();
  auto manifest = ProjectManifest::create(
      ProjectId::parse("77777777-7777-4777-8777-777777777777").take_value(),
      root.filename().string());
  require(manifest.ok(), "Cannot create fixture manifest.");
  workspace.manifest = manifest.take_value();
  SceneContentDocument document({.id = 7, .name = "Hierarchy fixture"});
  require(document.assets().add_mesh(1, triangle()) == 1, "Cannot add triangle.");
  require(document.assets().add_material(
              2, make_default_quader_material("Hierarchy material")) == 2,
          "Cannot add material.");
  auto &scene = document.scene();
  const auto add = [&](EntityId id, const char *name, EntityKind kind,
                        EntityId parent, QVec3 location) {
    require(scene.create_entity_with_id(id, name, kind) == id,
            "Cannot create fixture entity.");
    require(scene.set_transform(id, {.location = location}), "Cannot place entity.");
    if (parent != 0) require(scene.set_parent(id, parent, false), "Cannot parent entity.");
    if (kind == EntityKind::Mesh) {
      require(scene.set_mesh(id, {.mesh = 1, .collision_type = MeshCollisionType::Mesh}),
              "Cannot bind mesh.");
      require(scene.set_material(id, {.material = 2}), "Cannot bind material.");
    }
  };
  add(10, "Level Group", EntityKind::Empty, 0, {10, 0, 0});
  add(12, "Wall B", EntityKind::Mesh, 10, {2, 0, 0});
  add(11, "Nested Group", EntityKind::Empty, 10, {0, 3, 0});
  add(13, "Wall A", EntityKind::Mesh, 11, {0, 0, 4});
  add(14, "Window Light", EntityKind::Light, 11, {1, 2, 0});
  require(scene.set_light(14, {.type = LightType::Point}), "Cannot bind light.");
  require(scene.set_visible(10, false), "Cannot hide parent.");
  add(40, "Empty Group", EntityKind::Empty, 0, {0, 0, 0});
  add(100, "Stored Definition", EntityKind::Empty, 0, {100, 0, 0});
  add(101, "Assembly Inner", EntityKind::Empty, 100, {2, 0, 0});
  add(102, "Arch", EntityKind::Mesh, 101, {3, 0, 0});
  add(103, "Column", EntityKind::Mesh, 100, {0, 0, 5});
  require(scene.set_instance_definition(100), "Cannot define instance.");
  add(50, "Instance One", EntityKind::Instance, 10, {20, 0, 0});
  require(scene.set_instance_occurrence(50, {.definition = 100}), "Cannot bind occurrence.");
  add(60, "Instance Two", EntityKind::Instance, 0, {-5, 0, 0});
  require(scene.set_instance_occurrence(60, {.definition = 100}), "Cannot bind occurrence.");
  const auto scene_id = SceneId::parse("88888888-8888-4888-8888-888888888888").take_value();
  std::filesystem::create_directories(root / "Scenes");
  const auto written = QdrDocumentWriter::write_project_document_atomic(
      root / "Scenes/hierarchy.qdr", document, scene_id.bytes());
  require(written.ok, written.message);
  const auto path = ProjectRelativePath::parse_portable("Scenes/hierarchy.qdr").take_value();
  require(workspace.manifest.register_scene({.id = scene_id, .path = path}).ok(),
          "Cannot register fixture scene.");
  require(ProjectManifestStore::save_atomic(workspace).ok(), "Cannot save fixture manifest.");

  const auto *api = quader_bridge_get_api(QUADER_BRIDGE_ABI_VERSION);
  QuaderBridgeError error{};
  QuaderBridgeProject *project = nullptr;
  const auto manifest_utf8 = workspace.manifest_path.generic_u8string();
  require(api != nullptr, "Reader ABI is unavailable.");
  checked(api->open_project(
      reinterpret_cast<const char *>(manifest_utf8.c_str()), &project, &error), error);
  QuaderBridgeScene *snapshot = nullptr;
  checked(api->open_scene(project, scene_id.str().data(), &snapshot, &error), error);
  QuaderBridgeSceneView view{};
  checked(api->scene_view(snapshot, &view, &error), error);
  require(view.node_count == 14 && view.mesh_count == 6 && view.light_count == 1,
          "Unexpected native hierarchy fixture counts.");
  for (uint32_t index = 0; index < view.node_count; ++index) {
    QuaderBridgeNodeView node{};
    checked(api->node_view(snapshot, index, &node, &error), error);
    std::cout << node.order << " " << node.name << " entity=" << node.entity
              << " occurrence=" << node.occurrence_entity << " parent="
              << node.parent_entity << ":" << node.parent_occurrence_entity
              << " visible=" << node.visible << " world=" << node.transform[12]
              << "," << node.transform[13] << "," << node.transform[14] << '\n';
  }
  api->release_scene(snapshot);
  api->release_project(project);
}
} // namespace

int main(int argc, char **argv) {
  try {
    require(argc == 2, "Expected a fresh output directory.");
    generate(std::filesystem::path(argv[1]));
    return 0;
  } catch (const std::exception &error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
