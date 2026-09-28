////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/assets/project_asset_service.hpp"

#include "assets/default_material_catalog.hpp"
#include "core/release_features.hpp"
#include "editor/material/project_image_decoder.hpp"
#include "editor/material/project_material_persistence.hpp"
#include "editor/material/standard_material_edit_session_service.hpp"
#include "editor/session/project_session.hpp"
#include "io/project/project_bridge_notifications.hpp"
#include "io/project/project_texture_catalog.hpp"
#include "io/project/qmat_codec.hpp"
#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_document_reader.hpp"

#include <QImageReader>
#include <QString>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <map>
#include <memory>
#include <ranges>
#include <set>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace quader {
namespace {

constexpr std::uint64_t kHashOffset = 14695981039346656037ull;
constexpr std::uint64_t kHashPrime = 1099511628211ull;

ProjectAssetOperationResult failure(std::string message) {
  return {.message = std::move(message)};
}

void append(std::string &message, std::string_view detail) {
  if (!detail.empty()) {
    if (!message.empty()) {
      message += "; ";
    }
    message += detail;
  }
}

void hash_text(std::uint64_t &hash, std::string_view text) {
  for (const unsigned char character : text) {
    hash = (hash ^ character) * kHashPrime;
  }
  hash = (hash ^ 0xffu) * kHashPrime;
}

std::string utf8(const std::filesystem::path &path) {
  const auto value = path.generic_u8string();
  return {reinterpret_cast<const char *>(value.data()), value.size()};
}

std::string parent_of(const ProjectRelativePath &path) {
  return utf8(path.native_path().parent_path());
}

bool within(std::string_view path, std::string_view directory) {
  return path == directory ||
         (path.size() > directory.size() && path.starts_with(directory) &&
          path[directory.size()] == '/');
}

ProjectIoResult<ProjectRelativePath> child_path(std::string_view parent,
                                              std::string_view filename) {
  const QString text = QString::fromUtf8(filename.data(),
                                        static_cast<qsizetype>(filename.size()));
  if (text.toUtf8().toStdString() != filename || text.trimmed().isEmpty() ||
      text.size() > 255 || text.endsWith(QChar('.')) ||
      text.endsWith(QChar(' ')) || text.startsWith(QChar('.')) ||
      std::ranges::any_of(text, [](QChar character) {
        return character.unicode() < 32u ||
               QStringView(u"<>:\"/\\|?*").contains(character);
      })) {
    return ProjectIoResult<ProjectRelativePath>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Enter a valid name without separators, reserved characters, or a "
        "leading dot or trailing dot/space.");
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
        "This name is reserved by Windows.");
  }
  return ProjectRelativePath::parse_portable(
      (parent.empty() ? std::string{} : std::string(parent) + "/") +
      std::string(filename));
}

ProjectIoResult<std::filesystem::path> unaliased(
    const ProjectFileSystem &file_system, const ProjectRelativePath &path,
    bool folder) {
  const auto physical = file_system.resolve_physical_location(path);
  if (!physical.ok()) {
    return physical;
  }
  std::error_code error;
  if (!folder) {
    const auto status = std::filesystem::status(physical.value(), error);
    if (!std::filesystem::exists(status)) {
      return ProjectIoResult<std::filesystem::path>::failure(
          ProjectIoErrorCode::FileNotFound, "The project asset no longer exists.", physical.value());
    }
    if (error || !std::filesystem::is_regular_file(status)) {
      return ProjectIoResult<std::filesystem::path>::failure(
          ProjectIoErrorCode::FileNotRegular, "The project asset is not a regular file.", physical.value());
    }
  }
  const auto links = folder ? 1u : std::filesystem::hard_link_count(
                                      physical.value(), error);
  if (error || links != 1u ||
      physical.value().lexically_relative(file_system.root()) != path.native_path()) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::PathCaseCollision,
        "Linked or aliased asset paths cannot be organized.", physical.value());
  }
  return physical;
}

bool is_texture_extension(const std::filesystem::path &path) {
  return project_texture_extension_supported(path);
}

const ProjectAssetEntry *find_entry(const ProjectAssetSnapshot &snapshot,
                                   const ProjectRelativePath &path) {
  const auto found = std::ranges::find(snapshot.entries, path,
                                      &ProjectAssetEntry::path);
  return found == snapshot.entries.end() ? nullptr : &*found;
}

ProjectIoResult<std::string> file_bytes(const std::filesystem::path &path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::IoFailure, "Could not read asset transaction data.", path);
  }
  std::string bytes{std::istreambuf_iterator<char>(stream), {}};
  return stream.bad()
             ? ProjectIoResult<std::string>::failure(
                   ProjectIoErrorCode::IoFailure, "Could not finish reading asset data.", path)
             : ProjectIoResult<std::string>::success(std::move(bytes));
}

ProjectIoResult<std::string> tree_seal(const std::filesystem::path &root) {
  std::error_code error;
  const bool directory = std::filesystem::is_directory(root, error);
  if (error || !std::filesystem::exists(root, error)) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::FileNotFound, "Asset transaction source disappeared.", root);
  }
  std::vector<std::filesystem::path> entries{root};
  if (directory) {
    std::filesystem::recursive_directory_iterator iterator(root, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && iterator != end; iterator.increment(error)) {
      entries.push_back(iterator->path());
    }
  }
  if (error) {
    return ProjectIoResult<std::string>::failure(ProjectIoErrorCode::IoFailure, error.message());
  }
  std::ranges::sort(entries);
  std::uint64_t hash = kHashOffset;
  std::array<char, 64u * 1024u> buffer{};
  for (const auto &entry : entries) {
    if (std::filesystem::is_symlink(std::filesystem::symlink_status(entry, error))) {
      return ProjectIoResult<std::string>::failure(
          ProjectIoErrorCode::PathCaseCollision, "Linked recovery entries are not supported.", entry);
    }
    const auto canonical = std::filesystem::canonical(entry, error);
    if (error || canonical != entry.lexically_normal()) {
      return ProjectIoResult<std::string>::failure(
          ProjectIoErrorCode::PathCaseCollision, "Aliased recovery entries are not supported.", entry);
    }
    hash_text(hash, entry == root ? std::string{} : utf8(entry.lexically_relative(root)));
    if (std::filesystem::is_directory(entry, error)) {
      hash_text(hash, "directory");
      continue;
    }
    if (error || std::filesystem::hard_link_count(entry, error) != 1u) {
      return ProjectIoResult<std::string>::failure(
          ProjectIoErrorCode::PathCaseCollision, "Aliased recovery file cannot be changed.", entry);
    }
    std::ifstream stream(entry, std::ios::binary);
    if (!stream) {
      return ProjectIoResult<std::string>::failure(
          ProjectIoErrorCode::IoFailure, "Could not read asset recovery baseline.", entry);
    }
    while (stream) {
      stream.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      for (std::streamsize index = 0; index < stream.gcount(); ++index) {
        hash = (hash ^ static_cast<unsigned char>(buffer[static_cast<std::size_t>(index)])) * kHashPrime;
      }
    }
    if (stream.bad()) {
      return ProjectIoResult<std::string>::failure(
          ProjectIoErrorCode::IoFailure, "Could not finish asset recovery baseline.", entry);
    }
    hash_text(hash, "file-end");
  }
  return ProjectIoResult<std::string>::success(std::to_string(hash));
}

/// Durable intent lets a subsequent open finish a committed operation or
/// compensate published files without overwriting external changes.
class AssetOrganizationJournal final {
public:
  ProjectIoResult<void> begin(const ProjectWorkspaceStorage &workspace,
                             const ProjectManifest &candidate) {
    file_system_ = workspace.file_system;
    const auto metadata = ProjectRelativePath::parse_portable(".quader");
    const auto absolute = file_system_.resolve(metadata.value());
    const auto physical = file_system_.resolve_physical_location(metadata.value());
    if (!absolute.ok() || !physical.ok() || absolute.value() != physical.value()) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::PathCaseCollision, "Project recovery storage is aliased.");
    }
    std::error_code error;
    (void)std::filesystem::create_directory(absolute.value(), error);
    if (error) {
      return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure, error.message());
    }
    const auto relative = ProjectRelativePath::parse_portable(
        ".quader/asset-organization-" + std::string(ProjectAssetId::generate().str()) + ".json");
    const auto destination = file_system_.resolve_for_write(relative.value());
    if (!destination.ok()) {
      return ProjectIoResult<void>::failure(destination.error().code, destination.error().message);
    }
    const auto baseline = file_bytes(workspace.manifest_path);
    if (!baseline.ok()) {
      return ProjectIoResult<void>::failure(baseline.error().code, baseline.error().message);
    }
    const auto manifest_path = file_system_.relative_from_absolute(workspace.manifest_path);
    if (!manifest_path.ok()) {
      return ProjectIoResult<void>::failure(manifest_path.error().code, manifest_path.error().message);
    }
    path_ = destination.value();
    data_ = {{"version", 1}, {"state", "prepared"},
             {"project", std::string(candidate.id().str())},
             {"manifest", manifest_path.value().portable_string()},
             {"baseline", baseline.value()},
             {"candidate", ProjectManifestStore::serialize(candidate)},
             {"actions", nlohmann::json::array()}};
    return persist();
  }

  ProjectIoResult<void> record_directory(const std::filesystem::path &path) {
    if (path_.empty()) return ProjectIoResult<void>::success();
    return record({{"kind", "directory"}, {"target", relative(path)}});
  }

  ProjectIoResult<void> record_move(const std::filesystem::path &source,
                                   const std::filesystem::path &destination) {
    if (path_.empty()) return ProjectIoResult<void>::success();
    const auto seal = tree_seal(source);
    if (!seal.ok()) {
      return ProjectIoResult<void>::failure(seal.error().code, seal.error().message);
    }
    return record({{"kind", "move"}, {"source", relative(source)},
                   {"target", relative(destination)}, {"seal", seal.value()}});
  }

  ProjectIoResult<void> record_file(const std::filesystem::path &staging,
                                   const std::filesystem::path &destination) {
    if (path_.empty()) return ProjectIoResult<void>::success();
    const auto seal = tree_seal(staging);
    if (!seal.ok()) {
      return ProjectIoResult<void>::failure(seal.error().code, seal.error().message);
    }
    return record({{"kind", "file"}, {"target", relative(destination)},
                   {"staging", relative(staging)}, {"seal", seal.value()}});
  }

  ProjectIoResult<void> record_replacement(const std::filesystem::path &target,
                                          std::string_view replacement) {
    if (path_.empty()) return ProjectIoResult<void>::success();
    const auto original = file_bytes(target);
    if (!original.ok()) {
      return ProjectIoResult<void>::failure(original.error().code, original.error().message);
    }
    return record({{"kind", "replacement"}, {"target", relative(target)},
                   {"before", original.value()}, {"after", replacement}});
  }

  ProjectIoResult<void> commit() {
    if (path_.empty()) {
      return ProjectIoResult<void>::success();
    }
    data_["state"] = "committed";
    return persist();
  }

  ProjectIoResult<void> prepare_rollback() {
    if (path_.empty()) return ProjectIoResult<void>::success();
    data_["state"] = "prepared";
    return persist();
  }

  void finish() noexcept {
    if (!path_.empty()) {
      std::error_code error;
      (void)std::filesystem::remove(path_, error);
      path_.clear();
    }
  }

