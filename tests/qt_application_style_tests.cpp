////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include <gtest/gtest.h>

#include "ui/qt_application_style.hpp"
#include "ui/qt_application_theme.hpp"
#include "ui/qt_dock_title_presentation.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAction>
#include <QAbstractButton>
#include <QApplication>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QMenu>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTransform>
#include <QTreeWidget>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

namespace {

class QtApplicationStyleTests : public ::testing::Test {
protected:
  void SetUp() override {
    if (QCoreApplication::instance() != nullptr) {
      GTEST_SKIP() << "This test owns the QApplication style lifecycle";
    }
    application_ = std::make_unique<QApplication>(argc_, argv_.data());
    QApplication::setPalette(quader::qt_obsidian_application_palette(
        QApplication::style()->standardPalette()));
  }

  int argc_ = 1;
  char application_name_[24] = "quader_style_test";
  std::array<char *, 2> argv_{application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

class InspectableMenu final : public QMenu {
public:
  [[nodiscard]] QStyleOptionMenuItem action_option(QAction *action) const {
    QStyleOptionMenuItem option;
    initStyleOption(&option, action);
    option.rect = actionGeometry(action);
    return option;
  }
};

class InspectableDock final : public QDockWidget {
public:
  using QDockWidget::QDockWidget;

  [[nodiscard]] QStyleOptionDockWidget title_option() const {
    QStyleOptionDockWidget option;
    initStyleOption(&option);
    return option;
  }
};

class InspectableTabBar final : public QTabBar {
public:
  [[nodiscard]] QStyleOptionTab tab_option(int index) const {
    QStyleOptionTab option;
    initStyleOption(&option, index);
    return option;
  }
};

QImage render_primitive(const QStyle &style, QStyle::PrimitiveElement element,
                        const QStyleOption &option,
                        const QWidget *widget = nullptr) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  style.drawPrimitive(element, &option, &painter, widget);
  return image;
}

QImage render_complex(const QStyle &style, QStyle::ComplexControl control,
                      const QStyleOptionComplex &option) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  style.drawComplexControl(control, &option, &painter);
  return image;
}

QImage render_button(const QStyle &style, const QStyleOptionButton &option,
                      const QPushButton &button) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  style.drawControl(QStyle::CE_PushButton, &option, &painter, &button);
  return image;
}

QImage render_menu_item(const QStyle &style,
                         const QStyleOptionMenuItem &option,
                         const QMenu &menu) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(option.palette.color(QPalette::AlternateBase));
  QPainter painter(&image);
  painter.setFont(option.font);
  style.drawControl(QStyle::CE_MenuItem, &option, &painter, &menu);
  return image;
}

QImage render_dock_title(const QStyle &style,
                         const QStyleOptionDockWidget &option,
                         const QDockWidget &dock) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setFont(dock.font());
  painter.translate(-option.rect.topLeft());
  style.drawControl(QStyle::CE_DockWidgetTitle, &option, &painter, &dock);
  return image;
}

QImage render_tab_shape(const QStyle &style, const QStyleOptionTab &option,
                        const QTabBar &tabs) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.translate(-option.rect.topLeft());
  style.drawControl(QStyle::CE_TabBarTabShape, &option, &painter, &tabs);
  return image;
}

QImage render_tab_label(const QStyle &style, const QStyleOptionTab &option,
                        const QTabBar &tabs) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  painter.setFont(tabs.font());
  painter.translate(-option.rect.topLeft());
  style.drawControl(QStyle::CE_TabBarTabLabel, &option, &painter, &tabs);
  return image;
}

QRect changed_pixels(const QImage &first, const QImage &second) {
  QRect result;
  for (int y = 0; y < first.height(); ++y) {
    for (int x = 0; x < first.width(); ++x) {
      if (first.pixel(x, y) != second.pixel(x, y)) {
        result = result.united(QRect(x, y, 1, 1));
      }
    }
  }
  return result;
}

bool contains_color(const QImage &image, const QColor &color) {
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      if (image.pixelColor(x, y) == color) {
        return true;
      }
    }
  }
  return false;
}

QIcon menu_state_icon() {
  QPixmap normal(16, 16);
  normal.fill(QColor(225, 95, 75));
  QPixmap active(16, 16);
  active.fill(QColor(75, 205, 135));
  QIcon icon;
  icon.addPixmap(normal, QIcon::Normal);
  icon.addPixmap(active, QIcon::Active);
  return icon;
}

void send_menu_key(QMenu &menu, int key) {
  QKeyEvent event(QEvent::KeyPress, key, Qt::NoModifier);
  QApplication::sendEvent(&menu, &event);
  QCoreApplication::processEvents();
}

double relative_luminance(const QColor &color) {
  const auto channel = [](double encoded) {
    return encoded <= 0.04045 ? encoded / 12.92
                              : std::pow((encoded + 0.055) / 1.055, 2.4);
  };
  return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) +
         0.0722 * channel(color.blueF());
}

double contrast_ratio(const QColor &first, const QColor &second) {
  const double first_luminance = relative_luminance(first);
  const double second_luminance = relative_luminance(second);
  return (std::max(first_luminance, second_luminance) + 0.05) /
         (std::min(first_luminance, second_luminance) + 0.05);
}

constexpr auto kEnabledState = QStyle::State_Enabled | QStyle::State_Active;

} // namespace

TEST_F(QtApplicationStyleTests,
       HighContrastLeavesStockControlPaintingUnchanged) {
  QApplication::setPalette(QPalette{});
  quader::QtUiScaleProxyStyle style;
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  QStyleOptionButton button;
  button.rect = QRect(0, 0, 96, 28);
  button.palette = QApplication::palette();
  button.state = kEnabledState | QStyle::State_HasFocus | QStyle::State_MouseOver;
  EXPECT_FALSE(quader::qt_application_style_active(&button));
  for (const auto element : {QStyle::PE_PanelButtonCommand,
                             QStyle::PE_IndicatorCheckBox,
                             QStyle::PE_IndicatorRadioButton,
                             QStyle::PE_IndicatorTabClose}) {
    EXPECT_EQ(render_primitive(style, element, button),
                render_primitive(*fusion, element, button));
  }

  QStyleOptionComboBox combo;
  combo.rect = button.rect;
  combo.palette = button.palette;
  combo.state = button.state;
  combo.direction = Qt::RightToLeft;
  combo.subControls = QStyle::SC_All;
  EXPECT_EQ(render_complex(style, QStyle::CC_ComboBox, combo),
              render_complex(*fusion, QStyle::CC_ComboBox, combo));

  QStyleOptionSpinBox spin;
  spin.rect = button.rect;
  spin.palette = button.palette;
  spin.state = button.state;
  spin.subControls = QStyle::SC_All;
  spin.stepEnabled = QAbstractSpinBox::StepUpEnabled;
  EXPECT_EQ(render_complex(style, QStyle::CC_SpinBox, spin),
              render_complex(*fusion, QStyle::CC_SpinBox, spin));
}

