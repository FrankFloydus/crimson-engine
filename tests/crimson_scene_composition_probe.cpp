////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "ui/qt_vulkan_composition_widget.hpp"

#include <crimson/renderer/BorrowedVulkan.hpp>
#include <crimson/renderer/EditorRendering.hpp>
#include <crimson/renderer/Scene.hpp>

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
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <numbers>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

using ScenePosition = crimson::shared::Float3;
using SceneAttribute = crimson::shared::VertexAttributes;

int display_channel(float linear) {
  const float encoded = linear <= 0.0031308f
      ? 12.92f * linear
      : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
  return static_cast<int>(std::lround(std::clamp(encoded, 0.0f, 1.0f) * 255.0f));
}

float linear_channel(int encoded) {
  const float value = static_cast<float>(encoded) / 255.0f;
  return value <= 0.04045f ? value / 12.92f
                         : std::pow((value + 0.055f) / 1.055f, 2.4f);
}

template <class Value>
crimson::shared::PinnedStream<Value> pinned(
    const std::shared_ptr<const std::vector<Value>> &values,
    std::uint64_t revision = 1u) {
  return {std::span<const Value>(*values), values, revision};
}

/** Verifies scene publication with real borrowed-device rendering and Qt output. */
class CrimsonSceneCompositionProbe final {
public:
  CrimsonSceneCompositionProbe(QApplication &application, QString output_directory)
      : application_(application), output_directory_(std::move(output_directory)) {
    positions_ = std::make_shared<const std::vector<ScenePosition>>(
        std::vector<ScenePosition>{{-0.75f, -0.75f, 0.5f}, {0.75f, -0.75f, 0.5f},
                                   {0.75f, 0.75f, 0.5f}, {-0.75f, 0.75f, 0.5f}});
    front_indices_ = std::make_shared<const std::vector<std::uint32_t>>(
        std::vector<std::uint32_t>{0, 1, 2, 0, 2, 3});
    back_indices_ = std::make_shared<const std::vector<std::uint32_t>>(
        std::vector<std::uint32_t>{0, 2, 1, 0, 3, 2});
    invalid_indices_ = std::make_shared<const std::vector<std::uint32_t>>(
        std::vector<std::uint32_t>{0, 1, 99});
    std::vector<SceneAttribute> attributes(4);
    const std::array<crimson::shared::Float2, 4> uv{{{0, 1}, {1, 1}, {1, 0}, {0, 0}}};
    for (std::size_t index = 0; index < attributes.size(); ++index) {
      attributes[index].normal = {0, 0, 1};
      attributes[index].geometric_normal = {0, 0, 1};
      attributes[index].uv0 = uv[index];
    }
    attributes_ = std::make_shared<const std::vector<SceneAttribute>>(attributes);
    for (SceneAttribute &attribute : attributes) {
      attribute.normal = {1, 1, 1};
    }
    angled_attributes_ = std::make_shared<const std::vector<SceneAttribute>>(std::move(attributes));
    std::vector<SceneAttribute> circular(*attributes_);
    std::vector<SceneAttribute> quad(*attributes_);
    for (SceneAttribute &attribute : circular) {
      attribute.uv0 = {0.2f, 0.25f};
      attribute.interpolation_coordinates = {0.3f, 0.4f, 0.1f, 0};
      attribute.interpolation_correction = {1, 0, 0, 0};
      attribute.interpolation_mode = 1.0f;
    }
    for (SceneAttribute &attribute : quad) {
      attribute.uv0 = {0.4f, 0.25f};
      attribute.interpolation_coordinates = {0.4f, 0.5f, 0, 0};
      attribute.interpolation_correction = {0, 0, 1, 0};
      attribute.interpolation_mode = 2.0f;
    }
    circular_attributes_ = std::make_shared<const std::vector<SceneAttribute>>(std::move(circular));
    quad_attributes_ = std::make_shared<const std::vector<SceneAttribute>>(std::move(quad));
    std::vector<std::uint8_t> image(8u * 8u * 4u);
    for (std::size_t y = 0; y < 8; ++y) {
      for (std::size_t x = 0; x < 8; ++x) {
        const auto &color = kImageColors[(y >= 4 ? 2u : 0u) + (x >= 4 ? 1u : 0u)];
        const std::size_t offset = (y * 8u + x) * 4u;
        for (std::size_t channel = 0; channel < 3; ++channel) {
          image[offset + channel] = static_cast<std::uint8_t>(color[channel]);
        }
        image[offset + 3] = 255u;
      }
    }
    image_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(image));
    alpha_image_ = std::make_shared<const std::vector<std::uint8_t>>(
        std::vector<std::uint8_t>{255u, 255u, 255u, 128u});
    std::vector<std::uint8_t> checker(64u * 64u * 4u);
    for (std::size_t y = 0; y < 64u; ++y) {
      for (std::size_t x = 0; x < 64u; ++x) {
        const auto value = static_cast<std::uint8_t>((x + y) % 2u ? 255u : 0u);
        const std::size_t offset = (y * 64u + x) * 4u;
        checker[offset] = checker[offset + 1u] = checker[offset + 2u] = value;
        checker[offset + 3u] = 255u;
      }
    }
    minify_image_ = std::make_shared<const std::vector<std::uint8_t>>(std::move(checker));
    timer_.setInterval(50);
    QObject::connect(&timer_, &QTimer::timeout, &application_, [this] { advance(); });
  }

  ~CrimsonSceneCompositionProbe() { root_.reset(); }

  int run() {
    if (!QDir().mkpath(output_directory_)) {
      std::cerr << "Cannot create scene probe evidence directory\n";
      return 1;
    }
    create_root();
    step_clock_.start();
    timer_.start();
    return application_.exec();
  }

