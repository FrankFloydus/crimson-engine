////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_standard_material_documents_api.hpp"

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_api_status_internal.hpp"
#include "api/quader_events_api.hpp"
#include "api/quader_events_api_internal_access.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"

#include <utility>

namespace quader {
namespace {

QuaderProjectTextureColorSpace
to_public_color_space(ProjectTextureColorSpace color_space) {
  return color_space == ProjectTextureColorSpace::Linear
             ? QuaderProjectTextureColorSpace::Linear
             : QuaderProjectTextureColorSpace::Srgb;
}

QuaderNormalMapConvention
to_public_normal_convention(NormalMapConvention convention) {
  return convention == NormalMapConvention::DirectX
             ? QuaderNormalMapConvention::DirectX
             : QuaderNormalMapConvention::OpenGL;
}

std::optional<NormalMapConvention>
to_internal_normal_convention(QuaderNormalMapConvention convention) {
  switch (convention) {
  case QuaderNormalMapConvention::OpenGL:
    return NormalMapConvention::OpenGL;
  case QuaderNormalMapConvention::DirectX:
    return NormalMapConvention::DirectX;
  }
  return std::nullopt;
}

std::optional<StandardMaterialTextureSlot>
to_internal_texture_slot(QuaderStandardMaterialTextureSlot slot) {
  switch (slot) {
  case QuaderStandardMaterialTextureSlot::Albedo:
    return StandardMaterialTextureSlot::Albedo;
  case QuaderStandardMaterialTextureSlot::Roughness:
    return StandardMaterialTextureSlot::Roughness;
  case QuaderStandardMaterialTextureSlot::Metallic:
    return StandardMaterialTextureSlot::Metallic;
  case QuaderStandardMaterialTextureSlot::Normal:
    return StandardMaterialTextureSlot::Normal;
  }
  return std::nullopt;
}

std::optional<StandardMaterialScalar>
to_internal_scalar(QuaderStandardMaterialScalar scalar) {
  switch (scalar) {
  case QuaderStandardMaterialScalar::Roughness:
    return StandardMaterialScalar::Roughness;
  case QuaderStandardMaterialScalar::Metallic:
    return StandardMaterialScalar::Metallic;
  case QuaderStandardMaterialScalar::NormalStrength:
    return StandardMaterialScalar::NormalStrength;
  }
  return std::nullopt;
}

std::optional<StandardMaterialTextureTransformComponent>
to_internal_texture_transform_component(
    QuaderStandardMaterialTextureTransformComponent component) {
  using Public = QuaderStandardMaterialTextureTransformComponent;
  using Internal = StandardMaterialTextureTransformComponent;
  switch (component) {
  case Public::CenterU:
    return Internal::CenterU;
  case Public::CenterV:
    return Internal::CenterV;
  case Public::ScaleU:
    return Internal::ScaleU;
  case Public::ScaleV:
    return Internal::ScaleV;
  case Public::OffsetU:
    return Internal::OffsetU;
  case Public::OffsetV:
    return Internal::OffsetV;
  case Public::RotationDegrees:
    return Internal::RotationDegrees;
  }
  return std::nullopt;
}

std::optional<QuaderStandardMaterialTextureReference> to_public_texture(
    const std::optional<ProjectMaterialTextureSlot> &texture) {
  if (!texture.has_value()) {
    return std::nullopt;
  }
  return QuaderStandardMaterialTextureReference{
      .id = texture->id,
      .project_relative_path = texture->project_relative_path,
      .color_space = to_public_color_space(texture->color_space),
  };
}

QuaderStandardMaterialTextureTransform to_public_texture_transform(
    const StandardMaterialTextureTransform &transform) {
  return {
      .center = transform.center,
      .scale = transform.scale,
      .offset = transform.offset,
      .rotation_degrees = transform.rotation_degrees,
  };
}

StandardMaterialTextureTransform to_internal_texture_transform(
    const QuaderStandardMaterialTextureTransform &transform) {
  return {
      .center = transform.center,
      .scale = transform.scale,
      .offset = transform.offset,
      .rotation_degrees = transform.rotation_degrees,
  };
}

QuaderStandardMaterialDefinition to_public_material(
    const ProjectStandardMaterialDefinition &material) {
  return {
      .base_color = material.base_color,
      .roughness = material.roughness,
      .metallic = material.metallic,
      .normal_strength = material.normal_strength,
      .normal_convention =
          to_public_normal_convention(material.normal_convention),
      .texture_transform =
          to_public_texture_transform(material.texture_transform),
      .base_color_texture = to_public_texture(material.base_color_texture),
      .roughness_texture = to_public_texture(material.roughness_texture),
      .metallic_texture = to_public_texture(material.metallic_texture),
      .normal_texture = to_public_texture(material.normal_texture),
  };
}

QuaderApiVoidResult status(QuaderApiSession session, bool ok, bool changed,
                           std::string message = {}) {
  return quader_api_void_status(
      QuaderApiSessionInternalAccess::scene_session(session), ok, changed,
      std::move(message),
      changed ? QuaderMutationKind::Runtime : QuaderMutationKind::None);
}

QuaderApiVoidResult status(QuaderApiSession session,
                           const StandardMaterialEditResult &result) {
  return status(session, result.ok, result.changed, result.message);
}

QuaderStandardMaterialDocumentInfo to_public(
    const StandardMaterialEditSnapshot &snapshot) {
  const ProjectStandardMaterialDefinition *material =
      snapshot.document.standard_material();
  return {
      .session_id = snapshot.session_id,
      .id = snapshot.document.id,
      .name = snapshot.document.name,
      .path = snapshot.path,
      .material = material != nullptr
                      ? to_public_material(*material)
                      : QuaderStandardMaterialDefinition{},
      .pending_texture_sources = snapshot.pending_texture_sources,
      .draft_revision = snapshot.draft_revision,
      .clean_revision = snapshot.clean_revision,
      .dirty = snapshot.dirty,
  };
}

StandardMaterialEditSessionService *service(QuaderApiSession session) {
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session);
  return project != nullptr && project->has_project()
             ? &project->standard_material_editor()
             : nullptr;
}

} // namespace

