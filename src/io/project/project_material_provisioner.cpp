////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "io/project/project_material_provisioner.hpp"

#include "assets/default_material_catalog.hpp"
#include "io/qdr/qdr_atomic_write.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace quader {
namespace {

ProjectIoResult<ProjectRelativePath> project_path(std::string_view path) {
  ProjectIoResult<ProjectRelativePath> parsed =
      ProjectRelativePath::parse_portable(path);
  if (!parsed.ok()) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        parsed.error().code, parsed.error().message, parsed.error().path);
  }
  return parsed;
}

ProjectIoResult<std::string>
read_file(const std::filesystem::path &path, std::uintmax_t maximum_size) {
  std::error_code error;
  if (!std::filesystem::is_regular_file(path, error)) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::FileNotFound,
        error ? "Could not inspect packaged material resource: " +
                    error.message()
              : "Packaged material resource is missing.",
        path);
  }
  const std::uintmax_t size = std::filesystem::file_size(path, error);
  if (error || size > maximum_size) {
    return ProjectIoResult<std::string>::failure(
        error ? ProjectIoErrorCode::IoFailure
              : ProjectIoErrorCode::MaterialMalformed,
        error ? "Could not inspect packaged material resource: " +
                    error.message()
              : "Packaged material resource exceeds the size limit.",
        path);
  }
  std::ifstream stream(path, std::ios::binary);
  if (!stream.is_open()) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not open packaged material resource.", path);
  }
  std::string bytes(static_cast<std::size_t>(size), '\0');
  stream.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
  if (!stream.good() && !stream.eof()) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not read packaged material resource.", path);
  }
  return ProjectIoResult<std::string>::success(std::move(bytes));
}

bool matches_packaged_default_contract(
    const ProjectMaterialDocument &material) {
  const std::optional<ProjectAssetId> expected_material_id =
      ProjectAssetId::parse(kDefaultProjectMaterialId);
  return expected_material_id.has_value() &&
         material.id == *expected_material_id &&
         material.kind == ProjectMaterialKind::Standard &&
         material.standard_material() != nullptr;
}

ProjectIoResult<void> create_directory_if_missing(
    const std::filesystem::path &path,
    ProjectMaterialProvisioningChanges &changes) {
  std::error_code error;
  if (std::filesystem::exists(path, error)) {
    if (error || !std::filesystem::is_directory(path, error)) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::RootNotDirectory,
          error ? "Could not inspect project asset directory: " +
                      error.message()
                : "Project asset directory path is occupied by a file.",
          path);
    }
    return ProjectIoResult<void>::success();
  }
  if (error || !std::filesystem::create_directory(path, error) || error) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not create project asset directory: " + error.message(),
        path);
  }
  changes.created_directories.push_back(path);
  return ProjectIoResult<void>::success();
}

ProjectIoResult<void> install_file_without_overwrite(
    const std::filesystem::path &target, std::string_view source_bytes,
    ProjectMaterialProvisioningChanges &changes) {
  std::error_code error;
  if (std::filesystem::exists(target, error)) {
    if (error || !std::filesystem::is_regular_file(target, error)) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::FileNotRegular,
          error ? "Could not inspect project material file: " +
                      error.message()
                : "Project material path is not a regular file.",
          target);
    }
    ProjectIoResult<std::string> existing =
        read_file(target, std::max<std::uintmax_t>(
                              kMaximumQmatBytes, 64u * 1024u * 1024u));
    if (!existing.ok()) {
      return ProjectIoResult<void>::failure(
          existing.error().code, existing.error().message,
          existing.error().path);
    }
    if (existing.value() != source_bytes) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::DuplicateAsset,
          "A reserved built-in material path contains different data.",
          target);
    }
    return ProjectIoResult<void>::success();
  }
  if (error) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not inspect project material path: " + error.message(),
        target);
  }
  const quader_io::QdrAtomicWriteResult written =
      quader_io::write_qdr_text_atomic(target, source_bytes);
  if (!written.ok) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::IoFailure, written.message, target);
  }
  changes.created_files.push_back(target);
  return ProjectIoResult<void>::success();
}

ProjectIoResult<ProjectMaterialProvisioningChanges> fail_and_rollback(
    ProjectIoError error, ProjectMaterialProvisioningChanges changes) {
  ProjectMaterialProvisioner::rollback(changes);
  return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
      error.code, std::move(error.message), std::move(error.path));
}

} // namespace