private:
  std::string relative(const std::filesystem::path &path) const {
    const auto value = file_system_.relative_from_absolute(path);
    if (!value.ok()) {
      throw std::runtime_error(value.error().message);
    }
    return value.value().portable_string();
  }

  ProjectIoResult<void> record(nlohmann::json action) {
    if (path_.empty()) {
      return ProjectIoResult<void>::success();
    }
    data_["actions"].push_back(std::move(action));
    return persist();
  }

  ProjectIoResult<void> persist() const {
    const auto written = quader_io::write_qdr_text_atomic(path_, data_.dump(2));
    return written.ok ? ProjectIoResult<void>::success()
                      : ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                                        written.message);
  }

  ProjectFileSystem file_system_;
  std::filesystem::path path_;
  nlohmann::json data_;
};

/// Retains physical operations until catalog and manifest publication commit.
class AssetFileTransaction final {
public:
  ~AssetFileTransaction() {
    if (committed_) return;
    try {
      if (!rollback().empty()) preserve();
    } catch (...) {
      // Leave recovery artifacts intact if unwinding cannot allocate a message.
      preserve();
    }
  }

  ProjectIoResult<void> begin(const ProjectWorkspaceStorage &workspace,
                             const ProjectManifest &candidate) {
    return journal_.begin(workspace, candidate);
  }

  ProjectIoResult<void> record_replacement(const std::filesystem::path &path,
                                          std::string_view replacement) {
    return journal_.record_replacement(path, replacement);
  }

  ProjectIoResult<void> prepare_commit() {
    const auto marked = journal_.commit();
    if (marked.ok()) commit_intent_ = true;
    return marked;
  }

  ProjectIoResult<void> abort_commit() {
    if (!commit_intent_) return ProjectIoResult<void>::success();
    const auto reset = journal_.prepare_rollback();
    if (reset.ok()) commit_intent_ = false;
    return reset;
  }

  void preserve() noexcept {
    for (auto &entry : files_) (void)entry->preserve_published_for_recovery();
    files_.clear();
    committed_ = true;
  }

  ProjectIoResult<void> create_directory(const std::filesystem::path &path) {
    const auto recorded = journal_.record_directory(path);
    if (!recorded.ok()) {
      return recorded;
    }
    std::error_code error;
    if (!std::filesystem::create_directory(path, error)) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::IoFailure,
          error ? error.message() : "An entry with this name already exists.",
          path);
    }
    directories_.push_back(path);
    return ProjectIoResult<void>::success();
  }

  ProjectIoResult<void> move(const std::filesystem::path &source,
                             const std::filesystem::path &destination) {
    const auto recorded = journal_.record_move(source, destination);
    if (!recorded.ok()) {
      return recorded;
    }
    moves_.emplace_back(source, destination);
    const auto moved =
        quader_io::move_project_entry_no_replace(source, destination);
    if (!moved.ok) {
      moves_.pop_back();
      return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                             moved.message);
    }
    return ProjectIoResult<void>::success();
  }

  template <typename Writer>
  ProjectIoResult<void> create(const std::filesystem::path &path,
                               Writer &&writer) {
    auto transaction = std::make_unique<quader_io::QdrAtomicFileTransaction>();
    const auto prepared = transaction->prepare(
        path, quader_io::QdrAtomicWriteDisposition::CreateNew);
    if (!prepared.ok) {
      return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                             prepared.message);
    }
    const auto written = writer(transaction->staging_path());
    if (!written.ok()) {
      return written;
    }
    const auto recorded = journal_.record_file(transaction->staging_path(), path);
    if (!recorded.ok()) {
      return recorded;
    }
    const auto published = transaction->publish();
    if (!published.ok) {
      return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                             published.message);
    }
    files_.push_back(std::move(transaction));
    return ProjectIoResult<void>::success();
  }

  ProjectIoResult<void> create_bytes(const std::filesystem::path &path,
                                    std::span<const std::byte> bytes) {
    return create(path, [bytes](const auto &staging) {
      const auto written = quader_io::write_qdr_text_atomic(
          staging, {reinterpret_cast<const char *>(bytes.data()), bytes.size()});
      return written.ok
                 ? ProjectIoResult<void>::success()
                 : ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                                   written.message);
    });
  }

  std::string rollback() {
    const auto aborted = abort_commit();
    if (!aborted.ok()) {
      preserve();
      return "Asset commit recovery was preserved: " + aborted.error().message;
    }
    std::string result;
    for (auto entry = files_.rbegin(); entry != files_.rend(); ++entry) {
      const auto rolled_back = (*entry)->rollback();
      if (!rolled_back.ok) {
        append(result, rolled_back.message);
      }
    }
    if (!result.empty()) {
      return result;
    }
    files_.clear();
    for (auto entry = moves_.rbegin(); entry != moves_.rend(); ++entry) {
      const auto restored = quader_io::move_project_entry_no_replace(
          entry->second, entry->first);
      if (!restored.ok) {
        append(result, restored.message);
        return result;
      }
    }
    moves_.clear();
    for (auto entry = directories_.rbegin(); entry != directories_.rend();
         ++entry) {
      std::error_code error;
      (void)std::filesystem::remove(*entry, error);
      if (error) {
        append(result, error.message());
      }
    }
    directories_.clear();
    if (result.empty()) {
      journal_.finish();
    }
    return result;
  }

  std::vector<std::string> commit() {
    committed_ = true;
    std::vector<std::string> warnings;
    for (auto &entry : files_) {
      const auto committed = entry->commit();
      if (!committed.ok) {
        warnings.push_back(committed.message);
        (void)entry->preserve_published_for_recovery();
      }
    }
    files_.clear();
    moves_.clear();
    directories_.clear();
    if (warnings.empty()) {
      journal_.finish();
    }
    return warnings;
  }

private:
  std::vector<std::unique_ptr<quader_io::QdrAtomicFileTransaction>> files_;
  std::vector<std::pair<std::filesystem::path, std::filesystem::path>> moves_;
  std::vector<std::filesystem::path> directories_;
  AssetOrganizationJournal journal_;
  bool committed_ = false;
  bool commit_intent_ = false;
};

ProjectIoResult<ProjectRelativePath> unique_path(
    const ProjectFileSystem &file_system, std::string_view folder,
    std::string_view stem, std::string_view extension,
    std::set<std::wstring> &reserved, bool numbered = true) {
  for (std::size_t ordinal = 1u; ordinal != 0u; ++ordinal) {
    const std::string filename = std::string(stem) +
                                 (ordinal == 1u ? std::string{}
                                                : "_" + std::to_string(ordinal)) +
                                 std::string(extension);
    const auto path = child_path(folder, filename);
    if (!path.ok()) {
      return path;
    }
    if (is_project_asset_reserved_path(path.value())) {
      return ProjectIoResult<ProjectRelativePath>::failure(
          ProjectIoErrorCode::InvalidRelativePath,
          "This location is reserved for project metadata.");
    }
    const auto absolute = file_system.resolve(path.value());
    if (!absolute.ok()) {
      if (numbered && absolute.error().code == ProjectIoErrorCode::PathCaseCollision) {
        continue;
      }
      return ProjectIoResult<ProjectRelativePath>::failure(
          absolute.error().code, absolute.error().message);
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(absolute.value(), error);
    if (error) {
      return ProjectIoResult<ProjectRelativePath>::failure(
          ProjectIoErrorCode::IoFailure, error.message());
    }
    if (!exists && reserved.insert(path.value().portable_case_key()).second) {
      return path;
    }
    if (!numbered) {
      return ProjectIoResult<ProjectRelativePath>::failure(
          ProjectIoErrorCode::DuplicateAsset,
          "An asset or folder with this name already exists.");
    }
  }
  return ProjectIoResult<ProjectRelativePath>::failure(
      ProjectIoErrorCode::DuplicateAsset, "Could not allocate a unique asset name.");
}

ProjectIoResult<void> validate_folder_contents(
    const ProjectFileSystem &file_system, const ProjectAssetSnapshot &snapshot,
    const ProjectRelativePath &folder) {
  const auto absolute = unaliased(file_system, folder, true);
  if (!absolute.ok()) {
    return ProjectIoResult<void>::failure(absolute.error().code,
                                          absolute.error().message);
  }
  std::error_code error;
  std::filesystem::recursive_directory_iterator iterator(absolute.value(), error);
  const std::filesystem::recursive_directory_iterator end;
  for (; !error && iterator != end; iterator.increment(error)) {
    const auto relative = file_system.relative_from_absolute(iterator->path());
    if (!relative.ok() || find_entry(snapshot, relative.value()) == nullptr) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::UnsupportedFormat,
          "This folder contains hidden, unsupported, or invalid files. Move "
          "those files outside the folder before organizing it.", iterator->path());
    }
    const bool directory = iterator->is_directory(error);
    const auto physical = unaliased(file_system, relative.value(), directory);
    if (!physical.ok()) {
      return ProjectIoResult<void>::failure(physical.error().code,
                                            physical.error().message);
    }
  }
  return error ? ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
                                                error.message())
               : ProjectIoResult<void>::success();
}

