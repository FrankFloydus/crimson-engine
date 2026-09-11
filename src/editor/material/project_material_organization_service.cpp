////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_material_organization_service.hpp"

#include "assets/default_material_catalog.hpp"
#include "editor/material/project_material_persistence.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_bridge_notifications.hpp"

#include <QString>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {
namespace {

ProjectSessionResult failure(std::string message) {
  return {.message = std::move(message)};
}

bool within(std::string_view path, std::string_view folder) {
  return path == folder ||
         (path.size() > folder.size() && path.starts_with(folder) &&
          path[folder.size()] == '/');
}

ProjectIoResult<ProjectRelativePath> child_path(
    std::string_view parent, std::string_view name) {
  const QString text = QString::fromUtf8(name.data(),
                                        static_cast<qsizetype>(name.size()));
  if (text.isEmpty() || text.trimmed().isEmpty() || text.size() > 255 ||
      text.endsWith(QChar('.')) || text.endsWith(QChar(' ')) ||
      std::ranges::any_of(text, [](QChar value) {
        return value.unicode() < 32u ||
               QStringView(u"<>:\"/\\|?*").contains(value);
      })) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Enter a valid folder name without separators or reserved characters.");
  }
  const QString device = text.section(QChar('.'), 0, 0).toUpper();
  const bool numbered_device =
      device.size() == 4 &&
      (device.startsWith(QStringLiteral("COM")) ||
       device.startsWith(QStringLiteral("LPT"))) &&
      QStringView(u"123456789¹²³").contains(device.back());
  if (device == QStringLiteral("CON") || device == QStringLiteral("PRN") ||
      device == QStringLiteral("AUX") || device == QStringLiteral("NUL") ||
      device == QStringLiteral("CLOCK$") || numbered_device) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "This folder name is reserved by Windows.");
  }
  return ProjectRelativePath::parse_portable(std::string(parent) + "/" +
                                             std::string(name));
}

ProjectIoResult<ProjectMaterialCatalog> current_catalog(ProjectSession *project) {
  if (project == nullptr || project->workspace() == nullptr) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Open a project before organizing materials.");
  }
  auto scanned = ProjectMaterialCatalog::scan(project->workspace()->file_system);
  if (scanned.ok() && scanned.value().content_revision() !=
                          project->material_catalog().content_revision()) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Project materials changed on disk. Refresh the browser and try again.");
  }
  return scanned;
}

ProjectIoResult<std::filesystem::path> unaliased_file(
    const ProjectFileSystem &file_system, const ProjectRelativePath &path) {
  auto absolute = file_system.resolve_existing_file(path);
  if (!absolute.ok()) {
    return absolute;
  }
  const auto physical = file_system.resolve_physical_location(path);
  if (!physical.ok()) {
    return physical;
  }
  std::error_code error;
  const auto links = std::filesystem::hard_link_count(absolute.value(), error);
  if (error || links != 1u || physical.value() != absolute.value()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::PathCaseCollision,
        "Linked or aliased material files cannot be moved.", absolute.value());
  }
  return absolute;
}

/// Compensates completed moves in reverse order until catalog publication commits.
class OrganizationRollback final {
public:
  ~OrganizationRollback() {
    try {
      (void)rollback();
    } catch (...) {
      // Preserve surviving files if diagnostic allocation fails during unwinding.
    }
  }

  [[nodiscard]] ProjectSessionResult move(
      const std::filesystem::path &source,
      const std::filesystem::path &destination) {
    entries_.push_back({source, destination});
    const auto moved =
        quader_io::move_project_entry_no_replace(source, destination);
    if (!moved.ok) {
      entries_.pop_back();
      return failure(moved.message);
    }
    return {.ok = true, .changed = true};
  }

  [[nodiscard]] std::string rollback() {
    std::string errors;
    for (std::size_t index = entries_.size(); index > 0u; --index) {
      const auto &entry = entries_[index - 1u];
      const auto restored = quader_io::move_project_entry_no_replace(
          entry.destination, entry.source);
      if (restored.ok) {
        entries_.erase(entries_.begin() +
                       static_cast<std::ptrdiff_t>(index - 1u));
      } else {
        errors += "; Could not restore " + entry.source.string() + ": " +
                  restored.message;
      }
    }
    return errors;
  }

