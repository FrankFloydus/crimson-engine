////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_material_persistence.hpp"

#include "io/project/project_material_catalog.hpp"
#include "io/project/qmat_codec.hpp"

#include <QString>

#include <algorithm>
#include <fstream>
#include <system_error>
#include <utility>

namespace quader {
namespace {

constexpr std::size_t kMaximumUniqueDestinationAttempts = 32u;
constexpr qsizetype kMaximumAssetFilenameCharacters = 255;
constexpr std::size_t kMaximumMaterialNameBytes = 256u;

void chop_character(QString &text) {
  const qsizetype last = text.size() - 1;
  const bool surrogate_pair =
      last > 0 && text.at(last).isLowSurrogate() &&
      text.at(last - 1).isHighSurrogate();
  text.chop(surrogate_pair ? 2 : 1);
}

QString safe_filename_part(std::string_view source) {
  QString result = QString::fromUtf8(source.data(),
                                    static_cast<qsizetype>(source.size()));
  for (QChar &character : result) {
    if (character.unicode() < 32u ||
        QStringView(u"<>:\"/\\|?*").contains(character)) {
      character = QChar('_');
    }
  }
  return result;
}

std::string readable_filename(std::string_view source_stem,
                               std::string_view fallback,
                               std::string_view source_extension) {
  QString stem = safe_filename_part(source_stem);
  QString extension = safe_filename_part(source_extension);
  if (!extension.isEmpty() && extension.front() != QChar('.')) {
    extension.prepend(QChar('.'));
  }
  if (stem.isEmpty()) {
    stem = QString::fromUtf8(fallback.data(),
                            static_cast<qsizetype>(fallback.size()));
  }
  const QString device = stem.section(QChar('.'), 0, 0).trimmed().toUpper();
  const bool numbered_device =
      device.size() == 4 &&
      (device.startsWith(QStringLiteral("COM")) ||
       device.startsWith(QStringLiteral("LPT"))) &&
      (QStringView(u"123456789¹²³").contains(device.back()));
  if (device == QStringLiteral("CON") || device == QStringLiteral("PRN") ||
      device == QStringLiteral("AUX") || device == QStringLiteral("NUL") ||
      device == QStringLiteral("CLOCK$") || numbered_device) {
    stem.prepend(QChar('_'));
  }
  while (extension.size() >= kMaximumAssetFilenameCharacters) {
    chop_character(extension);
  }
  while (!stem.isEmpty() &&
         stem.size() + extension.size() > kMaximumAssetFilenameCharacters) {
    chop_character(stem);
  }
  QString filename = stem + extension;
  while (!filename.isEmpty() &&
         (filename.back() == QChar('.') || filename.back() == QChar(' '))) {
    filename.chop(1);
  }
  return filename.isEmpty() ? std::string(fallback)
                            : filename.toUtf8().toStdString();
}

ProjectIoResult<ProjectMaterialAssetDestination> make_unique_destination(
    const ProjectFileSystem &file_system, std::string_view directory,
    std::string_view source_stem, std::string_view fallback_stem,
    std::string_view extension) {
  const std::string filename =
      readable_filename(source_stem, fallback_stem, extension);

  // Check the owning directory separately so a casing error in Materials or
  // Textures cannot be mistaken for a collision in the desired asset leaf.
  const auto parent = ProjectRelativePath::parse_portable(directory);
  if (!parent.ok()) {
    return ProjectIoResult<ProjectMaterialAssetDestination>::failure(
        parent.error().code, parent.error().message, parent.error().path);
  }
  const auto resolved_parent = file_system.resolve(parent.value());
  if (!resolved_parent.ok()) {
    return ProjectIoResult<ProjectMaterialAssetDestination>::failure(
        resolved_parent.error().code, resolved_parent.error().message,
        resolved_parent.error().path);
  }

  for (std::size_t attempt = 0u;
       attempt < kMaximumUniqueDestinationAttempts; ++attempt) {
    const ProjectAssetId id = ProjectAssetId::generate();
    const std::string portable_path =
        std::string(directory) + "/" +
        (attempt == 0u ? std::string{} : std::string(id.str()) + "/") +
        filename;
    ProjectIoResult<ProjectRelativePath> relative =
        ProjectRelativePath::parse_portable(portable_path);
    if (!relative.ok()) {
      return ProjectIoResult<ProjectMaterialAssetDestination>::failure(
          relative.error().code, relative.error().message,
          relative.error().path);
    }
    ProjectIoResult<std::filesystem::path> absolute =
        file_system.resolve(relative.value());
    if (!absolute.ok()) {
      if (absolute.error().code == ProjectIoErrorCode::PathCaseCollision) {
        continue;
      }
      return ProjectIoResult<ProjectMaterialAssetDestination>::failure(
          absolute.error().code, absolute.error().message,
          absolute.error().path);
    }
    std::error_code error;
    const bool exists = std::filesystem::exists(absolute.value(), error);
    if (error) {
      return ProjectIoResult<ProjectMaterialAssetDestination>::failure(
          ProjectIoErrorCode::IoFailure,
          "Could not inspect project asset destination: " + error.message(),
          absolute.value());
    }
    if (!exists) {
      return ProjectIoResult<ProjectMaterialAssetDestination>::success({
          .id = id,
          .path = relative.take_value(),
      });
    }
  }
  return ProjectIoResult<ProjectMaterialAssetDestination>::failure(
      ProjectIoErrorCode::PathCaseCollision,
      "Could not allocate a collision-free project asset destination.");
}

void append_rollback_error(std::string &message,
                           const std::filesystem::path &path,
                           std::string_view detail) {
  if (!message.empty()) {
    message += "; ";
  }
  message += "Could not roll back '" + path.string() + "': " +
             std::string(detail);
}

} // namespace

ProjectIoResult<ProjectMaterialAssetDestination>
make_unique_material_destination(const ProjectFileSystem &file_system,
                                 std::string_view source_stem) {
  return make_unique_destination(file_system, "Materials", source_stem,
                                 "material", ".qmat");
}

ProjectIoResult<ProjectMaterialAssetDestination>
make_unique_texture_destination(const ProjectFileSystem &file_system,
                                std::string_view source_stem,
                                std::string_view extension) {
  return make_unique_destination(file_system, "Textures", source_stem,
                                 "texture", extension);
}

ProjectIoResult<std::string>
default_project_material_name(const ProjectMaterialCatalog &catalog,
                              std::string_view albedo_stem) {
  QString stem = QString::fromUtf8(albedo_stem.data(),
                                  static_cast<qsizetype>(albedo_stem.size()));
  if (!stem.trimmed().isEmpty()) {
    while (static_cast<std::size_t>(stem.toUtf8().size()) >
           kMaximumMaterialNameBytes) {
      chop_character(stem);
    }
    if (!stem.trimmed().isEmpty()) {
      return ProjectIoResult<std::string>::success(
          stem.toUtf8().toStdString());
    }
  }

  constexpr std::string_view kPrefix = "Material_";
  std::string largest = "0";
  for (const ProjectMaterialCatalogEntry &entry : catalog.entries()) {
    const std::string_view name = entry.material.name;
    if (!name.starts_with(kPrefix)) {
      continue;
    }
    std::string_view digits = name.substr(kPrefix.size());
    if (digits.empty() || !std::ranges::all_of(digits, [](char character) {
          return character >= '0' && character <= '9';
        })) {
      continue;
    }
    const std::size_t first = digits.find_first_not_of('0');
    digits = first == std::string_view::npos ? std::string_view("0")
                                            : digits.substr(first);
    if (digits.size() > largest.size() ||
        (digits.size() == largest.size() && digits > largest)) {
      largest = digits;
    }
  }
  bool carry = true;
  for (std::size_t index = largest.size(); index != 0u && carry; --index) {
    char &digit = largest[index - 1u];
    carry = digit == '9';
    digit = carry ? '0' : static_cast<char>(digit + 1);
  }
  if (carry) {
    largest.insert(largest.begin(), '1');
  }
  if (kPrefix.size() + largest.size() > kMaximumMaterialNameBytes) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::InvalidArgument,
        "The next Material_<number> name exceeds the material name limit.");
  }
  return ProjectIoResult<std::string>::success(std::string(kPrefix) + largest);
}