ProjectIoResult<void> validate_material_catalog(
    const ProjectMaterialCatalog &catalog) {
  const auto id = ProjectAssetId::parse(kDefaultProjectMaterialId);
  if (!id.has_value() || catalog.find(*id) == nullptr) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "The project must retain its built-in Default Material.");
  }
  return ProjectIoResult<void>::success();
}

ProjectIoResult<ProjectManifest> discover_manifest(ProjectSession &project) {
  // The operation preflight already compared every saved QMAT with this
  // installed catalog. Texture discovery does not need to parse them again.
  return ProjectTextureCatalog::discover(project.workspace()->file_system,
                                         *project.manifest(), project.material_catalog());
}

void publish_organized_catalog(ProjectSession &project, ProjectMaterialCatalog catalog) {
  const auto &previous = project.material_catalog();
  const bool appearance_changed = previous.entries().size() != catalog.entries().size() ||
      std::ranges::any_of(catalog.entries(), [&](const auto &entry) {
        const auto *old = previous.find(entry.material.id);
        return old == nullptr || old->material.kind != entry.material.kind ||
               old->material.payload != entry.material.payload;
      });
  project.publish_material_catalog(std::move(catalog), appearance_changed);
}

void update_snapshot_revision(ProjectAssetSnapshot &snapshot) {
  std::ranges::sort(snapshot.entries, {}, [](const ProjectAssetEntry &entry) {
    return entry.path.portable_string();
  });
  snapshot.revision = kHashOffset;
  hash_text(snapshot.revision, snapshot.project_id.str());
  for (const auto &entry : snapshot.entries) {
    hash_text(snapshot.revision, entry.path.portable_string());
    hash_text(snapshot.revision, entry.asset_id.str());
    hash_text(snapshot.revision, entry.scene_id.str());
    hash_text(snapshot.revision, std::to_string(entry.content_revision));
  }
}

} // namespace

bool is_project_asset_reserved_path(const ProjectRelativePath &path) {
  return !project_asset_path_visible(path);
}

ProjectIoResult<std::filesystem::path> resolve_project_asset_folder(
    const ProjectFileSystem &file_system, std::string_view folder) {
  if (folder.empty()) {
    return file_system.valid()
               ? ProjectIoResult<std::filesystem::path>::success(file_system.root())
               : ProjectIoResult<std::filesystem::path>::failure(
                     ProjectIoErrorCode::InvalidWorkspaceState,
                     "Open a project before selecting an asset folder.");
  }
  const auto path = ProjectRelativePath::parse_portable(folder);
  if (!path.ok() || path.value().portable_string() != folder ||
      is_project_asset_reserved_path(path.value())) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::InvalidRelativePath,
        "Choose an exact folder path inside the current project.");
  }
  const auto absolute = unaliased(file_system, path.value(), true);
  if (!absolute.ok()) {
    return absolute;
  }
  std::error_code error;
  if (!std::filesystem::is_directory(absolute.value(), error) || error) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::DirectoryNotFound,
        "The project asset folder does not exist.", absolute.value());
  }
  return absolute;
}

const ProjectIoResult<ProjectAssetSnapshot> &ProjectAssetService::snapshot() const {
  const ProjectId no_project;
  const ProjectId &project_id = project_->manifest() == nullptr
                                    ? no_project
                                    : project_->manifest()->id();
  if (!cached_.has_value() || cached_project_ != project_id ||
      cached_material_revision_ != project_->material_catalog().content_revision() ||
      cached_project_generation_ != project_->project_generation()) {
    return refresh();
  }
  return *cached_;
}

const ProjectIoResult<ProjectAssetSnapshot> &ProjectAssetService::refresh() const {
  cached_ = scan();
  cached_project_ = project_->manifest() == nullptr ? ProjectId{}
                                                   : project_->manifest()->id();
  cached_material_revision_ = project_->material_catalog().content_revision();
  cached_project_generation_ = project_->project_generation();
  return *cached_;
}

ProjectIoResult<ProjectAssetSnapshot> ProjectAssetService::inspect(
    std::span<const ProjectRelativePath> roots) const {
  if (roots.empty()) {
    return ProjectIoResult<ProjectAssetSnapshot>::failure(
        ProjectIoErrorCode::InvalidArgument, "Select project assets to inspect.");
  }
  return scan(roots);
}

std::uint64_t ProjectAssetService::revision() const noexcept {
  return cached_.has_value() && cached_->ok() ? cached_->value().revision : 0u;
}

void ProjectAssetService::invalidate() noexcept { cached_.reset(); }

void ProjectAssetService::retain_committed_changes(
    ProjectAssetSnapshot previous, std::span<const ProjectRelativePath> removed,
    std::span<const ProjectRelativePath> changed) {
  const auto current = changed.empty()
      ? ProjectIoResult<ProjectAssetSnapshot>::success(ProjectAssetSnapshot{})
      : inspect(changed);
  if (!current.ok()) {
    invalidate();
    return;
  }
  std::erase_if(previous.entries, [&](const auto &entry) {
    const auto matches = [&](const auto &root) {
      return within(entry.path.portable_string(), root.portable_string());
    };
    return std::ranges::any_of(removed, matches) || std::ranges::any_of(changed, matches);
  });
  previous.entries.insert(previous.entries.end(), current.value().entries.begin(), current.value().entries.end());
  // Discovery may have registered legacy texture identities while committing.
  for (auto &entry : previous.entries) {
    if (entry.kind == ProjectAssetKind::Texture) {
      if (const auto *texture = project_->manifest()->find_texture(entry.path)) entry.asset_id = texture->id;
    } else if (entry.kind == ProjectAssetKind::Scene) {
      if (const auto *scene = project_->manifest()->find_scene(entry.path)) {
        entry.scene_id = scene->id;
      } else if (project_->manifest()->find_scene(entry.scene_id) != nullptr) {
        entry.scene_id = {};
      } else if (!entry.scene_id.valid()) {
        // Removing a registered scene can make a previously ambiguous external
        // copy independently organizable again. Recover just that file's UUID.
        const auto scene = inspect(std::array{entry.path});
        if (scene.ok() && scene.value().entries.size() == 1u) {
          entry.scene_id = scene.value().entries.front().scene_id;
        }
      }
    }
  }
  update_snapshot_revision(previous);
  cached_ = ProjectIoResult<ProjectAssetSnapshot>::success(std::move(previous));
  cached_project_ = project_->manifest()->id();
  cached_material_revision_ = project_->material_catalog().content_revision();
  cached_project_generation_ = project_->project_generation();
}