private:
  /** Each phase changes one observable contract before the next presented capture. */
  enum class Step {
    Front, BackCulled, DoubleSided, Texture, Camera, Pose,
    SameGeneration, Rejected, Normal, Circular, Quad,
    Alpha, AlphaSwap, AlphaRepeat, Minified, MinifiedCamera, MinifiedRetained, Reinitialized,
    AtomicBaseline, AtomicRejected, AtomicRetry
  };

  static constexpr std::array<std::array<int, 3>, 4> kImageColors{{
      {64, 128, 192}, {192, 64, 128}, {128, 192, 64}, {48, 96, 160}}};
  static constexpr crimson::shared::Color kBackground{0.04f, 0.06f, 0.08f, 1};

  void fail(std::string_view message) {
    if (error_.empty()) {
      error_ = message;
      std::cerr << "FAIL step=" << static_cast<int>(step_) << ": " << error_ << std::endl;
    }
  }

  bool accept(crimson::shared::Status status) {
    if (status.ok()) {
      return true;
    }
    fail(status.error_message());
    return false;
  }

  void create_root() {
    root_ = std::make_unique<QWidget>();
    root_->setWindowFlag(Qt::WindowStaysOnTopHint);
    root_->setWindowTitle(QStringLiteral("Quader Crimson scene GPU gate"));
    root_->resize(384, 288);
    if (QScreen *screen = application_.primaryScreen()) {
      root_->move(screen->availableGeometry().center() - root_->rect().center());
    }
    auto *layout = new QVBoxLayout(root_.get());
    layout->setContentsMargins(0, 0, 0, 0);
    widget_ = new QtVulkanCompositionWidget(root_.get());
    layout->addWidget(widget_);
    QObject::connect(widget_, &QRhiWidget::frameSubmitted, root_.get(), [this] {
      presented_generation_ = rendered_generation_;
      presentation_clock_.restart();
    });
    widget_->set_render_callback([this](const RenderVulkanCompositionTarget &target) {
      return render(target);
    });
    widget_->set_release_callback([this] {
      if (renderer_.initialized()) {
        if (!accept(renderer_.shutdown())) {
          qFatal("Scene GPU probe cannot release a borrowed Qt device with unknown GPU completion");
        }
        ++release_count_;
      }
      target_ = {};
      atomic_target_ = {};
      extent_ = {};
    });
    ++root_count_;
    root_->show();
  }

  bool initialize_renderer() {
    const RenderVulkanSharedContext *context = widget_->shared_context();
    if (context == nullptr || !context->enabled_capabilities) {
      fail("Qt did not attest its enabled Vulkan capabilities");
      return false;
    }
    capabilities_pin_ = context->enabled_capabilities;
    std::vector<const char *> instance_extensions;
    std::vector<const char *> device_extensions;
    for (const std::string &name : capabilities_pin_->instance_extensions) {
      instance_extensions.push_back(name.c_str());
    }
    for (const std::string &name : capabilities_pin_->device_extensions) {
      device_extensions.push_back(name.c_str());
    }
    const crimson::shared::BorrowedVulkanContext borrowed{
        .instance = context->instance, .physicalDevice = context->physical_device,
        .device = context->device, .queue = context->graphics_queue,
        .queueFamilyIndex = context->graphics_queue_family,
        .queueIndex = context->graphics_queue_index,
        .apiVersion = capabilities_pin_->api_version,
        .enabledFeatures = capabilities_pin_->features,
        .maxSamplerAnisotropy = capabilities_pin_->features.samplerAnisotropy ? 8.0f : 1.0f,
        .enabledInstanceExtensionCount = static_cast<std::uint32_t>(instance_extensions.size()),
        .enabledInstanceExtensions = instance_extensions.data(),
        .enabledDeviceExtensionCount = static_cast<std::uint32_t>(device_extensions.size()),
        .enabledDeviceExtensions = device_extensions.data()};
    if (!accept(renderer_.initialize(borrowed))) {
      return false;
    }
    std::cout << "backend=Crimson/bgfx Vulkan; borrowed_qt_device=" << context->device
              << "; root=" << root_count_ << std::endl;
    return true;
  }

  bool has_texture() const {
    return step_ >= Step::Texture;
  }

  bool alpha_step() const {
    return step_ >= Step::Alpha && step_ <= Step::AlphaRepeat;
  }

  bool unchanged_uploads(const crimson::shared::RetentionStats &before,
                         const crimson::shared::RetentionStats &after) {
    if (before.position_uploads != after.position_uploads ||
        before.attribute_uploads != after.attribute_uploads ||
        before.index_uploads != after.index_uploads ||
        before.image_uploads != after.image_uploads) {
      fail("A camera/instance/repeated/rejected update uploaded unchanged GPU streams");
      return false;
    }
    return true;
  }

  bool stage_scene() {
    if (!stage_needed_) {
      return true;
    }
    if (step_ == Step::AtomicRejected) {
      atomic_pending_generation_ = scene_generation_ + 1u;
      mesh_.indices = pinned(front_indices_);
      surface_.revision = atomic_pending_generation_;
      surface_.base_color = {0.2f, 0.8f, 0.1f, 1};
      surface_.image = {};
      instance_.transform_revision = atomic_pending_generation_;
      if (!accept(renderer_.stage_scene({atomic_pending_generation_, std::span(&mesh_, 1u),
              std::span(&surface_, 1u), std::span(&instance_, 1u)}))) return false;
      stage_needed_ = false;
      return true;
    }
    if (alpha_step()) {
      crimson::shared::MeshUpdate mesh;
      mesh.id = 1u;
      mesh.positions = pinned(positions_);
      mesh.attributes = pinned(attributes_);
      mesh.indices = pinned(front_indices_);
      std::array<crimson::shared::SurfaceUpdate, 2> surfaces;
      for (std::size_t index = 0; index < surfaces.size(); ++index) {
        surfaces[index].id = index + 1u;
        surfaces[index].revision = 1u;
        surfaces[index].base_color = index == 0u
            ? crimson::shared::Float4{1, 0, 0, 1} : crimson::shared::Float4{0, 0, 1, 1};
        surfaces[index].image = {.id = 2u, .revision = 1u, .extent = {1, 1},
                                 .srgb = true, .rgba8 = pinned(alpha_image_)};
      }
      std::array<crimson::shared::InstanceUpdate, 2> instances;
      for (std::size_t index = 0; index < instances.size(); ++index) {
        instances[index].id = index + 1u;
        instances[index].mesh = 1u;
        instances[index].surface = index + 1u;
        instances[index].transform_revision = scene_generation_ + 1u;
        const bool near = (index == 0u) == (step_ == Step::Alpha);
        instances[index].world_from_local.values[14] = near ? 0.25f : -0.25f;
      }
      const crimson::shared::SceneCandidate candidate{
          scene_generation_ + 1u, std::span(&mesh, 1u), surfaces, instances};
      if (!accept(renderer_.stage_scene(candidate))) {
        return false;
      }
      ++scene_generation_;
      stage_needed_ = false;
      return true;
    }
    const bool reject = step_ == Step::Rejected;
    const bool normal = step_ == Step::Normal;
    const std::uint64_t candidate_generation = step_ == Step::SameGeneration
        ? scene_generation_ : scene_generation_ + 1u;
    mesh_.id = 1u;
    mesh_.positions = pinned(positions_);
    mesh_.attributes = pinned(normal ? angled_attributes_ : attributes_, normal ? 2u : 1u);
    if (step_ == Step::Circular) {
      mesh_.attributes = pinned(circular_attributes_, 3u);
    } else if (step_ == Step::Quad) {
      mesh_.attributes = pinned(quad_attributes_, 4u);
    }
    mesh_.indices = pinned(reject ? invalid_indices_
        : step_ == Step::BackCulled || step_ == Step::DoubleSided ? back_indices_ : front_indices_);
    surface_ = {};
    surface_.id = 1u;
    surface_.revision = candidate_generation;
    surface_.base_color = {0.5f, 0.5f, 0.5f, 1};
    surface_.solid_color = normal ? crimson::shared::Float4{0.25f, 0.25f, 0.25f, 1}
                                 : crimson::shared::Float4{0.25f, 0.5f, 0.75f, 1};
    if (has_texture()) {
      surface_.image = {.id = 1u, .revision = 1u, .extent = {8, 8},
                        .srgb = true, .rgba8 = pinned(image_)};
    }
    if (step_ >= Step::Minified && step_ <= Step::MinifiedRetained) {
      surface_.base_color = {1, 1, 1, 1};
      surface_.uv_scale = {32, 32};
      surface_.image = {.id = 3u, .revision = 1u, .extent = {64, 64},
                        .srgb = true, .rgba8 = pinned(minify_image_)};
    }
    instance_ = {};
    instance_.id = 1u;
    instance_.mesh = 1u;
    instance_.surface = 1u;
    instance_.transform_revision = candidate_generation;
    instance_.double_sided = step_ == Step::DoubleSided;
    if (step_ == Step::Pose || step_ == Step::SameGeneration || reject) {
      instance_.world_from_local.values[12] = -0.2f;
    }
    if (normal) {
      constexpr float kAngle = std::numbers::pi_v<float> / 6.0f;
      const float cosine = std::cos(kAngle), sine = std::sin(kAngle);
      instance_.world_from_local.values = {
          -1.1f * cosine, -1.1f * sine, 0, 0,
          -0.45f * sine, 0.45f * cosine, 0, 0,
          0, 0, 1, 0, 0, 0, 0, 1};
    }
    const crimson::shared::SceneCandidate candidate{
        candidate_generation, std::span(&mesh_, 1u),
        std::span(&surface_, 1u), std::span(&instance_, 1u)};
    const auto before = renderer_.retention_stats();
    const auto result = renderer_.stage_scene(candidate);
    stage_needed_ = false;
    if (reject) {
      if (result.ok()) {
        fail("An out-of-range index candidate was accepted");
        return false;
      }
      const auto after = renderer_.retention_stats();
      if (after.accepted_generation != scene_generation_ || after.pending_generation != 0u ||
          !unchanged_uploads(before, after)) {
        fail("Rejected publication changed the accepted scene");
        return false;
      }
      rejection_checked_ = true;
      return true;
    }
    if (!accept(result)) {
      return false;
    }
    scene_generation_ = candidate_generation;
    return true;
  }

  bool render(const RenderVulkanCompositionTarget &destination) {
    if (!error_.empty() || (!renderer_.initialized() && !initialize_renderer())) {
      return false;
    }
    const crimson::shared::Extent required{destination.width, destination.height};
    if (target_.value == 0u) {
      if (!accept(renderer_.create_target(required, target_))) {
        return false;
      }
      extent_ = required;
    } else if (extent_ != required) {
      if (!accept(renderer_.resize_target(target_, required))) {
        return false;
      }
      extent_ = required;
    }
    if (!stage_scene()) {
      return false;
    }
    crimson::shared::ViewFrame frame;
    frame.generation = frame_generation_;
    frame.scene_generation = step_ == Step::AtomicRejected || step_ == Step::AtomicRetry
        ? atomic_pending_generation_ : scene_generation_;
    frame.camera_revision = step_ >= Step::Camera && step_ <= Step::Rejected ? 2u : 1u;
    frame.eye_world = {0, 0, 2};
    frame.forward_world = {0, 0, -1};
    frame.projection = crimson::shared::ProjectionKind::Orthographic;
    frame.depth = crimson::shared::DepthConvention::Forward;
    frame.pass = has_texture() ? crimson::shared::ScenePass::Shaded : crimson::shared::ScenePass::Solid;
    frame.light.enabled = 0.0f;
    frame.clear_color = kBackground;
    if (alpha_step()) {
      frame.clear_color = {0, 0, 0, 1};
      frame.depth = crimson::shared::DepthConvention::Reverse;
    }
    if ((step_ >= Step::Camera && step_ <= Step::Rejected) ||
        step_ == Step::AtomicBaseline || step_ == Step::AtomicRejected || step_ == Step::AtomicRetry) {
      frame.view_from_world.values[12] = 0.2f;
    }
    if (step_ == Step::MinifiedCamera || step_ == Step::MinifiedRetained) {
      frame.view_from_world.values[12] = 0.1f;
      frame.camera_revision = 3u;
    }
    if (step_ == Step::Normal) {
      frame.pass = crimson::shared::ScenePass::Solid;
      frame.light = {.key_direction_view = {0, 1, 0}, .key_intensity = 1,
                     .diffuse_wrap = 0, .ground_fill = 0, .sky_fill = 0, .enabled = 1};
    }
    const bool atomic = step_ == Step::AtomicBaseline || step_ == Step::AtomicRejected || step_ == Step::AtomicRetry;
    if (atomic) {
      if (atomic_target_.value == 0u && !accept(renderer_.create_target(required, atomic_target_))) return false;
      auto editor = std::make_shared<crimson::shared::EditorFrame>();
      editor->revision = frame.generation;
      editor->scene_generation = frame.scene_generation;
      frame.editor = editor;
      if (!accept(renderer_.prepare_view(target_, frame))) return false;
      if (step_ == Step::AtomicRejected) {
        auto mismatched = std::make_shared<crimson::shared::EditorFrame>(*editor);
        mismatched->scene_generation = scene_generation_;
        auto second = frame;
        second.editor = std::move(mismatched);
        if (renderer_.prepare_view(atomic_target_, second).ok()) {
          fail("A second-pane editor/scene generation mismatch passed epoch preflight");
          return false;
        }
        const auto stats = renderer_.retention_stats();
        if (stats.accepted_generation != scene_generation_ ||
            stats.pending_generation != atomic_pending_generation_) {
          fail("Failed later-pane preflight changed accepted scene publication");
          return false;
        }
        atomic_rejection_checked_ = true;
      } else {
        if (!accept(renderer_.prepare_view(atomic_target_, frame)) ||
            !accept(renderer_.render_view(target_, frame)) ||
            !accept(renderer_.render_view(atomic_target_, frame)) ||
            !accept(renderer_.submit())) return false;
        if (step_ == Step::AtomicBaseline) atomic_baseline_frame_generation_ = frame_generation_;
        if (step_ == Step::AtomicRetry) scene_generation_ = atomic_pending_generation_;
      }
    } else if (!accept(renderer_.render_view(target_, frame)) || !accept(renderer_.submit())) {
      return false;
    }
    if (renderer_.retention_stats().accepted_generation != scene_generation_) {
      fail("GPU submission did not acknowledge the scene generation");
      return false;
    }
    const crimson::shared::VulkanImageTarget output{
        .image = destination.image, .format = VK_FORMAT_R8G8B8A8_UNORM,
        .oldLayout = destination.old_layout,
        .finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .width = destination.width, .height = destination.height};
    if (!accept(renderer_.copy_to_vulkan(target_, output, last_submission_))) {
      return false;
    }
    const auto expected_target_generation = step_ == Step::AtomicRejected
        ? atomic_baseline_frame_generation_ : frame_generation_;
    if (last_submission_.generation != expected_target_generation || last_submission_.serial == 0u) {
      fail("The copy did not identify the accepted target frame");
      return false;
    }
    crimson::shared::VulkanTargetInfo native;
    if (!accept(renderer_.inspect_vulkan_target(target_, native))) {
      return false;
    }
    if (native.colorFormat != VK_FORMAT_R8G8B8A8_UNORM ||
        native.depthStencilFormat != VK_FORMAT_D32_SFLOAT_S8_UINT ||
        native.linearColorFormat != VK_FORMAT_R16G16B16A16_SFLOAT) {
      fail("The scene target did not retain its exact color/linear/depth-stencil formats");
      return false;
    }
    rendered_generation_ = frame_generation_;
    ++render_count_;
    return true;
  }

  bool sample(const QImage &pixels, float x, float y, std::array<int, 3> expected,
              std::string_view label) {
    const QPoint position(std::clamp(static_cast<int>(x * pixels.width()), 0, pixels.width() - 1),
                          std::clamp(static_cast<int>(y * pixels.height()), 0, pixels.height() - 1));
    const QColor actual = pixels.pixelColor(position);
    if (std::abs(actual.red() - expected[0]) > 3 ||
        std::abs(actual.green() - expected[1]) > 3 ||
        std::abs(actual.blue() - expected[2]) > 3 || actual.alpha() != 255) {
      std::cerr << "sample=" << label << "; at=" << position.x() << ',' << position.y()
                << "; actual=" << actual.red() << ',' << actual.green() << ',' << actual.blue()
                << "; expected=" << expected[0] << ',' << expected[1] << ',' << expected[2] << std::endl;
      fail("Rendered pixels violate scene coverage, texture encoding or transformed lighting");
      return false;
    }
    return true;
  }

  bool verify_image(const QImage &pixels, std::string_view label) {
    if (pixels.isNull()) {
      fail("Qt returned an empty scene readback");
      return false;
    }
    const std::array<int, 3> clear = alpha_step() ? std::array<int, 3>{0, 0, 0}
                                                : std::array<int, 3>{10, 15, 20};
    if (!sample(pixels, 0.03f, 0.03f, clear, label)) {
      return false;
    }
    if (step_ == Step::BackCulled) {
      return sample(pixels, 0.5f, 0.5f, clear, label) &&
             sample(pixels, 0.3f, 0.3f, clear, label);
    }
    if (step_ == Step::AtomicRetry) {
      const std::array<int, 3> expected{display_channel(0.2f), display_channel(0.8f), display_channel(0.1f)};
      return sample(pixels, 0.5f, 0.5f, expected, label) &&
          sample(pixels, 0.3f, 0.3f, expected, label);
    }
    if (step_ >= Step::Minified && step_ <= Step::MinifiedRetained) {
      // Equal black/white sRGB texels average to linear .5, encoded near188.
      // A display-space average128 and unfiltered base-level aliases both fail.
      const int encoded = display_channel(0.5f);
      for (const auto &position : std::array<std::array<float, 2>, 5>{{
               {0.29f, 0.32f}, {0.44f, 0.61f}, {0.57f, 0.37f},
               {0.68f, 0.66f}, {0.73f, 0.49f}}}) {
        if (!sample(pixels, position[0], position[1], {encoded, encoded, encoded}, label)) {
          return false;
        }
      }
      return true;
    }
    if (alpha_step()) {
      constexpr float kAlpha = 128.0f / 255.0f;
      const int near = display_channel(kAlpha);
      const int far = display_channel(kAlpha * (1.0f - kAlpha));
      const std::array<int, 3> expected = step_ == Step::Alpha
          ? std::array<int, 3>{near, 0, far} : std::array<int, 3>{far, 0, near};
      return sample(pixels, 0.5f, 0.5f, expected, label) &&
             sample(pixels, 0.3f, 0.3f, expected, label);
    }
    if (step_ == Step::Normal) {
      // For Rz(30deg)*diag(-1.1,.45,1), inverse-transpose maps (1,1,1)
      // to Rz(30deg)*(-1/1.1,1/.45,1). Only its world Y lights this fixture.
      constexpr float kAngle = std::numbers::pi_v<float> / 6.0f;
      const float normal_y = (-std::sin(kAngle) / 1.1f + std::cos(kAngle) / 0.45f) /
          std::sqrt(1.0f / (1.1f * 1.1f) + 1.0f / (0.45f * 0.45f) + 1.0f);
      const int encoded = display_channel(0.25f * normal_y);
      return sample(pixels, 0.5f, 0.5f, {encoded, encoded, encoded}, label);
    }
    if (step_ == Step::Circular || step_ == Step::Quad) {
      // Constant carriers isolate each nonlinear branch from raster interpolation.
      // Circular: sqrt(.3^2+.4^2)-.1=.4. Quad: (.4*.5)/(1)=.2.
      // Raw U=.2/.4 respectively, so both resolve U=.6 in the top-right texel block.
      std::array<int, 3> expected{};
      for (std::size_t channel = 0; channel < expected.size(); ++channel) {
        expected[channel] = display_channel(linear_channel(kImageColors[1][channel]) * 0.5f);
      }
      return sample(pixels, 0.5f, 0.5f, expected, label) &&
             sample(pixels, 0.3f, 0.3f, expected, label);
    }
    if (has_texture()) {
      const float shift = step_ == Step::Camera ? 0.1f : 0.0f;
      for (std::size_t index = 0; index < kImageColors.size(); ++index) {
        std::array<int, 3> expected{};
        for (std::size_t channel = 0; channel < expected.size(); ++channel) {
          expected[channel] = display_channel(linear_channel(kImageColors[index][channel]) * 0.5f);
        }
        if (!sample(pixels, (index % 2 ? 0.6875f : 0.3125f) + shift,
                    index >= 2 ? 0.6875f : 0.3125f, expected, label)) {
          return false;
        }
      }
      return step_ != Step::Camera || sample(pixels, 0.15f, 0.5f, clear, label);
    }
    const std::array<int, 3> fill{
        display_channel(0.25f), display_channel(0.5f), display_channel(0.75f)};
    return sample(pixels, 0.5f, 0.5f, fill, label) &&
           sample(pixels, 0.3f, 0.3f, fill, label);
  }

  bool verify_pixels() {
    QWindow *window = root_->windowHandle();
    if (window == nullptr || window->screen() == nullptr) {
      fail("The scene probe root has no presentation screen");
      return false;
    }
    QScreen *screen = window->screen();
    const QPoint origin = root_->mapToGlobal(QPoint{}) - screen->geometry().topLeft();
    const QImage presented = screen->grabWindow(0, origin.x(), origin.y(),
                                               root_->width(), root_->height()).toImage();
    // This test-only call renders an additional offscreen frame. Capture the
    // desktop first so evidence still proves the preceding presented frame.
    const QImage offscreen = widget_->grabFramebuffer();
    const QString stem = QStringLiteral("scene-%1-").arg(static_cast<int>(step_));
    const bool saved = presented.save(QDir(output_directory_).filePath(stem + QStringLiteral("presented.png"))) &&
        offscreen.save(QDir(output_directory_).filePath(stem + QStringLiteral("offscreen.png")));
    if (!saved) {
      fail("Cannot save scene GPU evidence");
      return false;
    }
    if (!verify_image(offscreen, "offscreen") || !verify_image(presented, "presented")) {
      return false;
    }
    if (step_ == Step::AtomicBaseline) atomic_baseline_image_ = offscreen;
    if (step_ == Step::AtomicRejected && offscreen != atomic_baseline_image_) {
      fail("A rejected later-pane preflight changed the prior accepted target's copied pixels");
      return false;
    }
    const auto stats = renderer_.retention_stats();
    if (step_ >= Step::Camera && step_ <= Step::Rejected && !unchanged_uploads(upload_baseline_, stats)) {
      return false;
    }
    if (step_ == Step::AlphaSwap || step_ == Step::AlphaRepeat) {
      if (!unchanged_uploads(alpha_baseline_, stats) ||
          stats.image_alpha_scans != alpha_baseline_.image_alpha_scans ||
          (step_ == Step::AlphaRepeat && stats.draw_order_rebuilds != alpha_baseline_.draw_order_rebuilds)) {
        fail("Retained alpha images/order performed work without an invalidating source");
        return false;
      }
    }
    if (step_ == Step::MinifiedCamera || step_ == Step::MinifiedRetained) {
      if (!unchanged_uploads(minify_baseline_, stats) ||
          stats.image_alpha_scans != minify_baseline_.image_alpha_scans) {
        fail("A minified camera/unchanged frame rebuilt its retained image");
        return false;
      }
    }
    std::cout << "PASS scene_step=" << static_cast<int>(step_)
              << "; scene_generation=" << stats.accepted_generation
              << "; target_generation=" << last_submission_.generation
              << "; serial=" << last_submission_.serial
              << "; uploads=" << stats.position_uploads << ',' << stats.attribute_uploads
              << ',' << stats.index_uploads << ',' << stats.image_uploads
              << "; alpha_scans=" << stats.image_alpha_scans
              << "; order_rebuilds=" << stats.draw_order_rebuilds
              << "; offscreen_and_presented=verified" << std::endl;
    return true;
  }

  void next(Step step, bool stage = true) {
    step_ = step;
    stage_needed_ = stage;
    ++frame_generation_;
    step_clock_.restart();
    widget_->update();
  }

  void advance() {
    if (!error_.empty() || step_clock_.elapsed() > 20000) {
      if (error_.empty()) {
        fail("Timed out waiting for the current presented scene frame");
      }
      root_.reset();
      widget_ = nullptr;
      timer_.stop();
      application_.exit(1);
      return;
    }
    if (presented_generation_ != frame_generation_ || presentation_clock_.elapsed() < 100) {
      return;
    }
    if (!verify_pixels()) {
      return;
    }
    switch (step_) {
    case Step::Front: next(Step::BackCulled); break;
    case Step::BackCulled: next(Step::DoubleSided); break;
    case Step::DoubleSided: next(Step::Texture); break;
    case Step::Texture:
      upload_baseline_ = renderer_.retention_stats();
      next(Step::Camera, false);
      break;
    case Step::Camera: next(Step::Pose); break;
    case Step::Pose: next(Step::SameGeneration); break;
    case Step::SameGeneration: next(Step::Rejected); break;
    case Step::Rejected:
      if (!rejection_checked_) {
        fail("Malformed scene rejection was not exercised");
        break;
      }
      next(Step::AtomicBaseline, false);
      break;
    case Step::AtomicBaseline: next(Step::AtomicRejected); break;
    case Step::AtomicRejected:
      if (!atomic_rejection_checked_) {
        fail("The later-pane preflight rejection was not exercised");
        break;
      }
      next(Step::AtomicRetry, false);
      break;
    case Step::AtomicRetry: next(Step::Normal); break;
    case Step::Normal: next(Step::Circular); break;
    case Step::Circular: next(Step::Quad); break;
    case Step::Quad: next(Step::Alpha); break;
    case Step::Alpha:
      alpha_baseline_ = renderer_.retention_stats();
      next(Step::AlphaSwap);
      break;
    case Step::AlphaSwap:
      alpha_baseline_ = renderer_.retention_stats();
      next(Step::AlphaRepeat, false);
      break;
    case Step::AlphaRepeat: next(Step::Minified); break;
    case Step::Minified:
      minify_baseline_ = renderer_.retention_stats();
      next(Step::MinifiedCamera, false);
      break;
    case Step::MinifiedCamera: next(Step::MinifiedRetained, false); break;
    case Step::MinifiedRetained:
      root_.reset();
      widget_ = nullptr;
      if (release_count_ != 1u) {
        fail("Root teardown did not drain the first renderer");
        break;
      }
      capabilities_pin_.reset();
      scene_generation_ = 0u;
      step_ = Step::Reinitialized;
      stage_needed_ = true;
      ++frame_generation_;
      step_clock_.restart();
      create_root();
      break;
    case Step::Reinitialized:
      root_.reset();
      widget_ = nullptr;
      timer_.stop();
      if (release_count_ != 2u) {
        fail("Root replacement did not drain the second renderer");
      }
      std::cout << "roots=" << root_count_ << "; releases=" << release_count_
                << "; rendered_frames=" << render_count_ << std::endl;
      application_.exit(error_.empty() ? 0 : 1);
      break;
    }
  }

  QApplication &application_;
  QString output_directory_;
  QTimer timer_;
  QElapsedTimer step_clock_, presentation_clock_;
  std::unique_ptr<QWidget> root_;
  QtVulkanCompositionWidget *widget_ = nullptr;
  crimson::shared::Renderer renderer_;
  crimson::shared::TargetId target_;
  crimson::shared::TargetId atomic_target_;
  crimson::shared::Extent extent_;
  crimson::shared::Submission last_submission_;
  crimson::shared::MeshUpdate mesh_;
  crimson::shared::SurfaceUpdate surface_;
  crimson::shared::InstanceUpdate instance_;
  crimson::shared::RetentionStats upload_baseline_, alpha_baseline_, minify_baseline_;
  std::shared_ptr<const RenderVulkanEnabledCapabilities> capabilities_pin_;
  std::shared_ptr<const std::vector<ScenePosition>> positions_;
  std::shared_ptr<const std::vector<SceneAttribute>> attributes_, angled_attributes_;
  std::shared_ptr<const std::vector<SceneAttribute>> circular_attributes_, quad_attributes_;
  std::shared_ptr<const std::vector<std::uint32_t>> front_indices_, back_indices_, invalid_indices_;
  std::shared_ptr<const std::vector<std::uint8_t>> image_, alpha_image_, minify_image_;
  std::uint64_t scene_generation_ = 0u, frame_generation_ = 1u;
  std::uint64_t rendered_generation_ = 0u, presented_generation_ = 0u;
  std::uint64_t render_count_ = 0u;
  std::uint64_t atomic_pending_generation_ = 0u, atomic_baseline_frame_generation_ = 0u;
  QImage atomic_baseline_image_;
  std::uint32_t root_count_ = 0u, release_count_ = 0u;
  Step step_ = Step::Front;
  bool stage_needed_ = true, rejection_checked_ = false, atomic_rejection_checked_ = false;
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
  const QCommandLineOption output_option(QStringLiteral("output-dir"),
      QStringLiteral("Scene GPU evidence directory."), QStringLiteral("directory"));
  parser.addOption(output_option);
  parser.process(application);
  const QString output_directory = parser.value(output_option);
  if (output_directory.isEmpty()) {
    std::cerr << "--output-dir must select the scene GPU evidence directory\n";
    return 2;
  }
  quader::CrimsonSceneCompositionProbe probe(application, output_directory);
  return probe.run();
}
