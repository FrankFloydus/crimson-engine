////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////

#include "api/quader_api_session_internal_access.hpp"
#include "api/quader_editor_api.hpp"
#include "api/quader_polygon_document_internal_access.hpp"
#include "editor/render/quader_render_api_runtime_access.hpp"
#include "editor/session/scene_session.hpp"
#include "render/render_host.hpp"
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
#include "render/crimson/crimson_render_host.hpp"
#endif
#include "render/overlay/render_overlay_snapshot.hpp"
#include "ui/qt_viewport_panel.hpp"
#include "ui/qt_viewport_surface.hpp"
#include "ui/qt_vulkan_composition_widget.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_settings.hpp"

#include <quader/modeling/neutral/polygon/document_model.hpp>

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
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace quader {
namespace {

/** Drives the production four-pane adapter without selecting it for an app. */
class EditorCompositionProbe final {
public:
  EditorCompositionProbe(QApplication &application, QString output_directory,
                         std::filesystem::path resources, bool overlay_parity,
                         QString reference_directory)
      : application_(application), output_directory_(std::move(output_directory)),
        resources_(std::move(resources)),
        reference_directory_(std::move(reference_directory)), overlay_parity_(overlay_parity),
        editor_(QuaderApiSessionInternalAccess::from_scene_session(session_)),
#if defined(QUADER_FILAMENT_REFERENCE_PROBE)
        host_(create_app_render_host()) {
#else
        host_(create_crimson_render_host()) {
#endif
    timer_.setInterval(50);
    QObject::connect(&timer_, &QTimer::timeout, &application_, [this] { advance(); });
  }

  ~EditorCompositionProbe() {
    trace_retirement("probe_destructor_enter");
    retire_root();
    trace_retirement("probe_destructor_exit");
  }

  int run() {
    if (!QDir().mkpath(output_directory_) || !host_) {
      std::cerr << "Cannot initialize editor probe output/host\n";
      return 1;
    }
    if (!accept(editor_.commands().set_keybinding_storage_path({})) ||
        !accept(editor_.viewports().set_layout(ViewportLayoutId::FourQuad))) {
      return 1;
    }
    const auto created = editor_.modeling().create_box(
        {.min = {-0.6f, -0.35f, -0.45f}, .max = {0.6f, 0.35f, 0.45f}},
        "Crimson public composition box");
    if (!accept(created)) {
      return 1;
    }
    object_ = created.value();
    if (!accept(editor_.selection().select_object(object_))) {
      return 1;
    }
    settings_ = ViewportSettings::defaults();
    (void)settings_.set_color(ViewportColorSettingId::Background, {0, 0, 0, 1});
    for (const auto setting : {ViewportBoolSettingId::ShowGrid,
                              ViewportBoolSettingId::ShowMeshGrid,
                              ViewportBoolSettingId::ShowOverlays,
                              ViewportBoolSettingId::ShowGizmos,
                              ViewportBoolSettingId::ShowDimensions,
                              ViewportBoolSettingId::ShowHoverDimensions}) {
      settings_.set_bool_value(setting, false);
    }
    (void)session_.viewport_workspace().update_control_defaults(settings_, ++settings_revision_);
    for (const auto pane : kPanes) {
      if (!accept(editor_.viewports().set_shading_mode(pane, QuaderShadingMode::Shaded))) {
        return 1;
      }
      session_.viewport_workspace().camera(pane).focus_bounds({-2, -2, -2}, {2, 2, 2});
    }
    create_root();
    step_clock_.start();
    settle_clock_.start();
    timer_.start();
    return application_.exec();
  }

private:
  /** API mutations and shell transitions each require a fresh presented epoch. */
  enum class Step {
    Initial, Unchanged, Pose, Preview, Cancel, Camera, Resize, Hidden, Reshow, Replaced,
    ObjectSingleton, ObjectSingletonCustom, ObjectsActivePrimary, ObjectsActiveOther,
    FacesBase, FacesSelected, FacesHovered, FacesHoverCleared, Edges, Vertices, VertexHovered,
    GridBase, Grid, GridCamera, GridRetained, Solid, ShadedWire, Wireframe,
    Gizmo, GizmoCamera, GizmoRetained,
    ActivePreviewBase, ActivePreview, ActivePreviewRetained, ActivePreviewCancel,
    HostRejected, HostRetry, CollisionOff, CollisionRestored,
    CollisionMaxOpacity, CollisionOpacityRestored
  };

  static constexpr std::array kPanes{
      ViewportPaneId::Perspective, ViewportPaneId::Top,
      ViewportPaneId::Front, ViewportPaneId::Side};
  static constexpr QVec3 kPreviewDelta{0.7f, 0.7f, 0.2f};
#if defined(QUADER_FILAMENT_REFERENCE_PROBE)
  static constexpr const char *kBackendName = "FullFilament";
#else
  static constexpr const char *kBackendName = "Crimson";
#endif

  void fail(std::string_view message) {
    if (error_.empty()) {
      error_ = message;
      std::cerr << "FAIL editor_step=" << static_cast<int>(step_) << ": " << error_ << std::endl;
    }
  }

  bool accept(const QuaderApiStatus &status) {
    if (status.ok()) {
      return true;
    }
    fail(status.error_message());
    return false;
  }

  bool select_preview_box_vertices() {
    // Edit entry clears first-admission components. Select All only extends
    // an already participating component selection (or a sticky hover seed),
    // so seed this known box through the same explicit public API as the
    // canonical EditorModeServiceTests fixture.
    if (!accept(editor_.selection().select_vertices(object_, {1, 2, 3, 4, 5, 6, 7, 8}))) {
      return false;
    }
    const auto mode = editor_.mode().state();
    const auto selected = editor_.selection().summary();
    if (mode.mode != QuaderEditorMode::Edit || mode.participants.size() != 1u ||
        mode.participants.front().id != object_.entity ||
        selected.mode != QuaderSelectionMode::Vertex || selected.selected_vertex_count != 8u) {
      std::cerr << "preview_participants=" << mode.participants.size()
                << "; editor_mode=" << static_cast<int>(mode.mode)
                << "; selection_mode=" << static_cast<int>(selected.mode)
                << "; selected_vertices=" << selected.selected_vertex_count << std::endl;
      fail("The explicit box selection did not establish its Edit participant and eight vertices");
      return false;
    }
    return true;
  }

  void apply_settings() {
    (void)session_.viewport_workspace().update_control_defaults(settings_, ++settings_revision_);
  }

  bool set_shading(QuaderShadingMode mode) {
    for (const auto pane : kPanes) {
      if (!accept(editor_.viewports().set_shading_mode(pane, mode))) {
        return false;
      }
    }
    return true;
  }

  bool begin_overlay_fixture() {
    if (!accept(editor_.mode().set(QuaderEditorMode::Object))) {
      return false;
    }
    const auto other = editor_.modeling().create_box(
        {.min = {-1.25f, -0.35f, -0.45f}, .max = {-0.35f, 0.35f, 0.45f}},
        "Ordinary versus active selection");
    if (!accept(other)) {
      return false;
    }
    other_object_ = other.value();
    settings_.set_bool_value(ViewportBoolSettingId::ShowOverlays, true);
    settings_.set_bool_value(ViewportBoolSettingId::ShowCollisions, true);
    collision_color_ = settings_.color(ViewportColorSettingId::CollisionFill);
    settings_.set_bool_value(ViewportBoolSettingId::ShowGrid, false);
    settings_.set_bool_value(ViewportBoolSettingId::ShowGizmos, false);
    (void)settings_.set_color(ViewportColorSettingId::SelectionWire, {1, 0.45f, 0.05f, 1});
    (void)settings_.set_color(ViewportColorSettingId::ActiveSelection, {1, 105.0f / 255.0f, 180.0f / 255.0f, 1});
    apply_settings();
    return accept(editor_.selection().select_object(object_));
  }

  bool begin_face_fixture() {
    if (!accept(editor_.mode().set(QuaderEditorMode::Object)) ||
        !accept(editor_.scene().set_visible(other_object_.entity, false))) {
      return false;
    }
    quader_poly::Document document;
    const auto quad = [&](float left, float right, float bottom, float top, float z, bool front) {
      std::array vertices{
          quader_poly::add_vertex(document, {left, bottom, z}),
          quader_poly::add_vertex(document, {right, bottom, z}),
          quader_poly::add_vertex(document, {right, top, z}),
          quader_poly::add_vertex(document, {left, top, z})};
      if (component_vertices_.empty()) {
        component_vertices_.assign(vertices.begin(), vertices.end());
      }
      if (!front) {
        std::swap(vertices[1], vertices[3]);
      }
      component_faces_.push_back(quader_poly::add_face(document, vertices));
    };
    // Back-facing coplanar source surfaces let the component tint priority show
    // without opaque source color. The front-facing inset still occludes it.
    quad(-1.6f, -0.65f, -0.3f, 0.35f, 0.6f, false);
    quad(-1.05f, -0.1f, -0.3f, 0.35f, 0.6f, false);
    quad(-0.95f, -0.75f, 0.15f, 0.3f, 0.8f, true);
    const auto created = editor_.modeling().create_mesh(
        QuaderPolygonDocumentInternalAccess::from_backend(std::move(document)),
        {.name = "Public component overlay priority"});
    if (!accept(created)) {
      return false;
    }
    component_object_ = created.value();
    settings_.set_bool_value(ViewportBoolSettingId::BackfaceCulling, true);
    // Orthographic hover is outline-only when this picking preference is off.
    // This fixture explicitly exercises the enabled hover-fill priority path.
    settings_.set_bool_value(ViewportBoolSettingId::BackfaceSelection, true);
    (void)settings_.set_color(ViewportColorSettingId::SelectionFace, {1, 0, 0, 0.35f});
    (void)settings_.set_color(ViewportColorSettingId::ActiveSelection, {0, 1, 1, 1});
    (void)settings_.set_color(ViewportColorSettingId::HoverFace, {1, 1, 0, 0.35f});
    (void)settings_.set_color(ViewportColorSettingId::SelectedVertex, {1, 0.45f, 0.05f, 1});
    (void)settings_.set_color(ViewportColorSettingId::HoverVertex, {1, 1, 0, 1});
    apply_settings();
    // This analytic fixture defines front at +Z and its opaque inset closer
    // to that eye; use an explicit camera instead of the Front preset's -Z eye.
    session_.viewport_workspace().camera(ViewportPaneId::Front)
        .view_axis(ViewportViewAxis::Z, 1);
    return accept(editor_.selection().select_object(component_object_)) &&
        accept(editor_.mode().set(QuaderEditorMode::Edit)) &&
        accept(editor_.selection().set_mode(QuaderSelectionMode::Face)) &&
        accept(editor_.selection().clear()) && accept(editor_.selection().clear_hover());
  }

  QRect object_rectangle(std::size_t pane, bool other = false) const {
    const auto &camera = session_.viewport_workspace().camera(kPanes[pane]);
    QRect bounds;
    for (const float x : {other ? -1.25f : 0.2f, other ? -0.35f : 1.4f}) {
      for (const float y : {-0.35f, 0.35f}) {
        for (const float z : {-0.45f, 0.45f}) {
          const auto projected = camera.project_world_point({x, y, z});
          const QPoint pixel(qRound(projected.position.x), qRound(projected.position.y));
          bounds = bounds.isNull() ? QRect(pixel, QSize(1, 1)) : bounds.united(QRect(pixel, QSize(1, 1)));
        }
      }
    }
    return bounds.adjusted(-8, -8, 8, 8);
  }

  std::size_t pink_pixels(const QImage &image, QRect rectangle) const {
    rectangle = rectangle.intersected(image.rect());
    std::size_t result = 0u;
    for (int y = rectangle.top(); y <= rectangle.bottom(); ++y) {
      for (int x = rectangle.left(); x <= rectangle.right(); ++x) {
        const QColor color = image.pixelColor(x, y);
        result += color.red() > color.green() + 30 && color.blue() > color.green() + 20;
      }
    }
    return result;
  }

  QColor world_sample(const QImage &image, std::size_t pane, QVec3 world) const {
    const auto projected = session_.viewport_workspace().camera(kPanes[pane]).project_world_point(world);
    const QPoint pixel(qRound(projected.position.x), qRound(projected.position.y));
    return image.pixelColor(std::clamp(pixel.x(), 0, image.width() - 1),
                            std::clamp(pixel.y(), 0, image.height() - 1));
  }

  bool near_colors(QColor first, QColor second, int tolerance = 6) const {
    return std::abs(first.red() - second.red()) <= tolerance &&
        std::abs(first.green() - second.green()) <= tolerance &&
        std::abs(first.blue() - second.blue()) <= tolerance;
  }

  int hue(QColor color) const {
    const int r = color.red(), g = color.green(), b = color.blue();
    if (r > g + 30 && b > g + 20) return 1;
    if (g > r + 30 && b > r + 30) return 2;
    if (r > b + 30 && g > b + 30 && r * 4 >= g * 3 && g * 4 >= r * 3) return 3;
    if (r > g + 30 && r > b + 30) return 4;
    if (g > r + 30 && g > b + 30) return 5;
    if (b > r + 30 && b > g + 30) return 6;
    return 0;
  }

  bool compare_reference(const QImage &image, std::size_t pane) {
    if (reference_directory_.isEmpty() || step_ < Step::ObjectSingleton ||
        step_ == Step::HostRejected || step_ == Step::HostRetry) {
      return true;
    }
    const QString name = QStringLiteral("editor-%1-pane-%2-offscreen.png")
        .arg(static_cast<int>(step_)).arg(pane);
    const QImage reference(QDir(reference_directory_).filePath(name));
    if (reference.isNull() || reference.size() != image.size()) {
      fail("The Full reference image is missing or has different pane dimensions");
      return false;
    }
    std::size_t reference_colored = 0u, actual_colored = 0u, matched = 0u;
    std::size_t reference_collision_pixels = 0u, matched_collision_colors = 0u;
    for (int y = 2; y < image.height() - 2; ++y) {
      for (int x = 2; x < image.width() - 2; ++x) {
        const QColor reference_color = reference.pixelColor(x, y);
        const int expected = hue(reference_color);
        actual_colored += hue(image.pixelColor(x, y)) != 0;
        if (expected == 0) continue;
        ++reference_colored;
        const bool collision_color = step_ == Step::CollisionMaxOpacity && expected == 2;
        reference_collision_pixels += collision_color;
        bool found = false;
        bool color_matched = false;
        for (int dy = -2; dy <= 2 && (!found || (collision_color && !color_matched)); ++dy) {
          for (int dx = -2; dx <= 2; ++dx) {
            const QColor actual = image.pixelColor(x + dx, y + dy);
            const bool same_hue = hue(actual) == expected;
            found |= same_hue;
            color_matched |= collision_color && same_hue && near_colors(reference_color, actual, 12);
          }
        }
        matched += found;
        matched_collision_colors += color_matched;
      }
    }
    if (reference_colored > 20u &&
        (matched * 100u < reference_colored * 90u ||
         actual_colored * 100u < reference_colored * 85u ||
         actual_colored * 100u > reference_colored * 118u)) {
      std::cerr << "reference_pane=" << pane << "; colored=" << reference_colored
                << "; actual=" << actual_colored << "; matched_within_2px=" << matched << std::endl;
      fail("GPU overlay color/coverage disagrees with the Full reference");
      return false;
    }
    // Selected collision wires multiply authored alpha by 1.6. Match RGB as
    // well as hue so clamping that value in vertex storage cannot pass by
    // preserving only the cyan edge coverage. The spatial tolerance absorbs AA.
    if (step_ == Step::CollisionMaxOpacity &&
        (reference_collision_pixels == 0u ||
         matched_collision_colors * 100u < reference_collision_pixels * 90u)) {
      std::cerr << "reference_pane=" << pane
                << "; collision_cyan=" << reference_collision_pixels
                << "; rgb_matched_within_2px_12codes=" << matched_collision_colors << std::endl;
      fail("Maximum-opacity collision RGB disagrees with the Full reference");
      return false;
    }
    return true;
  }

  bool has_hue_near(const QImage &image, std::size_t pane, QVec3 world, int expected,
                    int radius = 8) const {
    const auto projected = session_.viewport_workspace().camera(kPanes[pane]).project_world_point(world);
    const QPoint center(qRound(projected.position.x), qRound(projected.position.y));
    for (int y = -radius; y <= radius; ++y) {
      for (int x = -radius; x <= radius; ++x) {
        const QPoint pixel = center + QPoint(x, y);
        if (image.rect().contains(pixel) && hue(image.pixelColor(pixel)) == expected) {
          return true;
        }
      }
    }
    return false;
  }

  bool verify_overlay_frame(const std::array<QImage, 4> &captures) {
    if (step_ < Step::ObjectSingleton) return true;
    constexpr std::size_t kFront = 2u;
    const auto &front = captures[kFront];
    if (step_ == Step::ObjectSingleton) collision_on_captures_ = captures;
    if (step_ == Step::CollisionOff || step_ == Step::CollisionRestored ||
        step_ == Step::CollisionMaxOpacity || step_ == Step::CollisionOpacityRestored) {
      for (std::size_t pane = 0u; pane < captures.size(); ++pane) {
        const auto cyan_pixels = [this](const QImage &image) {
          std::size_t count = 0u;
          for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
              count += hue(image.pixelColor(x, y)) == 2;
            }
          }
          return count;
        };
        const auto baseline_cyan = cyan_pixels(collision_on_captures_[pane]);
        const auto current_cyan = cyan_pixels(captures[pane]);
        std::cout << "collision_pixels; step=" << static_cast<int>(step_)
                  << "; pane=" << pane << "; baseline_cyan=" << baseline_cyan
                  << "; current_cyan=" << current_cyan << std::endl;
        if (baseline_cyan < 10u ||
            (step_ == Step::CollisionOff &&
             (current_cyan != 0u || captures[pane] == collision_on_captures_[pane])) ||
            (step_ == Step::CollisionMaxOpacity &&
             (current_cyan == 0u || captures[pane] == collision_on_captures_[pane])) ||
            ((step_ == Step::CollisionRestored || step_ == Step::CollisionOpacityRestored) &&
             captures[pane] != collision_on_captures_[pane])) {
          fail("Collision visibility or opacity failed to change its cyan geometry and restore the accepted pane pixels");
          return false;
        }
      }
    }
    if (step_ == Step::ObjectSingletonCustom) {
      // A style mutation can rebuild proxy raster state. Preserve the ordinary
      // silhouette's color and physical width while allowing a one-pixel edge
      // coverage decision; unchanged-frame checks below remain pixel-exact.
      for (std::size_t pane = 0; pane < captures.size(); ++pane) {
        const auto &before = previous_captures_[pane];
        const auto &after = captures[pane];
        const QRect bounds = object_rectangle(pane).intersected(after.rect().adjusted(1, 1, -1, -1));
        std::array<std::size_t, 2> orange{}, cyan{};
        const auto ordinary = [](int value) { return value == 3 || value == 4; };
        for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
          for (int x = bounds.left(); x <= bounds.right(); ++x) {
            const int old_hue = hue(before.pixelColor(x, y));
            const int new_hue = hue(after.pixelColor(x, y));
            // Display encoding moves the orange core into the broad yellow
            // class. Include that core rather than measuring only its red AA fringe.
            orange[0] += ordinary(old_hue); orange[1] += ordinary(new_hue);
            cyan[0] += old_hue == 2; cyan[1] += new_hue == 2;
            for (const bool reverse : {false, true}) {
              if (!ordinary(reverse ? new_hue : old_hue)) continue;
              bool matched = false;
              const auto &target = reverse ? before : after;
              for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                  matched |= ordinary(hue(target.pixelColor(x + dx, y + dy)));
                }
              }
              if (!matched) {
                fail("A singleton's ordinary silhouette moved or lost its configured orange color");
                return false;
              }
            }
          }
        }
        if (orange[0] == 0u || orange[1] * 100u < orange[0] * 99u ||
            orange[1] * 100u > orange[0] * 101u || cyan[0] != cyan[1]) {
          std::cerr << "singleton_pane=" << pane << "; ordinary_pixels=" << orange[0] << ',' << orange[1]
                    << "; cyan_pixels=" << cyan[0] << ',' << cyan[1] << std::endl;
          fail("A singleton object adopted the active-only color or changed ordinary silhouette width");
          return false;
        }
      }
    }
    if (step_ == Step::ObjectsActivePrimary) {
      active_primary_captures_ = captures;
      if (pink_pixels(front, object_rectangle(kFront)) < 20u ||
          pink_pixels(front, object_rectangle(kFront)) <=
              pink_pixels(previous_captures_[kFront], object_rectangle(kFront))) {
        fail("Adding another selected object did not enable the primary pink silhouette");
        return false;
      }
    }
    if (step_ == Step::ObjectsActiveOther) {
      if (pink_pixels(front, object_rectangle(kFront)) != 0u ||
          pink_pixels(front, object_rectangle(kFront, true)) < 20u ||
          pink_pixels(front, object_rectangle(kFront, true)) <=
              pink_pixels(active_primary_captures_[kFront], object_rectangle(kFront, true))) {
        fail("Changing the active object did not transfer the pink silhouette");
        return false;
      }
    }
    if (step_ == Step::FacesBase) face_base_captures_ = captures;
    if (step_ == Step::FacesSelected || step_ == Step::FacesHovered) {
      // Keep face samples away from the source-wire diagonals retained by
      // the component frame; their separately styled coverage is not face tint.
      constexpr std::array<QVec3, 4> kSamples{{
          {-1.35f, -0.15f, 0.6f}, {-0.9f, -0.1f, 0.6f},
          {-0.35f, -0.15f, 0.6f}, {-0.9f, 0.25f, 0.8f}}};
      const QColor left = world_sample(front, kFront, kSamples[0]);
      const QColor overlap = world_sample(front, kFront, kSamples[1]);
      const QColor right = world_sample(front, kFront, kSamples[2]);
      const QColor blocker = world_sample(front, kFront, kSamples[3]);
      const QColor blocked_base = world_sample(face_base_captures_[kFront], kFront, kSamples[3]);
      // The configured .35 opacity becomes89/255 in the packed overlay color.
      // Linear OVER on black and one sRGB encode yields about159 display codes.
      const int tint = static_cast<int>(std::lround(
          (1.055 * std::pow(89.0 / 255.0, 1.0 / 2.4) - 0.055) * 255.0));
      const QColor active(0, tint, tint);
      const QColor ordinary(tint, 0, 0);
      const QColor hovered(tint, tint, 0);
      const bool priority = step_ == Step::FacesSelected
          ? near_colors(left, active) && near_colors(right, ordinary) && near_colors(left, overlap)
          : near_colors(left, active) && near_colors(right, hovered) && near_colors(right, overlap);
      if (!priority || !near_colors(blocker, blocked_base)) {
        for (std::size_t index = 0; index < kSamples.size(); ++index) {
          const auto projected = session_.viewport_workspace().camera(kPanes[kFront])
              .project_world_point(kSamples[index]);
          const auto actual = world_sample(front, kFront, kSamples[index]);
          const auto base = world_sample(face_base_captures_[kFront], kFront, kSamples[index]);
          std::cerr << "face_sample=" << index << "; projected=" << projected.position.x << ',' << projected.position.y
                    << "; actual_rgb=" << actual.red() << ',' << actual.green() << ',' << actual.blue()
                    << "; base_rgb=" << base.red() << ',' << base.green() << ',' << base.blue()
                    << "; expected_tint=" << tint << std::endl;
        }
        fail("Component face priority accumulated tints or crossed opaque source depth");
        return false;
      }
      if (step_ == Step::FacesSelected) face_selected_captures_ = captures;
    }
    if (step_ == Step::FacesHoverCleared && captures != face_selected_captures_) {
      fail("Clearing component hover did not restore the accepted selected-face pixels");
      return false;
    }
    if (step_ == Step::Edges &&
        (!has_hue_near(front, kFront, {-1.125f, -0.3f, 0.6f}, 2) ||
         !has_hue_near(front, kFront, {-1.125f, 0.35f, 0.6f}, 4))) {
      fail("Active and ordinary component edges did not reach their source spans");
      return false;
    }
    if ((step_ == Step::Vertices || step_ == Step::VertexHovered) &&
        (!has_hue_near(front, kFront, {-1.6f, -0.3f, 0.6f}, 2) ||
         !has_hue_near(front, kFront, {-0.65f, -0.3f, 0.6f}, step_ == Step::Vertices ? 4 : 3))) {
      fail("Selected/active/hover vertex markers lost their visible authored positions");
      return false;
    }
    if (step_ == Step::ActivePreviewBase) active_preview_base_captures_ = captures;
    if (step_ == Step::ActivePreview || step_ == Step::ActivePreviewRetained) {
      if (!has_hue_near(front, kFront, {-1.4f, -0.1f, 0.6f}, 2, 3) ||
          !has_hue_near(front, kFront, {-0.45f, -0.1f, 0.6f}, 4, 3) ||
          has_hue_near(front, kFront, {-1.6f, -0.3f, 0.6f}, 2, 3) ||
          has_hue_near(front, kFront, {-0.65f, -0.3f, 0.6f}, 4, 3)) {
        fail("The first active preview candidate retained old selected points or lost moved overlay points");
        return false;
      }
    }
    if (step_ == Step::ActivePreviewRetained && captures != previous_captures_) {
      fail("An unchanged active preview changed retained pane pixels");
      return false;
    }
    if (step_ == Step::ActivePreviewCancel && captures != active_preview_base_captures_) {
      fail("Cancelling the active preview did not restore the source mesh and overlay pixels");
      return false;
    }
    if (step_ == Step::GridBase) grid_base_captures_ = captures;
    if (step_ == Step::Grid) {
      std::size_t panes_with_grid = 0u;
      for (std::size_t pane = 0; pane < captures.size(); ++pane) {
        std::size_t changed = 0u;
        for (int y = 0; y < captures[pane].height(); ++y) {
          for (int x = 0; x < captures[pane].width(); ++x) {
            changed += !near_colors(captures[pane].pixelColor(x, y),
                grid_base_captures_[pane].pixelColor(x, y), 12);
          }
        }
        panes_with_grid += changed > 100u;
      }
      if (panes_with_grid < 2u) {
        fail("Ground grid toggling did not change actual pixels in independent panes");
        return false;
      }
    }
    if ((step_ == Step::GridRetained || step_ == Step::GizmoRetained) &&
        captures != previous_captures_) {
      fail("An unchanged grid/gizmo frame changed its retained GPU pixels");
      return false;
    }
    if (step_ == Step::ShadedWire) {
      bool colored = false;
      const QRect bounds = object_rectangle(kFront).intersected(front.rect());
      for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
        for (int x = bounds.left(); x <= bounds.right(); ++x) {
          colored |= hue(front.pixelColor(x, y)) == 2;
        }
      }
      if (!colored) {
        fail("Wireframe-on-Shaded did not display its configured topology color");
        return false;
      }
    }
    if (step_ == Step::Wireframe) {
      // The exact center lies on a legitimate projected topology diagonal.
      // These four interiors avoid both diagonal families while still testing
      // both halves of the face for retained opaque scene color.
      for (const QVec3 world : std::array<QVec3, 4>{{
               {0.4f, 0.1f, 0}, {1.15f, 0.1f, 0},
               {0.4f, -0.1f, 0}, {1.15f, -0.1f, 0}}}) {
        const QColor interior = world_sample(front, kFront, world);
        if (std::max({interior.red(), interior.green(), interior.blue()}) > 12) {
          const auto projected = session_.viewport_workspace().camera(kPanes[kFront]).project_world_point(world);
          std::cerr << "wireframe_interior=" << projected.position.x << ',' << projected.position.y
                    << "; actual_rgb=" << interior.red() << ',' << interior.green() << ',' << interior.blue() << std::endl;
          fail("Wireframe retained an opaque mesh interior away from authored wires");
          return false;
        }
      }
    }
    if (step_ == Step::Gizmo || step_ == Step::GizmoCamera) {
      const auto pivot = session_.viewport_workspace().camera(ViewportPaneId::Front)
          .project_world_point({0.8f, 0, 0});
      std::array<std::size_t, 7> counts{};
      const QRect bounds(qRound(pivot.position.x) - 100, qRound(pivot.position.y) - 100, 201, 201);
      const QRect clipped = bounds.intersected(front.rect());
      for (int y = clipped.top(); y <= clipped.bottom(); ++y) {
        for (int x = clipped.left(); x <= clipped.right(); ++x) {
          ++counts[static_cast<std::size_t>(hue(front.pixelColor(x, y)))];
        }
      }
      if (static_cast<int>(counts[4] > 6u) + static_cast<int>(counts[5] > 6u) +
          static_cast<int>(counts[6] > 6u) < 2) {
        fail("The Move gizmo did not display independent semantic axes");
        return false;
      }
    }
    for (std::size_t pane = 0; pane < captures.size(); ++pane) {
      if (!compare_reference(captures[pane], pane)) return false;
    }
    return true;
  }

