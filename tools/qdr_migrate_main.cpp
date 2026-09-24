////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "qdr_retired_schema10_converter.hpp"

#include "io/qdr/qdr_atomic_write.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "io/shared_file_read.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace quader;

/** Keeps original bytes alive until the complete batch has committed. */
struct Migration final {
  std::filesystem::path path;
  std::vector<std::byte> original;
  std::vector<std::byte> migrated;
  std::unique_ptr<quader_io::QdrAtomicFileTransaction> transaction;
};

std::vector<std::byte> read_bytes(const std::filesystem::path &path) {
  auto read = quader_io::read_file_bytes_shared(path, 2ull * 1024ull * 1024ull * 1024ull);
  if (!read.ok) {
    throw std::runtime_error(path.string() + ": " + read.message);
  }
  return std::move(read.bytes);
}

void require_result(const quader_io::QdrAtomicWriteResult &result) {
  if (!result.ok) {
    throw std::runtime_error(result.message);
  }
}

QdrProjectWriteSnapshot snapshot_from_read(const QdrProjectReadResult &read) {
  auto snapshot = qdr_project_write_snapshot_from_document(read.document);
  if (!snapshot.ok()) {
    throw std::runtime_error(snapshot.error().message);
  }
  snapshot.value().project_asset_dependencies = read.project_asset_dependencies;
  snapshot.value().viewport_camera_pose = read.viewport_camera_pose;
  return snapshot.take_value();
}

void require_source_unchanged(const Migration &migration) {
  if (read_bytes(migration.path) != migration.original) {
    throw std::runtime_error(migration.path.string() +
                             ": source changed during migration; no replacement is authorized.");
  }
}

Migration prepare_migration(const std::filesystem::path &path) {
  Migration result;
  result.path = path;
  result.original = read_bytes(path);
  auto read = QdrDocumentReader::deserialize_project_document(result.original);
  std::array<std::byte, 16> scene_uuid{};
  QdrProjectWriteSnapshot snapshot;
  bool retired = false;
  if (read.ok) {
    scene_uuid = read.scene_uuid;
    snapshot = snapshot_from_read(read);
  } else {
    auto container = quader_io::read_qdr_binary_container(result.original);
    scene_uuid = container.scene_uuid;
    try {
      snapshot = migration::decode_retired_schema10(container);
    } catch (const std::exception &exception) {
      throw std::runtime_error(path.string() + ": canonical read failed: " + read.message +
                               "; retired conversion failed: " + exception.what());
    }
    retired = true;
  }
  result.migrated = QdrDocumentWriter::serialize_project_document_bytes(snapshot, scene_uuid);
  const auto check = QdrDocumentReader::deserialize_project_document(result.migrated);
  if (!check.ok || check.scene_uuid != scene_uuid) {
    throw std::runtime_error(path.string() + ": migrated scene failed canonical reread: " + check.message);
  }
  const auto replay = QdrDocumentWriter::serialize_project_document_bytes(snapshot_from_read(check), scene_uuid);
  if (replay != result.migrated) {
    throw std::runtime_error(path.string() +
                             ": semantic round trip changed the canonical snapshot; original preserved.");
  }
  std::cout << path.string() << ": validated " << snapshot.entities.size() << " entities, "
            << snapshot.root_entities.size() << " roots, " << snapshot.meshes.size()
            << " mesh assets, " << snapshot.project_asset_dependencies.size()
            << " dependencies; " << (retired ? "retired schema 10 -> 16" : "canonical schema 16")
            << "; original hash " << quader_io::qdr_binary_content_hash(result.original)
            << ", output hash " << quader_io::qdr_binary_content_hash(result.migrated) << '\n';
  return result;
}

void write_staging(const Migration &migration) {
  // The canonical writer flushes file contents to storage before publication.
  // The outer transaction has only reserved this path and seals it later.
  require_result(quader_io::write_qdr_text_atomic(
      migration.transaction->staging_path(),
      std::string_view(reinterpret_cast<const char *>(migration.migrated.data()),
                       migration.migrated.size())));
  if (read_bytes(migration.transaction->staging_path()) != migration.migrated) {
    throw std::runtime_error("Migration staging verification failed.");
  }
  const auto check = QdrDocumentReader::read_project_document(migration.transaction->staging_path());
  if (!check.ok) {
    throw std::runtime_error("Staged migration cannot be read: " + check.message);
  }
}

