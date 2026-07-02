////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_keybinding_types.hpp"
#include "editor/input/quader_input_binding.hpp"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace quader {

/**
 * Stores boolean context keys used by keybinding when-expressions.
 */
struct QuaderKeybindingContext {
    std::unordered_map<std::string, bool> values;

    void set(std::string key, bool value) {
        values[std::move(key)] = value;
    }

    [[nodiscard]] bool get(std::string_view key) const {
        const auto found = values.find(std::string(key));
        return found != values.end() && found->second;
    }
};

/**
 * Stores the result of one keybinding dispatch step.
 */
struct QuaderKeybindingDispatch {
    /**
     * Enumerates keybinding dispatch result kinds used by the editor input layer.
     */
    enum class Kind {
        NoMatch,
        PendingChord,
        ExactMatch,
    };

    Kind kind = Kind::NoMatch;
    std::string target_id;
    std::string shortcut;
};

/**
 * Stores the outcome of a user keybinding mutation.
 */
struct QuaderKeybindingMutationResult {
    bool ok = true;
    bool changed = false;
    std::string message;
};

namespace keybinding_detail {

inline std::string trim_copy(std::string_view text) {
  std::size_t first = 0u;
  while (first < text.size() &&
         std::isspace(static_cast<unsigned char>(text[first])) != 0) {
    ++first;
  }
  std::size_t last = text.size();
  while (last > first &&
         std::isspace(static_cast<unsigned char>(text[last - 1u])) != 0) {
    --last;
  }
  return std::string(text.substr(first, last - first));
}

inline std::string lower_copy(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::tolower(value));
                 });
  return result;
}

inline std::string upper_copy(std::string_view text) {
  std::string result(text);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char value) {
                   return static_cast<char>(std::toupper(value));
                 });
  return result;
}

inline std::vector<std::string> split_copy(std::string_view text,
                                           char delimiter) {
  std::vector<std::string> parts;
  std::size_t start = 0u;
  while (start <= text.size()) {
    const std::size_t end = text.find(delimiter, start);
    const std::size_t count =
        end == std::string_view::npos ? text.size() - start : end - start;
    parts.push_back(trim_copy(text.substr(start, count)));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1u;
  }
  return parts;
}

inline std::string canonical_key_token(std::string_view token) {
  const std::string trimmed = trim_copy(token);
  if (trimmed.empty()) {
    return {};
  }

  const std::string lower = lower_copy(trimmed);
  if (lower.size() == 1u &&
      std::isalpha(static_cast<unsigned char>(lower.front())) != 0) {
    return upper_copy(lower);
  }
  if (lower.size() == 1u &&
      std::isdigit(static_cast<unsigned char>(lower.front())) != 0) {
    return lower;
  }
  if (lower == "esc") {
    return "Escape";
  }
  if (lower == "del") {
    return "Delete";
  }
  if (lower == "return") {
    return "Enter";
  }
  if (lower == "spacebar") {
    return "Space";
  }
  if (lower == "pgup") {
    return "PageUp";
  }
  if (lower == "pgdn") {
    return "PageDown";
  }
  if (lower == "keypad1" || lower == "numpad1") {
    return "Keypad1";
  }
  if (lower == "keypad2" || lower == "numpad2") {
    return "Keypad2";
  }
  if (lower == "keypad3" || lower == "numpad3") {
    return "Keypad3";
  }
  if (lower == "keypad4" || lower == "numpad4") {
    return "Keypad4";
  }
  if (lower == "keypad5" || lower == "numpad5") {
    return "Keypad5";
  }
  if (lower.size() >= 2u && lower.front() == 'f') {
    bool numeric = true;
    for (std::size_t index = 1u; index < lower.size(); ++index) {
      numeric = numeric &&
                std::isdigit(static_cast<unsigned char>(lower[index])) != 0;
    }
    if (numeric) {
      return upper_copy(lower);
    }
  }

  static const std::unordered_map<std::string, std::string> kNamedKeys{
      {"backspace", "Backspace"},
      {"delete", "Delete"},
      {"home", "Home"},
      {"end", "End"},
      {"pageup", "PageUp"},
      {"pagedown", "PageDown"},
      {"space", "Space"},
      {"tab", "Tab"},
      {"enter", "Enter"},
      {"escape", "Escape"},
      {"left", "Left"},
      {"right", "Right"},
      {"up", "Up"},
      {"down", "Down"},
  };
  const auto named = kNamedKeys.find(lower);
  return named == kNamedKeys.end() ? trimmed : named->second;
}

