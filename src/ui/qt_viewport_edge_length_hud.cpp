////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_viewport_edge_length_hud.hpp"

#include "editor/modeling/modeling_types.hpp"
#include "platform/native_window_input.hpp"
#include "render/render_signature_builder.hpp"
#include "viewport/host_viewport_contract_adapter.hpp"
#include "viewport/viewport_camera_adapter.hpp"
#include "viewport/viewport_camera_controller.hpp"
#include "viewport/viewport_layout.hpp"

#include <QByteArray>
#include <QBitmap>
#include <QColor>
#include <QCoreApplication>
#include <QEvent>
#include <QFrame>
#include <QFont>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QImage>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPalette>
#include <QPointF>
#include <QPointer>
#include <QRegion>
#include <QResource>
#include <QSizePolicy>
#include <QStringList>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cstdint>
#include <cmath>
#include <string_view>

// Q_INIT_RESOURCE must remain in the global namespace so its generated symbol
// is resolved from the static quader_ui resource object.
static void initialize_quader_dimension_font_resource() {
  Q_INIT_RESOURCE(quader_dimension_fonts);
}

namespace quader {
namespace {

constexpr int kDimensionLabelGapPixels = 3;
constexpr int kDimensionOutlineRadiusPixels = 1;
constexpr int kDimensionOutlinePaddingPixels = 2;
constexpr double kDefaultDimensionFontSizePoints = 12.0;
constexpr double kMinimumDimensionFontSizePoints = 6.0;
constexpr double kMaximumDimensionFontSizePoints = 24.0;
constexpr auto kDimensionFontResourcePath =
    ":/quader/fonts/HomeVideo-Regular.ttf";
constexpr auto kDimensionFontFallbackFamily = "Home Video";

struct DimensionFontRegistration final {
  QPointer<QCoreApplication> application;
  QString family;
};

const QString &dimension_font_family() {
  static const bool kResourceInitialized = [] {
    initialize_quader_dimension_font_resource();
    return true;
  }();
  (void)kResourceInitialized;

  static DimensionFontRegistration registration;
  QCoreApplication *application = QCoreApplication::instance();
  if (registration.application == application &&
      !registration.family.isEmpty()) {
    return registration.family;
  }

  registration.application = application;
  registration.family = QString::fromLatin1(kDimensionFontFallbackFamily);
  const int font_id = QFontDatabase::addApplicationFont(
      QString::fromLatin1(kDimensionFontResourcePath));
  if (font_id < 0) {
    return registration.family;
  }
  const QStringList families =
      QFontDatabase::applicationFontFamilies(font_id);
  if (!families.isEmpty()) {
    registration.family = families.front();
  }
  return registration.family;
}

QFont dimension_font(double point_size) {
  QFont font{dimension_font_family()};
  font.setPointSizeF(point_size);
  font.setStyleStrategy(static_cast<QFont::StyleStrategy>(
      QFont::NoAntialias | QFont::NoFontMerging));
  font.setHintingPreference(QFont::PreferFullHinting);
  return font;
}

QRect dimension_glyph_bounds(const QFont &font, const QString &text) {
  return QFontMetrics(font).tightBoundingRect(text);
}

QPoint dimension_glyph_baseline(const QRect &glyph_bounds,
                                int outline_padding_pixels) {
  return {outline_padding_pixels - glyph_bounds.left(),
          outline_padding_pixels - glyph_bounds.top()};
}

void fill_dimension_region(QImage &image, const QRegion &region, QRgb color) {
  for (const QRect &source_rect : region) {
    const QRect rect = source_rect.intersected(image.rect());
    for (int y = rect.top(); y <= rect.bottom(); ++y) {
      QRgb *row = reinterpret_cast<QRgb *>(image.scanLine(y));
      std::fill_n(row + rect.left(), rect.width(), color);
    }
  }
}

/// Keeps the HUD's native child window out of viewport pointer hit testing.
class QtViewportEdgeLengthLabel final : public QLabel {
public:
  explicit QtViewportEdgeLengthLabel(QWidget *parent) : QLabel(parent) {}

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    const int outline_radius =
        std::max(1, metrics.px(kDimensionOutlineRadiusPixels));
    const int outline_padding =
        std::max(1, metrics.px(kDimensionOutlinePaddingPixels));
    if (ui_scale_revision_ == metrics.revision() &&
        ui_scale_factor_ == metrics.factor() &&
        outline_radius_pixels_ == outline_radius &&
        outline_padding_pixels_ == outline_padding) {
      return;
    }
    ui_scale_revision_ = metrics.revision();
    ui_scale_factor_ = metrics.factor();
    outline_radius_pixels_ = outline_radius;
    outline_padding_pixels_ = outline_padding;
    invalidate_shape();
    updateGeometry();
  }

