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

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
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
