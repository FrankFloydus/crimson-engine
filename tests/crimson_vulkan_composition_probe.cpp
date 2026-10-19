////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "ui/qt_vulkan_composition_widget.hpp"

#include <crimson/renderer/BorrowedVulkan.hpp>
#include <crimson/renderer/Renderer.hpp>

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QScreen>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <cmath>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

class CrimsonCompositionProbe final {
public:
  CrimsonCompositionProbe(QApplication &application, QString output_directory)
      : application_(application), output_directory_(std::move(output_directory)) {
    timer_.setInterval(50);
    QObject::connect(&timer_, &QTimer::timeout, &application_, [this] { advance(); });
  }

  ~CrimsonCompositionProbe() { root_.reset(); }

  int run() {
    if (!QDir().mkpath(output_directory_)) {
      std::cerr << "Cannot create probe output directory\n";
      return 1;
    }
    create_root();
    step_clock_.start();
    timer_.start();
    return application_.exec();
  }

private:
  enum class Step { Initial, Grow, Crop, Hidden, Reshow, Replacement };

  bool accept(crimson::shared::Status status) {
    if (status.ok()) {
      return true;
    }
    fail(status.error_message());
    return false;
  }

  void fail(std::string_view message) {
    if (error_.empty()) {
      error_ = message;
      std::cerr << "FAIL: " << error_ << std::endl;
    }
  }

  void create_root() {
    root_ = std::make_unique<QWidget>();
    root_->setWindowFlag(Qt::WindowStaysOnTopHint);
    root_->setWindowTitle(QStringLiteral("Quader Crimson Vulkan composition gate"));
    root_->resize(420, 300);
    auto *layout = new QVBoxLayout(root_.get());
    layout->setContentsMargins(0, 0, 0, 0);
    widget_ = new QtVulkanCompositionWidget(root_.get());
    layout->addWidget(widget_);
    QObject::connect(widget_, &QRhiWidget::frameSubmitted, root_.get(), [this] {
      presented_generation_ = rendered_generation_;
      presentation_clock_.restart();
    });
    widget_->set_render_callback(
        [this](const RenderVulkanCompositionTarget &destination) {
          return render(destination);
        });
    widget_->set_release_callback([this] {
      if (renderer_.initialized()) {
        if (!accept(renderer_.shutdown())) {
          qFatal("Crimson could not prove borrowed Vulkan shutdown completion");
        }
        ++release_count_;
      }
      target_ = {};
      target_extent_ = {};
    });
    ++root_count_;
    root_->show();
  }

  bool initialize_renderer() {
    const RenderVulkanSharedContext *context = widget_->shared_context();
    if (context == nullptr || !context->enabled_capabilities) {
      fail("Qt did not attest the enabled borrowed Vulkan capabilities");
      return false;
    }
    const auto &capabilities = *context->enabled_capabilities;
    std::vector<const char *> instance_extensions;
    std::vector<const char *> device_extensions;
    for (const std::string &name : capabilities.instance_extensions) {
      instance_extensions.push_back(name.c_str());
    }
    for (const std::string &name : capabilities.device_extensions) {
      device_extensions.push_back(name.c_str());
    }
    const crimson::shared::BorrowedVulkanContext borrowed{
        .instance = context->instance,
        .physicalDevice = context->physical_device,
        .device = context->device,
        .queue = context->graphics_queue,
        .queueFamilyIndex = context->graphics_queue_family,
        .queueIndex = context->graphics_queue_index,
        .apiVersion = capabilities.api_version,
        .enabledFeatures = capabilities.features,
        .maxSamplerAnisotropy = capabilities.features.samplerAnisotropy ? 8.0f : 1.0f,
        .enabledInstanceExtensionCount =
            static_cast<std::uint32_t>(instance_extensions.size()),
        .enabledInstanceExtensions = instance_extensions.data(),
        .enabledDeviceExtensionCount =
            static_cast<std::uint32_t>(device_extensions.size()),
        .enabledDeviceExtensions = device_extensions.data()};
    if (!accept(renderer_.initialize(borrowed))) {
      return false;
    }
    std::cout << "backend=Crimson/bgfx Vulkan; borrowed_qt_device="
              << context->device << "; root=" << root_count_ << std::endl;
    return true;
  }