void apply_batch(std::vector<Migration> &migrations, const std::filesystem::path &backup_directory) {
  if (std::filesystem::exists(backup_directory)) {
    throw std::runtime_error("Backup directory must be a new path; existing backups are never overwritten.");
  }
  for (const auto &migration : migrations) {
    require_source_unchanged(migration);
  }
  if (!std::filesystem::create_directory(backup_directory)) {
    throw std::runtime_error("Could not create the backup directory.");
  }
  for (const auto &migration : migrations) {
    const auto backup = backup_directory / migration.path.filename();
    require_result(quader_io::write_qdr_text_atomic(
        backup, std::string_view(reinterpret_cast<const char *>(migration.original.data()),
                                 migration.original.size()), {},
        quader_io::QdrAtomicWriteDisposition::CreateNew));
    if (read_bytes(backup) != migration.original) {
      throw std::runtime_error("Original backup verification failed; source files are unchanged.");
    }
  }
  for (auto &migration : migrations) {
    migration.transaction = std::make_unique<quader_io::QdrAtomicFileTransaction>();
    require_result(migration.transaction->prepare(migration.path));
    write_staging(migration);
  }
  try {
    for (auto &migration : migrations) {
      require_source_unchanged(migration);
      require_result(migration.transaction->publish());
      // ReplaceFileW captures the exact displaced version. Recheck that copy
      // after replacement so a concurrent save between our preflight read and
      // publication causes rollback instead of discarding the newer source.
      if (read_bytes(migration.transaction->backup_path()) != migration.original) {
        throw std::runtime_error(migration.path.string() +
                                 ": source changed during atomic replacement; rolling back the complete batch.");
      }
    }
    for (const auto &migration : migrations) {
      require_result(migration.transaction->verify_published());
    }
  } catch (...) {
    for (auto iterator = migrations.rbegin(); iterator != migrations.rend(); ++iterator) {
      if (iterator->transaction && iterator->transaction->active()) {
        const auto rollback = iterator->transaction->rollback();
        if (!rollback.ok) {
          std::cerr << "Rollback requires recovery for " << iterator->path.string() << ": "
                    << rollback.message << "; original retained in " << backup_directory.string() << '\n';
        }
      }
    }
    throw;
  }
  // All replacements are verified before any rollback copy is released. The
  // separate immutable originals remain available even after commit cleanup.
  for (auto &migration : migrations) {
    const auto committed = migration.transaction->commit();
    if (!committed.ok) {
      // Do not let destructors roll back only part of a verified batch.
      for (auto &remaining : migrations) {
        if (remaining.transaction->published()) {
          (void)remaining.transaction->preserve_published_for_recovery();
        }
      }
      throw std::runtime_error("Commit needs recovery; verified outputs and originals retained: " + committed.message);
    }
    if (!committed.message.empty()) {
      std::cerr << committed.message << '\n';
    }
  }
  std::cout << "Migrated " << migrations.size() << " scenes. Original backups: "
            << backup_directory.string() << '\n';
}

} // namespace

int main(int argc, char **argv) {
  try {
    bool apply = false;
    std::filesystem::path backup_directory;
    std::vector<std::filesystem::path> paths;
    for (int index = 1; index < argc; ++index) {
      const std::string_view argument(argv[index]);
      if (argument == "--apply") {
        apply = true;
      } else if (argument == "--backup-directory" && index + 1 < argc) {
        backup_directory = std::filesystem::absolute(argv[++index]);
      } else if (argument.starts_with("--")) {
        throw std::runtime_error("Unknown or incomplete option: " + std::string(argument));
      } else {
        paths.push_back(std::filesystem::absolute(argv[index]));
      }
    }
    if (paths.empty() || (apply && backup_directory.empty())) {
      std::cerr << "Usage: qdr_migrate [--apply --backup-directory <new-directory>] <scene.qdr> [...]\n"
                   "Without --apply, validates the complete migration without writing files.\n";
      return 2;
    }
    for (std::size_t index = 0u; index < paths.size(); ++index) {
      if (!std::filesystem::is_regular_file(paths[index]) || std::filesystem::is_symlink(paths[index])) {
        throw std::runtime_error("Migration sources must be regular, non-symlink files.");
      }
      for (std::size_t earlier = 0u; earlier < index; ++earlier) {
        if (std::filesystem::equivalent(paths[index], paths[earlier]) ||
            paths[index].filename() == paths[earlier].filename()) {
          throw std::runtime_error("Duplicate source or backup filename in migration batch.");
        }
      }
    }
    std::vector<Migration> migrations;
    for (const auto &path : paths) {
      migrations.push_back(prepare_migration(path));
    }
    if (apply) {
      apply_batch(migrations, backup_directory);
    } else {
      std::cout << "Dry run completed. No source files were changed.\n";
    }
    return 0;
  } catch (const std::exception &exception) {
    std::cerr << "Migration failed: " << exception.what() << '\n';
    return 1;
  }
}