inline bool parse_chord(std::string_view text, QuaderInputChord &chord) {
  const std::optional<QuaderInputChord> parsed = quader_input_parse_chord(text);
  if (!parsed) {
    chord = {};
    return false;
  }
  chord = *parsed;
  return true;
}

inline std::optional<std::vector<QuaderInputChord>>
parse_shortcut(std::string_view shortcut) {
  return quader_input_parse_shortcut(shortcut);
}

inline std::string format_chord(const QuaderInputChord &chord) {
  return quader_input_format_chord(chord);
}

inline std::string
format_shortcut(const std::vector<QuaderInputChord> &sequence) {
  return quader_input_format_shortcut(sequence);
}

/**
 * Evaluates keybinding when-expressions against a boolean context map.
 */
class WhenParser final {
public:
    WhenParser(std::string_view expression, const QuaderKeybindingContext& context)
        : expression_(expression)
        , context_(context) {}

    bool evaluate() {
      skip_whitespace();
      if (at_end()) {
        return true;
      }
      const bool value = parse_or();
      skip_whitespace();
      return failed_ || !at_end() ? false : value;
    }

private:
  bool parse_or() {
    bool value = parse_and();
    while (consume("||")) {
      value = parse_and() || value;
    }
    return value;
  }

  bool parse_and() {
    bool value = parse_unary();
    while (consume("&&")) {
      value = parse_unary() && value;
    }
    return value;
  }

  bool parse_unary() {
    if (consume("!")) {
      return !parse_unary();
    }
    return parse_primary();
  }

  bool parse_primary() {
    skip_whitespace();
    if (consume("(")) {
      const bool value = parse_or();
      if (!consume(")")) {
        failed_ = true;
        return false;
      }
      return value;
    }

    const std::string identifier = parse_identifier();
    if (identifier.empty()) {
      failed_ = true;
      return false;
    }
    if (identifier == "true") {
      return true;
    }
    if (identifier == "false") {
      return false;
    }
    return context_.get(identifier);
  }

  std::string parse_identifier() {
    skip_whitespace();
    const std::size_t start = offset_;
    while (!at_end()) {
      const char value = expression_[offset_];
      const bool allowed =
          std::isalnum(static_cast<unsigned char>(value)) != 0 ||
          value == '_' || value == '.' || value == '-';
      if (!allowed) {
        break;
      }
      ++offset_;
    }
    return std::string(expression_.substr(start, offset_ - start));
  }

    bool consume(std::string_view token) {
      skip_whitespace();
      if (expression_.substr(offset_, token.size()) != token) {
        return false;
      }
        offset_ += token.size();
        return true;
    }

    void skip_whitespace() {
      while (!at_end() && std::isspace(static_cast<unsigned char>(
                              expression_[offset_])) != 0) {
        ++offset_;
      }
    }

    [[nodiscard]] bool at_end() const { return offset_ >= expression_.size(); }

    std::string_view expression_;
    const QuaderKeybindingContext& context_;
    std::size_t offset_ = 0u;
    bool failed_ = false;
};

inline bool when_matches(std::string_view expression,
                         const QuaderKeybindingContext &context) {
  return WhenParser(expression, context).evaluate();
}