ProjectIoResult<std::string>
read_project_material_file_bytes(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input.is_open()) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::IoFailure, "Could not back up material file.",
        path);
  }
  std::string bytes{std::istreambuf_iterator<char>(input),
                    std::istreambuf_iterator<char>()};
  if (input.bad()) {
    return ProjectIoResult<std::string>::failure(
        ProjectIoErrorCode::IoFailure, "Could not read material file.", path);
  }
  return ProjectIoResult<std::string>::success(std::move(bytes));
}

ProjectMaterialFileRollback::~ProjectMaterialFileRollback() {
  try {
    (void)rollback();
  } catch (...) {
    // File transactions retain their existing recovery behavior if directory
    // cleanup or its diagnostic allocation fails during stack unwinding.
  }
}

ProjectIoResult<std::filesystem::path>
ProjectMaterialFileRollback::prepare_destination(
    const ProjectFileSystem &file_system, const ProjectRelativePath &path) {
  auto resolved = file_system.resolve(path);
  if (!resolved.ok()) {
    return resolved;
  }
  const std::filesystem::path parent = resolved.value().parent_path();
  std::error_code error;
  const bool exists = std::filesystem::exists(parent, error);
  if (!error && !exists) {
    const bool created = std::filesystem::create_directory(parent, error);
    if (created) {
      entries_.push_back({.directory = parent});
    }
  }
  if (error) {
    return ProjectIoResult<std::filesystem::path>::failure(
        ProjectIoErrorCode::IoFailure,
        "Could not prepare project asset directory: " + error.message(),
        parent);
  }
  return file_system.resolve_for_write(path);
}

