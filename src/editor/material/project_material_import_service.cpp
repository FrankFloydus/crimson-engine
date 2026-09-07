////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_material_import_service.hpp"

#include "editor/material/project_image_decoder.hpp"
#include "editor/material/project_material_persistence.hpp"
#include "editor/session/project_session.hpp"

#include <QString>

#include <optional>
#include <utility>

namespace quader {
namespace {

std::string completed_message(std::size_t imported, std::size_t failed) {
  if (imported == 0u) {
    return "No materials were imported; " + std::to_string(failed) +
           " source file(s) failed to import.";
  }
  std::string message = "Imported " + std::to_string(imported) +
                        " material(s).";
  if (failed != 0u) {
    message += " " + std::to_string(failed) + " source file(s) failed.";
  }
  return message;
}

std::string with_rollback_diagnostic(
    std::string message, ProjectMaterialFileRollback &rollback,
    ProjectMaterialFileRollback::Checkpoint checkpoint) {
  const std::string rollback_message = rollback.rollback_to(checkpoint);
  if (!rollback_message.empty()) {
    message += "; " + rollback_message;
  }
  return message;
}

} // namespace

ProjectMaterialImportReport
ProjectMaterialImportService::import_textures_as_materials(
    std::span<const std::filesystem::path> source_paths) const {
  ProjectMaterialImportReport report;
  if (project_ == nullptr || !project_->has_project() ||
      project_->workspace() == nullptr) {
    report.message = "Open a project before importing material textures.";
    return report;
  }
  if (source_paths.empty()) {
    report.message = "Select at least one texture to import.";
    return report;
  }

  const ProjectFileSystem &file_system =
      project_->workspace()->file_system;
  ProjectMaterialFileRollback rollback;
  std::vector<ProjectMaterialImportSuccess> pending_successes;
  pending_successes.reserve(source_paths.size());
  report.failed.reserve(source_paths.size());

  for (const std::filesystem::path &source_path : source_paths) {
    const ProjectMaterialFileRollback::Checkpoint checkpoint =
        rollback.checkpoint();
    const auto reject = [&](std::string message) {
      const std::string rollback_message = rollback.rollback_to(checkpoint);
      if (!rollback_message.empty()) {
        report.message = std::move(message) + "; " + rollback_message;
        const std::string request_rollback_message = rollback.rollback();
        if (!request_rollback_message.empty()) {
          report.message += "; " + request_rollback_message;
        }
        return false;
      }
      report.failed.push_back(ProjectMaterialImportFailure{
          .source_path = source_path,
          .error_message = std::move(message),
      });
      return true;
    };

    ProjectIoResult<ProjectImportedImage> decoded =
        ProjectImageDecoder::read_import(source_path);
    if (!decoded.ok()) {
      if (!reject(decoded.error().message)) {
        return report;
      }
      continue;
    }
    ProjectImportedImage imported = decoded.take_value();

    std::optional<ProjectMaterialCatalog> pending_catalog;
    if (!pending_successes.empty() &&
        QString::fromUtf8(imported.source_stem).trimmed().isEmpty()) {
      auto scanned = ProjectMaterialCatalog::scan(file_system);
      if (!scanned.ok()) {
        if (!reject(scanned.error().message)) {
          return report;
        }
        continue;
      }
      pending_catalog = scanned.take_value();
    }
    auto name = default_project_material_name(
        pending_catalog ? *pending_catalog : project_->material_catalog(),
        imported.source_stem);
    if (!name.ok()) {
      if (!reject(name.error().message)) {
        return report;
      }
      continue;
    }
    ProjectIoResult<ProjectMaterialAssetDestination> material_destination =
        make_unique_material_destination(file_system, name.value());
    if (!material_destination.ok()) {
      if (!reject(material_destination.error().message)) {
        return report;
      }
      continue;
    }
    ProjectIoResult<ProjectMaterialAssetDestination> texture_destination =
        make_unique_texture_destination(file_system, imported.source_stem,
                                        imported.source_extension);
    if (!texture_destination.ok()) {
      if (!reject(texture_destination.error().message)) {
        return report;
      }
      continue;
    }

    ProjectIoResult<std::filesystem::path> texture_absolute =
        rollback.prepare_destination(file_system,
                                      texture_destination.value().path);
    if (!texture_absolute.ok()) {
      if (!reject(texture_absolute.error().message)) {
        return report;
      }
      continue;
    }
    ProjectIoResult<std::filesystem::path> material_absolute =
        rollback.prepare_destination(file_system,
                                      material_destination.value().path);
    if (!material_absolute.ok()) {
      if (!reject(material_absolute.error().message)) {
        return report;
      }
      continue;
    }

    ProjectMaterialDocument document{
        .id = material_destination.value().id,
        .name = name.take_value(),
        .kind = ProjectMaterialKind::Standard,
        .material_version = kStandardMaterialVersion,
        .payload = ProjectStandardMaterialDefinition{},
    };
    ProjectStandardMaterialDefinition *standard =
        document.standard_material();
    if (standard == nullptr) {
      if (!reject("Could not create the Standard Material payload.")) {
        return report;
      }
      continue;
    }
    standard->base_color_texture = ProjectMaterialTextureSlot{
        .id = texture_destination.value().id,
        .project_relative_path =
            texture_destination.value().path.portable_string(),
        .color_space = ProjectTextureColorSpace::Srgb,
    };

    ProjectIoResult<void> texture_written =
        rollback.create_bytes(texture_absolute.value(),
                              imported.encoded_bytes);
    if (!texture_written.ok()) {
      if (!reject(texture_written.error().message)) {
        return report;
      }
      continue;
    }

    ProjectIoResult<void> material_written =
        rollback.create_qmat(material_absolute.value(), document);
    if (!material_written.ok()) {
      if (!reject(material_written.error().message)) {
        return report;
      }
      continue;
    }
    pending_successes.push_back(ProjectMaterialImportSuccess{
        .source_path = source_path,
        .catalog_entry = ProjectMaterialCatalogEntry{
            .material = std::move(document),
            .path = material_destination.value().path,
        },
    });
  }

  if (pending_successes.empty()) {
    rollback.commit();
    report.ok = false;
    report.message = completed_message(0u, report.failed.size());
    return report;
  }

  ProjectIoResult<ProjectMaterialCatalog> scanned =
      ProjectMaterialCatalog::scan(file_system);
  if (!scanned.ok()) {
    report.message = with_rollback_diagnostic(
        scanned.error().message, rollback, 0u);
    return report;
  }
  for (ProjectMaterialImportSuccess &success : pending_successes) {
    const ProjectMaterialCatalogEntry *entry =
        scanned.value().find(success.catalog_entry.material.id);
    if (entry == nullptr) {
      report.message = with_rollback_diagnostic(
          "An imported Standard Material is missing from the project catalog.",
          rollback, 0u);
      return report;
    }
    success.catalog_entry = *entry;
  }

  ProjectSessionResult installed = project_->install_material_catalog(
      scanned.take_value(), std::nullopt);
  if (!installed.ok) {
    report.message = with_rollback_diagnostic(installed.message, rollback, 0u);
    return report;
  }

  rollback.commit();
  report.ok = true;
  report.imported = std::move(pending_successes);
  report.message = completed_message(report.imported.size(),
                                     report.failed.size());
  return report;
}

} // namespace quader