inline std::vector<std::string> when_identifiers(std::string_view expression) {
  std::vector<std::string> identifiers;
  std::unordered_set<std::string> seen;
  const auto add_identifier = [&](std::string identifier) {
    if (identifier == "true" || identifier == "false") {
      return;
    }
    if (seen.insert(identifier).second) {
      identifiers.push_back(std::move(identifier));
    }
  };
  std::size_t offset = 0u;
  while (offset < expression.size()) {
    const char value = expression[offset];
    const bool allowed_start =
        std::isalnum(static_cast<unsigned char>(value)) != 0 || value == '_' ||
        value == '.' || value == '-';
    if (!allowed_start) {
      ++offset;
      continue;
    }

    const std::size_t start = offset;
    while (offset < expression.size()) {
      const char token = expression[offset];
      const bool allowed =
          std::isalnum(static_cast<unsigned char>(token)) != 0 ||
          token == '_' || token == '.' || token == '-';
      if (!allowed) {
        break;
      }
      ++offset;
    }

    const std::string identifier =
        std::string(expression.substr(start, offset - start));
    add_identifier(identifier);
    if (identifier == "navigation.flyActive" ||
        identifier == "navigation.orbitActive" ||
        identifier == "navigation.panActive") {
      add_identifier("navigation.active");
    }
  }
  return identifiers;
}

inline bool at_most_one_prefix_true(const QuaderKeybindingContext &context,
                                    std::string_view prefix) {
  bool found = false;
  for (const auto &[key, value] : context.values) {
    if (!value || key.find(prefix) != 0u) {
      continue;
    }
    if (found) {
      return false;
    }
    found = true;
  }
  return true;
}

inline bool
valid_semantic_when_context(const QuaderKeybindingContext &context) {
  if (!at_most_one_prefix_true(context, "activeTool.")) {
    return false;
  }
  if (!at_most_one_prefix_true(context, "selectionMode.")) {
    return false;
  }

  const bool fly = context.get("navigation.flyActive");
  const bool orbit = context.get("navigation.orbitActive");
  const bool pan = context.get("navigation.panActive");
  if ((fly ? 1 : 0) + (orbit ? 1 : 0) + (pan ? 1 : 0) > 1) {
    return false;
  }
  if ((fly || orbit || pan) && !context.get("navigation.active")) {
    return false;
  }
  return true;
}

inline bool when_expressions_overlap(std::string_view left,
                                     std::string_view right) {
  std::vector<std::string> identifiers = when_identifiers(left);
  for (const std::string &identifier : when_identifiers(right)) {
    if (std::find(identifiers.begin(), identifiers.end(), identifier) ==
        identifiers.end()) {
      identifiers.push_back(identifier);
    }
  }

  if (identifiers.size() > 16u) {
    return true;
  }

  const std::size_t combinations = std::size_t{1} << identifiers.size();
  for (std::size_t mask = 0u; mask < combinations; ++mask) {
    QuaderKeybindingContext context;
    for (std::size_t index = 0u; index < identifiers.size(); ++index) {
      context.set(identifiers[index], (mask & (std::size_t{1} << index)) != 0u);
    }
    if (!valid_semantic_when_context(context)) {
      continue;
    }
    if (when_matches(left, context) && when_matches(right, context)) {
      return true;
    }
  }
  return false;
}

inline bool sequence_starts_with(const std::vector<QuaderInputChord> &sequence,
                                 const std::vector<QuaderInputChord> &prefix) {
  return quader_input_sequence_starts_with(sequence, prefix);
}

inline bool same_shortcut(const std::vector<QuaderInputChord> &left,
                          const std::vector<QuaderInputChord> &right) {
  return quader_input_shortcuts_equal(left, right);
}

/**
 * Represents one persisted keybinding JSON entry before validation.
 */
struct KeybindingJsonEntry {
    std::string command;
    std::string key;
    std::string shortcut;
    std::string when;
};

/**
 * Parses the supported keybindings JSON array shape without adding API target dependencies.
 */
class KeybindingJsonReader final {
public:
    explicit KeybindingJsonReader(std::string_view input)
        : input_(input) {}

    bool parse(std::vector<KeybindingJsonEntry>& entries) {
        entries.clear();
        skip_whitespace();
        if (!consume('[')) {
            error_ = "Keybindings file must contain a JSON array.";
            return false;
        }
        skip_whitespace();
        if (consume(']')) {
          skip_whitespace();
          return at_end();
        }

        while (!at_end()) {
          KeybindingJsonEntry entry;
          if (!parse_object(entry)) {
            return false;
          }
          entries.push_back(std::move(entry));
          skip_whitespace();
          if (consume(']')) {
            skip_whitespace();
            if (!at_end()) {
              error_ = "Unexpected trailing JSON content.";
              return false;
            }
            return true;
          }
          if (!consume(',')) {
            error_ = "Expected ',' or ']' after keybinding entry.";
            return false;
          }
        }

        error_ = "Unterminated keybindings array.";
        return false;
    }

