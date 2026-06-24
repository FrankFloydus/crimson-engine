////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "editor/input/QuaderInputBinding.h"

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
 * Stores a keybinding rule as loaded from defaults or user settings.
 */
struct QuaderKeybindingRule {
    std::string targetId;
    std::vector<QuaderInputChord> sequence;
    std::string shortcut;
    std::string when;
    std::string source = "default";
    int weight = 0;
    bool removal = false;
    std::size_t order = 0u;
};

/**
 * Stores keybinding conflict information for rebinding UI and tests.
 */
struct QuaderKeybindingConflict {
    std::string targetId;
    std::string shortcut;
    std::string when;
    std::string source;
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
    std::string targetId;
    std::string shortcut;
};

namespace keybinding_detail {

inline std::string trimCopy(std::string_view text) {
    std::size_t first = 0u;
    while (first < text.size() && std::isspace(static_cast<unsigned char>(text[first])) != 0) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && std::isspace(static_cast<unsigned char>(text[last - 1u])) != 0) {
        --last;
    }
    return std::string(text.substr(first, last - first));
}

inline std::string lowerCopy(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });
    return result;
}

inline std::string upperCopy(std::string_view text) {
    std::string result(text);
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    return result;
}

inline std::vector<std::string> splitCopy(std::string_view text, char delimiter) {
    std::vector<std::string> parts;
    std::size_t start = 0u;
    while (start <= text.size()) {
        const std::size_t end = text.find(delimiter, start);
        const std::size_t count = end == std::string_view::npos ? text.size() - start : end - start;
        parts.push_back(trimCopy(text.substr(start, count)));
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1u;
    }
    return parts;
}

inline std::string canonicalKeyToken(std::string_view token) {
    const std::string trimmed = trimCopy(token);
    if (trimmed.empty()) {
        return {};
    }

    const std::string lower = lowerCopy(trimmed);
    if (lower.size() == 1u && std::isalpha(static_cast<unsigned char>(lower.front())) != 0) {
        return upperCopy(lower);
    }
    if (lower.size() == 1u && std::isdigit(static_cast<unsigned char>(lower.front())) != 0) {
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
            numeric = numeric && std::isdigit(static_cast<unsigned char>(lower[index])) != 0;
        }
        if (numeric) {
            return upperCopy(lower);
        }
    }

    static const std::unordered_map<std::string, std::string> namedKeys{
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
    const auto named = namedKeys.find(lower);
    return named == namedKeys.end() ? trimmed : named->second;
}

inline bool parseChord(std::string_view text, QuaderInputChord& chord) {
    const std::optional<QuaderInputChord> parsed = quaderInputParseChord(text);
    if (!parsed) {
        chord = {};
        return false;
    }
    chord = *parsed;
    return true;
}

inline std::optional<std::vector<QuaderInputChord>> parseShortcut(std::string_view shortcut) {
    return quaderInputParseShortcut(shortcut);
}

inline std::string formatChord(const QuaderInputChord& chord) {
    return quaderInputFormatChord(chord);
}

inline std::string formatShortcut(const std::vector<QuaderInputChord>& sequence) {
    return quaderInputFormatShortcut(sequence);
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
        skipWhitespace();
        if (atEnd()) {
            return true;
        }
        const bool value = parseOr();
        skipWhitespace();
        return failed_ || !atEnd() ? false : value;
    }

private:
    bool parseOr() {
        bool value = parseAnd();
        while (consume("||")) {
            value = parseAnd() || value;
        }
        return value;
    }

    bool parseAnd() {
        bool value = parseUnary();
        while (consume("&&")) {
            value = parseUnary() && value;
        }
        return value;
    }

    bool parseUnary() {
        if (consume("!")) {
            return !parseUnary();
        }
        return parsePrimary();
    }

    bool parsePrimary() {
        skipWhitespace();
        if (consume("(")) {
            const bool value = parseOr();
            if (!consume(")")) {
                failed_ = true;
                return false;
            }
            return value;
        }

        const std::string identifier = parseIdentifier();
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

    std::string parseIdentifier() {
        skipWhitespace();
        const std::size_t start = offset_;
        while (!atEnd()) {
            const char value = expression_[offset_];
            const bool allowed = std::isalnum(static_cast<unsigned char>(value)) != 0 ||
                    value == '_' ||
                    value == '.' ||
                    value == '-';
            if (!allowed) {
                break;
            }
            ++offset_;
        }
        return std::string(expression_.substr(start, offset_ - start));
    }

    bool consume(std::string_view token) {
        skipWhitespace();
        if (expression_.substr(offset_, token.size()) != token) {
            return false;
        }
        offset_ += token.size();
        return true;
    }

    void skipWhitespace() {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(expression_[offset_])) != 0) {
            ++offset_;
        }
    }

    [[nodiscard]] bool atEnd() const {
        return offset_ >= expression_.size();
    }

    std::string_view expression_;
    const QuaderKeybindingContext& context_;
    std::size_t offset_ = 0u;
    bool failed_ = false;
};