ProjectIoResult<void> ProjectAssetService::recover_pending_operations(
    const std::filesystem::path &manifest_path) {
  try {
    const auto opened = ProjectFileSystem::open(manifest_path.parent_path());
    if (!opened.ok()) {
      return ProjectIoResult<void>::failure(opened.error().code, opened.error().message);
    }
    const auto &file_system = opened.value();
    const auto manifest_relative = file_system.relative_from_absolute(manifest_path);
    if (!manifest_relative.ok()) {
      return ProjectIoResult<void>::failure(manifest_relative.error().code,
                                            manifest_relative.error().message);
    }
    const auto metadata = ProjectRelativePath::parse_portable(".quader").value();
    const auto directory = file_system.resolve(metadata);
    const auto physical_directory = file_system.resolve_physical_location(metadata);
    if (!directory.ok() || !physical_directory.ok() ||
        directory.value() != physical_directory.value()) {
      return ProjectIoResult<void>::failure(
          ProjectIoErrorCode::PathCaseCollision, "Asset recovery storage is aliased.");
    }
    std::error_code error;
    if (!std::filesystem::exists(directory.value(), error)) {
      return error ? ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure, error.message())
                   : ProjectIoResult<void>::success();
    }
    std::vector<std::filesystem::path> journals;
    for (std::filesystem::directory_iterator iterator(directory.value(), error), end;
         !error && iterator != end; iterator.increment(error)) {
      const std::string name = utf8(iterator->path().filename());
      if (name.starts_with("asset-organization-") && name.ends_with(".json")) {
        const std::string_view id(name.data() + 19u, name.size() - 24u);
        if (!ProjectAssetId::parse(id).has_value()) {
          throw std::runtime_error("Invalid asset organization recovery filename.");
        }
        journals.push_back(iterator->path());
      }
    }
    if (error) throw std::runtime_error(error.message());
    std::ranges::sort(journals);
    const auto exists = [](const std::filesystem::path &path) {
      std::error_code ec;
      if (!std::filesystem::exists(path.parent_path(), ec)) {
        if (ec) throw std::runtime_error(ec.message());
        return false;
      }
      for (std::filesystem::directory_iterator iterator(path.parent_path(), ec), end;
           !ec && iterator != end; iterator.increment(ec)) {
        if (iterator->path().filename() == path.filename()) return true;
      }
      if (ec) throw std::runtime_error(ec.message());
      return false;
    };
    const auto remove_exact = [](const std::filesystem::path &path) {
      std::error_code ec;
      (void)std::filesystem::remove(path, ec);
      if (ec) throw std::runtime_error("Could not remove recovered asset: " + ec.message());
    };
    for (const auto &journal_path : journals) {
      const auto journal_relative = file_system.relative_from_absolute(journal_path);
      if (!journal_relative.ok()) throw std::runtime_error(journal_relative.error().message);
      const auto validated_journal = unaliased(file_system, journal_relative.value(), false);
      if (!validated_journal.ok()) throw std::runtime_error(validated_journal.error().message);
      if (std::filesystem::file_size(journal_path) > 128u * 1024u * 1024u) {
        throw std::runtime_error("Asset organization recovery journal exceeds its size limit.");
      }
      const auto encoded = file_bytes(journal_path);
      if (!encoded.ok()) throw std::runtime_error(encoded.error().message);
      const auto journal = nlohmann::json::parse(encoded.value());
      if (journal.at("version").get<int>() != 1 ||
          journal.at("manifest").get<std::string>() != manifest_relative.value().portable_string()) {
        throw std::runtime_error("Asset organization recovery journal does not match this project.");
      }
      const auto baseline = journal.at("baseline").get<std::string>();
      const auto candidate = journal.at("candidate").get<std::string>();
      const auto baseline_manifest = ProjectManifestStore::deserialize(baseline);
      const auto candidate_manifest = ProjectManifestStore::deserialize(candidate);
      if (!baseline_manifest.ok() || !candidate_manifest.ok() ||
          baseline_manifest.value().id() != candidate_manifest.value().id() ||
          candidate_manifest.value().id().str() != journal.at("project").get<std::string>()) {
        throw std::runtime_error("Asset recovery manifest baselines are invalid.");
      }
      const auto current = file_bytes(manifest_path);
      if (!current.ok()) throw std::runtime_error(current.error().message);
      const std::string state = journal.at("state").get<std::string>();
      if (state != "prepared" && state != "committed") {
        throw std::runtime_error("Asset recovery journal has an invalid state.");
      }
      const bool committed = state == "committed" ||
                             (candidate != baseline && current.value() == candidate);
      if (!committed && current.value() != baseline) {
        throw std::runtime_error(
            "Project manifest changed after the interrupted asset operation. "
            "Recovery data was preserved without replacing the newer project.");
      }
      const auto &actions = journal.at("actions");
      if (!actions.is_array()) throw std::runtime_error("Invalid asset recovery action list.");
      std::map<std::string, ProjectRelativePath> case_aliases;
      for (std::size_t index = 1u; index < actions.size(); ++index) {
        const auto &first = actions[index - 1u];
        const auto &second = actions[index];
        if (first.at("kind") != "move" || second.at("kind") != "move" ||
            first.at("target") != second.at("source")) continue;
        const auto staged = ProjectRelativePath::parse_portable(first.at("target").get<std::string>());
        const auto original = ProjectRelativePath::parse_portable(first.at("source").get<std::string>());
        const auto final = ProjectRelativePath::parse_portable(second.at("target").get<std::string>());
        if (!staged.ok() || !original.ok() || !final.ok()) continue;
        const auto staged_leaf = utf8(staged.value().native_path().filename());
        if (!staged_leaf.starts_with(".quader-asset-rename-") ||
            !ProjectAssetId::parse(std::string_view(staged_leaf).substr(21u)).has_value() ||
            parent_of(original.value()) != parent_of(staged.value()) ||
            parent_of(original.value()) != parent_of(final.value()) ||
            original.value().portable_case_key() != final.value().portable_case_key()) continue;
        case_aliases.emplace(original.value().portable_string(), final.value());
        case_aliases.emplace(final.value().portable_string(), original.value());
      }
      const auto resolve_action_path = [&](std::string_view value, bool private_path) {
        const auto relative = ProjectRelativePath::parse_portable(value);
        if (!relative.ok() ||
            (!private_path && is_project_asset_reserved_path(relative.value()))) {
          throw std::runtime_error("Invalid authored path in asset recovery journal.");
        }
        const auto absolute = file_system.resolve(relative.value());
        const auto physical = file_system.resolve_physical_location(relative.value());
        if (!absolute.ok() && absolute.error().code == ProjectIoErrorCode::PathCaseCollision) {
          const auto alias = case_aliases.find(std::string(value));
          if (alias != case_aliases.end()) {
            const auto counterpart = file_system.resolve(alias->second);
            const auto counterpart_physical = file_system.resolve_physical_location(alias->second);
            if (counterpart.ok() && counterpart_physical.ok() &&
                counterpart.value() == counterpart_physical.value()) {
              // The verified two-move chain changes one leaf's case under the
              // same exact parent. Keep its intended spelling for compensation.
              return counterpart.value().parent_path() / relative.value().native_path().filename();
            }
          }
        }
        if (!absolute.ok() || !physical.ok() || absolute.value() != physical.value()) {
          throw std::runtime_error("Asset recovery path is linked or outside the project.");
        }
        return absolute.value();
      };
      const auto validate_authored_or_case_stage = [&](const std::string &value) {
        const auto relative = ProjectRelativePath::parse_portable(value);
        if (!relative.ok()) throw std::runtime_error(relative.error().message);
        const auto leaf = utf8(relative.value().native_path().filename());
        const bool rename_stage = leaf.starts_with(".quader-asset-rename-") &&
            ProjectAssetId::parse(std::string_view(leaf).substr(21u)).has_value();
        if (rename_stage) {
          const auto parent = parent_of(relative.value());
          const auto resolved_parent = resolve_project_asset_folder(file_system, parent);
          if (!resolved_parent.ok()) throw std::runtime_error(resolved_parent.error().message);
        }
        return resolve_action_path(value, rename_stage);
      };
      // Validate every path before performing the first compensation.
      for (const auto &action : actions) {
        const auto kind = action.at("kind").get<std::string>();
        const auto target = validate_authored_or_case_stage(action.at("target").get<std::string>());
        if (kind == "move") {
          (void)validate_authored_or_case_stage(action.at("source").get<std::string>());
          (void)action.at("seal").get<std::string>();
        } else if (kind == "file") {
          const auto staging = resolve_action_path(action.at("staging").get<std::string>(), true);
          const auto staging_name = utf8(staging.filename());
          if (staging.parent_path() != target.parent_path() ||
              !staging_name.starts_with("." + utf8(target.filename()) + ".") ||
              staging.extension() != ".stage") {
            throw std::runtime_error("Invalid private asset staging path.");
          }
          (void)action.at("seal").get<std::string>();
        } else if (kind == "replacement") {
          (void)action.at("before").get<std::string>();
          (void)action.at("after").get<std::string>();
        } else if (kind != "directory") {
          throw std::runtime_error("Unknown asset recovery action.");
        }
      }
      if (committed && candidate != baseline && current.value() == baseline) {
        // The durable commit intent precedes manifest publication. Only finish
        // that publication when every final authored result is still exact.
        for (const auto &action : actions) {
          const auto kind = action.at("kind").get<std::string>();
          const auto target_text = action.at("target").get<std::string>();
          const auto target = validate_authored_or_case_stage(target_text);
          if (kind == "move" && std::ranges::any_of(actions, [&](const auto &later) {
                return later.at("kind") == "move" && later.at("source") == target_text;
              })) continue;
          if (!exists(target)) {
            throw std::runtime_error("A committed asset result is missing; recovery was preserved.");
          }
          if (kind == "directory") {
            if (!std::filesystem::is_directory(target)) {
              throw std::runtime_error("A committed asset folder was replaced; recovery was preserved.");
            }
          } else if (kind == "replacement") {
            const auto bytes = file_bytes(target);
            if (!bytes.ok() || bytes.value() != action.at("after").get<std::string>()) {
              throw std::runtime_error("A renamed material changed; recovery was preserved.");
            }
          } else {
            const bool replaced = std::ranges::any_of(actions, [&](const auto &other) {
              return other.at("kind") == "replacement" && other.at("target") == target_text;
            });
            if (replaced) continue;
            const auto seal = tree_seal(target);
            if (!seal.ok() || seal.value() != action.at("seal").get<std::string>()) {
              throw std::runtime_error("Committed asset content changed; recovery was preserved.");
            }
          }
        }
        const auto restored_manifest = ProjectManifestStore::save_atomic(
            file_system, manifest_path, candidate_manifest.value());
        if (!restored_manifest.ok()) throw std::runtime_error(restored_manifest.error().message);
      }
      if (!committed) {
        for (auto action = actions.rbegin(); action != actions.rend(); ++action) {
          const auto kind = action->at("kind").get<std::string>();
          const auto target = validate_authored_or_case_stage(action->at("target").get<std::string>());
          if (kind == "move") {
            const auto source = validate_authored_or_case_stage(action->at("source").get<std::string>());
            const bool source_exists = exists(source);
            const bool target_exists = exists(target);
            if (source_exists && !target_exists) {
              const auto source_seal = tree_seal(source);
              if (!source_seal.ok() || source_seal.value() != action->at("seal").get<std::string>()) {
                throw std::runtime_error("An asset recovery source changed; recovery was preserved.");
              }
              continue;
            }
            if (source_exists || !target_exists) {
              throw std::runtime_error("Asset recovery found conflicting or missing move locations.");
            }
            const auto seal = tree_seal(target);
            if (!seal.ok() || seal.value() != action->at("seal").get<std::string>()) {
              throw std::runtime_error("An asset changed after its move; recovery was preserved.");
            }
            const auto moved = quader_io::move_project_entry_no_replace(target, source);
            if (!moved.ok) throw std::runtime_error(moved.message);
          } else if (kind == "file") {
            if (exists(target)) {
              const auto seal = tree_seal(target);
              if (!seal.ok() || seal.value() != action->at("seal").get<std::string>()) {
                throw std::runtime_error("A copied asset has newer content; recovery was preserved.");
              }
              remove_exact(target);
            }
            const auto staging = resolve_action_path(action->at("staging").get<std::string>(), true);
            if (exists(staging)) {
              const auto seal = tree_seal(staging);
              if (!seal.ok() || seal.value() != action->at("seal").get<std::string>()) {
                throw std::runtime_error("Asset staging bytes changed; recovery was preserved.");
              }
              remove_exact(staging);
            }
          } else if (kind == "replacement" && exists(target)) {
            const auto bytes = file_bytes(target);
            if (!bytes.ok()) throw std::runtime_error(bytes.error().message);
            if (bytes.value() != action->at("before").get<std::string>()) {
              if (bytes.value() != action->at("after").get<std::string>()) {
                throw std::runtime_error("An edited material has newer content; recovery was preserved.");
              }
              const auto restored = quader_io::write_qdr_text_atomic(target, action->at("before").get<std::string>());
              if (!restored.ok) throw std::runtime_error(restored.message);
            }
          } else if (kind == "directory" && exists(target)) {
            remove_exact(target);
          }
        }
      }
      for (const auto &action : actions) {
        if (action.at("kind") != "file") continue;
        const auto staging = resolve_action_path(action.at("staging").get<std::string>(), true);
        if (!exists(staging)) continue;
        const auto seal = tree_seal(staging);
        if (!seal.ok() || seal.value() != action.at("seal").get<std::string>()) {
          throw std::runtime_error("Asset staging bytes changed; recovery was preserved.");
        }
        remove_exact(staging);
      }
      remove_exact(journal_path);
    }
    return ProjectIoResult<void>::success();
  } catch (const std::exception &error) {
    return ProjectIoResult<void>::failure(ProjectIoErrorCode::IoFailure,
        std::string("Could not recover interrupted asset organization: ") + error.what());
  }
}