    [[nodiscard]] const std::string& error() const {
        return error_;
    }

private:
  bool parse_object(KeybindingJsonEntry &entry) {
    skip_whitespace();
    if (!consume('{')) {
      error_ = "Each keybinding entry must be an object.";
      return false;
    }
    skip_whitespace();
    if (consume('}')) {
      return true;
    }

    while (!at_end()) {
      std::string property;
      if (!parse_string(property)) {
        error_ = "Expected a string property name in keybinding entry.";
        return false;
      }
      skip_whitespace();
      if (!consume(':')) {
        error_ = "Expected ':' after keybinding property name.";
        return false;
      }
      skip_whitespace();

      std::string value;
      if (peek() == '"') {
        if (!parse_string(value)) {
          return false;
        }
        if (property == "command") {
          entry.command = std::move(value);
        } else if (property == "key") {
          entry.key = std::move(value);
        } else if (property == "shortcut") {
          entry.shortcut = std::move(value);
        } else if (property == "when") {
          entry.when = std::move(value);
        }
      } else if (!skip_value()) {
        return false;
      }

      skip_whitespace();
      if (consume('}')) {
        return true;
      }
      if (!consume(',')) {
        error_ = "Expected ',' or '}' after keybinding property.";
        return false;
      }
      skip_whitespace();
    }

    error_ = "Unterminated keybinding entry.";
    return false;
  }

  bool parse_string(std::string &value) {
    value.clear();
    if (!consume('"')) {
      return false;
    }
    while (!at_end()) {
      const char ch = input_[offset_++];
      if (ch == '"') {
        return true;
      }
      if (ch != '\\') {
        value.push_back(ch);
        continue;
      }
      if (at_end()) {
        error_ = "Unterminated escape sequence in keybindings file.";
        return false;
      }
      const char escaped = input_[offset_++];
      switch (escaped) {
      case '"':
      case '\\':
      case '/':
        value.push_back(escaped);
        break;
      case 'b':
        value.push_back('\b');
        break;
      case 'f':
        value.push_back('\f');
        break;
      case 'n':
        value.push_back('\n');
        break;
      case 'r':
        value.push_back('\r');
        break;
      case 't':
        value.push_back('\t');
        break;
      case 'u':
        if (offset_ + 4u > input_.size()) {
          error_ = "Invalid unicode escape in keybindings file.";
          return false;
        }
        offset_ += 4u;
        value.push_back('?');
        break;
      default:
        error_ = "Invalid escape sequence in keybindings file.";
        return false;
      }
    }
    error_ = "Unterminated string in keybindings file.";
    return false;
  }

  bool skip_value() {
    skip_whitespace();
    if (at_end()) {
      error_ = "Expected a JSON value.";
      return false;
    }
    if (peek() == '"') {
      std::string ignored;
      return parse_string(ignored);
    }
    if (peek() == '{' || peek() == '[') {
      return skip_container();
    }
    while (!at_end() && peek() != ',' && peek() != '}' && peek() != ']') {
      ++offset_;
    }
    return true;
  }

  bool skip_container() {
    const char open = input_[offset_++];
    const char close = open == '{' ? '}' : ']';
    int depth = 1;
    while (!at_end() && depth > 0) {
      if (peek() == '"') {
        std::string ignored;
        if (!parse_string(ignored)) {
          return false;
        }
        continue;
      }
      const char ch = input_[offset_++];
      if (ch == open) {
        ++depth;
      } else if (ch == close) {
        --depth;
      }
    }
    if (depth != 0) {
      error_ = "Unterminated nested JSON value in keybindings file.";
      return false;
    }
    return true;
  }

    bool consume(char token) {
      skip_whitespace();
      if (at_end() || input_[offset_] != token) {
        return false;
      }
        ++offset_;
        return true;
    }

    void skip_whitespace() {
      while (!at_end() &&
             std::isspace(static_cast<unsigned char>(input_[offset_])) != 0) {
        ++offset_;
      }
    }

    [[nodiscard]] char peek() const {
      return at_end() ? '\0' : input_[offset_];
    }

