////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_image_decoder.hpp"

#include <QBuffer>
#include <QByteArray>
#include <QImage>
#include <QImageReader>
#include <QString>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <system_error>

namespace quader {
namespace {

bool has_truevision_tga_footer(
    std::span<const std::byte> encoded_bytes) noexcept {
  static constexpr std::array<unsigned char, 18> kSignature{
      'T', 'R', 'U', 'E', 'V', 'I', 'S', 'I', 'O',
      'N', '-', 'X', 'F', 'I', 'L', 'E', '.', '\0',
  };
  return encoded_bytes.size() >= kSignature.size() &&
         std::equal(kSignature.begin(), kSignature.end(),
                    encoded_bytes.end() -
                        static_cast<std::ptrdiff_t>(kSignature.size()),
                    [](unsigned char expected, std::byte actual) {
                      return expected == std::to_integer<unsigned char>(actual);
                    });
}

bool unsupported_high_dynamic_range(const QImage &image) noexcept {
  switch (image.format()) {
  case QImage::Format_RGBX16FPx4:
  case QImage::Format_RGBA16FPx4:
  case QImage::Format_RGBA16FPx4_Premultiplied:
  case QImage::Format_RGBX32FPx4:
  case QImage::Format_RGBA32FPx4:
  case QImage::Format_RGBA32FPx4_Premultiplied:
    return true;
  default:
    return false;
  }
}

ProjectIoResult<ProjectDecodedImage>
decode_reader(QImageReader &reader, std::uintmax_t encoded_byte_size) {
  if (!reader.canRead()) {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::UnsupportedFormat,
        "Texture is not a supported still-image format: " +
            reader.errorString().toStdString());
  }
  if (reader.supportsAnimation() && reader.imageCount() != 1) {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::UnsupportedFormat,
        "Animated images are not supported as material textures.");
  }
  const QByteArray source_format = reader.format().toLower();
  if (source_format == "hdr" || source_format == "exr") {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::UnsupportedFormat,
        "HDR and EXR textures are not supported yet.");
  }
  const QSize declared_size = reader.size();
  if (declared_size.isValid() &&
      (declared_size.width() >
           static_cast<int>(kMaximumProjectTextureDimension) ||
       declared_size.height() >
           static_cast<int>(kMaximumProjectTextureDimension))) {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Texture dimensions exceed the supported 8192-pixel limit.");
  }

  QImage image = reader.read();
  if (image.isNull()) {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Texture could not be decoded: " + reader.errorString().toStdString());
  }
  if (image.width() <= 0 || image.height() <= 0 ||
      image.width() > static_cast<int>(kMaximumProjectTextureDimension) ||
      image.height() > static_cast<int>(kMaximumProjectTextureDimension)) {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Texture dimensions are invalid or exceed the supported limit.");
  }
  if (unsupported_high_dynamic_range(image)) {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::UnsupportedFormat,
        "HDR floating-point textures are not supported yet.");
  }
  const bool has_alpha_channel = image.hasAlphaChannel();
  image = image.convertToFormat(QImage::Format_RGBA8888);
  const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(image.width()) * 4u;
  const std::uint64_t total_bytes =
      row_bytes * static_cast<std::uint64_t>(image.height());
  if (total_bytes > std::numeric_limits<std::size_t>::max()) {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        "Decoded texture allocation exceeds the supported size.");
  }

  ProjectDecodedImage decoded{
      .width = static_cast<std::uint32_t>(image.width()),
      .height = static_cast<std::uint32_t>(image.height()),
      .rgba8 = std::vector<std::byte>(static_cast<std::size_t>(total_bytes)),
      .detected_format = source_format.toStdString(),
      .encoded_byte_size = encoded_byte_size,
      .has_alpha_channel = has_alpha_channel,
  };
  for (int row = 0; row < image.height(); ++row) {
    std::memcpy(decoded.rgba8.data() +
                    static_cast<std::size_t>(row) *
                        static_cast<std::size_t>(row_bytes),
                image.constScanLine(row), static_cast<std::size_t>(row_bytes));
  }
  return ProjectIoResult<ProjectDecodedImage>::success(std::move(decoded));
}

} // namespace

