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
#include "io/project/qmat_codec.hpp"

#include <QByteArray>
#include <QString>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <ranges>
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

ProjectMaterialOrganizationResult organization_failure(std::string message) {
  return {.message = std::move(message)};
}

void append_diagnostic(std::string &message, std::string_view detail) {
  if (detail.empty()) {
    return;
  }
  if (!message.empty()) {
    message += "; ";
  }
  message += detail;
}

bool within(std::string_view path, std::string_view folder) {
  return path == folder ||
         (path.size() > folder.size() && path.starts_with(folder) &&
          path[folder.size()] == '/');
}

ProjectIoResult<ProjectRelativePath> child_path(std::string_view parent,
                                                std::string_view name) {
  const QString text =
      QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
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

ProjectIoResult<ProjectRelativePath> material_path(std::string_view parent,
                                                   std::string_view name) {
  const QString text =
      QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
  const QByteArray round_trip = text.toUtf8();
  const bool valid_utf8 =
      std::string_view(round_trip.constData(),
                       static_cast<std::size_t>(round_trip.size())) == name;
  if (!valid_utf8 || text.trimmed().isEmpty() || name.size() > 256u ||
      text.size() + 5 > 255 || text.endsWith(QChar('.')) ||
      text.endsWith(QChar(' ')) || std::ranges::any_of(text, [](QChar value) {
        return value.unicode() < 32u ||
               QStringView(u"<>:\"/\\|?*").contains(value);
      })) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Enter a valid material name without separators, reserved characters, "
        "or a trailing dot or space.");
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
        "This material name is reserved by Windows.");
  }
  return ProjectRelativePath::parse_portable(std::string(parent) + "/" +
                                             std::string(name) + ".qmat");
}

std::string parent_folder(const ProjectRelativePath &path) {
  const auto parent =
      ProjectRelativePath::parse(path.native_path().parent_path());
  return parent.ok() ? parent.value().portable_string() : std::string{};
}

std::string folded_name(std::string_view name) {
  return QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()))
      .toCaseFolded()
      .toUtf8()
      .toStdString();
}

void chop_character(QString &text) {
  const qsizetype last = text.size() - 1;
  const bool surrogate_pair = last > 0 && text.at(last).isLowSurrogate() &&
                              text.at(last - 1).isHighSurrogate();
  text.chop(surrogate_pair ? 2 : 1);
}

std::string clone_name(std::string_view source, std::size_t ordinal) {
  QString stem =
      QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size()));
  const QString suffix =
      ordinal == 1u ? QStringLiteral(" Copy")
                    : QStringLiteral(" Copy ") + QString::number(ordinal);
  while (!stem.isEmpty() && (stem + suffix).toUtf8().size() > 256) {
    chop_character(stem);
  }
  if (stem.trimmed().isEmpty()) {
    stem = QStringLiteral("Material");
  }
  return (stem + suffix).toUtf8().toStdString();
}

std::string clone_filename_stem(std::string_view display_name) {
  QString stem = QString::fromUtf8(display_name.data(),
                                   static_cast<qsizetype>(display_name.size()));
  for (QChar &character : stem) {
    if (character.unicode() < 32u ||
        QStringView(u"<>:\"/\\|?*").contains(character)) {
      character = QChar('_');
    }
  }
  while (stem.size() + 5 > 255) {
    chop_character(stem);
  }
  while (!stem.isEmpty() &&
         (stem.endsWith(QChar('.')) || stem.endsWith(QChar(' ')))) {
    stem.chop(1);
  }
  if (stem.isEmpty()) {
    stem = QStringLiteral("Material Copy");
  }
  const QString device = stem.section(QChar('.'), 0, 0).toUpper();
  const bool numbered_device =
      device.size() == 4 &&
      (device.startsWith(QStringLiteral("COM")) ||
       device.startsWith(QStringLiteral("LPT"))) &&
      QStringView(u"123456789¹²³").contains(device.back());
  if (device == QStringLiteral("CON") || device == QStringLiteral("PRN") ||
      device == QStringLiteral("AUX") || device == QStringLiteral("NUL") ||
      device == QStringLiteral("CLOCK$") || numbered_device) {
    stem.prepend(QChar('_'));
  }
  return stem.toUtf8().toStdString();
}