    [[nodiscard]] bool at_end() const { return offset_ >= input_.size(); }

    std::string_view input_;
    std::size_t offset_ = 0u;
    std::string error_;
};

inline std::string json_escape(std::string_view value) {
  std::string escaped;
  for (const char ch : value) {
    switch (ch) {
    case '"':
      escaped += "\\\"";
      break;
    case '\\':
      escaped += "\\\\";
      break;
    case '\b':
      escaped += "\\b";
      break;
    case '\f':
      escaped += "\\f";
      break;
    case '\n':
      escaped += "\\n";
      break;
    case '\r':
      escaped += "\\R";
      break;
    case '\t':
      escaped += "\\t";
      break;
    default:
      escaped.push_back(ch);
      break;
    }
  }
  return escaped;
}

} // namespace keybinding_detail

/**
 * Owns default and user keybindings, context-aware resolution, and persistence.
 */
class QuaderKeybindingService final {
public:
  void set_storage_path(std::filesystem::path path) {
    storage_path_ = std::move(path);
  }

  [[nodiscard]] const std::filesystem::path &storage_path() const {
    return storage_path_;
  }

  void clear_user_keybindings() {
    user_rules_.clear();
    pending_sequence_.clear();
  }

  void register_target(std::string target_id) {
    if (!target_id.empty()) {
      known_targets_.insert(std::move(target_id));
    }
  }

  [[nodiscard]] bool has_target(std::string_view target_id) const {
    if (known_targets_.contains(std::string(target_id))) {
      return true;
    }
    const auto has_target_rule = [&](const QuaderKeybindingRule &rule) {
      return rule.target_id == target_id;
    };
    return std::any_of(default_rules_.begin(), default_rules_.end(),
                       has_target_rule) ||
           std::any_of(user_rules_.begin(), user_rules_.end(), has_target_rule);
  }

  bool add_default_keybinding(std::string target_id, std::string shortcut,
                              std::string when = {}, int weight = 0) {
    return add_rule(default_rules_, std::move(target_id), std::move(shortcut),
                    std::move(when), "default", weight, false);
  }

  [[nodiscard]] QuaderKeybindingMutationResult
  set_user_shortcut(std::string target_id, std::string shortcut,
                    std::string when = {}) {
    if (target_id.empty()) {
      return {.ok = false, .message = "Command id is required."};
    }
    if (!has_target(target_id)) {
      return {.ok = false, .message = "Unknown command id."};
    }

    const auto parsed = keybinding_detail::parse_shortcut(shortcut);
    if (!parsed || parsed->empty()) {
      return {.ok = false, .message = "Shortcut is invalid."};
    }

    QuaderKeybindingRule requested;
    requested.target_id = target_id;
    requested.sequence = *parsed;
    requested.shortcut = keybinding_detail::format_shortcut(requested.sequence);
    requested.when = when;
    requested.source = "user";
    requested.weight = kUserWeight;

    if (const std::optional<QuaderKeybindingRule> current =
            effective_rule_for_target(target_id);
        current && !current->removal &&
        keybinding_detail::same_shortcut(current->sequence,
                                         requested.sequence) &&
        current->when == requested.when) {
      pending_sequence_.clear();
      return {.ok = true, .changed = false, .message = "Shortcut unchanged."};
    }

    erase_user_rules_for(target_id);
    requested.order = next_order_++;
    user_rules_.push_back(std::move(requested));
    pending_sequence_.clear();
    if (const std::optional<std::string> error = save_if_configured()) {
      return {.ok = false, .changed = true, .message = *error};
    }
    return {.ok = true, .changed = true, .message = "Shortcut updated."};
  }

  [[nodiscard]] QuaderKeybindingMutationResult
  remove_user_shortcut(std::string target_id) {
    if (target_id.empty()) {
      return {.ok = false, .message = "Command id is required."};
    }
    if (!has_target(target_id)) {
      return {.ok = false, .message = "Unknown command id."};
    }

    if (!effective_rule_for_target(target_id)) {
      pending_sequence_.clear();
      return {
          .ok = true, .changed = false, .message = "Shortcut already removed."};
    }

    erase_user_rules_for(target_id);
    QuaderKeybindingRule rule;
    rule.target_id = std::move(target_id);
    rule.source = "user";
    rule.weight = kUserWeight;
    rule.removal = true;
    rule.order = next_order_++;
    user_rules_.push_back(std::move(rule));
    pending_sequence_.clear();
    if (const std::optional<std::string> error = save_if_configured()) {
      return {.ok = false, .changed = true, .message = *error};
    }
    return {.ok = true, .changed = true, .message = "Shortcut removed."};
  }