ProjectIoResult<ProjectDecodedImage>
ProjectImageDecoder::decode(std::span<const std::byte> encoded_bytes) {
  if (encoded_bytes.empty() ||
      encoded_bytes.size() > kMaximumProjectTextureEncodedBytes) {
    return ProjectIoResult<ProjectDecodedImage>::failure(
        ProjectIoErrorCode::MaterialMalformed,
        encoded_bytes.empty() ? "Texture data is empty."
                              : "Texture exceeds the supported 64 MiB limit.");
  }
  QByteArray bytes(reinterpret_cast<const char *>(encoded_bytes.data()),
                   static_cast<qsizetype>(encoded_bytes.size()));
  const auto encoded_byte_size = encoded_bytes.size();
  const auto decode = [&bytes,
                       encoded_byte_size](const QByteArray &format,
                                          bool decide_from_content) {
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::ReadOnly)) {
      return ProjectIoResult<ProjectDecodedImage>::failure(
          ProjectIoErrorCode::IoFailure,
          "Texture decode buffer could not be opened.");
    }
    QImageReader reader(&buffer, format);
    reader.setDecideFormatFromContent(decide_from_content);
    reader.setAutoTransform(false);
    return decode_reader(reader, encoded_byte_size);
  };

  const bool has_tga_footer = has_truevision_tga_footer(encoded_bytes);
  if (has_tga_footer) {
    ProjectIoResult<ProjectDecodedImage> tga = decode("tga", false);
    if (tga.ok()) {
      return tga;
    }
  }

  ProjectIoResult<ProjectDecodedImage> detected = decode({}, true);
  if (detected.ok()) {
    return detected;
  }

  // Uncompressed true-color TGA begins with the same 00 00 02 00 bytes as a
  // Windows cursor. Qt's content-only probe can therefore select the CUR
  // handler first and fail without trying the TGA plugin. A forced TGA retry
  // remains fully validated by QTgaHandler and makes byte-only project reads
  // deterministic after import.
  if (!has_tga_footer) {
    ProjectIoResult<ProjectDecodedImage> tga = decode("tga", false);
    if (tga.ok()) {
      return tga;
    }
  }
  return detected;
}

ProjectIoResult<ProjectImportedImage>
ProjectImageDecoder::read_import(const std::filesystem::path &source_path) {
  std::error_code error;
  if (source_path.empty() || !std::filesystem::is_regular_file(source_path,
                                                               error)) {
    return ProjectIoResult<ProjectImportedImage>::failure(
        ProjectIoErrorCode::FileNotRegular,
        error ? "Could not inspect texture source: " + error.message()
              : "Texture source is not a regular file.",
        source_path);
  }
  const std::uintmax_t size = std::filesystem::file_size(source_path, error);
  if (error || size == 0u || size > kMaximumProjectTextureEncodedBytes) {
    return ProjectIoResult<ProjectImportedImage>::failure(
        error ? ProjectIoErrorCode::IoFailure
              : ProjectIoErrorCode::MaterialMalformed,
        error ? "Could not inspect texture source: " + error.message()
              : size == 0u ? "Texture source is empty."
                           : "Texture exceeds the supported 64 MiB limit.",
        source_path);
  }
  std::ifstream input(source_path, std::ios::binary);
  if (!input.is_open()) {
    return ProjectIoResult<ProjectImportedImage>::failure(
        ProjectIoErrorCode::IoFailure, "Could not open texture source.",
        source_path);
  }
  std::vector<std::byte> bytes(static_cast<std::size_t>(size));
  input.read(reinterpret_cast<char *>(bytes.data()),
             static_cast<std::streamsize>(bytes.size()));
  if (!input || static_cast<std::size_t>(input.gcount()) != bytes.size()) {
    return ProjectIoResult<ProjectImportedImage>::failure(
        ProjectIoErrorCode::IoFailure, "Could not read texture source.",
        source_path);
  }
  ProjectIoResult<ProjectDecodedImage> decoded = decode(bytes);
  if (!decoded.ok()) {
    return ProjectIoResult<ProjectImportedImage>::failure(
        decoded.error().code, decoded.error().message, source_path);
  }
  std::string extension =
      QString::fromStdWString(source_path.extension().wstring())
          .toUtf8()
          .toStdString();
  if (extension.empty()) {
    extension = "." + decoded.value().detected_format;
  }
  return ProjectIoResult<ProjectImportedImage>::success({
      .source_path = source_path,
      .source_stem = QString::fromStdWString(source_path.stem().wstring())
                         .toUtf8()
                         .toStdString(),
      .source_extension = std::move(extension),
      .encoded_bytes = std::move(bytes),
      .decoded = decoded.take_value(),
  });
}

} // namespace quader