TEST_F(QtApplicationStyleTests,
       NativeTabCloseButtonsKeepPaletteStatesDispatchAndScaledLayout) {
  quader::apply_qt_obsidian_application_theme(
      QApplication::style()->standardPalette());
  quader::QtUiScaleApplicationController controller(*application_);
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  InspectableTabBar tabs;
  tabs.setProperty(quader::kQtConnectedTabSurfaceProperty, true);
  tabs.setAttribute(Qt::WA_DontShowOnScreen, true);
  tabs.setDocumentMode(true);
  tabs.setTabsClosable(true);
  tabs.setExpanding(false);
  tabs.setElideMode(Qt::ElideRight);
  tabs.addTab(QStringLiteral("test_scene.qdr"));
  tabs.addTab(QStringLiteral("Untitled*"));
  tabs.setCurrentIndex(0);
  std::vector<int> close_requests;
  QObject::connect(&tabs, &QTabBar::tabCloseRequested, &tabs,
                   [&close_requests](int index) { close_requests.push_back(index); });
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const auto tab_close_button = [&tabs](int index) {
    auto *button = qobject_cast<QAbstractButton *>(
        tabs.tabButton(index, QTabBar::RightSide));
    return button != nullptr ? button : qobject_cast<QAbstractButton *>(
        tabs.tabButton(index, QTabBar::LeftSide));
  };
  const auto contains_glyph_color = [](const QImage &image, const QColor &color) {
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const QColor pixel = image.pixelColor(x, y);
        // Small antialiased strokes need not contain an opaque source pixel.
        // Unpremultiplication can round partially covered RGB channels.
        if (pixel.alpha() >= 64 && std::abs(pixel.red() - color.red()) <= 3 &&
            std::abs(pixel.green() - color.green()) <= 3 &&
            std::abs(pixel.blue() - color.blue()) <= 3) {
          return true;
        }
      }
    }
    return false;
  };
  const auto visible_glyph_bounds = [](const QImage &image) {
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        // Ignore faint coverage outside the round stroke; that fringe is not
        // the visible glyph extent and varies with subpixel placement.
        if (image.pixelColor(x, y).alpha() >= 64) {
          bounds = bounds.united(QRect(x, y, 1, 1));
        }
      }
    }
    return bounds;
  };
  const auto composite = [](const QColor &pixel, const QColor &background) {
    const qreal coverage = pixel.alphaF();
    return QColor::fromRgbF(
        pixel.redF() * coverage + background.redF() * (1.0 - coverage),
        pixel.greenF() * coverage + background.greenF() * (1.0 - coverage),
        pixel.blueF() * coverage + background.blueF() * (1.0 - coverage));
  };
  int scale_pass = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    const auto &metrics = controller.metrics();
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      SCOPED_TRACE(::testing::Message() << factor << ' ' << direction);
      tabs.setLayoutDirection(direction);
      // Native tab-button placement refreshes on style changes, not direction
      // changes. Settle the fixture before inspecting its mirrored layout.
      QEvent style_change(QEvent::StyleChange);
      QApplication::sendEvent(&tabs, &style_change);
      QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QCoreApplication::processEvents();
      tabs.resize(std::max(metrics.px(300), tabs.sizeHint().width()),
                  tabs.sizeHint().height());
      tabs.show();
      QCoreApplication::processEvents();
      for (const int index : {0, 1}) {
        SCOPED_TRACE(index);
        QAbstractButton *button = tab_close_button(index);
        ASSERT_NE(button, nullptr);
        EXPECT_TRUE(tabs.rect().contains(button->geometry()));
        EXPECT_TRUE(tabs.tabRect(index).contains(button->geometry()));
        // Qt retains the created close widget's native size across font-scale
        // changes. Shrinking its glyph must not shrink that native hit target.
        EXPECT_GE(button->width(), fusion->pixelMetric(
                                      QStyle::PM_TabCloseIndicatorWidth));
        EXPECT_GE(button->height(), fusion->pixelMetric(
                                       QStyle::PM_TabCloseIndicatorHeight));
        QStyleOption glyph_option;
        glyph_option.initFrom(button);
        glyph_option.rect = button->rect();
        glyph_option.state = kEnabledState;
        const QImage glyph = render_primitive(*tabs.style(),
            QStyle::PE_IndicatorTabClose, glyph_option, button);
        QImage blank(glyph.size(), glyph.format());
        blank.fill(Qt::transparent);
        const QRect glyph_ink = changed_pixels(blank, glyph);
        ASSERT_FALSE(glyph_ink.isEmpty());
        const QRect visible_ink = visible_glyph_bounds(glyph);
        ASSERT_FALSE(visible_ink.isEmpty());
        EXPECT_LE(visible_ink.width(), metrics.px(7));
        EXPECT_LE(visible_ink.height(), metrics.px(7));
        QStyleOptionTab label_option = tabs.tab_option(index);
        label_option.state.setFlag(QStyle::State_HasFocus, false);
        const QImage label = render_tab_label(*tabs.style(), label_option, tabs);
        blank = QImage(label.size(), label.format());
        blank.fill(Qt::transparent);
        const QRect label_ink = changed_pixels(blank, label);
        ASSERT_FALSE(label_ink.isEmpty());
        const qreal glyph_center = QRectF(glyph_ink).center().y() + button->y();
        const qreal label_center = QRectF(label_ink).center().y() +
                                   label_option.rect.y();
        EXPECT_NEAR(glyph_center, label_center, 1.0);
      }
      QAbstractButton *close = tab_close_button(1);
      ASSERT_NE(close, nullptr);
      EXPECT_TRUE(close->rect().isValid());
      EXPECT_TRUE(tabs.rect().contains(close->geometry()));
      EXPECT_TRUE(tabs.tabRect(1).contains(close->geometry()));

      QStyleOption option;
      option.initFrom(close);
      option.rect = close->rect();
      option.state = kEnabledState;
      const QImage idle = render_primitive(*tabs.style(), QStyle::PE_IndicatorTabClose,
                                            option, close);
      EXPECT_EQ(idle.pixelColor(0, 0).alpha(), 0);
      EXPECT_TRUE(contains_glyph_color(idle, option.palette.color(QPalette::Active,
                                                                 QPalette::WindowText)));
      for (const auto state : {QStyle::State_MouseOver, QStyle::State_Sunken,
                                QStyle::State_Selected}) {
        option.state = kEnabledState | state;
        const QImage active = render_primitive(*tabs.style(),
                                               QStyle::PE_IndicatorTabClose, option,
                                               close);
        EXPECT_NE(idle, active);
        QStyleOption without_glyph = option;
        without_glyph.palette.setColor(QPalette::Active, QPalette::Text,
                                        Qt::transparent);
        const QImage background = render_primitive(*tabs.style(),
            QStyle::PE_IndicatorTabClose, without_glyph, close);
        EXPECT_FALSE(changed_pixels(background, active).isEmpty());
        double maximum_contrast = 1.0;
        const QColor canvas = option.palette.color(QPalette::Active,
                                                    QPalette::Window);
        for (int y = 0; y < active.height(); ++y) {
          for (int x = 0; x < active.width(); ++x) {
            maximum_contrast = std::max(maximum_contrast, contrast_ratio(
                composite(active.pixelColor(x, y), canvas),
                composite(background.pixelColor(x, y), canvas)));
          }
        }
        EXPECT_GE(maximum_contrast, 3.0);
      }
      option.state = QStyle::State_Active;
      const QImage disabled = render_primitive(*tabs.style(),
                                               QStyle::PE_IndicatorTabClose, option,
                                               close);
      EXPECT_NE(idle, disabled);
      EXPECT_TRUE(contains_glyph_color(disabled, option.palette.color(QPalette::Disabled,
                                                                     QPalette::WindowText)));

      for (const QString &state : {QStringLiteral("idle"), QStringLiteral("hover"),
                                   QStringLiteral("pressed")}) {
        close->setAttribute(Qt::WA_UnderMouse, state != QStringLiteral("idle"));
        close->setDown(state == QStringLiteral("pressed"));
        if (!review_directory.isEmpty()) {
          EXPECT_TRUE(tabs.grab().save(QDir(review_directory).filePath(
              QStringLiteral("scene-tabs-%1-%2-%3-%4.png")
                  .arg(scale_pass).arg(qRound(factor * 100.0F))
                  .arg(direction == Qt::LeftToRight ? QStringLiteral("ltr")
                                                    : QStringLiteral("rtl"))
                  .arg(state))));
        }
      }
      close->setDown(false);
      close->setAttribute(Qt::WA_UnderMouse, false);
      close_requests.clear();
      close->setEnabled(false);
      close->click();
      EXPECT_TRUE(close_requests.empty());
      close->setEnabled(true);
      close->click();
      EXPECT_EQ(close_requests, (std::vector<int>{1}));
      EXPECT_EQ(tabs.currentIndex(), 0);
      EXPECT_EQ(tabs.count(), 2);
    }
    ++scale_pass;
  }

  QPalette light_palette = fusion->standardPalette();
  light_palette.setColor(QPalette::Window, QColor(245, 245, 245));
  light_palette.setColor(QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::Text, Qt::black);
  tabs.setPalette(light_palette);
  QStyleOption light;
  light.initFrom(&tabs);
  light.rect = QRect(0, 0, 20, 20);
  light.state = kEnabledState | QStyle::State_MouseOver;
  EXPECT_FALSE(quader::qt_application_style_active(&light));
  EXPECT_EQ(render_primitive(controller.style(), QStyle::PE_IndicatorTabClose, light),
            render_primitive(*fusion, QStyle::PE_IndicatorTabClose, light));
}

TEST_F(QtApplicationStyleTests,
       ButtonsDistinguishRestHoverPressCheckedDisabledAndFocus) {
  quader::QtUiScaleProxyStyle style;
  QStyleOptionButton option;
  option.rect = QRect(0, 0, 96, 28);
  option.palette = QApplication::palette();
  const std::array<QStyle::State, 6> states{
      kEnabledState, kEnabledState | QStyle::State_MouseOver,
      kEnabledState | QStyle::State_Sunken, kEnabledState | QStyle::State_On,
      QStyle::State_Active, kEnabledState | QStyle::State_HasFocus};
  std::vector<QImage> images;
  for (const auto state : states) {
    option.state = state;
    images.push_back(render_primitive(style, QStyle::PE_PanelButtonCommand,
                                       option));
  }
  for (std::size_t first = 0; first < images.size(); ++first) {
    for (std::size_t second = first + 1; second < images.size(); ++second) {
      EXPECT_NE(images[first], images[second])
          << "states " << first << " and " << second;
    }
  }
}

TEST_F(QtApplicationStyleTests,
       ToggleToolButtonsKeepPointerFocusQuietAndKeyboardFocusVisible) {
  quader::QtUiScaleProxyStyle style;
  QToolBar toolbar;
  QWidget pane_strip;
  QToolButton toolbar_button(&toolbar);
  QToolButton pane_button(&pane_strip);
  for (QToolButton *button : {&toolbar_button, &pane_button}) {
    button->setCheckable(true);
    button->setAutoRaise(false);
    button->setFocusPolicy(Qt::StrongFocus);
  }
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    style.set_metrics(metrics);
    for (QToolButton *button : {&toolbar_button, &pane_button}) {
      SCOPED_TRACE(::testing::Message()
                   << "scale=" << factor << " toolbar="
                   << (button == &toolbar_button));
      const auto render = [&](QStyle::State state) {
        QStyleOptionToolButton option;
        option.initFrom(button);
        option.rect = QRect(QPoint{}, metrics.size(QSize(28, 28)));
        option.state = state;
        option.subControls = QStyle::SC_ToolButton;
        QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        style.drawComplexControl(QStyle::CC_ToolButton, &option, &painter,
                                 button);
        return image;
      };
      const QStyle::State idle = kEnabledState | QStyle::State_Raised;
      for (const QStyle::State interaction :
           {QStyle::State{}, QStyle::State(QStyle::State_MouseOver),
            QStyle::State(QStyle::State_On)}) {
        EXPECT_EQ(render(idle | interaction),
                  render(idle | interaction | QStyle::State_HasFocus));
      }
      EXPECT_NE(render(idle),
                render(idle | QStyle::State_HasFocus |
                       QStyle::State_KeyboardFocusChange));
      EXPECT_NE(render(idle), render(idle | QStyle::State_On));
      EXPECT_EQ(button->focusPolicy(), Qt::StrongFocus);
    }
  }
}

