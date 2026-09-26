////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_texture_info_window.hpp"

#include "ui/qt_contextual_input_hints.hpp"

#include <QAbstractItemView>
#include <QEvent>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QImage>
#include <QLocale>
#include <QMouseEvent>
#include <QPainter>
#include <QPalette>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSplitter>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace quader {
namespace {

constexpr int kDefaultWidth = 720;
constexpr int kDefaultHeight = 620;
constexpr int kPreviewMinimumWidth = 240;
constexpr int kPreviewMinimumHeight = 180;
constexpr int kTableMinimumHeight = 155;
constexpr int kCheckerExtent = 12;
constexpr double kMinimumTextureZoom = 0.1;
constexpr double kMaximumTextureZoom = 64.0;
constexpr std::uint8_t kAllChannels = 0x0Fu;

constexpr int kNameRow = 0;
constexpr int kLocationRow = 1;
constexpr int kFormatRow = 2;
constexpr int kSizeRow = 3;
constexpr int kFileSizeRow = 4;
constexpr int kPropertyColumn = 0;
constexpr int kValueColumn = 1;

QString utf8_string(std::string_view text) {
  if (text.empty()) {
    return {};
  }
  return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString normalized_format(std::string_view format) {
  return utf8_string(format).trimmed().toUpper();
}

QToolButton *make_channel_button(QWidget *parent, const QString &text,
                                 const QString &object_name) {
  auto *button = new QToolButton(parent);
  button->setObjectName(object_name);
  button->setText(text);
  button->setCheckable(true);
  button->setChecked(true);
  button->setAutoRaise(true);
  button->setToolButtonStyle(Qt::ToolButtonTextOnly);
  button->setFocusPolicy(Qt::StrongFocus);
  return button;
}

} // namespace

/// Paints a retained RGBA image over a checkerboard with fit, zoom, and pan.
class QtTextureInfoCanvas final : public QWidget {
public:
  explicit QtTextureInfoCanvas(QWidget *parent = nullptr) : QWidget(parent) {
    setObjectName(QStringLiteral("textureInfo.canvas"));
    setFocusPolicy(Qt::StrongFocus);
    // A trailing buttonless move must retire a pan whose release was lost.
    setMouseTracking(true);
    setMinimumSize(kPreviewMinimumWidth, kPreviewMinimumHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    qt_register_contextual_input_hints(
        *this, *this,
        [this](Qt::KeyboardModifiers) {
          return QtContextualInputHintSourceState{
              .revision = static_cast<std::uint64_t>(!image_.isNull()) |
                          (static_cast<std::uint64_t>(panning_) << 1u),
              .active = !image_.isNull() && panning_};
        },
        [this](Qt::KeyboardModifiers) {
          using Gesture = QuaderOperationInputGesture;
          std::vector<QuaderOperationInputHint> hints;
          if (image_.isNull()) {
            return hints;
          }
          hints.push_back({Gesture::PrimaryOrMiddleDrag, {},
                           "operation.hints.texture_info.pan", "Pan image",
                           90, panning_});
          if (!panning_) {
            hints.push_back({Gesture::Wheel, {},
                             "operation.hints.texture_info.zoom", "Zoom image", 85});
            hints.push_back({Gesture::PrimaryOrMiddleDoubleClick, {},
                             "operation.hints.texture_info.fit", "Fit image", 50});
          }
          return hints;
        });
    refresh_properties();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    setMinimumSize(metrics.size(
        QSize(kPreviewMinimumWidth, kPreviewMinimumHeight)));
    checker_extent_ = std::max(1, metrics.px(kCheckerExtent));
    refresh_properties();
    updateGeometry();
    update();
  }

  void set_image(QImage image, bool reset_view) {
    image_ = std::move(image);
    if (reset_view) {
      this->reset_view();
      return;
    }
    refresh_properties();
    update();
  }

  void reset_view() {
    zoom_ = 1.0;
    pan_ = {};
    panning_ = false;
    refresh_properties();
    update();
  }

  void set_channel_mask(std::uint8_t channel_mask,
                        std::uint64_t rebuild_count) {
    setProperty("channelMask", static_cast<unsigned int>(channel_mask));
    setProperty("maskedImageRebuildCount",
                static_cast<qulonglong>(rebuild_count));
  }

protected:
  bool event(QEvent *event) override {
    if (event->type() == QEvent::UngrabMouse ||
        event->type() == QEvent::FocusOut ||
        event->type() == QEvent::WindowDeactivate ||
        event->type() == QEvent::Hide) {
      panning_ = false;
    }
    return QWidget::event(event);
  }

  void paintEvent(QPaintEvent *) override {
    QPainter painter(this);
    painter.fillRect(rect(), palette().brush(backgroundRole()));
    if (image_.isNull()) {
      return;
    }

    const double scale = fit_scale() * zoom_;
    if (!(scale > 0.0)) {
      return;
    }
    const QSizeF drawn_size(static_cast<double>(image_.width()) * scale,
                            static_cast<double>(image_.height()) * scale);
    const QRectF image_rect(QPointF(rect().center()) + pan_ -
                                QPointF(drawn_size.width() * 0.5,
                                        drawn_size.height() * 0.5),
                            drawn_size);
    draw_checkerboard(painter, image_rect);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(image_rect, image_);
  }

  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    refresh_properties();
  }

