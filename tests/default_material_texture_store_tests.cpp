////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "editor/material/default_material_texture_store.hpp"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

#include <filesystem>
#include <memory>

namespace quader::test {
namespace {

class DefaultMaterialTextureStoreTests : public ::testing::Test {
protected:
  static void SetUpTestSuite() {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application = std::make_unique<QCoreApplication>(argc, argv);
    QCoreApplication::addLibraryPath(QCoreApplication::applicationDirPath());
  }

  static void TearDownTestSuite() { application.reset(); }

  std::filesystem::path path(const char *name) const {
    return std::filesystem::path(directory_.path().toStdWString()) / name;
  }

  void write_image(const std::filesystem::path &destination,
                   const char *format, QRgb color = qRgba(40, 110, 190, 255)) {
    ASSERT_TRUE(directory_.isValid());
    QImage image(8, 4, QImage::Format_ARGB32);
    image.fill(color);
    ASSERT_TRUE(image.save(QString::fromStdWString(destination.wstring()), format));
  }

  QByteArray read_bytes(const std::filesystem::path &source) const {
    QFile file(QString::fromStdWString(source.wstring()));
    EXPECT_TRUE(file.open(QIODevice::ReadOnly));
    return file.readAll();
  }

  QTemporaryDir directory_;

private:
  inline static int argc = 1;
  inline static char name[] = "quader-default-texture-tests";
  inline static char *argv[] = {name, nullptr};
  inline static std::unique_ptr<QCoreApplication> application;
};

TEST_F(DefaultMaterialTextureStoreTests,
       PngChoicePreservesPixelsAndSurvivesRemovingTheOriginal) {
  const auto source = path("source.png");
  write_image(source, "PNG", qRgba(40, 110, 190, 128));
  const auto original = DefaultMaterialTextureStore::validate_image(source);
  ASSERT_TRUE(original.ok()) << original.error().message;
  const auto imported = DefaultMaterialTextureStore::import_image(source, path("settings"));
  ASSERT_TRUE(imported.ok()) << imported.error().message;
  EXPECT_NE(imported.value(), source);
  EXPECT_EQ(imported.value().parent_path().parent_path(),
            path("settings") / "default_material");
  EXPECT_EQ(imported.value().filename(), source.filename());
  ASSERT_TRUE(std::filesystem::remove(source));
  const auto stored = DefaultMaterialTextureStore::validate_image(imported.value());
  ASSERT_TRUE(stored.ok()) << stored.error().message;
  EXPECT_EQ(stored.value().decoded.width, original.value().decoded.width);
  EXPECT_EQ(stored.value().decoded.height, original.value().decoded.height);
  EXPECT_EQ(stored.value().decoded.rgba8, original.value().decoded.rgba8);
}

TEST_F(DefaultMaterialTextureStoreTests,
       JpegBecomesRuntimeCompatiblePngAndIdenticalChoicesReuseTheFile) {
  const auto source = path("source.jpg");
  write_image(source, "JPEG");
  const auto original = DefaultMaterialTextureStore::validate_image(source);
  ASSERT_TRUE(original.ok()) << original.error().message;
  const auto imported = DefaultMaterialTextureStore::import_image(source, path("settings"));
  ASSERT_TRUE(imported.ok()) << imported.error().message;
  const auto stored = DefaultMaterialTextureStore::validate_image(imported.value());
  ASSERT_TRUE(stored.ok()) << stored.error().message;
  EXPECT_EQ(stored.value().decoded.detected_format, "png");
  EXPECT_EQ(stored.value().decoded.rgba8, original.value().decoded.rgba8);
  const auto initial_bytes = read_bytes(imported.value());
  const auto initial_time = std::filesystem::last_write_time(imported.value());
  const auto repeated = DefaultMaterialTextureStore::import_image(source, path("settings"));
  ASSERT_TRUE(repeated.ok()) << repeated.error().message;
  EXPECT_EQ(repeated.value(), imported.value());
  EXPECT_EQ(read_bytes(repeated.value()), initial_bytes);
  EXPECT_EQ(std::filesystem::last_write_time(repeated.value()), initial_time);
}

TEST_F(DefaultMaterialTextureStoreTests,
       InvalidAndUnsupportedChoicesDoNotCreateSettingsFiles) {
  const auto disguised_bitmap = path("not-really-png.png");
  write_image(disguised_bitmap, "BMP");
  const auto unsupported = DefaultMaterialTextureStore::import_image(
      disguised_bitmap, path("settings"));
  EXPECT_FALSE(unsupported.ok());
  EXPECT_EQ(unsupported.error().code, ProjectIoErrorCode::UnsupportedFormat);
  EXPECT_FALSE(std::filesystem::exists(path("settings")));
  EXPECT_FALSE(DefaultMaterialTextureStore::import_image(
      path("missing.png"), path("settings")).ok());
  EXPECT_FALSE(std::filesystem::exists(path("settings")));
}

TEST_F(DefaultMaterialTextureStoreTests,
       NewChoicesNeverOverwriteTheImageUsedByExistingSnapshots) {
  const auto source = path("source.png");
  write_image(source, "PNG");
  const auto first = DefaultMaterialTextureStore::import_image(source, path("settings"));
  ASSERT_TRUE(first.ok()) << first.error().message;
  const auto first_bytes = read_bytes(first.value());
  write_image(source, "PNG", qRgba(200, 30, 50, 255));
  const auto second = DefaultMaterialTextureStore::import_image(source, path("settings"));
  ASSERT_TRUE(second.ok()) << second.error().message;
  EXPECT_NE(first.value(), second.value());
  EXPECT_EQ(read_bytes(first.value()), first_bytes);
  EXPECT_NE(read_bytes(second.value()), first_bytes);
  const auto reset = DefaultMaterialTextureStore::import_image({}, path("settings"));
  ASSERT_TRUE(reset.ok());
  EXPECT_TRUE(reset.value().empty());
  EXPECT_EQ(read_bytes(first.value()), first_bytes);
}

TEST_F(DefaultMaterialTextureStoreTests,
       StorageFailurePreservesTheSourceAndPriorManagedImages) {
  const auto source = path("source.png");
  write_image(source, "PNG");
  const auto source_bytes = read_bytes(source);
  const auto imported = DefaultMaterialTextureStore::import_image(source, path("settings"));
  ASSERT_TRUE(imported.ok()) << imported.error().message;
  const auto stored_bytes = read_bytes(imported.value());
  const auto failed = DefaultMaterialTextureStore::import_image(source, imported.value());
  EXPECT_FALSE(failed.ok());
  EXPECT_EQ(read_bytes(source), source_bytes);
  EXPECT_EQ(read_bytes(imported.value()), stored_bytes);
}

TEST_F(DefaultMaterialTextureStoreTests,
       EmptyChoiceUsesTheBuiltinWithoutCreatingADirectory) {
  const auto result = DefaultMaterialTextureStore::import_image({}, path("settings"));
  ASSERT_TRUE(result.ok());
  EXPECT_TRUE(result.value().empty());
  EXPECT_FALSE(std::filesystem::exists(path("settings")));
}

} // namespace
} // namespace quader::test