QuaderStandardMaterialDocumentsApi::QuaderStandardMaterialDocumentsApi(
    QuaderApiSession session)
    : session_(session) {}

QuaderApiResult<QuaderStandardMaterialDocumentInfo>
QuaderStandardMaterialDocumentsApi::current() const {
  StandardMaterialEditSessionService *editor = service(session_);
  const StandardMaterialEditSnapshot *snapshot =
      editor != nullptr ? editor->current() : nullptr;
  if (snapshot == nullptr) {
    return quader_api_result_from_status<QuaderStandardMaterialDocumentInfo>(
        status(session_, false, false,
               "No Standard Material document is open."));
  }
  return quader_api_result_from_status(status(session_, true, false),
                                       to_public(*snapshot));
}

QuaderApiResult<QuaderStandardMaterialDocumentRevision>
QuaderStandardMaterialDocumentsApi::current_revision() const {
  StandardMaterialEditSessionService *editor = service(session_);
  const StandardMaterialEditSnapshot *snapshot =
      editor != nullptr ? editor->current() : nullptr;
  if (snapshot == nullptr) {
    return quader_api_result_from_status<
        QuaderStandardMaterialDocumentRevision>(
        status(session_, false, false,
               "No Standard Material document is open."));
  }
  return quader_api_result_from_status(
      status(session_, true, false),
      QuaderStandardMaterialDocumentRevision{
          .session_id = snapshot->session_id,
          .id = snapshot->document.id,
          .draft_revision = snapshot->draft_revision,
          .clean_revision = snapshot->clean_revision,
          .dirty = snapshot->dirty,
      });
}

