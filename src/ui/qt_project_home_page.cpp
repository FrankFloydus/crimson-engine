////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_project_home_page.hpp"
#include "ui/qt_application_style.hpp"
#include "ui/qt_multiresolution_icon.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QCursor>
#include <QDateTime>
#include <QEnterEvent>
#include <QEvent>
#include <QFrame>
#include <QFont>
#include <QFontInfo>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QLinearGradient>
#include <QLocale>
#include <QMenu>
#include <QModelIndex>
#include <QMouseEvent>
#include <QPalette>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QRegion>
#include <QScopedValueRollback>
#include <QScrollArea>
#include <QScrollBar>
#include <QShowEvent>
#include <QSizePolicy>
#include <QSpacerItem>
#include <QStackedWidget>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QTableView>
#include <QTimer>
#include <QVariant>
#include <QVBoxLayout>

#include <algorithm>
#include <limits>
#include <utility>

// Keep the resource entry point outside namespaces so static-library linking
// retains the shared Home artwork for both executable profiles and tests.
static void initialize_project_home_artwork() {
  static const bool kInitialized = []() {
    Q_INIT_RESOURCE(quader_project_home_artwork);
    return true;
  }();
  Q_UNUSED(kInitialized);
}

namespace quader {
namespace {

/** Frames and clips the retained table and empty state as one native surface. */
class RecentProjectsFrame final : public QFrame {
public:
  explicit RecentProjectsFrame(QWidget *parent) : QFrame(parent) {
    setObjectName(QStringLiteral("projectHome.recentProjects.frame"));
    frame_layout_ = new QVBoxLayout(this);
    frame_layout_->setSpacing(0);
    content_ = new QStackedWidget(this);
    content_->setFrameShape(QFrame::NoFrame);
    content_->installEventFilter(this);
    frame_layout_->addWidget(content_);
    refresh_frame();
  }

  [[nodiscard]] QStackedWidget *content() const { return content_; }

  void set_metrics(const QtUiScaleMetrics &metrics) {
    metrics_ = metrics;
    refresh_frame();
  }

protected:
  bool event(QEvent *event) override {
    const bool handled = QFrame::event(event);
    if (event->type() == QEvent::StyleChange ||
        event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::Show) {
      refresh_frame();
    }
    return handled;
  }

  bool eventFilter(QObject *watched, QEvent *event) override {
    if (watched == content_ && event->type() == QEvent::Resize) {
      refresh_frame();
    }
    return QFrame::eventFilter(watched, event);
  }

  void paintEvent(QPaintEvent *event) override {
    QStyleOption option;
    option.initFrom(this);
    if (!qt_application_style_active(&option)) {
      {
        QPainter painter(this);
        painter.fillRect(rect(), option.palette.brush(QPalette::Base));
      }
      QFrame::paintEvent(event);
      return;
    }
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal hairline = 1.0 / painter.device()->devicePixelRatioF();
    const QRectF bounds = QRectF(rect()).adjusted(
        hairline * 0.5, hairline * 0.5, -hairline * 0.5, -hairline * 0.5);
    QPen border(option.palette.color(QPalette::Midlight));
    border.setWidthF(hairline);
    painter.setPen(border);
    painter.setBrush(option.palette.brush(QPalette::Base));
    painter.drawRoundedRect(bounds, metrics_.px(6.0), metrics_.px(6.0));
  }

private:
  void refresh_frame() {
    if (content_ == nullptr || refreshing_) {
      return;
    }
    const QScopedValueRollback<bool> guard(refreshing_, true);
    QStyleOption option;
    option.initFrom(this);
    const bool rounded = qt_application_style_active(&option);
    setFrameShape(rounded ? QFrame::NoFrame : QFrame::StyledPanel);
    const int inset = rounded ? 1 : 0;
    const QMargins margins(inset, inset, inset, inset);
    if (frame_layout_->contentsMargins() != margins) {
      frame_layout_->setContentsMargins(margins);
    }
    const QSize content_size = content_->size();
    const qreal device_pixel_ratio = devicePixelRatioF();
    if (clipped_size_ != content_size || clipped_scale_ != metrics_.factor() ||
        clipped_device_pixel_ratio_ != device_pixel_ratio ||
        clipped_rounded_ != rounded) {
      clipped_size_ = content_size;
      clipped_scale_ = metrics_.factor();
      clipped_device_pixel_ratio_ = device_pixel_ratio;
      clipped_rounded_ = rounded;
      if (rounded && !content_size.isEmpty()) {
        QPainterPath outline;
        const qreal inner_radius = std::max(0.0, metrics_.px(6.0) - inset);
        outline.addRoundedRect(QRectF(QPointF{}, QSizeF(content_size)),
                               inner_radius, inner_radius);
        content_->setMask(QRegion(outline.toFillPolygon().toPolygon()));
      } else {
        content_->clearMask();
      }
    }
    update();
  }

  QVBoxLayout *frame_layout_ = nullptr;
  QStackedWidget *content_ = nullptr;
  QtUiScaleMetrics metrics_;
  QSize clipped_size_;
  float clipped_scale_ = 0.0F;
  qreal clipped_device_pixel_ratio_ = 0.0;
  bool clipped_rounded_ = false;
  bool refreshing_ = false;
};

/** Neutral launcher chrome drawn through the shared multiresolution icon owner. */
enum class ProjectHomeGlyph { Folder, Plus, Arrow, External };

Qt::Alignment project_home_leading_alignment(Qt::LayoutDirection direction,
                                             Qt::Alignment vertical) {
  return (direction == Qt::RightToLeft ? Qt::AlignRight : Qt::AlignLeft) |
         Qt::AlignAbsolute | vertical;
}

QFont project_home_heading_font(const QWidget &widget, qreal minimum_point_size,
                                 qreal emphasis_points) {
  QFont font = widget.font();
  const auto *scale_style =
      dynamic_cast<const QtUiScaleProxyStyle *>(QApplication::style());
  const qreal inherited_scale =
      scale_style != nullptr ? scale_style->metrics().factor() : 1.0;
  const qreal inherited_point_size = font.pointSizeF() > 0.0
                                         ? font.pointSizeF()
                                         : QFontInfo(font).pointSizeF();
  // Startup installs the saved scale before constructing Home. Store a 100%
  // font baseline so opening at 200% matches scaling an existing page to 200%.
  font.setPointSizeF(std::max(minimum_point_size,
                             inherited_point_size / inherited_scale + emphasis_points));
  font.setWeight(QFont::DemiBold);
  return font;
}

QIcon project_home_icon(ProjectHomeGlyph glyph, const QPalette &palette,
                        QPalette::ColorRole role, Qt::LayoutDirection direction) {
  QImage source(96, 96, QImage::Format_ARGB32_Premultiplied);
  source.fill(Qt::transparent);
  QPainter painter(&source);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.scale(4.0, 4.0);
  if (glyph == ProjectHomeGlyph::Arrow && direction == Qt::RightToLeft) {
    painter.translate(24.0, 0.0);
    painter.scale(-1.0, 1.0);
  }
  QPen pen(palette.color(role), 1.6);
  pen.setCapStyle(Qt::RoundCap);
  pen.setJoinStyle(Qt::RoundJoin);
  painter.setPen(pen);
  painter.setBrush(Qt::NoBrush);
  QPainterPath path;
  switch (glyph) {
  case ProjectHomeGlyph::Folder:
    path.moveTo(3.0, 7.0);
    path.lineTo(3.0, 5.0);
    path.lineTo(9.0, 5.0);
    path.lineTo(12.0, 8.0);
    path.lineTo(21.0, 8.0);
    path.lineTo(21.0, 20.0);
    path.lineTo(3.0, 20.0);
    path.closeSubpath();
    path.moveTo(3.0, 9.0);
    path.lineTo(21.0, 9.0);
    break;
  case ProjectHomeGlyph::Plus:
    path.moveTo(4.0, 12.0);
    path.lineTo(20.0, 12.0);
    path.moveTo(12.0, 4.0);
    path.lineTo(12.0, 20.0);
    break;
  case ProjectHomeGlyph::Arrow:
    path.moveTo(4.0, 12.0);
    path.lineTo(20.0, 12.0);
    path.moveTo(14.0, 6.0);
    path.lineTo(20.0, 12.0);
    path.lineTo(14.0, 18.0);
    break;
  case ProjectHomeGlyph::External:
    path.moveTo(11.0, 5.0);
    path.lineTo(4.0, 5.0);
    path.lineTo(4.0, 20.0);
    path.lineTo(19.0, 20.0);
    path.lineTo(19.0, 13.0);
    path.moveTo(14.0, 3.0);
    path.lineTo(21.0, 3.0);
    path.lineTo(21.0, 10.0);
    path.moveTo(21.0, 3.0);
    path.lineTo(11.0, 13.0);
    break;
  }
  painter.drawPath(path);
  painter.end();
  return qt_multiresolution_icon(source);
}

/** Keeps native header labels aligned with the inset project-row content. */
class RecentProjectsHeader final : public QHeaderView {
public:
  explicit RecentProjectsHeader(QWidget *parent)
      : QHeaderView(Qt::Horizontal, parent) {}

