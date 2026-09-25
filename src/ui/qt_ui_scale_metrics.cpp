////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_ui_scale_metrics.hpp"

#include "ui/qt_application_style.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/ui_settings.hpp"

#include <QApplication>
#include <QDockWidget>
#include <QEvent>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QPointer>
#include <QStyleOption>
#include <QStyleFactory>
#include <QThread>
#include <QWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <utility>
#include <vector>

namespace quader {
namespace {

float normalized_factor(float factor) noexcept {
  if (!std::isfinite(factor)) {
    return UISettings::kDefaultInterfaceScale;
  }
  return std::clamp(factor, UISettings::kMinimumInterfaceScale,
                    UISettings::kMaximumInterfaceScale);
}

int rounded_pixels(double value) noexcept {
  const double bounded =
      std::clamp(value, static_cast<double>(std::numeric_limits<int>::min()),
                 static_cast<double>(std::numeric_limits<int>::max()));
  return static_cast<int>(std::lround(bounded));
}

bool scale_pixel_metric(QStyle::PixelMetric metric) noexcept {
  switch (metric) {
  case QStyle::PM_ButtonMargin:
  case QStyle::PM_ButtonDefaultIndicator:
  case QStyle::PM_MenuButtonIndicator:
  case QStyle::PM_ScrollBarExtent:
  case QStyle::PM_ScrollBarSliderMin:
  case QStyle::PM_SliderThickness:
  case QStyle::PM_SliderControlThickness:
  case QStyle::PM_SliderLength:
  case QStyle::PM_SliderTickmarkOffset:
  case QStyle::PM_DockWidgetSeparatorExtent:
  case QStyle::PM_DockWidgetHandleExtent:
  case QStyle::PM_TabBarTabOverlap:
  case QStyle::PM_TabBarTabHSpace:
  case QStyle::PM_TabBarTabVSpace:
  case QStyle::PM_TabBarBaseHeight:
  case QStyle::PM_TabBarBaseOverlap:
  case QStyle::PM_ProgressBarChunkWidth:
  case QStyle::PM_SplitterWidth:
  case QStyle::PM_TitleBarHeight:
  case QStyle::PM_MenuScrollerHeight:
  case QStyle::PM_MenuHMargin:
  case QStyle::PM_MenuVMargin:
  case QStyle::PM_MenuTearoffHeight:
  case QStyle::PM_MenuBarItemSpacing:
  case QStyle::PM_MenuBarVMargin:
  case QStyle::PM_MenuBarHMargin:
  case QStyle::PM_IndicatorWidth:
  case QStyle::PM_IndicatorHeight:
  case QStyle::PM_ExclusiveIndicatorWidth:
  case QStyle::PM_ExclusiveIndicatorHeight:
  case QStyle::PM_MdiSubWindowMinimizedWidth:
  case QStyle::PM_HeaderMargin:
  case QStyle::PM_HeaderMarkSize:
  case QStyle::PM_HeaderGripMargin:
  case QStyle::PM_TabBarScrollButtonWidth:
  case QStyle::PM_ToolBarHandleExtent:
  case QStyle::PM_ToolBarItemSpacing:
  case QStyle::PM_ToolBarItemMargin:
  case QStyle::PM_ToolBarSeparatorExtent:
  case QStyle::PM_ToolBarExtensionExtent:
  case QStyle::PM_SpinBoxSliderHeight:
  case QStyle::PM_ToolBarIconSize:
  case QStyle::PM_ListViewIconSize:
  case QStyle::PM_IconViewIconSize:
  case QStyle::PM_SmallIconSize:
  case QStyle::PM_LargeIconSize:
  case QStyle::PM_FocusFrameVMargin:
  case QStyle::PM_FocusFrameHMargin:
  case QStyle::PM_CheckBoxLabelSpacing:
  case QStyle::PM_TabBarIconSize:
  case QStyle::PM_SizeGripSize:
  case QStyle::PM_DockWidgetTitleMargin:
  case QStyle::PM_MessageBoxIconSize:
  case QStyle::PM_ButtonIconSize:
  case QStyle::PM_DockWidgetTitleBarButtonMargin:
  case QStyle::PM_RadioButtonLabelSpacing:
  case QStyle::PM_LayoutLeftMargin:
  case QStyle::PM_LayoutTopMargin:
  case QStyle::PM_LayoutRightMargin:
  case QStyle::PM_LayoutBottomMargin:
  case QStyle::PM_LayoutHorizontalSpacing:
  case QStyle::PM_LayoutVerticalSpacing:
  case QStyle::PM_TabBar_ScrollButtonOverlap:
  case QStyle::PM_TabCloseIndicatorWidth:
  case QStyle::PM_TabCloseIndicatorHeight:
  case QStyle::PM_ScrollView_ScrollBarSpacing:
  case QStyle::PM_SubMenuOverlap:
  case QStyle::PM_TreeViewIndentation:
  case QStyle::PM_HeaderDefaultSectionSizeHorizontal:
  case QStyle::PM_HeaderDefaultSectionSizeVertical:
  case QStyle::PM_TitleBarButtonIconSize:
  case QStyle::PM_TitleBarButtonSize:
  case QStyle::PM_LineEditIconSize:
  case QStyle::PM_LineEditIconMargin:
    return true;
  default:
    return false;
  }
}

QPalette resolved_visual_palette(const QPalette &palette) {
  QPalette resolved = palette;
  constexpr std::array<QPalette::ColorGroup, 3> kGroups{
      QPalette::Active, QPalette::Inactive, QPalette::Disabled};
  for (const QPalette::ColorGroup group : kGroups) {
    for (int role = 0; role < static_cast<int>(QPalette::NColorRoles);
         ++role) {
      const auto color_role = static_cast<QPalette::ColorRole>(role);
      if (color_role != QPalette::NoRole) {
        resolved.setBrush(group, color_role,
                          palette.brush(group, color_role));
      }
    }
  }
  return resolved;
}

} // namespace

QtUiScaleMetrics::QtUiScaleMetrics(float factor, std::uint64_t revision)
    : factor_(normalized_factor(factor)), revision_(revision) {}

int QtUiScaleMetrics::px(int design_pixels) const noexcept {
  return rounded_pixels(static_cast<double>(design_pixels) * factor_);
}

float QtUiScaleMetrics::px(float design_pixels) const noexcept {
  return design_pixels * factor_;
}

double QtUiScaleMetrics::px(double design_pixels) const noexcept {
  return design_pixels * static_cast<double>(factor_);
}

QSize QtUiScaleMetrics::size(const QSize &design_size) const noexcept {
  const int width =
      design_size.width() < 0 ? design_size.width() : px(design_size.width());
  const int height = design_size.height() < 0 ? design_size.height()
                                              : px(design_size.height());
  return {width, height};
}

QMargins
QtUiScaleMetrics::margins(const QMargins &design_margins) const noexcept {
  return {px(design_margins.left()), px(design_margins.top()),
          px(design_margins.right()), px(design_margins.bottom())};
}

QFont QtUiScaleMetrics::font(const QFont &design_font) const {
  QFont scaled = design_font;
  if (design_font.pointSizeF() > 0.0) {
    scaled.setPointSizeF(px(design_font.pointSizeF()));
  } else if (design_font.pixelSize() > 0) {
    scaled.setPixelSize(std::max(1, px(design_font.pixelSize())));
  }
  return scaled;
}

int QtUiScaleMetrics::device_pixels(int design_pixels,
                                    qreal device_pixel_ratio) const noexcept {
  if (design_pixels < 0) {
    return design_pixels;
  }
  const double usable_dpr =
      std::isfinite(device_pixel_ratio) && device_pixel_ratio > 0.0
          ? device_pixel_ratio
          : 1.0;
  return rounded_pixels(static_cast<double>(design_pixels) * factor_ *
                        usable_dpr);
}

QtUiScaleProxyStyle::QtUiScaleProxyStyle(const QtUiScaleMetrics &metrics)
    : QProxyStyle(QStringLiteral("fusion")), metrics_(metrics),
      design_metrics_style_(QStyleFactory::create(QStringLiteral("fusion"))) {
  setObjectName(QStringLiteral("quaderUiScaleStyle"));
  if (design_metrics_style_ != nullptr) {
    design_metrics_style_->setParent(this);
  }
}

void QtUiScaleProxyStyle::set_metrics(
    const QtUiScaleMetrics &metrics) noexcept {
  metrics_ = metrics;
}

int QtUiScaleProxyStyle::pixelMetric(PixelMetric metric,
                                     const QStyleOption *option,
                                     const QWidget *widget) const {
  if (widget != nullptr &&
      (qobject_cast<const QDockWidget *>(widget) != nullptr ||
       qobject_cast<const QDockWidget *>(widget->parentWidget()) != nullptr)) {
    QStyleOption dock_option;
    dock_option.initFrom(widget);
    if (qt_application_style_active(option != nullptr ? option : &dock_option)) {
      if (metric == PM_DockWidgetTitleMargin) {
        return metrics_.px(6);
      }
      if (metric == PM_DockWidgetTitleBarButtonMargin) {
        return metrics_.px(3);
      }
    }
  }
  if ((metric == PM_MenuHMargin || metric == PM_MenuVMargin) &&
      qobject_cast<const QMenu *>(widget) != nullptr) {
    // QMenu's layout supplies an option, but its scroller paint does not.
    // Both must resolve the same themed content inset.
    QStyleOption menu_option;
    menu_option.initFrom(widget);
    if (qt_application_style_active(option != nullptr ? option : &menu_option)) {
      return metrics_.px(4);
    }
  }
  // Fusion derives metrics such as PM_TabBarIconSize through
  // proxy()->pixelMetric(). Querying QProxyStyle's installed base would route
  // that dependency back through this override, producing factor-squared
  // results (or an unbounded same-metric recursion). A pristine Fusion style
  // resolves the complete design metric graph first; this outer seam then
  // applies the user factor exactly once.
  const int unscaled =
      design_metrics_style_ != nullptr
          ? design_metrics_style_->pixelMetric(metric, option, widget)
          : QProxyStyle::pixelMetric(metric, option, widget);
  if (!scale_pixel_metric(metric) || unscaled <= 0) {
    return unscaled;
  }
  return std::max(1, metrics_.px(unscaled));
}

QSize QtUiScaleProxyStyle::sizeFromContents(
    ContentsType type, const QStyleOption *option, const QSize &contents_size,
    const QWidget *widget) const {
  QSize size = QProxyStyle::sizeFromContents(type, option, contents_size, widget);
  const auto *item = qstyleoption_cast<const QStyleOptionMenuItem *>(option);
  if (type != CT_MenuItem || item == nullptr ||
      qobject_cast<const QMenu *>(widget) == nullptr ||
      !qt_application_style_active(option)) {
    return size;
  }
  if (item->menuItemType == QStyleOptionMenuItem::Separator) {
    if (item->text.isEmpty() && item->icon.isNull()) {
      size.setHeight(metrics_.px(8));
    }
    return size;
  }
  if (item->menuItemType == QStyleOptionMenuItem::Normal ||
      item->menuItemType == QStyleOptionMenuItem::DefaultItem ||
      item->menuItemType == QStyleOptionMenuItem::SubMenu) {
    const int height = std::max({size.height(), metrics_.px(28),
                                item->fontMetrics.height() + metrics_.px(8)});
    // Fusion derives its check/icon column from the painted row height.
    // Reserve the added height horizontally too, so longer labels and the
    // shortcut column retain their native separation at every scale.
    size.rwidth() += height - size.height();
    size.setHeight(height);
  }
  return size;
}

QIcon QtUiScaleProxyStyle::standardIcon(StandardPixmap icon,
                                        const QStyleOption *option,
                                        const QWidget *widget) const {
  if (qobject_cast<const QDockWidget *>(widget) != nullptr &&
      (icon == SP_TitleBarCloseButton || icon == SP_TitleBarNormalButton)) {
    QStyleOption dock_option;
    dock_option.initFrom(widget);
    const QStyleOption *effective = option != nullptr ? option : &dock_option;
    if (qt_application_style_active(effective)) {
      // Qt re-requests these icons after docking, feature and style changes.
      return qt_dock_title_glyph_icon(icon == SP_TitleBarCloseButton
                                         ? QtDockTitleGlyph::Close
                                         : QtDockTitleGlyph::Float,
                                     effective->palette);
    }
  }
  return QProxyStyle::standardIcon(icon, option, widget);
}

void QtUiScaleProxyStyle::drawControl(ControlElement element,
                                      const QStyleOption *option,
                                      QPainter *painter,
                                      const QWidget *widget) const {
  if (!qt_draw_application_control(element, option, painter, *this, metrics_,
                                    widget)) {
    QProxyStyle::drawControl(element, option, painter, widget);
  }
}

void QtUiScaleProxyStyle::drawPrimitive(PrimitiveElement element,
                                       const QStyleOption *option,
                                       QPainter *painter,
                                       const QWidget *widget) const {
  if (!qt_draw_application_primitive(element, option, painter, *this, metrics_,
                                      widget)) {
    QProxyStyle::drawPrimitive(element, option, painter, widget);
  }
}

void QtUiScaleProxyStyle::drawComplexControl(
    ComplexControl control, const QStyleOptionComplex *option,
    QPainter *painter, const QWidget *widget) const {
  if (!qt_draw_application_complex_control(control, option, painter, *this,
                                            metrics_, widget)) {
    QProxyStyle::drawComplexControl(control, option, painter, widget);
  }
}

int QtUiScaleProxyStyle::styleHint(StyleHint hint, const QStyleOption *option,
                                   const QWidget *widget,
                                   QStyleHintReturn *return_data) const {
  if (hint == SH_DockWidget_ButtonsHaveFrame && widget != nullptr) {
    QStyleOption dock_option;
    dock_option.initFrom(widget);
    if (qt_application_style_active(option != nullptr ? option : &dock_option)) {
      // Fusion's framed dock buttons shrink the icon from 16 to 10 pixels.
      return 0;
    }
  }
  if (hint == SH_Menu_Mask && qobject_cast<const QMenu *>(widget) != nullptr &&
      qt_application_style_active(option)) {
    if (auto *mask = qstyleoption_cast<QStyleHintReturnMask *>(return_data)) {
      QPainterPath outline;
      outline.addRoundedRect(QRectF(option->rect), metrics_.px(6.0),
                             metrics_.px(6.0));
      mask->region = QRegion(outline.toFillPolygon().toPolygon());
    }
    return 1;
  }
  if (hint == SH_Table_GridLineColor && option != nullptr &&
      QApplication::palette().resolveMask() != 0) {
    return static_cast<int>(option->palette.color(QPalette::Midlight).rgba());
  }
  return QProxyStyle::styleHint(hint, option, widget, return_data);
}

struct QtUiScaleApplicationController::Impl {
  struct ListenerRecord {
    ListenerId id = 0U;
    QPointer<QObject> receiver;
    ChangeListener listener;
  };

