////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/default_material_texture_store.hpp"

#include "io/qdr/qdr_atomic_write.hpp"

#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QString>

#include <string_view>
#include <system_error>

namespace quader {

ProjectIoResult<ProjectImportedImage>
DefaultMaterialTextureStore::validate_image(
    const std::filesystem::path &source_path) {
  auto imported = ProjectImageDecoder::read_import(source_path);
  if (!imported.ok()) {
    return imported;
  }
  const auto &format = imported.value().decoded.detected_format;
  if (format != "png" && format != "jpeg" && format != "jpg") {
    return ProjectIoResult<ProjectImportedImage>::failure(
        ProjectIoErrorCode::UnsupportedFormat,
        "The default material texture must be a PNG or JPEG image.",
        source_path);
  }
  return imported;
}

ProjectIoResult<std::filesystem::path>
DefaultMaterialTextureStore::import_image(
    const std::filesystem::path &source_path,
    const std::filesystem::path &settings_directory) {
  using Result = ProjectIoResult<std::filesystem::path>;
  if (source_path.empty()) {
    return Result::success({});
  }
  auto imported = validate_image(source_path);
  if (!imported.ok()) {
    const auto &error = imported.error();
    return Result::failure(error.code, error.message, error.path);
  }
  if (settings_directory.empty()) {
    return Result::failure(ProjectIoErrorCode::InvalidArgument,
                           "The default texture settings directory is empty.");
  }

  // The existing runtime loader accepts PNG. Normalize JPEG at import, keeping
  // the renderer independent of Qt image plugins and the original source file.
  const auto &decoded = imported.value().decoded;
  const QImage image(
      reinterpret_cast<const unsigned char *>(decoded.rgba8.data()),
      static_cast<int>(decoded.width), static_cast<int>(decoded.height),
      static_cast<qsizetype>(decoded.width) * 4, QImage::Format_RGBA8888);
  QByteArray encoded;
  QBuffer buffer(&encoded);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG") ||
      static_cast<std::uintmax_t>(encoded.size()) >
          kMaximumProjectTextureEncodedBytes) {
    return Result::failure(
        ProjectIoErrorCode::IoFailure,
        "The default material image could not be stored within the texture size limit.",
        source_path);
  }

  std::error_code error;
  const auto directory =
      std::filesystem::absolute(settings_directory, error) / "default_material";
  if (error) {
    return Result::failure(ProjectIoErrorCode::IoFailure, error.message(),
                           settings_directory);
  }
  const auto hash = QCryptographicHash::hash(encoded, QCryptographicHash::Sha256)
                        .toHex().toStdString();
  const auto image_directory = directory / hash;
  std::filesystem::create_directories(image_directory, error);
  if (error) {
    return Result::failure(ProjectIoErrorCode::IoFailure, error.message(),
                           image_directory);
  }
  // Keep the chosen name useful in Settings while the parent identifies the
  // immutable pixel content. JPEG choices are normalized to PNG above.
  auto filename = source_path.filename();
  filename.replace_extension(".png");
  const auto destination = image_directory / filename;
  if (std::filesystem::exists(destination, error)) {
    // Never overwrite a prior choice: live snapshots may still reference it.
    const auto size = std::filesystem::file_size(destination, error);
    QFile existing(QString::fromStdWString(destination.wstring()));
    if (error || size != static_cast<std::uintmax_t>(encoded.size()) ||
        !existing.open(QIODevice::ReadOnly) || existing.readAll() != encoded) {
      return Result::failure(
          ProjectIoErrorCode::IoFailure,
          "An existing stored default texture could not be verified.",
          destination);
    }
    return Result::success(destination);
  }
  if (error) {
    return Result::failure(ProjectIoErrorCode::IoFailure, error.message(),
                           destination);
  }
  const auto written = quader_io::write_qdr_text_atomic(
      destination,
      std::string_view(encoded.constData(), static_cast<std::size_t>(encoded.size())),
      {}, quader_io::QdrAtomicWriteDisposition::CreateNew);
  if (!written.ok) {
    return Result::failure(ProjectIoErrorCode::IoFailure, written.message,
                           destination);
  }
  return Result::success(destination);
}

} // namespace quader