#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
  bool diagnostics(CrimsonRenderDiagnostics &value) {
    if (!crimson_render_diagnostics(*host_, value)) {
      fail("The facade did not retain the requested Crimson host");
      return false;
    }
    const bool expected_rejection = (step_ == Step::HostRejected || step_ == Step::HostRetry) &&
        value.error.find("out-of-range triangle index") != std::string::npos;
    if (!value.error.empty() && !expected_rejection) {
      fail(value.error);
      return false;
    }
    return true;
  }

  bool prepare_host_rejection() {
    const auto *selected = snapshots_.selected_object_snapshot();
    if (selected == nullptr) {
      fail("The accepted host fixture has no selected-object scene binding");
      return false;
    }
    host_retry_scene_ = build_render_scene_snapshot(session_.scene(), session_.assets(),
        {.scene_binding = selected->scene_binding});
    if (host_retry_scene_.meshes.empty() || !host_retry_scene_.meshes.front().mesh.geometry_payload) {
      fail("The host rejection fixture has no pinned ordinary mesh");
      return false;
    }
    host_rejected_scene_ = host_retry_scene_;
    auto &mesh = host_rejected_scene_.meshes.front().mesh;
    auto malformed = std::make_shared<MeshGeometryPayload>(*mesh.geometry_payload);
    if (malformed->indices.empty()) {
      fail("The host rejection fixture has no source triangle indices");
      return false;
    }
    malformed->indices.back() = static_cast<std::uint32_t>(malformed->vertices.size());
    mesh.geometry_payload = malformed;
    mesh.indices = malformed->indices;
    ++mesh.mesh_revision;
    (void)settings_.set_color(ViewportColorSettingId::SelectionWire, {0, 1, 1, 1});
    apply_settings();
    return true;
  }