  void mousePressEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton ||
        event->button() == Qt::MiddleButton) {
      panning_ = true;
      previous_pointer_position_ = event->position();
      event->accept();
      return;
    }
    QWidget::mousePressEvent(event);
  }

  void mouseMoveEvent(QMouseEvent *event) override {
    if (!panning_ ||
        (!event->buttons().testFlag(Qt::LeftButton) &&
         !event->buttons().testFlag(Qt::MiddleButton))) {
      panning_ = false;
      QWidget::mouseMoveEvent(event);
      return;
    }
    pan_ += event->position() - previous_pointer_position_;
    previous_pointer_position_ = event->position();
    refresh_properties();
    update();
    event->accept();
  }

  void mouseReleaseEvent(QMouseEvent *event) override {
    if ((event->button() == Qt::LeftButton ||
         event->button() == Qt::MiddleButton) &&
        panning_) {
      panning_ = false;
      event->accept();
      return;
    }
    QWidget::mouseReleaseEvent(event);
  }

  void mouseDoubleClickEvent(QMouseEvent *event) override {
    if (event->button() == Qt::LeftButton ||
        event->button() == Qt::MiddleButton) {
      reset_view();
      event->accept();
      return;
    }
    QWidget::mouseDoubleClickEvent(event);
  }

  void wheelEvent(QWheelEvent *event) override {
    const double zoom_factor = std::pow(1.0015, event->angleDelta().y());
    zoom_ = std::clamp(zoom_ * zoom_factor, kMinimumTextureZoom,
                       kMaximumTextureZoom);
    refresh_properties();
    update();
    event->accept();
  }

private:
  [[nodiscard]] double fit_scale() const noexcept {
    if (image_.isNull() || width() <= 0 || height() <= 0) {
      return 1.0;
    }
    return std::min(static_cast<double>(width()) / image_.width(),
                    static_cast<double>(height()) / image_.height());
  }

  void draw_checkerboard(QPainter &painter, const QRectF &image_rect) const {
    painter.save();
    painter.setClipRect(image_rect);
    const QColor first = palette().color(QPalette::Base);
    const QColor second = palette().color(QPalette::Mid);
    const int left = static_cast<int>(std::floor(image_rect.left()));
    const int top = static_cast<int>(std::floor(image_rect.top()));
    const int right = static_cast<int>(std::ceil(image_rect.right()));
    const int bottom = static_cast<int>(std::ceil(image_rect.bottom()));
    painter.fillRect(image_rect, first);
    for (int y = top; y < bottom; y += checker_extent_) {
      const int row = (y - top) / checker_extent_;
      for (int x = left; x < right; x += checker_extent_) {
        const int column = (x - left) / checker_extent_;
        if ((row + column) % 2 != 0) {
          painter.fillRect(QRect(x, y, checker_extent_, checker_extent_),
                           second);
        }
      }
    }
    painter.restore();
  }

  void refresh_properties() {
    setProperty("zoom", zoom_);
    setProperty("fitScale", fit_scale());
    setProperty("panX", pan_.x());
    setProperty("panY", pan_.y());
  }

  QImage image_;
  QPointF pan_;
  QPointF previous_pointer_position_;
  double zoom_ = 1.0;
  int checker_extent_ = kCheckerExtent;
  bool panning_ = false;
};

class QtTextureInfoWindow::ImageStorage final {
public:
  QImage source;
  QImage masked;
};