ProjectIoResult<ProjectAssetSnapshot> ProjectAssetService::scan(
    std::span<const ProjectRelativePath> roots) const {
  if (project_ == nullptr || project_->workspace() == nullptr) {
    return ProjectIoResult<ProjectAssetSnapshot>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Open a project before browsing project assets.");
  }
  const auto &workspace = *project_->workspace();
  ++work_.scans;
  const auto file_system = workspace.file_system.begin_resolution_batch();
  ProjectAssetSnapshot result{.project_id = workspace.manifest.id(),
                              .revision = kHashOffset};
  std::error_code error;
  std::optional<ProjectIoError> inspection_error;
  const auto inspect_entry = [&](const std::filesystem::directory_entry &source) {
    ++work_.inspected_entries;
    // Enumeration already provides a path under this root. Parse its spelling
    // first; the physical authority below performs containment/link validation.
    const auto relative = ProjectRelativePath::parse(
        source.path().lexically_relative(file_system.root()));
    if (!relative.ok()) {
      inspection_error = relative.error();
      return false;
    }
    const bool directory = source.is_directory(error);
    if (error) {
      return false;
    }
    if (is_project_asset_reserved_path(relative.value())) {
      return false;
    }
    const auto physical = unaliased(file_system, relative.value(), directory);
    if (!physical.ok()) {
      if (physical.error().code == ProjectIoErrorCode::SymlinkEscape ||
          physical.error().code == ProjectIoErrorCode::PathOutsideProject) {
        inspection_error = physical.error();
      }
      return false;
    }
    ProjectAssetEntry entry{.path = relative.value()};
    entry.name = utf8(directory ? entry.path.native_path().filename()
                                : entry.path.native_path().stem());
    if (directory) {
      entry.kind = ProjectAssetKind::Folder;
    } else {
      const auto extension =
          QString::fromStdWString(entry.path.native_path().extension().wstring())
              .toLower();
      if (extension == QStringLiteral(".qdr")) {
        const auto read = QdrDocumentReader::read_project_dependencies(physical.value());
        if (!read.ok) {
          return false;
        }
        entry.kind = ProjectAssetKind::Scene;
        const auto *registered = workspace.manifest.find_scene(entry.path);
        entry.scene_id = registered != nullptr ? registered->id
                                               : SceneId::from_bytes(read.scene_uuid);
        if (registered == nullptr && workspace.manifest.find_scene(entry.scene_id) != nullptr) {
          entry.scene_id = {};
        }
        entry.content_revision = read.source_content_hash;
      } else if (extension == QStringLiteral(".qmat") &&
                 release_has_feature(ReleaseFeature::Materials)) {
        const auto current = QmatCodec::read(file_system, entry.path);
        if (!current.ok()) {
          return false;
        }
        entry.kind = ProjectAssetKind::Material;
        entry.asset_id = current.value().material.id;
        entry.name = current.value().material.name;
        entry.content_revision = current.value().content_revision;
      } else if (release_has_feature(ReleaseFeature::Materials) &&
                 is_texture_extension(entry.path.native_path())) {
        entry.kind = ProjectAssetKind::Texture;
        if (const auto *texture = workspace.manifest.find_texture(entry.path)) {
          entry.asset_id = texture->id;
        }
      } else {
        return false;
      }
      const auto size = source.file_size(error);
      if (error) {
        return false;
      }
      if (entry.kind == ProjectAssetKind::Texture) {
        entry.content_revision = kHashOffset;
        hash_text(entry.content_revision, std::to_string(size));
      }
    }
    // Folder contents are represented by the sorted entry paths. Directory
    // timestamps also change for private staging files and can settle lazily on
    // Windows; they are not authored asset revisions.
    if (entry.kind == ProjectAssetKind::Texture) {
      const auto modified = source.last_write_time(error);
      if (error) {
        return false;
      }
      hash_text(entry.content_revision,
                std::to_string(modified.time_since_epoch().count()));
    }
    result.entries.push_back(std::move(entry));
    return directory;
  };
  const auto inspect_directory = [&](const std::filesystem::path &directory) {
    std::filesystem::recursive_directory_iterator iterator(directory, error);
    const std::filesystem::recursive_directory_iterator end;
    for (; !error && !inspection_error && iterator != end; iterator.increment(error)) {
      if (!inspect_entry(*iterator)) iterator.disable_recursion_pending();
    }
  };
  if (roots.empty()) {
    inspect_directory(file_system.root());
  } else {
    for (const auto &root : roots) {
      const auto absolute = file_system.resolve(root);
      if (!absolute.ok()) {
        inspection_error = absolute.error();
        break;
      }
      const std::filesystem::directory_entry source(absolute.value(), error);
      if (error) break;
      if (inspect_entry(source)) inspect_directory(absolute.value());
      if (error || inspection_error) break;
    }
  }
  if (inspection_error) {
    return ProjectIoResult<ProjectAssetSnapshot>::failure(
        inspection_error->code, inspection_error->message, inspection_error->path);
  }
  if (error) {
    return ProjectIoResult<ProjectAssetSnapshot>::failure(
        ProjectIoErrorCode::IoFailure, "Could not enumerate project assets: " + error.message());
  }
  update_snapshot_revision(result);
  return ProjectIoResult<ProjectAssetSnapshot>::success(std::move(result));
}

ProjectIoResult<ProjectAssetSnapshot> ProjectAssetService::preflight(
    const ProjectId &expected_project, std::uint64_t expected_revision) const {
  if (project_->manifest() == nullptr ||
      (expected_project.valid() && expected_project != project_->manifest()->id())) {
    return ProjectIoResult<ProjectAssetSnapshot>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "The asset selection belongs to a different project.");
  }
  auto current = scan();
  if (current.ok()) {
    // A legacy intent can arrive before the generic browser's first snapshot.
    // Keep the already-published owner catalog as its baseline instead of
    // silently adopting filesystem changes during that lazy first read.
    std::unordered_set<std::string> expected_folders;
    expected_folders.reserve(project_->material_catalog().folders().size());
    for (const auto &folder : project_->material_catalog().folders()) {
      expected_folders.insert(folder.portable_string());
    }
    std::size_t material_count = 0u;
    bool changed = false;
    for (const auto &entry : current.value().entries) {
      if (entry.kind == ProjectAssetKind::Folder) {
        changed |= expected_folders.erase(entry.path.portable_string()) == 0u;
      } else if (entry.kind == ProjectAssetKind::Material) {
        ++material_count;
        const auto *installed = project_->material_catalog().find(entry.asset_id);
        changed |= installed == nullptr || installed->path != entry.path ||
                   installed->content_revision != entry.content_revision;
      }
    }
    changed |= !expected_folders.empty();
    if (release_has_feature(ReleaseFeature::Materials)) {
      changed |= material_count != project_->material_catalog().entries().size();
    }
    if (changed) {
      return ProjectIoResult<ProjectAssetSnapshot>::failure(
          ProjectIoErrorCode::InvalidWorkspaceState,
          "Project assets changed outside the installed catalog. Refresh the "
          "browser and try again.");
    }
  }
  if (current.ok() && expected_revision != 0u &&
      current.value().revision != expected_revision) {
    return ProjectIoResult<ProjectAssetSnapshot>::failure(
        ProjectIoErrorCode::InvalidWorkspaceState,
        "Project assets changed. Refresh the browser and try again.");
  }
  return current;
}

