////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/project_image_decoder.hpp"

#include <gtest/gtest.h>

#include <QBuffer>
#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

using namespace quader;

namespace {

/// Keeps Qt image plugins initialized throughout the decoder test suite.
class ProjectImageDecoderTests : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application = std::make_unique<QCoreApplication>(argc, argv);
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
  }

  static void TearDownTestSuite() { application.reset(); }

private:
  inline static int argc = 1;
  inline static char application_name[] = "quader-image-decoder-tests";
  inline static char *argv[] = {application_name, nullptr};
  inline static std::unique_ptr<QCoreApplication> application;
};

std::vector<std::byte> encoded_image(QImage image, const char *format) {
  QByteArray bytes;
  QBuffer buffer(&bytes);
  EXPECT_TRUE(buffer.open(QIODevice::WriteOnly));
  EXPECT_TRUE(image.save(&buffer, format));
  std::vector<std::byte> result(static_cast<std::size_t>(bytes.size()));
  for (qsizetype index = 0; index < bytes.size(); ++index) {
    result[static_cast<std::size_t>(index)] =
        static_cast<std::byte>(static_cast<unsigned char>(bytes[index]));
  }
  return result;
}

std::vector<std::byte> bytes_from_base64(std::string_view encoded) {
  const QByteArray decoded = QByteArray::fromBase64(
      QByteArray(encoded.data(), static_cast<qsizetype>(encoded.size())),
      QByteArray::AbortOnBase64DecodingErrors);
  std::vector<std::byte> result(static_cast<std::size_t>(decoded.size()));
  for (qsizetype index = 0; index < decoded.size(); ++index) {
    result[static_cast<std::size_t>(index)] =
        static_cast<std::byte>(static_cast<unsigned char>(decoded[index]));
  }
  return result;
}

/// Describes one encoded still-image fixture and its accepted Qt aliases.
struct StillImageFixture final {
  std::string_view display_name;
  std::string_view encoded_base64;
  std::uint32_t width = 0u;
  std::uint32_t height = 0u;
  std::array<std::string_view, 2> reader_formats;
};

const std::array<StillImageFixture, 7> &still_image_fixtures() {
  // Synthetic RGB still images keep decoder coverage independent of writer
  // support while exercising every format exposed by the texture picker.
  static constexpr std::array<StillImageFixture, 7> kFixtures{
      StillImageFixture{
          .display_name = "PNG",
          .encoded_base64 =
              "iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAACXBIWXMAAAABAAAA"
              "AQBPJcTWAAAAEElEQVR4nGMwTJkORAwQCgAfRgSxIZiKowAAAABJRU5ErkJggg==",
          .width = 2u,
          .height = 2u,
          .reader_formats = {"png", ""},
      },
      StillImageFixture{
          .display_name = "JPEG",
          .encoded_base64 =
              "/9j/4AAQSkZJRgABAgAAAQABAAD//gAQTGF2YzYwLjMxLjEwMgD/2wBDAAgEBAQE"
              "BAUFBQUFBQYGBgYGBgYGBgYGBgYHBwcICAgHBwcGBgcHCAgICAkJCQgICAgJCQoK"
              "CgwMCwsODg4RERT/xABMAAEBAAAAAAAAAAAAAAAAAAAABgEBAQAAAAAAAAAAAAAA"
              "AAAABQYQAQAAAAAAAAAAAAAAAAAAAAARAQAAAAAAAAAAAAAAAAAAAAD/wAARCAAC"
              "AAIDASIAAhEAAxEA/9oADAMBAAIRAxEAPwCbAUQF/9k=",
          .width = 2u,
          .height = 2u,
          .reader_formats = {"jpeg", "jpg"},
      },
      StillImageFixture{
          .display_name = "BMP",
          .encoded_base64 =
              "Qk1GAAAAAAAAADYAAAAoAAAAAgAAAAIAAAABABgAAAAAABAAAAAAAAAAAAAAAAAAA"
              "AAAAAAAAAD/AP8AAAAA/wD///8AAA==",
          .width = 2u,
          .height = 2u,
          .reader_formats = {"bmp", ""},
      },
      StillImageFixture{
          .display_name = "GIF",
          .encoded_base64 =
              "R0lGODlhAQABAIAAAAAAAP///ywAAAAAAQABAAACAUwAOw==",
          .width = 1u,
          .height = 1u,
          .reader_formats = {"gif", ""},
      },
      StillImageFixture{
          .display_name = "WebP",
          .encoded_base64 =
              "UklGRj4AAABXRUJQVlA4IDIAAADQAQCdASoCAAIAAMASJaACdLoB+AADsAD+98Rf"
              "/5l58tC+Whf8y8/+5APZAPZAP37gAA==",
          .width = 2u,
          .height = 2u,
          .reader_formats = {"webp", ""},
      },
      StillImageFixture{
          .display_name = "TIFF",
          .encoded_base64 =
              "SUkqAGEAAAD9XwGeZggACAAIAEgAAAABAAAASAAAAAEAAABMYXZjNjAuMzEuMTAy"
              "AA8AAAABAAAA6wAAAAEAAACAAAAAAQAAAPAAAAABAAAAgAAAAAEAAADwAAAAAQAA"
              "ABAA/gAEAAEAAAAAAAAAAAEEAAEAAAACAAAAAQEEAAEAAAACAAAAAgEDAAMAAAAN"
              "AAAAAwEDAAEAAAAFgAAABgEDAAEAAAAGAAAAEQEEAAEAAAAIAAAAFQEDAAEAAAAD"
              "AAAAFgEEAAEAAAAACAAAFwEEAAEAAAAFAAAAGgEFAAEAAAATAAAAGwEFAAEAAAAb"
              "AAAAKAEDAAEAAAACAAAAMQECAA4AAAAjAAAAEgIDAAIAAAACAAIAFAIFAAYAAAAx"
              "AAAAAAAAAA==",
          .width = 2u,
          .height = 2u,
          .reader_formats = {"tiff", "tif"},
      },
      StillImageFixture{
          .display_name = "TGA",
          .encoded_base64 =
              "AAACAAAAAAAAAAAAAgACABgAv18fv18fv18fv18fAAAAAAAAAABUUlVFVklTSU9O"
              "LVhGSUxFLgA=",
          .width = 2u,
          .height = 2u,
          .reader_formats = {"tga", ""},
      },
  };
  return kFixtures;
}

