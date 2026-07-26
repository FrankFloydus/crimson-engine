////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_material_catalog.hpp"

#include "core/qstr.hpp"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace quader {
namespace {

constexpr std::uintmax_t kMaximumMaterialPreviewBytes = 64u * 1024u * 1024u;

bool has_qmat_extension(const std::filesystem::path &path) {
  return QStr::to_ascii_lower(path.extension().string()) == ".qmat";
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

/// Identifies a physical file independently of its project-relative alias.
struct PhysicalFileIdentity final {
  std::uint64_t device = 0u;
  std::uint64_t file = 0u;

  friend bool operator==(const PhysicalFileIdentity &,
                         const PhysicalFileIdentity &) = default;
};

/// Hashes physical file identities used for alias validation.
struct PhysicalFileIdentityHash final {
  std::size_t operator()(const PhysicalFileIdentity &identity) const noexcept {
    const std::uint64_t mixed =
        identity.device ^
        (identity.file + 0x9e3779b97f4a7c15ull +
         (identity.device << 6u) + (identity.device >> 2u));
    return static_cast<std::size_t>(mixed);
  }
};

ProjectIoResult<PhysicalFileIdentity>
physical_file_identity(const std::filesystem::path &path) {
#ifdef _WIN32
  /// Closes a Windows file handle when identity inspection completes.
  class ScopedHandle final {
  public:
    explicit ScopedHandle(HANDLE handle) : handle_(handle) {}
    ~ScopedHandle() {
      if (handle_ != INVALID_HANDLE_VALUE) {
        (void)::CloseHandle(handle_);
      }
    }
    [[nodiscard]] HANDLE get() const noexcept { return handle_; }

  private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
  };

  ScopedHandle file(::CreateFileW(
      path.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
  if (file.get() == INVALID_HANDLE_VALUE) {
    return ProjectIoResult<PhysicalFileIdentity>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not open project material for identity validation: " +
            std::error_code(static_cast<int>(::GetLastError()),
                            std::system_category())
                .message(),
        path);
  }
  BY_HANDLE_FILE_INFORMATION information{};
  if (::GetFileInformationByHandle(file.get(), &information) == 0 ||
      (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0u) {
    return ProjectIoResult<PhysicalFileIdentity>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not inspect project material file identity: " +
            std::error_code(static_cast<int>(::GetLastError()),
                            std::system_category())
                .message(),
        path);
  }
  return ProjectIoResult<PhysicalFileIdentity>::success({
      .device = information.dwVolumeSerialNumber,
      .file =
          (static_cast<std::uint64_t>(information.nFileIndexHigh) << 32u) |
          information.nFileIndexLow,
  });
#else
  const int descriptor = ::open(path.c_str(), O_RDONLY);
  if (descriptor < 0) {
    return ProjectIoResult<PhysicalFileIdentity>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not open project material for identity validation: " +
            std::error_code(errno, std::generic_category()).message(),
        path);
  }
  /// Closes a POSIX file descriptor when identity inspection completes.
  struct ScopedDescriptor final {
    explicit ScopedDescriptor(int value) : value(value) {}
    ~ScopedDescriptor() { (void)::close(value); }
    int value = -1;
  } file(descriptor);
  struct stat information {};
  if (::fstat(file.value, &information) != 0 ||
      !S_ISREG(information.st_mode)) {
    return ProjectIoResult<PhysicalFileIdentity>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not inspect project material file identity: " +
            std::error_code(errno, std::generic_category()).message(),
        path);
  }
  return ProjectIoResult<PhysicalFileIdentity>::success({
      .device = static_cast<std::uint64_t>(information.st_dev),
      .file = static_cast<std::uint64_t>(information.st_ino),
  });
#endif
}

ProjectIoResult<void> validate_no_physical_aliases(
    const ProjectFileSystem &file_system,
    std::span<const ProjectRelativePath> paths) {
  ProjectIoResult<void> basic = file_system.validate_no_path_aliases(paths);
  if (!basic.ok()) {
    return basic;
  }
  std::unordered_set<PhysicalFileIdentity, PhysicalFileIdentityHash>
      identities;
  identities.reserve(paths.size());
  for (const ProjectRelativePath &path : paths) {
    ProjectIoResult<std::filesystem::path> resolved =
        file_system.resolve_existing_file(path);
    if (!resolved.ok()) {
      return ProjectIoResult<void>::failure(
          resolved.error().code, resolved.error().message,
          resolved.error().path);
    }
    ProjectIoResult<PhysicalFileIdentity> identity =
        physical_file_identity(resolved.value());
    if (!identity.ok()) {
      return ProjectIoResult<void>::failure(
          identity.error().code, identity.error().message,
          identity.error().path);
    }
    if (!identities.insert(identity.value()).second) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::PathCaseCollision,
          "Project material files alias the same physical location.",
          path.native_path());
    }
  }
  return ProjectIoResult<void>::success();
}

} // namespace

ProjectIoResult<ProjectMaterialCatalog>
ProjectMaterialCatalog::scan(const ProjectFileSystem &file_system,
                             ProjectFileSystemWorkStats *work_stats) {
  if (!file_system.valid()) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Cannot scan materials without an open project filesystem.");
  }

  ProjectFileSystem scan_file_system =
      file_system.begin_resolution_batch(work_stats);
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
      scan_file_system.resolve(materials_relative.value());
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
        scan_file_system.relative_from_absolute(entry.path());
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
      validate_no_physical_aliases(scan_file_system, paths);
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
        QmatCodec::read(scan_file_system, path);
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
          scan_file_system.resolve_existing_file(relative.value());
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
    const std::string left_name = QStr::to_ascii_lower(left.material.name);
    const std::string right_name = QStr::to_ascii_lower(right.material.name);
    if (left_name != right_name) {
      return left_name < right_name;
    }
    return left.path.portable_case_key() < right.path.portable_case_key();
  });

  catalog.entry_index_by_id_.reserve(catalog.entries_.size());
  for (std::size_t index = 0u; index < catalog.entries_.size(); ++index) {
    catalog.entry_index_by_id_.emplace(
        catalog.entries_[index].material.id, index);
  }

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
  const auto found = entry_index_by_id_.find(id);
  return found == entry_index_by_id_.end()
             ? nullptr
             : &entries_[found->second];
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