  [[nodiscard]] QSize sizeHint() const override {
    const QFont raster_font{font(), this};
    const QRect bounds = dimension_glyph_bounds(raster_font, text());
    return {
        std::max(1, bounds.width() + 2 * outline_padding_pixels_),
        std::max(1, bounds.height() + 2 * outline_padding_pixels_),
    };
  }

  void invalidate_shape() {
    if (!shape_valid_) {
      return;
    }
    shape_valid_ = false;
    update();
  }

  void refresh_native_mask_if_needed() {
    rebuild_layout_if_needed();
    if (mask_shape_revision_ == shape_revision_) {
      return;
    }
    setMask(outline_region_);
    setProperty("quaderDimensionMaskRevision",
                property("quaderDimensionMaskRevision").toInt() + 1);
    mask_shape_revision_ = shape_revision_;
  }

protected:
  void paintEvent(QPaintEvent *event) override {
    event->accept();
    rebuild_layout_if_needed();
    QPainter painter(this);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(QPoint{}, raster_);
  }

  bool nativeEvent(const QByteArray &event_type, void *message,
                   qintptr *result) override {
    if (result != nullptr &&
        event_type == QByteArrayLiteral("windows_generic_MSG")) {
      if (const std::optional<std::intptr_t> transparent_result =
              platform_transparent_overlay_input_result(message)) {
        *result = static_cast<qintptr>(*transparent_result);
        return true;
      }
    }
    return QLabel::nativeEvent(event_type, message, result);
  }

private:
  void rebuild_layout_if_needed() {
    const qreal raster_device_pixel_ratio =
        std::isfinite(devicePixelRatioF()) && devicePixelRatioF() > 0.0
            ? devicePixelRatioF()
            : 1.0;
    if (shape_valid_ && shape_size_ == size() && shape_text_ == text() &&
        shape_font_ == font() &&
        shape_device_pixel_ratio_ == raster_device_pixel_ratio) {
      return;
    }
    // Resolve point size against this label's screen once, then use the same
    // device-bound font for both metrics and the offscreen one-bit raster.
    // QBitmap otherwise resolves against the primary screen and can crop the
    // glyph after moving a viewport between mixed-DPI monitors.
    const QFont raster_font{font(), this};
    const QRect glyph_bounds = dimension_glyph_bounds(raster_font, text());
    glyph_baseline_ =
        dimension_glyph_baseline(glyph_bounds, outline_padding_pixels_);
    QBitmap glyph_mask(size());
    glyph_mask.fill(Qt::color0);
    {
      QPainter painter(&glyph_mask);
      painter.setRenderHint(QPainter::Antialiasing, false);
      painter.setRenderHint(QPainter::TextAntialiasing, false);
      painter.setFont(raster_font);
      painter.setPen(Qt::color1);
      painter.drawText(glyph_baseline_, text());
    }

    glyph_region_ = QRegion{glyph_mask};
    outline_region_ = glyph_region_;
    for (int y = -outline_radius_pixels_;
         y <= outline_radius_pixels_; ++y) {
      for (int x = -outline_radius_pixels_;
           x <= outline_radius_pixels_; ++x) {
        if (x != 0 || y != 0) {
          outline_region_ |= glyph_region_.translated(x, y);
        }
      }
    }
    outline_region_ &= QRegion{rect()};

    const QSize physical_size{
        std::max(1, static_cast<int>(
                        std::ceil(size().width() * raster_device_pixel_ratio))),
        std::max(1, static_cast<int>(std::ceil(
                        size().height() * raster_device_pixel_ratio))),
    };
    QBitmap physical_glyph_mask(physical_size);
    physical_glyph_mask.setDevicePixelRatio(raster_device_pixel_ratio);
    physical_glyph_mask.fill(Qt::color0);
    {
      QPainter painter(&physical_glyph_mask);
      painter.setRenderHint(QPainter::Antialiasing, false);
      painter.setRenderHint(QPainter::TextAntialiasing, false);
      painter.setFont(raster_font);
      painter.setPen(Qt::color1);
      painter.drawText(glyph_baseline_, text());
    }
    const QRegion physical_glyph_region{physical_glyph_mask};
    QRegion physical_outline_region = physical_glyph_region;
    const int physical_outline_radius = std::max(
        1, static_cast<int>(std::ceil(outline_radius_pixels_ *
                                      raster_device_pixel_ratio)));
    for (int y = -physical_outline_radius; y <= physical_outline_radius;
         ++y) {
      for (int x = -physical_outline_radius; x <= physical_outline_radius;
           ++x) {
        if (x != 0 || y != 0) {
          physical_outline_region |= physical_glyph_region.translated(x, y);
        }
      }
    }
    physical_outline_region &= QRegion{QRect(QPoint{}, physical_size)};

    raster_ = QImage(physical_size, QImage::Format_ARGB32);
    raster_.fill(Qt::transparent);
    fill_dimension_region(raster_, physical_outline_region,
                          qRgba(0, 0, 0, 255));
    fill_dimension_region(raster_, physical_glyph_region,
                          qRgba(255, 255, 255, 255));
    raster_.setDevicePixelRatio(raster_device_pixel_ratio);
    setProperty("quaderDimensionRasterPixelSize", physical_size);
    setProperty("quaderDimensionRasterDevicePixelRatio",
                raster_device_pixel_ratio);
    shape_size_ = size();
    shape_text_ = text();
    shape_font_ = font();
    shape_device_pixel_ratio_ = raster_device_pixel_ratio;
    shape_valid_ = true;
    ++shape_revision_;
  }