inline bool whenMatches(std::string_view expression, const QuaderKeybindingContext& context) {
    return WhenParser(expression, context).evaluate();
}

inline bool sequenceStartsWith(
        const std::vector<QuaderInputChord>& sequence,
        const std::vector<QuaderInputChord>& prefix) {
    return quaderInputSequenceStartsWith(sequence, prefix);
}

inline bool sameShortcut(
        const std::vector<QuaderInputChord>& left,
        const std::vector<QuaderInputChord>& right) {
    return quaderInputShortcutsEqual(left, right);
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
        skipWhitespace();
        if (!consume('[')) {
            error_ = "Keybindings file must contain a JSON array.";
            return false;
        }
        skipWhitespace();
        if (consume(']')) {
            skipWhitespace();
            return atEnd();
        }

        while (!atEnd()) {
            KeybindingJsonEntry entry;
            if (!parseObject(entry)) {
                return false;
            }
            entries.push_back(std::move(entry));
            skipWhitespace();
            if (consume(']')) {
                skipWhitespace();
                if (!atEnd()) {
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
    bool parseObject(KeybindingJsonEntry& entry) {
        skipWhitespace();
        if (!consume('{')) {
            error_ = "Each keybinding entry must be an object.";
            return false;
        }
        skipWhitespace();
        if (consume('}')) {
            return true;
        }

        while (!atEnd()) {
            std::string property;
            if (!parseString(property)) {
                error_ = "Expected a string property name in keybinding entry.";
                return false;
            }
            skipWhitespace();
            if (!consume(':')) {
                error_ = "Expected ':' after keybinding property name.";
                return false;
            }
            skipWhitespace();

            std::string value;
            if (peek() == '"') {
                if (!parseString(value)) {
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
            } else if (!skipValue()) {
                return false;
            }

            skipWhitespace();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                error_ = "Expected ',' or '}' after keybinding property.";
                return false;
            }
            skipWhitespace();
        }

        error_ = "Unterminated keybinding entry.";
        return false;
    }

    bool parseString(std::string& value) {
        value.clear();
        if (!consume('"')) {
            return false;
        }
        while (!atEnd()) {
            const char ch = input_[offset_++];
            if (ch == '"') {
                return true;
            }
            if (ch != '\\') {
                value.push_back(ch);
                continue;
            }
            if (atEnd()) {
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

    bool skipValue() {
        skipWhitespace();
        if (atEnd()) {
            error_ = "Expected a JSON value.";
            return false;
        }
        if (peek() == '"') {
            std::string ignored;
            return parseString(ignored);
        }
        if (peek() == '{' || peek() == '[') {
            return skipContainer();
        }
        while (!atEnd() && peek() != ',' && peek() != '}' && peek() != ']') {
            ++offset_;
        }
        return true;
    }

    bool skipContainer() {
        const char open = input_[offset_++];
        const char close = open == '{' ? '}' : ']';
        int depth = 1;
        while (!atEnd() && depth > 0) {
            if (peek() == '"') {
                std::string ignored;
                if (!parseString(ignored)) {
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
        skipWhitespace();
        if (atEnd() || input_[offset_] != token) {
            return false;
        }
        ++offset_;
        return true;
    }

    void skipWhitespace() {
        while (!atEnd() && std::isspace(static_cast<unsigned char>(input_[offset_])) != 0) {
            ++offset_;
        }
    }

    [[nodiscard]] char peek() const {
        return atEnd() ? '\0' : input_[offset_];
    }

    [[nodiscard]] bool atEnd() const {
        return offset_ >= input_.size();
    }

    std::string_view input_;
    std::size_t offset_ = 0u;
    std::string error_;
};

inline std::string jsonEscape(std::string_view value) {
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
            escaped += "\\r";
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
    void setStoragePath(std::filesystem::path path) {
        storagePath_ = std::move(path);
    }

    [[nodiscard]] const std::filesystem::path& storagePath() const {
        return storagePath_;
    }

    void clearUserKeybindings() {
        userRules_.clear();
        pendingSequence_.clear();
    }

    void registerTarget(std::string targetId) {
        if (!targetId.empty()) {
            knownTargets_.insert(std::move(targetId));
        }
    }

    [[nodiscard]] bool hasTarget(std::string_view targetId) const {
        if (knownTargets_.contains(std::string(targetId))) {
            return true;
        }
        const auto hasTargetRule = [&](const QuaderKeybindingRule& rule) {
            return rule.targetId == targetId;
        };
        return std::any_of(defaultRules_.begin(), defaultRules_.end(), hasTargetRule) ||
                std::any_of(userRules_.begin(), userRules_.end(), hasTargetRule);
    }

    bool addDefaultKeybinding(
            std::string targetId,
            std::string shortcut,
            std::string when = {},
            int weight = 0) {
        return addRule(defaultRules_, std::move(targetId), std::move(shortcut), std::move(when), "default", weight, false);
    }

    [[nodiscard]] std::optional<std::string> setUserShortcut(std::string targetId, std::string shortcut, std::string when = {}) {
        if (targetId.empty()) {
            return "Command id is required.";
        }
        if (!hasTarget(targetId)) {
            return "Unknown command id.";
        }

        const auto parsed = keybinding_detail::parseShortcut(shortcut);
        if (!parsed || parsed->empty()) {
            return "Shortcut is invalid.";
        }

        eraseUserRulesFor(targetId);
        QuaderKeybindingRule rule;
        rule.targetId = std::move(targetId);
        rule.sequence = *parsed;
        rule.shortcut = keybinding_detail::formatShortcut(rule.sequence);
        rule.when = std::move(when);
        rule.source = "user";
        rule.weight = kUserWeight;
        rule.order = nextOrder_++;
        userRules_.push_back(std::move(rule));
        pendingSequence_.clear();
        return saveIfConfigured();
    }

    [[nodiscard]] std::optional<std::string> removeUserShortcut(std::string targetId) {
        if (targetId.empty()) {
            return "Command id is required.";
        }
        if (!hasTarget(targetId)) {
            return "Unknown command id.";
        }

        eraseUserRulesFor(targetId);
        QuaderKeybindingRule rule;
        rule.targetId = std::move(targetId);
        rule.source = "user";
        rule.weight = kUserWeight;
        rule.removal = true;
        rule.order = nextOrder_++;
        userRules_.push_back(std::move(rule));
        pendingSequence_.clear();
        return saveIfConfigured();
    }

    [[nodiscard]] std::optional<std::string> resetUserShortcut(std::string_view targetId) {
        if (targetId.empty()) {
            return "Command id is required.";
        }
        if (!hasTarget(targetId)) {
            return "Unknown command id.";
        }

        eraseUserRulesFor(targetId);
        pendingSequence_.clear();
        return saveIfConfigured();
    }

    [[nodiscard]] std::string shortcutFor(std::string_view targetId) const {
        const std::vector<QuaderKeybindingRule> rules = effectiveRules();
        const QuaderKeybindingRule* best = nullptr;
        for (const QuaderKeybindingRule& rule : rules) {
            if (rule.targetId != targetId) {
                continue;
            }
            if (best == nullptr || rulePrecedes(*best, rule)) {
                best = &rule;
            }
        }
        return best == nullptr ? std::string{} : best->shortcut;
    }

    [[nodiscard]] std::vector<QuaderKeybindingRule> keybindings() const {
        return effectiveRules();
    }

    [[nodiscard]] std::vector<QuaderKeybindingConflict> conflictsFor(
            std::string_view shortcut,
            std::string_view when = {}) const {
        const auto parsed = keybinding_detail::parseShortcut(shortcut);
        if (!parsed || parsed->empty()) {
            return {};
        }

        std::vector<QuaderKeybindingConflict> conflicts;
        for (const QuaderKeybindingRule& rule : effectiveRules()) {
            if (!keybinding_detail::sameShortcut(rule.sequence, *parsed)) {
                continue;
            }
            if (!when.empty() && !rule.when.empty() && rule.when != when) {
                continue;
            }
            conflicts.push_back({
                    .targetId = rule.targetId,
                    .shortcut = rule.shortcut,
                    .when = rule.when,
                    .source = rule.source,
            });
        }
        return conflicts;
    }

    [[nodiscard]] QuaderKeybindingDispatch resolveSequence(
            const std::vector<QuaderInputChord>& sequence,
            const QuaderKeybindingContext& context) const {
        if (sequence.empty()) {
            return {};
        }

        std::vector<const QuaderKeybindingRule*> prefixMatches;
        std::vector<const QuaderKeybindingRule*> exactMatches;
        const std::vector<QuaderKeybindingRule> rules = effectiveRules();
        for (const QuaderKeybindingRule& rule : rules) {
            if (!keybinding_detail::sequenceStartsWith(rule.sequence, sequence)) {
                continue;
            }
            if (!keybinding_detail::whenMatches(rule.when, context)) {
                continue;
            }
            if (rule.sequence.size() == sequence.size()) {
                exactMatches.push_back(&rule);
            } else {
                prefixMatches.push_back(&rule);
            }
        }

        if (!exactMatches.empty()) {
            const QuaderKeybindingRule* best = exactMatches.front();
            for (const QuaderKeybindingRule* candidate : exactMatches) {
                if (rulePrecedes(*best, *candidate)) {
                    best = candidate;
                }
            }
            return {
                    .kind = QuaderKeybindingDispatch::Kind::ExactMatch,
                    .targetId = best->targetId,
                    .shortcut = best->shortcut,
            };
        }

        if (!prefixMatches.empty()) {
            return {
                    .kind = QuaderKeybindingDispatch::Kind::PendingChord,
                    .shortcut = keybinding_detail::formatShortcut(sequence),
            };
        }

        return {};
    }

    [[nodiscard]] QuaderKeybindingDispatch dispatch(
            const QuaderInputChord& chord,
            const QuaderKeybindingContext& context) {
        if (chord.empty()) {
            pendingSequence_.clear();
            return {};
        }

        std::vector<QuaderInputChord> sequence = pendingSequence_;
        sequence.push_back(chord);
        QuaderKeybindingDispatch result = resolveSequence(sequence, context);
        if (result.kind == QuaderKeybindingDispatch::Kind::PendingChord) {
            pendingSequence_ = std::move(sequence);
            return result;
        }

        pendingSequence_.clear();
        return result;
    }

    void resetPendingChord() {
        pendingSequence_.clear();
    }

    [[nodiscard]] bool hasPendingChord() const {
        return !pendingSequence_.empty();
    }

    [[nodiscard]] std::optional<std::string> loadUserKeybindings() {
        if (storagePath_.empty() || !std::filesystem::exists(storagePath_)) {
            return std::nullopt;
        }
        return loadUserKeybindings(storagePath_);
    }

    [[nodiscard]] std::optional<std::string> loadUserKeybindings(const std::filesystem::path& path) {
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
        for (const keybinding_detail::KeybindingJsonEntry& entry : entries) {
            const std::string command = entry.command;
            const std::string key = entry.key.empty() ? entry.shortcut : entry.key;
            const std::string when = entry.when;
            if (command.empty()) {
                return "Each keybinding entry must include a command.";
            }

            const bool removal = command.starts_with("-");
            const std::string targetId = removal ? command.substr(1u) : command;
            if (targetId.empty()) {
                return "Removal keybinding must include a command id.";
            }
            if (!hasTarget(targetId)) {
                registerTarget(targetId);
            }

            QuaderKeybindingRule rule;
            rule.targetId = targetId;
            rule.when = when;
            rule.source = "user";
            rule.weight = kUserWeight;
            rule.removal = removal;
            rule.order = nextOrder_++;
            if (!removal) {
                const auto parsed = keybinding_detail::parseShortcut(key);
                if (!parsed || parsed->empty()) {
                    return "Keybinding entry has an invalid shortcut.";
                }
                rule.sequence = *parsed;
                rule.shortcut = keybinding_detail::formatShortcut(rule.sequence);
            }
            loaded.push_back(std::move(rule));
        }

        userRules_ = std::move(loaded);
        pendingSequence_.clear();
        return std::nullopt;
    }

    [[nodiscard]] std::optional<std::string> saveUserKeybindings() const {
        if (storagePath_.empty()) {
            return std::nullopt;
        }
        return saveUserKeybindings(storagePath_);
    }

    [[nodiscard]] std::optional<std::string> saveUserKeybindings(const std::filesystem::path& path) const {
        std::error_code error;
        if (path.has_parent_path()) {
            std::filesystem::create_directories(path.parent_path(), error);
            if (error) {
                return "Unable to create keybindings directory.";
            }
        }

        std::ostringstream json;
        json << "[\n";
        for (const QuaderKeybindingRule& rule : userRules_) {
            const std::size_t index = static_cast<std::size_t>(&rule - userRules_.data());
            json << "  {\n";
            json << "    \"command\": \"" << keybinding_detail::jsonEscape(rule.removal ? "-" + rule.targetId : rule.targetId) << "\"";
            if (!rule.removal) {
                json << ",\n";
                json << "    \"key\": \"" << keybinding_detail::jsonEscape(rule.shortcut) << "\"";
            }
            if (!rule.when.empty()) {
                json << ",\n";
                json << "    \"when\": \"" << keybinding_detail::jsonEscape(rule.when) << "\"";
            }
            json << "\n";
            json << "  }";
            if (index + 1u < userRules_.size()) {
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

    bool addRule(
            std::vector<QuaderKeybindingRule>& rules,
            std::string targetId,
            std::string shortcut,
            std::string when,
            std::string source,
            int weight,
            bool removal) {
        if (targetId.empty()) {
            return false;
        }
        registerTarget(targetId);

        QuaderKeybindingRule rule;
        rule.targetId = std::move(targetId);
        rule.when = std::move(when);
        rule.source = std::move(source);
        rule.weight = weight;
        rule.removal = removal;
        rule.order = nextOrder_++;
        if (!removal) {
            const auto parsed = keybinding_detail::parseShortcut(shortcut);
            if (!parsed || parsed->empty()) {
                return false;
            }
            rule.sequence = *parsed;
            rule.shortcut = keybinding_detail::formatShortcut(rule.sequence);
        }
        rules.push_back(std::move(rule));
        return true;
    }

    void eraseUserRulesFor(std::string_view targetId) {
        userRules_.erase(
                std::remove_if(userRules_.begin(), userRules_.end(), [&](const QuaderKeybindingRule& rule) {
                    return rule.targetId == targetId;
                }),
                userRules_.end());
    }

    [[nodiscard]] std::optional<std::string> saveIfConfigured() const {
        return storagePath_.empty() ? std::nullopt : saveUserKeybindings(storagePath_);
    }

    [[nodiscard]] std::vector<QuaderKeybindingRule> effectiveRules() const {
        std::unordered_set<std::string> shadowedTargets;
        for (const QuaderKeybindingRule& rule : userRules_) {
            shadowedTargets.insert(rule.targetId);
        }

        std::vector<QuaderKeybindingRule> result;
        result.reserve(defaultRules_.size() + userRules_.size());
        for (const QuaderKeybindingRule& rule : defaultRules_) {
            if (!shadowedTargets.contains(rule.targetId)) {
                result.push_back(rule);
            }
        }
        for (const QuaderKeybindingRule& rule : userRules_) {
            if (!rule.removal) {
                result.push_back(rule);
            }
        }
        return result;
    }

    [[nodiscard]] static bool rulePrecedes(const QuaderKeybindingRule& left, const QuaderKeybindingRule& right) {
        if (left.weight != right.weight) {
            return left.weight < right.weight;
        }
        return left.order < right.order;
    }

    std::filesystem::path storagePath_;
    std::unordered_set<std::string> knownTargets_;
    std::vector<QuaderKeybindingRule> defaultRules_;
    std::vector<QuaderKeybindingRule> userRules_;
    std::vector<QuaderInputChord> pendingSequence_;
    std::size_t nextOrder_ = 1u;
};

} // namespace quader