std::vector<std::byte> animated_gif() {
  constexpr std::array<unsigned char, 107> kBytes{
      0x47, 0x49, 0x46, 0x38, 0x39, 0x61, 0x01, 0x00, 0x01, 0x00, 0x81,
      0x00, 0x00, 0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x21, 0xff, 0x0b, 0x4e, 0x45, 0x54, 0x53, 0x43,
      0x41, 0x50, 0x45, 0x32, 0x2e, 0x30, 0x03, 0x01, 0x00, 0x00, 0x00,
      0x21, 0xf9, 0x04, 0x00, 0x0a, 0x00, 0x00, 0x00, 0x2c, 0x00, 0x00,
      0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x00, 0x08, 0x04, 0x00, 0x01,
      0x04, 0x04, 0x00, 0x21, 0xf9, 0x04, 0x01, 0x0a, 0x00, 0x01, 0x00,
      0x2c, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x81, 0x00,
      0xff, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x08, 0x04, 0x00, 0x01, 0x04, 0x04, 0x00, 0x3b,
  };
  std::vector<std::byte> result(kBytes.size());
  for (std::size_t index = 0u; index < kBytes.size(); ++index) {
    result[index] = static_cast<std::byte>(kBytes[index]);
  }
  return result;
}

std::vector<std::byte> animated_webp() {
  // Synthetic two-frame 2x2 lossless animation.
  return bytes_from_base64(
      "UklGRp4AAABXRUJQVlA4WAoAAAASAAAAAQAAAQAAQU5JTQYAAAD/////AABBTk1G"
      "NgAAAAAAAAAAAAEAAAEAAPQBAAJWUDhMHgAAAC8BQAAAFzD/AoIi/0eb//kPNAsK"
      "27ZBYXEQ0f/IA0FOTUY0AAAAAAAAAAAAAQAAAQAA9AEAAFZQOEwcAAAALwFAABAX"
      "IBBIYZM//wKCIv9Hm/+AvcEYRPQ/BA==");
}

bool supports_any_format(const QList<QByteArray> &supported,
                         const std::array<std::string_view, 2> &aliases) {
  return std::any_of(
      aliases.begin(), aliases.end(), [&](std::string_view alias) {
        if (alias.empty()) {
          return false;
        }
        return std::any_of(supported.cbegin(), supported.cend(),
                           [&](const QByteArray &candidate) {
                             return candidate.toLower() ==
                                    QByteArray(
                                        alias.data(),
                                        static_cast<qsizetype>(alias.size()));
                           });
      });
}

TEST_F(ProjectImageDecoderTests, AdvertisesEveryTexturePickerStillImageFormat) {
  const QList<QByteArray> supported = QImageReader::supportedImageFormats();
  for (const StillImageFixture &fixture : still_image_fixtures()) {
    SCOPED_TRACE(fixture.display_name);
    EXPECT_TRUE(supports_any_format(supported, fixture.reader_formats));
  }
}

TEST_F(ProjectImageDecoderTests, DecodesEveryTexturePickerStillImageFormat) {
  for (const StillImageFixture &fixture : still_image_fixtures()) {
    SCOPED_TRACE(fixture.display_name);
    const std::vector<std::byte> encoded =
        bytes_from_base64(fixture.encoded_base64);
    ASSERT_FALSE(encoded.empty());

    const ProjectIoResult<ProjectDecodedImage> decoded =
        ProjectImageDecoder::decode(encoded);
    ASSERT_TRUE(decoded.ok()) << decoded.error().message;
    EXPECT_EQ(decoded.value().width, fixture.width);
    EXPECT_EQ(decoded.value().height, fixture.height);
    EXPECT_EQ(decoded.value().rgba8.size(),
              static_cast<std::size_t>(fixture.width) *
                  static_cast<std::size_t>(fixture.height) * 4u);
    const bool detected_format_matches =
        std::any_of(fixture.reader_formats.begin(),
                    fixture.reader_formats.end(), [&](std::string_view alias) {
                      return !alias.empty() &&
                             decoded.value().detected_format == alias;
                    });
    EXPECT_TRUE(detected_format_matches)
        << "Detected format: " << decoded.value().detected_format;
  }
}

