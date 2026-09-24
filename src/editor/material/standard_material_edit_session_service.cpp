////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/standard_material_edit_session_service.hpp"

#include "core/release_features.hpp"
#include "core/qstr.hpp"
#include "editor/material/project_material_persistence.hpp"
#include "io/project/project_bridge_notifications.hpp"
#include "editor/session/project_session.hpp"

#include <algorithm>
#include <cmath>
#include <system_error>
#include <utility>

namespace quader {
namespace {

constexpr std::size_t slot_index(StandardMaterialTextureSlot slot) {
  return static_cast<std::size_t>(slot);
}

StandardMaterialEditResult failure(std::string message) {
  return {.message = std::move(message)};
}

StandardMaterialEditResult success(bool changed, std::string message = {}) {
  return {.ok = true, .changed = changed, .message = std::move(message)};
}

bool finite_unit(float value) {
  return std::isfinite(value) && value >= 0.0f && value <= 1.0f;
}

bool valid_material_path(const ProjectRelativePath &path) {
  if (!path.valid() ||
      QStr::to_ascii_lower(path.native_path().extension().string()) !=
          ".qmat") {
    return false;
  }
  const auto first = path.native_path().begin();
  return first != path.native_path().end() &&
         QStr::to_ascii_lower(first->string()) == "materials";
}

} // namespace

/// Owns mutable draft state and staged source images for one edit session.
struct StandardMaterialEditSessionService::Draft final {
  /// Owns the cancel checkpoint for the albedo field alone.
  struct BaseColorPreview final {
    std::array<float, 3> before;
    bool scene_preview_before = false;
  };
  /// Retains one failed dependency result until the texture identity changes.
  struct CachedPreviewError final {
    ProjectAssetId texture_id;
    ProjectIoError error;
  };