ProjectAssetOperationResult ProjectAssetService::create_folder(
    std::string_view parent, std::string_view name, ProjectId expected_project,
    std::uint64_t expected_revision) {
  const auto before = preflight(expected_project, expected_revision);
  if (!before.ok()) {
    return failure(before.error().message);
  }
  const auto &file_system = project_->workspace()->file_system;
  const auto folder = resolve_project_asset_folder(file_system, parent);
  if (!folder.ok()) {
    return failure(folder.error().message);
  }
  std::set<std::wstring> reserved;
  const auto path = unique_path(file_system, parent, name, {}, reserved);
  if (!path.ok()) {
    return failure(path.error().message);
  }
  const auto absolute = file_system.resolve(path.value());
  if (!absolute.ok()) {
    return failure(absolute.error().message);
  }
  AssetFileTransaction transaction;
  auto discovered = discover_manifest(*project_);
  if (!discovered.ok()) return failure(discovered.error().message);
  auto manifest = discovered.take_value();
  const auto prepared = project_->prepare_asset_manifest(manifest);
  if (!prepared.ok) return failure(prepared.message);
  const auto journal = transaction.begin(*project_->workspace(), manifest);
  if (!journal.ok()) return failure(journal.error().message);
  const auto created = transaction.create_directory(absolute.value());
  if (!created.ok()) {
    return failure(created.error().message);
  }
  auto catalog = ProjectMaterialCatalog::scan(file_system, nullptr, &manifest);
  if (!catalog.ok()) {
    return failure(catalog.error().message + transaction.rollback());
  }
  const auto valid = validate_material_catalog(catalog.value());
  if (!valid.ok()) {
    return failure(valid.error().message + transaction.rollback());
  }
  const auto committed_intent = transaction.prepare_commit();
  if (!committed_intent.ok()) {
    return failure(committed_intent.error().message + transaction.rollback());
  }
  const auto published = project_->install_asset_manifest(std::move(manifest));
  if (!published.ok) {
    return failure(published.message + transaction.rollback());
  }
  publish_organized_catalog(*project_, catalog.take_value());
  auto warnings = transaction.commit();
  retain_committed_changes(before.value(), {}, std::array{path.value()});
  return {.ok = true, .changed = true, .message = "Folder created.",
          .paths = {path.value()}, .warnings = std::move(warnings)};
}

ProjectAssetOperationResult ProjectAssetService::create_material(
    std::string_view parent, std::string_view name, ProjectId expected_project,
    std::uint64_t expected_revision) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }
  const auto before = preflight(expected_project, expected_revision);
  if (!before.ok()) {
    return failure(before.error().message);
  }
  const auto &file_system = project_->workspace()->file_system;
  const auto folder = resolve_project_asset_folder(file_system, parent);
  if (!folder.ok()) {
    return failure(folder.error().message);
  }
  std::set<std::wstring> reserved;
  const auto path = unique_path(file_system, parent, name, ".qmat", reserved);
  if (!path.ok()) {
    return failure(path.error().message);
  }
  const auto absolute = file_system.resolve_for_write(path.value());
  if (!absolute.ok()) {
    return failure(absolute.error().message);
  }
  ProjectMaterialDocument document{.id = ProjectAssetId::generate(),
                                    .name = utf8(path.value().native_path().stem())};
  const auto encoded = QmatCodec::encode(document);
  if (!encoded.ok()) return failure(encoded.error().message);
  auto discovered = discover_manifest(*project_);
  if (!discovered.ok()) return failure(discovered.error().message);
  auto manifest = discovered.take_value();
  const auto prepared = project_->prepare_asset_manifest(manifest);
  if (!prepared.ok) return failure(prepared.message);
  AssetFileTransaction transaction;
  const auto journal = transaction.begin(*project_->workspace(), manifest);
  if (!journal.ok()) return failure(journal.error().message);
  const auto created = transaction.create_bytes(absolute.value(),
      std::as_bytes(std::span(encoded.value().data(), encoded.value().size())));
  if (!created.ok()) {
    return failure(created.error().message);
  }
  auto catalog = ProjectMaterialCatalog::scan(file_system);
  if (!catalog.ok()) {
    return failure(catalog.error().message + transaction.rollback());
  }
  const auto valid = validate_material_catalog(catalog.value());
  if (!valid.ok()) {
    return failure(valid.error().message + transaction.rollback());
  }
  const auto committed_intent = transaction.prepare_commit();
  if (!committed_intent.ok()) {
    return failure(committed_intent.error().message + transaction.rollback());
  }
  const auto published = project_->install_asset_manifest(std::move(manifest));
  if (!published.ok) {
    return failure(published.message + transaction.rollback());
  }
  publish_organized_catalog(*project_, catalog.take_value());
  auto warnings = transaction.commit();
  retain_committed_changes(before.value(), {}, std::array{path.value()});
  return {.ok = true, .changed = true, .message = "Material created.",
          .paths = {path.value()}, .warnings = std::move(warnings)};
}

ProjectAssetOperationResult ProjectAssetService::create_scene(
    std::string_view parent, std::string_view name, ProjectId expected_project,
    std::uint64_t expected_revision) {
  const auto before = preflight(expected_project, expected_revision);
  if (!before.ok()) {
    return failure(before.error().message);
  }
  const auto &file_system = project_->workspace()->file_system;
  const auto folder = resolve_project_asset_folder(file_system, parent);
  if (!folder.ok()) {
    return failure(folder.error().message);
  }
  std::set<std::wstring> reserved;
  const auto path = unique_path(file_system, parent, name, ".qdr", reserved);
  if (!path.ok()) {
    return failure(path.error().message);
  }
  const auto created = project_->create_scene_asset(path.value());
  if (!created.ok()) {
    return failure(created.error().message);
  }
  retain_committed_changes(before.value(), {}, std::array{path.value()});
  return {.ok = true, .changed = true, .message = "Scene created.",
          .paths = {path.value()}};
}

ProjectAssetOperationResult ProjectAssetService::import_textures(
    std::span<const std::filesystem::path> sources, std::string_view destination,
    ProjectId expected_project, std::uint64_t expected_revision) {
  if (!release_has_feature(ReleaseFeature::Materials)) {
    return failure(release_feature_unavailable_message());
  }
  const auto before = preflight(expected_project, expected_revision);
  if (!before.ok()) {
    return failure(before.error().message);
  }
  if (sources.empty()) {
    return failure("Select at least one texture to import.");
  }
  const auto &file_system = project_->workspace()->file_system;
  const auto folder = resolve_project_asset_folder(file_system, destination);
  if (!folder.ok()) {
    return failure(folder.error().message);
  }
  struct Import final {
    ProjectImportedImage image;
    ProjectRelativePath path;
  };
  std::vector<Import> imports;
  std::set<std::wstring> reserved;
  auto discovered = discover_manifest(*project_);
  if (!discovered.ok()) return failure(discovered.error().message);
  auto manifest = discovered.take_value();
  for (const auto &source : sources) {
    auto decoded = ProjectImageDecoder::read_import(source);
    if (!decoded.ok()) {
      return failure(decoded.error().message);
    }
    const auto path = unique_path(file_system, destination,
                                  decoded.value().source_stem,
                                  decoded.value().source_extension, reserved);
    if (!path.ok()) {
      return failure(path.error().message);
    }
    const auto registered = manifest.register_texture(
        {.id = ProjectAssetId::generate(), .path = path.value()});
    if (!registered.ok()) {
      return failure(registered.error().message);
    }
    imports.push_back({decoded.take_value(), path.value()});
  }
  AssetFileTransaction transaction;
  const auto prepared = project_->prepare_asset_manifest(manifest);
  if (!prepared.ok) return failure(prepared.message);
  const auto journal = transaction.begin(*project_->workspace(), manifest);
  if (!journal.ok()) return failure(journal.error().message);
  ProjectAssetOperationResult result{.ok = true, .changed = true,
                                      .message = "Textures imported."};
  for (const auto &entry : imports) {
    const auto absolute = file_system.resolve_for_write(entry.path);
    if (!absolute.ok()) {
      return failure(absolute.error().message + transaction.rollback());
    }
    const auto created = transaction.create_bytes(absolute.value(), entry.image.encoded_bytes);
    if (!created.ok()) {
      return failure(created.error().message + transaction.rollback());
    }
    result.paths.push_back(entry.path);
  }
  const auto committed_intent = transaction.prepare_commit();
  if (!committed_intent.ok()) {
    return failure(committed_intent.error().message + transaction.rollback());
  }
  const auto published = project_->install_asset_manifest(std::move(manifest));
  if (!published.ok) {
    return failure(published.message + transaction.rollback());
  }
  result.warnings = transaction.commit();
  retain_committed_changes(before.value(), {}, result.paths);
  return result;
}

ProjectAssetOperationResult ProjectAssetService::rename(
    const ProjectRelativePath &path, std::string_view name,
    ProjectId expected_project, std::uint64_t expected_revision) {
  return organize(std::span(&path, 1u), {}, false, true, name,
                   expected_project, expected_revision);
}