  bool render(const RenderVulkanCompositionTarget &destination) {
    if (!error_.empty()) {
      return false;
    }
    if (!renderer_.initialized() && !initialize_renderer()) {
      return false;
    }
    const bool cropped = step_ == Step::Crop;
    const std::uint32_t source_x = cropped ? 32 : 0;
    const std::uint32_t source_y = cropped ? 24 : 0;
    const crimson::shared::Extent required_extent{
        destination.width + source_x + 16,
        destination.height + source_y + 16};
    if (target_.value == 0) {
      if (!accept(renderer_.create_target(required_extent, target_))) {
        return false;
      }
      target_extent_ = required_extent;
    } else if (target_extent_ != required_extent) {
      if (!accept(renderer_.resize_target(target_, required_extent))) {
        return false;
      }
      target_extent_ = required_extent;
    }
    if (!rejection_checked_) {
      if (renderer_.resize_target(target_, {}).ok()) {
        fail("An invalid target replacement was accepted");
        return false;
      }
      rejection_checked_ = true;
    }
    if (!accept(renderer_.clear(target_, color_, generation_))) {
      return false;
    }
    if (cropped &&
        (!accept(renderer_.clear_region(target_, {0, 0, 64, 48}, {1, 0, 0, 1},
                                       generation_)) ||
         !accept(renderer_.clear_region(target_, {64, 0, 32, 96}, {0, 1, 0, 1},
                                       generation_)))) {
      return false;
    }
    if (!accept(renderer_.submit())) {
      return false;
    }
    const crimson::shared::VulkanImageTarget image{
        .image = destination.image,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .oldLayout = destination.old_layout,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .width = destination.width,
        .height = destination.height,
        .sourceX = source_x,
        .sourceY = source_y};
    if (!accept(renderer_.copy_to_vulkan(target_, image, last_submission_))) {
      return false;
    }
    if (last_submission_.generation != generation_ || last_submission_.serial == 0) {
      fail("Copy did not identify the accepted frame generation");
      return false;
    }
    rendered_generation_ = generation_;
    ++render_count_;
    return true;
  }

  bool verify_image(const QImage &pixels, const QString &name) {
    if (pixels.isNull()) {
      fail("Qt returned an empty composition readback");
      return false;
    }
    const std::array positions{
        QPoint(2, 2), QPoint(33, 10), QPoint(10, 20), QPoint(10, 26),
        QPoint(pixels.width() / 2, pixels.height() / 2),
        QPoint(pixels.width() - 3, pixels.height() - 3)};
    for (const QPoint position : positions) {
      crimson::shared::Color expected_color = color_;
      if (step_ == Step::Crop) {
        const int source_x = position.x() + 32;
        const int source_y = position.y() + 24;
        if (source_x < 64 && source_y < 48) {
          expected_color = {1, 0, 0, 1};
        } else if (source_x >= 64 && source_x < 96 && source_y < 96) {
          expected_color = {0, 1, 0, 1};
        }
      }
      const std::array expected{
          static_cast<int>(std::lround(expected_color.red * 255.0f)),
          static_cast<int>(std::lround(expected_color.green * 255.0f)),
          static_cast<int>(std::lround(expected_color.blue * 255.0f))};
      const QColor actual = pixels.pixelColor(position);
      if (std::abs(actual.red() - expected[0]) > 2 ||
          std::abs(actual.green() - expected[1]) > 2 ||
          std::abs(actual.blue() - expected[2]) > 2 || actual.alpha() != 255) {
        std::cerr << "capture=" << name.toStdString() << "; pixel="
                  << actual.red() << ',' << actual.green() << ','
                  << actual.blue() << ',' << actual.alpha() << std::endl;
        fail("Composited GPU pixels disagree with the submitted clear");
        return false;
      }
    }
    const QString path = QDir(output_directory_).filePath(
        QStringLiteral("step-%1-%2.png").arg(static_cast<int>(step_)).arg(name));
    if (!pixels.save(path)) {
      fail("Cannot save composition evidence");
      return false;
    }
    std::cout << "PASS capture=" << name.toStdString()
              << "; step=" << static_cast<int>(step_)
              << "; generation=" << last_submission_.generation
              << "; serial=" << last_submission_.serial
              << "; pixels=" << pixels.width() << 'x' << pixels.height()
              << "; gpu_complete=" << renderer_.is_complete(last_submission_)
              << std::endl;
    return true;
  }