TEST_F(QtApplicationStyleTests,
       NativeToolbarDividersKeepVisibleSpacingAndOnePhysicalPixelWidth) {
  QMainWindow host;
  host.setAttribute(Qt::WA_DontShowOnScreen, true);
  QToolBar toolbar(&host);
  toolbar.setMovable(false);
  QAction *before = toolbar.addAction(QStringLiteral("Before"));
  QAction *divider = toolbar.addSeparator();
  QAction *after = toolbar.addAction(QStringLiteral("After"));
  QStyle &presentation =
      quader::install_native_toolbar_button_presentation(toolbar);
  for (QAction *action : {before, after}) {
    quader::configure_native_toolbar_button_face(toolbar, *action, presentation);
  }
  QAction *late_divider = toolbar.addSeparator();
  QAction *last = toolbar.addAction(QStringLiteral("Later"));
  quader::configure_native_toolbar_button_face(toolbar, *last, presentation);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const auto review = [&](const quader::QtUiScaleMetrics &metrics,
                          Qt::Orientation orientation,
                          Qt::LayoutDirection direction,
                          const QString &palette_name, int scale_pass) {
    SCOPED_TRACE(::testing::Message()
                 << metrics.factor() << ' ' << orientation << ' ' << direction
                 << ' ' << palette_name.toStdString());
    host.removeToolBar(&toolbar);
    host.addToolBar(orientation == Qt::Horizontal ? Qt::TopToolBarArea
                                                : Qt::LeftToolBarArea,
                    &toolbar);
    toolbar.setLayoutDirection(direction);
    quader::apply_native_toolbar_presentation_ui_scale(toolbar, metrics);
    for (QAction *action : {before, after, last}) {
      quader::apply_native_toolbar_button_face_ui_scale(toolbar, *action, metrics);
    }
    host.resize(metrics.size(orientation == Qt::Horizontal ? QSize(280, 100)
                                                           : QSize(100, 280)));
    host.show();
    toolbar.show();
    for (int pass = 0; pass != 3; ++pass) {
      QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QApplication::processEvents();
      toolbar.layout()->activate();
      host.layout()->activate();
    }
    QWidget *separator_widget = toolbar.widgetForAction(divider);
    QWidget *late_separator_widget = toolbar.widgetForAction(late_divider);
    ASSERT_NE(separator_widget, nullptr);
    ASSERT_NE(late_separator_widget, nullptr);
    EXPECT_EQ(separator_widget->style(), &presentation);
    EXPECT_EQ(late_separator_widget->style(), &presentation);
    ASSERT_TRUE(separator_widget->isVisible());
    EXPECT_FALSE(separator_widget->geometry().intersects(toolbar.actionGeometry(before)));
    EXPECT_FALSE(separator_widget->geometry().intersects(toolbar.actionGeometry(after)));
    const QImage image = separator_widget->grab().toImage();
    ASSERT_FALSE(image.isNull());
    QStyleOption option;
    option.initFrom(separator_widget);
    option.rect = separator_widget->rect();
    option.state.setFlag(QStyle::State_Horizontal, orientation == Qt::Horizontal);
    EXPECT_EQ(quader::qt_application_style_active(&option),
              palette_name == QStringLiteral("themed"));
    if (quader::qt_application_style_active(&option)) {
      const int extent = orientation == Qt::Horizontal ? separator_widget->width()
                                                      : separator_widget->height();
      EXPECT_EQ(extent, metrics.px(12));
      EXPECT_EQ(orientation == Qt::Horizontal ? late_separator_widget->width()
                                             : late_separator_widget->height(),
                metrics.px(12));
      const QPalette::ColorGroup group =
          quader::qt_toolbar_button_color_group(option.state);
      const QColor line_color = option.palette.color(group, QPalette::Midlight);
      EXPECT_GE(contrast_ratio(line_color,
                               option.palette.color(group, QPalette::Window)), 1.5);
      QRect line_bounds;
      for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
          if (image.pixelColor(x, y) == line_color) {
            line_bounds |= QRect(x, y, 1, 1);
          }
        }
      }
      ASSERT_FALSE(line_bounds.isEmpty());
      EXPECT_TRUE(contains_color(late_separator_widget->grab().toImage(),
                                 line_color));
      EXPECT_EQ(orientation == Qt::Horizontal ? line_bounds.width()
                                             : line_bounds.height(), 1);
      const int line_length = orientation == Qt::Horizontal ? line_bounds.height()
                                                           : line_bounds.width();
      EXPECT_GE(line_length, qRound(metrics.px(24.0) * image.devicePixelRatioF()));
      EXPECT_TRUE(image.rect().adjusted(1, 1, -1, -1).contains(line_bounds));
      toolbar.setEnabled(false);
      QApplication::processEvents();
      EXPECT_TRUE(contains_color(separator_widget->grab().toImage(),
          option.palette.color(QPalette::Disabled, QPalette::Midlight)));
      toolbar.setEnabled(true);
    } else {
      std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
      ASSERT_NE(fusion, nullptr);
      EXPECT_EQ(render_primitive(presentation, QStyle::PE_IndicatorToolBarSeparator,
                                 option),
                render_primitive(*fusion, QStyle::PE_IndicatorToolBarSeparator,
                                 option));
    }
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(toolbar.grab().save(QDir(review_directory).filePath(
          QStringLiteral("toolbar-dividers-%1-%2-%3-%4-%5.png")
              .arg(palette_name)
              .arg(orientation == Qt::Horizontal ? QStringLiteral("horizontal")
                                                 : QStringLiteral("vertical"))
              .arg(direction == Qt::LeftToRight ? QStringLiteral("ltr")
                                                : QStringLiteral("rtl"))
              .arg(qRound(metrics.factor() * 100.0F)).arg(scale_pass))));
    }
  };
  int scale_pass = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    const quader::QtUiScaleMetrics metrics(factor,
        static_cast<std::uint64_t>(scale_pass));
    for (const auto orientation : {Qt::Horizontal, Qt::Vertical}) {
      for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        ASSERT_NO_FATAL_FAILURE(review(metrics, orientation, direction,
                                       QStringLiteral("themed"), scale_pass));
      }
    }
    ++scale_pass;
  }
  QApplication::setPalette(QPalette{});
  scale_pass = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    const quader::QtUiScaleMetrics metrics(factor,
        static_cast<std::uint64_t>(scale_pass));
    for (const auto orientation : {Qt::Horizontal, Qt::Vertical}) {
      for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        ASSERT_NO_FATAL_FAILURE(review(metrics, orientation, direction,
                                       QStringLiteral("high-contrast"), scale_pass));
      }
    }
    ++scale_pass;
  }
  QApplication::setPalette(quader::qt_obsidian_application_palette(
      QApplication::style()->standardPalette()));
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  QPalette light_palette = fusion->standardPalette();
  light_palette.setColor(QPalette::Window, Qt::white);
  light_palette.setColor(QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::Text, Qt::black);
  light_palette.setColor(QPalette::Button, Qt::white);
  light_palette.setColor(QPalette::ButtonText, Qt::black);
  toolbar.setPalette(light_palette);
  for (const auto orientation : {Qt::Horizontal, Qt::Vertical}) {
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      ASSERT_NO_FATAL_FAILURE(review(quader::QtUiScaleMetrics{}, orientation,
                                     direction, QStringLiteral("light"), 0));
    }
  }
}

TEST_F(QtApplicationStyleTests, CheckboxesKeepMixedAndDisabledStatesVisible) {
  quader::QtUiScaleProxyStyle style;
  QStyleOptionButton option;
  option.rect = QRect(0, 0, 18, 18);
  option.palette = QApplication::palette();
  option.state = kEnabledState;
  const QImage unchecked =
      render_primitive(style, QStyle::PE_IndicatorCheckBox, option);
  option.state |= QStyle::State_On;
  const QImage checked =
      render_primitive(style, QStyle::PE_IndicatorCheckBox, option);
  option.state = kEnabledState | QStyle::State_NoChange;
  const QImage mixed =
      render_primitive(style, QStyle::PE_IndicatorCheckBox, option);
  option.state = QStyle::State_Active | QStyle::State_On;
  const QImage disabled =
      render_primitive(style, QStyle::PE_IndicatorCheckBox, option);
  EXPECT_NE(unchecked, checked);
  EXPECT_NE(checked, mixed);
  EXPECT_NE(unchecked, mixed);
  EXPECT_NE(disabled, checked);
  EXPECT_GT(disabled.pixelColor(option.rect.center()).alpha(), 0);
}

