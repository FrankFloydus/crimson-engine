////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "app/app.hpp"
#if defined(QUADER_HAS_CRIMSON_DIAGNOSTICS)
#include "ui/qt_crimson_instancing_probe.hpp"
#endif
#include "core/logger.hpp"
#include "core/release_features.hpp"
#include "core/user_config_paths.hpp"
#include "platform/fatal_error_handler.hpp"
#include "platform/sentry_client.hpp"
#include "quader_app_version.hpp"
#include "ui/qt_application_cursor.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_numeric_spin_box.hpp"

#include <QApplication>
#include <QIcon>
#include <QLocale>
#include <QSize>
#include <QStyle>
#include <QStyleFactory>
#include <QVariant>

#include <exception>
#include <charconv>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <Windows.h>
#endif

namespace {

QIcon quader_application_icon() {
#if defined(CRIMSON_ENGINE_APPLICATION)
  return QIcon(QStringLiteral(":/crimson/crimson-engine.ico"));
#else
  QIcon icon;
  icon.addFile(QStringLiteral(":/quader/icons/quader_16.png"), QSize(16, 16));
  icon.addFile(QStringLiteral(":/quader/icons/quader_32.png"), QSize(32, 32));
  icon.addFile(QStringLiteral(":/quader/icons/quader_48.png"), QSize(48, 48));
  icon.addFile(QStringLiteral(":/quader/icons/quader_64.png"), QSize(64, 64));
  icon.addFile(QStringLiteral(":/quader/icons/quader_256.png"),
               QSize(256, 256));
  return icon;
#endif
}

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
  quader::RendererBackend render_backend = quader::RendererBackend::Vulkan;
  std::optional<quader::RenderCompositionApi> composition_api;
  std::string profile_mode,error;
  std::uint32_t profile_duration{15};
  bool profile_duration_explicit{};
  std::filesystem::path profile_output,frame_profile;
  bool sentry_verify = false;
  std::filesystem::path crimson_instancing_output;
};

CommandLineOptions parse_command_line(int argc, char **argv) {
  CommandLineOptions options;
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument = argv[index] != nullptr
                                          ? std::string_view(argv[index])
                                          : std::string_view();
    if(argument=="--profile"||argument=="--profile-duration"||argument=="--profile-output"||argument=="--frame-profile"){
      if(index+1>=argc||argv[index+1]==nullptr){options.error="Missing value for "+std::string(argument);break;}
      const std::string_view value(argv[++index]);
      if(argument=="--profile"){if(value!="startup"&&value!="editor"&&value!="gameplay")options.error="Profile mode must be startup, editor or gameplay.";else options.profile_mode=std::string(value);}
      else if(argument=="--profile-duration"){std::uint32_t seconds{};const auto parsed=std::from_chars(value.data(),value.data()+value.size(),seconds);if(parsed.ec!=std::errc{}||parsed.ptr!=value.data()+value.size()||seconds<1||seconds>3600)options.error="Profile duration must be between 1 and 3600 seconds.";else {options.profile_duration=seconds;options.profile_duration_explicit=true;}}
      else if(value.empty())options.error="Profile output paths cannot be empty.";
      else if(argument=="--profile-output")options.profile_output=std::filesystem::u8path(value);
      else options.frame_profile=std::filesystem::u8path(value);
    } else if(argument=="--renderer"||argument.starts_with("--renderer=")){
      std::string_view backend;if(argument=="--renderer"){if(index+1>=argc){options.error="Missing value for --renderer.";break;}backend=argv[++index];}else backend=argument.substr(11);
      if(backend=="dx11")options.composition_api=quader::RenderCompositionApi::Direct3D11;
      else if(backend=="dx12")options.composition_api=quader::RenderCompositionApi::Direct3D12;
      else options.error="Renderer must be dx11 or dx12.";
    } else if (argument == "--vulkan" ||
               argument == "--render-backend=vulkan") {
      options.render_backend = quader::RendererBackend::Vulkan;
    } else if (argument == "--opengl" ||
               argument == "--render-backend=opengl") {
      // The public host composes through Qt's Vulkan device. Retain the
      // legacy backend selector for the Full host that implements it.
      if (quader::current_product_release() == quader::kInternalFullRelease) {
        options.render_backend = quader::RendererBackend::OpenGL;
      }
    } else if (argument == "--sentry-verify" ||
               argument == "--sentry-test-event") {
      options.sentry_verify = true;
    } else if (argument.starts_with("--crimson-instancing-test-output=")) {
      options.crimson_instancing_output = std::filesystem::path(
          std::string(argument.substr(std::string_view("--crimson-instancing-test-output=").size())));
    }
  }
  if(options.profile_duration_explicit&&options.profile_mode.empty())options.error="--profile-duration requires --profile startup, editor or gameplay.";
  if(!options.profile_duration_explicit&&(options.profile_mode=="editor"||options.profile_mode=="gameplay"))options.profile_duration=10;
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
#if defined(CRIMSON_ENGINE_APPLICATION)
  const char* app_data=std::getenv("LOCALAPPDATA");
  quader::initialize_user_config_directory(app_data&&*app_data
      ?std::filesystem::absolute(std::filesystem::path(app_data)/"CrimsonEngine")
      :std::filesystem::absolute(executable_dir/"settings"));