  [[nodiscard]] QuaderKeybindingMutationResult
  reset_user_shortcut(std::string_view target_id) {
    if (target_id.empty()) {
      return {.ok = false, .message = "Command id is required."};
    }
    if (!has_target(target_id)) {
      return {.ok = false, .message = "Unknown command id."};
    }
    if (!has_user_rule_for(target_id)) {
      pending_sequence_.clear();
      return {.ok = true,
              .changed = false,
              .message = "Shortcut already at default."};
    }

    erase_user_rules_for(target_id);
    pending_sequence_.clear();
    if (const std::optional<std::string> error = save_if_configured()) {
      return {.ok = false, .changed = true, .message = *error};
    }
    return {.ok = true, .changed = true, .message = "Shortcut reset."};
  }

  [[nodiscard]] std::string shortcut_for(std::string_view target_id) const {
    const std::vector<QuaderKeybindingRule> rules = effective_rules();
    const QuaderKeybindingRule *best = nullptr;
    for (const QuaderKeybindingRule &rule : rules) {
      if (rule.target_id != target_id) {
        continue;
      }
      if (best == nullptr || rule_precedes(*best, rule)) {
        best = &rule;
      }
    }
    return best == nullptr ? std::string{} : best->shortcut;
  }

    [[nodiscard]] std::vector<QuaderKeybindingRule> keybindings() const {
      return effective_rules();
    }

    [[nodiscard]] std::vector<QuaderKeybindingConflict>
    conflicts_for(std::string_view shortcut, std::string_view when = {}) const {
      const auto parsed = keybinding_detail::parse_shortcut(shortcut);
      if (!parsed || parsed->empty()) {
        return {};
      }

      std::vector<QuaderKeybindingConflict> conflicts;
      for (const QuaderKeybindingRule &rule : effective_rules()) {
        if (!keybinding_detail::same_shortcut(rule.sequence, *parsed)) {
          continue;
        }
        if (!keybinding_detail::when_expressions_overlap(rule.when, when)) {
          continue;
        }
        conflicts.push_back({
            .target_id = rule.target_id,
            .shortcut = rule.shortcut,
            .when = rule.when,
            .source = rule.source,
        });
      }
      return conflicts;
    }

    [[nodiscard]] QuaderKeybindingDispatch
    resolve_sequence(const std::vector<QuaderInputChord> &sequence,
                     const QuaderKeybindingContext &context) const {
      if (sequence.empty()) {
        return {};
      }

      std::vector<const QuaderKeybindingRule *> prefix_matches;
      std::vector<const QuaderKeybindingRule *> exact_matches;
      const std::vector<QuaderKeybindingRule> rules = effective_rules();
      for (const QuaderKeybindingRule &rule : rules) {
        if (!keybinding_detail::sequence_starts_with(rule.sequence, sequence)) {
          continue;
        }
        if (!keybinding_detail::when_matches(rule.when, context)) {
          continue;
        }
        if (rule.sequence.size() == sequence.size()) {
          exact_matches.push_back(&rule);
        } else {
          prefix_matches.push_back(&rule);
        }
      }

      if (!exact_matches.empty()) {
        const QuaderKeybindingRule *best = exact_matches.front();
        for (const QuaderKeybindingRule *candidate : exact_matches) {
          if (rule_precedes(*best, *candidate)) {
            best = candidate;
          }
        }
        return {
            .kind = QuaderKeybindingDispatch::Kind::ExactMatch,
            .target_id = best->target_id,
            .shortcut = best->shortcut,
        };
      }

      if (!prefix_matches.empty()) {
        return {
            .kind = QuaderKeybindingDispatch::Kind::PendingChord,
            .shortcut = keybinding_detail::format_shortcut(sequence),
        };
      }

      return {};
    }