  QApplication &application;
  const QFont baseline_font;
  QtUiScaleMetrics metrics;
  QtUiScaleProxyStyle *style = nullptr;
  ListenerId next_listener_id = 1U;
  std::vector<ListenerRecord> listeners;

  Impl(QApplication &new_application, float initial_factor)
      : application(new_application), baseline_font(new_application.font()),
        metrics(initial_factor) {
    const QPalette application_palette = application.palette();
    // An empty resolve mask is intentional under Windows High Contrast: it
    // leaves the platform/style palette authoritative. Resolving every brush
    // here would silently turn that accessibility choice into an application
    // override. Ordinary themed palettes are materialized before replacing
    // Fusion so their complete visual result remains unchanged.
    const bool platform_palette_is_authoritative =
        application_palette.resolveMask() == 0;
    const QPalette preserved_palette =
        platform_palette_is_authoritative
            ? QPalette{}
            : resolved_visual_palette(application_palette);
    style = new QtUiScaleProxyStyle(metrics);
    QApplication::setStyle(style);
    QApplication::setPalette(preserved_palette);
    QApplication::setFont(metrics.font(baseline_font));
  }

  void notify_listeners() {
    std::erase_if(listeners, [](const ListenerRecord &record) {
      return record.receiver.isNull();
    });

    const std::vector<ListenerRecord> snapshot = listeners;
    for (const ListenerRecord &record : snapshot) {
      if (!record.receiver.isNull() && record.listener) {
        record.listener(metrics);
      }
    }
  }