  void set_metrics(const QtUiScaleMetrics &metrics) { metrics_ = metrics; }

protected:
  void paintSection(QPainter *painter, const QRect &rect,
                     int logical_index) const override {
    QStyleOptionHeader option;
    initStyleOptionForIndex(&option, logical_index);
    option.rect = rect;
    if (!qt_application_style_active(&option)) {
      QHeaderView::paintSection(painter, rect, logical_index);
      return;
    }
    painter->save();
    painter->setClipRect(rect);
    style()->drawControl(QStyle::CE_HeaderSection, &option, painter, this);
    const int inset = metrics_.px(16);
    option.rect.adjust(inset, 0, -inset, 0);
    option.textAlignment = project_home_leading_alignment(
        layoutDirection(), Qt::AlignVCenter);
    option.palette.setColor(QPalette::ButtonText,
                             option.palette.color(QPalette::WindowText));
    style()->drawControl(QStyle::CE_HeaderLabel, &option, painter, this);
    painter->restore();
  }

private:
  QtUiScaleMetrics metrics_;
};

/** Presents two-line project names without replacing native row interaction. */
class RecentProjectDelegate final : public QStyledItemDelegate {
public:
  explicit RecentProjectDelegate(QObject *parent)
      : QStyledItemDelegate(parent) {}

  void set_metrics(const QtUiScaleMetrics &metrics) { metrics_ = metrics; }

  void paint(QPainter *painter, const QStyleOptionViewItem &option,
             const QModelIndex &index) const override {
    QStyleOptionViewItem item(option);
    initStyleOption(&item, index);
    // Qt normalizes newlines in the style option for ordinary item labels;
    // retain the model's two-line contract before splitting name and path.
    const QString text = index.data(Qt::DisplayRole).toString();
    item.text.clear();
    const QStyle *style = item.widget != nullptr ? item.widget->style()
                                                : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &item, painter, item.widget);