    [[nodiscard]] QuaderKeybindingDispatch dispatch(
            const QuaderInputChord& chord,
            const QuaderKeybindingContext& context) {
        if (chord.empty()) {
          pending_sequence_.clear();
          return {};
        }

        std::vector<QuaderInputChord> sequence = pending_sequence_;
        sequence.push_back(chord);
        QuaderKeybindingDispatch result = resolve_sequence(sequence, context);
        if (result.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
          pending_sequence_ = std::move(sequence);
          return result;
        }

        pending_sequence_.clear();
        return result;
    }

    void reset_pending_chord() { pending_sequence_.clear(); }

    [[nodiscard]] bool has_pending_chord() const {
      return !pending_sequence_.empty();
    }

    [[nodiscard]] std::optional<std::string> load_user_keybindings() {
      if (storage_path_.empty() || !std::filesystem::exists(storage_path_)) {
        return std::nullopt;
      }
      return load_user_keybindings(storage_path_);
    }

    [[nodiscard]] std::optional<std::string>
    load_user_keybindings(const std::filesystem::path &path) {
      std::ifstream file(path);
      if (!file) {
        return "Unable to open keybindings file.";
      }

      std::ostringstream buffer;
      buffer << file.rdbuf();
      const std::string text = buffer.str();
      std::vector<keybinding_detail::KeybindingJsonEntry> entries;
      keybinding_detail::KeybindingJsonReader reader(text);
      if (!reader.parse(entries)) {
        return reader.error();
      }

      std::vector<QuaderKeybindingRule> loaded;
      for (const keybinding_detail::KeybindingJsonEntry &entry : entries) {
        const std::string command = entry.command;
        const std::string key = entry.key.empty() ? entry.shortcut : entry.key;
        const std::string when = entry.when;
        if (command.empty()) {
          return "Each keybinding entry must include a command.";
        }

        const bool removal = command.starts_with("-");
        const std::string target_id = removal ? command.substr(1u) : command;
        if (target_id.empty()) {
          return "Removal keybinding must include a command id.";
        }
        if (!has_target(target_id)) {
          return "Keybinding entry references an unknown command id: " +
                 target_id;
        }

        QuaderKeybindingRule rule;
        rule.target_id = target_id;
        rule.when = when;
        rule.source = "user";
        rule.weight = kUserWeight;
        rule.removal = removal;
        rule.order = next_order_++;
        if (!removal) {
          const auto parsed = keybinding_detail::parse_shortcut(key);
          if (!parsed || parsed->empty()) {
            return "Keybinding entry has an invalid shortcut.";
          }
          rule.sequence = *parsed;
          rule.shortcut = keybinding_detail::format_shortcut(rule.sequence);
        }
        loaded.push_back(std::move(rule));
      }

      user_rules_ = std::move(loaded);
      pending_sequence_.clear();
      return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> save_user_keybindings() const {
      if (storage_path_.empty()) {
        return std::nullopt;
      }
      return save_user_keybindings(storage_path_);
    }

    [[nodiscard]] std::optional<std::string>
    save_user_keybindings(const std::filesystem::path &path) const {
      std::error_code error;
      if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
          return "Unable to create keybindings directory.";
        }
      }

      std::ostringstream json;
      json << "[\n";
      for (const QuaderKeybindingRule &rule : user_rules_) {
        const std::size_t index =
            static_cast<std::size_t>(&rule - user_rules_.data());
        json << "  {\n";
        json << "    \"command\": \""
             << keybinding_detail::json_escape(
                    rule.removal ? "-" + rule.target_id : rule.target_id)
             << "\"";
        if (!rule.removal) {
          json << ",\n";
          json << "    \"key\": \""
               << keybinding_detail::json_escape(rule.shortcut) << "\"";
        }
        if (!rule.when.empty()) {
          json << ",\n";
          json << "    \"when\": \""
               << keybinding_detail::json_escape(rule.when) << "\"";
        }
        json << "\n";
        json << "  }";
        if (index + 1u < user_rules_.size()) {
          json << ",";
        }
        json << "\n";
      }
      json << "]\n";