QuaderApiResult<QuaderStandardMaterialPreviewInfo>
QuaderStandardMaterialDocumentsApi::preview() const {
  StandardMaterialEditSessionService *editor = service(session_);
  const StandardMaterialEditSnapshot *snapshot =
      editor != nullptr ? editor->current() : nullptr;
  if (snapshot == nullptr || snapshot->document.standard_material() == nullptr) {
    return quader_api_result_from_status<QuaderStandardMaterialPreviewInfo>(
        status(session_, false, false,
               "No Standard Material document is open."));
  }
  QuaderStandardMaterialPreviewInfo preview{
      .session_id = snapshot->session_id,
      .id = snapshot->document.id,
      .draft_revision = snapshot->draft_revision,
      .material = to_public_material(*snapshot->document.standard_material()),
  };
  for (std::size_t index = 0u; index < preview.textures.size(); ++index) {
    const auto decoded = editor->texture_preview(
        static_cast<StandardMaterialTextureSlot>(index));
    if (!decoded.ok()) {
      preview.texture_diagnostics[index] = decoded.error().message;
      continue;
    }
    if (!decoded.value().has_value()) {
      continue;
    }
    const StandardMaterialTexturePreview &texture = *decoded.value();
    preview.textures[index] = QuaderStandardMaterialPreviewTexture{
        .id = texture.id,
        .color_space = to_public_color_space(texture.color_space),
        .content_revision = texture.content_revision,
        .rgba8 = texture.rgba8,
        .width = texture.width,
        .height = texture.height,
        .detected_format = texture.detected_format,
        .encoded_byte_size = texture.encoded_byte_size,
    };
  }
  return quader_api_result_from_status(status(session_, true, false),
                                       std::move(preview));
}

QuaderApiResult<QuaderStandardMaterialDocumentInfo>
QuaderStandardMaterialDocumentsApi::new_material() {
  StandardMaterialEditSessionService *editor = service(session_);
  if (editor == nullptr) {
    return quader_api_result_from_status<QuaderStandardMaterialDocumentInfo>(
        status(session_, false, false,
               "Standard Materials are unavailable without an open project."));
  }
  const StandardMaterialEditSnapshot *before = editor->current();
  const std::optional<QuaderStandardMaterialDocumentInfo> replaced =
      before != nullptr ? std::optional(to_public(*before)) : std::nullopt;
  const StandardMaterialEditResult result = editor->new_material();
  if (result.ok && result.changed) {
    if (replaced.has_value()) {
      publish(quader_event::kStandardMaterialClosed, &*replaced);
    }
    publish(quader_event::kStandardMaterialOpened);
  }
  const StandardMaterialEditSnapshot *snapshot = editor->current();
  return quader_api_result_from_status(
      status(session_, result),
      snapshot != nullptr ? to_public(*snapshot)
                          : QuaderStandardMaterialDocumentInfo{});
}

QuaderApiResult<QuaderStandardMaterialDocumentInfo>
QuaderStandardMaterialDocumentsApi::open(ProjectAssetId id) {
  StandardMaterialEditSessionService *editor = service(session_);
  if (editor == nullptr) {
    return quader_api_result_from_status<QuaderStandardMaterialDocumentInfo>(
        status(session_, false, false,
               "Standard Materials are unavailable without an open project."));
  }
  const StandardMaterialEditSnapshot *before = editor->current();
  const std::optional<QuaderStandardMaterialDocumentInfo> replaced =
      before != nullptr ? std::optional(to_public(*before)) : std::nullopt;
  const StandardMaterialEditResult result = editor->open(id);
  if (result.ok && result.changed) {
    if (replaced.has_value()) {
      publish(quader_event::kStandardMaterialClosed, &*replaced);
    }
    publish(quader_event::kStandardMaterialOpened);
  }
  const StandardMaterialEditSnapshot *snapshot = editor->current();
  return quader_api_result_from_status(
      status(session_, result),
      snapshot != nullptr ? to_public(*snapshot)
                          : QuaderStandardMaterialDocumentInfo{});
}

QuaderApiResult<QuaderStandardMaterialDocumentInfo>
QuaderStandardMaterialDocumentsApi::open(ProjectRelativePath path) {
  StandardMaterialEditSessionService *editor = service(session_);
  if (editor == nullptr) {
    return quader_api_result_from_status<QuaderStandardMaterialDocumentInfo>(
        status(session_, false, false,
               "Standard Materials are unavailable without an open project."));
  }
  const StandardMaterialEditSnapshot *before = editor->current();
  const std::optional<QuaderStandardMaterialDocumentInfo> replaced =
      before != nullptr ? std::optional(to_public(*before)) : std::nullopt;
  const StandardMaterialEditResult result = editor->open(std::move(path));
  if (result.ok && result.changed) {
    if (replaced.has_value()) {
      publish(quader_event::kStandardMaterialClosed, &*replaced);
    }
    publish(quader_event::kStandardMaterialOpened);
  }
  const StandardMaterialEditSnapshot *snapshot = editor->current();
  return quader_api_result_from_status(
      status(session_, result),
      snapshot != nullptr ? to_public(*snapshot)
                          : QuaderStandardMaterialDocumentInfo{});
}

