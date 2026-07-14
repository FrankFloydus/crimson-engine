////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_api_result.hpp"
#include "api/quader_api_session.hpp"
#include "io/project/project_types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace quader {

class QuaderEventsApi;
class QuaderStandardMaterialDocumentsApiInternalAccess;

/// Identifies one texture input exposed by the Standard Material editor API.
enum class QuaderStandardMaterialTextureSlot : std::uint8_t {
  Albedo,
  Roughness,
  Metallic,
  Normal,
};

/// Declares the tangent-space convention exposed by the public material API.
enum class QuaderNormalMapConvention : std::uint8_t {
  OpenGL,
  DirectX,
};

/// Declares how decoded project texture values are interpreted by rendering.
enum class QuaderProjectTextureColorSpace : std::uint8_t {
  Srgb,
  Linear,
};

/// Public stable reference to one texture dependency of a Standard Material.
struct QuaderStandardMaterialTextureReference final {
  ProjectAssetId id;
  std::string project_relative_path;
  QuaderProjectTextureColorSpace color_space =
      QuaderProjectTextureColorSpace::Srgb;
};

/// Renderer-neutral Standard Material values exposed through the public API.
struct QuaderStandardMaterialDefinition final {
  std::array<float, 3> base_color{1.0f, 1.0f, 1.0f};
  float roughness = 1.0f;
  float metallic = 0.0f;
  float normal_strength = 1.0f;
  QuaderNormalMapConvention normal_convention =
      QuaderNormalMapConvention::OpenGL;
  std::optional<QuaderStandardMaterialTextureReference> base_color_texture;
  std::optional<QuaderStandardMaterialTextureReference> roughness_texture;
  std::optional<QuaderStandardMaterialTextureReference> metallic_texture;
  std::optional<QuaderStandardMaterialTextureReference> normal_texture;

  [[nodiscard]]
  const std::optional<QuaderStandardMaterialTextureReference> *
  texture(QuaderStandardMaterialTextureSlot slot) const noexcept {
    switch (slot) {
    case QuaderStandardMaterialTextureSlot::Albedo:
      return &base_color_texture;
    case QuaderStandardMaterialTextureSlot::Roughness:
      return &roughness_texture;
    case QuaderStandardMaterialTextureSlot::Metallic:
      return &metallic_texture;
    case QuaderStandardMaterialTextureSlot::Normal:
      return &normal_texture;
    }
    return nullptr;
  }
};

/// Snapshot of the retained Standard Material document and its draft state.
struct QuaderStandardMaterialDocumentInfo {
  MaterialEditSessionId session_id;
  ProjectAssetId id;
  std::string name;
  std::optional<ProjectRelativePath> path;
  QuaderStandardMaterialDefinition material;
  std::array<std::optional<std::filesystem::path>, 4> pending_texture_sources;
  std::uint64_t draft_revision = 0u;
  std::uint64_t clean_revision = 0u;
  bool dirty = false;
};

/// Immutable decoded texture data used by a Standard Material preview.
struct QuaderStandardMaterialPreviewTexture {
  std::optional<ProjectAssetId> id;
  QuaderProjectTextureColorSpace color_space =
      QuaderProjectTextureColorSpace::Srgb;
  std::uint64_t content_revision = 0u;
  std::shared_ptr<const std::vector<std::byte>> rgba8;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
};

/// Complete public evaluation snapshot for the current Standard Material draft.
struct QuaderStandardMaterialPreviewInfo {
  MaterialEditSessionId session_id;
  ProjectAssetId id;
  std::uint64_t draft_revision = 0u;
  QuaderStandardMaterialDefinition material;
  std::array<std::optional<QuaderStandardMaterialPreviewTexture>, 4> textures;
  std::array<std::string, 4> texture_diagnostics;
};

/// Public user-intent surface for the one retained Standard Material draft.
class QuaderStandardMaterialDocumentsApi final {
public:
  explicit QuaderStandardMaterialDocumentsApi(QuaderApiSession session);

  [[nodiscard]] QuaderApiResult<QuaderStandardMaterialDocumentInfo>
  current() const;
  [[nodiscard]] QuaderApiResult<QuaderStandardMaterialPreviewInfo>
  preview() const;
  [[nodiscard]] QuaderApiResult<QuaderStandardMaterialDocumentInfo>
  new_material();
  [[nodiscard]] QuaderApiResult<QuaderStandardMaterialDocumentInfo>
  open(ProjectAssetId id);
  [[nodiscard]] QuaderApiResult<QuaderStandardMaterialDocumentInfo>
  open(ProjectRelativePath path);

  QuaderApiVoidResult set_name(std::string name);
  QuaderApiVoidResult set_base_color(std::array<float, 3> color);
  QuaderApiVoidResult set_roughness(float value);
  QuaderApiVoidResult set_metallic(float value);
  QuaderApiVoidResult set_normal_strength(float value);
  QuaderApiVoidResult
  set_normal_convention(QuaderNormalMapConvention convention);
  QuaderApiVoidResult import_texture(QuaderStandardMaterialTextureSlot slot,
                                     const std::filesystem::path &source_path);
  QuaderApiVoidResult clear_texture(QuaderStandardMaterialTextureSlot slot);
  QuaderApiVoidResult save();
  QuaderApiVoidResult save_as(ProjectRelativePath path);
  QuaderApiVoidResult close(bool discard_dirty = false);

  void rebind_session(QuaderApiSession session) { session_ = session; }

private:
  void attach_events(QuaderEventsApi &events) { events_ = &events; }
  void publish(
      std::string_view event_name,
      const QuaderStandardMaterialDocumentInfo *document_override = nullptr);

  friend class QuaderStandardMaterialDocumentsApiInternalAccess;
  QuaderApiSession session_;
  QuaderEventsApi *events_ = nullptr;
};

} // namespace quader