QtTextureInfoWindow::QtTextureInfoWindow(QWidget *parent,
                                         const QtUiScaleMetrics &metrics)
    : QMainWindow(parent, Qt::Tool | Qt::CustomizeWindowHint |
                              Qt::WindowTitleHint | Qt::WindowSystemMenuHint |
                              Qt::WindowCloseButtonHint),
      ui_scale_metrics_(metrics), images_(std::make_unique<ImageStorage>()) {
  setObjectName(QStringLiteral("textureInfo.window"));
  qt_register_contextual_input_hints(
      *this, *this,
      [](Qt::KeyboardModifiers) { return QtContextualInputHintSourceState{}; },
      [](Qt::KeyboardModifiers) {
        return std::vector<QuaderOperationInputHint>{};
      });
  setAttribute(Qt::WA_DeleteOnClose, false);
  resize(metrics.size(QSize(kDefaultWidth, kDefaultHeight)));

  auto *central = new QWidget(this);
  central->setObjectName(QStringLiteral("textureInfo.content"));
  auto *layout = new QVBoxLayout(central);

  auto *channels = new QHBoxLayout();
  channels->setObjectName(QStringLiteral("textureInfo.channels"));
  channels->addStretch(1);
  red_button_ = make_channel_button(
      central, QStringLiteral("R"), QStringLiteral("textureInfo.channel.red"));
  green_button_ = make_channel_button(
      central, QStringLiteral("G"), QStringLiteral("textureInfo.channel.green"));
  blue_button_ = make_channel_button(
      central, QStringLiteral("B"), QStringLiteral("textureInfo.channel.blue"));
  alpha_button_ = make_channel_button(
      central, QStringLiteral("A"), QStringLiteral("textureInfo.channel.alpha"));
  channels->addWidget(red_button_);
  channels->addWidget(green_button_);
  channels->addWidget(blue_button_);
  channels->addWidget(alpha_button_);
  channels->addStretch(1);
  layout->addLayout(channels);

  splitter_ = new QSplitter(Qt::Vertical, central);
  splitter_->setObjectName(QStringLiteral("textureInfo.splitter"));
  splitter_->setChildrenCollapsible(false);
  canvas_ = new QtTextureInfoCanvas(splitter_);
  table_ = new QTableWidget(5, 2, splitter_);
  table_->setObjectName(QStringLiteral("textureInfo.table"));
  table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  table_->setSelectionMode(QAbstractItemView::NoSelection);
  table_->setFocusPolicy(Qt::NoFocus);
  table_->verticalHeader()->hide();
  table_->horizontalHeader()->setSectionsMovable(false);
  table_->horizontalHeader()->setSectionsClickable(false);
  table_->horizontalHeader()->setStretchLastSection(false);
  table_->horizontalHeader()->setSectionResizeMode(kPropertyColumn,
                                                   QHeaderView::Interactive);
  table_->horizontalHeader()->setSectionResizeMode(kValueColumn,
                                                   QHeaderView::Stretch);
  table_->horizontalHeader()->resizeSection(kPropertyColumn, metrics.px(140));
  table_->setMinimumHeight(metrics.px(kTableMinimumHeight));

  for (int row = 0; row < table_->rowCount(); ++row) {
    auto *property_item = new QTableWidgetItem();
    property_item->setFlags(Qt::ItemIsEnabled);
    table_->setItem(row, kPropertyColumn, property_item);
    auto *value_item = new QTableWidgetItem();
    value_item->setFlags(Qt::ItemIsEnabled);
    table_->setItem(row, kValueColumn, value_item);
  }

  splitter_->addWidget(canvas_);
  splitter_->addWidget(table_);
  splitter_->setStretchFactor(0, 3);
  splitter_->setStretchFactor(1, 1);
  layout->addWidget(splitter_, 1);
  setCentralWidget(central);

  QObject::connect(red_button_, &QToolButton::toggled, this,
                   [this](bool enabled) {
                     set_channel_enabled(Red, enabled);
                   });
  QObject::connect(green_button_, &QToolButton::toggled, this,
                   [this](bool enabled) {
                     set_channel_enabled(Green, enabled);
                   });
  QObject::connect(blue_button_, &QToolButton::toggled, this,
                   [this](bool enabled) {
                     set_channel_enabled(Blue, enabled);
                   });
  QObject::connect(alpha_button_, &QToolButton::toggled, this,
                   [this](bool enabled) {
                     set_channel_enabled(Alpha, enabled);
                   });

  refresh_labels();
  refresh_channel_properties();
  canvas_->apply_ui_scale(metrics);
}

QtTextureInfoWindow::~QtTextureInfoWindow() = default;

