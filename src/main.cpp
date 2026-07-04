////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app.hpp"
#include "core/logger.hpp"
#include "core/user_config_paths.hpp"
#include "platform/fatal_error_handler.hpp"
#include "platform/sentry_client.hpp"
#include "quader_app_version.hpp"

#include <exception>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace {

std::filesystem::path executable_path(int argc, char **argv) {
  if (argc > 0 && argv && argv[0]) {
    return std::filesystem::absolute(argv[0]);
  }

#if defined(_WIN32)
    wchar_t path[MAX_PATH] = {};
    const DWORD count = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (count > 0) {
        return std::filesystem::path(path);
    }
#endif

    return std::filesystem::current_path() / "quader.exe";
}

/**
 * Stores the Command Line Options data contract used by the Quader native app.
 */
struct CommandLineOptions {
  bool borderless_window = true;
  quader::RendererBackend render_backend = quader::RendererBackend::Vulkan;
  bool sentry_verify = false;
};

CommandLineOptions parse_command_line(int argc, char **argv) {
  CommandLineOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index] != nullptr
                                          ? std::string_view(argv[index])
                                          : std::string_view();
    if (argument == "--borderless-window" ||
        argument == "--window-borderless") {
      options.borderless_window = true;
    } else if (argument == "--native-window" ||
               argument == "--no-borderless-window") {
      options.borderless_window = false;
    } else if (argument == "--vulkan" ||
               argument == "--render-backend=vulkan") {
      options.render_backend = quader::RendererBackend::Vulkan;
    } else if (argument == "--opengl" ||
               argument == "--render-backend=opengl") {
      options.render_backend = quader::RendererBackend::OpenGL;
    } else if (argument == "--sentry-verify" ||
               argument == "--sentry-test-event") {
      options.sentry_verify = true;
    }
  }
  return options;
}

std::filesystem::path
sentry_database_path(const std::filesystem::path &executable_dir) {
  return quader::quader_user_config_directory(executable_dir) / "sentry-native";
}

std::filesystem::path
sentry_handler_path(const std::filesystem::path &executable_dir) {
#if defined(_WIN32)
  return executable_dir / "crashpad_handler.exe";
#else
  return executable_dir / "crashpad_handler";
#endif
}

std::string sentry_release_name() {
  return std::string{"quader@"} + quader::kQuaderAppVersion;
}

std::string sentry_environment_name() {
#if defined(NDEBUG)
  return "release";
#else
  return "debug";
#endif
}

bool sentry_debug_enabled() {
#if defined(NDEBUG)
  return false;
#else
  return true;
#endif
}

} // namespace

int main(int argc, char** argv) {
  const std::filesystem::path exe = executable_path(argc, argv);
  const CommandLineOptions command_line = parse_command_line(argc, argv);
  const std::filesystem::path executable_dir = exe.parent_path();
  const std::filesystem::path log_path = executable_dir / "quader.log";
  quader::Logger::initialize({.file_path = log_path.string()});
  quader::install_fatal_error_handler(log_path);
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
  (void)quader::initialize_sentry_client({
      .dsn = std::string{quader::kQuaderSentryDsn},
      .database_path = sentry_database_path(executable_dir),
      .handler_path = sentry_handler_path(executable_dir),
      .release = sentry_release_name(),
      .environment = sentry_environment_name(),
      .debug = sentry_debug_enabled(),
  });
  if (command_line.sentry_verify) {
    quader::capture_sentry_message(quader::SentryEventLevel::Info, "custom",
                                   "It works!");
  }
#endif
  LOG_INFO("Booting Quader from {}", exe.string());
  if (command_line.borderless_window) {
    LOG_INFO("Borderless window mode enabled");
  }
  LOG_INFO("Render backend: {}",
           quader::to_string(command_line.render_backend));

  try {
    auto app = std::make_unique<quader::App>();
    if (!app->initialize(exe,
                         {.borderless_window = command_line.borderless_window,
                          .render_backend = command_line.render_backend})) {
      LOG_CRITICAL("Application initialization failed");
      app->shutdown();
      app.reset();
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
      quader::shutdown_sentry_client();
#endif
      quader::Logger::shutdown();
      return 1;
    }

    const int result = app->run();
    app->shutdown();
    LOG_INFO("Quader shutdown complete");
    app.reset();
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
    quader::shutdown_sentry_client();
#endif
    quader::Logger::shutdown();
    return result;
  } catch (const std::exception &error) {
    LOG_CRITICAL("Unhandled exception in Quader main: {}", error.what());
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
    const std::string message =
        std::string{"Unhandled exception in Quader main: "} + error.what();
    quader::capture_sentry_message(quader::SentryEventLevel::Fatal, "fatal",
                                   message);
    quader::shutdown_sentry_client();
#endif
    quader::report_fatal_error_and_exit(
        quader::FatalErrorCode::UnhandledCppException, error.what(), 1);
  } catch (...) {
    LOG_CRITICAL("Unhandled non-standard exception in Quader main");
#if defined(QUADER_ENABLE_SENTRY) && QUADER_ENABLE_SENTRY
    quader::capture_sentry_message(
        quader::SentryEventLevel::Fatal, "fatal",
        "Unhandled non-standard exception in Quader main.");
    quader::shutdown_sentry_client();
#endif
    quader::report_fatal_error_and_exit(
        quader::FatalErrorCode::UnhandledNonStandardException,
        "Unhandled non-standard exception in Quader main.", 1);
  }
}
