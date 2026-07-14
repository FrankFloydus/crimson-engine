////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_material_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>

namespace quader {
namespace {

constexpr std::uintmax_t kMaximumMaterialPreviewBytes = 64u * 1024u * 1024u;

std::string lowercase_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(),
                 [](unsigned char character) {
                   return static_cast<char>(std::tolower(character));
                 });
  return value;
}

bool has_qmat_extension(const std::filesystem::path &path) {
  return lowercase_ascii(path.extension().string()) == ".qmat";
}

bool catalog_security_error(ProjectIoErrorCode code) {
  return code == ProjectIoErrorCode::PathOutsideProject ||
         code == ProjectIoErrorCode::PathCaseCollision ||
         code == ProjectIoErrorCode::SymlinkEscape;
}

std::uint64_t append_hash(std::uint64_t hash, std::string_view text) {
  for (unsigned char value : text) {
    hash ^= value;
    hash *= 1'099'511'628'211ull;
  }
  return hash;
}

std::uint64_t append_hash(std::uint64_t hash, std::uint64_t value) {
  for (std::size_t offset = 0u; offset < sizeof(value); ++offset) {
    hash ^= static_cast<unsigned char>(value >> (offset * 8u));
    hash *= 1'099'511'628'211ull;
  }
  return hash;
}

ProjectIoResult<void> validate_no_physical_aliases(
    const ProjectFileSystem &file_system,
    std::span<const ProjectRelativePath> paths) {
  ProjectIoResult<void> basic = file_system.validate_no_path_aliases(paths);
  if (!basic.ok()) {
    return basic;
  }
  for (std::size_t left = 0u; left < paths.size(); ++left) {
    for (std::size_t right = left + 1u; right < paths.size(); ++right) {
      ProjectIoResult<bool> same =
          file_system.refers_to_same_location(paths[left], paths[right]);
      if (!same.ok()) {
        return ProjectIoResult<void>::failure(
            same.error().code, same.error().message, same.error().path);
      }
      if (same.value()) {
        return ProjectIoResult<void>::failure(
            ProjectIoErrorCode::PathCaseCollision,
            "Project material files alias the same physical location.",
            paths[right].native_path());
      }
    }
  }
  return ProjectIoResult<void>::success();
}

} // namespace