  QPoint glyph_baseline_;
  QImage raster_;
  QRegion glyph_region_;
  QRegion outline_region_;
  QSize shape_size_;
  QString shape_text_;
  QFont shape_font_;
  qreal shape_device_pixel_ratio_ = 0.0;
  std::uint64_t shape_revision_ = 0;
  std::uint64_t mask_shape_revision_ = 0;
  std::uint64_t ui_scale_revision_ = 0;
  float ui_scale_factor_ = 1.0F;
  int outline_radius_pixels_ = kDimensionOutlineRadiusPixels;
  int outline_padding_pixels_ = kDimensionOutlinePaddingPixels;
  bool shape_valid_ = false;
};

bool same_optional_point(const std::optional<QVec3> &left,
                         const std::optional<QVec3> &right) {
  if (!left.has_value() || !right.has_value()) {
    return left.has_value() == right.has_value();
  }
  return left->x == right->x && left->y == right->y && left->z == right->z;
}

template <typename Dimension>
bool same_dimension_geometry(const std::optional<Dimension> &current,
                             const std::optional<Dimension> &requested) {
  if (!current.has_value() || !requested.has_value()) {
    return current.has_value() == requested.has_value();
  }
  return current->start.x == requested->start.x &&
         current->start.y == requested->start.y &&
         current->start.z == requested->start.z &&
         current->end.x == requested->end.x &&
         current->end.y == requested->end.y &&
         current->end.z == requested->end.z &&
         same_optional_point(current->outside_reference,
                             requested->outside_reference);
}

QString dimension_length_text(QVec3 start, QVec3 end) {
  const double x = static_cast<double>(end.x) - start.x;
  const double y = static_cast<double>(end.y) - start.y;
  const double z = static_cast<double>(end.z) - start.z;
  const double map_length =
      std::hypot(x, y, z) * viewport_host_defaults().map_units_per_world_unit;
  if (!std::isfinite(map_length) || map_length <= 0.0) {
    return {};
  }
  return QString::number(map_length, 'f', 2);
}

QPoint logical_point(QVec2 render_point, QVec2 render_size,
                     const QRect &logical_bounds) {
  return {
      logical_bounds.x() +
          qRound(render_point.x * logical_bounds.width() / render_size.x),
      logical_bounds.y() +
          qRound(render_point.y * logical_bounds.height() / render_size.y),
  };
}

QRect centered_label_geometry(QPointF center, QSize size) {
  return {
      qRound(center.x()) - (size.width() - 1) / 2,
      qRound(center.y()) - (size.height() - 1) / 2,
      size.width(),
      size.height(),
  };
}

QRect clamped_label_geometry(QRect geometry, const QRect &bounds) {
  geometry.moveLeft(
      std::clamp(geometry.left(), bounds.left(), bounds.right() - geometry.width() + 1));
  geometry.moveTop(
      std::clamp(geometry.top(), bounds.top(), bounds.bottom() - geometry.height() + 1));
  return geometry;
}

} // namespace