TEST_F(QtApplicationStyleTests,
       SpinAndComboSubcontrolsKeepScaledRtlHitTargets) {
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      QStyleOptionSpinBox spin;
      spin.rect = QRect(QPoint{}, metrics.size(QSize(120, 28)));
      spin.palette = QApplication::palette();
      spin.state = kEnabledState;
      spin.direction = direction;
      spin.subControls = QStyle::SC_All;
      spin.stepEnabled = QAbstractSpinBox::StepUpEnabled |
                         QAbstractSpinBox::StepDownEnabled;
      const QRect edit = style.subControlRect(QStyle::CC_SpinBox, &spin,
                                               QStyle::SC_SpinBoxEditField, nullptr);
      for (const auto control : {QStyle::SC_SpinBoxUp,
                                   QStyle::SC_SpinBoxDown}) {
        const QRect hit = style.subControlRect(QStyle::CC_SpinBox, &spin,
                                                control, nullptr);
        EXPECT_FALSE(hit.isEmpty());
        EXPECT_FALSE(edit.intersects(hit));
        EXPECT_EQ(style.hitTestComplexControl(QStyle::CC_SpinBox, &spin,
                                               hit.center(), nullptr), control);
        EXPECT_EQ(hit.center().x() > edit.center().x(),
                    direction == Qt::LeftToRight);
      }
      QStyleOptionComboBox combo;
      combo.rect = spin.rect;
      combo.palette = spin.palette;
      combo.state = spin.state;
      combo.direction = direction;
      combo.subControls = QStyle::SC_All;
      const QRect arrow = style.subControlRect(QStyle::CC_ComboBox, &combo,
                                                QStyle::SC_ComboBoxArrow, nullptr);
      EXPECT_EQ(style.hitTestComplexControl(QStyle::CC_ComboBox, &combo,
                                             arrow.center(), nullptr),
                  QStyle::SC_ComboBoxArrow);
      EXPECT_EQ(arrow.center().x() > combo.rect.center().x(),
                  direction == Qt::LeftToRight);
    }
  }
}

TEST_F(QtApplicationStyleTests, SpinButtonsPreserveGlyphsAndStepAvailability) {
  quader::QtUiScaleProxyStyle style;
  QStyleOptionSpinBox spin;
  spin.rect = QRect(0, 0, 120, 28);
  spin.palette = QApplication::palette();
  spin.state = kEnabledState;
  spin.subControls = QStyle::SC_All;
  spin.stepEnabled = QAbstractSpinBox::StepUpEnabled |
                     QAbstractSpinBox::StepDownEnabled;
  const QImage arrows = render_complex(style, QStyle::CC_SpinBox, spin);
  spin.buttonSymbols = QAbstractSpinBox::PlusMinus;
  const QImage plus_minus = render_complex(style, QStyle::CC_SpinBox, spin);
  EXPECT_NE(arrows, plus_minus);
  spin.buttonSymbols = QAbstractSpinBox::UpDownArrows;
  spin.stepEnabled = QAbstractSpinBox::StepUpEnabled;
  const QImage down_disabled = render_complex(style, QStyle::CC_SpinBox, spin);
  EXPECT_NE(arrows, down_disabled);
  const QRect up = style.subControlRect(QStyle::CC_SpinBox, &spin,
                                         QStyle::SC_SpinBoxUp, nullptr);
  EXPECT_EQ(arrows.copy(up), down_disabled.copy(up));
}

TEST_F(QtApplicationStyleTests, FlatButtonsKeepNativeKeyboardFocusIndicator) {
  quader::QtUiScaleProxyStyle style;
  QToolButton disclosure;
  disclosure.setAutoRaise(true);
  QPushButton navigation;
  navigation.setFlat(true);
  QStyleOptionFocusRect focus;
  focus.rect = QRect(0, 0, 96, 24);
  focus.palette = QApplication::palette();
  focus.backgroundColor = focus.palette.color(QPalette::Window);
  focus.state = kEnabledState | QStyle::State_HasFocus |
                QStyle::State_KeyboardFocusChange;
  QImage blank(focus.rect.size(), QImage::Format_ARGB32_Premultiplied);
  blank.fill(Qt::transparent);
  for (const QWidget *widget :
       {static_cast<const QWidget *>(&disclosure),
          static_cast<const QWidget *>(&navigation)}) {
    QImage image = blank;
    {
      QPainter painter(&image);
      style.drawPrimitive(QStyle::PE_FrameFocusRect, &focus, &painter, widget);
    }
    EXPECT_NE(image, blank);
  }
}

TEST_F(QtApplicationStyleTests,
       DefaultButtonLabelStaysReadableAcrossEnabledInteractionStates) {
  quader::QtUiScaleProxyStyle style;
  QPushButton button(QStringLiteral("Apply"));
  button.setDefault(true);
  QStyleOptionButton option;
  option.rect = QRect(0, 0, 120, 32);
  option.palette = QApplication::palette();
  option.text = button.text();
  option.features = QStyleOptionButton::DefaultButton;
  for (const auto group : {QPalette::Active, QPalette::Inactive}) {
    option.palette.setCurrentColorGroup(group);
    const QStyle::State enabled_state = QStyle::State_Enabled |
        QStyle::State_Raised |
        (group == QPalette::Active ? QStyle::State_Active : QStyle::State_None);
    for (const QStyle::State interaction :
         {QStyle::State(QStyle::State_None),
            QStyle::State(QStyle::State_MouseOver),
            QStyle::State(QStyle::State_Sunken),
            QStyle::State(QStyle::State_On),
            QStyle::State(QStyle::State_HasFocus |
                          QStyle::State_KeyboardFocusChange)}) {
      option.state = enabled_state | interaction;
      const QImage rendered = render_button(style, option, button);
      const QColor surface = rendered.pixelColor(10, option.rect.center().y());
      EXPECT_EQ(surface.alpha(), 255);
      EXPECT_GE(contrast_ratio(option.palette.color(group, QPalette::ButtonText),
                                 surface), 4.5)
          << "group=" << group << " state=" << static_cast<int>(option.state);
    }
  }
}

TEST_F(QtApplicationStyleTests,
       DefaultButtonKeyboardFocusDiffersFromItsRestingAccent) {
  quader::QtUiScaleProxyStyle style;
  QPushButton button(QStringLiteral("Apply"));
  button.setDefault(true);
  QStyleOptionButton option;
  option.rect = QRect(0, 0, 120, 32);
  option.palette = QApplication::palette();
  option.features = QStyleOptionButton::DefaultButton;
  option.text = button.text();
  option.state = kEnabledState | QStyle::State_Raised;
  const QImage resting = render_button(style, option, button);
  option.state |= QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
  const QImage focused = render_button(style, option, button);
  EXPECT_NE(resting, focused);
}

TEST_F(QtApplicationStyleTests,
       FlatButtonKeyboardFocusIsVisibleThroughTheCompleteNativeDraw) {
  quader::QtUiScaleProxyStyle style;
  QPushButton button(QStringLiteral("Recent projects"));
  button.setFlat(true);
  QStyleOptionButton option;
  option.rect = QRect(0, 0, 160, 32);
  option.palette = QApplication::palette();
  option.features = QStyleOptionButton::Flat;
  option.text = button.text();
  option.state = kEnabledState;
  const QImage resting = render_button(style, option, button);
  option.state |= QStyle::State_HasFocus | QStyle::State_KeyboardFocusChange;
  const QImage focused = render_button(style, option, button);
  EXPECT_NE(resting, focused);
}

TEST_F(QtApplicationStyleTests,
       MenuGlyphsChecksAndSubmenuArrowsRemainVisibleAcrossScaleAndDirection) {
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    QMenu menu;
    menu.setStyle(&style);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      SCOPED_TRACE(::testing::Message() << "scale=" << factor
                                       << " direction=" << direction);
      QStyleOptionMenuItem option;
      option.rect = QRect(QPoint{}, metrics.size(QSize(300, 28)));
      option.palette = QApplication::palette();
      option.font = metrics.font(QApplication::font());
      option.fontMetrics = QFontMetrics(option.font);
      option.direction = direction;
      option.state = kEnabledState;
      option.menuItemType = QStyleOptionMenuItem::Normal;
      option.maxIconWidth = metrics.px(16);
      option.menuHasCheckableItems = true;
      const QImage blank = render_menu_item(style, option, menu);

      option.menuItemType = QStyleOptionMenuItem::SubMenu;
      const QImage submenu = render_menu_item(style, option, menu);
      const QRect arrow = changed_pixels(blank, submenu);
      ASSERT_FALSE(arrow.isEmpty());
      EXPECT_EQ(arrow.center().x() > option.rect.center().x(),
                  direction == Qt::LeftToRight);

      option.menuItemType = QStyleOptionMenuItem::Normal;
      option.checkType = QStyleOptionMenuItem::NonExclusive;
      const QImage unchecked = render_menu_item(style, option, menu);
      option.checked = true;
      const QImage checked = render_menu_item(style, option, menu);
      const QRect check = changed_pixels(unchecked, checked);
      ASSERT_FALSE(check.isEmpty());
      EXPECT_EQ(check.center().x() < option.rect.center().x(),
                  direction == Qt::LeftToRight);
      option.checkType = QStyleOptionMenuItem::Exclusive;
      EXPECT_NE(checked, render_menu_item(style, option, menu));

      option.checkType = QStyleOptionMenuItem::NotCheckable;
      option.checked = false;
      option.icon = menu_state_icon();
      const QImage icon = render_menu_item(style, option, menu);
      EXPECT_TRUE(contains_color(icon, QColor(225, 95, 75)));
      option.state |= QStyle::State_Selected;
      const QImage active_icon = render_menu_item(style, option, menu);
      EXPECT_TRUE(contains_color(active_icon, QColor(75, 205, 135)));
      EXPECT_FALSE(contains_color(active_icon, QColor(225, 95, 75)));
      option.state = QStyle::State_Active;
      option.palette.setCurrentColorGroup(QPalette::Disabled);
      EXPECT_NE(icon, render_menu_item(style, option, menu));
    }
  }
}