#endif
  const std::filesystem::path log_path =
      executable_dir /
#if defined(CRIMSON_ENGINE_APPLICATION)
      "renderer.log";
#else
      (quader::current_product_release() == quader::kInternalFullRelease
           ? "quader.log"
           : "quader-" + quader::product_release_label(
                             quader::current_product_release()) + ".log");
#endif
  quader::Logger::initialize({.file_path = log_path.string()});
  quader::install_fatal_error_handler(log_path);
  quader::install_qt_fatal_error_handler();
  if(!command_line.error.empty()){LOG_ERROR("{}",command_line.error);quader::Logger::shutdown();return 2;}

  // Quader's numeric editing contract is locale-independent. Installing the
  // C locale before QApplication construction makes every current and future
  // Qt decimal control use '.' without affecting translated UI catalogs.
  QLocale::setDefault(QLocale::c());
  QApplication qt_application(argc, argv);
#if defined(CRIMSON_ENGINE_APPLICATION)
  qt_application.setApplicationName(QStringLiteral("Crimson Engine"));
  qt_application.setOrganizationName(QStringLiteral("CrimsonEngine"));
  qt_application.setApplicationDisplayName(QStringLiteral("Crimson Engine"));
  qt_application.setProperty("crimsonEngineApplication",true);
#else
  qt_application.setApplicationName(QStringLiteral("Quader"));
  qt_application.setOrganizationName(QStringLiteral("Quader"));
#endif
  QStyle *fusion_style = QStyleFactory::create(QStringLiteral("Fusion"));
  if (fusion_style == nullptr) {
    return 1;
  }
  QApplication::setStyle(fusion_style);
  // Fusion keeps native widget metrics and interaction behavior while the
  // application palette provides Quader's deterministic dark presentation.
  quader::apply_qt_obsidian_application_theme(
      fusion_style->standardPalette());
  quader::install_qt_application_default_cursor(qt_application);
  quader::qt_install_default_numeric_spin_boxes(qt_application);
  const QIcon application_icon = quader_application_icon();
  if (application_icon.isNull()) {
    return 1;
  }
  qt_application.setWindowIcon(application_icon);

#if defined(QUADER_HAS_CRIMSON_DIAGNOSTICS)
  if (!command_line.crimson_instancing_output.empty()) {
    const int result = quader::run_qt_crimson_instancing_probe(
        qt_application, command_line.crimson_instancing_output, executable_dir);
    quader::Logger::shutdown();
    return result;
  }
#endif

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

  try {
    auto app = std::make_unique<quader::App>();
    if (!app->initialize(qt_application, exe,
                         {.render_backend = command_line.render_backend,
                          .composition_api = command_line.composition_api,
                          .profile_mode = command_line.profile_mode,
                          .profile_duration = command_line.profile_duration,
                          .profile_output = command_line.profile_output,
                          .frame_profile = command_line.frame_profile})) {
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
