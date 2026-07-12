////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "core/localization_service.hpp"
#include "ui/qt_settings_controller.hpp"
#include "ui/ui_settings.hpp"
#include "viewport/viewport_settings_defaults.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <vector>

#ifndef QUADER_SOURCE_DIR
#define QUADER_SOURCE_DIR "."
#endif

namespace quader {
namespace {

void expect_color_exact(ViewportSettingColor actual,
                        ViewportSettingColor expected) {
  EXPECT_FLOAT_EQ(actual.r, expected.r);
  EXPECT_FLOAT_EQ(actual.g, expected.g);
  EXPECT_FLOAT_EQ(actual.b, expected.b);
  EXPECT_FLOAT_EQ(actual.a, expected.a);
}

ViewportSettingColor changed_color(ViewportSettingColor initial) {
  initial.r = initial.r < 0.5f ? 0.875f : 0.125f;
  initial.a = initial.a < 0.5f ? 0.875f : 0.375f;
  return initial;
}

float changed_float(const ViewportFloatSettingDescriptor &descriptor) {
  return descriptor.default_value == descriptor.maximum ? descriptor.minimum
                                                        : descriptor.maximum;
}

LocalizationService loaded_localization(std::string_view locale = "en-US") {
  LocalizationService localization;
  EXPECT_TRUE(localization.load(
      std::filesystem::path(QUADER_SOURCE_DIR) / "resources" / "locales",
      std::string{locale}));
  return localization;
}

TEST(QtSettingsControllerTests,
     GridLabelsClearlySeparateGroundAndMeshSurfaceColors) {
  /**
   * Describes one expected localized grid color-setting label.
   */
  struct ExpectedColorLabel {
    ViewportColorSettingId id;
    std::string_view fallback;
    std::string_view english;
    std::string_view italian;
  };
  constexpr std::array<ExpectedColorLabel, 4> kExpectedColors{{
      {ViewportColorSettingId::GridMinor, "Ground Minor", "Ground Minor",
       "Griglia a terra minore"},
      {ViewportColorSettingId::GridMajor, "Ground Major", "Ground Major",
       "Griglia a terra maggiore"},
      {ViewportColorSettingId::MeshGridMinor, "Mesh Surface Minor",
       "Mesh Surface Minor", "Superficie mesh minore"},
      {ViewportColorSettingId::MeshGridMajor, "Mesh Surface Major",
       "Mesh Surface Major", "Superficie mesh maggiore"},
  }};

  const LocalizationService english = loaded_localization();
  const LocalizationService italian = loaded_localization("it-IT");
  for (const ExpectedColorLabel &expected : kExpectedColors) {
    const auto descriptor = std::find_if(
        viewport_color_settings().begin(), viewport_color_settings().end(),
        [&](const ViewportColorSettingDescriptor &candidate) {
          return candidate.id == expected.id;
        });
    ASSERT_NE(descriptor, viewport_color_settings().end());
    EXPECT_EQ(descriptor->label, expected.fallback);
    const std::string key =
        std::string{"viewport.settings."} + std::string{descriptor->key} +
        ".label";
    EXPECT_EQ(english.text(key, descriptor->label), expected.english);
    EXPECT_EQ(italian.text(key, descriptor->label), expected.italian);
  }

  EXPECT_EQ(english.text("viewport.settings.ShowGrid.label", ""),
            "Show Ground Grid");
  EXPECT_EQ(english.text("viewport.settings.ShowMeshGrid.label", ""),
            "Show Mesh Surface Grid");
  EXPECT_EQ(italian.text("viewport.settings.ShowGrid.label", ""),
            "Mostra griglia a terra");
  EXPECT_EQ(italian.text("viewport.settings.ShowMeshGrid.label", ""),
            "Mostra griglia superficie mesh");
  EXPECT_EQ(english.text("ui.settings.color.opacity", ""), "Opacity: %1%");
  EXPECT_EQ(italian.text("ui.settings.color.opacity", ""), "Opacità: %1%");
}

TEST(QtSettingsControllerTests,
     EveryOneOfTheFortyFourControlsMutatesItsOwnerAndNotifiesItsPhase) {
  static_assert(kViewportColorSettingCount == 20u);
  static_assert(kViewportFloatSettingCount == 16u);
  static_assert(kViewportBoolSettingCount == 7u);
  static_assert(1u + kViewportColorSettingCount + kViewportFloatSettingCount +
                    kViewportBoolSettingCount ==
                44u);

  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChangeKind> changes;
  QtSettingsController controller;
  controller.bind(ui_settings, viewport_settings, localization,
                  [&](QtSettingsChangeKind kind) { changes.push_back(kind); });

  std::size_t expected_preview_count = 0u;
  std::size_t expected_viewport_commit_count = 0u;
  for (const ViewportColorSettingDescriptor &descriptor :
       viewport_color_settings()) {
    const ViewportSettingColor initial = viewport_settings.color(descriptor.id);
    const ViewportSettingColor edited = changed_color(initial);
    ASSERT_TRUE(controller.preview_color(descriptor.id, edited))
        << descriptor.key;
    expect_color_exact(viewport_settings.color(descriptor.id), edited);
    ASSERT_TRUE(controller.commit_color(descriptor.id, initial))
        << descriptor.key;
    ++expected_preview_count;
    ++expected_viewport_commit_count;
  }

  for (const ViewportFloatSettingDescriptor &descriptor :
       viewport_float_settings()) {
    const float edited = changed_float(descriptor);
    ASSERT_TRUE(controller.set_float(descriptor.id, edited)) << descriptor.key;
    EXPECT_FLOAT_EQ(viewport_settings.float_value(descriptor.id), edited)
        << descriptor.key;
    ++expected_viewport_commit_count;
  }

  for (const ViewportBoolSettingDescriptor &descriptor :
       viewport_bool_settings()) {
    const bool edited = !descriptor.default_value;
    ASSERT_TRUE(controller.set_bool(descriptor.id, edited)) << descriptor.key;
    EXPECT_EQ(viewport_settings.bool_value(descriptor.id), edited)
        << descriptor.key;
    ++expected_viewport_commit_count;
  }

  ASSERT_TRUE(controller.select_locale("it-IT"));
  EXPECT_EQ(ui_settings.locale(), "it-IT");
  EXPECT_EQ(localization.current_locale(), "en-US");

  EXPECT_EQ(static_cast<std::size_t>(std::count(
                changes.begin(), changes.end(),
                QtSettingsChangeKind::ViewportPreview)),
            expected_preview_count);
  EXPECT_EQ(static_cast<std::size_t>(std::count(
                changes.begin(), changes.end(),
                QtSettingsChangeKind::ViewportCommit)),
            expected_viewport_commit_count);
  EXPECT_EQ(static_cast<std::size_t>(std::count(
                changes.begin(), changes.end(),
                QtSettingsChangeKind::InterfaceCommit)),
            1u);
}

TEST(QtSettingsControllerTests,
     ColorCancelRestoresTheExactInitialValueWithoutACommit) {
  UISettings ui_settings = UISettings::defaults();
  ViewportSettings viewport_settings = ViewportSettings::defaults();
  LocalizationService localization = loaded_localization();
  std::vector<QtSettingsChangeKind> changes;
  QtSettingsController controller;
  controller.bind(ui_settings, viewport_settings, localization,
                  [&](QtSettingsChangeKind kind) { changes.push_back(kind); });

  constexpr ViewportColorSettingId kSetting =
      ViewportColorSettingId::GridYAxis;
  const ViewportSettingColor initial = viewport_settings.color(kSetting);
  ASSERT_TRUE(controller.preview_color(kSetting, changed_color(initial)));
  ASSERT_TRUE(controller.rollback_color(kSetting, initial));

  expect_color_exact(viewport_settings.color(kSetting), initial);
  ASSERT_EQ(changes.size(), 2u);
  EXPECT_EQ(changes[0], QtSettingsChangeKind::ViewportPreview);
  EXPECT_EQ(changes[1], QtSettingsChangeKind::ViewportPreview);
  EXPECT_FALSE(controller.commit_color(kSetting, initial));
}

TEST(QtSettingsControllerTests,
     LoadsOnlyTheExactLegacyTransparentYAxisDefaultAsCorrectedDefault) {
  const std::filesystem::path migrated_path =
      std::filesystem::temp_directory_path() /
      "quader_legacy_y_axis_default_migration.ini";
  const std::filesystem::path custom_path =
      std::filesystem::temp_directory_path() /
      "quader_custom_transparent_y_axis.ini";
  std::filesystem::remove(migrated_path);
  std::filesystem::remove(custom_path);

  {
    std::ofstream file(migrated_path, std::ios::trunc);
    ASSERT_TRUE(file.is_open());
    file << "QVertexWorks.Quader.Viewport.GridYAxisColor=4A940000\n";
  }
  {
    std::ofstream file(custom_path, std::ios::trunc);
    ASSERT_TRUE(file.is_open());
    file << "QVertexWorks.Quader.Viewport.GridYAxisColor=4B940000\n";
  }

  ViewportSettings migrated = ViewportSettings::load(migrated_path);
  expect_color_exact(migrated.color(ViewportColorSettingId::GridYAxis),
                     kDefaultViewportGridYAxisColor);
  ASSERT_TRUE(migrated.save(migrated_path));
  {
    std::ifstream migrated_file(migrated_path);
    const std::string migrated_text{
        std::istreambuf_iterator<char>(migrated_file),
        std::istreambuf_iterator<char>()};
    EXPECT_NE(migrated_text.find("GridYAxisColor=4A9400B8"),
              std::string::npos);
  }

  const ViewportSettings custom = ViewportSettings::load(custom_path);
  const ViewportSettingColor custom_y =
      custom.color(ViewportColorSettingId::GridYAxis);
  EXPECT_FLOAT_EQ(custom_y.a, 0.0f);
  EXPECT_NEAR(custom_y.r, 75.0f / 255.0f, 0.00001f);

  std::filesystem::remove(migrated_path);
  std::filesystem::remove(custom_path);
}

} // namespace
} // namespace quader