      std::ofstream file(path);
      if (!file) {
        return "Unable to write keybindings file.";
      }
      file << json.str();
      return std::nullopt;
    }

private:
    static constexpr int kUserWeight = 1000;

    bool add_rule(std::vector<QuaderKeybindingRule> &rules,
                  std::string target_id, std::string shortcut, std::string when,
                  std::string source, int weight, bool removal) {
      if (target_id.empty()) {
        return false;
      }
      register_target(target_id);

      QuaderKeybindingRule rule;
      rule.target_id = std::move(target_id);
      rule.when = std::move(when);
      rule.source = std::move(source);
      rule.weight = weight;
      rule.removal = removal;
      rule.order = next_order_++;
      if (!removal) {
        const auto parsed = keybinding_detail::parse_shortcut(shortcut);
        if (!parsed || parsed->empty()) {
          return false;
        }
        rule.sequence = *parsed;
        rule.shortcut = keybinding_detail::format_shortcut(rule.sequence);
      }
      const auto duplicate =
          std::find_if(rules.begin(), rules.end(),
                       [&](const QuaderKeybindingRule &existing) {
                         return existing.target_id == rule.target_id &&
                                existing.shortcut == rule.shortcut &&
                                existing.when == rule.when &&
                                existing.source == rule.source &&
                                existing.removal == rule.removal &&
                                keybinding_detail::same_shortcut(
                                    existing.sequence, rule.sequence);
                       });
      if (duplicate != rules.end()) {
        return true;
      }
      rules.push_back(std::move(rule));
      return true;
    }

    [[nodiscard]] bool has_user_rule_for(std::string_view target_id) const {
      return std::any_of(user_rules_.begin(), user_rules_.end(),
                         [&](const QuaderKeybindingRule &rule) {
                           return rule.target_id == target_id;
                         });
    }

    [[nodiscard]] std::optional<QuaderKeybindingRule>
    effective_rule_for_target(std::string_view target_id) const {
      const std::vector<QuaderKeybindingRule> rules = effective_rules();
      const QuaderKeybindingRule *best = nullptr;
      for (const QuaderKeybindingRule &rule : rules) {
        if (rule.target_id != target_id) {
          continue;
        }
        if (best == nullptr || rule_precedes(*best, rule)) {
          best = &rule;
        }
      }
      return best == nullptr ? std::nullopt
                             : std::optional<QuaderKeybindingRule>{*best};
    }

    void erase_user_rules_for(std::string_view target_id) {
      user_rules_.erase(std::remove_if(user_rules_.begin(), user_rules_.end(),
                                       [&](const QuaderKeybindingRule &rule) {
                                         return rule.target_id == target_id;
                                       }),
                        user_rules_.end());
    }

    [[nodiscard]] std::optional<std::string> save_if_configured() const {
      return storage_path_.empty() ? std::nullopt
                                   : save_user_keybindings(storage_path_);
    }

    [[nodiscard]] std::vector<QuaderKeybindingRule> effective_rules() const {
      std::unordered_set<std::string> shadowed_targets;
      for (const QuaderKeybindingRule &rule : user_rules_) {
        shadowed_targets.insert(rule.target_id);
      }

      std::vector<QuaderKeybindingRule> result;
      result.reserve(default_rules_.size() + user_rules_.size());
      for (const QuaderKeybindingRule &rule : default_rules_) {
        if (!shadowed_targets.contains(rule.target_id)) {
          result.push_back(rule);
        }
      }
      for (const QuaderKeybindingRule &rule : user_rules_) {
        if (!rule.removal) {
          result.push_back(rule);
        }
      }
      return result;
    }

    [[nodiscard]] static bool rule_precedes(const QuaderKeybindingRule &left,
                                            const QuaderKeybindingRule &right) {
      if (left.weight != right.weight) {
        return left.weight < right.weight;
      }
      return left.order < right.order;
    }

    std::filesystem::path storage_path_;
    std::unordered_set<std::string> known_targets_;
    std::vector<QuaderKeybindingRule> default_rules_;
    std::vector<QuaderKeybindingRule> user_rules_;
    std::vector<QuaderInputChord> pending_sequence_;
    std::size_t next_order_ = 1u;
};

} // namespace quader