TEST_F(QtApplicationStyleTests,
       MenuShortcutsStaySecondaryAndSelectionKeepsRoundedInset) {
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    QMenu menu;
    menu.setStyle(&style);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      QStyleOptionMenuItem option;
      option.rect = QRect(QPoint{}, metrics.size(QSize(320, 28)));
      option.palette = QApplication::palette();
      option.font = metrics.font(QApplication::font());
      option.fontMetrics = QFontMetrics(option.font);
      option.direction = direction;
      option.state = kEnabledState;
      option.menuItemType = QStyleOptionMenuItem::Normal;
      option.reservedShortcutWidth =
          option.fontMetrics.horizontalAdvance(QStringLiteral("Ctrl+D"));
      const QImage blank = render_menu_item(style, option, menu);
      option.text = QStringLiteral("Duplicate Objects");
      const QImage label = render_menu_item(style, option, menu);
      option.text += QStringLiteral("\tCtrl+D");
      const QImage complete = render_menu_item(style, option, menu);
      const QRect shortcut = changed_pixels(label, complete);
      ASSERT_FALSE(shortcut.isEmpty());
      EXPECT_EQ(shortcut.center().x() > option.rect.center().x(),
                  direction == Qt::LeftToRight);

      double label_luminance = 0.0;
      double shortcut_luminance = 0.0;
      for (int y = 0; y < complete.height(); ++y) {
        for (int x = 0; x < complete.width(); ++x) {
          if (label.pixel(x, y) != blank.pixel(x, y)) {
            label_luminance = std::max(
                label_luminance, relative_luminance(label.pixelColor(x, y)));
          }
          if (complete.pixel(x, y) != label.pixel(x, y)) {
            shortcut_luminance = std::max(shortcut_luminance,
                relative_luminance(complete.pixelColor(x, y)));
          }
        }
      }
      EXPECT_GT(shortcut_luminance,
                  relative_luminance(option.palette.color(QPalette::AlternateBase)));
      EXPECT_LT(shortcut_luminance, label_luminance);

      option.text.clear();
      option.state |= QStyle::State_Selected;
      const QImage selected = render_menu_item(style, option, menu);
      EXPECT_NE(selected.pixelColor(option.rect.center()),
                  blank.pixelColor(option.rect.center()));
      EXPECT_EQ(selected.pixelColor(0, 0), blank.pixelColor(0, 0));
    }
  }
}

TEST_F(QtApplicationStyleTests,
       RealMenusScaleAndKeepNativeKeyboardNavigationAndActivation) {
  quader::apply_qt_obsidian_application_theme(
      QApplication::style()->standardPalette());
  for (const float factor : {1.0F, 2.0F}) {
    quader::QtUiScaleApplicationController controller(*application_, factor);
    const auto &metrics = controller.metrics();
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      SCOPED_TRACE(::testing::Message() << "scale=" << factor
                                       << " direction=" << direction);
      QMenu menu;
      menu.setLayoutDirection(direction);
      menu.setAttribute(Qt::WA_DontShowOnScreen, true);
      QAction *first = menu.addAction(QStringLiteral("&Duplicate Objects\tCtrl+D"));
      QAction *checked = menu.addAction(QStringLiteral("Texture Lock"));
      checked->setCheckable(true);
      checked->setChecked(true);
      QAction *disabled = menu.addAction(QStringLiteral("Unavailable operation"));
      disabled->setEnabled(false);
      QAction *separator = menu.addSeparator();
      QAction *icon = menu.addAction(menu_state_icon(), QStringLiteral("Create Instance"));
      QMenu *submenu = menu.addMenu(QStringLiteral("Align"));
      submenu->addAction(QStringLiteral("Along X"));
      menu.addSeparator();
      QAction *last = menu.addAction(QStringLiteral("&Copy\tCtrl+C"));
      menu.popup(QPoint(20, 20));
      QCoreApplication::processEvents();
      ASSERT_TRUE(menu.isVisible());
      const QRect first_bounds = menu.actionGeometry(first);
      EXPECT_GE(first_bounds.height(), metrics.px(28));
      EXPECT_GT(first_bounds.height(), menu.fontMetrics().height());
      EXPECT_GE(first_bounds.left(), metrics.px(4));
      EXPECT_GE(first_bounds.top(), metrics.px(4));
      QStyleOption menu_option;
      menu_option.initFrom(&menu);
      for (const auto metric : {QStyle::PM_MenuHMargin, QStyle::PM_MenuVMargin}) {
        EXPECT_EQ(menu.style()->pixelMetric(metric, nullptr, &menu),
                  menu.style()->pixelMetric(metric, &menu_option, &menu));
      }
      EXPECT_LT(menu.actionGeometry(separator).height(), first_bounds.height());
      EXPECT_GE(menu.width(), menu.fontMetrics().horizontalAdvance(
                                   QStringLiteral("Duplicate ObjectsCtrl+D")));

      menu.setActiveAction(first);
      send_menu_key(menu, Qt::Key_Down);
      EXPECT_EQ(menu.activeAction(), checked);
      send_menu_key(menu, Qt::Key_Down);
      EXPECT_EQ(menu.activeAction(), icon);
      send_menu_key(menu, Qt::Key_Down);
      EXPECT_EQ(menu.activeAction(), submenu->menuAction());
      send_menu_key(menu, direction == Qt::LeftToRight ? Qt::Key_Right
                                                     : Qt::Key_Left);
      EXPECT_TRUE(submenu->isVisible());
      submenu->close();
      menu.setActiveAction(first);

      const QString review_directory =
          qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
      if (!review_directory.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(review_directory));
        QImage image(menu.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        menu.render(&image);
        const QString filename = QStringLiteral("menu-controls-%1-%2.png")
                                     .arg(direction == Qt::LeftToRight
                                              ? QStringLiteral("ltr")
                                              : QStringLiteral("rtl"))
                                     .arg(qRound(factor * 100.0F));
        EXPECT_TRUE(image.save(QDir(review_directory).filePath(filename)));
      }

      bool triggered = false;
      QObject::connect(last, &QAction::triggered, &menu,
                         [&triggered]() { triggered = true; });
      menu.setActiveAction(last);
      send_menu_key(menu, Qt::Key_Return);
      EXPECT_TRUE(triggered);
      EXPECT_FALSE(menu.isVisible());
    }
  }
}

TEST_F(QtApplicationStyleTests,
       LongMenuLabelsKeepTheirCompleteNativeRasterBesideShortcutColumns) {
  quader::apply_qt_obsidian_application_theme(
      QApplication::style()->standardPalette());
  quader::QtUiScaleApplicationController controller(*application_);
  InspectableMenu menu;
  menu.setAttribute(Qt::WA_DontShowOnScreen, true);
  menu.addAction(QStringLiteral("Undo\tCtrl+Z"));
  menu.addAction(QStringLiteral("Redo\tCtrl+Shift+Z"));
  menu.addMenu(QStringLiteral("Undo History"))->addAction(QStringLiteral("Create Box"));
  menu.addSeparator();
  QAction *long_label = menu.addAction(QStringLiteral("Find and Replace Materials"));
  long_label->setEnabled(false);
  menu.addAction(QStringLiteral("Preferences"));
  struct ScaleCase final {
    float factor;
    const char *suffix;
  };
  constexpr std::array<ScaleCase, 3> kScales{{
      {1.0F, "100"}, {2.0F, "200"}, {1.0F, "scale-down"},
  }};
  for (const ScaleCase &scale : kScales) {
    (void)controller.apply_factor(scale.factor);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      SCOPED_TRACE(::testing::Message() << "scale=" << scale.suffix
                                       << " direction=" << direction);
      menu.setLayoutDirection(direction);
      menu.popup(QPoint(20, 20));
      QCoreApplication::processEvents();
      ASSERT_TRUE(menu.isVisible());
      QStyleOptionMenuItem option = menu.action_option(long_label);
      option.rect = QRect(QPoint{}, option.rect.size());
      ASSERT_GT(option.reservedShortcutWidth, 0);
      const QImage actual = render_menu_item(*menu.style(), option, menu);
      QStyleOptionMenuItem blank_option(option);
      blank_option.text.clear();
      const QImage blank = render_menu_item(*menu.style(), blank_option, menu);
      const QRect actual_ink = changed_pixels(blank, actual);
      ASSERT_FALSE(actual_ink.isEmpty());

      // Extra space guarantees an unelided native reference without duplicating
      // Fusion's private text rectangle, check gutter or shortcut geometry.
      option.rect.setWidth(option.rect.width() +
                           option.fontMetrics.horizontalAdvance(option.text));
      const QImage roomy = render_menu_item(*menu.style(), option, menu);
      blank_option = option;
      blank_option.text.clear();
      const QImage roomy_blank = render_menu_item(*menu.style(), blank_option, menu);
      const QRect full_ink = changed_pixels(roomy_blank, roomy);
      ASSERT_FALSE(full_ink.isEmpty());
      EXPECT_EQ(actual.copy(actual_ink), roomy.copy(full_ink));

      const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
      if (!directory.isEmpty()) {
        ASSERT_TRUE(QDir().mkpath(directory));
        QImage image(menu.size(), QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        menu.render(&image);
        const QString filename = QStringLiteral("menu-long-label-%1-%2.png")
            .arg(direction == Qt::LeftToRight ? QStringLiteral("ltr")
                                            : QStringLiteral("rtl"))
            .arg(QString::fromLatin1(scale.suffix));
        EXPECT_TRUE(image.save(QDir(directory).filePath(filename)));
      }
      menu.close();
      QCoreApplication::processEvents();
    }
  }
}