  StandardMaterialEditSnapshot snapshot;
  bool automatic_name = false;
  bool scene_color_preview = false;
  std::optional<BaseColorPreview> base_color_preview;
  std::array<std::optional<ProjectImportedImage>, 4> pending;
  std::array<std::optional<StandardMaterialTexturePreview>, 4>
      decoded_previews;
  std::array<std::optional<CachedPreviewError>, 4> preview_errors;
};

StandardMaterialEditSessionService::StandardMaterialEditSessionService(
    ProjectSession &project)
    : project_(&project) {}

StandardMaterialEditSessionService::~StandardMaterialEditSessionService() =
    default;

const StandardMaterialEditSnapshot *
StandardMaterialEditSessionService::current() const noexcept {
  return current_ ? &current_->snapshot : nullptr;
}

void StandardMaterialEditSessionService::retarget_saved_path(
    ProjectAssetId id, ProjectRelativePath path) {
  if (current_ && current_->snapshot.path.has_value() &&
      current_->snapshot.document.id == id && current_->snapshot.path != path) {
    current_->snapshot.path = std::move(path);
    advance_draft_revision();
    if (!current_->snapshot.dirty) {
      current_->snapshot.clean_revision = current_->snapshot.draft_revision;
    }
  }
}

void StandardMaterialEditSessionService::retarget_saved_material(
    ProjectAssetId id, ProjectRelativePath path, std::string saved_name,
    std::uint64_t source_content_revision) {
  if (!current_ || !current_->snapshot.path.has_value() ||
      current_->snapshot.document.id != id) {
    return;
  }
  StandardMaterialEditSnapshot &snapshot = current_->snapshot;
  if (snapshot.path == path && snapshot.document.name == saved_name &&
      snapshot.source_content_revision == source_content_revision) {
    return;
  }
  const bool was_clean = !snapshot.dirty;
  snapshot.path = std::move(path);
  snapshot.document.name = std::move(saved_name);
  snapshot.source_content_revision = source_content_revision;
  current_->automatic_name = false;
  advance_draft_revision();
  if (was_clean) {
    snapshot.clean_revision = snapshot.draft_revision;
  }
}

MaterialEditSessionId
StandardMaterialEditSessionService::allocate_session_id() {
  ++generation_;
  if (generation_ == 0u) {
    ++generation_;
  }
  return MaterialEditSessionId::from_generation(generation_);
}

void StandardMaterialEditSessionService::reset_with(
    ProjectMaterialDocument document,
    std::optional<ProjectRelativePath> path,
    std::uint64_t source_content_revision) {
  if (current_ && current_->scene_color_preview) {
    advance_scene_preview_revision();
  }
  Draft draft;
  draft.snapshot.session_id = allocate_session_id();
  draft.snapshot.document = std::move(document);
  draft.snapshot.path = std::move(path);
  draft.snapshot.source_content_revision = source_content_revision;
  draft.snapshot.draft_revision = 1u;
  draft.snapshot.clean_revision = 1u;
  current_ = std::make_unique<Draft>(std::move(draft));
}

StandardMaterialEditResult
StandardMaterialEditSessionService::new_material() {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (current_ && current_->snapshot.dirty) {
    return failure("Current Standard Material has unsaved changes.");
  }
  ProjectMaterialDocument document;
  auto name = default_project_material_name(project_->material_catalog());
  if (!name.ok()) {
    return failure(name.error().message);
  }
  document.id = ProjectAssetId::generate();
  document.name = name.take_value();
  document.kind = ProjectMaterialKind::Standard;
  document.material_version = kStandardMaterialVersion;
  document.payload = ProjectStandardMaterialDefinition{};
  reset_with(std::move(document), std::nullopt, 0u);
  current_->automatic_name = true;
  return success(true, "New Standard Material created.");
}

StandardMaterialEditResult
StandardMaterialEditSessionService::open(ProjectAssetId id) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (project_ != nullptr && project_->manifest() != nullptr &&
      project_->manifest()->is_material_deleted(id)) {
    return failure("This material was permanently deleted.");
  }
  if (current_ && current_->snapshot.document.id == id) {
    return success(false, "Standard Material is already open.");
  }
  if (current_ && current_->snapshot.dirty) {
    return failure("Current Standard Material has unsaved changes.");
  }
  if (project_ == nullptr || !project_->has_project()) {
    return failure("Open a project before editing materials.");
  }
  const ProjectMaterialCatalogEntry *entry =
      project_->material_catalog().find(id);
  if (entry == nullptr) {
    return failure("Project material is missing or unavailable.");
  }
  if (entry->material.kind != ProjectMaterialKind::Standard ||
      entry->material.standard_material() == nullptr) {
    return failure("The selected QMAT is not a supported Standard Material.");
  }
  reset_with(entry->material, entry->path, entry->content_revision);
  return success(true, "Standard Material opened.");
}

StandardMaterialEditResult
StandardMaterialEditSessionService::open(ProjectRelativePath path) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (project_ == nullptr || !project_->has_project()) {
    return failure("Open a project before editing materials.");
  }
  const auto found = std::ranges::find_if(
      project_->material_catalog().entries(),
      [&path](const ProjectMaterialCatalogEntry &entry) {
        return entry.path == path;
      });
  if (found == project_->material_catalog().entries().end()) {
    return failure("Selected QMAT is not present in the project catalog.");
  }
  return open(found->material.id);
}

StandardMaterialEditResult
StandardMaterialEditSessionService::close(bool discard_dirty) {
  if (!current_) {
    return success(false, "No Standard Material is open.");
  }
  if (current_->snapshot.dirty && !discard_dirty) {
    return failure("Standard Material contains unsaved changes.");
  }
  if (current_->scene_color_preview) {
    advance_scene_preview_revision();
  }
  current_.reset();
  return success(true, "Standard Material closed.");
}

ProjectStandardMaterialDefinition *
StandardMaterialEditSessionService::standard() noexcept {
  return current_ ? current_->snapshot.document.standard_material() : nullptr;
}

StandardMaterialEditResult
StandardMaterialEditSessionService::mark_changed() {
  if (!current_) {
    return failure("No Standard Material is open.");
  }
  current_->snapshot.dirty = true;
  advance_draft_revision();
  return success(true, "Standard Material changed.");
}

