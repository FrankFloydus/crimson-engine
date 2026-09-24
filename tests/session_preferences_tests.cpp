////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "api/quader_preferences_api.hpp"
#include "core/session_preferences.hpp"
#include "ui/ui_settings.hpp"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace quader {
namespace {

class SessionPreferencesTests : public ::testing::Test {
protected:
  void SetUp() override {
    path_ = std::filesystem::temp_directory_path() /
            (std::string{"quader_session_preferences_"} +
             ::testing::UnitTest::GetInstance()->current_test_info()->name() +
             ".ini");
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  void TearDown() override {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  void write(std::string_view contents) const {
    std::ofstream file(path_, std::ios::trunc);
    ASSERT_TRUE(file.is_open());
    file << contents;
    file.flush();
    ASSERT_TRUE(file.good());
  }

  std::filesystem::path path_;
};

TEST_F(SessionPreferencesTests, MissingAndLegacyFilesKeepRecoveryDefaults) {
  const SessionPreferences defaults;
  EXPECT_EQ(defaults.undo_steps, 32);
  EXPECT_TRUE(defaults.autosave_enabled);
  EXPECT_EQ(defaults.autosave_interval_minutes, 2);
  EXPECT_EQ(defaults.autosave_max_files, 3);
  EXPECT_TRUE(defaults.valid());
  EXPECT_EQ(UISettings::load(path_).session_settings(), defaults);

  write("[Localization]\nLocale=it-IT\n[Rendering]\nVSync=false\n");
  const UISettings loaded = UISettings::load(path_);
  EXPECT_EQ(loaded.session_settings(), defaults);
  EXPECT_EQ(loaded.locale(), "it-IT");
  EXPECT_FALSE(loaded.vsync_enabled());
}

TEST_F(SessionPreferencesTests, RoundTripsEveryFieldIncludingDisabledHistory) {
  for (const SessionPreferences value : {
           SessionPreferences{},
           SessionPreferences{.undo_steps = 0,
                              .autosave_enabled = false,
                              .autosave_interval_minutes = 1,
                              .autosave_max_files = 1},
           SessionPreferences{.undo_steps = 1,
                              .autosave_enabled = true,
                              .autosave_interval_minutes = 5,
                              .autosave_max_files = 7},
           SessionPreferences{.undo_steps = 256,
                              .autosave_enabled = true,
                              .autosave_interval_minutes = 60,
                              .autosave_max_files = 20}}) {
    UISettings settings = UISettings::defaults();
    ASSERT_TRUE(settings.set_session_settings(value));
    settings.set_vsync_enabled(false);
    ASSERT_TRUE(settings.save(path_));
    const UISettings loaded = UISettings::load(path_);
    EXPECT_EQ(loaded.session_settings(), value);
    EXPECT_FALSE(loaded.vsync_enabled());
  }
}

TEST_F(SessionPreferencesTests, MalformedValuesDoNotDisableProtection) {
  for (const std::string_view invalid : {
           "", "-1", "257", "1.5", "2 minutes", "false",
           "99999999999999999999999999999"}) {
    SCOPED_TRACE(invalid);
    const std::string value{invalid};
    write("[History]\nUndoSteps=" + value +
          "\n[AutoSave]\nAutoSaveEnabled=invalid\nAutoSaveIntervalMinutes=" +
          value + "\nAutoSaveMaxFiles=" + value + '\n');
    EXPECT_EQ(UISettings::load(path_).session_settings(), SessionPreferences{});
  }

  write("UndoSteps=16\nUndoSteps=invalid\nAutoSaveIntervalMinutes=0\n"
        "AutoSaveMaxFiles=21\nAutoSaveEnabled=false\n");
  const SessionPreferences loaded = UISettings::load(path_).session_settings();
  EXPECT_EQ(loaded.undo_steps, 16);
  EXPECT_FALSE(loaded.autosave_enabled);
  EXPECT_EQ(loaded.autosave_interval_minutes, 2);
  EXPECT_EQ(loaded.autosave_max_files, 3);
}

TEST_F(SessionPreferencesTests, InvalidGroupsAreRejectedBeforeRuntimeMutation) {
  UISettings settings = UISettings::defaults();
  SessionPreferences initial{.undo_steps = 8,
                             .autosave_enabled = false,
                             .autosave_interval_minutes = 7,
                             .autosave_max_files = 5};
  ASSERT_TRUE(settings.set_session_settings(initial));
  QuaderPreferencesApi preferences;
  int setter_calls = 0;
  preferences.set_runtime_hooks({
      .session_settings = [&]() { return settings.session_settings(); },
      .set_session_settings = [&](SessionPreferences) {
        ++setter_calls;
        return QuaderApiVoidResult{.ok = true, .changed = true};
      },
  });

  for (const SessionPreferences invalid : {
           SessionPreferences{.undo_steps = -1},
           SessionPreferences{.undo_steps = 257},
           SessionPreferences{.autosave_interval_minutes = 0},
           SessionPreferences{.autosave_interval_minutes = 61},
           SessionPreferences{.autosave_max_files = 0},
           SessionPreferences{.autosave_max_files = 21}}) {
    EXPECT_FALSE(settings.set_session_settings(invalid));
    EXPECT_EQ(settings.session_settings(), initial);
    const QuaderApiVoidResult result =
        preferences.set_session_settings(invalid);
    EXPECT_FALSE(result.ok());
    EXPECT_FALSE(result.changed());
    EXPECT_EQ(result.mutation_kind, QuaderMutationKind::None);
    EXPECT_FALSE(result.error_message().empty());
    EXPECT_EQ(preferences.session_settings(), initial);
  }
  EXPECT_EQ(setter_calls, 0);
}

TEST_F(SessionPreferencesTests, ApiPreservesUnavailableAndFailedCommitState) {
  QuaderPreferencesApi preferences;
  const SessionPreferences requested{.undo_steps = 4,
                                     .autosave_enabled = false,
                                     .autosave_interval_minutes = 10,
                                     .autosave_max_files = 2};
  EXPECT_EQ(preferences.session_settings(), SessionPreferences{});
  const QuaderApiVoidResult unavailable =
      preferences.set_session_settings(requested);
  EXPECT_FALSE(unavailable.ok());
  EXPECT_FALSE(unavailable.changed());
  EXPECT_EQ(unavailable.mutation_kind, QuaderMutationKind::None);

  UISettings settings = UISettings::defaults();
  bool reject = false;
  preferences.set_runtime_hooks({
      .session_settings = [&]() { return settings.session_settings(); },
      .set_session_settings = [&](SessionPreferences value) {
        if (reject) {
          return QuaderApiVoidResult{
              .ok = false,
              .changed = false,
              .message = "Preferences could not be saved.",
              .mutation_kind = QuaderMutationKind::None,
          };
        }
        const bool changed = value != settings.session_settings();
        EXPECT_TRUE(settings.set_session_settings(value));
        return QuaderApiVoidResult{
            .ok = true,
            .changed = changed,
            .mutation_kind = QuaderMutationKind::Runtime,
        };
      },
  });
  const QuaderApiVoidResult committed =
      preferences.set_session_settings(requested);
  EXPECT_TRUE(committed.ok());
  EXPECT_TRUE(committed.changed());
  EXPECT_EQ(preferences.session_settings(), requested);
  const QuaderApiVoidResult unchanged =
      preferences.set_session_settings(requested);
  EXPECT_TRUE(unchanged.ok());
  EXPECT_FALSE(unchanged.changed());

  reject = true;
  const QuaderApiVoidResult failed =
      preferences.set_session_settings(SessionPreferences{});
  EXPECT_FALSE(failed.ok());
  EXPECT_FALSE(failed.changed());
  EXPECT_EQ(failed.mutation_kind, QuaderMutationKind::None);
  EXPECT_EQ(failed.error_message(), "Preferences could not be saved.");
  EXPECT_EQ(preferences.session_settings(), requested);
}

TEST_F(SessionPreferencesTests, DefaultMaterialTextureRoundTripsUnicodeAndReset) {
  UISettings settings;
  EXPECT_TRUE(UISettings::load(path_).default_material_texture().empty());
  const auto texture = std::filesystem::u8path(u8"C:/Utenti/é/default_material/纹理.png");
  settings.set_default_material_texture(texture);
  ASSERT_TRUE(settings.save(path_));
  EXPECT_EQ(UISettings::load(path_).default_material_texture(), texture);
  settings.set_default_material_texture({});
  ASSERT_TRUE(settings.save(path_));
  EXPECT_TRUE(UISettings::load(path_).default_material_texture().empty());
}

TEST_F(SessionPreferencesTests, InvalidDefaultMaterialPathKeepsOtherPreferences) {
  write("[Rendering]\nDefaultMaterialTexture=C:/images/\xff.png\nVSync=false\n");
  UISettings loaded;
  ASSERT_NO_THROW(loaded = UISettings::load(path_));
  EXPECT_TRUE(loaded.default_material_texture().empty());
  EXPECT_FALSE(loaded.vsync_enabled());
}

TEST_F(SessionPreferencesTests, DefaultMaterialApiDelegatesAndReportsMissingRuntime) {
  QuaderPreferencesApi preferences;
  EXPECT_TRUE(preferences.default_material_texture().empty());
  const auto absent = preferences.set_default_material_texture("new.png");
  EXPECT_FALSE(absent.ok());
  EXPECT_FALSE(absent.changed());
  std::filesystem::path value;
  preferences.set_runtime_hooks({
      .default_material_texture = [&]() { return value; },
      .set_default_material_texture = [&](std::filesystem::path requested) {
        value = std::move(requested);
        return QuaderApiVoidResult{.ok = true, .changed = true,
                                  .mutation_kind = QuaderMutationKind::Runtime};
      }});
  EXPECT_TRUE(preferences.set_default_material_texture("chosen.png").ok());
  EXPECT_EQ(preferences.default_material_texture(), std::filesystem::path{"chosen.png"});
  EXPECT_TRUE(preferences.set_default_material_texture({}).ok());
  EXPECT_TRUE(preferences.default_material_texture().empty());
}

} // namespace
} // namespace quader