ProjectIoResult<void>
ProjectMaterialFileRollback::create_bytes(
    const std::filesystem::path &path, std::span<const std::byte> bytes) {
  const std::string_view view(reinterpret_cast<const char *>(bytes.data()),
                              bytes.size());
  return publish(path, view, {},
                 quader_io::QdrAtomicWriteDisposition::CreateNew);
}

ProjectIoResult<void> ProjectMaterialFileRollback::create_qmat(
    const std::filesystem::path &path,
    const ProjectMaterialDocument &document) {
  ProjectIoResult<std::string> encoded = QmatCodec::encode(document);
  if (!encoded.ok()) {
    return ProjectIoResult<void>::failure(
        encoded.error().code, encoded.error().message, path);
  }
  return publish(
      path, encoded.value(),
      [&document](const std::filesystem::path &temporary_path) {
        ProjectIoResult<std::string> temporary =
            read_project_material_file_bytes(temporary_path);
        if (!temporary.ok()) {
          return quader_io::QdrAtomicWriteResult{
              false, temporary.error().message};
        }
        ProjectIoResult<ProjectMaterialDocument> decoded =
            QmatCodec::decode(temporary.value());
        const bool matches = decoded.ok() && decoded.value() == document;
        return quader_io::QdrAtomicWriteResult{
            matches,
            matches ? std::string{}
                    : decoded.ok()
                          ? "Temporary QMAT content changed during write."
                          : decoded.error().message};
      },
      quader_io::QdrAtomicWriteDisposition::CreateNew);
}