  void refresh_widget_geometry() {
    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *widget : widgets) {
      if (widget == nullptr) {
        continue;
      }
      QPointer<QWidget> guarded_widget(widget);
      QEvent style_change(QEvent::StyleChange);
      QApplication::sendEvent(widget, &style_change);
      if (!guarded_widget.isNull()) {
        guarded_widget->updateGeometry();
        guarded_widget->update();
      }
    }
  }
};

QtUiScaleApplicationController::QtUiScaleApplicationController(
    QApplication &application, float initial_factor)
    : impl_(std::make_unique<Impl>(application, initial_factor)) {}

QtUiScaleApplicationController::~QtUiScaleApplicationController() {
  if (QCoreApplication::instance() == &impl_->application) {
    impl_->style->set_metrics(QtUiScaleMetrics{});
    QApplication::setFont(impl_->baseline_font);
  }
}

const QtUiScaleMetrics &
QtUiScaleApplicationController::metrics() const noexcept {
  return impl_->metrics;
}

const QFont &QtUiScaleApplicationController::baseline_font() const noexcept {
  return impl_->baseline_font;
}

QtUiScaleProxyStyle &QtUiScaleApplicationController::style() const noexcept {
  return *impl_->style;
}

bool QtUiScaleApplicationController::apply_factor(float factor) {
  Q_ASSERT(QThread::currentThread() == impl_->application.thread());
  const QtUiScaleMetrics requested(factor, impl_->metrics.revision());
  if (requested.factor() == impl_->metrics.factor()) {
    return false;
  }

  std::uint64_t revision = impl_->metrics.revision() + 1U;
  if (revision == 0U) {
    revision = 1U;
  }
  impl_->metrics = QtUiScaleMetrics(requested.factor(), revision);
  impl_->style->set_metrics(impl_->metrics);
  QApplication::setFont(impl_->metrics.font(impl_->baseline_font));
  impl_->notify_listeners();
  impl_->refresh_widget_geometry();
  return true;
}

QtUiScaleApplicationController::ListenerId
QtUiScaleApplicationController::add_change_listener(
    QObject &receiver, ChangeListener listener, bool notify_immediately) {
  Q_ASSERT(QThread::currentThread() == impl_->application.thread());
  if (!listener) {
    return 0U;
  }

  ListenerId id = impl_->next_listener_id++;
  if (id == 0U) {
    id = impl_->next_listener_id++;
  }
  impl_->listeners.push_back(
      {.id = id, .receiver = &receiver, .listener = std::move(listener)});
  if (notify_immediately) {
    impl_->listeners.back().listener(impl_->metrics);
  }
  return id;
}

void QtUiScaleApplicationController::remove_change_listener(ListenerId id) {
  Q_ASSERT(QThread::currentThread() == impl_->application.thread());
  std::erase_if(impl_->listeners,
                [id](const Impl::ListenerRecord &record) {
                  return record.id == id;
                });
}

} // namespace quader