    const bool selected = item.state.testFlag(QStyle::State_Selected);
    const bool available = index.data(QtRecentProjectsModel::AvailableRole).toBool();
    const QPalette::ColorGroup group =
        !item.state.testFlag(QStyle::State_Enabled)
            ? QPalette::Disabled
            : item.state.testFlag(QStyle::State_Active) ? QPalette::Active
                                                       : QPalette::Inactive;
    const bool shared_style = qt_application_style_active(&item);
    const QColor primary = item.palette.color(
        group, selected ? QPalette::HighlightedText : QPalette::Text);
    const QColor secondary = item.palette.color(
        group, selected ? QPalette::HighlightedText
                        : shared_style ? QPalette::WindowText : QPalette::Text);
    const QColor foreground = available || selected
                                  ? primary
                                  : item.palette.color(QPalette::Disabled,
                                                       QPalette::Text);
    const int inset = metrics_.px(16);
    const QRect logical_content = item.rect.adjusted(inset, 0, -inset, 0);
    painter->save();
    painter->setClipRect(item.rect);
    painter->setFont(item.font);
    painter->setPen(secondary);
    if (index.column() == QtRecentProjectsModel::NameColumn) {
      const int icon_width = metrics_.px(18);
      const int icon_height = metrics_.px(22);
      const QRect logical_icon(logical_content.left(),
                               item.rect.center().y() - icon_height / 2,
                               icon_width, icon_height);
      const QRect icon_rect = QStyle::visualRect(item.direction, item.rect,
                                               logical_icon);
      painter->setRenderHint(QPainter::Antialiasing);
      QPen icon_pen(secondary);
      icon_pen.setWidthF(metrics_.px(1.25));
      icon_pen.setJoinStyle(Qt::RoundJoin);
      painter->setPen(icon_pen);
      const qreal fold = metrics_.px(6.0);
      const QRectF icon_bounds = QRectF(icon_rect).adjusted(1.0, 1.0, -1.0, -1.0);
      QPainterPath file;
      file.moveTo(icon_bounds.left(), icon_bounds.top());
      file.lineTo(icon_bounds.right() - fold, icon_bounds.top());
      file.lineTo(icon_bounds.right(), icon_bounds.top() + fold);
      file.lineTo(icon_bounds.right(), icon_bounds.bottom());
      file.lineTo(icon_bounds.left(), icon_bounds.bottom());
      file.closeSubpath();
      file.moveTo(icon_bounds.right() - fold, icon_bounds.top());
      file.lineTo(icon_bounds.right() - fold, icon_bounds.top() + fold);
      file.lineTo(icon_bounds.right(), icon_bounds.top() + fold);
      painter->drawPath(file);

      QRect text_rect = logical_content.adjusted(icon_width + metrics_.px(16),
                                                0, 0, 0);
      text_rect = QStyle::visualRect(item.direction, item.rect, text_rect);
      const qsizetype newline = text.indexOf(QLatin1Char('\n'));
      const QString name = newline < 0 ? text : text.left(newline);
      const QString path = newline < 0 ? QString{} : text.mid(newline + 1);
      QFont name_font(item.font);
      name_font.setWeight(QFont::DemiBold);
      const QFontMetrics name_metrics(name_font);
      const QFontMetrics path_metrics(item.font);
      const int gap = metrics_.px(3);
      const int text_height = name_metrics.height() + path_metrics.height() + gap;
      const int top = item.rect.center().y() - text_height / 2;
      const Qt::Alignment alignment =
          QStyle::visualAlignment(item.direction, Qt::AlignLeft) | Qt::AlignVCenter;
      painter->setFont(name_font);
      painter->setPen(foreground);
      painter->drawText(QRect(text_rect.left(), top, text_rect.width(),
                              name_metrics.height()),
                        alignment, name_metrics.elidedText(
                                       name, Qt::ElideRight, text_rect.width()));
      painter->setFont(item.font);
      painter->setPen(secondary);
      painter->drawText(QRect(text_rect.left(), top + name_metrics.height() + gap,
                              text_rect.width(), path_metrics.height()),
                        alignment, path_metrics.elidedText(
                                       path, Qt::ElideMiddle, text_rect.width()));
      if (selected && shared_style) {
        const QRect stripe = QStyle::visualRect(
            item.direction, item.rect,
            QRect(item.rect.left(), item.rect.top(), metrics_.px(3),
                  item.rect.height()));
        painter->fillRect(stripe, item.palette.color(group, QPalette::Accent));
      }
    } else {
      const Qt::Alignment alignment =
          index.column() == QtRecentProjectsModel::ActionsColumn
              ? Qt::AlignCenter
              : QStyle::visualAlignment(item.direction, Qt::AlignLeft) |
                    Qt::AlignVCenter;
      painter->drawText(logical_content, alignment,
                        QFontMetrics(item.font).elidedText(
                            text, Qt::ElideRight, logical_content.width()));
    }
    if (shared_style) {
      QPen divider(item.palette.color(group, QPalette::Mid));
      divider.setCosmetic(true);
      painter->setPen(divider);
      painter->drawLine(item.rect.bottomLeft(), item.rect.bottomRight());
    }
    painter->restore();
  }

private:
  QtUiScaleMetrics metrics_;
};

QString unavailable_metadata_text() {
  return QStringLiteral("\u2014");
}

QString format_local_timestamp(
    const std::optional<std::uint64_t> unix_milliseconds) {
  if (!unix_milliseconds.has_value()) {
    return unavailable_metadata_text();
  }
  const std::uint64_t maximum_qint64 =
      static_cast<std::uint64_t>(std::numeric_limits<qint64>::max());
  if (*unix_milliseconds > maximum_qint64) {
    return unavailable_metadata_text();
  }
  const QDateTime local_time =
      QDateTime::fromMSecsSinceEpoch(static_cast<qint64>(*unix_milliseconds))
          .toLocalTime();
  return local_time.isValid()
             ? QLocale().toString(local_time, QLocale::LongFormat)
             : unavailable_metadata_text();
}

std::uint64_t current_unix_milliseconds() {
  return static_cast<std::uint64_t>(
      std::max<qint64>(0, QDateTime::currentMSecsSinceEpoch()));
}

QString format_relative_timestamp(
    const std::optional<std::uint64_t> unix_milliseconds,
    std::uint64_t now_unix_ms, const QtProjectHomeLabels &labels) {
  if (!unix_milliseconds.has_value() ||
      *unix_milliseconds > static_cast<std::uint64_t>(
                               std::numeric_limits<qint64>::max())) {
    return unavailable_metadata_text();
  }
  const std::uint64_t elapsed_seconds =
      now_unix_ms > *unix_milliseconds
          ? (now_unix_ms - *unix_milliseconds) / 1000
          : 0;
  if (elapsed_seconds < 60) {
    return labels.modified_just_now;
  }
  const auto plural = [](std::uint64_t count, const QString &singular,
                         const QString &multiple) {
    return count == 1 ? singular : multiple.arg(QLocale().toString(count));
  };
  if (elapsed_seconds < 60 * 60) {
    return plural(elapsed_seconds / 60, labels.modified_minute_ago,
                  labels.modified_minutes_ago);
  }
  if (elapsed_seconds < 24 * 60 * 60) {
    return plural(elapsed_seconds / (60 * 60), labels.modified_hour_ago,
                  labels.modified_hours_ago);
  }
  const std::uint64_t elapsed_days = elapsed_seconds / (24 * 60 * 60);
  if (elapsed_days == 1) {
    return labels.modified_yesterday;
  }
  if (elapsed_days < 7) {
    return labels.modified_days_ago.arg(QLocale().toString(elapsed_days));
  }
  if (elapsed_days < 30) {
    return plural(elapsed_days / 7, labels.modified_week_ago,
                  labels.modified_weeks_ago);
  }
  if (elapsed_days < 365) {
    return plural(elapsed_days / 30, labels.modified_month_ago,
                  labels.modified_months_ago);
  }
  return plural(elapsed_days / 365, labels.modified_year_ago,
                labels.modified_years_ago);
}

} // namespace