ProjectAssetOperationResult ProjectAssetService::move(
    std::span<const ProjectRelativePath> paths, std::string_view destination,
    ProjectId expected_project, std::uint64_t expected_revision) {
  return organize(paths, destination, false, false, std::nullopt,
                   expected_project, expected_revision);
}

ProjectAssetOperationResult ProjectAssetService::copy(
    std::span<const ProjectRelativePath> paths, std::string_view destination,
    ProjectId expected_project, std::uint64_t expected_revision) {
  return organize(paths, destination, true, false, std::nullopt,
                   expected_project, expected_revision);
}

ProjectAssetOperationResult ProjectAssetService::duplicate(
    std::span<const ProjectRelativePath> paths, ProjectId expected_project,
    std::uint64_t expected_revision) {
  return organize(paths, {}, true, true, std::nullopt,
                   expected_project, expected_revision);
}

ProjectAssetOperationResult ProjectAssetService::organize(
    std::span<const ProjectRelativePath> paths, std::string_view destination,
    bool clone, bool keep_parent, std::optional<std::string_view> name,
    const ProjectId &expected_project, std::uint64_t expected_revision) {
  const auto before = preflight(expected_project, expected_revision);
  if (!before.ok()) {
    return failure(before.error().message);
  }
  if (paths.empty()) {
    return failure("Select at least one project asset.");
  }
  const auto &file_system = project_->workspace()->file_system;
  if (!keep_parent) {
    const auto folder = resolve_project_asset_folder(file_system, destination);
    if (!folder.ok()) {
      return failure(folder.error().message);
    }
  }
  std::vector<const ProjectAssetEntry *> roots;
  for (const auto &path : paths) {
    const auto *entry = find_entry(before.value(), path);
    if (entry == nullptr) {
      return failure("A selected asset is no longer available in this project.");
    }
    if (!clone && entry->kind == ProjectAssetKind::Scene && !entry->scene_id.valid()) {
      return failure("This external scene copy shares another scene's identity. "
                     "Open it once before moving or renaming it.");
    }
    if (!clone && (entry->asset_id.str() == kDefaultProjectMaterialId ||
        entry->asset_id.str() == kDefaultProjectAlbedoTextureId)) {
      return failure("Built-in default assets cannot be organized.");
    }
    if (entry->kind == ProjectAssetKind::Folder) {
      const auto safe = validate_folder_contents(file_system, before.value(), path);
      if (!safe.ok()) {
        return failure(safe.error().message);
      }
      if (!keep_parent && within(destination, path.portable_string())) {
        return failure("A folder cannot be placed inside itself.");
      }
    }
    if (std::ranges::none_of(roots, [entry](const auto *other) {
          return other->path == entry->path;
        })) {
      roots.push_back(entry);
    }
  }
  // A selected ancestor already owns its descendants in the same batch.
  const auto all_roots = roots;
  std::erase_if(roots, [&all_roots](const auto *entry) {
    return std::ranges::any_of(all_roots, [entry](const auto *other) {
      return entry != other && other->kind == ProjectAssetKind::Folder &&
             within(entry->path.portable_string(), other->path.portable_string());
    });
  });
  struct RootMove final {
    const ProjectAssetEntry *source = nullptr;
    ProjectRelativePath destination;
  };
  struct Pending final {
    const ProjectAssetEntry *source = nullptr;
    ProjectRelativePath destination;
    ProjectAssetId asset_id;
    SceneId scene_id;
  };
  std::vector<RootMove> root_moves;
  std::vector<Pending> pending;
  std::set<std::wstring> reserved;
  for (const auto *root : roots) {
    const std::string folder = keep_parent ? parent_of(root->path)
                                           : std::string(destination);
    const std::string stem = name.has_value() ? std::string(*name)
        : utf8(root->kind == ProjectAssetKind::Folder
                   ? root->path.native_path().filename()
                   : root->path.native_path().stem());
    const std::string extension = root->kind == ProjectAssetKind::Folder
                                      ? std::string{}
                                      : utf8(root->path.native_path().extension());
    const auto requested = child_path(folder, stem + extension);
    if (!requested.ok()) {
      return failure(requested.error().message);
    }
    if (!clone && requested.value() == root->path) {
      continue;
    }
    const bool case_only = !clone &&
                          requested.value().portable_case_key() ==
                              root->path.portable_case_key();
    auto target = case_only ? requested
                           : unique_path(file_system, folder, stem, extension,
                                          reserved, clone);
    if (clone && root->kind == ProjectAssetKind::Material) {
      // Keep the established material duplicate naming across both facades.
      for (std::size_t ordinal = 1u; ordinal != 0u; ++ordinal) {
        const std::string copy_name = root->name + " Copy" +
            (ordinal == 1u ? std::string{} : " " + std::to_string(ordinal));
        const QString folded = QString::fromUtf8(copy_name).toCaseFolded();
        if (std::ranges::any_of(before.value().entries, [&](const auto &other) {
              return other.kind == ProjectAssetKind::Material &&
                     parent_of(other.path) == folder &&
                     QString::fromUtf8(other.name).toCaseFolded() == folded;
            })) {
          continue;
        }
        target = unique_path(file_system, folder, copy_name, extension, reserved, false);
        if (target.ok() ||
            (target.error().code != ProjectIoErrorCode::DuplicateAsset &&
             target.error().code != ProjectIoErrorCode::PathCaseCollision)) {
          break;
        }
      }
    }
    if (!target.ok()) {
      return failure(target.error().message);
    }
    root_moves.push_back({root, target.value()});
    for (const auto &entry : before.value().entries) {
      if (entry.path != root->path &&
          (root->kind != ProjectAssetKind::Folder ||
           !within(entry.path.portable_string(), root->path.portable_string()))) {
        continue;
      }
      if (!clone && (entry.asset_id.str() == kDefaultProjectMaterialId ||
          entry.asset_id.str() == kDefaultProjectAlbedoTextureId)) {
        return failure("This folder contains built-in default assets and cannot "
                       "be organized.");
      }
      if (!clone && entry.kind == ProjectAssetKind::Scene && !entry.scene_id.valid()) {
        return failure("This folder contains an external scene copy with a shared identity. "
                       "Open that scene before organizing its folder.");
      }
      if ((entry.kind == ProjectAssetKind::Material ||
           entry.kind == ProjectAssetKind::Texture) &&
          !release_has_feature(ReleaseFeature::Materials)) {
        return failure(release_feature_unavailable_message());
      }
      const auto validated = unaliased(file_system, entry.path,
                                        entry.kind == ProjectAssetKind::Folder);
      if (!validated.ok()) {
        return failure(validated.error().message);
      }
      const auto target_path = ProjectRelativePath::parse_portable(
          target.value().portable_string() +
          entry.path.portable_string().substr(root->path.portable_string().size()));
      if (!target_path.ok()) {
        return failure(target_path.error().message);
      }
      pending.push_back({
          .source = &entry,
          .destination = target_path.value(),
          .asset_id = clone && entry.kind != ProjectAssetKind::Folder &&
                                  entry.kind != ProjectAssetKind::Scene
                          ? ProjectAssetId::generate()
                          : entry.asset_id,
          .scene_id = clone && entry.kind == ProjectAssetKind::Scene
                          ? SceneId::generate()
                          : entry.scene_id,
      });
    }
  }
  if (pending.empty()) {
    return {.ok = true};
  }
  std::vector<std::pair<ProjectAssetId, ProjectAssetId>> material_remaps;
  std::unordered_map<ProjectAssetId, const Pending *, ProjectAssetIdHash> texture_remaps;
  for (const auto &entry : pending) {
    if (entry.source->kind == ProjectAssetKind::Material && clone) {
      material_remaps.emplace_back(entry.source->asset_id, entry.asset_id);
    }
    if (entry.source->kind == ProjectAssetKind::Texture && entry.source->asset_id.valid()) {
      texture_remaps.emplace(entry.source->asset_id, &entry);
      if (const auto *texture = project_->manifest()->find_texture(entry.source->asset_id)) {
        for (const auto &alias : texture->aliases) {
          texture_remaps.emplace(alias, &entry);
        }
      }
    }
  }
  const bool scene_files_only = std::ranges::all_of(pending, [](const auto &entry) {
    return entry.source->kind == ProjectAssetKind::Scene;
  });
  const bool files_only = std::ranges::none_of(pending, [](const auto &entry) {
    return entry.source->kind == ProjectAssetKind::Folder;
  });
  const bool needs_texture_discovery = std::ranges::any_of(before.value().entries, [](const auto &entry) {
    return entry.kind == ProjectAssetKind::Texture && !entry.asset_id.valid();
  });
  auto discovered = files_only && !needs_texture_discovery
      ? ProjectIoResult<ProjectManifest>::success(*project_->manifest())
      : discover_manifest(*project_);
  if (!discovered.ok()) return failure(discovered.error().message);
  auto manifest = discovered.take_value();
  for (const auto &entry : pending) {
    if (entry.source->kind == ProjectAssetKind::Scene && !clone &&
        manifest.find_scene(entry.scene_id) != nullptr) {
      const auto removed = manifest.unregister_scene(entry.scene_id);
      if (!removed.ok()) {
        return failure(removed.error().message);
      }
    }
  }
  for (auto &entry : pending) {
    if (entry.source->kind == ProjectAssetKind::Scene) {
      const auto registered = manifest.register_scene(
          {.id = entry.scene_id, .path = entry.destination});
      if (!registered.ok()) {
        return failure(registered.error().message);
      }
    } else if (entry.source->kind == ProjectAssetKind::Texture) {
      if (!entry.asset_id.valid()) {
        const auto *discovered_texture = manifest.find_texture(entry.source->path);
        entry.asset_id = discovered_texture == nullptr ? ProjectAssetId::generate()
                                                       : discovered_texture->id;
      }
      const auto changed =
          clone || manifest.find_texture(entry.asset_id) == nullptr
              ? manifest.register_texture({.id = entry.asset_id,
                                            .path = entry.destination})
              : manifest.relocate_texture(entry.asset_id, entry.destination);
      if (!changed.ok()) {
        return failure(changed.error().message);
      }
    }
  }
  const auto prepared = project_->prepare_asset_manifest(manifest);
  if (!prepared.ok) {
    return failure(prepared.message);
  }
  if (files_only && !needs_texture_discovery) {
    // The first full preflight seals the browser intent. Preparing a pure
    // file relocation only changes in-memory catalog metadata; recheck the
    // selected sources here, not every unrelated project asset a second time.
    const auto selected = inspect(paths);
    if (!selected.ok()) return failure(selected.error().message);
    for (const auto &entry : pending) {
      const auto *current = find_entry(selected.value(), entry.source->path);
      if (current == nullptr || current->kind != entry.source->kind ||
          current->asset_id != entry.source->asset_id ||
          current->scene_id != entry.source->scene_id ||
          current->content_revision != entry.source->content_revision) {
        return failure("Selected assets changed. Refresh the browser and try again.");
      }
    }
  } else {
    const auto still_current = preflight(before.value().project_id, before.value().revision);
    if (!still_current.ok()) return failure(still_current.error().message);
  }
  AssetFileTransaction transaction;
  const auto journal = transaction.begin(*project_->workspace(), manifest);
  if (!journal.ok()) return failure(journal.error().message);
  ProjectMaterialFileRollback rewritten_material;
  const auto reject = [&](std::string message) {
    const auto aborted = transaction.abort_commit();
    if (!aborted.ok()) {
      rewritten_material.commit();
      transaction.preserve();
      append(message, "Asset commit recovery was preserved: " + aborted.error().message);
      invalidate();
      return failure(std::move(message));
    }
    append(message, rewritten_material.rollback());
    append(message, transaction.rollback());
    invalidate();
    return failure(std::move(message));
  };
  if (!clone) {
    for (const auto &root : root_moves) {
      const auto source = unaliased(file_system, root.source->path,
                                     root.source->kind == ProjectAssetKind::Folder);
      if (!source.ok()) {
        return reject(source.error().message);
      }
      auto source_absolute = source.value();
      if (root.source->path.portable_case_key() == root.destination.portable_case_key()) {
        const auto temporary = ProjectRelativePath::parse_portable(
            (parent_of(root.source->path).empty() ? std::string{}
                                                : parent_of(root.source->path) + "/") +
            ".quader-asset-rename-" + std::string(ProjectAssetId::generate().str()));
        const auto temporary_absolute = file_system.resolve(temporary.value());
        if (!temporary_absolute.ok()) {
          return reject(temporary_absolute.error().message);
        }
        const auto staged = transaction.move(source_absolute, temporary_absolute.value());
        if (!staged.ok()) {
          return reject(staged.error().message);
        }
        source_absolute = temporary_absolute.value();
      }
      const auto target = file_system.resolve(root.destination);
      if (!target.ok()) {
        return reject(target.error().message);
      }
      const auto moved = transaction.move(source_absolute, target.value());
      if (!moved.ok()) {
        return reject(moved.error().message);
      }
    }
    if (name.has_value() && pending.size() == 1u &&
        pending.front().source->kind == ProjectAssetKind::Material) {
      const auto &entry = pending.front();
      const auto read = QmatCodec::read(file_system, entry.destination);
      if (!read.ok()) {
        return reject(read.error().message);
      }
      auto document = read.value().material;
      document.name = std::string(*name);
      const auto absolute = file_system.resolve_existing_file(entry.destination);
      if (!absolute.ok()) {
        return reject(absolute.error().message);
      }
      const auto encoded = QmatCodec::encode(document);
      if (!encoded.ok()) return reject(encoded.error().message);
      const auto recorded = transaction.record_replacement(absolute.value(), encoded.value());
      if (!recorded.ok()) return reject(recorded.error().message);
      const auto rewritten = rewritten_material.replace_qmat(absolute.value(), document);
      if (!rewritten.ok()) {
        return reject(rewritten.error().message);
      }
    }
  } else {
    // Lexicographic source order always places a directory before descendants.
    std::ranges::sort(pending, {}, [](const Pending &entry) {
      return entry.source->path.portable_string();
    });
    // Sorting invalidates the pointer index built above; rebuild its targets.
    texture_remaps.clear();
    for (const auto &entry : pending) {
      if (entry.source->kind != ProjectAssetKind::Texture || !entry.source->asset_id.valid()) {
        continue;
      }
      texture_remaps.emplace(entry.source->asset_id, &entry);
      if (const auto *texture = project_->manifest()->find_texture(entry.source->asset_id)) {
        for (const auto &alias : texture->aliases) {
          texture_remaps.emplace(alias, &entry);
        }
      }
    }
    for (const auto &entry : pending) {
      const auto absolute = file_system.resolve(entry.destination);
      if (!absolute.ok()) {
        return reject(absolute.error().message);
      }
      ProjectIoResult<void> created = ProjectIoResult<void>::success();
      if (entry.source->kind == ProjectAssetKind::Folder) {
        created = transaction.create_directory(absolute.value());
      } else if (entry.source->kind == ProjectAssetKind::Scene) {
        created = transaction.create(absolute.value(), [&](const auto &staging) {
          return project_->stage_scene_asset_copy(entry.source->path, staging,
                                                   entry.scene_id, material_remaps);
        });
      } else if (entry.source->kind == ProjectAssetKind::Material) {
        auto read = QmatCodec::read(file_system, entry.source->path);
        if (!read.ok()) {
          return reject(read.error().message);
        }
        auto document = read.value().material;
        document.id = entry.asset_id;
        document.name = utf8(entry.destination.native_path().stem());
        if (auto *standard = document.standard_material()) {
          for (auto *slot : {&standard->base_color_texture, &standard->roughness_texture,
                             &standard->metallic_texture, &standard->normal_texture}) {
            if (!slot->has_value()) {
              continue;
            }
            const auto remap = texture_remaps.find(slot->value().id);
            if (remap != texture_remaps.end()) {
              slot->value().id = remap->second->asset_id;
              slot->value().project_relative_path = remap->second->destination.portable_string();
            }
          }
        }
        const auto encoded = QmatCodec::encode(document);
        if (!encoded.ok()) {
          return reject(encoded.error().message);
        }
        created = transaction.create_bytes(
            absolute.value(), std::as_bytes(std::span(encoded.value().data(), encoded.value().size())));
      } else if (entry.source->kind == ProjectAssetKind::Texture) {
        const auto source = unaliased(file_system, entry.source->path, false);
        if (!source.ok()) {
          return reject(source.error().message);
        }
        const auto decoded = ProjectImageDecoder::read_import(source.value());
        if (!decoded.ok()) {
          return reject(decoded.error().message);
        }
        created = transaction.create_bytes(absolute.value(), decoded.value().encoded_bytes);
      }
      if (!created.ok()) {
        return reject(created.error().message);
      }
    }
  }
  auto catalog = scene_files_only
      ? ProjectIoResult<ProjectMaterialCatalog>::success(project_->material_catalog())
      : ProjectMaterialCatalog::scan(file_system, nullptr, &manifest);
  if (!catalog.ok()) {
    return reject(catalog.error().message);
  }
  const auto valid = validate_material_catalog(catalog.value());
  if (!valid.ok()) {
    return reject(valid.error().message);
  }
  const auto committed_intent = transaction.prepare_commit();
  if (!committed_intent.ok()) return reject(committed_intent.error().message);
  const auto published = project_->install_asset_manifest(std::move(manifest));
  if (!published.ok) {
    return reject(published.message);
  }
  publish_organized_catalog(*project_, catalog.take_value());
  rewritten_material.commit();
  ProjectAssetOperationResult result{
      .ok = true, .changed = true,
      .message = clone ? "Assets copied." : "Assets organized.",
      .warnings = transaction.commit(),
  };
  for (const auto &entry : pending) {
    if (entry.source->kind == ProjectAssetKind::Scene) {
      const auto notified = project_->publish_scene_asset(entry.destination);
      const auto &warning = notified.ok() ? notified.warning() : notified.error().message;
      if (!warning.empty()) result.warnings.push_back(warning);
      continue;
    }
    if (entry.source->kind != ProjectAssetKind::Material) {
      continue;
    }
    const auto *material = project_->material_catalog().find(entry.asset_id);
    if (material == nullptr) {
      continue;
    }
    if (!clone) {
      if (name.has_value() && pending.size() == 1u) {
        project_->standard_material_editor().retarget_saved_material(
            entry.asset_id, entry.destination, material->material.name,
            material->content_revision);
      } else {
        project_->standard_material_editor().retarget_saved_path(
            entry.asset_id, entry.destination);
      }
    }
    const auto notified = ProjectBridgeNotifications::publish_material(
        file_system, project_->manifest()->id(), *material);
    const auto &warning = notified.ok() ? notified.warning() : notified.error().message;
    if (!warning.empty()) {
      result.warnings.push_back(warning);
    }
  }
  std::vector<ProjectRelativePath> relocated;
  for (const auto &entry : root_moves) {
    result.paths.push_back(entry.destination);
    if (!clone) relocated.push_back(entry.source->path);
  }
  retain_committed_changes(before.value(), relocated, result.paths);
  return result;
}

} // namespace quader