QtViewportEdgeLengthHud::QtViewportEdgeLengthHud(
    QWidget &parent, ViewportPaneId pane_id)
    : QObject(&parent) {
  const std::string_view pane_name = viewport_pane_stable_id(pane_id);
  const QString pane = QString::fromUtf8(
      pane_name.data(), static_cast<qsizetype>(pane_name.size()));
  const std::array<QString, 3> object_names{
      QStringLiteral("viewportEdgeLength_%1").arg(pane),
      QStringLiteral("viewportFootprintDimension0_%1").arg(pane),
      QStringLiteral("viewportFootprintDimension1_%1").arg(pane),
  };
  for (std::size_t index = 0; index < labels_.size(); ++index) {
    QLabel *label = new QtViewportEdgeLengthLabel(&parent);
    labels_[index] = label;
    label->setObjectName(object_names[index]);
    label->setTextFormat(Qt::PlainText);
    label->setAlignment(Qt::AlignCenter);
    label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    label->setFrameStyle(QFrame::NoFrame);
    label->setMargin(0);
    label->setAutoFillBackground(false);
    label->setAttribute(Qt::WA_NoSystemBackground);
    label->setFont(dimension_font(kDefaultDimensionFontSizePoints));
    QPalette palette = label->palette();
    palette.setColor(QPalette::WindowText, QColor(255, 255, 255, 255));
    label->setPalette(palette);
    label->setForegroundRole(QPalette::WindowText);
    label->setFocusPolicy(Qt::NoFocus);
    label->setAttribute(Qt::WA_NativeWindow);
    // This Qt attribute handles widget routing; the label's native hit-test
    // override also passes input to the sibling viewport QWindow on Windows.
    label->setAttribute(Qt::WA_TransparentForMouseEvents);
    label->installEventFilter(this);
    label->hide();
  }
}

void QtViewportEdgeLengthHud::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (ui_scale_metrics_.revision() == metrics.revision() &&
      ui_scale_metrics_.factor() == metrics.factor()) {
    return;
  }
  ui_scale_metrics_ = metrics;
  const double authored_font_size =
      font_size_points_ > 0.0 ? font_size_points_
                              : kDefaultDimensionFontSizePoints;
  const QFont requested_font = dimension_font(
      authored_font_size * static_cast<double>(metrics.factor()));
  for (QLabel *label : labels_) {
    auto *dimension_label =
        static_cast<QtViewportEdgeLengthLabel *>(label);
    dimension_label->apply_ui_scale(metrics);
    if (label->font() != requested_font) {
      label->setFont(requested_font);
      dimension_label->invalidate_shape();
    }
  }
  font_size_valid_ = font_size_points_ > 0.0;
  view_valid_ = false;
}

