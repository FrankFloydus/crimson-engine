////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app.hpp"
#include "core/logger.hpp"
#include "platform/fatal_error_handler.hpp"

#include <exception>
#include <filesystem>
#include <memory>
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
    }
  }
  return options;
}

} // namespace

int main(int argc, char** argv) {
  const std::filesystem::path exe = executable_path(argc, argv);
  const CommandLineOptions command_line = parse_command_line(argc, argv);
  const std::filesystem::path log_path = exe.parent_path() / "quader.log";
  quader::Logger::initialize({.file_path = log_path.string()});
  quader::install_fatal_error_handler(log_path);
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
      quader::Logger::shutdown();
      return 1;
    }

    const int result = app->run();
    app->shutdown();
    LOG_INFO("Quader shutdown complete");
    app.reset();
    quader::Logger::shutdown();
    return result;
  } catch (const std::exception &error) {
    LOG_CRITICAL("Unhandled exception in Quader main: {}", error.what());
    quader::report_fatal_error_and_exit(
        quader::FatalErrorCode::UnhandledCppException, error.what(), 1);
  } catch (...) {
    LOG_CRITICAL("Unhandled non-standard exception in Quader main");
    quader::report_fatal_error_and_exit(
        quader::FatalErrorCode::UnhandledNonStandardException,
        "Unhandled non-standard exception in Quader main.", 1);
  }
}
