////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#pragma once

#include "api/quader_input_types.hpp"

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace quader {

namespace input_binding_detail {

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

inline QuaderKeyCode letterKeyCode(char letter) {
    switch (static_cast<char>(std::toupper(static_cast<unsigned char>(letter)))) {
    case 'A':
        return QuaderKeyCode::A;
    case 'B':
        return QuaderKeyCode::B;
    case 'C':
        return QuaderKeyCode::C;
    case 'D':
        return QuaderKeyCode::D;
    case 'E':
        return QuaderKeyCode::E;
    case 'F':
        return QuaderKeyCode::F;
    case 'G':
        return QuaderKeyCode::G;
    case 'H':
        return QuaderKeyCode::H;
    case 'I':
        return QuaderKeyCode::I;
    case 'J':
        return QuaderKeyCode::J;
    case 'K':
        return QuaderKeyCode::K;
    case 'L':
        return QuaderKeyCode::L;
    case 'M':
        return QuaderKeyCode::M;
    case 'N':
        return QuaderKeyCode::N;
    case 'O':
        return QuaderKeyCode::O;
    case 'P':
        return QuaderKeyCode::P;
    case 'Q':
        return QuaderKeyCode::Q;
    case 'R':
        return QuaderKeyCode::R;
    case 'S':
        return QuaderKeyCode::S;
    case 'T':
        return QuaderKeyCode::T;
    case 'U':
        return QuaderKeyCode::U;
    case 'V':
        return QuaderKeyCode::V;
    case 'W':
        return QuaderKeyCode::W;
    case 'X':
        return QuaderKeyCode::X;
    case 'Y':
        return QuaderKeyCode::Y;
    case 'Z':
        return QuaderKeyCode::Z;
    default:
        return QuaderKeyCode::None;
    }
}

inline QuaderKeyCode digitKeyCode(char digit) {
    switch (digit) {
    case '0':
        return QuaderKeyCode::Digit0;
    case '1':
        return QuaderKeyCode::Digit1;
    case '2':
        return QuaderKeyCode::Digit2;
    case '3':
        return QuaderKeyCode::Digit3;
    case '4':
        return QuaderKeyCode::Digit4;
    case '5':
        return QuaderKeyCode::Digit5;
    case '6':
        return QuaderKeyCode::Digit6;
    case '7':
        return QuaderKeyCode::Digit7;
    case '8':
        return QuaderKeyCode::Digit8;
    case '9':
        return QuaderKeyCode::Digit9;
    default:
        return QuaderKeyCode::None;
    }
}

inline std::optional<QuaderKeyCode> functionKeyCode(std::string_view lower) {
    if (lower.size() < 2u || lower.front() != 'f') {
        return std::nullopt;
    }

    int number = 0;
    for (std::size_t index = 1u; index < lower.size(); ++index) {
        if (std::isdigit(static_cast<unsigned char>(lower[index])) == 0) {
            return std::nullopt;
        }
        number = (number * 10) + (lower[index] - '0');
    }

    if (number < 1 || number > 24) {
        return std::nullopt;
    }
    return static_cast<QuaderKeyCode>(static_cast<int>(QuaderKeyCode::F1) + (number - 1));
}

inline std::optional<QuaderKeyCode> numpadKeyCode(std::string_view lower) {
    std::string_view suffix;
    if (lower.rfind("numpad", 0) == 0) {
        suffix = lower.substr(6u);
    } else if (lower.rfind("keypad", 0) == 0) {
        suffix = lower.substr(6u);
    } else if (lower.rfind("kp", 0) == 0) {
        suffix = lower.substr(2u);
    } else {
        return std::nullopt;
    }

    if (suffix.size() == 1u && std::isdigit(static_cast<unsigned char>(suffix.front())) != 0) {
        return static_cast<QuaderKeyCode>(static_cast<int>(QuaderKeyCode::Numpad0) + (suffix.front() - '0'));
    }
    static const std::unordered_map<std::string_view, QuaderKeyCode> aliases{
            {"add", QuaderKeyCode::NumpadAdd},
            {"plus", QuaderKeyCode::NumpadAdd},
            {"subtract", QuaderKeyCode::NumpadSubtract},
            {"minus", QuaderKeyCode::NumpadSubtract},
            {"multiply", QuaderKeyCode::NumpadMultiply},
            {"asterisk", QuaderKeyCode::NumpadMultiply},
            {"divide", QuaderKeyCode::NumpadDivide},
            {"slash", QuaderKeyCode::NumpadDivide},
            {"decimal", QuaderKeyCode::NumpadDecimal},
            {"period", QuaderKeyCode::NumpadDecimal},
            {"enter", QuaderKeyCode::NumpadEnter},
    };
    const auto found = aliases.find(suffix);
    return found == aliases.end() ? std::nullopt : std::optional<QuaderKeyCode>(found->second);
}

inline std::optional<QuaderKeyCode> keyCodeFromToken(std::string_view token) {
    const std::string trimmed = trimCopy(token);
    if (trimmed.empty()) {
        return std::nullopt;
    }

    const std::string lower = lowerCopy(trimmed);
    if (lower.size() == 1u) {
        const char ch = lower.front();
        if (std::isalpha(static_cast<unsigned char>(ch)) != 0) {
            return letterKeyCode(ch);
        }
        if (std::isdigit(static_cast<unsigned char>(ch)) != 0) {
            return digitKeyCode(ch);
        }
        switch (ch) {
        case ',':
            return QuaderKeyCode::Comma;
        case '.':
            return QuaderKeyCode::Period;
        case '/':
            return QuaderKeyCode::Slash;
        case '\\':
            return QuaderKeyCode::Backslash;
        case ';':
            return QuaderKeyCode::Semicolon;
        case '\'':
            return QuaderKeyCode::Apostrophe;
        case '[':
            return QuaderKeyCode::LeftBracket;
        case ']':
            return QuaderKeyCode::RightBracket;
        case '-':
            return QuaderKeyCode::Minus;
        case '=':
            return QuaderKeyCode::Equal;
        case '`':
            return QuaderKeyCode::Grave;
        default:
            break;
        }
    }

    if (const std::optional<QuaderKeyCode> function = functionKeyCode(lower)) {
        return function;
    }
    if (const std::optional<QuaderKeyCode> numpad = numpadKeyCode(lower)) {
        return numpad;
    }

    static const std::unordered_map<std::string_view, QuaderKeyCode> namedKeys{
            {"comma", QuaderKeyCode::Comma},
            {"period", QuaderKeyCode::Period},
            {"dot", QuaderKeyCode::Period},
            {"slash", QuaderKeyCode::Slash},
            {"backslash", QuaderKeyCode::Backslash},
            {"semicolon", QuaderKeyCode::Semicolon},
            {"apostrophe", QuaderKeyCode::Apostrophe},
            {"quote", QuaderKeyCode::Apostrophe},
            {"leftbracket", QuaderKeyCode::LeftBracket},
            {"rightbracket", QuaderKeyCode::RightBracket},
            {"lbracket", QuaderKeyCode::LeftBracket},
            {"rbracket", QuaderKeyCode::RightBracket},
            {"minus", QuaderKeyCode::Minus},
            {"hyphen", QuaderKeyCode::Minus},
            {"equal", QuaderKeyCode::Equal},
            {"equals", QuaderKeyCode::Equal},
            {"grave", QuaderKeyCode::Grave},
            {"backtick", QuaderKeyCode::Grave},
            {"esc", QuaderKeyCode::Escape},
            {"escape", QuaderKeyCode::Escape},
            {"tab", QuaderKeyCode::Tab},
            {"backspace", QuaderKeyCode::Backspace},
            {"enter", QuaderKeyCode::Enter},
            {"return", QuaderKeyCode::Enter},
            {"space", QuaderKeyCode::Space},
            {"spacebar", QuaderKeyCode::Space},
            {"insert", QuaderKeyCode::Insert},
            {"ins", QuaderKeyCode::Insert},
            {"delete", QuaderKeyCode::Delete},
            {"del", QuaderKeyCode::Delete},
            {"home", QuaderKeyCode::Home},
            {"end", QuaderKeyCode::End},
            {"pageup", QuaderKeyCode::PageUp},
            {"pgup", QuaderKeyCode::PageUp},
            {"pagedown", QuaderKeyCode::PageDown},
            {"pgdn", QuaderKeyCode::PageDown},
            {"left", QuaderKeyCode::Left},
            {"right", QuaderKeyCode::Right},
            {"up", QuaderKeyCode::Up},
            {"down", QuaderKeyCode::Down},
    };
    const auto found = namedKeys.find(lower);
    return found == namedKeys.end() ? std::nullopt : std::optional<QuaderKeyCode>(found->second);
}

inline std::optional<QuaderMouseButton> mouseButtonFromToken(std::string_view lower) {
    static const std::unordered_map<std::string_view, QuaderMouseButton> buttons{
            {"leftmouse", QuaderMouseButton::Left},
            {"mouseleft", QuaderMouseButton::Left},
            {"leftbutton", QuaderMouseButton::Left},
            {"lmb", QuaderMouseButton::Left},
            {"middlemouse", QuaderMouseButton::Middle},
            {"mousemiddle", QuaderMouseButton::Middle},
            {"middlebutton", QuaderMouseButton::Middle},
            {"mmb", QuaderMouseButton::Middle},
            {"rightmouse", QuaderMouseButton::Right},
            {"mouseright", QuaderMouseButton::Right},
            {"rightbutton", QuaderMouseButton::Right},
            {"rmb", QuaderMouseButton::Right},
            {"mouse4", QuaderMouseButton::Back},
            {"mousebutton4", QuaderMouseButton::Back},
            {"backmouse", QuaderMouseButton::Back},
            {"mouseback", QuaderMouseButton::Back},
            {"backbutton", QuaderMouseButton::Back},
            {"mouse5", QuaderMouseButton::Forward},
            {"mousebutton5", QuaderMouseButton::Forward},
            {"forwardmouse", QuaderMouseButton::Forward},
            {"mouseforward", QuaderMouseButton::Forward},
            {"forwardbutton", QuaderMouseButton::Forward},
            {"mouse6", QuaderMouseButton::Button6},
            {"mousebutton6", QuaderMouseButton::Button6},
            {"mouse7", QuaderMouseButton::Button7},
            {"mousebutton7", QuaderMouseButton::Button7},
            {"mouse8", QuaderMouseButton::Button8},
            {"mousebutton8", QuaderMouseButton::Button8},
    };
    const auto found = buttons.find(lower);
    return found == buttons.end() ? std::nullopt : std::optional<QuaderMouseButton>(found->second);
}

inline std::optional<QuaderInputChord> mouseChordFromToken(std::string_view token) {
    const std::string lower = lowerCopy(trimCopy(token));
    if (lower.empty()) {
        return std::nullopt;
    }

    static const std::unordered_map<std::string_view, QuaderInputGesture> wheels{
            {"wheel", QuaderInputGesture::Wheel},
            {"mousewheel", QuaderInputGesture::Wheel},
            {"wheelup", QuaderInputGesture::WheelUp},
            {"mousewheelup", QuaderInputGesture::WheelUp},
            {"wheeldown", QuaderInputGesture::WheelDown},
            {"mousewheeldown", QuaderInputGesture::WheelDown},
            {"wheelleft", QuaderInputGesture::WheelLeft},
            {"mousewheelleft", QuaderInputGesture::WheelLeft},
            {"wheelright", QuaderInputGesture::WheelRight},
            {"mousewheelright", QuaderInputGesture::WheelRight},
    };
    if (const auto wheel = wheels.find(lower); wheel != wheels.end()) {
        return QuaderInputChord{
                .device = QuaderInputDevice::Mouse,
                .gesture = wheel->second,
        };
    }

    constexpr std::string_view dragSuffix = "drag";
    if (lower.size() > dragSuffix.size() &&
            lower.substr(lower.size() - dragSuffix.size()) == dragSuffix) {
        const std::string base = lower.substr(0u, lower.size() - dragSuffix.size());
        if (const std::optional<QuaderMouseButton> button = mouseButtonFromToken(base)) {
            return QuaderInputChord{
                    .device = QuaderInputDevice::Mouse,
                    .mouseButton = *button,
                    .gesture = QuaderInputGesture::Drag,
            };
        }
    }

    constexpr std::string_view doubleClickSuffix = "doubleclick";
    if (lower.size() > doubleClickSuffix.size() &&
            lower.substr(lower.size() - doubleClickSuffix.size()) == doubleClickSuffix) {
        const std::string base = lower.substr(0u, lower.size() - doubleClickSuffix.size());
        if (const std::optional<QuaderMouseButton> button = mouseButtonFromToken(base)) {
            return QuaderInputChord{
                    .device = QuaderInputDevice::Mouse,
                    .mouseButton = *button,
                    .gesture = QuaderInputGesture::DoubleClick,
            };
        }
    }

    if (const std::optional<QuaderMouseButton> button = mouseButtonFromToken(lower)) {
        return QuaderInputChord{
                .device = QuaderInputDevice::Mouse,
                .mouseButton = *button,
                .gesture = QuaderInputGesture::Press,
        };
    }
    return std::nullopt;
}

inline const char* keyCodeDisplayName(QuaderKeyCode key) {
    switch (key) {
    case QuaderKeyCode::A:
        return "A";
    case QuaderKeyCode::B:
        return "B";
    case QuaderKeyCode::C:
        return "C";
    case QuaderKeyCode::D:
        return "D";
    case QuaderKeyCode::E:
        return "E";
    case QuaderKeyCode::F:
        return "F";
    case QuaderKeyCode::G:
        return "G";
    case QuaderKeyCode::H:
        return "H";
    case QuaderKeyCode::I:
        return "I";
    case QuaderKeyCode::J:
        return "J";
    case QuaderKeyCode::K:
        return "K";
    case QuaderKeyCode::L:
        return "L";
    case QuaderKeyCode::M:
        return "M";
    case QuaderKeyCode::N:
        return "N";
    case QuaderKeyCode::O:
        return "O";
    case QuaderKeyCode::P:
        return "P";
    case QuaderKeyCode::Q:
        return "Q";
    case QuaderKeyCode::R:
        return "R";
    case QuaderKeyCode::S:
        return "S";
    case QuaderKeyCode::T:
        return "T";
    case QuaderKeyCode::U:
        return "U";
    case QuaderKeyCode::V:
        return "V";
    case QuaderKeyCode::W:
        return "W";
    case QuaderKeyCode::X:
        return "X";
    case QuaderKeyCode::Y:
        return "Y";
    case QuaderKeyCode::Z:
        return "Z";
    case QuaderKeyCode::Digit0:
        return "0";
    case QuaderKeyCode::Digit1:
        return "1";
    case QuaderKeyCode::Digit2:
        return "2";
    case QuaderKeyCode::Digit3:
        return "3";
    case QuaderKeyCode::Digit4:
        return "4";
    case QuaderKeyCode::Digit5:
        return "5";
    case QuaderKeyCode::Digit6:
        return "6";
    case QuaderKeyCode::Digit7:
        return "7";
    case QuaderKeyCode::Digit8:
        return "8";
    case QuaderKeyCode::Digit9:
        return "9";
    case QuaderKeyCode::Comma:
        return "Comma";
    case QuaderKeyCode::Period:
        return "Period";
    case QuaderKeyCode::Slash:
        return "Slash";
    case QuaderKeyCode::Backslash:
        return "Backslash";
    case QuaderKeyCode::Semicolon:
        return "Semicolon";
    case QuaderKeyCode::Apostrophe:
        return "Apostrophe";
    case QuaderKeyCode::LeftBracket:
        return "LeftBracket";
    case QuaderKeyCode::RightBracket:
        return "RightBracket";
    case QuaderKeyCode::Minus:
        return "Minus";
    case QuaderKeyCode::Equal:
        return "Equal";
    case QuaderKeyCode::Grave:
        return "Grave";
    case QuaderKeyCode::Escape:
        return "Escape";
    case QuaderKeyCode::Tab:
        return "Tab";
    case QuaderKeyCode::Backspace:
        return "Backspace";
    case QuaderKeyCode::Enter:
        return "Enter";
    case QuaderKeyCode::Space:
        return "Space";
    case QuaderKeyCode::Insert:
        return "Insert";
    case QuaderKeyCode::Delete:
        return "Delete";
    case QuaderKeyCode::Home:
        return "Home";
    case QuaderKeyCode::End:
        return "End";
    case QuaderKeyCode::PageUp:
        return "PageUp";
    case QuaderKeyCode::PageDown:
        return "PageDown";
    case QuaderKeyCode::Left:
        return "Left";
    case QuaderKeyCode::Right:
        return "Right";
    case QuaderKeyCode::Up:
        return "Up";
    case QuaderKeyCode::Down:
        return "Down";
    case QuaderKeyCode::F1:
        return "F1";
    case QuaderKeyCode::F2:
        return "F2";
    case QuaderKeyCode::F3:
        return "F3";
    case QuaderKeyCode::F4:
        return "F4";
    case QuaderKeyCode::F5:
        return "F5";
    case QuaderKeyCode::F6:
        return "F6";
    case QuaderKeyCode::F7:
        return "F7";
    case QuaderKeyCode::F8:
        return "F8";
    case QuaderKeyCode::F9:
        return "F9";
    case QuaderKeyCode::F10:
        return "F10";
    case QuaderKeyCode::F11:
        return "F11";
    case QuaderKeyCode::F12:
        return "F12";
    case QuaderKeyCode::F13:
        return "F13";
    case QuaderKeyCode::F14:
        return "F14";
    case QuaderKeyCode::F15:
        return "F15";
    case QuaderKeyCode::F16:
        return "F16";
    case QuaderKeyCode::F17:
        return "F17";
    case QuaderKeyCode::F18:
        return "F18";
    case QuaderKeyCode::F19:
        return "F19";
    case QuaderKeyCode::F20:
        return "F20";
    case QuaderKeyCode::F21:
        return "F21";
    case QuaderKeyCode::F22:
        return "F22";
    case QuaderKeyCode::F23:
        return "F23";
    case QuaderKeyCode::F24:
        return "F24";
    case QuaderKeyCode::Numpad0:
        return "Keypad0";
    case QuaderKeyCode::Numpad1:
        return "Keypad1";
    case QuaderKeyCode::Numpad2:
        return "Keypad2";
    case QuaderKeyCode::Numpad3:
        return "Keypad3";
    case QuaderKeyCode::Numpad4:
        return "Keypad4";
    case QuaderKeyCode::Numpad5:
        return "Keypad5";
    case QuaderKeyCode::Numpad6:
        return "Keypad6";
    case QuaderKeyCode::Numpad7:
        return "Keypad7";
    case QuaderKeyCode::Numpad8:
        return "Keypad8";
    case QuaderKeyCode::Numpad9:
        return "Keypad9";
    case QuaderKeyCode::NumpadAdd:
        return "KeypadAdd";
    case QuaderKeyCode::NumpadSubtract:
        return "KeypadSubtract";
    case QuaderKeyCode::NumpadMultiply:
        return "KeypadMultiply";
    case QuaderKeyCode::NumpadDivide:
        return "KeypadDivide";
    case QuaderKeyCode::NumpadDecimal:
        return "KeypadDecimal";
    case QuaderKeyCode::NumpadEnter:
        return "KeypadEnter";
    case QuaderKeyCode::None:
        return "";
    }
    return "";
}

inline const char* mouseButtonDisplayName(QuaderMouseButton button) {
    switch (button) {
    case QuaderMouseButton::Left:
        return "LeftMouse";
    case QuaderMouseButton::Middle:
        return "MiddleMouse";
    case QuaderMouseButton::Right:
        return "RightMouse";
    case QuaderMouseButton::Back:
        return "Mouse4";
    case QuaderMouseButton::Forward:
        return "Mouse5";
    case QuaderMouseButton::Button6:
        return "Mouse6";
    case QuaderMouseButton::Button7:
        return "Mouse7";
    case QuaderMouseButton::Button8:
        return "Mouse8";
    case QuaderMouseButton::None:
        return "";
    }
    return "";
}

inline std::string mouseChordDisplayName(const QuaderInputChord& chord) {
    switch (chord.gesture) {
    case QuaderInputGesture::Wheel:
        return "Wheel";
    case QuaderInputGesture::WheelUp:
        return "WheelUp";
    case QuaderInputGesture::WheelDown:
        return "WheelDown";
    case QuaderInputGesture::WheelLeft:
        return "WheelLeft";
    case QuaderInputGesture::WheelRight:
        return "WheelRight";
    case QuaderInputGesture::DoubleClick:
        return std::string(mouseButtonDisplayName(chord.mouseButton)) + "DoubleClick";
    case QuaderInputGesture::Drag:
        return std::string(mouseButtonDisplayName(chord.mouseButton)) + "Drag";
    case QuaderInputGesture::Press:
        return mouseButtonDisplayName(chord.mouseButton);
    }
    return {};
}

} // namespace input_binding_detail

inline std::optional<QuaderInputChord> quaderInputParseChord(std::string_view text) {
    QuaderInputChord chord;
    bool inputSeen = false;
    for (const std::string& rawPart : input_binding_detail::splitCopy(text, '+')) {
        if (rawPart.empty()) {
            return std::nullopt;
        }
        const std::string part = input_binding_detail::lowerCopy(rawPart);
        if (part == "ctrl" || part == "control" || part == "cmd" || part == "command" || part == "primary") {
            chord.primary = true;
        } else if (part == "shift") {
            chord.shift = true;
        } else if (part == "alt" || part == "option") {
            chord.alt = true;
        } else {
            if (inputSeen) {
                return std::nullopt;
            }
            if (const std::optional<QuaderInputChord> mouse = input_binding_detail::mouseChordFromToken(rawPart)) {
                chord.device = mouse->device;
                chord.mouseButton = mouse->mouseButton;
                chord.gesture = mouse->gesture;
                inputSeen = true;
            } else if (const std::optional<QuaderKeyCode> key = input_binding_detail::keyCodeFromToken(rawPart)) {
                chord.device = QuaderInputDevice::Keyboard;
                chord.key = *key;
                chord.gesture = QuaderInputGesture::Press;
                inputSeen = true;
            } else {
                return std::nullopt;
            }
        }
    }

    return inputSeen && !chord.empty() ? std::optional<QuaderInputChord>(chord) : std::nullopt;
}

inline std::optional<std::vector<QuaderInputChord>> quaderInputParseShortcut(std::string_view shortcut) {
    const std::string trimmed = input_binding_detail::trimCopy(shortcut);
    if (trimmed.empty()) {
        return std::vector<QuaderInputChord>{};
    }

    std::vector<QuaderInputChord> sequence;
    std::size_t start = 0u;
    while (start < trimmed.size()) {
        while (start < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[start])) != 0) {
            ++start;
        }
        if (start >= trimmed.size()) {
            break;
        }
        std::size_t end = start;
        while (end < trimmed.size() && std::isspace(static_cast<unsigned char>(trimmed[end])) == 0) {
            ++end;
        }

        const std::optional<QuaderInputChord> chord =
                quaderInputParseChord(std::string_view(trimmed).substr(start, end - start));
        if (!chord) {
            return std::nullopt;
        }
        sequence.push_back(*chord);
        start = end;
    }
    return sequence;
}