/** Retained artwork and native text/buttons for one launcher resource card. */
class QtProjectHomeResourceCard final : public QWidget {
public:
  QtProjectHomeResourceCard(const QString &object_prefix,
                            const QString &artwork, ProjectHomeGlyph glyph,
                            QWidget *parent)
      : QWidget(parent), source_image_(artwork), action_glyph_(glyph) {
    setObjectName(object_prefix + QStringLiteral(".card"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    card_layout_ = new QVBoxLayout(this);
    title_ = new QLabel(this);
    title_->setObjectName(object_prefix + QStringLiteral(".title"));
    title_->setForegroundRole(QPalette::Text);
    title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    title_->setWordWrap(true);
    title_font_baseline_ = project_home_heading_font(*title_, 12.0, 2.0);
    card_layout_->addWidget(title_);
    description_ = new QLabel(this);
    description_->setObjectName(object_prefix + QStringLiteral(".description"));
    description_->setForegroundRole(QPalette::WindowText);
    description_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    description_->setWordWrap(true);
    card_layout_->addWidget(description_);
    card_layout_->addStretch(1);
    action_ = new QPushButton(this);
    action_->setObjectName(object_prefix + QStringLiteral(".action"));
    action_->setAutoDefault(false);
    action_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    card_layout_->addWidget(action_, 0, Qt::AlignLeading);
    apply_ui_scale(QtUiScaleMetrics{});
    refresh_action_icon();
  }

  void set_labels(const QString &title, const QString &description,
                   const QString &action) {
    title_->setText(title);
    description_->setText(description);
    action_->setText(action);
    action_->setAccessibleName(action);
    setAccessibleName(title);
    setAccessibleDescription(description);
    updateGeometry();
  }

  void apply_ui_scale(const QtUiScaleMetrics &metrics) {
    metrics_ = metrics;
    title_->setFont(metrics.font(title_font_baseline_));
    card_layout_->setContentsMargins(metrics.margins(QMargins(24, 24, 24, 24)));
    card_layout_->setSpacing(metrics.px(8));
    action_->setMinimumHeight(metrics.px(34));
    action_->setIconSize(metrics.size(QSize(18, 18)));
    setMinimumHeight(metrics.px(180));
    updateGeometry();
    update();
  }

protected:
  bool event(QEvent *event) override {
    const bool handled = QWidget::event(event);
    if (event->type() == QEvent::DevicePixelRatioChange ||
        event->type() == QEvent::Show) {
      refresh_artwork();
    }
    if (action_ != nullptr &&
        (event->type() == QEvent::PaletteChange ||
         event->type() == QEvent::StyleChange ||
         event->type() == QEvent::LayoutDirectionChange ||
         event->type() == QEvent::Show)) {
      refresh_action_icon();
    }
    return handled;
  }

  void resizeEvent(QResizeEvent *event) override {
    QWidget::resizeEvent(event);
    refresh_artwork();
  }

  void paintEvent(QPaintEvent *event) override {
    Q_UNUSED(event);
    QStyleOption option;
    option.initFrom(this);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const qreal hairline = 1.0 / painter.device()->devicePixelRatioF();
    const QRectF bounds = QRectF(rect()).adjusted(
        hairline * 0.5, hairline * 0.5, -hairline * 0.5, -hairline * 0.5);
    QPainterPath outline;
    outline.addRoundedRect(bounds, metrics_.px(6.0), metrics_.px(6.0));
    painter.setClipPath(outline);
    painter.fillRect(rect(), palette().brush(QPalette::AlternateBase));
    if (qt_application_style_active(&option) && !scaled_artwork_.isNull()) {
      const QSizeF artwork_size = scaled_artwork_.deviceIndependentSize();
      painter.save();
      if (layoutDirection() == Qt::RightToLeft) {
        painter.translate(width(), 0);
        painter.scale(-1.0, 1.0);
      }
      painter.drawPixmap(QPointF((width() - artwork_size.width()) * 0.5,
                                  (height() - artwork_size.height()) * 0.5),
                           scaled_artwork_);
      painter.restore();
      QColor darkened = palette().color(QPalette::Window);
      darkened.setAlphaF(0.16);
      painter.fillRect(rect(), darkened);
      QColor opaque = palette().color(QPalette::Window);
      QColor middle = opaque;
      QColor trailing = opaque;
      opaque.setAlphaF(0.97);
      middle.setAlphaF(0.83);
      trailing.setAlphaF(0.24);
      QLinearGradient overlay(layoutDirection() == Qt::RightToLeft ? width() : 0,
                               0,
                               layoutDirection() == Qt::RightToLeft ? 0 : width(),
                               0);
      overlay.setColorAt(0.0, opaque);
      overlay.setColorAt(0.55, middle);
      overlay.setColorAt(1.0, trailing);
      painter.fillRect(rect(), overlay);
    }
    painter.setClipping(false);
    QPen border(palette().color(QPalette::Midlight));
    border.setWidthF(hairline);
    painter.setPen(border);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);
  }

private:
  void refresh_action_icon() {
    QStyleOption option;
    option.initFrom(this);
    action_->setIcon(project_home_icon(
        action_glyph_, palette(), qt_application_style_active(&option)
                                     ? QPalette::Accent : QPalette::ButtonText,
        layoutDirection()));
    const Qt::Alignment alignment = project_home_leading_alignment(
        layoutDirection(), Qt::AlignTop);
    title_->setAlignment(alignment);
    description_->setAlignment(alignment);
  }

  void refresh_artwork() {
    const qreal device_pixel_ratio = devicePixelRatioF();
    if (source_image_.isNull() || size().isEmpty() ||
        (cached_size_ == size() && cached_device_pixel_ratio_ == device_pixel_ratio)) {
      return;
    }
    cached_size_ = size();
    cached_device_pixel_ratio_ = device_pixel_ratio;
    const QSize backing_size(std::max(1, qRound(width() * device_pixel_ratio)),
                              std::max(1, qRound(height() * device_pixel_ratio)));
    scaled_artwork_ = QPixmap::fromImage(source_image_.scaled(
        backing_size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
    scaled_artwork_.setDevicePixelRatio(device_pixel_ratio);
    update();
  }

  QVBoxLayout *card_layout_ = nullptr;
  QLabel *title_ = nullptr;
  QLabel *description_ = nullptr;
  QPushButton *action_ = nullptr;
  QFont title_font_baseline_;
  QImage source_image_;
  QPixmap scaled_artwork_;
  QSize cached_size_;
  qreal cached_device_pixel_ratio_ = 0.0;
  QtUiScaleMetrics metrics_;
  ProjectHomeGlyph action_glyph_;
};

QtProjectHomeLabels QtProjectHomeLabels::defaults() {
  return {
      .new_project = QStringLiteral("New project"),
      .open_project = QStringLiteral("Open project\u2026"),
      .recent_projects = QStringLiteral("Recent projects"),
      .recent_name_column = QStringLiteral("Project"),
      .recent_last_modified_column = QStringLiteral("Last modified"),
      .recent_editor_version_column = QStringLiteral("Editor version"),
      .recent_actions_column = QStringLiteral("Actions"),
      .no_recent_projects = QStringLiteral("No recent projects"),
      .unavailable_project = QStringLiteral("Project is unavailable"),
      .open_recent_project = QStringLiteral("Open"),
      .open_recent_project_location =
          QStringLiteral("Open file location"),
      .copy_recent_project_path = QStringLiteral("Copy path"),
      .remove_recent_project = QStringLiteral("Remove from recent"),
  };
}

QtRecentProjectsModel::QtRecentProjectsModel(QObject *parent)
    : QAbstractTableModel(parent), now_unix_ms_(current_unix_milliseconds()) {}

int QtRecentProjectsModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(projects_.size());
}

int QtRecentProjectsModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : ColumnCount;
}

QVariant QtRecentProjectsModel::data(const QModelIndex &index,
                                     int role) const {
  const QtRecentProjectItem *project = project_at(index.row());
  if (project == nullptr || !index.isValid() ||
      index.column() < 0 || index.column() >= ColumnCount) {
    return {};
  }

  if (role == ManifestPathRole) {
    return project->manifest_path;
  }
  if (role == AvailableRole) {
    return project->available;
  }
  if (role == Qt::TextAlignmentRole &&
      index.column() == ActionsColumn) {
    return Qt::AlignCenter;
  }
  if ((role == Qt::ToolTipRole || role == Qt::AccessibleDescriptionRole) &&
      index.column() == LastModifiedColumn) {
    return format_local_timestamp(project->manifest_last_modified_unix_ms);
  }
  if (role == Qt::ToolTipRole) {
    if (index.column() == ActionsColumn) {
      return labels_.recent_actions_column;
    }
    if (!project->available) {
      return QStringLiteral("%1\n%2")
          .arg(project->manifest_path, labels_.unavailable_project);
    }
    return project->manifest_path;
  }
  if (role == Qt::AccessibleDescriptionRole && !project->available) {
    return labels_.unavailable_project;
  }
  if (role != Qt::DisplayRole && role != Qt::AccessibleTextRole) {
    return {};
  }

  if (index.column() == ActionsColumn) {
    return role == Qt::DisplayRole ? QStringLiteral("\u2026")
                                   : labels_.recent_actions_column;
  }
  if (index.column() == NameColumn) {
    const QString display_name =
        project->available
            ? project->name
            : QStringLiteral("%1 (%2)")
                  .arg(project->name, labels_.unavailable_project);
    return QStringLiteral("%1\n%2")
        .arg(display_name, project->manifest_path);
  }
  if (index.column() == LastModifiedColumn) {
    return relative_times_[static_cast<std::size_t>(index.row())];
  }
  return project->last_saved_app_version.isEmpty()
             ? unavailable_metadata_text()
             : QStringLiteral("v%1").arg(project->last_saved_app_version);
}

QVariant QtRecentProjectsModel::headerData(int section,
                                           Qt::Orientation orientation,
                                           int role) const {
  if (orientation != Qt::Horizontal ||
      (role != Qt::DisplayRole && role != Qt::AccessibleTextRole)) {
    return {};
  }
  if (section == NameColumn) {
    return labels_.recent_name_column;
  }
  if (section == LastModifiedColumn) {
    return labels_.recent_last_modified_column;
  }
  if (section == EditorVersionColumn) {
    return labels_.recent_editor_version_column;
  }
  if (section == ActionsColumn) {
    return role == Qt::DisplayRole ? QString{} : labels_.recent_actions_column;
  }
  return {};
}

Qt::ItemFlags QtRecentProjectsModel::flags(const QModelIndex &index) const {
  const QtRecentProjectItem *project = project_at(index.row());
  if (!index.isValid() || project == nullptr) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void QtRecentProjectsModel::set_labels(QtProjectHomeLabels labels) {
  labels_ = std::move(labels);
  for (std::size_t row = 0; row < projects_.size(); ++row) {
    relative_times_[row] = format_relative_timestamp(
        projects_[row].manifest_last_modified_unix_ms, now_unix_ms_, labels_);
  }
  emit headerDataChanged(Qt::Horizontal, NameColumn, ActionsColumn);
  if (!projects_.empty()) {
    emit dataChanged(index(0, 0),
                     index(static_cast<int>(projects_.size()) - 1,
                           ColumnCount - 1),
                     {Qt::DisplayRole, Qt::AccessibleTextRole,
                      Qt::ToolTipRole, Qt::AccessibleDescriptionRole});
  }
}

bool QtRecentProjectsModel::set_projects(
    std::vector<QtRecentProjectItem> projects) {
  if (projects.size() > kQtProjectHomeMaximumRecentProjects) {
    projects.resize(kQtProjectHomeMaximumRecentProjects);
  }
  if (projects_ == projects) {
    return false;
  }
  beginResetModel();
  projects_ = std::move(projects);
  relative_times_.clear();
  relative_times_.reserve(projects_.size());
  now_unix_ms_ = current_unix_milliseconds();
  for (const QtRecentProjectItem &project : projects_) {
    relative_times_.push_back(format_relative_timestamp(
        project.manifest_last_modified_unix_ms, now_unix_ms_, labels_));
  }
  endResetModel();
  return true;
}

void QtRecentProjectsModel::refresh_relative_times(std::uint64_t now_unix_ms) {
  now_unix_ms_ = now_unix_ms;
  for (std::size_t row = 0; row < projects_.size(); ++row) {
    const QString updated = format_relative_timestamp(
        projects_[row].manifest_last_modified_unix_ms, now_unix_ms_, labels_);
    if (relative_times_[row] == updated) {
      continue;
    }
    relative_times_[row] = updated;
    const QModelIndex changed = index(static_cast<int>(row), LastModifiedColumn);
    emit dataChanged(changed, changed,
                     {Qt::DisplayRole, Qt::AccessibleTextRole});
  }
}

const QtRecentProjectItem *
QtRecentProjectsModel::project_at(int row) const {
  if (row < 0 || row >= static_cast<int>(projects_.size())) {
    return nullptr;
  }
  return &projects_[static_cast<std::size_t>(row)];
}

QtProjectHomePage::QtProjectHomePage(QWidget *parent) : QWidget(parent) {
  initialize_project_home_artwork();
  setObjectName(QStringLiteral("projectHome"));
  setAutoFillBackground(true);
  setBackgroundRole(QPalette::Window);
  auto *root_layout = new QVBoxLayout(this);
  root_layout->setContentsMargins(0, 0, 0, 0);
  root_layout->setSpacing(0);

  scroll_area_ = new QScrollArea(this);
  scroll_area_->setObjectName(QStringLiteral("projectHome.scroll"));
  scroll_area_->setFrameShape(QFrame::NoFrame);
  scroll_area_->setWidgetResizable(true);
  scroll_area_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  root_layout->addWidget(scroll_area_);
  content_ = new QWidget(scroll_area_);
  content_->setObjectName(QStringLiteral("projectHome.recentContent"));
  content_->setBackgroundRole(QPalette::Window);
  content_->installEventFilter(this);
  recent_layout_ = new QVBoxLayout(content_);

  header_layout_ = new QBoxLayout(QBoxLayout::LeftToRight);
  brand_ = new QWidget(content_);
  brand_->setObjectName(QStringLiteral("projectHome.brand"));
  brand_layout_ = new QHBoxLayout(brand_);
  brand_layout_->setContentsMargins(0, 0, 0, 0);
  logo_ = new QLabel(brand_);
  logo_->setObjectName(QStringLiteral("projectHome.logo"));
  logo_->setAccessibleName(QStringLiteral("Quader"));
  logo_->setAlignment(Qt::AlignCenter);
  logo_->installEventFilter(this);
  brand_layout_->addWidget(logo_, 0, Qt::AlignVCenter);
  title_ = new QLabel(brand_);
  title_->setObjectName(QStringLiteral("projectHome.title"));
  title_->setForegroundRole(QPalette::Text);
  title_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
  title_->setWordWrap(true);
  title_font_baseline_ = project_home_heading_font(*title_, 20.0, 8.0);
  brand_layout_->addWidget(title_, 1);
  header_layout_->addWidget(brand_, 1);

  project_actions_ = new QWidget(content_);
  project_actions_->setObjectName(QStringLiteral("projectHome.projectActions"));
  project_actions_->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Preferred);
  project_actions_layout_ = new QHBoxLayout(project_actions_);
  project_actions_layout_->setContentsMargins(0, 0, 0, 0);
  open_project_button_ = new QPushButton(project_actions_);
  open_project_button_->setObjectName(
      QStringLiteral("projectHome.openProject"));
  project_actions_layout_->addWidget(open_project_button_);
  new_project_button_ = new QPushButton(project_actions_);
  new_project_button_->setObjectName(
      QStringLiteral("projectHome.newProject"));
  new_project_button_->setDefault(true);
  project_actions_layout_->addWidget(new_project_button_);
  header_layout_->addWidget(project_actions_);
  recent_layout_->addLayout(header_layout_);
  header_body_spacer_ =
      new QSpacerItem(0, 12, QSizePolicy::Minimum, QSizePolicy::Fixed);
  recent_layout_->addSpacerItem(header_body_spacer_);

  recent_projects_label_ = new QLabel(content_);
  recent_projects_label_->setObjectName(
      QStringLiteral("projectHome.recentProjects.label"));
  recent_projects_label_->setForegroundRole(QPalette::Text);
  recent_projects_label_->setWordWrap(true);
  recent_heading_font_baseline_ =
      project_home_heading_font(*recent_projects_label_, 12.0, 2.0);
  recent_layout_->addWidget(recent_projects_label_);

  auto *recent_frame = new RecentProjectsFrame(content_);
  recent_projects_frame_ = recent_frame;
  recent_projects_stack_ = recent_frame->content();
  recent_projects_stack_->setObjectName(
      QStringLiteral("projectHome.recentProjects.stack"));

  recent_projects_view_ = new QTableView(recent_projects_stack_);
  recent_projects_view_->setObjectName(
      QStringLiteral("projectHome.recentProjects.view"));
  recent_projects_view_->setFrameShape(QFrame::NoFrame);
  recent_projects_view_->setHorizontalHeader(
      new RecentProjectsHeader(recent_projects_view_));
  recent_projects_view_->setSelectionBehavior(
      QAbstractItemView::SelectRows);
  recent_projects_view_->setSelectionMode(
      QAbstractItemView::SingleSelection);
  recent_projects_view_->setShowGrid(false);
  recent_projects_view_->setMouseTracking(true);
  recent_projects_view_->viewport()->installEventFilter(this);
  recent_projects_view_->setWordWrap(true);
  recent_projects_view_->setAlternatingRowColors(false);
  recent_projects_view_->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
  recent_projects_view_->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
  recent_projects_view_->setItemDelegate(
      new RecentProjectDelegate(recent_projects_view_));
  recent_projects_view_->verticalHeader()->hide();
  recent_projects_view_->horizontalHeader()->setStretchLastSection(false);
  recent_projects_view_->horizontalHeader()->setDefaultAlignment(
      Qt::AlignLeading | Qt::AlignVCenter);
  recent_projects_view_->horizontalHeader()->setHighlightSections(false);
  recent_projects_view_->horizontalHeader()->setSectionsClickable(false);
  recent_projects_view_->installEventFilter(this);

  recent_projects_model_ =
      new QtRecentProjectsModel(recent_projects_view_);
  recent_projects_model_->setObjectName(
      QStringLiteral("projectHome.recentProjects.model"));
  recent_projects_view_->setModel(recent_projects_model_);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::NameColumn, QHeaderView::Stretch);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::LastModifiedColumn, QHeaderView::Fixed);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::EditorVersionColumn, QHeaderView::Fixed);
  recent_projects_view_->horizontalHeader()->setSectionResizeMode(
      QtRecentProjectsModel::ActionsColumn, QHeaderView::Fixed);
  recent_projects_stack_->addWidget(recent_projects_view_);

  no_recent_projects_label_ = new QLabel(recent_projects_stack_);
  no_recent_projects_label_->setObjectName(
      QStringLiteral("projectHome.recentProjects.empty"));
  no_recent_projects_label_->setAlignment(Qt::AlignCenter);
  no_recent_projects_label_->setWordWrap(true);
  no_recent_projects_label_->setSizePolicy(QSizePolicy::Expanding,
                                            QSizePolicy::Expanding);
  recent_projects_stack_->addWidget(no_recent_projects_label_);
  recent_layout_->addWidget(recent_projects_frame_, 1);

  cards_layout_ = new QBoxLayout(QBoxLayout::LeftToRight);
  tutorial_card_ = new QtProjectHomeResourceCard(
      QStringLiteral("projectHome.tutorial"),
      QStringLiteral(":/quader/home/tutorial.png"), ProjectHomeGlyph::Arrow,
      content_);
  documentation_card_ = new QtProjectHomeResourceCard(
      QStringLiteral("projectHome.documentation"),
      QStringLiteral(":/quader/home/documentation.png"), ProjectHomeGlyph::External,
      content_);
  cards_layout_->addWidget(tutorial_card_, 1);
  cards_layout_->addWidget(documentation_card_, 1);
  recent_layout_->addLayout(cards_layout_);
  scroll_area_->setWidget(content_);

  recent_project_actions_menu_ = new QMenu(this);
  recent_project_actions_menu_->setObjectName(
      QStringLiteral("projectHome.recentProjects.actionsMenu"));
  open_recent_project_action_ =
      recent_project_actions_menu_->addAction(QString{});
  open_recent_project_action_->setObjectName(
      QStringLiteral("projectHome.recentProjects.action.open"));
  open_recent_project_location_action_ =
      recent_project_actions_menu_->addAction(QString{});
  open_recent_project_location_action_->setObjectName(
      QStringLiteral("projectHome.recentProjects.action.openFileLocation"));
  copy_recent_project_path_action_ =
      recent_project_actions_menu_->addAction(QString{});
  copy_recent_project_path_action_->setObjectName(
      QStringLiteral("projectHome.recentProjects.action.copyPath"));
  remove_recent_project_action_ =
      recent_project_actions_menu_->addAction(QString{});
  remove_recent_project_action_->setObjectName(
      QStringLiteral("projectHome.recentProjects.action.removeFromRecent"));
  open_recent_project_action_->setEnabled(false);
  open_recent_project_location_action_->setEnabled(false);
  copy_recent_project_path_action_->setEnabled(false);
  remove_recent_project_action_->setEnabled(false);

  relative_time_timer_ = new QTimer(this);
  relative_time_timer_->setObjectName(
      QStringLiteral("projectHome.recentProjects.relativeTimeTimer"));
  relative_time_timer_->setTimerType(Qt::CoarseTimer);
  relative_time_timer_->setInterval(60 * 1000);
  QObject::connect(relative_time_timer_, &QTimer::timeout, this, [this]() {
    recent_projects_model_->refresh_relative_times(current_unix_milliseconds());
  });
  QObject::connect(recent_projects_model_, &QAbstractItemModel::modelReset, this,
                   [this]() { refresh_recent_project_cursor(); });
  QObject::connect(recent_projects_view_->verticalScrollBar(),
                   &QScrollBar::valueChanged, this,
                   [this]() { refresh_recent_project_cursor(); });
  QObject::connect(recent_projects_view_->horizontalScrollBar(),
                   &QScrollBar::valueChanged, this,
                   [this]() { refresh_recent_project_cursor(); });
  QObject::connect(recent_projects_view_->horizontalHeader(),
                   &QHeaderView::sectionResized, this,
                   [this]() { refresh_recent_project_cursor(); });
  QObject::connect(recent_projects_view_->horizontalHeader(),
                   &QHeaderView::sectionMoved, this,
                   [this]() { refresh_recent_project_cursor(); });

  QObject::connect(new_project_button_, &QPushButton::clicked, this,
                   [this]() {
                     if (new_project_requested_) {
                       new_project_requested_();
                     }
                   });
  QObject::connect(open_project_button_, &QPushButton::clicked, this,
                   [this]() {
                     if (open_project_requested_) {
                       open_project_requested_();
                     }
                   });
  QObject::connect(recent_projects_view_, &QTableView::clicked, this,
                   [this](const QModelIndex &index) {
                     if (index.column() ==
                         QtRecentProjectsModel::ActionsColumn) {
                       show_recent_project_actions(index);
                     }
                   });
  QObject::connect(recent_projects_view_, &QTableView::activated, this,
                   [this](const QModelIndex &index) {
                     if (index.column() ==
                         QtRecentProjectsModel::ActionsColumn) {
                       show_recent_project_actions(index);
                     } else {
                       dispatch_recent_project_open(index);
                     }
                   });
  QObject::connect(open_recent_project_action_, &QAction::triggered, this,
                   [this]() {
                     dispatch_recent_project_action(
                         QtRecentProjectAction::Open);
                   });
  QObject::connect(open_recent_project_location_action_,
                   &QAction::triggered, this, [this]() {
                     dispatch_recent_project_action(
                         QtRecentProjectAction::OpenFileLocation);
                   });
  QObject::connect(copy_recent_project_path_action_, &QAction::triggered,
                   this, [this]() {
                     dispatch_recent_project_action(
                         QtRecentProjectAction::CopyPath);
                   });
  QObject::connect(remove_recent_project_action_, &QAction::triggered, this,
                   [this]() {
                     dispatch_recent_project_action(
                         QtRecentProjectAction::RemoveFromRecent);
                   });

  apply_ui_scale(QtUiScaleMetrics{});
  refresh_labels();
  refresh_recent_projects_page();
}