void StandardMaterialEditSessionService::advance_draft_revision() {
  ++current_->snapshot.draft_revision;
  if (current_->snapshot.draft_revision == 0u) {
    ++current_->snapshot.draft_revision;
  }
}

void StandardMaterialEditSessionService::advance_scene_preview_revision() {
  ++scene_preview_revision_;
  if (scene_preview_revision_ == 0u) {
    ++scene_preview_revision_;
  }
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_name(std::string name) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (!current_) {
    return failure("No Standard Material is open.");
  }
  if (name.empty() || name.size() > 256u) {
    return failure("Material name must contain 1 to 256 characters.");
  }
  current_->automatic_name = false;
  if (current_->snapshot.document.name == name) {
    return success(false);
  }
  current_->snapshot.document.name = std::move(name);
  return mark_changed();
}

StandardMaterialEditResult StandardMaterialEditSessionService::set_base_color(
    std::array<float, 3> color) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (current_ && current_->base_color_preview.has_value()) {
    return failure("Finish the active albedo preview before editing its color.");
  }
  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr || !finite_unit(color[0]) ||
      !finite_unit(color[1]) || !finite_unit(color[2])) {
    return failure("Albedo values must be finite values from 0 to 1.");
  }
  if (material->base_color == color) {
    return success(false);
  }
  material->base_color = color;
  current_->scene_color_preview = true;
  advance_scene_preview_revision();
  return mark_changed();
}

StandardMaterialEditResult
StandardMaterialEditSessionService::begin_base_color_preview(
    MaterialEditSessionId session_id) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (!current_ || current_->snapshot.session_id != session_id) {
    return failure("The Standard Material edit session has expired.");
  }
  if (current_->base_color_preview.has_value()) {
    return failure("An albedo preview is already active.");
  }
  current_->base_color_preview = Draft::BaseColorPreview{
      .before = standard()->base_color,
      .scene_preview_before = current_->scene_color_preview,
  };
  return success(false);
}

StandardMaterialEditResult
StandardMaterialEditSessionService::update_base_color_preview(
    MaterialEditSessionId session_id, std::array<float, 3> color) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (!current_ || current_->snapshot.session_id != session_id ||
      !current_->base_color_preview.has_value()) {
    return failure("The Standard Material albedo preview has expired.");
  }
  if (!finite_unit(color[0]) || !finite_unit(color[1]) ||
      !finite_unit(color[2])) {
    return failure("Albedo values must be finite values from 0 to 1.");
  }
  if (standard()->base_color == color) {
    return success(false);
  }
  standard()->base_color = color;
  const Draft::BaseColorPreview &preview = *current_->base_color_preview;
  current_->scene_color_preview =
      preview.scene_preview_before || color != preview.before;
  advance_draft_revision();
  advance_scene_preview_revision();
  return success(true);
}

StandardMaterialEditResult
StandardMaterialEditSessionService::finish_base_color_preview(
    MaterialEditSessionId session_id, bool accept) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (!current_ || current_->snapshot.session_id != session_id ||
      !current_->base_color_preview.has_value()) {
    return failure("The Standard Material albedo preview has expired.");
  }
  const Draft::BaseColorPreview preview = *current_->base_color_preview;
  current_->base_color_preview.reset();
  const bool changed = standard()->base_color != preview.before;
  if (accept && changed) {
    return mark_changed();
  }
  const bool scene_changed =
      current_->scene_color_preview != preview.scene_preview_before;
  standard()->base_color = preview.before;
  current_->scene_color_preview = preview.scene_preview_before;
  if (changed) {
    advance_draft_revision();
  }
  if (changed || scene_changed) {
    advance_scene_preview_revision();
  }
  return success(changed || scene_changed);
}

std::optional<std::array<float, 3>>
StandardMaterialEditSessionService::scene_base_color_preview(
    ProjectAssetId id) const noexcept {
  if (!current_ || !current_->scene_color_preview ||
      current_->snapshot.document.id != id) {
    return std::nullopt;
  }
  const ProjectStandardMaterialDefinition *material =
      current_->snapshot.document.standard_material();
  return material != nullptr ? std::optional(material->base_color)
                             : std::nullopt;
}