#define QUADER_STANDARD_MATERIAL_MUTATION(call)                                \
  do {                                                                          \
    StandardMaterialEditSessionService *editor = service(session_);             \
    if (editor == nullptr) {                                                     \
      return status(session_, false, false,                                      \
                    "No Standard Material document is open.");                  \
    }                                                                           \
    const bool was_dirty =                                                       \
        editor->current() != nullptr && editor->current()->dirty;                \
    const StandardMaterialEditResult result = (call);                            \
    const bool is_dirty =                                                        \
        editor->current() != nullptr && editor->current()->dirty;                \
    if (result.ok && was_dirty != is_dirty) {                                    \
      publish(quader_event::kStandardMaterialDirtyChanged);                      \
    }                                                                           \
    return status(session_, result);                                             \
  } while (false)

QuaderApiVoidResult
QuaderStandardMaterialDocumentsApi::set_name(std::string name) {
  QUADER_STANDARD_MATERIAL_MUTATION(editor->set_name(std::move(name)));
}

QuaderApiVoidResult QuaderStandardMaterialDocumentsApi::set_base_color(
    std::array<float, 3> color) {
  QUADER_STANDARD_MATERIAL_MUTATION(editor->set_base_color(color));
}

QuaderApiVoidResult
QuaderStandardMaterialDocumentsApi::set_roughness(float value) {
  QUADER_STANDARD_MATERIAL_MUTATION(editor->set_roughness(value));
}

QuaderApiVoidResult
QuaderStandardMaterialDocumentsApi::set_metallic(float value) {
  QUADER_STANDARD_MATERIAL_MUTATION(editor->set_metallic(value));
}

QuaderApiVoidResult
QuaderStandardMaterialDocumentsApi::set_normal_strength(float value) {
  QUADER_STANDARD_MATERIAL_MUTATION(editor->set_normal_strength(value));
}

QuaderApiVoidResult QuaderStandardMaterialDocumentsApi::set_scalar(
    QuaderStandardMaterialScalar scalar, float value) {
  const std::optional<StandardMaterialScalar> internal =
      to_internal_scalar(scalar);
  if (!internal.has_value()) {
    return status(session_, false, false,
                  "The Standard Material scalar is invalid.");
  }
  QUADER_STANDARD_MATERIAL_MUTATION(editor->set_scalar(*internal, value));
}

QuaderApiVoidResult
QuaderStandardMaterialDocumentsApi::set_normal_convention(
    QuaderNormalMapConvention convention) {
  const std::optional<NormalMapConvention> internal =
      to_internal_normal_convention(convention);
  if (!internal.has_value()) {
    return status(session_, false, false,
                  "The Standard Material normal convention is invalid.");
  }
  QUADER_STANDARD_MATERIAL_MUTATION(
      editor->set_normal_convention(*internal));
}

QuaderApiVoidResult
QuaderStandardMaterialDocumentsApi::set_texture_transform(
    QuaderStandardMaterialTextureTransform transform) {
  QUADER_STANDARD_MATERIAL_MUTATION(editor->set_texture_transform(
      to_internal_texture_transform(transform)));
}

QuaderApiVoidResult
QuaderStandardMaterialDocumentsApi::set_texture_transform_component(
    QuaderStandardMaterialTextureTransformComponent component, float value) {
  const std::optional<StandardMaterialTextureTransformComponent> internal =
      to_internal_texture_transform_component(component);
  if (!internal.has_value()) {
    return status(session_, false, false,
                  "The Standard Material texture transform component is invalid.");
  }
  QUADER_STANDARD_MATERIAL_MUTATION(
      editor->set_texture_transform_component(*internal, value));
}

QuaderApiVoidResult QuaderStandardMaterialDocumentsApi::import_texture(
    QuaderStandardMaterialTextureSlot slot,
    const std::filesystem::path &source_path) {
  const std::optional<StandardMaterialTextureSlot> internal =
      to_internal_texture_slot(slot);
  if (!internal.has_value()) {
    return status(session_, false, false,
                  "The Standard Material texture slot is invalid.");
  }
  QUADER_STANDARD_MATERIAL_MUTATION(
      editor->import_texture(*internal, source_path));
}