void QtProjectHomePage::apply_ui_scale(
    const QtUiScaleMetrics &metrics) {
  if (applied_ui_scale_revision_ == metrics.revision() &&
      applied_ui_scale_factor_ == metrics.factor()) {
    return;
  }
  applied_ui_scale_revision_ = metrics.revision();
  applied_ui_scale_factor_ = metrics.factor();

  title_->setFont(metrics.font(title_font_baseline_));
  logo_->setFixedSize(metrics.size(QSize(32, 32)));
  brand_layout_->setSpacing(metrics.px(12));
  refresh_project_logo();
  recent_projects_label_->setFont(
      metrics.font(recent_heading_font_baseline_));
  header_layout_->setSpacing(metrics.px(16));
  project_actions_layout_->setSpacing(metrics.px(12));
  header_body_spacer_->changeSize(
      0, metrics.px(12), QSizePolicy::Minimum, QSizePolicy::Fixed);
  new_project_button_->setMinimumHeight(metrics.px(36));
  open_project_button_->setMinimumHeight(metrics.px(36));
  new_project_button_->setIconSize(metrics.size(QSize(18, 18)));
  open_project_button_->setIconSize(metrics.size(QSize(18, 18)));
  recent_layout_->setSpacing(metrics.px(14));
  cards_layout_->setSpacing(metrics.px(16));
  tutorial_card_->apply_ui_scale(metrics);
  documentation_card_->apply_ui_scale(metrics);
  static_cast<RecentProjectsFrame *>(recent_projects_frame_)->set_metrics(metrics);
  static_cast<RecentProjectDelegate *>(recent_projects_view_->itemDelegate())
      ->set_metrics(metrics);
  static_cast<RecentProjectsHeader *>(recent_projects_view_->horizontalHeader())
      ->set_metrics(metrics);
  refresh_project_icons();
  refresh_layout_metrics();
  refresh_reflow();
  updateGeometry();
}

