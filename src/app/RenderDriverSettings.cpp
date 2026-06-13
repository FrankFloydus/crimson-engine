module;

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <ios>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

module quader.app.render_driver_settings;

namespace quader {
namespace {

constexpr std::string_view kRenderDriverSettingsKey = "render_driver";

[[nodiscard]] char ascii_lower(char value) noexcept
{
    if (value >= 'A' && value <= 'Z') {
        return static_cast<char>(value - 'A' + 'a');
    }

    return value;
}

[[nodiscard]] bool ascii_equals_ignore_case(std::string_view left, std::string_view right) noexcept
{
    if (left.size() != right.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.size(); ++index) {
        if (ascii_lower(left[index]) != ascii_lower(right[index])) {
            return false;
        }
    }

    return true;
}

[[nodiscard]] bool is_ascii_space(char value) noexcept
{
    return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
        value == '\f' || value == '\v';
}

[[nodiscard]] std::string_view trim_ascii_space(std::string_view value) noexcept
{
    while (!value.empty() && is_ascii_space(value.front())) {
        value.remove_prefix(1);
    }

    while (!value.empty() && is_ascii_space(value.back())) {
        value.remove_suffix(1);
    }

    return value;
}

[[nodiscard]] std::string_view next_line(std::string_view& remaining) noexcept
{
    const std::size_t newline = remaining.find('\n');
    if (newline == std::string_view::npos) {
        const std::string_view line = remaining;
        remaining = {};
        return line;
    }

    const std::string_view line = remaining.substr(0, newline);
    remaining.remove_prefix(newline + 1);
    return line;
}

[[nodiscard]] std::string_view strip_inline_comment(std::string_view value) noexcept
{
    const std::size_t comment = value.find_first_of("#;");
    if (comment == std::string_view::npos) {
        return value;
    }

    return value.substr(0, comment);
}

[[nodiscard]] bool is_comment_line(std::string_view line) noexcept
{
    return !line.empty() && (line.front() == '#' || line.front() == ';');
}

[[nodiscard]] bool is_render_driver_settings_key(std::string_view key) noexcept
{
    return ascii_equals_ignore_case(key, kRenderDriverSettingsKey) ||
        ascii_equals_ignore_case(key, "driver");
}

[[nodiscard]] std::optional<RenderDriverSelection> parse_render_driver_value(
    std::string_view value) noexcept
{
    value = trim_ascii_space(strip_inline_comment(value));
    if (const std::optional<RenderDriverKind> driver = parse_render_driver_kind(value)) {
        return RenderDriverSelection {
            .requested_driver = *driver,
        };
    }

    return std::nullopt;
}

[[nodiscard]] std::string to_string(std::string_view value)
{
    return std::string(value.data(), value.size());
}

[[nodiscard]] std::string ios_failure_message(const std::ios_base::failure& failure)
{
    std::string message = failure.what();
    if (failure.code()) {
        message += " (";
        message += failure.code().message();
        message += ")";
    }
    return message;
}

void throw_filesystem_failure(
    std::string_view action,
    const std::filesystem::path& path,
    const std::error_code& error)
{
    throw std::runtime_error(
        to_string(action) + " '" + path.string() + "': " + error.message());
}

void ensure_parent_directory_exists(const std::string& settings_file)
{
    const std::filesystem::path settings_path(settings_file);
    const std::filesystem::path parent = settings_path.parent_path();
    if (parent.empty()) {
        return;
    }

    std::error_code error;
    const bool parent_exists = std::filesystem::exists(parent, error);
    if (error) {
        throw_filesystem_failure("Failed to inspect render driver settings directory", parent, error);
    }

    if (parent_exists) {
        const bool parent_is_directory = std::filesystem::is_directory(parent, error);
        if (error) {
            throw_filesystem_failure("Failed to inspect render driver settings directory", parent, error);
        }
        if (!parent_is_directory) {
            throw std::runtime_error(
                "Render driver settings path parent exists but is not a directory: " +
                parent.string());
        }
        return;
    }

    if (!std::filesystem::create_directories(parent, error) && error) {
        throw_filesystem_failure("Failed to create render driver settings directory", parent, error);
    }

    const bool created_parent_is_directory = std::filesystem::is_directory(parent, error);
    if (error) {
        throw_filesystem_failure("Failed to verify render driver settings directory", parent, error);
    }
    if (!created_parent_is_directory) {
        throw std::runtime_error(
            "Render driver settings directory was not created: " + parent.string());
    }
}

} // namespace

std::optional<RenderDriverSelection> parse_render_driver_settings(
    std::string_view contents) noexcept
{
    while (!contents.empty()) {
        std::string_view line = trim_ascii_space(next_line(contents));
        if (line.empty() || is_comment_line(line)) {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            return parse_render_driver_value(line);
        }

        const std::string_view key = trim_ascii_space(line.substr(0, equals));
        if (!is_render_driver_settings_key(key)) {
            continue;
        }

        return parse_render_driver_value(line.substr(equals + 1));
    }

    return std::nullopt;
}

std::string serialize_render_driver_settings(RenderDriverSelection selection)
{
    std::string contents;
    contents.reserve(kRenderDriverSettingsKey.size() + 1 + 10 + 1);
    contents += kRenderDriverSettingsKey;
    contents += '=';
    contents += render_driver_kind_token(selection.requested_driver);
    contents += '\n';
    return contents;
}

RenderDriverSettingsStore::RenderDriverSettingsStore(std::string settings_file)
    : settings_file_(std::move(settings_file))
{
    if (settings_file_.empty()) {
        throw std::invalid_argument("Render driver settings file path is empty.");
    }
}

std::string_view RenderDriverSettingsStore::settings_file() const noexcept
{
    return settings_file_;
}

RenderDriverSelection RenderDriverSettingsStore::load_selection() const
{
    std::ifstream input(settings_file_, std::ios::binary);
    if (!input.is_open()) {
        return RenderDriverSelection {};
    }

    std::string contents(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (input.bad()) {
        return RenderDriverSelection {};
    }

    if (const std::optional<RenderDriverSelection> parsed =
            parse_render_driver_settings(contents)) {
        return *parsed;
    }

    return RenderDriverSelection {};
}

void RenderDriverSettingsStore::save_selection(RenderDriverSelection selection) const
{
    ensure_parent_directory_exists(settings_file_);

    std::ofstream output;
    output.exceptions(std::ios::badbit | std::ios::failbit);

    try {
        output.open(settings_file_, std::ios::binary | std::ios::trunc);
        const std::string contents = serialize_render_driver_settings(selection);
        output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        output.flush();
    } catch (const std::ios_base::failure& failure) {
        throw std::runtime_error(
            "Failed to save render driver settings file '" + settings_file_ +
            "': " + ios_failure_message(failure));
    }
}

} // namespace quader