  void commit() noexcept { entries_.clear(); }

private:
  /// Records exact already-validated locations of one completed relocation.
  struct Move final {
    std::filesystem::path source;
    std::filesystem::path destination;
  };
  std::vector<Move> entries_;
};

ProjectSessionResult publish_moves(
    ProjectSession &project, OrganizationRollback &rollback,
    std::span<const ProjectAssetId> moved_ids) {
  auto scanned = ProjectMaterialCatalog::scan(project.workspace()->file_system);
  if (!scanned.ok()) {
    return failure(scanned.error().message + rollback.rollback());
  }
  // Moves must preserve every material's saved identity and content, including
  // entries outside the requested batch. External changes abort publication.
  if (scanned.value().entries().size() !=
      project.material_catalog().entries().size()) {
    return failure("Material catalog changed during the move." +
                   rollback.rollback());
  }
  for (const auto &before : project.material_catalog().entries()) {
    const auto *after = scanned.value().find(before.material.id);
    if (after == nullptr || after->content_revision != before.content_revision ||
        after->file_size != before.file_size || after->material != before.material) {
      return failure("Material content changed during the move." +
                     rollback.rollback());
    }
  }
  auto installed = project.install_material_catalog(scanned.take_value());
  if (!installed.ok) {
    return failure(installed.message + rollback.rollback());
  }
  rollback.commit();
  for (ProjectAssetId id : moved_ids) {
    const auto *entry = project.material_catalog().find(id);
    if (entry == nullptr) {
      continue;
    }
    project.standard_material_editor().retarget_saved_path(id, entry->path);
    const auto notified = ProjectBridgeNotifications::publish_material(
        project.workspace()->file_system, project.manifest()->id(), *entry);
    const auto &warning = notified.ok() ? notified.warning()
                                        : notified.error().message;
    if (!warning.empty()) {
      installed.warnings.push_back(warning);
    }
  }
  installed.message = "Material organization updated.";
  for (const auto &warning : installed.warnings) {
    installed.message += " " + warning;
  }
  return installed;
}

bool references_folder(const ProjectMaterialDocument &material,
                       std::string_view folder) {
  const auto *standard = material.standard_material();
  if (standard == nullptr) {
    return false;
  }
  for (const auto *texture : {&standard->base_color_texture,
                              &standard->roughness_texture,
                              &standard->metallic_texture,
                              &standard->normal_texture}) {
    if (texture->has_value() &&
        within(texture->value().project_relative_path, folder)) {
      return true;
    }
  }
  return false;
}

} // namespace

ProjectSessionResult ProjectMaterialOrganizationService::create_folder(
    std::string_view parent, std::string_view name) const {
  const auto catalog = current_catalog(project_);
  if (!catalog.ok()) {
    return failure(catalog.error().message);
  }
  const auto &file_system = project_->workspace()->file_system;
  const auto parent_absolute = resolve_material_destination_folder(file_system,
                                                                   parent);
  if (!parent_absolute.ok()) {
    return failure(parent_absolute.error().message);
  }
  const auto path = child_path(parent, name);
  if (!path.ok()) {
    return failure(path.error().message);
  }
  const auto destination = file_system.resolve(path.value());
  if (!destination.ok()) {
    return failure(destination.error().message);
  }
  std::error_code error;
  if (!std::filesystem::create_directory(destination.value(), error)) {
    return failure(error ? "Could not create folder: " + error.message()
                         : "A folder or file with that name already exists.");
  }
  auto scanned = ProjectMaterialCatalog::scan(file_system);
  auto result = scanned.ok()
                    ? project_->install_material_catalog(scanned.take_value())
                    : failure(scanned.error().message);
  if (!result.ok) {
    (void)std::filesystem::remove(destination.value(), error);
    if (error) {
      result.message += "; Could not remove the new empty folder: " +
                        error.message();
    }
  } else {
    result.message = "Material folder created.";
  }
  return result;
}