ProjectIoResult<ProjectMaterialCatalog>
ProjectMaterialCatalog::scan(const ProjectFileSystem &file_system) {
  if (!file_system.valid()) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Cannot scan materials without an open project filesystem.");
  }

  ProjectMaterialCatalog catalog;
  ProjectIoResult<ProjectRelativePath> materials_relative =
      ProjectRelativePath::parse_portable("Materials");
  if (!materials_relative.ok()) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        materials_relative.error().code,
        materials_relative.error().message,
        materials_relative.error().path);
  }
  ProjectIoResult<std::filesystem::path> materials_resolved =
      file_system.resolve(materials_relative.value());
  if (!materials_resolved.ok()) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        materials_resolved.error().code, materials_resolved.error().message,
        materials_resolved.error().path);
  }
  const std::filesystem::path materials_root =
      materials_resolved.take_value();
  std::error_code error;
  if (!std::filesystem::exists(materials_root, error)) {
    if (error) {
      return ProjectIoResult<ProjectMaterialCatalog>::failure(
          ProjectIoErrorCode::IoFailure,
          "Could not inspect project Materials directory: " +
              error.message(),
          materials_root);
    }
    return ProjectIoResult<ProjectMaterialCatalog>::success(
        std::move(catalog));
  }
  if (!std::filesystem::is_directory(materials_root, error) || error) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        ProjectIoErrorCode::RootNotDirectory,
        error ? "Could not inspect project Materials directory: " +
                    error.message()
              : "Project Materials path is not a directory.",
        materials_root);
  }

  std::vector<ProjectRelativePath> paths;
  std::filesystem::recursive_directory_iterator iterator(
      materials_root, std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && iterator != end; iterator.increment(error)) {
    const std::filesystem::directory_entry &entry = *iterator;
    if (!has_qmat_extension(entry.path())) {
      continue;
    }
    ProjectIoResult<ProjectRelativePath> relative =
        file_system.relative_from_absolute(entry.path());
    if (!relative.ok()) {
      return ProjectIoResult<ProjectMaterialCatalog>::failure(
          relative.error().code, relative.error().message,
          relative.error().path);
    }
    paths.push_back(relative.take_value());
  }
  if (error) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not enumerate project materials: " + error.message(),
        materials_root);
  }

  std::ranges::sort(paths, [](const ProjectRelativePath &left,
                             const ProjectRelativePath &right) {
    return left.portable_case_key() < right.portable_case_key();
  });
  ProjectIoResult<void> aliases =
      validate_no_physical_aliases(file_system, paths);
  if (!aliases.ok()) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        aliases.error().code, aliases.error().message,
        aliases.error().path);
  }

  std::unordered_map<ProjectAssetId, ProjectRelativePath, ProjectAssetIdHash>
      identities;
  identities.reserve(paths.size());
  for (const ProjectRelativePath &path : paths) {
    ProjectIoResult<QmatDecodedFile> decoded =
        QmatCodec::read(file_system, path);
    if (!decoded.ok()) {
      if (catalog_security_error(decoded.error().code)) {
        return ProjectIoResult<ProjectMaterialCatalog>::failure(
            decoded.error().code, decoded.error().message,
            decoded.error().path);
      }
      catalog.warnings_.push_back(path.portable_string() + ": " +
                                  decoded.error().message);
      continue;
    }
    const ProjectAssetId id = decoded.value().material.id;
    const auto [existing, inserted] = identities.emplace(id, path);
    if (!inserted) {
      return ProjectIoResult<ProjectMaterialCatalog>::failure(
          ProjectIoErrorCode::DuplicateAsset,
          "Project materials contain duplicate UUID " +
              std::string(id.str()) + " at " +
              existing->second.portable_string() + " and " +
              path.portable_string() + ".",
          path.native_path());
    }

    const ProjectStandardMaterialDefinition *standard =
        decoded.value().material.standard_material();
    if (standard == nullptr) {
      catalog.warnings_.push_back(
          path.portable_string() +
          ": material type is not supported by this catalog.");
      continue;
    }

    std::optional<ProjectIoError> texture_security_error;
    const auto inspect_texture = [&](const auto &slot,
                                     std::string_view label) {
      if (!slot.has_value()) {
        return;
      }
      ProjectIoResult<ProjectRelativePath> relative =
          ProjectRelativePath::parse_portable(
              slot->project_relative_path);
      if (!relative.ok()) {
        texture_security_error = relative.error();
        return;
      }
      ProjectIoResult<std::filesystem::path> texture =
          file_system.resolve_existing_file(relative.value());
      if (!texture.ok()) {
        if (catalog_security_error(texture.error().code)) {
          texture_security_error = texture.error();
          return;
        }
        catalog.warnings_.push_back(
            path.portable_string() + ": " + std::string(label) +
            " preview is unavailable: " + texture.error().message);
      }
    };
    inspect_texture(standard->base_color_texture, "albedo");
    inspect_texture(standard->roughness_texture, "roughness");
    inspect_texture(standard->metallic_texture, "metallic");
    inspect_texture(standard->normal_texture, "normal");
    if (texture_security_error.has_value()) {
      return ProjectIoResult<ProjectMaterialCatalog>::failure(
          texture_security_error->code, texture_security_error->message,
          texture_security_error->path);
    }
    catalog.entries_.push_back({
        .material = std::move(decoded.value().material),
        .path = path,
        .file_size = decoded.value().file_size,
        .content_revision = decoded.value().content_revision,
    });
  }

  std::ranges::sort(catalog.entries_, [](const auto &left, const auto &right) {
    const std::string left_name = lowercase_ascii(left.material.name);
    const std::string right_name = lowercase_ascii(right.material.name);
    if (left_name != right_name) {
      return left_name < right_name;
    }
    return left.path.portable_case_key() < right.path.portable_case_key();
  });

  std::uint64_t revision = 14'695'981'039'346'656'037ull;
  for (const ProjectMaterialCatalogEntry &entry : catalog.entries_) {
    revision = append_hash(revision, entry.material.id.str());
    revision = append_hash(revision, entry.path.portable_string());
    revision = append_hash(revision, entry.content_revision);
    revision = append_hash(revision, entry.file_size);
  }
  catalog.content_revision_ = revision;
  return ProjectIoResult<ProjectMaterialCatalog>::success(
      std::move(catalog));
}