void QtProjectHomePage::refresh_layout_metrics() {
  if (refreshing_layout_ || recent_projects_view_ == nullptr ||
      applied_ui_scale_factor_ <= 0.0F) {
    return;
  }
  refreshing_layout_ = true;
  const QtUiScaleMetrics metrics(applied_ui_scale_factor_,
                                 applied_ui_scale_revision_);
  const QFontMetrics body_metrics(recent_projects_view_->font());
  QFont name_font(recent_projects_view_->font());
  name_font.setWeight(QFont::DemiBold);
  const int row_height = std::max(
      metrics.px(56), body_metrics.height() + QFontMetrics(name_font).height() +
                          metrics.px(19));
  recent_projects_view_->verticalHeader()->setDefaultSectionSize(
      row_height);
  QHeaderView *header = recent_projects_view_->horizontalHeader();
  const QFontMetrics header_metrics(header->font());
  header->setMinimumHeight(std::max(metrics.px(36),
                                    header_metrics.height() + metrics.px(16)));
  header->setMinimumSectionSize(metrics.px(44));
  recent_projects_view_->setColumnWidth(
      QtRecentProjectsModel::LastModifiedColumn,
      std::max(metrics.px(184), header_metrics.horizontalAdvance(
                                   labels_.recent_last_modified_column) +
                                   metrics.px(32)));
  recent_projects_view_->setColumnWidth(
      QtRecentProjectsModel::EditorVersionColumn,
      std::max(metrics.px(132), header_metrics.horizontalAdvance(
                                  labels_.recent_editor_version_column) +
                                  metrics.px(32)));
  recent_projects_view_->setColumnWidth(QtRecentProjectsModel::ActionsColumn,
                                        metrics.px(52));
  // Keep a useful native table viewport even when the outer page must scroll.
  // Additional recent projects remain in this table's own scroll model.
  recent_projects_stack_->setMinimumHeight(header->minimumHeight() +
                                            row_height * 3 + metrics.px(2));
  refreshing_layout_ = false;
  refresh_recent_project_cursor();
}

