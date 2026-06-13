module;

#include <format>
#include <string>
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
        requires(sizeof...(Args) > 0)
    static void trace(std::format_string<Args...> message, Args&&... args)
    {
        trace(std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void debug(std::format_string<Args...> message, Args&&... args)
    {
        debug(std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void info(std::format_string<Args...> message, Args&&... args)
    {
        info(std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void warn(std::format_string<Args...> message, Args&&... args)
    {
        warn(std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void error(std::format_string<Args...> message, Args&&... args)
    {
        error(std::format(message, std::forward<Args>(args)...));
    }

    template <typename... Args>
        requires(sizeof...(Args) > 0)
    static void critical(std::format_string<Args...> message, Args&&... args)
    {
        critical(std::format(message, std::forward<Args>(args)...));
    }

private:
    Logger() = delete;
};

class LoggerSession final {
public:
    LoggerSession();

    LoggerSession(const LoggerSession&) = delete;
    LoggerSession& operator=(const LoggerSession&) = delete;

    ~LoggerSession();
};

} // namespace quader