StandardMaterialEditResult
StandardMaterialEditSessionService::cancel_base_color_preview() {
  return current_ && current_->base_color_preview.has_value()
             ? finish_base_color_preview(current_->snapshot.session_id, false)
             : success(false);
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_roughness(float value) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr || !finite_unit(value)) {
    return failure("Roughness must be a finite value from 0 to 1.");
  }
  if (material->roughness == value) {
    return success(false);
  }
  material->roughness = value;
  return mark_changed();
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_metallic(float value) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr || !finite_unit(value)) {
    return failure("Metallic must be a finite value from 0 to 1.");
  }
  if (material->metallic == value) {
    return success(false);
  }
  material->metallic = value;
  return mark_changed();
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_normal_strength(float value) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr ||
      !is_valid_standard_material_normal_strength(value)) {
    return failure(
        "Normal strength must be a finite value from -16 to 16.");
  }
  if (material->normal_strength == value) {
    return success(false);
  }
  material->normal_strength = value;
  return mark_changed();
}

StandardMaterialEditResult StandardMaterialEditSessionService::set_scalar(
    StandardMaterialScalar scalar, float value) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  switch (scalar) {
  case StandardMaterialScalar::Roughness:
    return set_roughness(value);
  case StandardMaterialScalar::Metallic:
    return set_metallic(value);
  case StandardMaterialScalar::NormalStrength:
    return set_normal_strength(value);
  }
  return failure("The Standard Material scalar is invalid.");
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_normal_convention(
    NormalMapConvention convention) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr) {
    return failure("No Standard Material is open.");
  }
  if (material->normal_convention == convention) {
    return success(false);
  }
  material->normal_convention = convention;
  return mark_changed();
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_diffuse_model(MaterialDiffuseModel model) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (!is_valid_material_diffuse_model(model)) {
    return failure("The Standard Material diffuse model is invalid.");
  }
  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr) {
    return failure("No Standard Material is open.");
  }
  if (material->diffuse_model == model) {
    return success(false);
  }
  material->diffuse_model = model;
  return mark_changed();
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_texture_transform(
    StandardMaterialTextureTransform transform) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr ||
      !is_valid_standard_material_texture_transform(transform)) {
    return failure("Texture transform values are outside their finite ranges.");
  }
  if (material->texture_transform == transform) {
    return success(false);
  }
  material->texture_transform = transform;
  return mark_changed();
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_texture_transform_component(
    StandardMaterialTextureTransformComponent component, float value) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr) {
    return failure("No Standard Material is open.");
  }
  StandardMaterialTextureTransform transform = material->texture_transform;
  switch (component) {
  case StandardMaterialTextureTransformComponent::CenterU:
    transform.center[0] = value;
    break;
  case StandardMaterialTextureTransformComponent::CenterV:
    transform.center[1] = value;
    break;
  case StandardMaterialTextureTransformComponent::ScaleU:
    transform.scale[0] = value;
    break;
  case StandardMaterialTextureTransformComponent::ScaleV:
    transform.scale[1] = value;
    break;
  case StandardMaterialTextureTransformComponent::OffsetU:
    transform.offset[0] = value;
    break;
  case StandardMaterialTextureTransformComponent::OffsetV:
    transform.offset[1] = value;
    break;
  case StandardMaterialTextureTransformComponent::RotationDegrees:
    transform.rotation_degrees = value;
    break;
  }
  return set_texture_transform(transform);
}