#endif

  void fit_root_to_screen(QSize requested) {
    QScreen *screen = root_->screen();
    if (screen == nullptr) screen = application_.primaryScreen();
    if (screen == nullptr) {
      root_->resize(requested);
      return;
    }
    // Qt reports available geometry in logical coordinates, including under
    // QT_SCALE_FACTOR. Reserve the real native frame before fitting the client.
    (void)root_->winId();
    const QMargins margins = root_->windowHandle()->frameMargins();
    const QRect available = screen->availableGeometry();
    const QSize frame(margins.left() + margins.right(), margins.top() + margins.bottom());
    const QSize capacity(std::max(1, available.width() - frame.width()),
                         std::max(1, available.height() - frame.height()));
    QSize fitted = requested;
    if (requested.width() > capacity.width() || requested.height() > capacity.height()) {
      // Fit both canonical sizes against the larger resize fixture so a small
      // logical desktop still exercises a real change in all pane extents.
      const double scale = std::min({1.0, static_cast<double>(capacity.width()) / 1060.0,
                                    static_cast<double>(capacity.height()) / 740.0});
      fitted = {std::max(1, static_cast<int>(std::floor(requested.width() * scale))),
                std::max(1, static_cast<int>(std::floor(requested.height() * scale)))};
    }
    root_->resize(fitted);
    root_->move(available.center() - QPoint((fitted.width() + frame.width()) / 2,
                                            (fitted.height() + frame.height()) / 2));
    std::cout << "editor_window; requested=" << requested.width() << ',' << requested.height()
              << "; fitted=" << fitted.width() << ',' << fitted.height()
              << "; available=" << available.width() << ',' << available.height()
              << "; device_pixel_ratio=" << screen->devicePixelRatio() << std::endl;
  }

  void create_root() {
    root_ = std::make_unique<QWidget>();
    root_->setWindowFlag(Qt::WindowStaysOnTopHint);
    root_->setWindowTitle(QStringLiteral("Quader %1 public editor adapter gate")
        .arg(QString::fromLatin1(kBackendName)));
    auto *layout = new QVBoxLayout(root_.get());
    layout->setContentsMargins(0, 0, 0, 0);
    panel_ = new QtViewportPanel(root_.get(), true);
    panel_->attach_workspace(session_.viewport_workspace());
    panel_->set_viewport_settings(settings_);
    layout->addWidget(panel_);
    anchor_ = new QtVulkanCompositionWidget(root_.get());
    anchor_->setFixedSize(1, 1);
    anchor_->setFixedColorBufferSize({1, 1});
    anchor_->setFocusPolicy(Qt::NoFocus);
    anchor_->setAttribute(Qt::WA_TransparentForMouseEvents);
    anchor_->set_release_callback([this] {
      if (host_->initialized()) {
        qFatal("Editor GPU probe released a borrowed Qt device before its host drained");
      }
      ++release_count_;
    });
    QObject::connect(anchor_, &QRhiWidget::frameSubmitted, anchor_, [this] {
      if (host_->initialized()) {
        host_->complete_composited_presentation_frame();
      }
    });
    for (std::size_t index = 0; index < kPanes.size(); ++index) {
      const QString name = QStringLiteral("viewportSurfaceContainer_%1")
          .arg(QString::fromUtf8(viewport_pane_stable_id(kPanes[index]).data()));
      pane_widgets_[index] = dynamic_cast<QtVulkanCompositionWidget *>(panel_->findChild<QWidget *>(name));
      if (pane_widgets_[index] == nullptr) {
        fail("The production pane is missing its composed widget");
        return;
      }
      QObject::connect(pane_widgets_[index], &QRhiWidget::frameSubmitted, root_.get(), [this, index] {
        submitted_generations_[index] = requested_generation_;
        presentation_clock_.restart();
      });
    }
    panel_->set_composed_frame_prepare_callback([this] {
      if (initialized_ && !preparing_) {
        prepare_frame();
      }
    });
    panel_->set_presentation_requested_callback([this] {
      if (!preparing_) {
        needs_prepare_ = true;
      }
    });
    // Fitting queries the native frame. Construct every Vulkan child first so
    // Qt selects the Vulkan surface and backing store when winId() creates it.
    fit_root_to_screen({920, 680});
    ++root_count_;
    root_->show();
    anchor_->show();
    anchor_->lower();
  }

  void trace_retirement(std::string_view milestone) const {
    std::cout << "editor_retirement; backend=" << kBackendName << "; root=" << root_count_
              << "; milestone=" << milestone << "; root_present=" << (root_ != nullptr) << std::endl;
  }

  void retire_root() {
    if (!root_) {
      return;
    }
    trace_retirement("retire_root_enter");
    initialized_ = false;
    if (panel_ != nullptr) {
      panel_->set_presentation_requested_callback({});
      panel_->set_composed_frame_prepare_callback({});
      panel_->reset_interactions(&editor_, &session_.native_tools());
      trace_retirement("before_panel_shutdown");
      panel_->shutdown();
      trace_retirement("after_panel_shutdown");
    }
    if (sink_) {
      trace_retirement("before_sink_shutdown");
      sink_->shutdown();
      trace_retirement("after_sink_shutdown");
      trace_retirement("before_sink_reset");
      sink_.reset();
      trace_retirement("after_sink_reset");
    }
    trace_retirement("before_snapshot_reset");
    snapshots_.reset();
    trace_retirement("after_snapshot_reset");
    if (host_) {
      // Production shutdown is fatal if completion cannot be proved. This call
      // precedes the widget release callback and destruction of Qt's device.
      trace_retirement("before_host_shutdown");
      host_->shutdown();
      trace_retirement("after_host_shutdown");
    }
    if (anchor_ != nullptr) {
      anchor_->set_render_system(nullptr);
    }
    trace_retirement("before_root_reset");
    root_.reset();
    trace_retirement("after_root_reset");
    preparation_count_ = 0u;
    preparation_frame_baseline_ = 0u;
    panel_ = nullptr;
    anchor_ = nullptr;
    pane_widgets_.fill(nullptr);
    submitted_generations_.fill(0u);
    trace_retirement("retire_root_exit");
  }

  bool initialize_host() {
    const auto *context = anchor_->shared_context();
    if (context == nullptr) {
      return false;
    }
    if (!host_->initialize(nullptr, resources_, {.backend = RendererBackend::Vulkan}, context)) {
      fail("The renderer facade could not initialize on Qt's real device");
      return false;
    }
    sink_ = host_->create_scene_sink(resources_);
    if (!sink_ || !panel_->initialize(*host_)) {
      fail("The four-pane facade resources could not initialize");
      return false;
    }
    anchor_->set_render_system(host_.get());
    panel_->set_startup_presentation_blocked(false);
    initialized_ = true;
    needs_prepare_ = true;
    std::cout << "backend=" << kBackendName << "; borrowed_qt_device=" << context->device
              << "; root=" << root_count_ << "; panes=4" << std::endl;
    return true;
  }

  void prepare_frame() {
    if (!initialized_ || preparing_) {
      return;
    }
    preparing_ = true;
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    CrimsonRenderDiagnostics current;
    if (!diagnostics(current)) {
      preparing_ = false;
      return;
    }
    if (preparation_count_ != 0u && current.frame_submissions - preparation_frame_baseline_ > 1u) {
      fail("Pane callbacks advanced multiple global frames within one preparation");
      preparing_ = false;
      return;
    }
    preparation_frame_baseline_ = current.frame_submissions;
#endif
    ++preparation_count_;
    host_->begin_shared_command_batch();
    if (accept(QuaderRenderApiRuntimeAccess::sync_scene(editor_.render(), *sink_))) {
      panel_->prepare_viewports(editor_, *sink_, snapshots_, session_.scene(),
          session_.assets(), session_.modeling(), session_.native_tools());
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
      // Inject after the ordinary source sync and after new editor semantics
      // are staged. Every nested preparation must retain this rejection until
      // the explicit retry, so a later pane cannot silently replace the source.
      if (step_ == Step::HostRejected) sink_->sync(host_rejected_scene_);
      if (step_ == Step::HostRetry && !host_retry_staged_) {
        sink_->sync(host_retry_scene_);
        host_retry_staged_ = true;
      }
#endif
      host_->begin_presentation_frame();
      (void)panel_->render_viewports(false);
      anchor_->update();
    }
    host_->end_shared_command_batch();
    preparing_ = false;
  }

  bool verify_pane(std::size_t index, QImage &offscreen) {
    auto *widget = pane_widgets_[index];
    const auto *context = widget->shared_context();
    const auto *root_context = anchor_->shared_context();
    if (context == nullptr || root_context == nullptr || context->device != root_context->device ||
        context->graphics_queue != root_context->graphics_queue) {
      fail("The pane did not borrow the one root Vulkan device/queue");
      return false;
    }
    QScreen *screen = root_->windowHandle()->screen();
    const QPoint global_origin = widget->mapToGlobal(QPoint{});
    if (!screen->availableGeometry().contains(QRect(global_origin, widget->size()))) {
      fail("A composed pane extends outside the available logical desktop");
      return false;
    }
    const QPoint origin = global_origin - screen->geometry().topLeft();
    const QImage presented = screen->grabWindow(0, origin.x(), origin.y(),
                                               widget->width(), widget->height()).toImage();
    offscreen = widget->grabFramebuffer().convertToFormat(QImage::Format_RGBA8888);
    const QString stem = QStringLiteral("editor-%1-pane-%2-")
        .arg(static_cast<int>(step_)).arg(index);
    const bool saved = presented.save(QDir(output_directory_).filePath(stem + QStringLiteral("presented.png"))) &&
        offscreen.save(QDir(output_directory_).filePath(stem + QStringLiteral("offscreen.png")));
    if (!saved || presented.isNull() || offscreen.isNull()) {
      fail("Cannot capture/save the real composed pane");
      return false;
    }
    std::size_t foreground = 0u, compared_foreground = 0u;
    for (int y = 0; y < offscreen.height(); ++y) {
      for (int x = 0; x < offscreen.width(); ++x) {
        const QColor color = offscreen.pixelColor(x, y);
        foreground += std::max({color.red(), color.green(), color.blue()}) > 24;
      }
    }
    const auto area = static_cast<std::size_t>(offscreen.width()) * offscreen.height();
    if (foreground < area / 500u || foreground > area * 4u / 5u) {
      fail("A pane lost its mesh or drew unrelated full-target pixels");
      return false;
    }
    // Pane controls occupy the outer strip. Compare content samples only;
    // the offscreen path contains no Qt chrome, and its pixels remain the oracle.
    for (int row = 3; row < 18; ++row) {
      for (int column = 3; column < 18; ++column) {
        const int x = column * offscreen.width() / 20;
        const int y = row * offscreen.height() / 20;
        const QColor expected = offscreen.pixelColor(x, y);
        const QColor actual = presented.pixelColor(
            column * presented.width() / 20, row * presented.height() / 20);
        if (std::abs(expected.red() - actual.red()) > 4 ||
            std::abs(expected.green() - actual.green()) > 4 ||
            std::abs(expected.blue() - actual.blue()) > 4) {
          std::cerr << "pane=" << index << "; sample=" << column << ',' << row
                    << "; offscreen=" << expected.red() << ',' << expected.green() << ',' << expected.blue()
                    << "; presented=" << actual.red() << ',' << actual.green() << ',' << actual.blue() << std::endl;
          fail("Presented pane content disagrees with the GPU target");
          return false;
        }
        compared_foreground += std::max({expected.red(), expected.green(), expected.blue()}) > 24;
      }
    }
    // Sparse Wireframe edges can fall between every regular grid sample.
    // Keep those comparisons and also prove that actual foreground pixels in
    // independent content cells reached the desktop with the same RGB bounds.
    const QRect content_region(
        QPoint(offscreen.width() * 3 / 20, offscreen.height() * 3 / 20),
        QPoint(offscreen.width() * 17 / 20, offscreen.height() * 17 / 20));
    for (int row = 0; row < 8; ++row) {
      for (int column = 0; column < 8; ++column) {
        const QRect cell(
            content_region.left() + content_region.width() * column / 8,
            content_region.top() + content_region.height() * row / 8,
            content_region.width() * (column + 1) / 8 - content_region.width() * column / 8,
            content_region.height() * (row + 1) / 8 - content_region.height() * row / 8);
        bool sampled = false;
        for (int y = cell.top(); y <= cell.bottom() && !sampled; ++y) {
          for (int x = cell.left(); x <= cell.right(); ++x) {
            const QColor expected = offscreen.pixelColor(x, y);
            if (std::max({expected.red(), expected.green(), expected.blue()}) <= 24) continue;
            const QPoint presented_pixel(
                static_cast<int>((static_cast<std::int64_t>(2 * x + 1) * presented.width()) / (2 * offscreen.width())),
                static_cast<int>((static_cast<std::int64_t>(2 * y + 1) * presented.height()) / (2 * offscreen.height())));
            const QColor actual = presented.pixelColor(presented_pixel);
            if (!near_colors(expected, actual, 4)) {
              std::cerr << "pane=" << index << "; foreground_sample=" << x << ',' << y
                        << "; offscreen=" << expected.red() << ',' << expected.green() << ',' << expected.blue()
                        << "; presented=" << actual.red() << ',' << actual.green() << ',' << actual.blue() << std::endl;
              fail("Presented foreground disagrees with an actual GPU edge or surface pixel");
              return false;
            }
            ++compared_foreground;
            sampled = true;
            break;
          }
        }
      }
    }
    if (compared_foreground == 0u) {
      fail("Presented comparisons missed all mesh pixels");
      return false;
    }
    QVec3 expected_center = step_ >= Step::Pose ? QVec3{0.8f, 0, 0} : QVec3{};
    if (step_ == Step::Preview) {
      expected_center += kPreviewDelta;
    }
    const auto projected = session_.viewport_workspace().camera(kPanes[index]).project_world_point(expected_center);
    if (!projected.visible) {
      fail("The canonical camera lost the expected scene center");
      return false;
    }
    const QVec2 render_size = panel_->surface(kPanes[index])->render_size_pixels();
    const QPoint center(qRound(projected.position.x * offscreen.width() / render_size.x),
                        qRound(projected.position.y * offscreen.height() / render_size.y));
    bool covered = false;
    for (int y = -3; y <= 3; ++y) {
      for (int x = -3; x <= 3; ++x) {
        const QPoint pixel = center + QPoint(x, y);
        if (offscreen.rect().contains(pixel)) {
          const QColor color = offscreen.pixelColor(pixel);
          covered |= std::max({color.red(), color.green(), color.blue()}) > 24;
        }
      }
    }
    if (!covered && step_ != Step::Wireframe) {
      fail("The GPU pane did not cover the API scene/preview center projected by its own camera");
      return false;
    }
    return true;
  }

  bool verify_frame() {
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    CrimsonRenderDiagnostics after;
    if (!diagnostics(after) || after.active_views != 4u || after.accepted_scene_generation == 0u) {
      fail("Four independent views did not accept the shared scene");
      return false;
    }
#endif
    std::array<QImage, 4> captures;
    for (std::size_t index = 0; index < captures.size(); ++index) {
      if (!verify_pane(index, captures[index])) {
        return false;
      }
    }
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    if (!diagnostics(after)) {
      return false;
    }
    for (const auto &view : after.editor_views) {
      std::cout << "accepted_editor_view; step=" << static_cast<int>(step_)
                << "; layer=" << static_cast<unsigned>(view.layer_mask)
                << "; identity=" << view.frame_identity << "; revision=" << view.frame_revision
                << "; scene_generation=" << view.scene_generation
                << "; masks=" << view.ordinary_masks << ',' << view.active_masks
                << "; ordinary_rgba=" << view.ordinary_color[0] << ',' << view.ordinary_color[1]
                << ',' << view.ordinary_color[2] << ',' << view.ordinary_color[3]
                << "; active_rgba=" << view.active_color[0] << ',' << view.active_color[1]
                << ',' << view.active_color[2] << ',' << view.active_color[3] << std::endl;
    }
    const std::uint64_t submitted = after.frame_submissions - before_.frame_submissions;
    const std::uint64_t preparations = preparation_count_ - step_preparation_baseline_;
    if (after.frame_submissions - preparation_frame_baseline_ > 1u ||
        submitted > preparations || (step_ == Step::Initial && submitted == 0u)) {
      fail("Pane callbacks advanced more than one global renderer frame for one preparation");
      return false;
    }
    if (step_ == Step::HostRejected &&
        (after.error.find("out-of-range triangle index") == std::string::npos || submitted != 0u ||
         after.accepted_scene_generation != before_.accepted_scene_generation ||
         after.accepted_editor_scene_generation != before_.accepted_editor_scene_generation)) {
      fail("A rejected scene published dirty editor state over the prior accepted host frame");
      return false;
    }
    if (step_ == Step::HostRetry &&
        (!after.error.empty() || submitted != 1u ||
         after.accepted_editor_scene_generation != after.accepted_scene_generation)) {
      fail("A corrected host candidate failed to publish scene and editor state together");
      return false;
    }
    if (step_ == Step::Unchanged || step_ == Step::Camera || step_ == Step::Pose ||
        step_ == Step::GridCamera || step_ == Step::GridRetained ||
        step_ == Step::GizmoCamera || step_ == Step::GizmoRetained ||
        step_ == Step::ActivePreviewRetained) {
      if (after.translation.position_packs != before_.translation.position_packs ||
          after.translation.attribute_packs != before_.translation.attribute_packs ||
          after.translation.index_packs != before_.translation.index_packs ||
          after.translation.image_decodes != before_.translation.image_decodes) {
        fail("Unchanged/camera/pose preparation repacked retained geometry or decoded its image");
        return false;
      }
    }
    if (step_ == Step::GridCamera || step_ == Step::GridRetained || step_ == Step::GizmoRetained ||
        step_ == Step::ActivePreviewRetained) {
      if (after.editor_projection.position_packs != before_.editor_projection.position_packs ||
          after.editor_projection.attribute_packs != before_.editor_projection.attribute_packs ||
          after.editor_projection.index_packs != before_.editor_projection.index_packs ||
          after.editor_gpu.vertex_uploads != before_.editor_gpu.vertex_uploads ||
          after.editor_gpu.index_uploads != before_.editor_gpu.index_uploads) {
        fail("Camera/unchanged editor preparation rebuilt retained overlay streams");
        return false;
      }
    }
    if ((step_ == Step::GridRetained || step_ == Step::GizmoRetained ||
         step_ == Step::ActivePreviewRetained) && submitted != 0u) {
      fail("An unchanged editor overlay advanced another shared frame");
      return false;
    }
    if (step_ >= Step::ActivePreviewBase && step_ <= Step::ActivePreviewCancel &&
        after.accepted_editor_scene_generation != after.accepted_scene_generation) {
      fail("Editor overlays did not use the same accepted scene generation as their visible mesh");
      return false;
    }
    if (step_ == Step::ActivePreview &&
        (submitted != 1u || after.accepted_scene_generation == before_.accepted_scene_generation ||
         after.translation.position_packs == before_.translation.position_packs)) {
      fail("The active preview needed a later shared submission instead of its first accepted candidate");
      return false;
    }
#endif
    std::size_t changed = 0u;
    for (std::size_t index = 0; index < captures.size(); ++index) {
      changed += captures[index] != previous_captures_[index];
    }
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    if (step_ == Step::Unchanged && submitted != 0u) {
      fail("An unchanged preparation submitted another shared frame");
      return false;
    }
#endif
    if (step_ == Step::Unchanged && changed != 0u) {
      fail("An unchanged preparation changed pane pixels or submitted another shared frame");
      return false;
    }
    if (step_ == Step::HostRejected && changed != 0u) {
      fail("A rejected host source changed pixels in a previously accepted pane");
      return false;
    }
    if (step_ == Step::HostRetry &&
        (changed == 0u || !has_hue_near(captures[2], 2u, {0.2f, -0.35f, 0.45f}, 2))) {
      fail("The corrected host retry did not display its newly staged cyan silhouette");
      return false;
    }
    if ((step_ == Step::Pose && changed < 2u) || (step_ == Step::Preview && changed < 3u)) {
      fail("API placement/preview changes did not reach independent pane pixels");
      return false;
    }
    if (step_ == Step::ObjectsActivePrimary || step_ == Step::ObjectsActiveOther) {
      std::cout << "object_selection_pixels; step=" << static_cast<int>(step_)
                << "; primary_pink=" << pink_pixels(captures[2], object_rectangle(2u))
                << "; other_pink=" << pink_pixels(captures[2], object_rectangle(2u, true)) << std::endl;
      if (const auto *selected = snapshots_.selected_object_snapshot()) {
        for (const auto &mesh : selected->meshes) {
          std::cout << "object_selection_source; step=" << static_cast<int>(step_)
                    << "; entity=" << mesh.entity << "; selected=" << mesh.selected
                    << "; active=" << mesh.active_selected << std::endl;
        }
      }
    }
    if (!verify_overlay_frame(captures)) return false;
    previous_captures_ = std::move(captures);
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
    std::cout << "PASS editor_step=" << static_cast<int>(step_)
              << "; active_views=" << after.active_views
              << "; global_submits=" << submitted << "; preparations=" << preparations
              << "; scene_generation=" << after.accepted_scene_generation
              << "; packs=" << after.translation.position_packs << ','
              << after.translation.attribute_packs << ',' << after.translation.index_packs
              << "; image_decodes=" << after.translation.image_decodes
              << "; editor_position_packs=" << after.editor_projection.position_packs
              << "; editor_attribute_packs=" << after.editor_projection.attribute_packs
              << "; editor_index_packs=" << after.editor_projection.index_packs
              << "; editor_vertex_uploads=" << after.editor_gpu.vertex_uploads
              << "; editor_index_uploads=" << after.editor_gpu.index_uploads
              << "; editor_overlays_pending=" << after.editor_overlays_pending
              << "; all_panes_presented_and_gpu=verified" << std::endl;
#else
    std::cout << "PASS editor_step=" << static_cast<int>(step_)
              << "; backend=FullFilament; bgfx_counters=inapplicable"
              << "; all_panes_presented_and_gpu=verified" << std::endl;
#endif
    return true;
  }

  void next(Step step) {
    step_ = step;
    needs_prepare_ = true;
    requested_generation_ = 0u;
    settle_clock_.restart();
    step_clock_.restart();
  }

  void finish() {
    retire_root();
    timer_.stop();
    if (release_count_ != 2u) {
      fail("The replacement editor root did not release its Vulkan device");
    }
    std::cout << "roots=" << root_count_ << "; releases=" << release_count_
              << "; four_pane_epochs=" << next_generation_
              << "; overlay_parity=" << overlay_parity_ << std::endl;
    application_.exit(error_.empty() ? 0 : 1);
  }

  void advance() {
    if (!error_.empty() || step_clock_.elapsed() > 25000) {
      if (error_.empty()) {
        fail("Timed out waiting for four current presented panes");
      }
      retire_root();
      timer_.stop();
      application_.exit(1);
      return;
    }
    if (step_ == Step::Hidden) {
      if (settle_clock_.elapsed() >= 150) {
        root_->show();
        next(Step::Reshow);
      }
      return;
    }
    if (!initialized_ && !initialize_host()) {
      return;
    }
    if (needs_prepare_) {
      if (settle_clock_.elapsed() < 100) {
        return;
      }
      if (requested_generation_ == 0u) {
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
        if (!diagnostics(before_)) {
          return;
        }
#endif
        step_preparation_baseline_ = preparation_count_;
      }
      requested_generation_ = ++next_generation_;
      needs_prepare_ = false;
      prepare_frame();
      return;
    }
    if (requested_generation_ == 0u ||
        !std::ranges::all_of(submitted_generations_, [this](std::uint64_t value) {
          return value == requested_generation_;
        }) || !presentation_clock_.isValid() || presentation_clock_.elapsed() < 100) {
      return;
    }
    if (!verify_frame()) {
      return;
    }
    switch (step_) {
    case Step::Initial: next(Step::Unchanged); break;
    case Step::Unchanged:
      if (accept(editor_.scene().set_transform(object_.entity, {.location = {0.8f, 0, 0}}))) {
        next(Step::Pose);
      }
      break;
    case Step::Pose:
      if (accept(editor_.mode().set(QuaderEditorMode::Edit)) &&
          accept(editor_.selection().set_mode(QuaderSelectionMode::Vertex)) &&
          select_preview_box_vertices() &&
          accept(editor_.modeling().begin_transform_preview()) &&
          accept(editor_.modeling().update_transform_translate(kPreviewDelta))) {
        next(Step::Preview);
      }
      break;
    case Step::Preview:
      if (accept(editor_.modeling().cancel_transform_preview())) {
        next(Step::Cancel);
      }
      break;
    case Step::Cancel:
      if (!session_.viewport_workspace().camera(ViewportPaneId::Perspective).restore_perspective_pose(
              {.eye = {5, 3, 5}, .target = {0.3f, 0.5f, 0}, .up = {0, 1, 0}})) {
        fail("The canonical camera rejected the probe pose");
        break;
      }
      next(Step::Camera);
      break;
    case Step::Camera:
      {
        const QSize previous_size = root_->size();
        fit_root_to_screen({1060, 740});
        if (root_->size() == previous_size) {
          fail("The available desktop did not admit a real four-pane resize");
          break;
        }
      }
      next(Step::Resize);
      break;
    case Step::Resize:
      root_->hide();
      step_ = Step::Hidden;
      settle_clock_.restart();
      step_clock_.restart();
      break;
    case Step::Reshow:
      retire_root();
      if (release_count_ != 1u) {
        fail("The first editor root did not release its Vulkan device");
        break;
      }
      next(Step::Replaced);
      create_root();
      break;
    case Step::Replaced:
      if (overlay_parity_) {
        if (begin_overlay_fixture()) next(Step::ObjectSingleton);
      } else {
        finish();
      }
      break;
    case Step::ObjectSingleton:
      settings_.set_bool_value(ViewportBoolSettingId::ShowCollisions, false);
      apply_settings();
      next(Step::CollisionOff);
      break;
    case Step::CollisionOff:
      settings_.set_bool_value(ViewportBoolSettingId::ShowCollisions, true);
      apply_settings();
      next(Step::CollisionRestored);
      break;
    case Step::CollisionRestored: {
      auto maximum_opacity = collision_color_;
      maximum_opacity.a = 1.0f;
      (void)settings_.set_color(ViewportColorSettingId::CollisionFill, maximum_opacity);
      apply_settings();
      next(Step::CollisionMaxOpacity);
      break;
    }
    case Step::CollisionMaxOpacity:
      (void)settings_.set_color(ViewportColorSettingId::CollisionFill, collision_color_);
      apply_settings();
      next(Step::CollisionOpacityRestored);
      break;
    case Step::CollisionOpacityRestored:
      (void)settings_.set_color(ViewportColorSettingId::ActiveSelection, {0, 1, 1, 1});
      (void)settings_.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 7.0f);
      apply_settings();
      next(Step::ObjectSingletonCustom);
      break;
    case Step::ObjectSingletonCustom:
      (void)settings_.set_color(ViewportColorSettingId::ActiveSelection, {1, 105.0f / 255.0f, 180.0f / 255.0f, 1});
      (void)settings_.set_float_value(ViewportFloatSettingId::ActiveSelectionLineSize, 2.0f);
      apply_settings();
      if (accept(editor_.selection().select_objects({object_.entity, other_object_.entity}, object_.entity))) {
        next(Step::ObjectsActivePrimary);
      }
      break;
    case Step::ObjectsActivePrimary:
      if (accept(editor_.selection().select_objects({object_.entity, other_object_.entity}, other_object_.entity))) {
        next(Step::ObjectsActiveOther);
      }
      break;
    case Step::ObjectsActiveOther:
      if (begin_face_fixture()) next(Step::FacesBase);
      break;
    case Step::FacesBase:
      if (accept(editor_.selection().select_components({
              .object = component_object_, .kind = QuaderComponentKind::Face,
              .faces = {component_faces_[0], component_faces_[1]},
              .active = {.object = component_object_, .kind = QuaderComponentKind::Face,
                         .face = component_faces_[0]}}))) {
        next(Step::FacesSelected);
      }
      break;
    case Step::FacesSelected:
      if (accept(editor_.selection().hover_face(component_object_, component_faces_[1]))) {
        next(Step::FacesHovered);
      }
      break;
    case Step::FacesHovered:
      if (accept(editor_.selection().clear_hover())) next(Step::FacesHoverCleared);
      break;
    case Step::FacesHoverCleared:
      if (accept(editor_.selection().set_mode(QuaderSelectionMode::Edge)) &&
          accept(editor_.selection().select_components({
              .object = component_object_, .kind = QuaderComponentKind::Edge,
              .edges = {{component_vertices_[0], component_vertices_[1]},
                        {component_vertices_[3], component_vertices_[2]}},
              .active = {.object = component_object_, .kind = QuaderComponentKind::Edge,
                         .edge_a = component_vertices_[0], .edge_b = component_vertices_[1]}}))) {
        next(Step::Edges);
      }
      break;
    case Step::Edges:
      if (accept(editor_.selection().set_mode(QuaderSelectionMode::Vertex)) &&
          accept(editor_.selection().select_components({
              .object = component_object_, .kind = QuaderComponentKind::Vertex,
              .vertices = {component_vertices_[0], component_vertices_[1]},
              .active = {.object = component_object_, .kind = QuaderComponentKind::Vertex,
                         .vertex = component_vertices_[0]}}))) {
        next(Step::Vertices);
      }
      break;
    case Step::Vertices:
      if (accept(editor_.selection().hover_vertex(component_object_, component_vertices_[1]))) {
        next(Step::VertexHovered);
      }
      break;
    case Step::VertexHovered:
      if (accept(editor_.selection().clear_hover())) next(Step::ActivePreviewBase);
      break;
    case Step::ActivePreviewBase:
      if (accept(editor_.modeling().begin_transform_preview()) &&
          accept(editor_.modeling().update_transform_translate({0.2f, 0.2f, 0}))) {
        next(Step::ActivePreview);
      }
      break;
    case Step::ActivePreview: next(Step::ActivePreviewRetained); break;
    case Step::ActivePreviewRetained:
      if (accept(editor_.modeling().cancel_transform_preview())) next(Step::ActivePreviewCancel);
      break;
    case Step::ActivePreviewCancel:
      if (
          accept(editor_.mode().set(QuaderEditorMode::Object)) &&
          accept(editor_.selection().clear())) {
        (void)settings_.set_color(ViewportColorSettingId::GridMinor, {0, 0.55f, 0, 1});
        (void)settings_.set_color(ViewportColorSettingId::GridMajor, {0, 0.7f, 0.7f, 1});
        apply_settings();
        next(Step::GridBase);
      }
      break;
    case Step::GridBase:
      settings_.set_bool_value(ViewportBoolSettingId::ShowGrid, true);
      apply_settings();
      next(Step::Grid);
      break;
    case Step::Grid:
      if (!session_.viewport_workspace().camera(ViewportPaneId::Perspective).restore_perspective_pose(
              {.eye = {4, 4, 6}, .target = {0, 0, 0}, .up = {0, 1, 0}})) {
        fail("The grid camera rejected the requested fixture pose");
      } else {
        next(Step::GridCamera);
      }
      break;
    case Step::GridCamera: next(Step::GridRetained); break;
    case Step::GridRetained:
      settings_.set_bool_value(ViewportBoolSettingId::ShowGrid, false);
      apply_settings();
      if (set_shading(QuaderShadingMode::Solid)) next(Step::Solid);
      break;
    case Step::Solid:
      settings_.set_bool_value(ViewportBoolSettingId::WireframeOnShaded, true);
      (void)settings_.set_color(ViewportColorSettingId::WireframeOnShaded, {0, 1, 1, 1});
      apply_settings();
      if (set_shading(QuaderShadingMode::Shaded)) next(Step::ShadedWire);
      break;
    case Step::ShadedWire:
      settings_.set_bool_value(ViewportBoolSettingId::WireframeOnShaded, false);
      apply_settings();
      if (set_shading(QuaderShadingMode::Wireframe)) next(Step::Wireframe);
      break;
    case Step::Wireframe:
      settings_.set_bool_value(ViewportBoolSettingId::ShowGizmos, true);
      apply_settings();
      if (set_shading(QuaderShadingMode::Shaded) &&
          accept(editor_.selection().select_object(object_)) &&
          accept(editor_.tools().activate(ViewportToolId::Move))) {
        next(Step::Gizmo);
      }
      break;
    case Step::Gizmo:
      if (!session_.viewport_workspace().camera(ViewportPaneId::Perspective).restore_perspective_pose(
              {.eye = {5, 2.5f, 4}, .target = {0.8f, 0, 0}, .up = {0, 1, 0}})) {
        fail("The gizmo camera rejected the requested fixture pose");
      } else {
        next(Step::GizmoCamera);
      }
      break;
    case Step::GizmoCamera: next(Step::GizmoRetained); break;
    case Step::GizmoRetained:
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
      if (prepare_host_rejection()) next(Step::HostRejected);
#else
      finish();
#endif
      break;
    case Step::HostRejected: next(Step::HostRetry); break;
    case Step::HostRetry: finish(); break;
    case Step::Hidden: break;
    }
  }

  QApplication &application_;
  QString output_directory_;
  std::filesystem::path resources_;
  QString reference_directory_;
  bool overlay_parity_ = false;
  SceneSession session_;
  QuaderEditorApi editor_;
  QuaderModelingObjectRef object_;
  QuaderModelingObjectRef other_object_, component_object_;
  std::vector<QuaderElementId> component_faces_, component_vertices_;
  std::unique_ptr<RenderHost> host_;
  std::unique_ptr<RenderSceneSink> sink_;
  RenderOverlaySnapshotCache snapshots_;
  ViewportSettings settings_;
  ViewportSettingColor collision_color_;
  std::unique_ptr<QWidget> root_;
  QtViewportPanel *panel_ = nullptr;
  QtVulkanCompositionWidget *anchor_ = nullptr;
  std::array<QtVulkanCompositionWidget *, 4> pane_widgets_{};
  std::array<std::uint64_t, 4> submitted_generations_{};
  std::array<QImage, 4> previous_captures_;
  std::array<QImage, 4> collision_on_captures_;
  std::array<QImage, 4> active_primary_captures_, face_base_captures_;
  std::array<QImage, 4> face_selected_captures_, grid_base_captures_;
  std::array<QImage, 4> active_preview_base_captures_;