ProjectSessionResult ProjectMaterialOrganizationService::rename_folder(
    std::string_view path, std::string_view name) const {
  const auto source = ProjectRelativePath::parse_portable(path);
  if (!source.ok()) {
    return failure(source.error().message);
  }
  const auto parent = ProjectRelativePath::parse(
      source.value().native_path().parent_path());
  if (!parent.ok()) {
    return failure("The Materials root cannot be renamed.");
  }
  const auto destination = child_path(parent.value().portable_string(), name);
  if (!destination.ok()) {
    return failure(destination.error().message);
  }
  return relocate_folder(path, destination.value().portable_string());
}

ProjectSessionResult ProjectMaterialOrganizationService::move_folder(
    std::string_view path, std::string_view destination_parent) const {
  const auto source = ProjectRelativePath::parse_portable(path);
  if (!source.ok()) {
    return failure(source.error().message);
  }
  const auto leaf = ProjectRelativePath::parse(
      source.value().native_path().filename());
  if (!leaf.ok()) {
    return failure(leaf.error().message);
  }
  const auto destination = child_path(destination_parent,
                                     leaf.value().portable_string());
  if (!destination.ok()) {
    return failure(destination.error().message);
  }
  return relocate_folder(path, destination.value().portable_string());
}

ProjectSessionResult ProjectMaterialOrganizationService::relocate_folder(
    std::string_view source, std::string_view destination) const {
  const auto catalog = current_catalog(project_);
  if (!catalog.ok()) {
    return failure(catalog.error().message);
  }
  const auto &file_system = project_->workspace()->file_system;
  const auto source_absolute =
      resolve_material_destination_folder(file_system, source);
  if (!source_absolute.ok()) {
    return failure(source_absolute.error().message);
  }
  if (source == "Materials") {
    return failure("The Materials root cannot be moved or renamed.");
  }
  if (source == destination) {
    return {.ok = true};
  }
  if (within(destination, source)) {
    return failure("A material folder cannot be moved into itself or a child.");
  }
  const auto relative = ProjectRelativePath::parse_portable(destination);
  if (!relative.ok()) {
    return failure(relative.error().message);
  }
  const auto parent = ProjectRelativePath::parse(
      relative.value().native_path().parent_path());
  if (!parent.ok()) {
    return failure(parent.error().message);
  }
  const auto parent_absolute = resolve_material_destination_folder(
      file_system, parent.value().portable_string());
  if (!parent_absolute.ok()) {
    return failure(parent_absolute.error().message);
  }
  const auto target = file_system.resolve(relative.value());
  const auto source_relative = ProjectRelativePath::parse_portable(source);
  const bool case_only_rename = source_relative.ok() &&
      source_relative.value().portable_case_key() ==
          relative.value().portable_case_key();
  if (!target.ok() &&
      !(case_only_rename && target.error().code ==
                               ProjectIoErrorCode::PathCaseCollision)) {
    return failure(target.error().message);
  }

  std::unordered_set<std::string> managed_paths;
  std::vector<ProjectAssetId> moved_ids;
  for (const auto &entry : catalog.value().entries()) {
    if (references_folder(entry.material, source)) {
      return failure("The folder contains a referenced texture resource and cannot be moved.");
    }
    if (!within(entry.path.portable_string(), source)) {
      continue;
    }
    if (entry.material.id.str() == kDefaultProjectMaterialId) {
      return failure("Default Material cannot be moved.");
    }
    managed_paths.insert(entry.path.portable_string());
    moved_ids.push_back(entry.material.id);
  }
  const auto *draft = project_->standard_material_editor().current();
  if (draft != nullptr && references_folder(draft->document, source)) {
    return failure("The folder contains a texture used by the current material draft.");
  }
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(
      source_absolute.value(), std::filesystem::directory_options::none, error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && iterator != end; iterator.increment(error)) {
    if (iterator->is_directory(error)) {
      continue;
    }
    if (error) {
      break;
    }
    const auto path = file_system.relative_from_absolute(iterator->path());
    if (!path.ok()) {
      return failure(path.error().message);
    }
    if (!managed_paths.contains(path.value().portable_string())) {
      return failure("The folder contains an unmanaged file and cannot be moved.");
    }
    const auto file = unaliased_file(file_system, path.value());
    if (!file.ok()) {
      return failure(file.error().message);
    }
  }
  if (error) {
    return failure("Could not inspect material folder: " + error.message());
  }
  OrganizationRollback rollback;
  std::filesystem::path move_source = source_absolute.value();
  if (case_only_rename) {
    const auto intermediate = child_path(
        parent.value().portable_string(),
        ".quader-move-" + std::string(ProjectAssetId::generate().str()));
    if (!intermediate.ok()) {
      return failure(intermediate.error().message);
    }
    const auto intermediate_absolute = file_system.resolve(intermediate.value());
    if (!intermediate_absolute.ok()) {
      return failure(intermediate_absolute.error().message);
    }
    const auto staged = rollback.move(move_source, intermediate_absolute.value());
    if (!staged.ok) {
      return staged;
    }
    move_source = intermediate_absolute.value();
  }
  // Resolve after staging so a spelling-only rename obeys the same portable
  // casing checks as any other move and still rejects a competing destination.
  const auto final_target = file_system.resolve(relative.value());
  if (!final_target.ok()) {
    return failure(final_target.error().message + rollback.rollback());
  }
  const auto moved = rollback.move(move_source, final_target.value());
  if (!moved.ok) {
    return failure(moved.message + rollback.rollback());
  }
  return publish_moves(*project_, rollback, moved_ids);
}

