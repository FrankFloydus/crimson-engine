////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/material/project_image_decoder.hpp"
#include "io/project/qmat_codec.hpp"

#include <array>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace quader {

class ProjectSession;

/// Reports the outcome of one Standard Material draft operation.
struct StandardMaterialEditResult final {
  bool ok = false;
  bool changed = false;
  std::string message;
};

/// Immutable state of the one retained Standard Material editing session.
struct StandardMaterialEditSnapshot final {
  MaterialEditSessionId session_id;
  ProjectMaterialDocument document;
  std::optional<ProjectRelativePath> path;
  std::uint64_t source_content_revision = 0u;
  std::uint64_t draft_revision = 0u;
  std::uint64_t clean_revision = 0u;
  bool dirty = false;
  std::array<std::optional<std::filesystem::path>, 4> pending_texture_sources;
};

/// Immutable decoded texture data used by draft preview adapters.
struct StandardMaterialTexturePreview final {
  std::optional<ProjectAssetId> id;
  ProjectTextureColorSpace color_space = ProjectTextureColorSpace::Srgb;
  std::uint64_t content_revision = 0u;
  std::shared_ptr<const std::vector<std::byte>> rgba8;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::string detected_format;
  std::uintmax_t encoded_byte_size = 0u;
};

/// Owns one reusable Standard Material draft independently of saved catalog state.
class StandardMaterialEditSessionService final {
public:
  explicit StandardMaterialEditSessionService(ProjectSession &project);
  ~StandardMaterialEditSessionService();

  [[nodiscard]] bool has_document() const noexcept {
    return current_ != nullptr;
  }
  [[nodiscard]] const StandardMaterialEditSnapshot *current() const noexcept;

  [[nodiscard]] StandardMaterialEditResult new_material();
  [[nodiscard]] StandardMaterialEditResult open(ProjectAssetId id);
  [[nodiscard]] StandardMaterialEditResult open(ProjectRelativePath path);
  [[nodiscard]] StandardMaterialEditResult close(bool discard_dirty = false);

  [[nodiscard]] StandardMaterialEditResult set_name(std::string name);
  [[nodiscard]] StandardMaterialEditResult
  set_base_color(std::array<float, 3> color);
  [[nodiscard]] StandardMaterialEditResult set_roughness(float value);
  [[nodiscard]] StandardMaterialEditResult set_metallic(float value);
  [[nodiscard]] StandardMaterialEditResult set_normal_strength(float value);
  [[nodiscard]] StandardMaterialEditResult
  set_normal_convention(NormalMapConvention convention);
  [[nodiscard]] StandardMaterialEditResult set_texture_transform(
      StandardMaterialTextureTransform transform);
  [[nodiscard]] StandardMaterialEditResult
  import_texture(StandardMaterialTextureSlot slot,
                 const std::filesystem::path &source_path);
  [[nodiscard]] StandardMaterialEditResult
  clear_texture(StandardMaterialTextureSlot slot);
  [[nodiscard]]
  ProjectIoResult<std::optional<StandardMaterialTexturePreview>>
  texture_preview(StandardMaterialTextureSlot slot);

  [[nodiscard]] StandardMaterialEditResult save();
  [[nodiscard]] StandardMaterialEditResult save_as(ProjectRelativePath path);

private:
  struct Draft;

  [[nodiscard]] StandardMaterialEditResult save_to(ProjectRelativePath path,
                                                    bool save_as);
  [[nodiscard]] StandardMaterialEditResult mark_changed();
  [[nodiscard]] ProjectStandardMaterialDefinition *standard() noexcept;
  void reset_with(ProjectMaterialDocument document,
                  std::optional<ProjectRelativePath> path,
                  std::uint64_t source_content_revision);
  [[nodiscard]] MaterialEditSessionId allocate_session_id();

  ProjectSession *project_ = nullptr;
  std::unique_ptr<Draft> current_;
  std::uint32_t generation_ = 0u;
};

} // namespace quader
