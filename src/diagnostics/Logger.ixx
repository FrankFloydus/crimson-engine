module;

#include <spdlog/spdlog.h>

#include <string_view>
#include <utility>

export module quader.diagnostics.logger;

export namespace quader {

class Logger final {
public:
    static void initialize();
    static void shutdown();

    static void trace(std::string_view message);
    static void debug(std::string_view message);
    static void info(std::string_view message);
    static void warn(std::string_view message);
    static void error(std::string_view message);
    static void critical(std::string_view message);

    template <typename... Args>
    static void trace(spdlog::format_string_t<Args...> message, Args&&... args)
    {
        spdlog::trace(message, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void debug(spdlog::format_string_t<Args...> message, Args&&... args)
    {
        spdlog::debug(message, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void info(spdlog::format_string_t<Args...> message, Args&&... args)
    {
        spdlog::info(message, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void warn(spdlog::format_string_t<Args...> message, Args&&... args)
    {
        spdlog::warn(message, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void error(spdlog::format_string_t<Args...> message, Args&&... args)
    {
        spdlog::error(message, std::forward<Args>(args)...);
    }

    template <typename... Args>
    static void critical(spdlog::format_string_t<Args...> message, Args&&... args)
    {
        spdlog::critical(message, std::forward<Args>(args)...);
    }

private:
    Logger() = delete;
};

} // namespace quader