TEST_F(ProjectImageDecoderTests, DecodesRgba8AndHonorsTheEightKBoundary) {
  QImage allowed(static_cast<int>(kMaximumProjectTextureDimension), 1,
                 QImage::Format_RGBA8888);
  allowed.fill(QColor(30, 60, 90, 255));
  const auto allowed_bytes = encoded_image(std::move(allowed), "PNG");
  const ProjectIoResult<ProjectDecodedImage> decoded =
      ProjectImageDecoder::decode(allowed_bytes);
  ASSERT_TRUE(decoded.ok()) << decoded.error().message;
  EXPECT_EQ(decoded.value().width, kMaximumProjectTextureDimension);
  EXPECT_EQ(decoded.value().height, 1u);
  EXPECT_EQ(decoded.value().detected_format, "png");
  EXPECT_EQ(decoded.value().rgba8.size(),
            static_cast<std::size_t>(kMaximumProjectTextureDimension) * 4u);

  QImage too_wide(static_cast<int>(kMaximumProjectTextureDimension) + 1, 1,
                  QImage::Format_RGBA8888);
  too_wide.fill(Qt::white);
  const auto too_wide_bytes = encoded_image(std::move(too_wide), "PNG");
  const ProjectIoResult<ProjectDecodedImage> rejected =
      ProjectImageDecoder::decode(too_wide_bytes);
  EXPECT_FALSE(rejected.ok());
  EXPECT_NE(rejected.error().message.find("8192"), std::string::npos);
}

TEST_F(ProjectImageDecoderTests, RejectsMalformedAndAnimatedData) {
  const std::array<std::byte, 8> malformed{
      std::byte{0x89}, std::byte{'N'}, std::byte{'O'}, std::byte{'T'},
      std::byte{'P'}, std::byte{'N'}, std::byte{'G'}, std::byte{0x00},
  };
  EXPECT_FALSE(ProjectImageDecoder::decode(malformed).ok());

  const ProjectIoResult<ProjectDecodedImage> animation =
      ProjectImageDecoder::decode(animated_gif());
  ASSERT_FALSE(animation.ok());
  EXPECT_NE(animation.error().message.find("Animated"), std::string::npos)
      << animation.error().message;

  const ProjectIoResult<ProjectDecodedImage> webp_animation =
      ProjectImageDecoder::decode(animated_webp());
  ASSERT_FALSE(webp_animation.ok());
  EXPECT_NE(webp_animation.error().message.find("Animated"), std::string::npos)
      << webp_animation.error().message;
}

TEST_F(ProjectImageDecoderTests, RejectsRadianceHdrInput) {
  std::string radiance =
      "#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 1 +X 1\n";
  radiance.push_back(static_cast<char>(0x80));
  radiance.push_back(static_cast<char>(0x80));
  radiance.push_back(static_cast<char>(0x80));
  radiance.push_back(static_cast<char>(0x81));
  const std::span<const char> characters(radiance.data(), radiance.size());
  const std::span<const std::byte> bytes = std::as_bytes(characters);

  const ProjectIoResult<ProjectDecodedImage> rejected =
      ProjectImageDecoder::decode(bytes);

  EXPECT_FALSE(rejected.ok());
}

TEST_F(ProjectImageDecoderTests, ConvertsSixteenBitSdrPngToRgba8) {
  QImage high_depth(2, 2, QImage::Format_RGBA64);
  high_depth.fill(QColor::fromRgbF(0.25, 0.5, 0.75, 1.0));
  const auto high_depth_bytes =
      encoded_image(std::move(high_depth), "PNG");
  const ProjectIoResult<ProjectDecodedImage> high_depth_result =
      ProjectImageDecoder::decode(high_depth_bytes);
  ASSERT_TRUE(high_depth_result.ok()) << high_depth_result.error().message;
  EXPECT_EQ(high_depth_result.value().width, 2u);
  EXPECT_EQ(high_depth_result.value().height, 2u);
  EXPECT_EQ(high_depth_result.value().rgba8.size(), 16u);
}

TEST_F(ProjectImageDecoderTests, RejectsEmptyAndOversizedEncodedInputs) {
  EXPECT_FALSE(ProjectImageDecoder::decode({}).ok());
  std::vector<std::byte> oversized(
      static_cast<std::size_t>(kMaximumProjectTextureEncodedBytes) + 1u);
  const ProjectIoResult<ProjectDecodedImage> result =
      ProjectImageDecoder::decode(oversized);
  ASSERT_FALSE(result.ok());
  EXPECT_NE(result.error().message.find("64 MiB"), std::string::npos);
}

} // namespace
