////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/standard_material_edit_session_service.hpp"

#include "core/qstr.hpp"
#include "editor/material/project_material_persistence.hpp"
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
  /// Retains one failed dependency result until the texture identity changes.
  struct CachedPreviewError final {
    ProjectAssetId texture_id;
    ProjectIoError error;
  };

  StandardMaterialEditSnapshot snapshot;
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
  if (current_ && current_->snapshot.dirty) {
    return failure("Current Standard Material has unsaved changes.");
  }
  ProjectMaterialDocument document;
  document.id = ProjectAssetId::generate();
  document.name = "Untitled Material";
  document.kind = ProjectMaterialKind::Standard;
  document.material_version = kStandardMaterialVersion;
  document.payload = ProjectStandardMaterialDefinition{};
  reset_with(std::move(document), std::nullopt, 0u);
  return success(true, "New Standard Material created.");
}

StandardMaterialEditResult
StandardMaterialEditSessionService::open(ProjectAssetId id) {
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
  ++current_->snapshot.draft_revision;
  if (current_->snapshot.draft_revision == 0u) {
    ++current_->snapshot.draft_revision;
  }
  return success(true, "Standard Material changed.");
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_name(std::string name) {
  if (!current_) {
    return failure("No Standard Material is open.");
  }
  if (name.empty() || name.size() > 256u) {
    return failure("Material name must contain 1 to 256 characters.");
  }
  if (current_->snapshot.document.name == name) {
    return success(false);
  }
  current_->snapshot.document.name = std::move(name);
  return mark_changed();
}

StandardMaterialEditResult StandardMaterialEditSessionService::set_base_color(
    std::array<float, 3> color) {
  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr || !finite_unit(color[0]) ||
      !finite_unit(color[1]) || !finite_unit(color[2])) {
    return failure("Albedo values must be finite values from 0 to 1.");
  }
  if (material->base_color == color) {
    return success(false);
  }
  material->base_color = color;
  return mark_changed();
}

StandardMaterialEditResult
StandardMaterialEditSessionService::set_roughness(float value) {
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
StandardMaterialEditSessionService::set_texture_transform(
    StandardMaterialTextureTransform transform) {
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
  current_->decoded_previews[slot_index(slot)] =
      StandardMaterialTexturePreview{
          .color_space = slot == StandardMaterialTextureSlot::Albedo
                             ? ProjectTextureColorSpace::Srgb
                             : ProjectTextureColorSpace::Linear,
          .content_revision = current_->snapshot.draft_revision + 1u,
          .rgba8 = std::make_shared<const std::vector<std::byte>>(
              value.decoded.rgba8),
          .width = value.decoded.width,
          .height = value.decoded.height,
          .detected_format = value.decoded.detected_format,
          .encoded_byte_size = value.encoded_bytes.size(),
      };
  current_->preview_errors[slot_index(slot)].reset();
  current_->pending[slot_index(slot)] = std::move(value);
  current_->snapshot.pending_texture_sources[slot_index(slot)] = source_path;
  return mark_changed();
}

StandardMaterialEditResult StandardMaterialEditSessionService::clear_texture(
    StandardMaterialTextureSlot slot) {
  ProjectStandardMaterialDefinition *material = standard();
  if (material == nullptr || slot_index(slot) >= 4u) {
    return failure("No Standard Material is open.");
  }
  std::optional<ProjectMaterialTextureSlot> *texture = material->texture(slot);
  const bool had_pending = current_->pending[slot_index(slot)].has_value();
  if (texture == nullptr || (!texture->has_value() && !had_pending)) {
    return success(false);
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
  current_->decoded_previews[index] = StandardMaterialTexturePreview{
      .id = texture.value().id,
      .color_space = texture.value().color_space,
      .content_revision = texture.value().content_revision,
      .rgba8 = std::make_shared<const std::vector<std::byte>>(
          decoded.value().rgba8),
      .width = decoded.value().width,
      .height = decoded.value().height,
      .detected_format = decoded.value().detected_format,
      .encoded_byte_size = texture.value().encoded_bytes.size(),
  };
  return ProjectIoResult<
      std::optional<StandardMaterialTexturePreview>>::success(
      current_->decoded_previews[index]);
}

StandardMaterialEditResult StandardMaterialEditSessionService::save() {
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
  return save_to(std::move(path), true);
}

StandardMaterialEditResult StandardMaterialEditSessionService::save_to(
    ProjectRelativePath path, bool save_as) {
  if (!current_ || project_ == nullptr || !project_->has_project() ||
      project_->workspace() == nullptr) {
    return failure("Open a project and Standard Material before saving.");
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
        file_system.resolve_for_write(destination.value().path);
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
  const bool activate = save_as || !current_->snapshot.path.has_value();
  ProjectSessionResult installed = project_->install_material_catalog(
      scanned.take_value(), activate ? std::optional(document.id)
                                     : std::nullopt);
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
  return success(true, save_as ? "Standard Material saved as a new asset."
                               : "Standard Material saved.");
}

} // namespace quader
