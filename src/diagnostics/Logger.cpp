module;

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <memory>
#include <mutex>
#include <string_view>
#include <vector>

module quader.diagnostics.logger;

namespace quader {
namespace {

std::mutex logger_mutex;
bool logger_is_initialized = false;

std::filesystem::path default_log_path()
{
    return std::filesystem::current_path() / "logs" / "quader-native.log";
}

std::vector<spdlog::sink_ptr> create_sinks()
{
    std::vector<spdlog::sink_ptr> sinks;
    sinks.push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());

    try {
        const std::filesystem::path path = default_log_path();
        std::filesystem::create_directories(path.parent_path());
        sinks.push_back(std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true));
    } catch (...) {
        // A missing log file must not stop the app from showing the real fatal message.
    }

    return sinks;
}

void log_plain_message(spdlog::level::level_enum level, std::string_view message)
{
    spdlog::log(level, "{}", message);
}

} // namespace

void Logger::initialize()
{
    const std::lock_guard lock(logger_mutex);
    if (logger_is_initialized) {
        return;
    }

    std::vector<spdlog::sink_ptr> sinks = create_sinks();
    auto logger = std::make_shared<spdlog::logger>("quader", sinks.begin(), sinks.end());

#if defined(NDEBUG)
    logger->set_level(spdlog::level::info);
#else
    logger->set_level(spdlog::level::trace);
#endif

    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] %v");
    logger->flush_on(spdlog::level::warn);
    spdlog::set_default_logger(logger);
    logger_is_initialized = true;
}

void Logger::shutdown()
{
    const std::lock_guard lock(logger_mutex);
    if (!logger_is_initialized) {
        return;
    }

    spdlog::shutdown();
    logger_is_initialized = false;
}

void Logger::trace(std::string_view message)
{
    log_plain_message(spdlog::level::trace, message);
}

void Logger::debug(std::string_view message)
{
    log_plain_message(spdlog::level::debug, message);
}

void Logger::info(std::string_view message)
{
    log_plain_message(spdlog::level::info, message);
}

void Logger::warn(std::string_view message)
{
    log_plain_message(spdlog::level::warn, message);
}

void Logger::error(std::string_view message)
{
    log_plain_message(spdlog::level::err, message);
}

void Logger::critical(std::string_view message)
{
    log_plain_message(spdlog::level::critical, message);
}

} // namespace quader