TEST_F(QtApplicationStyleTests,
       NativeFallbackMenuLabelsRemainUnelidedAcrossScaleAndDirection) {
  const QPalette fusion = QApplication::style()->standardPalette();
  QPalette light_palette = fusion;
  light_palette.setColor(QPalette::Window, Qt::white);
  light_palette.setColor(QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::Text, Qt::black);
  light_palette.setColor(QPalette::Button, Qt::white);
  light_palette.setColor(QPalette::ButtonText, Qt::black);
  quader::apply_qt_obsidian_application_theme(fusion);
  quader::QtUiScaleApplicationController controller(*application_);
  for (const bool high_contrast : {false, true}) {
    quader::apply_qt_application_theme_for_contrast_preference(
        fusion, high_contrast ? Qt::ContrastPreference::HighContrast
                              : Qt::ContrastPreference::NoPreference);
    InspectableMenu menu;
    menu.setAttribute(Qt::WA_DontShowOnScreen, true);
    menu.setPalette(high_contrast ? QPalette{} : light_palette);
    menu.addAction(QStringLiteral("Bake Location"));
    menu.addAction(QStringLiteral("Bake Rotation"));
    menu.addAction(QStringLiteral("Bake Scale"));
    menu.addAction(QStringLiteral("Bake All"));
    const std::array<float, 3> factors{1.0F, 2.0F, 1.0F};
    for (std::size_t index = 0; index < factors.size(); ++index) {
      (void)controller.apply_factor(factors[index]);
      for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        SCOPED_TRACE(::testing::Message() << "high_contrast=" << high_contrast
                                         << " scale=" << factors[index]
                                         << " direction=" << direction);
        menu.setLayoutDirection(direction);
        menu.popup(QPoint(20, 20));
        QCoreApplication::processEvents();
        ASSERT_TRUE(menu.isVisible());
        for (QAction *action : menu.actions()) {
          SCOPED_TRACE(action->text().toStdString());
          QStyleOptionMenuItem option = menu.action_option(action);
          option.rect.moveTopLeft(QPoint{});
          EXPECT_FALSE(quader::qt_application_style_active(&option));
          const QImage actual = render_menu_item(*menu.style(), option, menu);
          QStyleOptionMenuItem blank_option(option);
          blank_option.text.clear();
          const QImage blank = render_menu_item(*menu.style(), blank_option, menu);
          const QRect actual_ink = changed_pixels(blank, actual);
          ASSERT_FALSE(actual_ink.isEmpty());

          // Compare the actual text raster with an unconstrained native row.
          // This detects elision without mirroring Fusion's private gutters.
          option.rect.setWidth(option.rect.width() +
                               option.fontMetrics.horizontalAdvance(option.text));
          const QImage roomy = render_menu_item(*menu.style(), option, menu);
          blank_option = option;
          blank_option.text.clear();
          const QImage roomy_blank =
              render_menu_item(*menu.style(), blank_option, menu);
          const QRect full_ink = changed_pixels(roomy_blank, roomy);
          ASSERT_FALSE(full_ink.isEmpty());
          EXPECT_EQ(actual.copy(actual_ink), roomy.copy(full_ink));
        }
        const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
        if (!directory.isEmpty()) {
          ASSERT_TRUE(QDir().mkpath(directory));
          QImage image(menu.size(), QImage::Format_ARGB32_Premultiplied);
          image.fill(Qt::transparent);
          menu.render(&image);
          const QString filename =
              QStringLiteral("menu-native-fallback-%1-%2-%3.png")
                  .arg(high_contrast ? QStringLiteral("high-contrast")
                                     : QStringLiteral("light"))
                  .arg(direction == Qt::LeftToRight ? QStringLiteral("ltr")
                                                   : QStringLiteral("rtl"))
                  .arg(static_cast<int>(index));
          EXPECT_TRUE(image.save(QDir(directory).filePath(filename)));
        }
        menu.close();
        QCoreApplication::processEvents();
      }
    }
  }
}

TEST_F(QtApplicationStyleTests, HighContrastMenusKeepNativePaintAndSizing) {
  QApplication::setPalette(QPalette{});
  quader::QtUiScaleProxyStyle style;
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  QMenu menu;
  QStyleOptionMenuItem option;
  option.rect = QRect(0, 0, 320, 28);
  option.palette = QApplication::palette();
  option.font = menu.font();
  option.fontMetrics = menu.fontMetrics();
  option.state = kEnabledState | QStyle::State_Selected;
  option.text = QStringLiteral("Texture Lock\tCtrl+L");
  option.checkType = QStyleOptionMenuItem::NonExclusive;
  option.checked = true;
  for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
    option.direction = direction;
    for (const auto type : {QStyleOptionMenuItem::Normal,
                             QStyleOptionMenuItem::SubMenu,
                             QStyleOptionMenuItem::Separator}) {
      option.menuItemType = type;
      EXPECT_EQ(render_menu_item(style, option, menu),
                  render_menu_item(*fusion, option, menu));
      const QSize size = style.sizeFromContents(
          QStyle::CT_MenuItem, &option, QSize(150, 20), &menu);
      const QSize native_size = fusion->sizeFromContents(
          QStyle::CT_MenuItem, &option, QSize(150, 20), &menu);
      EXPECT_EQ(size.height(), native_size.height());
      if (type == QStyleOptionMenuItem::Separator) {
        EXPECT_EQ(size, native_size);
      } else {
        EXPECT_GE(size.width(), native_size.width());
      }
    }
  }
  for (const auto metric : {QStyle::PM_MenuHMargin, QStyle::PM_MenuVMargin}) {
    EXPECT_EQ(style.pixelMetric(metric, &option, &menu),
                fusion->pixelMetric(metric, &option, &menu));
  }
  QImage themed_panel(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  themed_panel.fill(Qt::transparent);
  QImage native_panel = themed_panel;
  {
    QPainter painter(&themed_panel);
    style.drawPrimitive(QStyle::PE_PanelMenu, &option, &painter, &menu);
  }
  {
    QPainter painter(&native_panel);
    fusion->drawPrimitive(QStyle::PE_PanelMenu, &option, &painter, &menu);
  }
  EXPECT_EQ(themed_panel, native_panel);
}

TEST_F(QtApplicationStyleTests,
       FolderDockTitlesKeepAQuietRailAndSlopingShoulderAcrossStateScaleAndDirection) {
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    QDockWidget dock;
    QFont font = dock.font();
    font.setPointSizeF(10.0);
    dock.setFont(metrics.font(font));
    for (const auto group : {QPalette::Active, QPalette::Inactive,
                            QPalette::Disabled}) {
      for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        for (const bool vertical : {false, true}) {
          SCOPED_TRACE(::testing::Message()
                       << factor << ' ' << group << ' ' << direction << ' '
                       << vertical);
          QStyleOptionDockWidget option;
          option.rect = QRect(QPoint{}, metrics.size(vertical ? QSize(32, 320)
                                                              : QSize(320, 32)));
          option.palette = QApplication::palette();
          option.palette.setCurrentColorGroup(group);
          option.state = (group == QPalette::Disabled ? QStyle::State_None
                                                      : QStyle::State_Enabled) |
                         (group == QPalette::Active ? QStyle::State_Active
                                                    : QStyle::State_None);
          option.direction = direction;
          option.title = QStringLiteral("Outliner");
          option.closable = true;
          option.floatable = true;
          option.movable = true;
          option.verticalTitleBar = vertical;
          const QImage title = render_dock_title(style, option, dock);
          const QColor surface = option.palette.color(group, QPalette::Button);
          EXPECT_TRUE(contains_color(title, surface));
          EXPECT_TRUE(contains_color(title,
                                     option.palette.color(group, QPalette::Text)));
          EXPECT_TRUE(contains_color(title,
                                     option.palette.color(group, QPalette::Midlight)));
          EXPECT_NE(surface, option.palette.color(group, QPalette::Base));
          if (group != QPalette::Disabled) {
            EXPECT_GE(contrast_ratio(option.palette.color(group, QPalette::Text),
                                     surface), 4.5);
          }
          // Normalize orientation so the same visible contract covers docked
          // vertical titles and the native RTL title/button allocation.
          QImage normalized = vertical
                                  ? title.transformed(QTransform().rotate(90))
                                  : title;
          if (direction == Qt::RightToLeft) {
            normalized = normalized.flipped(Qt::Horizontal);
          }
          const QColor rail = option.palette.color(group, QPalette::Window);
          EXPECT_EQ(normalized.pixelColor(normalized.width() * 3 / 4,
                                           normalized.height() / 2), rail);
          const auto last_surface_pixel = [&](int row) {
            for (int x = normalized.width() - 1; x >= 0; --x) {
              if (normalized.pixelColor(x, row) == surface) {
                return x;
              }
            }
            return -1;
          };
          const int upper_edge = last_surface_pixel(normalized.height() / 4);
          const int lower_edge = last_surface_pixel(normalized.height() * 3 / 4);
          EXPECT_GT(upper_edge, metrics.px(20));
          EXPECT_GE(lower_edge - upper_edge, metrics.px(2));
          EXPECT_LT(lower_edge, normalized.width() / 2);
          QStyleOptionDockWidget empty = option;
          empty.title.clear();
          EXPECT_NE(title, render_dock_title(style, empty, dock));
        }
      }
    }
  }
}