void QtViewportEdgeLengthHud::present(
    const ViewportOverlayFrame &frame, std::uint64_t frame_signature,
    const ViewportCameraController &camera,
    const QRect &logical_viewport_bounds, bool edge_enabled,
    double font_size_points,
    const ModelingBoxPreview *footprint_preview, bool footprint_enabled) {
  const double effective_font_size =
      std::isfinite(font_size_points)
          ? std::clamp(font_size_points, kMinimumDimensionFontSizePoints,
                       kMaximumDimensionFontSizePoints)
          : kDefaultDimensionFontSizePoints;
  if (!font_size_valid_ || font_size_points_ != effective_font_size) {
    font_size_points_ = effective_font_size;
    for (QLabel *label : labels_) {
      const QFont requested_font = dimension_font(
          effective_font_size *
          static_cast<double>(ui_scale_metrics_.factor()));
      if (label->font() == requested_font) {
        continue;
      }
      label->setFont(requested_font);
      static_cast<QtViewportEdgeLengthLabel *>(label)->invalidate_shape();
    }
    // setFont() emits FontChange synchronously. Commit the cache only after
    // every retained label has received the configured size so an unrelated
    // later FontChange can invalidate it without causing internal churn.
    font_size_valid_ = true;
    view_valid_ = false;
  }
  std::array<std::optional<Dimension>, 3> requested_dimensions;
  if (edge_enabled) {
    requested_dimensions[0] = dimensions_[0];
  } else {
    frame_signature_valid_ = false;
  }

  // A stationary hover must not rescan overlays or reformat its number. Reuse
  // the caller's signature rather than hashing this stream a second time.
  if (edge_enabled &&
      (!frame_signature_valid_ || frame_signature_ != frame_signature)) {
    const auto found = std::find_if(
        frame.lines.begin(), frame.lines.end(),
        [](const ViewportOverlayLine &line) {
          return line.layer == ViewportOverlayLayer::HoverEdge &&
                 line.source == ViewportOverlaySource::ComponentHover;
        });
    const ViewportOverlayLine *edge =
        found != frame.lines.end() ? &*found : nullptr;
    std::optional<QVec3> inside_reference;
    if (edge != nullptr && frame.hover_edge_dimension_context.has_value()) {
      const ViewportHoverEdgeDimensionContext &context =
          *frame.hover_edge_dimension_context;
      const bool same_edge =
          (edge->element.edge_a == context.element.edge_a &&
           edge->element.edge_b == context.element.edge_b) ||
          (edge->element.edge_a == context.element.edge_b &&
           edge->element.edge_b == context.element.edge_a);
      if (edge->element.object_id == context.element.object_id && same_edge &&
          edge->element.face_id == context.element.face_id) {
        inside_reference = context.inside_reference;
      }
    }
    requested_dimensions[0] =
        edge != nullptr
            ? std::optional<Dimension>{Dimension{.start = edge->start,
                                                 .end = edge->end,
                                                 .outside_reference =
                                                     inside_reference}}
            : std::nullopt;
    frame_signature_ = frame_signature;
    frame_signature_valid_ = true;
  }

  if (footprint_enabled && footprint_preview != nullptr &&
      footprint_preview->drag_state == ModelingBoxDragState::Footprint) {
    if (footprint_preview->footprint_count == 2U) {
      requested_dimensions[1] = Dimension{
          .start = footprint_preview->footprint[0],
          .end = footprint_preview->footprint[1],
      };
    } else if (footprint_preview->footprint_count >= 4U) {
      QVec3 center;
      for (std::size_t index = 0; index < 4U; ++index) {
        center += footprint_preview->footprint[index];
      }
      center = center * 0.25F;
      // Use the two far sides so each number naturally sits outside the
      // footprint instead of competing at the press anchor.
      requested_dimensions[1] = Dimension{
          .start = footprint_preview->footprint[1],
          .end = footprint_preview->footprint[2],
          .outside_reference = center,
      };
      requested_dimensions[2] = Dimension{
          .start = footprint_preview->footprint[3],
          .end = footprint_preview->footprint[2],
          .outside_reference = center,
      };
    }
  }

  bool dimensions_changed = false;
  for (std::size_t index = 0; index < dimensions_.size(); ++index) {
    if (same_dimension_geometry(dimensions_[index],
                                requested_dimensions[index])) {
      continue;
    }
    dimensions_[index] = requested_dimensions[index];
    dimension_texts_[index] =
        dimensions_[index].has_value()
            ? dimension_length_text(dimensions_[index]->start,
                                    dimensions_[index]->end)
            : QString{};
    dimensions_changed = true;
  }
  if (dimensions_changed) {
    view_valid_ = false;
  }
  if (std::none_of(dimensions_.begin(), dimensions_.end(),
                   [](const std::optional<Dimension> &dimension) {
                     return dimension.has_value();
                   })) {
    hide();
    return;
  }

  const std::uint64_t camera_signature =
      RenderSignatureBuilder::overlay_camera_signature(
          ViewportCameraAdapter::overlay_camera(camera,
                                                 camera.viewport_size()));
  if (view_valid_ && camera_signature_ == camera_signature &&
      logical_viewport_bounds_ == logical_viewport_bounds) {
    return;
  }
  camera_signature_ = camera_signature;
  logical_viewport_bounds_ = logical_viewport_bounds;
  view_valid_ = true;

  const QVec2 render_size = camera.viewport_size();
  if (render_size.x <= 0.0f || render_size.y <= 0.0f ||
      logical_viewport_bounds.isEmpty()) {
    hide();
    return;
  }

  for (std::size_t index = 0; index < labels_.size(); ++index) {
    QLabel *label = labels_[index];
    const std::optional<Dimension> &dimension = dimensions_[index];
    if (!dimension.has_value() || dimension_texts_[index].isEmpty()) {
      if (!label->isHidden()) {
        label->hide();
      }
      continue;
    }
    const std::optional<ViewportScreenSegment> visible_segment =
        camera.project_visible_world_segment(dimension->start, dimension->end);
    if (!visible_segment.has_value()) {
      if (!label->isHidden()) {
        label->hide();
      }
      continue;
    }
    if (label->text() != dimension_texts_[index]) {
      label->setText(dimension_texts_[index]);
      static_cast<QtViewportEdgeLengthLabel *>(label)->invalidate_shape();
    }
    const QSize size = label->sizeHint();
    if (size.width() > logical_viewport_bounds.width() ||
        size.height() > logical_viewport_bounds.height()) {
      if (!label->isHidden()) {
        label->hide();
      }
      continue;
    }

    // Each native surface renders physical pixels, while child QWidget geometry
    // is logical. Independent axis ratios also cover rounded fractional DPI.
    const QPoint logical_start =
        logical_point(visible_segment->start, render_size,
                      logical_viewport_bounds);
    const QPoint logical_end = logical_point(visible_segment->end, render_size,
                                             logical_viewport_bounds);
    const QPointF midpoint{(logical_start.x() + logical_end.x()) * 0.5,
                           (logical_start.y() + logical_end.y()) * 0.5};
    const QPointF tangent{static_cast<qreal>(logical_end.x() - logical_start.x()),
                          static_cast<qreal>(logical_end.y() - logical_start.y())};
    const qreal tangent_length = std::hypot(tangent.x(), tangent.y());
    if (!std::isfinite(tangent_length) || tangent_length <= 0.001) {
      if (!label->isHidden()) {
        label->hide();
      }
      continue;
    }
    QPointF normal{-tangent.y() / tangent_length,
                   tangent.x() / tangent_length};
    bool chose_outside_reference = false;
    if (dimension->outside_reference.has_value()) {
      const ViewportScreenPoint projected_reference =
          camera.project_world_point(*dimension->outside_reference);
      if (projected_reference.visible &&
          std::isfinite(projected_reference.position.x) &&
          std::isfinite(projected_reference.position.y)) {
        const QPointF logical_reference{
            logical_viewport_bounds.x() +
                projected_reference.position.x *
                    logical_viewport_bounds.width() / render_size.x,
            logical_viewport_bounds.y() +
                projected_reference.position.y *
                    logical_viewport_bounds.height() / render_size.y,
        };
        const QPointF away{midpoint.x() - logical_reference.x(),
                           midpoint.y() - logical_reference.y()};
        if (normal.x() * away.x() + normal.y() * away.y() < 0.0) {
          normal = QPointF{-normal.x(), -normal.y()};
        }
        chose_outside_reference = true;
      }
    }
    if (!chose_outside_reference &&
        (normal.y() > 0.0 ||
         (std::abs(normal.y()) <= 0.0001 && normal.x() > 0.0))) {
      normal = QPointF{-normal.x(), -normal.y()};
    }

    const qreal label_support_radius =
        (std::abs(normal.x()) * static_cast<qreal>(size.width()) +
         std::abs(normal.y()) * static_cast<qreal>(size.height())) *
        0.5;
    const qreal offset =
        label_support_radius +
        ui_scale_metrics_.px(kDimensionLabelGapPixels);
    const QPointF preferred_center = midpoint + normal * offset;
    const QPointF alternate_center = midpoint - normal * offset;
    QRect geometry = centered_label_geometry(preferred_center, size);
    const QRect alternate = centered_label_geometry(alternate_center, size);
    if (!chose_outside_reference &&
        !logical_viewport_bounds.contains(geometry) &&
        logical_viewport_bounds.contains(alternate)) {
      geometry = alternate;
    } else if (!logical_viewport_bounds.contains(geometry)) {
      geometry = clamped_label_geometry(geometry, logical_viewport_bounds);
    }
    if (chose_outside_reference) {
      const QPointF final_offset = geometry.center() - midpoint;
      const qreal final_outside_distance =
          final_offset.x() * normal.x() + final_offset.y() * normal.y();
      if (final_outside_distance + 1.0 < offset) {
        if (!label->isHidden()) {
          label->hide();
        }
        continue;
      }
    }
    const bool resized = label->size() != geometry.size();
    if (resized) {
      label->resize(geometry.size());
      static_cast<QtViewportEdgeLengthLabel *>(label)->invalidate_shape();
    }
    if (label->pos() != geometry.topLeft()) {
      label->move(geometry.topLeft());
    }
    static_cast<QtViewportEdgeLengthLabel *>(label)
        ->refresh_native_mask_if_needed();
    if (label->isHidden()) {
      label->show();
      label->raise();
    }
  }
}

void QtViewportEdgeLengthHud::hide() {
  view_valid_ = false;
  for (QLabel *label : labels_) {
    if (!label->isHidden()) {
      label->hide();
    }
  }
}

void QtViewportEdgeLengthHud::raise() {
  for (QLabel *label : labels_) {
    if (!label->isHidden()) {
      label->raise();
    }
  }
}

bool QtViewportEdgeLengthHud::eventFilter(QObject *watched, QEvent *event) {
  const bool watches_dimension_label =
      std::find(labels_.begin(), labels_.end(), watched) != labels_.end();
  if (watches_dimension_label && event != nullptr &&
      (event->type() == QEvent::FontChange ||
       event->type() == QEvent::StyleChange ||
       event->type() == QEvent::DevicePixelRatioChange ||
       event->type() == QEvent::ScreenChangeInternal)) {
    view_valid_ = false;
    font_size_valid_ = false;
    static_cast<QtViewportEdgeLengthLabel *>(watched)->invalidate_shape();
  }
  return QObject::eventFilter(watched, event);
}

} // namespace quader