#if !defined(QUADER_FILAMENT_REFERENCE_PROBE)
  CrimsonRenderDiagnostics before_;
  RenderSceneSnapshot host_rejected_scene_, host_retry_scene_;
  bool host_retry_staged_ = false;
#endif
  QTimer timer_;
  QElapsedTimer step_clock_, settle_clock_, presentation_clock_;
  std::uint64_t requested_generation_ = 0u, next_generation_ = 0u;
  std::uint64_t preparation_count_ = 0u, step_preparation_baseline_ = 0u;
  std::uint64_t preparation_frame_baseline_ = 0u;
  std::uint64_t settings_revision_ = 0u;
  std::uint32_t root_count_ = 0u, release_count_ = 0u;
  Step step_ = Step::Initial;
  bool initialized_ = false, preparing_ = false, needs_prepare_ = true;
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
      QStringLiteral("Editor GPU evidence directory."), QStringLiteral("directory"));
  const QCommandLineOption resource_option(QStringLiteral("resource-dir"),
      QStringLiteral("Native runtime directory containing packaged materials."), QStringLiteral("directory"));
  const QCommandLineOption overlay_option(QStringLiteral("overlay-parity"),
      QStringLiteral("Exercise public selection, hover, grid, wire and gizmo GPU contracts."));
  const QCommandLineOption reference_option(QStringLiteral("reference-dir"),
      QStringLiteral("Compare overlay color coverage with the same Full reference captures."), QStringLiteral("directory"));
  parser.addOption(output_option);
  parser.addOption(resource_option);
  parser.addOption(overlay_option);
  parser.addOption(reference_option);
  parser.process(application);
  if (parser.value(output_option).isEmpty() || parser.value(resource_option).isEmpty()) {
    std::cerr << "--output-dir and --resource-dir are required\n";
    return 2;
  }
  quader::EditorCompositionProbe probe(application, parser.value(output_option),
      std::filesystem::path(parser.value(resource_option).toStdWString()),
      parser.isSet(overlay_option), parser.value(reference_option));
  return probe.run();
}