bool QtTextureInfoWindow::present(
    const TextureInfoPresentation &presentation) {
  if (!valid_pixels(presentation)) {
    clear();
    return false;
  }

  const SourceKey next_source_key = source_key(presentation);
  const bool changed_source =
      !has_source_key_ || !(source_key_ == next_source_key);
  if (changed_source) {
    const std::uint64_t row_bytes =
        static_cast<std::uint64_t>(presentation.width) * 4u;
    const QImage borrowed(
        reinterpret_cast<const uchar *>(presentation.rgba8.data()),
        static_cast<int>(presentation.width),
        static_cast<int>(presentation.height), static_cast<int>(row_bytes),
        QImage::Format_RGBA8888);
    QImage owned = borrowed.copy();
    if (owned.isNull()) {
      clear();
      return false;
    }
    images_->source = std::move(owned);
    source_key_ = next_source_key;
    has_source_key_ = true;
  }

  current_context_ = presentation.context;
  has_presentation_ = true;
  refresh_table(presentation);
  if (changed_source) {
    rebuild_masked_image(true);
  }
  return true;
}

void QtTextureInfoWindow::clear() {
  has_presentation_ = false;
  has_source_key_ = false;
  current_context_ = {};
  source_key_ = {};
  images_->source = {};
  images_->masked = {};
  masked_channel_mask_ = 0u;
  channel_mask_ = kAllChannels;
  {
    const std::array<QToolButton *, 4> buttons{
        red_button_, green_button_, blue_button_, alpha_button_};
    for (QToolButton *button : buttons) {
      const QSignalBlocker blocker(button);
      button->setChecked(true);
    }
  }
  for (int row = 0; row < table_->rowCount(); ++row) {
    table_->item(row, kValueColumn)->setText({});
    table_->item(row, kValueColumn)->setToolTip({});
  }
  canvas_->set_image({}, true);
  refresh_channel_properties();
}

void QtTextureInfoWindow::set_labels(QtTextureInfoLabels labels) {
  labels_ = std::move(labels);
  refresh_labels();
}

void QtTextureInfoWindow::apply_ui_scale(const QtUiScaleMetrics &metrics) {
  if (ui_scale_metrics_.revision() == metrics.revision() &&
      ui_scale_metrics_.factor() == metrics.factor()) {
    return;
  }
  ui_scale_metrics_ = metrics;
  canvas_->apply_ui_scale(metrics);
  table_->setMinimumHeight(metrics.px(kTableMinimumHeight));
  table_->horizontalHeader()->resizeSection(kPropertyColumn, metrics.px(140));
  centralWidget()->updateGeometry();
}

const TextureInfoContext *
QtTextureInfoWindow::current_context() const noexcept {
  return has_presentation_ ? &current_context_ : nullptr;
}

QString QtTextureInfoWindow::format_storage_size(
    std::uintmax_t encoded_byte_size) {
  constexpr std::uintmax_t kMegabyte = 1'000'000u;
  constexpr std::uintmax_t kGigabyte = 1'000'000'000u;
  double divisor = 1'000.0;
  QString unit = QStringLiteral("KB");
  if (encoded_byte_size >= kGigabyte) {
    divisor = 1'000'000'000.0;
    unit = QStringLiteral("GB");
  } else if (encoded_byte_size >= kMegabyte) {
    divisor = 1'000'000.0;
    unit = QStringLiteral("MB");
  }

  const QLocale locale;
  QString value =
      locale.toString(static_cast<double>(encoded_byte_size) / divisor, 'f', 1);
  const QString decimal_zero = locale.decimalPoint() + QStringLiteral("0");
  if (value.endsWith(decimal_zero)) {
    value.chop(decimal_zero.size());
  }
  return value + QStringLiteral(" ") + unit;
}

QtTextureInfoWindow::SourceKey QtTextureInfoWindow::source_key(
    const TextureInfoPresentation &presentation) {
  const bool has_stable_texture_identity =
      !presentation.context.texture_identity.empty();
  return SourceKey{
      .project_identity = presentation.context.project_identity,
      .activation_generation = presentation.context.activation_generation,
      .texture_identity = presentation.context.texture_identity,
      .fallback_material_identity =
          has_stable_texture_identity
              ? std::string{}
              : presentation.context.material_identity,
      .fallback_location =
          has_stable_texture_identity ? std::string{} : presentation.location,
      .width = presentation.width,
      .height = presentation.height,
      .content_revision = presentation.content_revision,
  };
}

bool QtTextureInfoWindow::valid_pixels(
    const TextureInfoPresentation &presentation) noexcept {
  if (presentation.width == 0u || presentation.height == 0u ||
      presentation.rgba8.empty()) {
    return false;
  }
  const std::uint64_t row_bytes =
      static_cast<std::uint64_t>(presentation.width) * 4u;
  const std::uint64_t expected_bytes =
      row_bytes * static_cast<std::uint64_t>(presentation.height);
  return row_bytes <=
             static_cast<std::uint64_t>(std::numeric_limits<int>::max()) &&
         presentation.height <=
             static_cast<std::uint32_t>(std::numeric_limits<int>::max()) &&
         expected_bytes <= std::numeric_limits<std::size_t>::max() &&
         presentation.rgba8.size() ==
             static_cast<std::size_t>(expected_bytes);
}