bool QtProjectHomePage::eventFilter(QObject *watched, QEvent *event) {
  if (event != nullptr) {
    if (recent_projects_view_ != nullptr &&
        watched == recent_projects_view_->viewport()) {
      if (event->type() == QEvent::MouseMove) {
        update_recent_project_cursor(
            static_cast<QMouseEvent *>(event)->position().toPoint());
      } else if (event->type() == QEvent::Enter) {
        update_recent_project_cursor(
            static_cast<QEnterEvent *>(event)->position().toPoint());
      } else if (event->type() == QEvent::Leave ||
                 event->type() == QEvent::Hide) {
        recent_projects_view_->viewport()->unsetCursor();
      } else if (event->type() == QEvent::Resize ||
                 event->type() == QEvent::Show ||
                 event->type() == QEvent::EnabledChange) {
        refresh_recent_project_cursor();
      }
    } else if (watched == logo_ &&
        (event->type() == QEvent::Show ||
         event->type() == QEvent::DevicePixelRatioChange ||
         event->type() == QEvent::ScreenChangeInternal)) {
      refresh_project_logo();
    } else if (watched == content_ && event->type() == QEvent::Resize) {
      refresh_reflow();
    } else if (watched == recent_projects_view_ &&
               (event->type() == QEvent::FontChange ||
                event->type() == QEvent::StyleChange)) {
      refresh_layout_metrics();
    }
  }
  return QWidget::eventFilter(watched, event);
}

void QtProjectHomePage::refresh_recent_project_cursor() {
  if (recent_projects_view_ != nullptr && recent_projects_model_ != nullptr) {
    update_recent_project_cursor(
        recent_projects_view_->viewport()->mapFromGlobal(QCursor::pos()));
  }
}

void QtProjectHomePage::update_recent_project_cursor(
    const QPoint &viewport_position) {
  QWidget *viewport = recent_projects_view_->viewport();
  const QModelIndex hovered = recent_projects_view_->indexAt(viewport_position);
  const bool action_hovered =
      viewport->isVisible() && viewport->isEnabled() &&
      viewport->rect().contains(viewport_position) && hovered.isValid() &&
      hovered.column() == QtRecentProjectsModel::ActionsColumn;
  if (action_hovered) {
    if (!viewport->testAttribute(Qt::WA_SetCursor) ||
        viewport->cursor().shape() != Qt::PointingHandCursor) {
      viewport->setCursor(Qt::PointingHandCursor);
    }
  } else if (viewport->testAttribute(Qt::WA_SetCursor)) {
    viewport->unsetCursor();
  }
}

void QtProjectHomePage::showEvent(QShowEvent *event) {
  QWidget::showEvent(event);
  recent_projects_model_->refresh_relative_times(current_unix_milliseconds());
  if (recent_projects_model_->rowCount() != 0) {
    relative_time_timer_->start();
  }
  refresh_recent_project_cursor();
}

void QtProjectHomePage::hideEvent(QHideEvent *event) {
  relative_time_timer_->stop();
  recent_projects_view_->viewport()->unsetCursor();
  QWidget::hideEvent(event);
}

void QtProjectHomePage::changeEvent(QEvent *event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::PaletteChange ||
      event->type() == QEvent::StyleChange ||
      event->type() == QEvent::LayoutDirectionChange) {
    refresh_project_icons();
  }
  if (event->type() == QEvent::WindowIconChange) {
    refresh_project_logo();
  }
}

void QtProjectHomePage::refresh_project_logo() {
  if (logo_ == nullptr) {
    return;
  }
  const QIcon icon = QApplication::windowIcon();
  if (icon.isNull()) {
    logo_->clear();
    return;
  }
  logo_->setPixmap(icon.pixmap(logo_->size(), logo_->devicePixelRatioF()));
}