ProjectSessionResult ProjectMaterialOrganizationService::move_materials(
    std::span<const ProjectAssetId> materials,
    std::string_view destination) const {
  const auto catalog = current_catalog(project_);
  if (!catalog.ok()) {
    return failure(catalog.error().message);
  }
  const auto &file_system = project_->workspace()->file_system;
  const auto folder = resolve_material_destination_folder(file_system,
                                                          destination);
  if (!folder.ok()) {
    return failure(folder.error().message);
  }
  /// Holds an entire preflighted batch before any filesystem changes occur.
  struct PendingMove final {
    ProjectAssetId id;
    std::filesystem::path source;
    std::filesystem::path destination;
  };
  std::vector<PendingMove> pending;
  std::vector<ProjectRelativePath> targets;
  std::unordered_set<ProjectAssetId, ProjectAssetIdHash> identities;
  for (ProjectAssetId id : materials) {
    if (!identities.insert(id).second) {
      return failure("The material move contains duplicate identities.");
    }
    const auto *entry = catalog.value().find(id);
    if (entry == nullptr) {
      return failure("A dragged material is no longer in this project.");
    }
    if (id.str() == kDefaultProjectMaterialId) {
      return failure("Default Material cannot be moved.");
    }
    const auto source = unaliased_file(file_system, entry->path);
    if (!source.ok()) {
      return failure(source.error().message);
    }
    const auto leaf = ProjectRelativePath::parse(entry->path.native_path().filename());
    if (!leaf.ok()) {
      return failure(leaf.error().message);
    }
    const auto target_path = ProjectRelativePath::parse_portable(
        std::string(destination) + "/" + leaf.value().portable_string());
    if (!target_path.ok()) {
      return failure(target_path.error().message);
    }
    if (target_path.value() == entry->path) {
      continue;
    }
    const auto target = file_system.resolve_for_write(target_path.value());
    if (!target.ok()) {
      return failure(target.error().message);
    }
    std::error_code error;
    if (std::filesystem::exists(target.value(), error) || error) {
      return failure(error ? "Could not inspect move destination: " + error.message()
                           : "The destination already contains a file with this name.");
    }
    pending.push_back({id, source.value(), target.value()});
    targets.push_back(target_path.value());
  }
  const auto aliases = file_system.validate_no_path_aliases(targets);
  if (!aliases.ok()) {
    return failure(aliases.error().message);
  }
  if (pending.empty()) {
    return {.ok = true};
  }
  OrganizationRollback rollback;
  std::vector<ProjectAssetId> moved_ids;
  moved_ids.reserve(pending.size());
  for (const auto &entry : pending) {
    auto result = rollback.move(entry.source, entry.destination);
    if (!result.ok) {
      result.message += rollback.rollback();
      return result;
    }
    moved_ids.push_back(entry.id);
  }
  return publish_moves(*project_, rollback, moved_ids);
}

} // namespace quader