ProjectIoResult<void> ProjectMaterialFileRollback::replace_qmat(
    const std::filesystem::path &path,
    const ProjectMaterialDocument &document) {
  ProjectIoResult<std::string> encoded = QmatCodec::encode(document);
  if (!encoded.ok()) {
    return ProjectIoResult<void>::failure(
        encoded.error().code, encoded.error().message, path);
  }
  return publish(
      path, encoded.value(),
      [&document](const std::filesystem::path &temporary_path) {
        ProjectIoResult<std::string> temporary =
            read_project_material_file_bytes(temporary_path);
        if (!temporary.ok()) {
          return quader_io::QdrAtomicWriteResult{
              false, temporary.error().message};
        }
        ProjectIoResult<ProjectMaterialDocument> decoded =
            QmatCodec::decode(temporary.value());
        const bool matches = decoded.ok() && decoded.value() == document;
        return quader_io::QdrAtomicWriteResult{
            matches,
            matches ? std::string{}
                    : decoded.ok()
                          ? "Temporary QMAT content changed during write."
                          : decoded.error().message};
      },
      quader_io::QdrAtomicWriteDisposition::ReplaceExisting);
}

ProjectIoResult<void> ProjectMaterialFileRollback::publish(
    const std::filesystem::path &path, std::string_view bytes,
    quader_io::QdrAtomicWriteValidator validator,
    quader_io::QdrAtomicWriteDisposition disposition) {
  auto transaction =
      std::make_unique<quader_io::QdrAtomicFileTransaction>();
  quader_io::QdrAtomicWriteResult prepared =
      transaction->prepare(path, disposition);
  if (!prepared.ok) {
    return ProjectIoResult<void>::failure(
        ProjectIoErrorCode::IoFailure, prepared.message, path);
  }

  const auto fail_with_rollback =
      [&path, &transaction](quader_io::QdrAtomicWriteResult failure) {
        const quader_io::QdrAtomicWriteResult rolled_back =
            transaction->rollback();
        if (!rolled_back.ok) {
          failure.message += failure.message.empty() ? "" : "; ";
          failure.message += "Material file transaction rollback failed: " +
                             rolled_back.message;
        }
        return ProjectIoResult<void>::failure(
            ProjectIoErrorCode::IoFailure, std::move(failure.message), path);
      };

  quader_io::QdrAtomicWriteResult written =
      quader_io::write_qdr_text_atomic(transaction->staging_path(), bytes,
                                       std::move(validator));
  if (!written.ok) {
    return fail_with_rollback(std::move(written));
  }
  quader_io::QdrAtomicWriteResult published = transaction->publish();
  if (!published.ok) {
    return fail_with_rollback(std::move(published));
  }
  entries_.push_back({.file = std::move(transaction)});
  return ProjectIoResult<void>::success();
}

std::string ProjectMaterialFileRollback::rollback_to(
    Checkpoint checkpoint) {
  std::string message;
  if (checkpoint > entries_.size()) {
    checkpoint = entries_.size();
  }
  for (std::size_t index = entries_.size(); index > checkpoint; --index) {
    Publication &entry = entries_[index - 1u];
    bool restored = false;
    if (entry.file) {
      const auto rolled_back = entry.file->rollback();
      restored = rolled_back.ok;
      if (!restored) {
        append_rollback_error(message, entry.file->target_path(),
                              rolled_back.message);
      }
    } else {
      std::error_code error;
      (void)std::filesystem::remove(entry.directory, error);
      restored = !error;
      if (error) {
        append_rollback_error(message, entry.directory, error.message());
      }
    }
    if (restored) {
      entries_.erase(entries_.begin() +
                     static_cast<std::ptrdiff_t>(index - 1u));
    }
  }
  return message;
}

void ProjectMaterialFileRollback::commit() noexcept {
  for (const Publication &entry : entries_) {
    const auto &transaction = entry.file;
    if (!transaction) {
      continue;
    }
    try {
      const quader_io::QdrAtomicWriteResult committed = transaction->commit();
      if (!committed.ok && transaction->published()) {
        (void)transaction->preserve_published_for_recovery();
      }
    } catch (...) {
      if (transaction->published()) {
        (void)transaction->preserve_published_for_recovery();
      }
    }
  }
  entries_.clear();
}

} // namespace quader