StandardMaterialEditResult StandardMaterialEditSessionService::import_texture(
    StandardMaterialTextureSlot slot,
    const std::filesystem::path &source_path) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr || slot_index(slot) >= 4u) {
    return failure("No Standard Material is open.");
  }
  ProjectIoResult<ProjectImportedImage> imported =
      ProjectImageDecoder::read_import(source_path);
  if (!imported.ok()) {
    return failure(imported.error().message);
  }
  ProjectImportedImage value = imported.take_value();
  if (slot == StandardMaterialTextureSlot::Albedo &&
      current_->automatic_name) {
    auto name = default_project_material_name(project_->material_catalog(),
                                              value.source_stem);
    if (!name.ok()) {
      return failure(name.error().message);
    }
    current_->snapshot.document.name = name.take_value();
  }
  current_->decoded_previews[slot_index(slot)] =
      StandardMaterialTexturePreview{
          .color_space = slot == StandardMaterialTextureSlot::Albedo
                             ? ProjectTextureColorSpace::Srgb
                             : ProjectTextureColorSpace::Linear,
          .content_revision = current_->snapshot.draft_revision + 1u,
          .rgba8 = std::make_shared<const std::vector<std::byte>>(
              std::move(value.decoded.rgba8)),
          .width = value.decoded.width,
          .height = value.decoded.height,
          .detected_format = value.decoded.detected_format,
          .encoded_byte_size = value.decoded.encoded_byte_size,
          .has_alpha_channel = value.decoded.has_alpha_channel,
      };
  current_->preview_errors[slot_index(slot)].reset();
  current_->pending[slot_index(slot)] = std::move(value);
  current_->snapshot.pending_texture_sources[slot_index(slot)] = source_path;
  return mark_changed();
}

StandardMaterialEditResult StandardMaterialEditSessionService::clear_texture(
    StandardMaterialTextureSlot slot) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr || slot_index(slot) >= 4u) {
    return failure("No Standard Material is open.");
  }
  std::optional<ProjectMaterialTextureSlot> *texture = material->texture(slot);
  const bool had_pending = current_->pending[slot_index(slot)].has_value();
  if (texture == nullptr || (!texture->has_value() && !had_pending)) {
    return success(false);
  }
  if (slot == StandardMaterialTextureSlot::Albedo &&
      current_->automatic_name) {
    auto name = default_project_material_name(project_->material_catalog());
    if (!name.ok()) {
      return failure(name.error().message);
    }
    current_->snapshot.document.name = name.take_value();
  }
  texture->reset();
  current_->pending[slot_index(slot)].reset();
  current_->decoded_previews[slot_index(slot)].reset();
  current_->preview_errors[slot_index(slot)].reset();
  current_->snapshot.pending_texture_sources[slot_index(slot)].reset();
  return mark_changed();
}

ProjectIoResult<std::optional<StandardMaterialTexturePreview>>
StandardMaterialEditSessionService::texture_preview(
    StandardMaterialTextureSlot slot) {
  if (!current_ || slot_index(slot) >= 4u) {
    return ProjectIoResult<
        std::optional<StandardMaterialTexturePreview>>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "No Standard Material is open.");
  }
  const std::size_t index = slot_index(slot);
  if (current_->pending[index].has_value()) {
    return ProjectIoResult<
        std::optional<StandardMaterialTexturePreview>>::success(
        current_->decoded_previews[index]);
  }
  ProjectStandardMaterialDefinition *material = standard();
  std::optional<ProjectMaterialTextureSlot> *definition =
      material != nullptr ? material->texture(slot) : nullptr;
  if (definition == nullptr || !definition->has_value()) {
    return ProjectIoResult<
        std::optional<StandardMaterialTexturePreview>>::success(std::nullopt);
  }
  if (current_->decoded_previews[index].has_value() &&
      current_->decoded_previews[index]->id == (*definition)->id) {
    return ProjectIoResult<
        std::optional<StandardMaterialTexturePreview>>::success(
        current_->decoded_previews[index]);
  }
  if (current_->preview_errors[index].has_value() &&
      current_->preview_errors[index]->texture_id == (*definition)->id) {
    const ProjectIoError &cached = current_->preview_errors[index]->error;
    return ProjectIoResult<
        std::optional<StandardMaterialTexturePreview>>::failure(
        cached.code, cached.message, cached.path);
  }
  ProjectIoResult<ProjectMaterialTextureData> texture =
      project_->material_texture(current_->snapshot.document.id, slot);
  if (!texture.ok()) {
    current_->preview_errors[index] = Draft::CachedPreviewError{
        .texture_id = (*definition)->id,
        .error = texture.error(),
    };
    return ProjectIoResult<
        std::optional<StandardMaterialTexturePreview>>::failure(
        texture.error().code, texture.error().message, texture.error().path);
  }
  ProjectIoResult<ProjectDecodedImage> decoded =
      ProjectImageDecoder::decode(texture.value().encoded_bytes);
  if (!decoded.ok()) {
    current_->preview_errors[index] = Draft::CachedPreviewError{
        .texture_id = (*definition)->id,
        .error = decoded.error(),
    };
    return ProjectIoResult<
        std::optional<StandardMaterialTexturePreview>>::failure(
        decoded.error().code, decoded.error().message, decoded.error().path);
  }
  current_->preview_errors[index].reset();
  ProjectDecodedImage image = decoded.take_value();
  current_->decoded_previews[index] = StandardMaterialTexturePreview{
      .id = texture.value().id,
      .color_space = texture.value().color_space,
      .content_revision = texture.value().content_revision,
      .rgba8 = std::make_shared<const std::vector<std::byte>>(
          std::move(image.rgba8)),
      .width = image.width,
      .height = image.height,
      .detected_format = std::move(image.detected_format),
      .encoded_byte_size = image.encoded_byte_size,
      .has_alpha_channel = image.has_alpha_channel,
  };
  return ProjectIoResult<
      std::optional<StandardMaterialTexturePreview>>::success(
      current_->decoded_previews[index]);
}