ProjectIoResult<ProjectMaterialProvisioningChanges>
ProjectMaterialProvisioner::ensure_default(
    const ProjectFileSystem &file_system,
    const std::filesystem::path &resource_root,
    const ProjectMaterialCatalog &catalog) {
  if (!file_system.valid() || resource_root.empty()) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "Default material provisioning requires project and resource roots.");
  }

  const std::filesystem::path qmat_source =
      resource_root / std::filesystem::path(
                          kDefaultProjectMaterialTemplatePath);
  const std::filesystem::path albedo_source =
      resource_root /
      std::filesystem::path(kDefaultProjectAlbedoTemplatePath);
  ProjectIoResult<std::string> qmat_bytes =
      read_file(qmat_source, kMaximumQmatBytes);
  if (!qmat_bytes.ok()) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        qmat_bytes.error().code, qmat_bytes.error().message,
        qmat_bytes.error().path);
  }
  ProjectIoResult<ProjectMaterialDocument> packaged =
      QmatCodec::decode(qmat_bytes.value());
  if (!packaged.ok()) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        packaged.error().code,
        "Packaged default QMAT is invalid: " + packaged.error().message,
        qmat_source);
  }
  if (!matches_packaged_default_contract(packaged.value())) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Packaged default QMAT does not match the built-in contract.",
        qmat_source);
  }
  ProjectIoResult<std::string> albedo_bytes =
      read_file(albedo_source, 64u * 1024u * 1024u);
  if (!albedo_bytes.ok()) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        albedo_bytes.error().code, albedo_bytes.error().message,
        albedo_bytes.error().path);
  }

  const ProjectAssetId default_id =
      *ProjectAssetId::parse(kDefaultProjectMaterialId);
  const ProjectMaterialCatalogEntry *existing_default =
      catalog.find(default_id);

  ProjectIoResult<ProjectRelativePath> qmat_path =
      project_path(kDefaultProjectMaterialPath);
  ProjectIoResult<ProjectRelativePath> albedo_path =
      project_path(kDefaultProjectAlbedoTexturePath);
  if (!qmat_path.ok() || !albedo_path.ok()) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Built-in default material paths are invalid.");
  }
  ProjectIoResult<std::filesystem::path> qmat_target =
      file_system.resolve(qmat_path.value());
  ProjectIoResult<std::filesystem::path> albedo_target =
      file_system.resolve(albedo_path.value());
  if (!qmat_target.ok() || !albedo_target.ok()) {
    const ProjectIoError &error =
        !qmat_target.ok() ? qmat_target.error() : albedo_target.error();
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        error.code, error.message, error.path);
  }
  if (existing_default != nullptr &&
      existing_default->path.portable_case_key() !=
          qmat_path.value().portable_case_key()) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        ProjectIoErrorCode::DuplicateAsset,
        "The built-in default material UUID must use the reserved project "
        "path Materials/default_material.qmat.",
        existing_default->path.native_path());
  }
  if (existing_default != nullptr &&
      (existing_default->material.kind != ProjectMaterialKind::Standard ||
       existing_default->material.standard_material() == nullptr)) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::failure(
        ProjectIoErrorCode::DuplicateAsset,
        "The built-in default material UUID must identify a Standard "
        "Material.",
        existing_default->path.native_path());
  }

  // A valid material at the reserved identity/path is user-editable. Do not
  // compare its authored payload to the packaged template or restore assets
  // that the user intentionally changed or cleared.
  if (existing_default != nullptr) {
    return ProjectIoResult<ProjectMaterialProvisioningChanges>::success({});
  }

  ProjectMaterialProvisioningChanges changes;
  ProjectIoResult<void> materials_directory = create_directory_if_missing(
      file_system.root() / "Materials", changes);
  if (!materials_directory.ok()) {
    return fail_and_rollback(materials_directory.error(),
                             std::move(changes));
  }
  ProjectIoResult<void> textures_directory = create_directory_if_missing(
      file_system.root() / "Textures", changes);
  if (!textures_directory.ok()) {
    return fail_and_rollback(textures_directory.error(),
                             std::move(changes));
  }

  ProjectIoResult<void> installed_albedo = install_file_without_overwrite(
      albedo_target.value(), albedo_bytes.value(), changes);
  if (!installed_albedo.ok()) {
    return fail_and_rollback(installed_albedo.error(),
                             std::move(changes));
  }
  if (existing_default == nullptr) {
    ProjectIoResult<void> installed_qmat = install_file_without_overwrite(
        qmat_target.value(), qmat_bytes.value(), changes);
    if (!installed_qmat.ok()) {
      return fail_and_rollback(installed_qmat.error(),
                               std::move(changes));
    }
  }
  return ProjectIoResult<ProjectMaterialProvisioningChanges>::success(
      std::move(changes));
}

void ProjectMaterialProvisioner::rollback(
    const ProjectMaterialProvisioningChanges &changes) noexcept {
  std::error_code ignored;
  for (auto iterator = changes.created_files.rbegin();
       iterator != changes.created_files.rend(); ++iterator) {
    std::filesystem::remove(*iterator, ignored);
    ignored.clear();
  }
  for (auto iterator = changes.created_directories.rbegin();
       iterator != changes.created_directories.rend(); ++iterator) {
    std::filesystem::remove(*iterator, ignored);
    ignored.clear();
  }
}

} // namespace quader