TEST_F(QtApplicationStyleTests,
       PanelTitlesOmitButtonPaintAndHitAreasAcrossNativeLifecycleChanges) {
  const QPalette fusion_palette = QApplication::style()->standardPalette();
  quader::apply_qt_obsidian_application_theme(fusion_palette);
  quader::QtUiScaleApplicationController controller(*application_);
  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.setCentralWidget(new QWidget(&window));
  InspectableDock dock(QStringLiteral("Outliner"), &window);
  dock.setWidget(new QWidget(&dock));
  window.addDockWidget(Qt::LeftDockWidgetArea, &dock);
  window.resize(360, 260);
  window.show();
  QCoreApplication::processEvents();
  const auto initial_option = dock.title_option();
  const QRect initial_text = dock.style()->subElementRect(
      QStyle::SE_DockWidgetTitleBarText, &initial_option, &dock);
  std::vector<QPoint> old_button_centers;
  for (const char *name : {"qt_dockwidget_closebutton",
                           "qt_dockwidget_floatbutton"}) {
    auto *button = dock.findChild<QAbstractButton *>(
        QString::fromLatin1(name), Qt::FindDirectChildrenOnly);
    ASSERT_NE(button, nullptr);
    ASSERT_FALSE(button->geometry().isEmpty());
    old_button_centers.push_back(button->geometry().center());
  }

  quader::install_palette_aware_dock_title_presentation(dock);
  const auto settle = [&window, &dock]() {
    for (int pass = 0; pass < 2; ++pass) {
      if (dock.layout() != nullptr) {
        dock.layout()->activate();
      }
      window.layout()->activate();
      QApplication::processEvents();
    }
  };
  const auto expect_no_buttons = [&dock, &old_button_centers, &settle]() {
    settle();
    EXPECT_EQ(dock.titleBarWidget(), nullptr);
    const auto option = dock.title_option();
    const QRect text_rect = dock.style()->subElementRect(
        QStyle::SE_DockWidgetTitleBarText, &option, &dock);
    for (const char *name : {"qt_dockwidget_closebutton",
                             "qt_dockwidget_floatbutton"}) {
      auto *button = dock.findChild<QAbstractButton *>(
          QString::fromLatin1(name),
          Qt::FindDirectChildrenOnly);
      ASSERT_NE(button, nullptr);
      EXPECT_TRUE(button->geometry().isEmpty());
      EXPECT_TRUE(button->visibleRegion().isEmpty());
      EXPECT_FALSE(text_rect.intersects(button->geometry()));
      for (const QPoint &old_center : old_button_centers) {
        EXPECT_NE(dock.childAt(old_center), button);
      }
    }
  };
  expect_no_buttons();
  const auto installed_option = dock.title_option();
  const QRect installed_text = dock.style()->subElementRect(
      QStyle::SE_DockWidgetTitleBarText, &installed_option, &dock);
  EXPECT_GT(installed_text.width(), initial_text.width());
  const auto native_features = dock.features();
  EXPECT_TRUE(native_features.testFlag(QDockWidget::DockWidgetClosable));
  EXPECT_TRUE(native_features.testFlag(QDockWidget::DockWidgetFloatable));
  EXPECT_TRUE(native_features.testFlag(QDockWidget::DockWidgetMovable));

  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    quader::apply_ui_scale_to_palette_aware_dock_title_presentation(
        dock, controller.metrics());
    window.resize(controller.metrics().size(QSize(360, 260)));
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      dock.setLayoutDirection(direction);
      for (const bool vertical : {false, true}) {
        SCOPED_TRACE(::testing::Message() << factor << ' ' << direction << ' '
                                         << vertical);
        dock.setFeatures(QDockWidget::NoDockWidgetFeatures);
        expect_no_buttons();
        dock.setFeatures(native_features |
                         (vertical ? QDockWidget::DockWidgetVerticalTitleBar
                                   : QDockWidget::NoDockWidgetFeatures));
        expect_no_buttons();
        QEvent style_change(QEvent::StyleChange);
        QApplication::sendEvent(&dock, &style_change);
        expect_no_buttons();
        dock.setFloating(true);
        EXPECT_TRUE(dock.isFloating());
        expect_no_buttons();
        dock.setFloating(false);
        EXPECT_FALSE(dock.isFloating());
        expect_no_buttons();
      }
    }
  }

  dock.setFeatures(native_features);
  for (const auto contrast : {Qt::ContrastPreference::HighContrast,
                              Qt::ContrastPreference::NoPreference}) {
    quader::apply_qt_application_theme_for_contrast_preference(fusion_palette,
                                                              contrast);
    expect_no_buttons();
  }
  dock.toggleViewAction()->trigger();
  settle();
  EXPECT_FALSE(dock.isVisible());
  dock.toggleViewAction()->trigger();
  EXPECT_TRUE(dock.isVisible());
  expect_no_buttons();
  window.close();
}

TEST_F(QtApplicationStyleTests,
       FolderDocksKeepNativeLayoutBesideRetainedSectionsAtNarrowAndScaledWidths) {
  const QPalette fusion_palette = QApplication::style()->standardPalette();
  quader::apply_qt_obsidian_application_theme(fusion_palette);
  quader::QtUiScaleApplicationController controller(*application_);
  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  auto *viewport = new QLabel(QStringLiteral("Perspective"), &window);
  viewport->setAlignment(Qt::AlignCenter);
  window.setCentralWidget(viewport);
  InspectableDock outliner(QStringLiteral("Outliner"), &window);
  InspectableDock inspector(QStringLiteral("Inspector"), &window);
  InspectableDock face_texture(QStringLiteral("Face Texture"), &window);
  const std::array<InspectableDock *, 3> docks{&outliner, &inspector,
                                             &face_texture};
  auto *tree = new QTreeWidget(&outliner);
  tree->setHeaderHidden(true);
  for (const QString &name : {QStringLiteral("Curved wall"),
                              QStringLiteral("Box"),
                              QStringLiteral("Floor")}) {
    tree->addTopLevelItem(new QTreeWidgetItem(QStringList{name}));
  }
  tree->setCurrentItem(tree->topLevelItem(1));
  outliner.setWidget(tree);
  const auto add_property_section = [](QDockWidget &dock, const QString &title,
                                       const QStringList &labels) {
    auto *body = new QWidget(&dock);
    auto *layout = new QVBoxLayout(body);
    quader::qt_settings_detail::set_scalable_margins(*layout, QMargins(6, 6, 6, 6));
    quader::qt_settings_detail::set_scalable_spacing(*layout, 8);
    auto *section = new quader::qt_settings_detail::QtSettingsSection(
        QStringLiteral("folder-tab-review.section"), body);
    section->set_title(title);
    auto *fields = new QGridLayout(section->content());
    quader::qt_settings_detail::set_scalable_margins(*fields, QMargins(8, 6, 8, 8));
    quader::qt_settings_detail::set_scalable_grid_spacing(*fields, 6, 4);
    for (int row = 0; row < labels.size(); ++row) {
      fields->addWidget(new QLabel(labels[row], section->content()), row, 0);
      auto *field = new quader::QtDoubleSpinBox(section->content());
      field->setRange(-999.0, 999.0);
      field->setValue(row == 0 ? 1.0 : 0.0);
      fields->addWidget(field, row, 1);
    }
    fields->setColumnStretch(1, 1);
    layout->addWidget(section);
    layout->addStretch(1);
    dock.setWidget(body);
  };
  add_property_section(inspector, QStringLiteral("Transform"),
                       {QStringLiteral("X"), QStringLiteral("Y"),
                        QStringLiteral("Z")});
  add_property_section(face_texture, QStringLiteral("Texture Settings"),
                       {QStringLiteral("Scale U"), QStringLiteral("Shift U"),
                        QStringLiteral("Rotation")});
  for (InspectableDock *dock : docks) {
    quader::install_palette_aware_dock_title_presentation(*dock);
  }
  window.addDockWidget(Qt::LeftDockWidgetArea, &outliner);
  window.addDockWidget(Qt::LeftDockWidgetArea, &inspector);
  window.splitDockWidget(&outliner, &inspector, Qt::Vertical);
  window.addDockWidget(Qt::RightDockWidgetArea, &face_texture);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const auto capture = [&](const QString &name) {
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(window.grab().save(QDir(review_directory).filePath(name +
                                                                    ".png")));
    }
  };
  const auto settle = [&]() {
    window.show();
    for (int pass = 0; pass < 3; ++pass) {
      QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QApplication::processEvents();
      window.layout()->activate();
    }
  };
  int scale_pass = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    const auto &metrics = controller.metrics();
    for (InspectableDock *dock : docks) {
      quader::apply_ui_scale_to_palette_aware_dock_title_presentation(*dock, metrics);
      quader::qt_settings_detail::apply_scalable_layout_metrics(*dock, metrics);
      quader::qt_settings_detail::apply_scalable_widget_metrics(*dock, metrics);
    }
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      window.setLayoutDirection(direction);
      for (const bool narrow : {false, true}) {
        SCOPED_TRACE(::testing::Message() << scale_pass << ' ' << factor << ' '
                                         << direction << ' ' << narrow);
        window.resize(metrics.size(QSize(narrow ? 800 : 1100, 560)));
        window.resizeDocks({&outliner, &face_texture},
                           {metrics.px(narrow ? 180 : 260),
                            metrics.px(narrow ? 180 : 280)}, Qt::Horizontal);
        settle();
        for (InspectableDock *dock : docks) {
          const auto option = dock->title_option();
          const QRect text_rect = dock->style()->subElementRect(
              QStyle::SE_DockWidgetTitleBarText, &option, dock);
          QFont title_font = dock->font();
          title_font.setWeight(QFont::DemiBold);
          EXPECT_GT(text_rect.width() - metrics.px(18),
                    QFontMetrics(title_font).horizontalAdvance(dock->windowTitle()));
          for (const char *name : {"qt_dockwidget_closebutton",
                                   "qt_dockwidget_floatbutton"}) {
            auto *button = dock->findChild<QAbstractButton *>(
                QString::fromLatin1(name), Qt::FindDirectChildrenOnly);
            ASSERT_NE(button, nullptr);
            EXPECT_TRUE(button->geometry().isEmpty());
            EXPECT_TRUE(button->visibleRegion().isEmpty());
            EXPECT_FALSE(text_rect.intersects(button->geometry()));
          }
        }
        capture(QStringLiteral("folder-docks-%1-%2-%3-%4")
                    .arg(scale_pass).arg(qRound(factor * 100.0F))
                    .arg(direction == Qt::LeftToRight ? "ltr" : "rtl")
                    .arg(narrow ? "narrow" : "normal"));
        if (narrow) {
          face_texture.setWindowTitle(QStringLiteral(
              "Face Texture — UV Mapping and Material Assignment"));
          settle();
          const auto option = face_texture.title_option();
          const QRect text_rect = face_texture.style()->subElementRect(
              QStyle::SE_DockWidgetTitleBarText, &option, &face_texture);
          QFont title_font = face_texture.font();
          title_font.setWeight(QFont::DemiBold);
          EXPECT_LT(text_rect.width(),
                    QFontMetrics(title_font).horizontalAdvance(option.title));
          const QImage title = render_dock_title(*face_texture.style(), option,
                                                 face_texture);
          QStyleOptionDockWidget without_text = option;
          without_text.palette.setColor(QPalette::Text, Qt::transparent);
          const QRect text_ink = changed_pixels(
              title, render_dock_title(*face_texture.style(), without_text,
                                        face_texture));
          EXPECT_FALSE(text_ink.isEmpty());
          EXPECT_TRUE(text_rect.translated(-option.rect.topLeft()).contains(text_ink));
          capture(QStringLiteral("folder-docks-%1-%2-%3-long-title")
                      .arg(scale_pass).arg(qRound(factor * 100.0F))
                      .arg(direction == Qt::LeftToRight ? "ltr" : "rtl"));
          face_texture.setWindowTitle(QStringLiteral("Face Texture"));
        }
      }
    }
    ++scale_pass;
  }

  QPalette light_palette = fusion_palette;
  light_palette.setColor(QPalette::Window, QColor(245, 245, 245));
  light_palette.setColor(QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::AlternateBase, QColor(235, 235, 235));
  light_palette.setColor(QPalette::Text, Qt::black);
  light_palette.setColor(QPalette::Button, QColor(225, 225, 225));
  light_palette.setColor(QPalette::ButtonText, Qt::black);
  for (const bool high_contrast : {false, true}) {
    SCOPED_TRACE(::testing::Message() << "high_contrast=" << high_contrast);
    quader::apply_qt_application_theme_for_contrast_preference(
        fusion_palette, high_contrast ? Qt::ContrastPreference::HighContrast
                                     : Qt::ContrastPreference::NoPreference);
    window.setPalette(high_contrast ? QPalette{} : light_palette);
    for (InspectableDock *dock : docks) {
      // Docks are native subwindows; a parent palette need not propagate.
      dock->setPalette(high_contrast ? QPalette{} : light_palette);
    }
    settle();
    for (InspectableDock *dock : docks) {
      const auto option = dock->title_option();
      EXPECT_FALSE(quader::qt_application_style_active(&option));
    }
    capture(QStringLiteral("folder-docks-fallback-%1")
                .arg(high_contrast ? "high-contrast" : "light"));
  }
}