const ProjectMaterialCatalogEntry *
ProjectMaterialCatalog::find(ProjectAssetId id) const noexcept {
  const auto found = std::ranges::find_if(
      entries_, [id](const ProjectMaterialCatalogEntry &entry) {
        return entry.material.id == id;
      });
  return found == entries_.end() ? nullptr : &*found;
}

ProjectIoResult<ProjectMaterialPreview>
ProjectMaterialCatalog::read_preview(const ProjectFileSystem &file_system,
                                     ProjectAssetId id) const {
  ProjectIoResult<ProjectMaterialTextureData> texture =
      read_texture(file_system, id, StandardMaterialTextureSlot::Albedo);
  if (!texture.ok()) {
    return ProjectIoResult<ProjectMaterialPreview>::failure(
        texture.error().code, texture.error().message,
        texture.error().path);
  }
  return ProjectIoResult<ProjectMaterialPreview>::success({
      .encoded_bytes = std::move(texture.value().encoded_bytes),
      .content_revision = texture.value().content_revision,
  });
}

ProjectIoResult<ProjectMaterialTextureData>
ProjectMaterialCatalog::read_texture(
    const ProjectFileSystem &file_system, ProjectAssetId id,
    StandardMaterialTextureSlot kind) const {
  const ProjectMaterialCatalogEntry *entry = find(id);
  if (entry == nullptr) {
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        ProjectIoErrorCode::AssetNotFound,
        "Project material is not present in the catalog.");
  }

  const ProjectStandardMaterialDefinition *standard =
      entry->material.standard_material();
  if (standard == nullptr) {
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        ProjectIoErrorCode::UnsupportedFormat,
        "Project material is not a Standard Material.",
        entry->path.native_path());
  }
  const std::optional<ProjectMaterialTextureSlot> *slot =
      standard->texture(kind);
  if (slot == nullptr || !slot->has_value()) {
    std::string_view label;
    switch (kind) {
    case StandardMaterialTextureSlot::Albedo:
      label = "albedo";
      break;
    case StandardMaterialTextureSlot::Roughness:
      label = "roughness";
      break;
    case StandardMaterialTextureSlot::Metallic:
      label = "metallic";
      break;
    case StandardMaterialTextureSlot::Normal:
      label = "normal";
      break;
    }
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        ProjectIoErrorCode::AssetNotFound,
        "Project Standard Material has no " + std::string(label) +
            " texture.",
        entry->path.native_path());
  }
  ProjectIoResult<ProjectRelativePath> relative =
      ProjectRelativePath::parse_portable((*slot)->project_relative_path);
  if (!relative.ok()) {
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        relative.error().code, relative.error().message,
        relative.error().path);
  }
  ProjectIoResult<std::filesystem::path> absolute =
      file_system.resolve_existing_file(relative.value());
  if (!absolute.ok()) {
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        absolute.error().code, absolute.error().message,
        absolute.error().path);
  }
  std::error_code error;
  const std::uintmax_t size =
      std::filesystem::file_size(absolute.value(), error);
  if (error || size > kMaximumMaterialPreviewBytes) {
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        error ? ProjectIoErrorCode::IoFailure
              : ProjectIoErrorCode::MaterialMalformed,
        error ? "Could not inspect material texture: " + error.message()
              : "Material texture exceeds the supported size limit.",
        absolute.value());
  }
  std::ifstream stream(absolute.value(), std::ios::binary);
  if (!stream.is_open()) {
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not open material texture.", absolute.value());
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  stream.read(reinterpret_cast<char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
  if (!stream.good() && !stream.eof()) {
    return ProjectIoResult<ProjectMaterialTextureData>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not read material texture.", absolute.value());
  }
  const std::string_view byte_view(
      reinterpret_cast<const char *>(bytes.data()), bytes.size());
  return ProjectIoResult<ProjectMaterialTextureData>::success({
      .id = (*slot)->id,
      .color_space = (*slot)->color_space,
      .encoded_bytes = std::move(bytes),
      .content_revision = append_hash(
          append_hash(14'695'981'039'346'656'037ull, byte_view),
          entry->content_revision),
  });
}

} // namespace quader