inline std::string quaderInputFormatChord(const QuaderInputChord& chord) {
    std::string result;
    if (chord.primary) {
        result += "Ctrl+";
    }
    if (chord.shift) {
        result += "Shift+";
    }
    if (chord.alt) {
        result += "Alt+";
    }

    if (chord.device == QuaderInputDevice::Keyboard) {
        result += input_binding_detail::keyCodeDisplayName(chord.key);
    } else if (chord.device == QuaderInputDevice::Mouse) {
        result += input_binding_detail::mouseChordDisplayName(chord);
    }
    return result;
}

inline std::string quaderInputFormatShortcut(const std::vector<QuaderInputChord>& sequence) {
    std::string result;
    for (const QuaderInputChord& chord : sequence) {
        if (!result.empty()) {
            result += ' ';
        }
        result += quaderInputFormatChord(chord);
    }
    return result;
}

inline bool quaderInputSequenceStartsWith(
        const std::vector<QuaderInputChord>& sequence,
        const std::vector<QuaderInputChord>& prefix) {
    if (prefix.size() > sequence.size()) {
        return false;
    }
    for (std::size_t index = 0u; index < prefix.size(); ++index) {
        if (!(sequence[index] == prefix[index])) {
            return false;
        }
    }
    return true;
}

inline bool quaderInputShortcutsEqual(
        const std::vector<QuaderInputChord>& left,
        const std::vector<QuaderInputChord>& right) {
    return left.size() == right.size() && quaderInputSequenceStartsWith(left, right);
}

} // namespace quader