void QtProjectHomePage::refresh_project_icons() {
  if (new_project_button_ == nullptr || open_project_button_ == nullptr) {
    return;
  }
  new_project_button_->setIcon(project_home_icon(
      ProjectHomeGlyph::Plus, palette(), QPalette::ButtonText, layoutDirection()));
  open_project_button_->setIcon(project_home_icon(
      ProjectHomeGlyph::Folder, palette(), QPalette::ButtonText, layoutDirection()));
  const Qt::Alignment alignment = project_home_leading_alignment(
      layoutDirection(), Qt::AlignVCenter);
  if (title_ != nullptr) {
    title_->setAlignment(alignment);
  }
  if (recent_projects_label_ != nullptr) {
    recent_projects_label_->setAlignment(alignment);
  }
  if (recent_projects_view_ != nullptr) {
    recent_projects_view_->horizontalHeader()->setDefaultAlignment(alignment);
    refresh_recent_project_cursor();
  }
}

void QtProjectHomePage::refresh_reflow() {
  if (refreshing_layout_ || cards_layout_ == nullptr ||
      applied_ui_scale_factor_ <= 0.0F) {
    return;
  }
  refreshing_layout_ = true;
  const QtUiScaleMetrics metrics(applied_ui_scale_factor_,
                                 applied_ui_scale_revision_);
  const bool narrow = content_->width() < metrics.px(820);
  const int horizontal_margin = metrics.px(narrow ? 24 : 48);
  const QMargins margins(horizontal_margin, metrics.px(32),
                          horizontal_margin, metrics.px(32));
  if (recent_layout_->contentsMargins() != margins) {
    recent_layout_->setContentsMargins(margins);
  }
  const int available_width = content_->width() - horizontal_margin * 2;
  const QBoxLayout::Direction cards_direction = available_width < metrics.px(740)
                                                  ? QBoxLayout::TopToBottom
                                                  : QBoxLayout::LeftToRight;
  if (cards_layout_->direction() != cards_direction) {
    cards_layout_->setDirection(cards_direction);
  }
  const int title_width = title_->fontMetrics().horizontalAdvance(title_->text()) +
                          logo_->width() + brand_layout_->spacing();
  const bool stack_header = available_width <
      project_actions_->sizeHint().width() + title_width + metrics.px(40);
  const QBoxLayout::Direction header_direction = stack_header
                                                   ? QBoxLayout::TopToBottom
                                                   : QBoxLayout::LeftToRight;
  if (header_layout_->direction() != header_direction) {
    header_layout_->setDirection(header_direction);
    header_layout_->setAlignment(project_actions_, stack_header
                                                      ? Qt::AlignLeading
                                                      : Qt::Alignment{});
  }
  recent_layout_->invalidate();
  refreshing_layout_ = false;
}

void QtProjectHomePage::set_labels(QtProjectHomeLabels labels) {
  labels_ = std::move(labels);
  recent_projects_model_->set_labels(labels_);
  refresh_labels();
}

void QtProjectHomePage::set_recent_projects(
    std::vector<QtRecentProjectItem> projects) {
  if (!recent_projects_model_->set_projects(std::move(projects))) {
    return;
  }
  recent_project_actions_menu_->hide();
  recent_action_manifest_path_.clear();
  open_recent_project_action_->setEnabled(false);
  open_recent_project_location_action_->setEnabled(false);
  copy_recent_project_path_action_->setEnabled(false);
  remove_recent_project_action_->setEnabled(false);
  refresh_recent_projects_page();
}

void QtProjectHomePage::set_new_project_requested_callback(
    RequestCallback callback) {
  new_project_requested_ = std::move(callback);
}

void QtProjectHomePage::set_open_project_requested_callback(
    RequestCallback callback) {
  open_project_requested_ = std::move(callback);
}

void QtProjectHomePage::set_recent_project_action_requested_callback(
    RecentProjectActionCallback callback) {
  recent_project_action_requested_ = std::move(callback);
}

void QtProjectHomePage::dispatch_recent_project_open(
    const QModelIndex &index) {
  const QtRecentProjectItem *project =
      recent_projects_model_->project_at(index.row());
  if (project == nullptr || !project->available ||
      !recent_project_action_requested_) {
    return;
  }
  recent_project_action_requested_(QtRecentProjectAction::Open,
                                   project->manifest_path);
}

void QtProjectHomePage::dispatch_recent_project_action(
    QtRecentProjectAction action) {
  if (recent_action_manifest_path_.isEmpty() ||
      !recent_project_action_requested_) {
    return;
  }
  if (action == QtRecentProjectAction::Open &&
      !open_recent_project_action_->isEnabled()) {
    return;
  }
  recent_project_action_requested_(action, recent_action_manifest_path_);
}

void QtProjectHomePage::show_recent_project_actions(
    const QModelIndex &index) {
  const QtRecentProjectItem *project =
      recent_projects_model_->project_at(index.row());
  if (project == nullptr || !index.isValid()) {
    return;
  }
  recent_action_manifest_path_ = project->manifest_path;
  open_recent_project_action_->setEnabled(project->available);
  open_recent_project_location_action_->setEnabled(true);
  copy_recent_project_path_action_->setEnabled(true);
  remove_recent_project_action_->setEnabled(true);

  recent_projects_view_->selectRow(index.row());
  const QModelIndex action_index = recent_projects_model_->index(
      index.row(), QtRecentProjectsModel::ActionsColumn);
  recent_projects_view_->setCurrentIndex(action_index);
  const QRect action_rect = recent_projects_view_->visualRect(action_index);
  recent_project_actions_menu_->popup(
      recent_projects_view_->viewport()->mapToGlobal(action_rect.bottomRight()));
}

void QtProjectHomePage::refresh_labels() {
  title_->setText(labels_.title);
  title_->setAccessibleName(labels_.title);
  new_project_button_->setText(labels_.new_project);
  new_project_button_->setAccessibleName(labels_.new_project);
  open_project_button_->setText(labels_.open_project);
  open_project_button_->setAccessibleName(labels_.open_project);
  recent_projects_label_->setText(labels_.recent_projects);
  recent_projects_label_->setAccessibleName(labels_.recent_projects);
  no_recent_projects_label_->setText(labels_.no_recent_projects);
  open_recent_project_action_->setText(labels_.open_recent_project);
  open_recent_project_location_action_->setText(
      labels_.open_recent_project_location);
  copy_recent_project_path_action_->setText(
      labels_.copy_recent_project_path);
  remove_recent_project_action_->setText(labels_.remove_recent_project);
  tutorial_card_->set_labels(labels_.tutorial_title, labels_.tutorial_description,
                              labels_.tutorial_action);
  documentation_card_->set_labels(labels_.documentation_title,
                                   labels_.documentation_description,
                                   labels_.documentation_action);
  refresh_layout_metrics();
  refresh_reflow();
}

void QtProjectHomePage::refresh_recent_projects_page() {
  const bool has_recent_projects =
      recent_projects_model_->rowCount() != 0;
  recent_projects_stack_->setCurrentWidget(
      has_recent_projects ? static_cast<QWidget *>(recent_projects_view_)
                          : static_cast<QWidget *>(no_recent_projects_label_));
  if (has_recent_projects && isVisible()) {
    relative_time_timer_->start();
  } else {
    relative_time_timer_->stop();
  }
  refresh_recent_project_cursor();
}

} // namespace quader