ProjectIoResult<ProjectMaterialCatalog>
current_catalog(ProjectSession *project) {
  if (project == nullptr || project->workspace() == nullptr) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Open a project before organizing materials.");
  }
  auto scanned =
      ProjectMaterialCatalog::scan(project->workspace()->file_system);
  if (scanned.ok() && scanned.value().content_revision() !=
                          project->material_catalog().content_revision()) {
    return ProjectIoResult<ProjectMaterialCatalog>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Project materials changed on disk. Refresh the browser and try "
        "again.");
  }
  return scanned;
}

ProjectIoResult<std::filesystem::path>
unaliased_file(const ProjectFileSystem &file_system,
               const ProjectRelativePath &path) {
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

/// Compensates completed moves in reverse order until catalog publication
/// commits.
class OrganizationRollback final {
public:
  ~OrganizationRollback() {
    try {
      (void)rollback();
    } catch (...) {
      // Preserve surviving files if diagnostic allocation fails during
      // unwinding.
    }
  }

  [[nodiscard]] ProjectSessionResult
  move(const std::filesystem::path &source,
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

ProjectSessionResult publish_moves(ProjectSession &project,
                                   OrganizationRollback &rollback,
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
    if (after == nullptr ||
        after->content_revision != before.content_revision ||
        after->file_size != before.file_size ||
        after->material != before.material) {
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
    const auto &warning =
        notified.ok() ? notified.warning() : notified.error().message;
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
  for (const auto *texture :
       {&standard->base_color_texture, &standard->roughness_texture,
        &standard->metallic_texture, &standard->normal_texture}) {
    if (texture->has_value() &&
        within(texture->value().project_relative_path, folder)) {
      return true;
    }
  }
  return false;
}

} // namespace

ProjectSessionResult
ProjectMaterialOrganizationService::create_folder(std::string_view parent,
                                                  std::string_view name) const {
  const auto catalog = current_catalog(project_);
  if (!catalog.ok()) {
    return failure(catalog.error().message);
  }
  const auto &file_system = project_->workspace()->file_system;
  const auto parent_absolute =
      resolve_material_destination_folder(file_system, parent);
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
      result.message +=
          "; Could not remove the new empty folder: " + error.message();
    }
  } else {
    result.message = "Material folder created.";
  }
  return result;
}

ProjectSessionResult
ProjectMaterialOrganizationService::rename_folder(std::string_view path,
                                                  std::string_view name) const {
  const auto source = ProjectRelativePath::parse_portable(path);
  if (!source.ok()) {
    return failure(source.error().message);
  }
  const auto parent =
      ProjectRelativePath::parse(source.value().native_path().parent_path());
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
  const auto leaf =
      ProjectRelativePath::parse(source.value().native_path().filename());
  if (!leaf.ok()) {
    return failure(leaf.error().message);
  }
  const auto destination =
      child_path(destination_parent, leaf.value().portable_string());
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
  const auto parent =
      ProjectRelativePath::parse(relative.value().native_path().parent_path());
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
  const bool case_only_rename =
      source_relative.ok() && source_relative.value().portable_case_key() ==
                                  relative.value().portable_case_key();
  if (!target.ok() &&
      !(case_only_rename &&
        target.error().code == ProjectIoErrorCode::PathCaseCollision)) {
    return failure(target.error().message);
  }

  std::unordered_set<std::string> managed_paths;
  std::vector<ProjectAssetId> moved_ids;
  for (const auto &entry : catalog.value().entries()) {
    if (references_folder(entry.material, source)) {
      return failure("The folder contains a referenced texture resource and "
                     "cannot be moved.");
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
    return failure(
        "The folder contains a texture used by the current material draft.");
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
      return failure(
          "The folder contains an unmanaged file and cannot be moved.");
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
    const auto intermediate_absolute =
        file_system.resolve(intermediate.value());
    if (!intermediate_absolute.ok()) {
      return failure(intermediate_absolute.error().message);
    }
    const auto staged =
        rollback.move(move_source, intermediate_absolute.value());
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
  const auto folder =
      resolve_material_destination_folder(file_system, destination);
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
    const auto leaf =
        ProjectRelativePath::parse(entry->path.native_path().filename());
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
      return failure(
          error ? "Could not inspect move destination: " + error.message()
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

ProjectMaterialOrganizationResult
ProjectMaterialOrganizationService::rename_material(
    ProjectAssetId material, std::string_view name) const {
  const auto catalog = current_catalog(project_);
  if (!catalog.ok()) {
    return organization_failure(catalog.error().message);
  }
  const ProjectMaterialCatalogEntry *source_entry =
      catalog.value().find(material);
  if (source_entry == nullptr) {
    return organization_failure(
        "The material is no longer present in this project.");
  }
  if (material.str() == kDefaultProjectMaterialId) {
    return organization_failure("Default Material cannot be renamed.");
  }

  const std::string folder = parent_folder(source_entry->path);
  if (folder.empty()) {
    return organization_failure("The material folder path is invalid.");
  }
  const auto target_path = material_path(folder, name);
  if (!target_path.ok()) {
    return organization_failure(target_path.error().message);
  }
  for (const ProjectMaterialCatalogEntry &entry : catalog.value().entries()) {
    if (entry.material.id != material && parent_folder(entry.path) == folder &&
        folded_name(entry.material.name) == folded_name(name)) {
      return organization_failure(
          "This folder already contains a material with that name.");
    }
  }
  if (source_entry->path == target_path.value() &&
      source_entry->material.name == name) {
    return {.ok = true};
  }

  const ProjectFileSystem &file_system = project_->workspace()->file_system;
  const auto resolved_folder =
      resolve_material_destination_folder(file_system, folder);
  if (!resolved_folder.ok()) {
    return organization_failure(resolved_folder.error().message);
  }
  const auto source_absolute = unaliased_file(file_system, source_entry->path);
  if (!source_absolute.ok()) {
    return organization_failure(source_absolute.error().message);
  }
  const auto source_document = QmatCodec::read(file_system, source_entry->path);
  if (!source_document.ok()) {
    return organization_failure(source_document.error().message);
  }
  if (source_document.value().material != source_entry->material ||
      source_document.value().file_size != source_entry->file_size ||
      source_document.value().content_revision !=
          source_entry->content_revision) {
    return organization_failure(
        "The material changed on disk. Refresh the browser and try again.");
  }

  const bool same_path_key = source_entry->path.portable_case_key() ==
                             target_path.value().portable_case_key();
  std::filesystem::path target_absolute;
  if (same_path_key) {
    target_absolute = source_absolute.value();
  } else {
    const auto resolved_target =
        file_system.resolve_for_write(target_path.value());
    if (!resolved_target.ok()) {
      return organization_failure(resolved_target.error().message);
    }
    std::error_code error;
    if (std::filesystem::exists(resolved_target.value(), error) || error) {
      return organization_failure(
          error ? "Could not inspect material rename destination: " +
                      error.message()
                : "A file with that material name already exists.");
    }
    target_absolute = resolved_target.value();
  }

  OrganizationRollback moved_files;
  std::filesystem::path move_source = source_absolute.value();
  if (same_path_key && source_entry->path != target_path.value()) {
    const auto intermediate =
        child_path(folder, ".quader-rename-" +
                               std::string(ProjectAssetId::generate().str()));
    if (!intermediate.ok()) {
      return organization_failure(intermediate.error().message);
    }
    const auto intermediate_absolute =
        file_system.resolve(intermediate.value());
    if (!intermediate_absolute.ok()) {
      return organization_failure(intermediate_absolute.error().message);
    }
    const auto staged =
        moved_files.move(move_source, intermediate_absolute.value());
    if (!staged.ok) {
      return organization_failure(staged.message);
    }
    move_source = intermediate_absolute.value();
    const auto resolved_target =
        file_system.resolve_for_write(target_path.value());
    if (!resolved_target.ok()) {
      return organization_failure(resolved_target.error().message +
                                  moved_files.rollback());
    }
    target_absolute = resolved_target.value();
  }
  if (source_entry->path != target_path.value()) {
    const auto moved = moved_files.move(move_source, target_absolute);
    if (!moved.ok) {
      return organization_failure(moved.message + moved_files.rollback());
    }
  }

  const auto staged_source = QmatCodec::read(file_system, target_path.value());
  if (!staged_source.ok() ||
      staged_source.value().material != source_entry->material ||
      staged_source.value().file_size != source_entry->file_size ||
      staged_source.value().content_revision !=
          source_entry->content_revision) {
    std::string message = staged_source.ok()
                              ? "The material changed during rename staging."
                              : staged_source.error().message;
    append_diagnostic(message, moved_files.rollback());
    return organization_failure(std::move(message));
  }

  ProjectMaterialDocument renamed_document = source_entry->material;
  renamed_document.name = std::string(name);
  ProjectMaterialFileRollback rewritten_file;
  const auto rewritten =
      rewritten_file.replace_qmat(target_absolute, renamed_document);
  if (!rewritten.ok()) {
    std::string message = rewritten.error().message;
    append_diagnostic(message, moved_files.rollback());
    return organization_failure(std::move(message));
  }
  const auto fail_with_rollback = [&](std::string message) {
    append_diagnostic(message, rewritten_file.rollback());
    append_diagnostic(message, moved_files.rollback());
    return organization_failure(std::move(message));
  };

  auto scanned = ProjectMaterialCatalog::scan(file_system);
  if (!scanned.ok()) {
    return fail_with_rollback(scanned.error().message);
  }
  if (scanned.value().folders().size() != catalog.value().folders().size() ||
      !std::ranges::equal(scanned.value().folders(),
                          catalog.value().folders()) ||
      scanned.value().entries().size() != catalog.value().entries().size()) {
    return fail_with_rollback(
        "The material catalog changed during the rename.");
  }
  for (const ProjectMaterialCatalogEntry &before : catalog.value().entries()) {
    const ProjectMaterialCatalogEntry *after =
        scanned.value().find(before.material.id);
    if (after == nullptr) {
      return fail_with_rollback(
          "The material catalog changed during the rename.");
    }
    if (before.material.id == material) {
      if (after->path != target_path.value() ||
          after->material != renamed_document) {
        return fail_with_rollback(
            "The renamed material did not publish as requested.");
      }
    } else if (*after != before) {
      return fail_with_rollback("Another material changed during the rename.");
    }
  }

  auto installed = project_->install_material_catalog(scanned.take_value());
  if (!installed.ok) {
    return fail_with_rollback(installed.message);
  }
  rewritten_file.commit();
  moved_files.commit();

  const ProjectMaterialCatalogEntry *renamed =
      project_->material_catalog().find(material);
  if (renamed == nullptr) {
    return organization_failure(
        "The renamed material is missing from the installed catalog.");
  }
  project_->standard_material_editor().retarget_saved_material(
      material, renamed->path, renamed->material.name,
      renamed->content_revision);

  ProjectMaterialOrganizationResult result{
      .ok = true,
      .changed = true,
      .message = "Material renamed.",
      .material_ids = {material},
      .warnings = std::move(installed.warnings),
  };
  const auto notified = ProjectBridgeNotifications::publish_material(
      file_system, project_->manifest()->id(), *renamed);
  const std::string &warning =
      notified.ok() ? notified.warning() : notified.error().message;
  if (!warning.empty()) {
    result.warnings.push_back(warning);
  }
  for (const std::string &entry : result.warnings) {
    result.message += " " + entry;
  }
  return result;
}

ProjectMaterialOrganizationResult
ProjectMaterialOrganizationService::duplicate_materials(
    std::span<const ProjectAssetId> materials) const {
  return clone_materials(materials, {}, true);
}

ProjectMaterialOrganizationResult
ProjectMaterialOrganizationService::copy_materials(
    std::span<const ProjectAssetId> materials,
    std::string_view destination) const {
  return clone_materials(materials, destination, false);
}

ProjectMaterialOrganizationResult
ProjectMaterialOrganizationService::clone_materials(
    std::span<const ProjectAssetId> materials, std::string_view destination,
    bool preserve_source_folders) const {
  const auto catalog = current_catalog(project_);
  if (!catalog.ok()) {
    return organization_failure(catalog.error().message);
  }
  if (materials.empty()) {
    return {.ok = true};
  }
  const ProjectFileSystem &file_system = project_->workspace()->file_system;
  if (!preserve_source_folders) {
    const auto folder =
        resolve_material_destination_folder(file_system, destination);
    if (!folder.ok()) {
      return organization_failure(folder.error().message);
    }
  }

  struct PendingClone final {
    ProjectAssetId id;
    ProjectMaterialDocument document;
    ProjectRelativePath path;
    std::filesystem::path absolute;
  };
  std::vector<PendingClone> pending;
  pending.reserve(materials.size());
  std::vector<ProjectRelativePath> targets;
  targets.reserve(materials.size());
  std::unordered_set<ProjectAssetId, ProjectAssetIdHash> source_ids;
  std::unordered_set<ProjectAssetId, ProjectAssetIdHash> all_ids;
  std::unordered_set<std::string> reserved_names;
  std::unordered_set<std::wstring> reserved_paths;
  for (const ProjectMaterialCatalogEntry &entry : catalog.value().entries()) {
    all_ids.insert(entry.material.id);
    reserved_names.insert(parent_folder(entry.path) + "\n" +
                          folded_name(entry.material.name));
  }

  for (ProjectAssetId source_id : materials) {
    if (!source_ids.insert(source_id).second) {
      return organization_failure(
          "The material copy contains duplicate identities.");
    }
    const ProjectMaterialCatalogEntry *source = catalog.value().find(source_id);
    if (source == nullptr) {
      return organization_failure(
          "A copied material is no longer in this project.");
    }
    const auto source_absolute = unaliased_file(file_system, source->path);
    if (!source_absolute.ok()) {
      return organization_failure(source_absolute.error().message);
    }
    const auto committed = QmatCodec::read(file_system, source->path);
    if (!committed.ok()) {
      return organization_failure(committed.error().message);
    }
    if (committed.value().material != source->material ||
        committed.value().file_size != source->file_size ||
        committed.value().content_revision != source->content_revision) {
      return organization_failure("A copied material changed on disk. Refresh "
                                  "the browser and try again.");
    }

    const std::string target_folder = preserve_source_folders
                                          ? parent_folder(source->path)
                                          : std::string(destination);
    const auto folder =
        resolve_material_destination_folder(file_system, target_folder);
    if (!folder.ok()) {
      return organization_failure(folder.error().message);
    }

    std::optional<ProjectRelativePath> selected_path;
    std::string selected_name;
    std::filesystem::path selected_absolute;
    for (std::size_t ordinal = 1u; ordinal != 0u; ++ordinal) {
      std::string candidate_name = clone_name(source->material.name, ordinal);
      const std::string name_key =
          target_folder + "\n" + folded_name(candidate_name);
      if (reserved_names.contains(name_key)) {
        continue;
      }
      const std::string filename_stem = clone_filename_stem(candidate_name);
      const auto candidate_path = ProjectRelativePath::parse_portable(
          target_folder + "/" + filename_stem + ".qmat");
      if (!candidate_path.ok()) {
        return organization_failure(candidate_path.error().message);
      }
      if (reserved_paths.contains(candidate_path.value().portable_case_key())) {
        continue;
      }
      const auto candidate_absolute =
          file_system.resolve_for_write(candidate_path.value());
      if (!candidate_absolute.ok()) {
        if (candidate_absolute.error().code ==
                ProjectIoErrorCode::PathCaseCollision ||
            candidate_absolute.error().code ==
                ProjectIoErrorCode::FileNotRegular) {
          continue;
        }
        return organization_failure(candidate_absolute.error().message);
      }
      std::error_code error;
      const bool exists =
          std::filesystem::exists(candidate_absolute.value(), error);
      if (error) {
        return organization_failure(
            "Could not inspect material copy destination: " + error.message());
      }
      if (exists) {
        continue;
      }
      reserved_names.insert(name_key);
      reserved_paths.insert(candidate_path.value().portable_case_key());
      selected_name = std::move(candidate_name);
      selected_path = candidate_path.value();
      selected_absolute = candidate_absolute.value();
      break;
    }
    if (!selected_path.has_value()) {
      return organization_failure(
          "Could not allocate a collision-free material copy name.");
    }

    ProjectAssetId clone_id;
    do {
      clone_id = ProjectAssetId::generate();
    } while (!all_ids.insert(clone_id).second);
    ProjectMaterialDocument clone = source->material;
    clone.id = clone_id;
    clone.name = std::move(selected_name);
    targets.push_back(*selected_path);
    pending.push_back({
        .id = clone_id,
        .document = std::move(clone),
        .path = std::move(*selected_path),
        .absolute = std::move(selected_absolute),
    });
  }

  const auto aliases = file_system.validate_no_path_aliases(targets);
  if (!aliases.ok()) {
    return organization_failure(aliases.error().message);
  }
  ProjectMaterialFileRollback rollback;
  const auto fail_with_rollback = [&rollback](std::string message) {
    append_diagnostic(message, rollback.rollback());
    return organization_failure(std::move(message));
  };
  for (const PendingClone &clone : pending) {
    const auto created = rollback.create_qmat(clone.absolute, clone.document);
    if (!created.ok()) {
      return fail_with_rollback(created.error().message);
    }
  }

  auto scanned = ProjectMaterialCatalog::scan(file_system);
  if (!scanned.ok()) {
    return fail_with_rollback(scanned.error().message);
  }
  if (scanned.value().folders().size() != catalog.value().folders().size() ||
      !std::ranges::equal(scanned.value().folders(),
                          catalog.value().folders()) ||
      scanned.value().entries().size() !=
          catalog.value().entries().size() + pending.size()) {
    return fail_with_rollback("The material catalog changed during the copy.");
  }
  for (const ProjectMaterialCatalogEntry &before : catalog.value().entries()) {
    const ProjectMaterialCatalogEntry *after =
        scanned.value().find(before.material.id);
    if (after == nullptr || *after != before) {
      return fail_with_rollback(
          "An existing material changed during the copy.");
    }
  }
  for (const PendingClone &clone : pending) {
    const ProjectMaterialCatalogEntry *created = scanned.value().find(clone.id);
    if (created == nullptr || created->path != clone.path ||
        created->material != clone.document) {
      return fail_with_rollback(
          "A material copy did not publish as requested.");
    }
  }

  auto installed = project_->install_material_catalog(scanned.take_value());
  if (!installed.ok) {
    return fail_with_rollback(installed.message);
  }
  rollback.commit();

  ProjectMaterialOrganizationResult result{
      .ok = true,
      .changed = true,
      .message = preserve_source_folders ? "Materials duplicated."
                                         : "Materials copied.",
      .warnings = std::move(installed.warnings),
  };
  result.material_ids.reserve(pending.size());
  for (const PendingClone &clone : pending) {
    result.material_ids.push_back(clone.id);
    const ProjectMaterialCatalogEntry *created =
        project_->material_catalog().find(clone.id);
    if (created == nullptr) {
      continue;
    }
    const auto notified = ProjectBridgeNotifications::publish_material(
        file_system, project_->manifest()->id(), *created);
    const std::string &warning =
        notified.ok() ? notified.warning() : notified.error().message;
    if (!warning.empty()) {
      result.warnings.push_back(warning);
    }
  }
  for (const std::string &entry : result.warnings) {
    result.message += " " + entry;
  }
  return result;
}

} // namespace quader