void QtTextureInfoWindow::set_channel_enabled(Channel channel, bool enabled) {
  const std::uint8_t channel_bit = static_cast<std::uint8_t>(channel);
  const std::uint8_t next_mask =
      enabled ? static_cast<std::uint8_t>(channel_mask_ | channel_bit)
              : static_cast<std::uint8_t>(channel_mask_ & ~channel_bit);
  if (channel_mask_ == next_mask) {
    return;
  }
  channel_mask_ = next_mask;
  refresh_channel_properties();
  if (has_presentation_) {
    rebuild_masked_image(false);
  }
}

void QtTextureInfoWindow::rebuild_masked_image(bool reset_view) {
  if (!has_presentation_ || images_->source.isNull()) {
    return;
  }
  if (!reset_view && masked_channel_mask_ == channel_mask_) {
    return;
  }

  images_->masked = images_->source;
  if (channel_mask_ != kAllChannels) {
    images_->masked.detach();
    for (int row = 0; row < images_->masked.height(); ++row) {
      uchar *pixels = images_->masked.scanLine(row);
      for (int column = 0; column < images_->masked.width(); ++column) {
        uchar *pixel = pixels + column * 4;
        if ((channel_mask_ & Red) == 0u) {
          pixel[0] = 0u;
        }
        if ((channel_mask_ & Green) == 0u) {
          pixel[1] = 0u;
        }
        if ((channel_mask_ & Blue) == 0u) {
          pixel[2] = 0u;
        }
        if ((channel_mask_ & Alpha) == 0u) {
          pixel[3] = 255u;
        }
      }
    }
  }
  masked_channel_mask_ = channel_mask_;
  ++masked_image_rebuild_count_;
  canvas_->set_channel_mask(channel_mask_, masked_image_rebuild_count_);
  canvas_->set_image(images_->masked, reset_view);
}

void QtTextureInfoWindow::refresh_labels() {
  setWindowTitle(labels_.window_title);
  table_->setHorizontalHeaderLabels({labels_.property, labels_.value});
  table_->item(kNameRow, kPropertyColumn)->setText(labels_.name);
  table_->item(kLocationRow, kPropertyColumn)->setText(labels_.location);
  table_->item(kFormatRow, kPropertyColumn)->setText(labels_.format);
  table_->item(kSizeRow, kPropertyColumn)->setText(labels_.size);
  table_->item(kFileSizeRow, kPropertyColumn)->setText(labels_.file_size);
  red_button_->setToolTip(labels_.red_channel);
  red_button_->setAccessibleName(labels_.red_channel);
  green_button_->setToolTip(labels_.green_channel);
  green_button_->setAccessibleName(labels_.green_channel);
  blue_button_->setToolTip(labels_.blue_channel);
  blue_button_->setAccessibleName(labels_.blue_channel);
  alpha_button_->setToolTip(labels_.alpha_channel);
  alpha_button_->setAccessibleName(labels_.alpha_channel);
}

void QtTextureInfoWindow::refresh_table(
    const TextureInfoPresentation &presentation) {
  const QString name = utf8_string(presentation.name);
  const QString location = utf8_string(presentation.location);
  table_->item(kNameRow, kValueColumn)->setText(name);
  table_->item(kNameRow, kValueColumn)->setToolTip(name);
  table_->item(kLocationRow, kValueColumn)->setText(location);
  table_->item(kLocationRow, kValueColumn)->setToolTip(location);
  table_->item(kFormatRow, kValueColumn)
      ->setText(normalized_format(presentation.format));

  const QString size = QStringLiteral("%1x%2")
                           .arg(presentation.width)
                           .arg(presentation.height);
  table_->item(kSizeRow, kValueColumn)->setText(size);
  table_->item(kSizeRow, kValueColumn)->setToolTip(size);

  const QString file_size =
      format_storage_size(presentation.encoded_byte_size);
  table_->item(kFileSizeRow, kValueColumn)->setText(file_size);
  table_->item(kFileSizeRow, kValueColumn)->setToolTip(file_size);
}

void QtTextureInfoWindow::refresh_channel_properties() {
  setProperty("channelMask", static_cast<unsigned int>(channel_mask_));
  if (canvas_ != nullptr) {
    canvas_->set_channel_mask(channel_mask_, masked_image_rebuild_count_);
  }
}

} // namespace quader