QuaderApiVoidResult QuaderStandardMaterialDocumentsApi::clear_texture(
    QuaderStandardMaterialTextureSlot slot) {
  const std::optional<StandardMaterialTextureSlot> internal =
      to_internal_texture_slot(slot);
  if (!internal.has_value()) {
    return status(session_, false, false,
                  "The Standard Material texture slot is invalid.");
  }
  QUADER_STANDARD_MATERIAL_MUTATION(
      editor->clear_texture(*internal));
}

#undef QUADER_STANDARD_MATERIAL_MUTATION

QuaderApiVoidResult QuaderStandardMaterialDocumentsApi::save() {
  StandardMaterialEditSessionService *editor = service(session_);
  if (editor == nullptr) {
    return status(session_, false, false,
                  "No Standard Material document is open.");
  }
  const StandardMaterialEditResult result = editor->save();
  if (result.ok && result.changed) {
    publish(quader_event::kStandardMaterialSaved);
    publish(quader_event::kStandardMaterialDirtyChanged);
    publish(quader_event::kProjectAssetsChanged);
  }
  return status(session_, result);
}

QuaderApiVoidResult QuaderStandardMaterialDocumentsApi::save_as(
    ProjectRelativePath path) {
  StandardMaterialEditSessionService *editor = service(session_);
  if (editor == nullptr) {
    return status(session_, false, false,
                  "No Standard Material document is open.");
  }
  const StandardMaterialEditResult result = editor->save_as(std::move(path));
  if (result.ok && result.changed) {
    publish(quader_event::kStandardMaterialSaved);
    publish(quader_event::kStandardMaterialDirtyChanged);
    publish(quader_event::kProjectAssetsChanged);
    publish(quader_event::kActiveProjectMaterialChanged);
  }
  return status(session_, result);
}

QuaderApiVoidResult
QuaderStandardMaterialDocumentsApi::close(bool discard_dirty) {
  StandardMaterialEditSessionService *editor = service(session_);
  if (editor == nullptr) {
    return status(session_, true, false, "No Standard Material is open.");
  }
  const StandardMaterialEditSnapshot *snapshot = editor->current();
  const std::optional<QuaderStandardMaterialDocumentInfo> closing =
      snapshot != nullptr ? std::optional(to_public(*snapshot)) : std::nullopt;
  const StandardMaterialEditResult result = editor->close(discard_dirty);
  if (result.ok && result.changed) {
    publish(quader_event::kStandardMaterialClosed,
            closing.has_value() ? &*closing : nullptr);
  }
  return status(session_, result);
}

void QuaderStandardMaterialDocumentsApi::publish(
    std::string_view event_name,
    const QuaderStandardMaterialDocumentInfo *document_override) {
  if (events_ == nullptr) {
    return;
  }
  ProjectSession *project =
      QuaderApiSessionInternalAccess::project_session(session_);
  const StandardMaterialEditSnapshot *snapshot =
      project != nullptr ? project->standard_material_editor().current()
                         : nullptr;
  QuaderEvent event{.name = std::string(event_name)};
  if (project != nullptr && project->has_project()) {
    event.project_id = project->manifest()->id();
    event.project_name = project->manifest()->name();
    const auto root_utf8 = project->root().u8string();
    event.project_root.emplace(root_utf8.begin(), root_utf8.end());
  }
  if (document_override != nullptr) {
    event.project_asset_id = document_override->id;
    event.material_edit_session_id = document_override->session_id;
    event.material_name = document_override->name;
    event.material_path =
        document_override->path.has_value()
            ? std::optional(document_override->path->portable_string())
            : std::nullopt;
    event.material_dirty = document_override->dirty;
  } else if (snapshot != nullptr) {
    event.project_asset_id = snapshot->document.id;
    event.material_edit_session_id = snapshot->session_id;
    event.material_name = snapshot->document.name;
    event.material_path = snapshot->path.has_value()
                              ? std::optional(snapshot->path->portable_string())
                              : std::nullopt;
    event.material_dirty = snapshot->dirty;
  }
  (void)QuaderEventsApiInternalAccess::publish(*events_, std::move(event));
}

} // namespace quader
