////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_command_ids.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/scene_session.hpp"
#include "support/command_behavior_contract.hpp"
#include "support/command_behavior_fixture.hpp"
#include "support/command_observable_state.hpp"

#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <random>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace quader::test {
namespace {

using Json = nlohmann::ordered_json;

constexpr std::array kObservableDomains{
    CommandObservableDomain::Documents,
    CommandObservableDomain::Scene,
    CommandObservableDomain::Geometry,
    CommandObservableDomain::Selection,
    CommandObservableDomain::Overlay,
    CommandObservableDomain::Assets,
    CommandObservableDomain::History,
    CommandObservableDomain::Tools,
    CommandObservableDomain::Viewports,
    CommandObservableDomain::Preferences,
    CommandObservableDomain::Revisions,
};

constexpr CommandObservableDomainMask kSemanticDomains =
    kAllCommandObservableDomains &
    ~command_observable_domain_mask(CommandObservableDomain::Revisions);

[[nodiscard]] bool has_any(ObservableDomain domains,
                           ObservableDomain candidates) {
  return static_cast<std::uint64_t>(domains & candidates) != 0u;
}

[[nodiscard]] CommandObservableDomainMask
observable_mask_for_contract(const CommandBehaviorContract &contract) {
  const ObservableDomain declared = contract.observable_domains;
  CommandObservableDomainMask result = 0u;

  if (has_any(declared, ObservableDomain::Document |
                            ObservableDomain::Project |
                            ObservableDomain::SceneDocuments)) {
    result |= command_observable_domain_mask(
        CommandObservableDomain::Documents);
  }
  if (has_any(declared, ObservableDomain::SceneGraph |
                            ObservableDomain::Visibility |
                            ObservableDomain::Layers |
                            ObservableDomain::Instances |
                            ObservableDomain::Project |
                            ObservableDomain::SceneDocuments)) {
    result |= command_observable_domain_mask(CommandObservableDomain::Scene);
  }
  if (has_any(declared, ObservableDomain::ModelingContent)) {
    result |=
        command_observable_domain_mask(CommandObservableDomain::Geometry);
    result |= command_observable_domain_mask(CommandObservableDomain::Assets);
  }
  if (has_any(declared, ObservableDomain::Selection)) {
    result |=
        command_observable_domain_mask(CommandObservableDomain::Selection);
  }
  if (has_any(declared, ObservableDomain::Overlay)) {
    result |=
        command_observable_domain_mask(CommandObservableDomain::Overlay);
  }
  if (has_any(declared, ObservableDomain::Assets)) {
    result |= command_observable_domain_mask(CommandObservableDomain::Assets);
  }
  if (has_any(declared, ObservableDomain::History)) {
    result |=
        command_observable_domain_mask(CommandObservableDomain::History);
  }
  if (has_any(declared, ObservableDomain::Tool)) {
    result |= command_observable_domain_mask(CommandObservableDomain::Tools);
  }
  if (has_any(declared, ObservableDomain::Viewport)) {
    result |=
        command_observable_domain_mask(CommandObservableDomain::Viewports);
  }
  if (has_any(declared, ObservableDomain::Preferences)) {
    result |= command_observable_domain_mask(
        CommandObservableDomain::Preferences);
  }
  if (has_any(declared, ObservableDomain::Revisions)) {
    result |=
        command_observable_domain_mask(CommandObservableDomain::Revisions);
  }
  return result;
}

[[nodiscard]] bool same_revisions(const QuaderRevisionStamp &left,
                                  const QuaderRevisionStamp &right) {
  return left.project_revision == right.project_revision &&
         left.scene_revision == right.scene_revision &&
         left.scene_layer_revision == right.scene_layer_revision &&
         left.asset_revision == right.asset_revision &&
         left.modeling_content_revision ==
             right.modeling_content_revision &&
         left.modeling_selection_revision ==
             right.modeling_selection_revision;
}

[[nodiscard]] std::vector<EntityId>
selected_entities(const QuaderEditorApi &editor) {
  std::vector<EntityId> result;
  for (const QuaderEntitySnapshot &entity : editor.query().entities()) {
    if (entity.selected) {
      result.push_back(entity.id);
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

[[nodiscard]] bool mutation_kind_allowed(
    const CommandBehaviorContract &contract, QuaderMutationKind kind) {
  const ObservableDomain domains = contract.observable_domains;
  switch (kind) {
  case QuaderMutationKind::None:
    return true;
  case QuaderMutationKind::Document:
    return has_any(domains, ObservableDomain::Document |
                                ObservableDomain::SceneGraph |
                                ObservableDomain::Visibility |
                                ObservableDomain::Layers |
                                ObservableDomain::Instances |
                                ObservableDomain::Project |
                                ObservableDomain::SceneDocuments |
                                ObservableDomain::History);
  case QuaderMutationKind::Modeling:
    return has_any(domains, ObservableDomain::ModelingContent |
                                ObservableDomain::Selection |
                                ObservableDomain::Overlay |
                                ObservableDomain::Tool |
                                ObservableDomain::History);
  case QuaderMutationKind::Asset:
    return has_any(domains, ObservableDomain::Assets |
                                ObservableDomain::Document |
                                ObservableDomain::ModelingContent);
  case QuaderMutationKind::Runtime:
    return has_any(domains, ObservableDomain::Viewport |
                                ObservableDomain::Preferences |
                                ObservableDomain::Tool |
                                ObservableDomain::Project |
                                ObservableDomain::SceneDocuments |
                                ObservableDomain::Clipboard |
                                ObservableDomain::Instances |
                                ObservableDomain::ExternalRuntime);
  }
  return false;
}

void expect_global_command_laws(
    const CommandBehaviorContract &contract,
    const QuaderCommandDescriptor &descriptor,
    const CommandObservableState &before,
    const CommandObservableState &after,
    std::uint64_t external_calls_before,
    std::uint64_t external_calls_after,
    const QuaderApiVoidResult &result,
    const QuaderEditorApi &editor,
    bool enforce_canonical_outcome) {
  const CommandObservableStateDiff difference =
      diff_command_observable_state(before, after);
  const CommandObservableDomainMask allowed =
      observable_mask_for_contract(contract);
  const CommandObservableDomainMask unexpected =
      difference.changed_domains & ~allowed;

  EXPECT_TRUE(descriptor.enabled)
      << contract.command_id
      << ": canonical fixture did not enable its command";
  EXPECT_NE(result.error_message(), "Unknown command id.")
      << contract.command_id;
  EXPECT_NE(result.error_message(), "Command has no handler.")
      << contract.command_id;
  EXPECT_NE(result.error_message(), "Command is disabled.")
      << contract.command_id;

  const bool rejection_allowed =
      contract.outcome == CommandOutcomePolicy::MayReject;
  if (!result.ok()) {
    EXPECT_TRUE(rejection_allowed) << contract.command_id << ": "
                                   << result.error_message();
    EXPECT_FALSE(result.changed()) << contract.command_id;
    EXPECT_EQ(result.mutation_kind, QuaderMutationKind::None)
        << contract.command_id;
    EXPECT_EQ(difference.changed_domains & kSemanticDomains, 0u)
        << contract.command_id << ": rejected command mutated state:\n"
        << difference.summary;
  } else if (enforce_canonical_outcome) {
    if (contract.outcome == CommandOutcomePolicy::ChangeRequired) {
      EXPECT_TRUE(result.changed()) << contract.command_id;
    }
  }

  EXPECT_EQ(unexpected, 0u)
      << contract.command_id << ": command escaped its declared domains:\n"
      << difference.summary;
  EXPECT_TRUE(mutation_kind_allowed(contract, result.mutation_kind))
      << contract.command_id << ": undeclared mutation kind "
      << static_cast<int>(result.mutation_kind);
  EXPECT_TRUE(same_revisions(result.revisions, editor.revisions()))
      << contract.command_id
      << ": result revisions do not describe the committed state";

  if (!result.changed()) {
    EXPECT_EQ(result.mutation_kind, QuaderMutationKind::None)
        << contract.command_id;
    EXPECT_EQ(difference.changed_domains & kSemanticDomains, 0u)
        << contract.command_id << ": changed=false but state changed:\n"
        << difference.summary;
  } else {
    const bool opaque_change =
        external_calls_after != external_calls_before ||
        has_any(contract.observable_domains,
                ObservableDomain::Clipboard |
                    ObservableDomain::ExternalRuntime);
    EXPECT_NE(result.mutation_kind, QuaderMutationKind::None)
        << contract.command_id;
    EXPECT_TRUE((difference.changed_domains & kSemanticDomains) != 0u ||
                opaque_change)
        << contract.command_id
        << ": changed=true without a semantic or declared external change";
  }

  const std::optional<QuaderApiStatus> last_status =
      editor.commands().last_status();
  ASSERT_TRUE(last_status.has_value()) << contract.command_id;
  EXPECT_EQ(last_status->ok(), result.ok()) << contract.command_id;
  EXPECT_EQ(last_status->changed(), result.changed()) << contract.command_id;
  EXPECT_EQ(last_status->message, result.message) << contract.command_id;
  EXPECT_EQ(last_status->mutation_kind, result.mutation_kind)
      << contract.command_id;
}

[[nodiscard]] Json state_fingerprints(const CommandObservableState &state);
[[nodiscard]] Json revision_json(const QuaderRevisionStamp &revisions);

[[nodiscard]] Json validate_history_and_preview_contract(
    const CommandBehaviorContract &contract,
    CommandBehaviorFixture &fixture,
    const QuaderApiVoidResult &result) {
  Json lifecycle = Json::object();
  if (!result.ok() || !result.changed()) {
    return lifecycle;
  }

  QuaderEditorApi &editor = fixture.editor();
  const auto record_action = [&](std::string_view label,
                                 const CommandObservableState &action_before,
                                 const QuaderApiVoidResult &action_result,
                                 const CommandObservableState &action_after) {
    const CommandObservableStateDiff difference =
        diff_command_observable_state(action_before, action_after);
    lifecycle[std::string(label)] = {
        {"result",
         {{"ok", action_result.ok()},
          {"changed", action_result.changed()},
          {"mutation_kind",
           static_cast<int>(action_result.mutation_kind)},
          {"revisions", revision_json(action_result.revisions)}}},
        {"before", action_before.combined_fingerprint()},
        {"after", action_after.combined_fingerprint()},
        {"before_domains", state_fingerprints(action_before)},
        {"after_domains", state_fingerprints(action_after)},
        {"changed_domains", difference.changed_domains},
    };
  };
  const auto expect_auxiliary_laws = [&](std::string_view label,
                                         const CommandObservableState &action_before,
                                         const QuaderApiVoidResult &action_result,
                                         const CommandObservableState &action_after,
                                         bool must_change) {
    const CommandObservableStateDiff difference =
        diff_command_observable_state(action_before, action_after);
    const CommandObservableDomainMask allowed =
        observable_mask_for_contract(contract) |
        command_observable_domain_mask(CommandObservableDomain::History) |
        command_observable_domain_mask(CommandObservableDomain::Revisions);
    EXPECT_EQ(difference.changed_domains & ~allowed, 0u)
        << contract.command_id << ": " << label
        << " escaped its declared domains:\n" << difference.summary;
    EXPECT_TRUE(same_revisions(action_result.revisions, editor.revisions()))
        << contract.command_id << ": " << label;
    if (must_change) {
      EXPECT_TRUE(action_result.ok())
          << contract.command_id << ": " << label << ": "
          << action_result.error_message();
      EXPECT_TRUE(action_result.changed())
          << contract.command_id << ": " << label;
    }
    if (!action_result.ok() || !action_result.changed()) {
      EXPECT_EQ(action_result.mutation_kind, QuaderMutationKind::None)
          << contract.command_id << ": " << label;
      EXPECT_EQ(difference.changed_domains & kSemanticDomains, 0u)
          << contract.command_id << ": " << label
          << " reported no change but mutated semantic state:\n"
          << difference.summary;
    } else {
      EXPECT_NE(action_result.mutation_kind, QuaderMutationKind::None)
          << contract.command_id << ": " << label;
      EXPECT_NE(difference.changed_domains & kSemanticDomains, 0u)
          << contract.command_id << ": " << label
          << " reported a change without semantic state movement";
    }
  };
  const auto run_history_action = [&](std::string_view label,
                                      auto &&action,
                                      bool must_change) {
    const CommandObservableState action_before =
        capture_command_observable_state(editor);
    const QuaderApiVoidResult action_result = action();
    const CommandObservableState action_after =
        capture_command_observable_state(editor);
    expect_auxiliary_laws(label, action_before, action_result, action_after,
                          must_change);
    record_action(label, action_before, action_result, action_after);
    return action_result;
  };

  switch (contract.undo_repeat) {
  case CommandUndoRepeatPolicy::HistoryUndo: {
    (void)run_history_action("redo_after_command",
                             [&editor] { return editor.history().redo(); },
                             true);
    break;
  }
  case CommandUndoRepeatPolicy::HistoryRedo: {
    (void)run_history_action("undo_after_command",
                             [&editor] { return editor.history().undo(); },
                             true);
    break;
  }
  case CommandUndoRepeatPolicy::Undoable:
  case CommandUndoRepeatPolicy::UndoableAndRepeatable: {
    if (!editor.history().can_undo()) {
      ADD_FAILURE() << contract.command_id
                    << ": undoable command did not create undo history";
      break;
    }
    (void)run_history_action("undo",
                             [&editor] { return editor.history().undo(); },
                             true);
    (void)run_history_action("redo",
                             [&editor] { return editor.history().redo(); },
                             true);

    if (contract.undo_repeat ==
        CommandUndoRepeatPolicy::UndoableAndRepeatable) {
      const std::vector<EntityId> scope_before = selected_entities(editor);
      const CommandObservableState repeat_before =
          capture_command_observable_state(editor);
      const QuaderApiVoidResult repeated = editor.history().repeat_last_action();
      const CommandObservableState repeat_after =
          capture_command_observable_state(editor);
      expect_auxiliary_laws("repeat", repeat_before, repeated, repeat_after,
                            false);
      record_action("repeat", repeat_before, repeated, repeat_after);
      if (repeated.ok() && repeated.changed() &&
          !has_any(contract.observable_domains,
                   ObservableDomain::SceneGraph |
                       ObservableDomain::Instances)) {
        EXPECT_EQ(selected_entities(editor), scope_before)
            << contract.command_id
            << ": repeat broadened the participating object scope";
      }
    }
    break;
  }
  case CommandUndoRepeatPolicy::HistoryRepeat: {
    if (!editor.history().can_undo()) {
      ADD_FAILURE() << contract.command_id
                    << ": repeated command did not create undo history";
      break;
    }
    (void)run_history_action("undo",
                             [&editor] { return editor.history().undo(); },
                             true);
    (void)run_history_action("redo",
                             [&editor] { return editor.history().redo(); },
                             true);
    break;
  }
  case CommandUndoRepeatPolicy::BeginPreview:
    EXPECT_TRUE(editor.tools().active_settings().preview_active)
        << contract.command_id;
    break;
  case CommandUndoRepeatPolicy::CommitPreview:
  case CommandUndoRepeatPolicy::CancelPreview:
    EXPECT_FALSE(editor.tools().active_settings().preview_active)
        << contract.command_id;
    EXPECT_FALSE(editor.tools().has_active_primitive_creation())
        << contract.command_id;
    break;
  case CommandUndoRepeatPolicy::None:
    break;
  }
  return lifecycle;
}

[[nodiscard]] Json state_fingerprints(
    const CommandObservableState &state) {
  Json result = Json::object();
  for (const CommandObservableDomain domain : kObservableDomains) {
    result[std::string(command_observable_domain_name(domain))] =
        state.domain(domain).fingerprint;
  }
  return result;
}

[[nodiscard]] Json revision_json(const QuaderRevisionStamp &revisions) {
  return {
      {"project", revisions.project_revision},
      {"scene", revisions.scene_revision},
      {"scene_layer", revisions.scene_layer_revision},
      {"assets", revisions.asset_revision},
      {"modeling_content", revisions.modeling_content_revision},
      {"modeling_selection", revisions.modeling_selection_revision},
  };
}

[[nodiscard]] Json observe_command(const CommandBehaviorContract &contract) {
  std::unique_ptr<CommandBehaviorFixture> fixture =
      CommandBehaviorFixture::create(contract);
  QuaderEditorApi &editor = fixture->editor();

  const std::optional<QuaderCommandDescriptor> descriptor =
      editor.commands().descriptor(contract.command_id);
  if (!descriptor.has_value()) {
    throw std::runtime_error("Missing registered command: " +
                             std::string(contract.command_id));
  }

  const CommandObservableState before =
      capture_command_observable_state(editor);
  const std::uint64_t external_before = fixture->external_call_count();
  const QuaderApiVoidResult result =
      editor.commands().execute(contract.command_id);
  const std::uint64_t external_after = fixture->external_call_count();
  const CommandObservableState after =
      capture_command_observable_state(editor);
  const CommandObservableStateDiff difference =
      diff_command_observable_state(before, after);

  expect_global_command_laws(contract, *descriptor, before, after,
                             external_before, external_after, result, editor,
                             true);

  Json observation{
      {"fixture", static_cast<int>(contract.fixture)},
      {"allowed_domains",
       static_cast<std::uint64_t>(contract.observable_domains)},
      {"outcome", static_cast<int>(contract.outcome)},
      {"undo_repeat", static_cast<int>(contract.undo_repeat)},
      {"flags", static_cast<int>(contract.flags)},
      {"descriptor",
       {{"shortcut", descriptor->shortcut},
        {"enabled", descriptor->enabled}}},
      {"result",
       {{"ok", result.ok()},
        {"changed", result.changed()},
        {"mutation_kind", static_cast<int>(result.mutation_kind)},
        {"revisions", revision_json(result.revisions)}}},
      {"before", before.combined_fingerprint()},
      {"after", after.combined_fingerprint()},
      {"before_domains", state_fingerprints(before)},
      {"after_domains", state_fingerprints(after)},
      {"changed_domains", difference.changed_domains},
      {"external_calls_before", external_before},
      {"external_calls_after", external_after},
  };

  observation["lifecycle"] = validate_history_and_preview_contract(
      contract, *fixture, result);
  return observation;
}

[[nodiscard]] std::filesystem::path behavior_baseline_path() {
  return std::filesystem::path(QUADER_SOURCE_DIR) / "tests" / "baselines" /
         "command_behavior.json";
}

[[nodiscard]] bool baseline_update_requested() {
  const char *value =
      std::getenv("QUADER_UPDATE_COMMAND_BEHAVIOR_BASELINE");
  return value != nullptr && std::string_view(value) == "1";
}

[[nodiscard]] std::string required_update_reason() {
  const char *reason =
      std::getenv("QUADER_COMMAND_BEHAVIOR_CHANGE_REASON");
  if (reason == nullptr) {
    throw std::runtime_error(
        "Baseline updates require QUADER_COMMAND_BEHAVIOR_CHANGE_REASON.");
  }
  std::string value(reason);
  const auto not_space = [](unsigned char character) {
    return !std::isspace(character);
  };
  const auto first = std::find_if(value.begin(), value.end(), not_space);
  const auto last = std::find_if(value.rbegin(), value.rend(), not_space).base();
  if (first >= last) {
    throw std::runtime_error(
        "Baseline updates require a non-blank behavior change reason.");
  }
  return std::string(first, last);
}

void write_baseline(const Json &baseline) {
  const std::filesystem::path path = behavior_baseline_path();
  std::filesystem::create_directories(path.parent_path());
  std::filesystem::path temporary = path;
  temporary += ".tmp";
  std::error_code ignored;
  std::filesystem::remove(temporary, ignored);
  {
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
      throw std::runtime_error("Could not write " + temporary.string());
    }
    output << baseline.dump(2) << '\n';
    if (!output) {
      throw std::runtime_error("Could not finish writing " +
                               temporary.string());
    }
  }

  const bool replaced = std::filesystem::exists(path)
                            ? ReplaceFileW(path.c_str(), temporary.c_str(),
                                           nullptr, REPLACEFILE_WRITE_THROUGH,
                                           nullptr, nullptr) != FALSE
                            : MoveFileExW(temporary.c_str(), path.c_str(),
                                          MOVEFILE_WRITE_THROUGH) != FALSE;
  if (!replaced) {
    const DWORD error = GetLastError();
    std::filesystem::remove(temporary, ignored);
    throw std::runtime_error("Could not atomically replace " + path.string() +
                             " (Win32 error " +
                             std::to_string(error) + ").");
  }
}

[[nodiscard]] Json read_baseline() {
  const std::filesystem::path path = behavior_baseline_path();
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error(
        "Missing approved command behavior baseline. Run "
        "tools/update_command_behavior_baseline.ps1 -Reason <reason>.");
  }
  return Json::parse(input);
}

} // namespace

TEST(CommandBehaviorContractTests,
     EveryRegisteredCommandHasExactlyOneExplicitContract) {
  const CommandBehaviorContract *fixture_contract =
      find_command_behavior_contract(quader_command::kSelectAll.string());
  ASSERT_NE(fixture_contract, nullptr);
  std::unique_ptr<CommandBehaviorFixture> fixture =
      CommandBehaviorFixture::create(*fixture_contract);
  QuaderEditorApi &editor = fixture->editor();

  std::set<std::string> registered;
  for (const QuaderCommandDescriptor &descriptor :
       editor.commands().available()) {
    EXPECT_TRUE(registered.insert(descriptor.id).second)
        << "Duplicate registered command: " << descriptor.id;
  }

  std::set<std::string> contracted;
  for (const CommandBehaviorContract &contract :
       command_behavior_contracts()) {
    EXPECT_TRUE(contracted.insert(std::string(contract.command_id)).second)
        << "Duplicate behavior contract: " << contract.command_id;
    EXPECT_NE(contract.observable_domains, ObservableDomain::None)
        << contract.command_id;
  }

  EXPECT_EQ(contracted, registered)
      << "The registry and explicit behavior-contract catalog diverged.";
#ifndef NDEBUG
  EXPECT_EQ(contracted.size(), 219u);
#else
  EXPECT_EQ(contracted.size(), 218u);
#endif
}

TEST(CommandBehaviorContractTests,
     PathArrayActivationUsesANondegenerateCurveFixture) {
  const CommandBehaviorContract *contract = find_command_behavior_contract(
      quader_command::kToolsActivatePathArray.string());
  ASSERT_NE(contract, nullptr);
  EXPECT_EQ(contract->fixture, CanonicalCommandFixture::CurveSelection);
  EXPECT_EQ(contract->outcome, CommandOutcomePolicy::SuccessRequired);
  EXPECT_EQ(contract->undo_repeat, CommandUndoRepeatPolicy::None);
}

TEST(CommandBehaviorContractTests,
     VSyncFingerprintChangesOnlyPreferencesAndRestoresExactDefaultSnapshot) {
  const CommandBehaviorContract *contract =
      find_command_behavior_contract(quader_command::kSelectAll.string());
  ASSERT_NE(contract, nullptr);
  auto fixture = CommandBehaviorFixture::create(*contract);
  QuaderEditorApi &editor = fixture->editor();
  ASSERT_TRUE(editor.preferences().vsync_enabled());
  const CommandObservableState before =
      capture_command_observable_state(editor);
  EXPECT_EQ(before.domain(CommandObservableDomain::Preferences)
                .canonical.find("preferences.vsync_enabled"),
            std::string::npos)
      << "absent VSync encodes the fixed true default in approved baselines";

  const QuaderApiVoidResult disabled =
      editor.preferences().set_vsync_enabled(false);
  ASSERT_TRUE(disabled.ok()) << disabled.error_message();
  ASSERT_TRUE(disabled.changed());
  EXPECT_FALSE(editor.preferences().vsync_enabled());
  const CommandObservableState after = capture_command_observable_state(editor);
  const CommandObservableStateDiff difference =
      diff_command_observable_state(before, after);
  EXPECT_EQ(difference.changed_domains,
            command_observable_domain_mask(CommandObservableDomain::Preferences))
      << difference.summary;
  EXPECT_NE(after.domain(CommandObservableDomain::Preferences)
                .canonical.find("preferences.vsync_enabled=false\n"),
            std::string::npos);

  const QuaderApiVoidResult unchanged =
      editor.preferences().set_vsync_enabled(false);
  ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
  EXPECT_FALSE(unchanged.changed());
  EXPECT_EQ(after, capture_command_observable_state(editor));

  const QuaderApiVoidResult restored =
      editor.preferences().set_vsync_enabled(true);
  ASSERT_TRUE(restored.ok()) << restored.error_message();
  ASSERT_TRUE(restored.changed());
  EXPECT_TRUE(editor.preferences().vsync_enabled());
  EXPECT_EQ(before, capture_command_observable_state(editor));
}

TEST(CommandBehaviorContractTests,
     SessionPreferenceFingerprintsDetectEachFieldAndRestoreExactDefaults) {
  const CommandBehaviorContract *contract =
      find_command_behavior_contract(quader_command::kSelectAll.string());
  ASSERT_NE(contract, nullptr);
  auto fixture = CommandBehaviorFixture::create(*contract);
  QuaderEditorApi &editor = fixture->editor();
  const SessionPreferences defaults;
  ASSERT_EQ(editor.preferences().session_settings(), defaults);
  const CommandObservableState before =
      capture_command_observable_state(editor);
  EXPECT_EQ(before.domain(CommandObservableDomain::Preferences)
                .canonical.find("preferences.session."),
            std::string::npos);

  struct PreferenceChange {
    SessionPreferences value;
    std::string_view expected_line;
  };
  const std::array changes{
      PreferenceChange{
          {.undo_steps = 0}, "preferences.session.undo_steps=0\n"},
      PreferenceChange{
          {.autosave_enabled = false},
          "preferences.session.autosave_enabled=false\n"},
      PreferenceChange{
          {.autosave_interval_minutes = 10},
          "preferences.session.autosave_interval_minutes=10\n"},
      PreferenceChange{
          {.autosave_max_files = 7},
          "preferences.session.autosave_max_files=7\n"},
  };
  for (const PreferenceChange &change : changes) {
    SCOPED_TRACE(change.expected_line);
    const QuaderApiVoidResult changed =
        editor.preferences().set_session_settings(change.value);
    ASSERT_TRUE(changed.ok()) << changed.error_message();
    ASSERT_TRUE(changed.changed());
    EXPECT_EQ(editor.preferences().session_settings(), change.value);
    const CommandObservableState after =
        capture_command_observable_state(editor);
    const CommandObservableStateDiff difference =
        diff_command_observable_state(before, after);
    EXPECT_EQ(
        difference.changed_domains,
        command_observable_domain_mask(CommandObservableDomain::Preferences))
        << difference.summary;
    EXPECT_NE(after.domain(CommandObservableDomain::Preferences)
                  .canonical.find(change.expected_line),
              std::string::npos);

    const QuaderApiVoidResult unchanged =
        editor.preferences().set_session_settings(change.value);
    ASSERT_TRUE(unchanged.ok()) << unchanged.error_message();
    EXPECT_FALSE(unchanged.changed());
    EXPECT_EQ(after, capture_command_observable_state(editor));

    const QuaderApiVoidResult restored =
        editor.preferences().set_session_settings(defaults);
    ASSERT_TRUE(restored.ok()) << restored.error_message();
    ASSERT_TRUE(restored.changed());
    EXPECT_EQ(before, capture_command_observable_state(editor));
  }

  const QuaderApiVoidResult invalid = editor.preferences().set_session_settings(
      SessionPreferences{.undo_steps = -1, .autosave_enabled = false});
  EXPECT_FALSE(invalid.ok());
  EXPECT_FALSE(invalid.changed());
  EXPECT_EQ(before, capture_command_observable_state(editor));
}

TEST(CommandBehaviorContractTests,
     CurveFingerprintsCaptureAuthoredHandlesClosureAndLivePreviews) {
  const auto *contract =
      find_command_behavior_contract(quader_command::kSelectAll.string());
  ASSERT_NE(contract, nullptr);
  auto fixture = CommandBehaviorFixture::create(*contract);
  QuaderEditorApi &editor = fixture->editor();
  const auto created = editor.modeling().create_curve({
      .points = {{.position = {0.0F, 0.0F, 0.0F},
                  .outgoing_handle = {1.0F, 0.0F, 0.0F}},
                 {.position = {4.0F, 0.0F, 0.0F},
                  .incoming_handle = {-1.0F, 0.0F, 0.0F}}},
      .name = "Fingerprint Curve",
  });
  ASSERT_TRUE(created.ok()) << created.error_message();
  const auto snapshot = editor.modeling().curve_snapshot(created.value());
  ASSERT_TRUE(snapshot.ok()) << snapshot.error_message();
  const auto before = capture_command_observable_state(editor);
  auto candidate = snapshot.value().document;
  const auto anchor = candidate.points().front();
  ASSERT_TRUE(candidate.set_point(anchor.id, {
      .position = anchor.position,
      .incoming_handle = anchor.incoming_handle,
      .outgoing_handle = {1.0F, 2.0F, 0.0F},
  }).ok());
  ASSERT_TRUE(editor.modeling().begin_curve_preview(created.value()).ok());
  ASSERT_TRUE(editor.modeling().update_curve_preview(candidate).ok());
  const auto preview = capture_command_observable_state(editor);
  EXPECT_NE(before.domain(CommandObservableDomain::Geometry),
            preview.domain(CommandObservableDomain::Geometry));
  ASSERT_TRUE(editor.modeling().cancel_curve_preview().ok());
  EXPECT_EQ(before.domain(CommandObservableDomain::Geometry),
            capture_command_observable_state(editor).domain(
                CommandObservableDomain::Geometry));

  ASSERT_TRUE(editor.modeling().set_curve_closed(created.value(), true).ok());
  const auto closed = capture_command_observable_state(editor);
  EXPECT_NE(before.domain(CommandObservableDomain::Geometry),
            closed.domain(CommandObservableDomain::Geometry));
  ASSERT_TRUE(editor.history().undo().ok());
  EXPECT_EQ(before.domain(CommandObservableDomain::Geometry),
            capture_command_observable_state(editor).domain(
                CommandObservableDomain::Geometry));
}

TEST(CommandBehaviorContractTests,
     CreateOuterHullUsesEveryFaceOfTheSelectedObjectForItsPreview) {
  const CommandBehaviorContract *contract = find_command_behavior_contract(
      quader_command::kModelingCreateOuterHull.string());
  ASSERT_NE(contract, nullptr);
  std::unique_ptr<CommandBehaviorFixture> fixture =
      CommandBehaviorFixture::create(*contract);
  QuaderEditorApi &editor = fixture->editor();

  const CommandObservableState before =
      capture_command_observable_state(editor);
  const QuaderApiVoidResult begun =
      editor.commands().execute(quader_command::kModelingCreateOuterHull);
  ASSERT_TRUE(begun.ok()) << begun.error_message();
  ASSERT_TRUE(begun.changed());
  EXPECT_TRUE(editor.modeling().can_cancel_active_tool_or_preview());

  const CommandObservableState preview =
      capture_command_observable_state(editor);
  const CommandObservableStateDiff preview_change =
      diff_command_observable_state(before, preview);
  EXPECT_TRUE(preview_change.changed(CommandObservableDomain::Geometry));
  EXPECT_TRUE(preview_change.changed(CommandObservableDomain::Tools));

  const QuaderApiVoidResult canceled =
      editor.commands().execute(quader_command::kModelingCancelActivePreview);
  ASSERT_TRUE(canceled.ok()) << canceled.error_message();
  ASSERT_TRUE(canceled.changed());
  const CommandObservableState restored =
      capture_command_observable_state(editor);
  EXPECT_EQ(before.domain(CommandObservableDomain::Geometry),
            restored.domain(CommandObservableDomain::Geometry));
  EXPECT_EQ(before.domain(CommandObservableDomain::Selection),
            restored.domain(CommandObservableDomain::Selection));
}

TEST(CommandBehaviorContractTests,
     DefaultEdgeNormalsRestoresAuthoredHardStateThroughHistory) {
  const CommandBehaviorContract *contract = find_command_behavior_contract(
      quader_command::kModelingDefaultEdgeNormals.string());
  ASSERT_NE(contract, nullptr);
  EXPECT_EQ(contract->fixture,
            CanonicalCommandFixture::AuthoredHardEdgeSelection);
  EXPECT_EQ(contract->outcome, CommandOutcomePolicy::ChangeRequired);
  EXPECT_EQ(contract->undo_repeat,
            CommandUndoRepeatPolicy::UndoableAndRepeatable);

  std::unique_ptr<CommandBehaviorFixture> fixture =
      CommandBehaviorFixture::create(*contract);
  QuaderEditorApi &editor = fixture->editor();
  const QuaderApiVoidResult already_hard = editor.commands().execute(
      quader_command::kModelingHardenEdgeNormals);
  ASSERT_TRUE(already_hard.ok()) << already_hard.error_message();
  ASSERT_FALSE(already_hard.changed());

  const CommandObservableState before =
      capture_command_observable_state(editor);
  const QuaderApiVoidResult reset = editor.commands().execute(
      quader_command::kModelingDefaultEdgeNormals);
  ASSERT_TRUE(reset.ok()) << reset.error_message();
  ASSERT_TRUE(reset.changed());
  const QuaderApiVoidResult already_default = editor.commands().execute(
      quader_command::kModelingDefaultEdgeNormals);
  ASSERT_TRUE(already_default.ok()) << already_default.error_message();
  ASSERT_FALSE(already_default.changed());
  const CommandObservableState defaulted =
      capture_command_observable_state(editor);
  EXPECT_EQ(before.domain(CommandObservableDomain::Geometry),
            defaulted.domain(CommandObservableDomain::Geometry));

  const QuaderApiVoidResult undone = editor.history().undo();
  ASSERT_TRUE(undone.ok()) << undone.error_message();
  ASSERT_TRUE(undone.changed());
  const QuaderApiVoidResult restored_hard = editor.commands().execute(
      quader_command::kModelingHardenEdgeNormals);
  ASSERT_TRUE(restored_hard.ok()) << restored_hard.error_message();
  ASSERT_FALSE(restored_hard.changed());
  EXPECT_EQ(before.domain(CommandObservableDomain::Geometry),
            capture_command_observable_state(editor).domain(
                CommandObservableDomain::Geometry));

  const QuaderApiVoidResult redone = editor.history().redo();
  ASSERT_TRUE(redone.ok()) << redone.error_message();
  ASSERT_TRUE(redone.changed());
  const QuaderApiVoidResult restored_default = editor.commands().execute(
      quader_command::kModelingDefaultEdgeNormals);
  ASSERT_TRUE(restored_default.ok()) << restored_default.error_message();
  ASSERT_FALSE(restored_default.changed());
  EXPECT_EQ(defaulted.domain(CommandObservableDomain::Geometry),
            capture_command_observable_state(editor).domain(
                CommandObservableDomain::Geometry));

  const QuaderApiVoidResult repeated = editor.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  EXPECT_FALSE(repeated.changed());
  const QuaderApiVoidResult remains_default = editor.commands().execute(
      quader_command::kModelingDefaultEdgeNormals);
  ASSERT_TRUE(remains_default.ok()) << remains_default.error_message();
  EXPECT_FALSE(remains_default.changed());
}

TEST(CommandBehaviorContractTests,
     UvPeelActivationPreviewsBeforeOneCommitWithExactHistoryAndStickyRepeat) {
  const CommandBehaviorContract *contract =
      find_command_behavior_contract(quader_command::kModelingUvPeel.string());
  ASSERT_NE(contract, nullptr);
  EXPECT_EQ(contract->fixture, CanonicalCommandFixture::UvPeelEdgeSelection);
  EXPECT_EQ(contract->outcome, CommandOutcomePolicy::ChangeRequired);
  EXPECT_EQ(contract->undo_repeat,
            CommandUndoRepeatPolicy::BeginPreview);
  std::unique_ptr<CommandBehaviorFixture> fixture =
      CommandBehaviorFixture::create(*contract);
  QuaderEditorApi &editor = fixture->editor();
  const auto records = editor.query().modeling_objects();
  ASSERT_EQ(records.size(), 1u);
  const auto object =
      editor.query().modeling_object_for_id(records.front().modeling_object_id);
  ASSERT_TRUE(object.has_value());
  const auto mesh_before = editor.query().compiled_object_mesh(*object);
  ASSERT_TRUE(mesh_before.has_value());
  ASSERT_TRUE(mesh_before->has_authored_polygon_document);
  ASSERT_EQ(mesh_before->face_textures.size(), 1u);
  ASSERT_EQ(editor.selection().summary().selected_edge_count, 1u);

  const auto descriptor =
      editor.commands().descriptor(quader_command::kModelingUvPeel);
  ASSERT_TRUE(descriptor.has_value());
  ASSERT_TRUE(descriptor->enabled);
  const auto before = capture_command_observable_state(editor);
  const auto external_before = fixture->external_call_count();
  const auto undo_count_before = fixture->native_scene().history().undo_count();
  const auto result = editor.commands().execute(quader_command::kModelingUvPeel);
  ASSERT_TRUE(result.ok()) << result.error_message();
  ASSERT_TRUE(result.changed());
  const auto after = capture_command_observable_state(editor);
  expect_global_command_laws(*contract, *descriptor, before, after,
                            external_before, fixture->external_call_count(),
                            result, editor, true);
  EXPECT_EQ(editor.tools().active_tool(), "uv_peel");
  EXPECT_TRUE(editor.tools().active_settings().preview_active);
  EXPECT_EQ(fixture->native_scene().history().undo_count(), undo_count_before);
  EXPECT_EQ(before.domain(CommandObservableDomain::Selection),
            after.domain(CommandObservableDomain::Selection));
  EXPECT_EQ(before.domain(CommandObservableDomain::Scene),
            after.domain(CommandObservableDomain::Scene));
  EXPECT_EQ(before.domain(CommandObservableDomain::Preferences),
            after.domain(CommandObservableDomain::Preferences));

  const auto mesh_after = editor.query().compiled_object_mesh(*object);
  ASSERT_TRUE(mesh_after.has_value());
  EXPECT_EQ(mesh_after->indices, mesh_before->indices);
  ASSERT_EQ(mesh_after->vertices.size(), mesh_before->vertices.size());
  bool uv_changed = false;
  for (std::size_t index = 0; index < mesh_before->vertices.size(); ++index) {
    const auto &original = mesh_before->vertices[index];
    const auto &peeled = mesh_after->vertices[index];
    EXPECT_FLOAT_EQ(peeled.position.x, original.position.x);
    EXPECT_FLOAT_EQ(peeled.position.y, original.position.y);
    EXPECT_FLOAT_EQ(peeled.position.z, original.position.z);
    uv_changed = uv_changed || peeled.uv.x != original.uv.x ||
                 peeled.uv.y != original.uv.y;
  }
  EXPECT_TRUE(uv_changed);
  ASSERT_EQ(mesh_after->primitives.size(), mesh_before->primitives.size());
  for (std::size_t index = 0; index < mesh_before->primitives.size(); ++index) {
    const auto &original = mesh_before->primitives[index];
    const auto &peeled = mesh_after->primitives[index];
    EXPECT_EQ(peeled.index_offset, original.index_offset);
    EXPECT_EQ(peeled.index_count, original.index_count);
    EXPECT_EQ(peeled.material_slot, original.material_slot);
  }

  const auto applied = editor.tools().commit_active_tool_preview();
  ASSERT_TRUE(applied.ok()) << applied.error_message();
  ASSERT_TRUE(applied.changed());
  EXPECT_EQ(editor.tools().active_tool(), "select");
  EXPECT_EQ(fixture->native_scene().history().undo_count(), undo_count_before + 1u);

  const QuaderUvPeelOptions changed_options{
      .axis = QuaderUvPeelAxis::V,
      .world_space = false,
      .u_repeat = 2.0,
      .v_repeat = 3.0,
      .u_offset = 0.125,
      .v_offset = -0.25,
  };
  ASSERT_TRUE(editor.modeling().set_uv_peel_options(changed_options).ok());
  const auto expect_mesh_state = [&](const CommandObservableState &expected) {
    const auto actual = capture_command_observable_state(editor);
    for (const auto domain : {CommandObservableDomain::Geometry,
                             CommandObservableDomain::Selection,
                             CommandObservableDomain::Assets}) {
      EXPECT_EQ(expected.domain(domain), actual.domain(domain))
          << command_observable_domain_name(domain);
    }
    EXPECT_EQ(editor.modeling().uv_peel_settings().options, changed_options);
  };
  ASSERT_TRUE(editor.history().undo().ok());
  expect_mesh_state(before);
  const auto repeated = editor.history().repeat_last_action();
  ASSERT_TRUE(repeated.ok()) << repeated.error_message();
  ASSERT_TRUE(repeated.changed());
  expect_mesh_state(after);
  ASSERT_TRUE(editor.history().undo().ok());
  expect_mesh_state(before);
  ASSERT_TRUE(editor.history().redo().ok());
  expect_mesh_state(after);
}

TEST(CommandBehaviorContractTests, ApprovedFingerprintCoversEveryCommand) {
  Json commands = Json::object();
  for (const CommandBehaviorContract &contract :
       command_behavior_contracts()) {
    SCOPED_TRACE(contract.command_id);
    try {
      commands[std::string(contract.command_id)] = observe_command(contract);
    } catch (const std::exception &error) {
      ADD_FAILURE() << contract.command_id << ": " << error.what();
    }
  }

  Json generated{
      {"schema", 1},
      {"command_count", commands.size()},
      {"commands", std::move(commands)},
  };

  if (baseline_update_requested()) {
    const std::string reason = required_update_reason();
    ASSERT_FALSE(::testing::Test::HasFailure())
        << "Refusing to approve fingerprints while behavior laws fail.";
    generated["approved_change_reason"] = reason;
    write_baseline(generated);
    return;
  }

  Json approved;
  ASSERT_NO_THROW(approved = read_baseline());
  ASSERT_EQ(approved.value("schema", 0), 1);
  ASSERT_FALSE(approved.value("approved_change_reason", "").empty());
  ASSERT_TRUE(approved.contains("commands"));
  ASSERT_EQ(approved.at("commands").size(), generated.at("commands").size());

  for (const CommandBehaviorContract &contract :
       command_behavior_contracts()) {
    const std::string id(contract.command_id);
    ASSERT_TRUE(approved.at("commands").contains(id)) << id;
    ASSERT_TRUE(generated.at("commands").contains(id)) << id;
    EXPECT_EQ(approved.at("commands").at(id),
              generated.at("commands").at(id))
        << id << " changed from its explicitly approved behavior.\nActual:\n"
        << generated.at("commands").at(id).dump(2);
  }
}

TEST(CommandBehaviorContractTests,
     SeededCommandSequencesPreserveEveryGlobalMutationLaw) {
  constexpr std::array<std::string_view, 29> kSequenceCommands{
      "edit.clear_selection",
      "select.all",
      "select.inverse",
      "viewport.view.perspective",
      "viewport.view.top",
      "viewport.view.bottom",
      "viewport.view.left",
      "viewport.view.right",
      "viewport.view.front",
      "viewport.view.back",
      "viewport.shading.wireframe",
      "viewport.shading.solid",
      "viewport.shading.shaded",
      "viewport.shading.rendered",
      "viewport.layout.single",
      "viewport.layout.two_vertical",
      "viewport.layout.two_horizontal",
      "viewport.layout.four_quad",
      "viewport.grid.increment",
      "viewport.grid.decrement",
      "viewport.grid.reset",
      "viewport.toggle_grid",
      "viewport.toggle_gizmos",
      "viewport.toggle_mesh_grid",
      "viewport.toggle_overlays",
      "tools.activate.select",
      "tools.activate.move",
      "edit.undo",
      "edit.redo",
  };
  constexpr std::array<std::uint32_t, 4> kSeeds{
      0x0000C0DEu, 0x5E1EC710u, 0xA11C0DEu, 0x51A7E123u};

  for (const std::uint32_t seed : kSeeds) {
    SCOPED_TRACE(::testing::Message() << "replay seed=" << seed);
    const CommandBehaviorContract sequence_fixture{
        .command_id = quader_command::kSelectAll.string(),
        .fixture = CanonicalCommandFixture::ObjectSetSelection,
        .observable_domains = ObservableDomain::Document,
    };
    std::unique_ptr<CommandBehaviorFixture> fixture =
        CommandBehaviorFixture::create(sequence_fixture);
    QuaderEditorApi &editor = fixture->editor();
    std::mt19937 random(seed);

    for (int step = 0; step < 160; ++step) {
      const std::string_view command_id =
          kSequenceCommands[random() % kSequenceCommands.size()];
      SCOPED_TRACE(::testing::Message()
                   << "step=" << step << " command=" << command_id);
      const CommandBehaviorContract *contract =
          find_command_behavior_contract(command_id);
      ASSERT_NE(contract, nullptr);
      const std::optional<QuaderCommandDescriptor> descriptor =
          editor.commands().descriptor(command_id);
      ASSERT_TRUE(descriptor.has_value());

      const CommandObservableState before =
          capture_command_observable_state(editor);
      const std::uint64_t calls_before = fixture->external_call_count();
      const QuaderApiVoidResult result =
          editor.commands().execute(command_id);
      const std::uint64_t calls_after = fixture->external_call_count();
      const CommandObservableState after =
          capture_command_observable_state(editor);

      if (!descriptor->enabled) {
        EXPECT_FALSE(result.ok());
        EXPECT_FALSE(result.changed());
        EXPECT_TRUE(diff_command_observable_state(before, after).empty());
        EXPECT_TRUE(same_revisions(result.revisions, editor.revisions()));
        continue;
      }
      expect_global_command_laws(*contract, *descriptor, before, after,
                                 calls_before, calls_after, result, editor,
                                 false);
    }
  }
}

} // namespace quader::test
