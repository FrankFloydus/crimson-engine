////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_material_persistence.hpp"

#include "core/qstr.hpp"
#include "io/project/qmat_codec.hpp"

#include <fstream>
#include <system_error>
#include <utility>

namespace quader {
namespace {

constexpr std::size_t kMaximumUniqueDestinationAttempts = 32u;
constexpr std::size_t kMaximumGeneratedAssetStemBytes = 200u;

std::string sanitized_stem(std::string_view source,
                           std::string_view fallback) {
  std::string result;
  result.reserve(source.size());
  for (unsigned char character : source) {
    const bool ascii_alphanumeric =
        (character >= 'a' && character <= 'z') ||
        (character >= 'A' && character <= 'Z') ||
        (character >= '0' && character <= '9');
    if (ascii_alphanumeric || character == '-' || character == '_') {
      result.push_back(static_cast<char>(character));
    } else if (!result.empty() && result.back() != '_') {
      result.push_back('_');
    }
  }
  while (!result.empty() && result.back() == '_') {
    result.pop_back();
  }
  if (result.size() > kMaximumGeneratedAssetStemBytes) {
    result.resize(kMaximumGeneratedAssetStemBytes);
    while (!result.empty() && result.back() == '_') {
      result.pop_back();
    }
  }
  return result.empty() ? std::string(fallback) : result;
}

ProjectIoResult<ProjectMaterialAssetDestination> make_unique_destination(
    const ProjectFileSystem &file_system, std::string_view directory,
    std::string_view source_stem, std::string_view fallback_stem,
    std::string_view extension) {
  const std::string stem = sanitized_stem(source_stem, fallback_stem);
  std::string normalized_extension = QStr::to_ascii_lower(extension);
  if (!normalized_extension.empty() && normalized_extension.front() != '.') {
    normalized_extension.insert(normalized_extension.begin(), '.');
  }

  for (std::size_t attempt = 0u;
       attempt < kMaximumUniqueDestinationAttempts; ++attempt) {
    const ProjectAssetId id = ProjectAssetId::generate();
    const std::string suffix(id.str().substr(0u, 8u));
    const std::string portable_path =
        std::string(directory) + "/" + stem + "_" + suffix +
        normalized_extension;
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
  entries_.push_back(std::move(transaction));
  return ProjectIoResult<void>::success();
}

std::string ProjectMaterialFileRollback::rollback_to(
    Checkpoint checkpoint) {
  std::string message;
  if (checkpoint > entries_.size()) {
    checkpoint = entries_.size();
  }
  for (std::size_t index = entries_.size(); index > checkpoint; --index) {
    quader_io::QdrAtomicFileTransaction &transaction =
        *entries_[index - 1u];
    const quader_io::QdrAtomicWriteResult rolled_back =
        transaction.rollback();
    if (!rolled_back.ok) {
      append_rollback_error(message, transaction.target_path(),
                            rolled_back.message);
    } else {
      entries_.erase(entries_.begin() +
                     static_cast<std::ptrdiff_t>(index - 1u));
    }
  }
  return message;
}

void ProjectMaterialFileRollback::commit() noexcept {
  for (const auto &transaction : entries_) {
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
