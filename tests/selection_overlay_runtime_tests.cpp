////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "api/quader_api.hpp"
#include "api/quader_api_session_internal_access.hpp"
#include "render/filament_material_library.hpp"
#include "render/overlay/viewport_selection_marquee_overlay_builder.hpp"
#include "render/png_texture_loader.hpp"
#include "render/render_layers.hpp"
#include "render/scene_render_bridge.hpp"
#include "render/viewport_overlay_policy.hpp"
#include "render/viewport_renderer.hpp"
#include "viewport/viewport_camera_adapter.hpp"

#include <gtest/gtest.h>

#include <backend/PixelBufferDescriptor.h>
#include <filament/SwapChain.h>

#include <QCoreApplication>
#include <QColor>
#include <QDir>
#include <QImage>

#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace quader {
namespace {

filament::Texture *create_test_solid_color_texture(
    filament::Engine &engine) {
  const std::array<std::byte, 4u> neutral_gray{
      std::byte{181u}, std::byte{181u}, std::byte{181u}, std::byte{255u}};
  return load_rgba_texture(
      engine, neutral_gray, 1u, 1u,
      PngTextureLoadOptions{
          .color_space = PngTextureColorSpace::Srgb,
          .generate_mipmaps = true,
          .label = "selection runtime Solid viewport texture",
      });
}

/** Publishes the same semantic frame to the production retained overlay path. */
class SelectionRuntimeFrame final : public ViewportOverlaySelectionFrameProvider {
public:
  ViewportOverlayFrame build_selection_overlay_frame() const override {
    return frame;
  }

  ViewportOverlayFrame frame;
};

/** Runs the production scene and overlay passes on a readable Vulkan surface. */
class SelectionOverlayRuntimeHarness final {
public:
  static constexpr std::uint32_t kWidth = 640u;
  static constexpr std::uint32_t kHeight = 480u;
  static constexpr QVec2 kVisiblePixel{200.0f, 240.0f};
  static constexpr QVec2 kHiddenPixel{440.0f, 240.0f};