TEST_F(QtApplicationStyleTests,
       TabifiedDocksAndSceneTabsKeepDistinctShapesAndNativeSelection) {
  quader::apply_qt_obsidian_application_theme(
      QApplication::style()->standardPalette());
  quader::QtUiScaleApplicationController controller(*application_);
  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.setCentralWidget(new QWidget(&window));
  QDockWidget first(QStringLiteral("Outliner"), &window);
  QDockWidget second(QStringLiteral("Inspector"), &window);
  first.setWidget(new QWidget(&first));
  second.setWidget(new QWidget(&second));
  window.addDockWidget(Qt::LeftDockWidgetArea, &first);
  window.addDockWidget(Qt::LeftDockWidgetArea, &second);
  window.tabifyDockWidget(&first, &second);
  window.show();
  QApplication::processEvents();
  QTabBar *tabs = window.findChild<QTabBar *>(QString{},
                                             Qt::FindDirectChildrenOnly);
  ASSERT_NE(tabs, nullptr);
  ASSERT_EQ(tabs->count(), 2);
  QTabBar ordinary_tabs;
  QTabBar scene_tabs(&window);
  scene_tabs.setProperty(quader::kQtConnectedTabSurfaceProperty, true);
  scene_tabs.hide();
  bool first_visible = false;
  QObject::connect(&first, &QDockWidget::visibilityChanged, &window,
                   [&first_visible](bool visible) { first_visible = visible; });
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    const auto &metrics = controller.metrics();
    window.resize(metrics.size(QSize(520, 300)));
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      window.setLayoutDirection(direction);
      for (const auto position : {QTabWidget::North, QTabWidget::South}) {
        SCOPED_TRACE(::testing::Message() << factor << ' ' << direction << ' '
                                         << position);
        window.setTabPosition(Qt::LeftDockWidgetArea, position);
        second.raise();
        QApplication::processEvents();
        const int first_index = tabs->tabText(0) == first.windowTitle() ? 0 : 1;
        tabs->setCurrentIndex(first_index);
        QApplication::processEvents();
        EXPECT_TRUE(first_visible);
        EXPECT_EQ(tabs->currentIndex(), first_index);
        EXPECT_TRUE(window.tabifiedDockWidgets(&first).contains(&second));
        QStyleOptionTab option;
        option.initFrom(tabs);
        option.rect = tabs->tabRect(first_index);
        option.shape = tabs->shape();
        option.state = kEnabledState | QStyle::State_Selected;
        option.palette.setCurrentColorGroup(QPalette::Active);
        const QImage folder = render_tab_shape(*tabs->style(), option, *tabs);
        const QImage ordinary = render_tab_shape(*tabs->style(), option,
                                                 ordinary_tabs);
        EXPECT_FALSE(contains_color(folder, option.palette.color(QPalette::Accent)));
        EXPECT_TRUE(contains_color(ordinary, option.palette.color(QPalette::Accent)));
        EXPECT_NE(folder, ordinary);
        QImage normalized = position == QTabWidget::South
                                ? folder.flipped(Qt::Vertical) : folder;
        if (direction == Qt::RightToLeft) {
          normalized = normalized.flipped(Qt::Horizontal);
        }
        EXPECT_EQ(normalized.pixelColor(normalized.width() - metrics.px(3),
                                         normalized.height() / 4),
                  option.palette.color(QPalette::Window));
        option.shape = QTabBar::RoundedNorth;
        const QImage connected = render_tab_shape(*tabs->style(), option,
                                                  scene_tabs);
        EXPECT_EQ(connected.pixelColor(metrics.px(3), connected.height() / 4),
                  option.palette.color(QPalette::Window));
        EXPECT_EQ(connected.pixelColor(connected.width() - metrics.px(3),
                                        connected.height() / 4),
                  option.palette.color(QPalette::Window));
        EXPECT_EQ(connected.pixelColor(connected.width() / 2,
                                        connected.height() / 4),
                  option.palette.color(QPalette::AlternateBase));
        if (!review_directory.isEmpty()) {
          EXPECT_TRUE(window.grab().save(QDir(review_directory).filePath(
              QStringLiteral("folder-tabified-%1-%2-%3.png")
                  .arg(qRound(factor * 100.0F))
                  .arg(direction == Qt::LeftToRight ? "ltr" : "rtl")
                  .arg(position == QTabWidget::North ? "north" : "south"))));
        }
      }
    }
  }
}

TEST_F(QtApplicationStyleTests, HighContrastDockTitlesAndIconsUseNativeStyle) {
  QApplication::setPalette(QPalette{});
  quader::QtUiScaleProxyStyle style;
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  QDockWidget dock(QStringLiteral("Outliner"));
  QStyleOptionDockWidget option;
  option.rect = QRect(0, 0, 320, 32);
  option.palette = QApplication::palette();
  option.state = kEnabledState;
  option.title = dock.windowTitle();
  option.closable = true;
  option.floatable = true;
  for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
    option.direction = direction;
    EXPECT_EQ(render_dock_title(style, option, dock),
              render_dock_title(*fusion, option, dock));
    for (const auto icon : {QStyle::SP_TitleBarCloseButton,
                            QStyle::SP_TitleBarNormalButton}) {
      EXPECT_EQ(style.standardIcon(icon, &option, &dock)
                    .pixmap(QSize(24, 24)).toImage(),
                fusion->standardIcon(icon, &option, &dock)
                    .pixmap(QSize(24, 24)).toImage());
    }
  }
}