  bool verify_pixels() {
    // Capture the presented client before grabFramebuffer(), which renders a
    // fresh offscreen frame. Both checks are confined to this verification app.
    QWindow *window = root_->windowHandle();
    if (window == nullptr || window->screen() == nullptr) {
      fail("The composition root has no screen");
      return false;
    }
    QScreen *screen = window->screen();
    const QPoint screen_position =
        root_->mapToGlobal(QPoint{}) - screen->geometry().topLeft();
    const QImage presented = screen
                                 ->grabWindow(0, screen_position.x(), screen_position.y(),
                                              root_->width(), root_->height())
                                 .toImage();
    const QImage offscreen = widget_->grabFramebuffer();
    presented.save(QDir(output_directory_).filePath(QStringLiteral("presented-diagnostic.png")));
    offscreen.save(QDir(output_directory_).filePath(QStringLiteral("offscreen-diagnostic.png")));
    if (!verify_image(offscreen, QStringLiteral("offscreen"))) {
      return false;
    }
    return verify_image(presented, QStringLiteral("presented"));
  }

  void next(Step step, crimson::shared::Color color) {
    step_ = step;
    color_ = color;
    ++generation_;
    step_clock_.restart();
    if (widget_ != nullptr) {
      widget_->update();
    }
  }

  void advance() {
    if (!error_.empty() || step_clock_.elapsed() > 20000) {
      if (error_.empty()) {
        fail("Timed out waiting for a current Qt/Crimson composition generation");
      }
      root_.reset();
      timer_.stop();
      application_.exit(1);
      return;
    }
    if (step_ == Step::Hidden) {
      root_->show();
      next(Step::Reshow, {1, 1, 0, 1});
      return;
    }
    if (presented_generation_ != generation_ ||
        presentation_clock_.elapsed() < 100 || !verify_pixels()) {
      return;
    }
    switch (step_) {
    case Step::Initial:
      root_->resize(640, 420);
      next(Step::Grow, {1, 0, 1, 1});
      break;
    case Step::Grow:
      root_->resize(300, 210);
      next(Step::Crop, {0.125f, 0.5f, 0.75f, 1});
      break;
    case Step::Crop:
      root_->hide();
      step_ = Step::Hidden;
      step_clock_.restart();
      break;
    case Step::Reshow:
      root_.reset();
      widget_ = nullptr;
      if (release_count_ != 1) {
        fail("The first root did not release the borrowed renderer");
        break;
      }
      next(Step::Replacement, {0, 1, 0, 1});
      create_root();
      break;
    case Step::Replacement:
      root_.reset();
      timer_.stop();
      if (release_count_ != 2) {
        fail("The replacement root did not release the borrowed renderer");
      }
      std::cout << "roots=" << root_count_ << "; releases=" << release_count_
                << "; rendered_frames=" << render_count_ << std::endl;
      application_.exit(error_.empty() ? 0 : 1);
      break;
    case Step::Hidden:
      break;
    }
  }

  QApplication &application_;
  QString output_directory_;
  QTimer timer_;
  QElapsedTimer step_clock_;
  QElapsedTimer presentation_clock_;
  std::unique_ptr<QWidget> root_;
  QtVulkanCompositionWidget *widget_ = nullptr;
  crimson::shared::Renderer renderer_;
  crimson::shared::TargetId target_;
  crimson::shared::Extent target_extent_;
  crimson::shared::Submission last_submission_;
  crimson::shared::Color color_{0, 1, 1, 1};
  std::uint64_t generation_ = 1;
  std::uint64_t rendered_generation_ = 0;
  std::uint64_t presented_generation_ = 0;
  std::uint64_t render_count_ = 0;
  std::uint32_t root_count_ = 0;
  std::uint32_t release_count_ = 0;
  Step step_ = Step::Initial;
  bool rejection_checked_ = false;
  std::string error_;
};

} // namespace
} // namespace quader

int main(int argc, char **argv) {
  QApplication::setAttribute(Qt::AA_DontCreateNativeWidgetSiblings);
  QApplication application(argc, argv);
  application.setQuitOnLastWindowClosed(false);
  QCommandLineParser parser;
  parser.addHelpOption();
  const QCommandLineOption output_option(
      QStringLiteral("output-dir"), QStringLiteral("Probe evidence directory."),
      QStringLiteral("directory"));
  parser.addOption(output_option);
  parser.process(application);
  const QString output_directory = parser.value(output_option);
  if (output_directory.isEmpty()) {
    std::cerr << "--output-dir must select the composition evidence directory\n";
    return 2;
  }
  quader::CrimsonCompositionProbe probe(application, output_directory);
  return probe.run();
}