  ~SelectionOverlayRuntimeHarness() {
    if (engine_ == nullptr) return;
    (void)engine_->flushAndWait(5'000'000'000ULL);
    engine_->pumpMessageQueues();
    view_.shutdown();
    bridge_.shutdown();
    registry_.clear();
    materials_.shutdown();
    if (solid_color_texture_) engine_->destroy(solid_color_texture_);
    if (swapchain_) engine_->destroy(swapchain_);
    if (renderer_) engine_->destroy(renderer_);
    if (scene_) engine_->destroy(scene_);
    filament::Engine::destroy(&engine_);
  }

  bool initialize() {
    const std::filesystem::path resources(
        QCoreApplication::applicationDirPath().toStdWString());
    engine_ = filament::Engine::create(filament::Engine::Backend::VULKAN);
    if (engine_ == nullptr) return false;
    scene_ = engine_->createScene();
    renderer_ = engine_->createRenderer();
    swapchain_ = engine_->createSwapChain(
        kWidth, kHeight, filament::SwapChain::CONFIG_READABLE);
    if (!scene_ || !renderer_ || !swapchain_ ||
        !materials_.initialize(*engine_, resources)) return false;
    solid_color_texture_ = create_test_solid_color_texture(*engine_);
    if (solid_color_texture_ == nullptr) return false;
    bridge_.initialize(
        *engine_, *scene_, materials_.quader_mesh(),
        materials_.quader_mesh_lit(), materials_.vertex_color_overlay(),
        materials_.instance_selection_fill(), materials_.overlay_line(),
        materials_.selected_object_line(), materials_.edit_wire_line(),
        materials_.overlay_point(), materials_.overlay_icon(),
        *solid_color_texture_, resources);
    view_.initialize(*engine_, *scene_, registry_, materials_.ground_grid(),
                     viewport_grid_layer_mask(0u),
                     ViewportRenderSurface::Swapchain);
    camera_.set_viewport_size(kWidth, kHeight);
    camera_.set_view_direction({0.0f, 0.0f, -1.0f});
    camera_.set_clip_planes(0.001f, 200.0f);
    view_.resize(kWidth, kHeight);
    view_.set_overlay_visible(true);
    view_.set_scene_layer_mask(kViewportRenderedSceneLayerMask);
    view_.set_shadowing_enabled(false);
    view_.set_material_debug_mode(ViewportMaterialDebugMode::Albedo);
    SceneSettingsComponent settings;
    settings.environment.enabled = false;
    settings.environment.show_background = false;
    settings.environment.clear_color = {};
    settings.post_process.anti_aliasing = false;
    settings.post_process.dithering = false;
    settings.post_process.ambient_occlusion = false;
    settings.post_process.bloom = false;
    settings.post_process.tone_mapping = SceneToneMapping::Linear;
    view_.apply_scene_settings(settings, true);

    QuaderApi api(QuaderApiSessionInternalAccess::from_scene_session(session_));
    const auto wall = api.modeling().create_box(
        {.min = {-100.0f, -100.0f, -0.02f},
         .max = {100.0f, 100.0f, 0.0f}}, "Selection visibility wall");
    if (!wall.ok()) return false;
    const auto material = api.assets().add_material(QuaderMaterialAsset{
        .name = "Selection visibility neutral gray",
        .base_color = {0.18f, 0.18f, 0.18f}, .roughness = 1.0f});
    if (!material.ok() ||
        !api.assets().assign_material_to_entity(
            wall.value().entity, material.value().id).ok()) return false;
    bridge_.sync(session_.scene(), session_.assets());
    return true;
  }

  void set_sample(float eye_distance, bool hidden_point_on_top = false) {
    camera_.focus_origin(eye_distance);
    view_.update_camera(camera_);
    view_.update_ground_grid(camera_, 1.0f, ViewportSettings::defaults(), false);
    const auto overlay_camera = ViewportCameraAdapter::overlay_camera(
        camera_, {kWidth, kHeight});
    SelectionRuntimeFrame provider;
    // The same wall that is rendered by the scene pass supplies component
    // depth-stamp triangles. Both triangles face the camera.
    provider.frame.triangles = {
        {.layer = ViewportOverlayLayer::SourceWireDepthStamp,
         .a = {-100.0f, -100.0f, 0.0f},
         .b = {100.0f, -100.0f, 0.0f},
         .c = {100.0f, 100.0f, 0.0f},
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 2}},
        {.layer = ViewportOverlayLayer::SourceWireDepthStamp,
         .a = {-100.0f, -100.0f, 0.0f},
         .b = {100.0f, 100.0f, 0.0f},
         .c = {-100.0f, 100.0f, 0.0f},
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 2}},
    };
    // The editable mesh is an inward room; the opaque wall is a different
    // scene object. This is the exact old failure: camera-inside detection
    // promoted every room vertex above the unrelated wall's GPU depth.
    append_inward_room_stamps(provider.frame);
    constexpr auto kCyan = rgba8(40, 225, 255);
    provider.frame.points = {
        {.layer = ViewportOverlayLayer::UnselectedVertex,
         .position = point_at_plane(kVisiblePixel, 0.0f),
         .color = kCyan,
         .size_pixels = 9.0f,
         .style_role = ViewportOverlayStyleRole::ExplicitColor,
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 1, .vertex_id = 0}},
        {.layer = hidden_point_on_top ? ViewportOverlayLayer::ToolHandle
                                     : ViewportOverlayLayer::UnselectedVertex,
         .position = point_at_plane(kHiddenPixel, -0.25f),
         .color = kCyan,
         .size_pixels = 9.0f,
         .style_role = ViewportOverlayStyleRole::ExplicitColor,
         .source = ViewportOverlaySource::ComponentSelection,
         .element = {.object_id = 1, .vertex_id = 1}},
    };
    const std::vector<QVec2> lasso{
        {100.0f, 145.0f}, {240.0f, 105.0f}, {370.0f, 140.0f},
        {500.0f, 115.0f}, {540.0f, 325.0f}, {380.0f, 370.0f},
        {310.0f, 300.0f}, {220.0f, 385.0f}, {105.0f, 325.0f}};
    ViewportSelectionMarqueeOverlayBuilder::append(
        provider.frame, overlay_camera, {.active = true, .lasso_points = lasso});
    (void)bridge_.submit_viewport_overlay(
        {.layer_mask = viewport_grid_layer_mask(0u),
         .camera = overlay_camera,
         .selection_source = {.source_identity = 104u,
                              .selection_revision = ++revision_},
         .selection_frame_provider = &provider,
         .style_selection_context = ViewportOverlaySelectionContext::Vertex,
         .overlays_visible = true},
        ViewportSettings::defaults());
  }

  QImage capture() {
    for (int frame = 0; frame < 5; ++frame) {
      if (!begin_frame()) return {};
      view_.render(*renderer_);
      renderer_->endFrame();
    }
    if (!begin_frame()) return {};
    view_.render(*renderer_);
    auto readback = std::make_shared<Readback>();
    auto *owner = new std::shared_ptr<Readback>(readback);
    renderer_->readPixels(
        0, 0, kWidth, kHeight,
        filament::backend::PixelBufferDescriptor(
            readback->bytes.data(), readback->bytes.size(),
            filament::backend::PixelDataFormat::RGBA,
            filament::backend::PixelDataType::UBYTE,
            [](void *, std::size_t, void *user) {
              auto *holder = static_cast<std::shared_ptr<Readback> *>(user);
              (*holder)->complete.store(true, std::memory_order_release);
              delete holder;
            }, owner));
    renderer_->endFrame();
    for (int attempt = 0; attempt < 10 &&
         !readback->complete.load(std::memory_order_acquire); ++attempt) {
      if (!engine_->flushAndWait(1'000'000'000ULL)) return {};
      engine_->pumpMessageQueues();
    }
    if (!readback->complete.load(std::memory_order_acquire)) return {};
    return QImage(readback->bytes.data(), kWidth, kHeight,
                   QImage::Format_RGBA8888).mirrored(false, true);
  }

private:
  static void append_inward_room_stamps(ViewportOverlayFrame &frame) {
    const auto quad = [&](QVec3 a, QVec3 b, QVec3 c, QVec3 d) {
      for (const auto &triangle :
           std::array<std::array<QVec3, 3>, 2>{{{a, c, b}, {a, d, c}}}) {
        frame.triangles.push_back({
            .layer = ViewportOverlayLayer::SourceWireDepthStamp,
            .a = triangle[0], .b = triangle[1], .c = triangle[2],
            .source = ViewportOverlaySource::ComponentSelection,
            .element = {.object_id = 1},
        });
      }
    };
    // Outward quads are reversed by quad() to model an authored inward room.
    quad({-100, -100, 2}, {100, -100, 2}, {100, 100, 2}, {-100, 100, 2});
    quad({100, -100, -1}, {-100, -100, -1}, {-100, 100, -1}, {100, 100, -1});
    quad({-100, -100, -1}, {-100, -100, 2}, {-100, 100, 2}, {-100, 100, -1});
    quad({100, -100, 2}, {100, -100, -1}, {100, 100, -1}, {100, 100, 2});
    quad({-100, -100, -1}, {100, -100, -1}, {100, -100, 2}, {-100, -100, 2});
    quad({-100, 100, 2}, {100, 100, 2}, {100, 100, -1}, {-100, 100, -1});
  }

  /** Retains CPU bytes until Filament invokes its asynchronous release callback. */
  struct Readback final {
    std::array<unsigned char, kWidth * kHeight * 4> bytes{};
    std::atomic<bool> complete = false;
  };

  QVec3 point_at_plane(QVec2 pixel, float z) const {
    const auto ray = camera_.ray_from_viewport_point(pixel);
    return ray.origin + ray.direction * ((z - ray.origin.z) / ray.direction.z);
  }

  bool begin_frame() {
    for (int attempt = 0; attempt < 80; ++attempt) {
      if (renderer_->beginFrame(swapchain_)) return true;
      if (!engine_->flushAndWait(1'000'000'000ULL)) return false;
      engine_->pumpMessageQueues();
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
  }

  SceneSession session_;
  filament::Engine *engine_ = nullptr;
  filament::Scene *scene_ = nullptr;
  filament::Renderer *renderer_ = nullptr;
  filament::SwapChain *swapchain_ = nullptr;
  filament::Texture *solid_color_texture_ = nullptr;
  FilamentMaterialLibrary materials_;
  TextureRegistry registry_;
  SceneRenderBridge bridge_;
  ViewportRenderer view_;
  ViewportCameraController camera_;
  std::uint64_t revision_ = 0;
};

int cyan_pixels(const QImage &image, QVec2 center) {
  int count = 0;
  for (int y = int(center.y) - 3; y <= int(center.y) + 3; ++y) {
    for (int x = int(center.x) - 3; x <= int(center.x) + 3; ++x) {
      const QColor pixel = image.pixelColor(x, y);
      if (pixel.green() > pixel.red() + 45 &&
          pixel.blue() > pixel.red() + 45 && pixel.green() > 130) ++count;
    }
  }
  return count;
}

TEST(SelectionOverlayRuntimeTests,
     VulkanLassoAndOccludedVerticesStayCorrectAsCameraApproachesSurface) {
  if (qEnvironmentVariable("QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS") !=
      QStringLiteral("1")) {
    GTEST_SKIP() << "Set QUADER_RUN_SELECTION_OVERLAY_RUNTIME_TESTS=1 to read "
                    "and capture production offscreen Vulkan selection overlays.";
  }
  ASSERT_EQ(QCoreApplication::instance(), nullptr);
  int argc = 1;
  char application_name[] = "quader-selection-overlay-test";
  char *argv[] = {application_name, nullptr};
  QCoreApplication application(argc, argv);
  SelectionOverlayRuntimeHarness harness;
  ASSERT_TRUE(harness.initialize());
  QString output_directory =
      qEnvironmentVariable("QUADER_SELECTION_OVERLAY_OUTPUT_DIR");
  if (output_directory.isEmpty()) {
    output_directory = QDir(QCoreApplication::applicationDirPath())
                           .absoluteFilePath(QStringLiteral("../selection_features_visual"));
  }
  ASSERT_TRUE(QDir().mkpath(output_directory));
  for (const auto &[label, distance] :
       std::array<std::pair<const char *, float>, 3>{{
           {"near", 0.1f}, {"middle", 1.0f}, {"far", 20.0f}}}) {
    SCOPED_TRACE(label);
    harness.set_sample(distance);
    const QImage image = harness.capture();
    ASSERT_FALSE(image.isNull());
    const QString path = QDir(output_directory).filePath(
        QString::fromLatin1(label) + QStringLiteral("_lasso_depth.png"));
    ASSERT_TRUE(image.save(path));
    const int visible = cyan_pixels(image, SelectionOverlayRuntimeHarness::kVisiblePixel);
    const int hidden = cyan_pixels(image, SelectionOverlayRuntimeHarness::kHiddenPixel);
    EXPECT_GE(visible, 9) << "Visible surface vertex must survive depth testing.";
    EXPECT_EQ(hidden, 0) << "Occluded vertex must not leak through the wall.";
    std::cout << "Selection GPU " << label << ": visible_cyan=" << visible
              << ", hidden_cyan=" << hidden << ", png="
              << path.toStdString() << '\n';
  }

  // The same hidden position must be visible when deliberately drawn on top;
  // this rules out an offscreen point or failed point pipeline hiding the bug.
  harness.set_sample(0.1f, true);
  const QImage control = harness.capture();
  ASSERT_FALSE(control.isNull());
  ASSERT_TRUE(control.save(QDir(output_directory).filePath(
      QStringLiteral("near_on_top_control.png"))));
  EXPECT_GE(cyan_pixels(control, SelectionOverlayRuntimeHarness::kHiddenPixel), 9);
}

} // namespace
} // namespace quader