StandardMaterialEditResult StandardMaterialEditSessionService::save() {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (!current_) {
    return failure("No Standard Material is open.");
  }
  if (!current_->snapshot.path.has_value()) {
    return failure("Unsaved Standard Material requires Save As.");
  }
  return save_to(*current_->snapshot.path, false);
}

StandardMaterialEditResult
StandardMaterialEditSessionService::save_as(ProjectRelativePath path) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  return save_to(std::move(path), true);
}

StandardMaterialEditResult StandardMaterialEditSessionService::save_to(
    ProjectRelativePath path, bool save_as) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }

  if (current_ && current_->base_color_preview.has_value()) {
    return failure("Finish the albedo preview before saving the material.");
  }
  if (!current_ || project_ == nullptr || !project_->has_project() ||
      project_->workspace() == nullptr) {
    return failure("Open a project and Standard Material before saving.");
  }
  if (project_->manifest()->is_material_deleted(current_->snapshot.document.id)) {
    return failure("This material was permanently deleted and cannot be saved.");
  }
  if (!valid_material_path(path)) {
    return failure("Standard Materials must be saved below Materials/ as .qmat files.");
  }
  const ProjectFileSystem &file_system = project_->workspace()->file_system;
  ProjectIoResult<std::filesystem::path> qmat_absolute =
      file_system.resolve_for_write(path);
  if (!qmat_absolute.ok()) {
    return failure(qmat_absolute.error().message);
  }
  std::error_code error;
  const bool target_exists =
      std::filesystem::exists(qmat_absolute.value(), error);
  if (error) {
    return failure("Could not inspect Standard Material destination: " +
                   error.message());
  }
  if (save_as && target_exists) {
    return failure("Save As cannot overwrite another project material.");
  }

  const bool replace_existing_qmat = !save_as && target_exists;
  if (replace_existing_qmat) {
    ProjectIoResult<QmatDecodedFile> source = QmatCodec::read(file_system, path);
    if (!source.ok()) {
      return failure(source.error().message);
    }
    if (source.value().content_revision !=
        current_->snapshot.source_content_revision) {
      return failure("Standard Material changed on disk; reopen it before saving.");
    }
  }

  ProjectMaterialDocument document = current_->snapshot.document;
  if (save_as) {
    document.id = ProjectAssetId::generate();
  }
  ProjectStandardMaterialDefinition *material = document.standard_material();
  if (material == nullptr) {
    return failure("Only Standard Material drafts can be saved here.");
  }

  ProjectMaterialFileRollback rollback;
  const auto fail_with_rollback = [&rollback](std::string message) {
    const std::string rollback_message = rollback.rollback();
    if (!rollback_message.empty()) {
      message += "; " + rollback_message;
    }
    return failure(std::move(message));
  };
  for (std::size_t index = 0u; index < current_->pending.size(); ++index) {
    if (!current_->pending[index].has_value()) {
      continue;
    }
    const StandardMaterialTextureSlot slot =
        static_cast<StandardMaterialTextureSlot>(index);
    const ProjectImportedImage &imported = *current_->pending[index];
    ProjectIoResult<ProjectMaterialAssetDestination> destination =
        make_unique_texture_destination(file_system, imported.source_stem,
                                        imported.source_extension);
    if (!destination.ok()) {
      return fail_with_rollback(destination.error().message);
    }
    ProjectIoResult<std::filesystem::path> texture_absolute =
        rollback.prepare_destination(file_system, destination.value().path);
    if (!texture_absolute.ok()) {
      return fail_with_rollback(texture_absolute.error().message);
    }
    ProjectIoResult<void> written =
        rollback.create_bytes(texture_absolute.value(),
                              imported.encoded_bytes);
    if (!written.ok()) {
      return fail_with_rollback(written.error().message);
    }
    std::optional<ProjectMaterialTextureSlot> *target = material->texture(slot);
    if (target == nullptr) {
      return fail_with_rollback(
          "Unsupported Standard Material texture slot.");
    }
    *target = ProjectMaterialTextureSlot{
        .id = destination.value().id,
        .project_relative_path = destination.value().path.portable_string(),
        .color_space = slot == StandardMaterialTextureSlot::Albedo
                           ? ProjectTextureColorSpace::Srgb
                           : ProjectTextureColorSpace::Linear,
    };
  }

  ProjectIoResult<void> qmat_written =
      replace_existing_qmat
          ? rollback.replace_qmat(qmat_absolute.value(), document)
          : rollback.create_qmat(qmat_absolute.value(), document);
  if (!qmat_written.ok()) {
    return fail_with_rollback(qmat_written.error().message);
  }
  ProjectIoResult<ProjectMaterialCatalog> scanned =
      ProjectMaterialCatalog::scan(file_system);
  if (!scanned.ok()) {
    return fail_with_rollback(scanned.error().message);
  }
  ProjectSessionResult installed = project_->install_material_catalog(
      scanned.take_value());
  if (!installed.ok) {
    return fail_with_rollback(installed.message);
  }
  const ProjectMaterialCatalogEntry *saved =
      project_->material_catalog().find(document.id);
  if (saved == nullptr) {
    return fail_with_rollback(
        "Saved Standard Material is missing from the catalog.");
  }
  rollback.commit();
  if (current_->scene_color_preview) {
    current_->scene_color_preview = false;
    advance_scene_preview_revision();
  }
  current_->snapshot.document = saved->material;
  current_->snapshot.path = saved->path;
  current_->snapshot.source_content_revision = saved->content_revision;
  current_->snapshot.dirty = false;
  ++current_->snapshot.draft_revision;
  if (current_->snapshot.draft_revision == 0u) {
    ++current_->snapshot.draft_revision;
  }
  current_->snapshot.clean_revision = current_->snapshot.draft_revision;
  current_->pending.fill(std::nullopt);
  current_->decoded_previews.fill(std::nullopt);
  current_->preview_errors.fill(std::nullopt);
  current_->snapshot.pending_texture_sources.fill(std::nullopt);
  StandardMaterialEditResult result =
      success(true, save_as ? "Standard Material saved as a new asset."
                            : "Standard Material saved.");
  const auto notification = ProjectBridgeNotifications::publish_material(
      file_system, project_->manifest()->id(), *saved);
  const std::string &warning = notification.ok()
                                   ? notification.warning()
                                   : notification.error().message;
  if (!warning.empty()) {
    result.message += " " + warning;
  }
  return result;
}

} // namespace quader
