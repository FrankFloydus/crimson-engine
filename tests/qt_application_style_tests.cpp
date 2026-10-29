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
#include "ui/qt_panel_section_header.hpp"
#include "ui/qt_semantic_axis_colors.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include "ui/qt_toolbar_button_presentation.hpp"
#include "ui/qt_ui_scale_metrics.hpp"

#include <QAction>
#include <QActionGroup>
#include <QAbstractButton>
#include <QAccessible>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDockWidget>
#include <QEvent>
#include <QGroupBox>
#include <QHoverEvent>
#include <QIcon>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QLayout>
#include <QLineEdit>
#include <QMenu>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QSlider>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QTabWidget>
#include <QToolBar>
#include <QToolButton>
#include <QTransform>
#include <QTreeWidget>
#include <QVariant>

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

class InspectableCheckBox final : public QCheckBox {
public:
  using QCheckBox::QCheckBox;

  [[nodiscard]] QStyleOptionButton indicator_option() const {
    QStyleOptionButton option;
    initStyleOption(&option);
    return option;
  }
};

class InspectableSlider final : public QSlider {
public:
  using QSlider::QSlider;

  [[nodiscard]] QStyleOptionSlider slider_option() const {
    QStyleOptionSlider option;
    initStyleOption(&option);
    return option;
  }
};

QImage render_slider(const QStyle &style, const QStyleOptionSlider &option,
                     const QSlider &slider, qreal device_pixel_ratio = 1.0) {
  QImage image(option.rect.size() * device_pixel_ratio,
               QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(device_pixel_ratio);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  style.drawComplexControl(QStyle::CC_Slider, &option, &painter, &slider);
  return image;
}

void send_slider_mouse(QSlider &slider, QEvent::Type type,
                       const QPoint &position, Qt::MouseButton button,
                       Qt::MouseButtons buttons) {
  QMouseEvent event(type, QPointF(position),
                     QPointF(slider.mapToGlobal(position)), button, buttons,
                     Qt::NoModifier);
  QApplication::sendEvent(&slider, &event);
}

void send_slider_key(QSlider &slider, int key) {
  for (const auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
    QKeyEvent event(type, key, Qt::NoModifier);
    QApplication::sendEvent(&slider, &event);
  }
}

class InspectableGroupBox final : public QGroupBox {
public:
  using QGroupBox::QGroupBox;

  [[nodiscard]] QStyleOptionGroupBox group_option() const {
    QStyleOptionGroupBox option;
    initStyleOption(&option);
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
                      const QStyleOptionComplex &option,
                      const QWidget *widget = nullptr) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  style.drawComplexControl(control, &option, &painter, widget);
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

QColor previous_flat_header_hover_color(const QPalette &palette,
                                       QPalette::ColorGroup group) {
  const QColor base = palette.color(group, QPalette::Base);
  const QColor text = palette.color(group, QPalette::WindowText);
  constexpr qreal kHoverTint = 0.035;
  const QColor mixed = QColor::fromRgbF(
      base.redF() + (text.redF() - base.redF()) * kHoverTint,
      base.greenF() + (text.greenF() - base.greenF()) * kHoverTint,
      base.blueF() + (text.blueF() - base.blueF()) * kHoverTint);
  return QColor::fromRgba(mixed.rgba());
}

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
                             QStyle::PE_IndicatorItemViewItemCheck,
                             QStyle::PE_IndicatorRadioButton,
                             QStyle::PE_IndicatorDockWidgetResizeHandle,
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

  QTabBar tabs;
  QStyleOptionTab tab;
  tab.rect = QRect(0, 0, 140, 32);
  tab.palette = button.palette;
  tab.state = kEnabledState | QStyle::State_Selected;
  for (const auto shape : {QTabBar::RoundedNorth, QTabBar::RoundedSouth,
                           QTabBar::RoundedWest, QTabBar::RoundedEast}) {
    tab.shape = shape;
    EXPECT_EQ(render_tab_shape(style, tab, tabs),
              render_tab_shape(*fusion, tab, tabs));
  }
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
    EXPECT_EQ(tabs.style()->pixelMetric(QStyle::PM_TabBarTabHSpace, nullptr, &tabs),
              metrics.px(24));
    EXPECT_EQ(tabs.style()->pixelMetric(QStyle::PM_TabBarTabVSpace, nullptr, &tabs),
              metrics.px(10));
    EXPECT_EQ(tabs.style()->pixelMetric(QStyle::PM_TabBarTabOverlap, nullptr, &tabs),
              0);
    EXPECT_EQ(tabs.style()->pixelMetric(QStyle::PM_TabBarBaseOverlap, nullptr, &tabs),
              0);
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
        QStyleOptionTab keyboard_option(label_option);
        keyboard_option.state |= QStyle::State_HasFocus |
                                 QStyle::State_KeyboardFocusChange;
        EXPECT_NE(label, render_tab_label(*tabs.style(), keyboard_option, tabs));
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
       DenseCommandButtonsMuteOnlyDisabledLabelsUsingTheLivePalette) {
  quader::QtUiScaleProxyStyle style;
  QPushButton button(QStringLiteral("Insert Edge Loop"));
  QStyleOptionButton option;
  option.rect = QRect(0, 0, 220, 24);
  option.palette = QApplication::palette();
  option.palette.setCurrentColorGroup(QPalette::Disabled);
  option.text = button.text();
  option.state = QStyle::State_Active | QStyle::State_Raised;
  for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
    option.direction = direction;
    button.setEnabled(false);
    button.setProperty(quader::kQtMutedDisabledButtonTextProperty, false);
    const QImage ordinary = render_button(style, option, button);
    button.setProperty(quader::kQtMutedDisabledButtonTextProperty, true);
    const QImage muted = render_button(style, option, button);
    EXPECT_NE(muted, ordinary);

    QStyleOptionButton expected(option);
    expected.palette.setColor(QPalette::Disabled, QPalette::ButtonText,
        quader::qt_muted_disabled_button_text_color(option.palette, true));
    button.setProperty(quader::kQtMutedDisabledButtonTextProperty, false);
    EXPECT_EQ(muted, render_button(style, expected, button));
    EXPECT_LT(relative_luminance(quader::qt_muted_disabled_button_text_color(
                  option.palette, true)),
                relative_luminance(option.palette.color(
                    QPalette::Disabled, QPalette::ButtonText)));

    option.state |= QStyle::State_Enabled | QStyle::State_HasFocus |
                    QStyle::State_KeyboardFocusChange;
    option.palette.setCurrentColorGroup(QPalette::Active);
    button.setEnabled(true);
    const QImage enabled = render_button(style, option, button);
    button.setProperty(quader::kQtMutedDisabledButtonTextProperty, true);
    EXPECT_EQ(enabled, render_button(style, option, button));
    option.state = QStyle::State_Active | QStyle::State_Raised;
    option.palette.setCurrentColorGroup(QPalette::Disabled);
  }

  button.setEnabled(false);
  option.palette.setColor(QPalette::Disabled, QPalette::PlaceholderText,
                            QColor(97, 102, 109));
  const QImage live_palette = render_button(style, option, button);
  button.setProperty(quader::kQtMutedDisabledButtonTextProperty, false);
  option.palette.setColor(QPalette::Disabled, QPalette::ButtonText,
      quader::qt_muted_disabled_button_text_color(option.palette, true));
  EXPECT_EQ(live_palette, render_button(style, option, button));

  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  QPalette light_palette = fusion->standardPalette();
  for (const auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    light_palette.setColor(group, QPalette::Window, Qt::white);
    light_palette.setColor(group, QPalette::WindowText, Qt::black);
    light_palette.setColor(group, QPalette::Base, Qt::white);
    light_palette.setColor(group, QPalette::Text, Qt::black);
    light_palette.setColor(group, QPalette::Button, Qt::white);
    light_palette.setColor(group, QPalette::ButtonText, Qt::black);
  }
  light_palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(90, 90, 90));
  button.setPalette(light_palette);
  option.palette = button.palette();
  option.palette.setCurrentColorGroup(QPalette::Disabled);
  ASSERT_FALSE(quader::qt_application_style_active(&option));
  const QImage native = render_button(style, option, button);
  EXPECT_EQ(native, render_button(*fusion, option, button));
  button.setProperty(quader::kQtMutedDisabledButtonTextProperty, true);
  EXPECT_EQ(native, render_button(style, option, button));
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

TEST_F(QtApplicationStyleTests,
       SlimSwitchesKeepCompactDistinctStatesAcrossNativeIndicatorRoutes) {
  const auto pale_thumb_bounds = [](const QImage &image) {
    QRect bounds;
    for (int y = 0; y < image.height(); ++y) {
      for (int x = 0; x < image.width(); ++x) {
        const QColor pixel = image.pixelColor(x, y);
        if (pixel.alpha() > 192 && relative_luminance(pixel) > 0.5) {
          bounds = bounds.united(QRect(x, y, 1, 1));
        }
      }
    }
    return bounds;
  };
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      for (const auto element : {QStyle::PE_IndicatorCheckBox,
                                 QStyle::PE_IndicatorItemViewItemCheck}) {
        SCOPED_TRACE(::testing::Message() << factor << ' ' << direction << ' '
                                         << element);
        QStyleOptionButton option;
        option.rect = QRect(QPoint{}, metrics.size(QSize(26, 12)));
        option.palette = QApplication::palette();
        option.direction = direction;
        option.state = kEnabledState | QStyle::State_Off;
        const QImage off = render_primitive(style, element, option);
        option.state = kEnabledState | QStyle::State_On;
        const QImage on = render_primitive(style, element, option);
        option.state = kEnabledState | QStyle::State_NoChange;
        const QImage mixed = render_primitive(style, element, option);
        EXPECT_NE(off, on);
        EXPECT_NE(on, mixed);
        EXPECT_NE(off, mixed);
        const QRect off_thumb = pale_thumb_bounds(off);
        const QRect on_thumb = pale_thumb_bounds(on);
        const QRect mixed_thumb = pale_thumb_bounds(mixed);
        ASSERT_FALSE(off_thumb.isEmpty());
        ASSERT_FALSE(on_thumb.isEmpty());
        ASSERT_FALSE(mixed_thumb.isEmpty());
        EXPECT_EQ(off_thumb.center().x() < option.rect.center().x(),
                  direction == Qt::LeftToRight);
        EXPECT_EQ(on_thumb.center().x() > option.rect.center().x(),
                  direction == Qt::LeftToRight);
        EXPECT_NEAR(QRectF(mixed_thumb).center().x(),
                    QRectF(option.rect).center().x(), 1.0);
        for (const auto state : {QStyle::State_Off, QStyle::State_On,
                                  QStyle::State_NoChange}) {
          option.state = kEnabledState | state;
          const QImage resting = render_primitive(style, element, option);
          option.state |= QStyle::State_HasFocus;
          EXPECT_NE(resting, render_primitive(style, element, option));
          option.state = kEnabledState | state | QStyle::State_MouseOver;
          EXPECT_NE(resting, render_primitive(style, element, option));
          option.state = QStyle::State_Active | state;
          const QImage disabled = render_primitive(style, element, option);
          EXPECT_NE(resting, disabled);
          EXPECT_GT(disabled.pixelColor(option.rect.center()).alpha(), 0);
          if (state == QStyle::State_On) {
            const QColor thumb = option.palette.color(QPalette::Disabled,
                                                       QPalette::Text);
            const QColor track = option.palette.color(QPalette::Disabled,
                                                       QPalette::Highlight);
            EXPECT_TRUE(contains_color(disabled, thumb));
            EXPECT_TRUE(contains_color(disabled, track));
            EXPECT_GE(contrast_ratio(thumb, track), 3.0)
                << "a disabled On thumb remains visible inside its track";
          }
        }
        EXPECT_EQ(off.pixelColor(0, 0).alpha(), 0)
            << "the compact track retains its pill silhouette";
      }
    }
  }
}

TEST_F(QtApplicationStyleTests,
       NativeSwitchWidgetsKeepKeyboardPointerAndTriStateSemantics) {
  const QPalette fusion_palette = QApplication::style()->standardPalette();
  quader::apply_qt_obsidian_application_theme(fusion_palette);
  quader::QtUiScaleApplicationController controller(*application_);
  QWidget panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.setAutoFillBackground(true);
  panel.setBackgroundRole(QPalette::Base);
  auto *layout = new QVBoxLayout(&panel);
  quader::qt_settings_detail::set_scalable_margins(*layout, QMargins{});
  quader::qt_settings_detail::set_scalable_spacing(*layout, 0);
  auto *title = new QLabel(QStringLiteral("Inspector"), &panel);
  QFont title_font = title->font();
  title_font.setWeight(QFont::DemiBold);
  title->setFont(title_font);
  title->setMargin(8);
  title->setBackgroundRole(QPalette::AlternateBase);
  title->setAutoFillBackground(true);
  layout->addWidget(title);
  auto *section = new quader::qt_settings_detail::QtSettingsSection(
      QStringLiteral("switch-review.visibility"), &panel);
  section->set_title(QStringLiteral("Visibility"));
  section->set_presentation(quader::QtPanelSectionHeader::Presentation::Flat);
  auto *rows = new QGridLayout(section->content());
  quader::qt_settings_detail::set_scalable_margins(*rows, QMargins(8, 8, 8, 8));
  quader::qt_settings_detail::set_scalable_grid_spacing(*rows, 8, 4);
  const std::array<QString, 7> labels{
      QStringLiteral("Visible"), QStringLiteral("Selectable"),
      QStringLiteral("Mixed selection"), QStringLiteral("Disabled Off"),
      QStringLiteral("Disabled On"), QStringLiteral("Disabled Mixed"),
      QStringLiteral("Generate Collisions")};
  std::array<InspectableCheckBox *, labels.size()> switches{};
  for (std::size_t index = 0; index < labels.size(); ++index) {
    rows->addWidget(new QLabel(labels[index], section->content()),
                    static_cast<int>(index), 0);
    auto *control = new InspectableCheckBox(section->content());
    control->setAccessibleName(labels[index]);
    control->setTristate(index == 2 || index == 5);
    control->setCheckState(index == 2 || index == 5 ? Qt::PartiallyChecked
                            : index == 1 || index == 4 ? Qt::Checked
                                                       : Qt::Unchecked);
    control->setEnabled(index < 3 || index == 6);
    rows->addWidget(control, static_cast<int>(index), 1);
    switches[index] = control;
  }
  rows->setColumnStretch(1, 1);
  layout->addWidget(section);
  auto *group = new InspectableGroupBox(QStringLiteral("Checkable group"), &panel);
  group->setCheckable(true);
  auto *group_layout = new QVBoxLayout(group);
  group_layout->addWidget(new QLabel(QStringLiteral("Native child controls"), group));
  layout->addWidget(group);
  auto *tree = new QTreeWidget(&panel);
  tree->setHeaderHidden(true);
  tree->setRootIsDecorated(false);
  for (const auto state : {Qt::Unchecked, Qt::Checked, Qt::PartiallyChecked}) {
    auto *item = new QTreeWidgetItem(tree, QStringList{
        state == Qt::Unchecked ? QStringLiteral("Tree Off")
        : state == Qt::Checked ? QStringLiteral("Tree On")
                              : QStringLiteral("Tree Mixed")});
    item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
    item->setCheckState(0, state);
  }
  layout->addWidget(tree);
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  const auto send_click = [](QWidget &widget, const QPoint &position) {
    for (const auto type : {QEvent::MouseButtonPress, QEvent::MouseButtonRelease}) {
      QMouseEvent event(type, QPointF(position),
                         QPointF(widget.mapToGlobal(position)), Qt::LeftButton,
                         type == QEvent::MouseButtonPress ? Qt::LeftButton
                                                          : Qt::NoButton,
                         Qt::NoModifier);
      QApplication::sendEvent(&widget, &event);
    }
  };
  const auto send_space = [](QWidget &widget) {
    for (const auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
      QKeyEvent event(type, Qt::Key_Space, Qt::NoModifier);
      QApplication::sendEvent(&widget, &event);
    }
  };
  int scale_pass = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    const auto &metrics = controller.metrics();
    quader::qt_settings_detail::apply_scalable_layout_metrics(panel, metrics);
    quader::qt_settings_detail::apply_scalable_widget_metrics(panel, metrics);
    QFont scaled_title_font = panel.font();
    scaled_title_font.setWeight(QFont::DemiBold);
    title->setFont(scaled_title_font);
    title->setMargin(metrics.px(8));
    tree->setFixedHeight(metrics.px(84));
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      SCOPED_TRACE(::testing::Message() << scale_pass << ' ' << factor << ' '
                                       << direction);
      panel.setLayoutDirection(direction);
      panel.resize(metrics.px(340), panel.sizeHint().height());
      panel.show();
      QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QApplication::processEvents();
      layout->activate();
      for (InspectableCheckBox *control : switches) {
        const auto option = control->indicator_option();
        const QRect indicator = control->style()->subElementRect(
            QStyle::SE_CheckBoxIndicator, &option, control);
        EXPECT_EQ(indicator.size(), metrics.size(QSize(26, 12)));
        EXPECT_TRUE(control->rect().contains(indicator));
        EXPECT_EQ(control->accessibleName().isEmpty(), false);
      }
      const auto group_option = group->group_option();
      const QRect group_indicator = group->style()->subControlRect(
          QStyle::CC_GroupBox, &group_option, QStyle::SC_GroupBoxCheckBox, group);
      EXPECT_EQ(group_indicator.size(), metrics.size(QSize(26, 12)));
      group->setChecked(true);
      send_click(*group, group_indicator.center());
      EXPECT_FALSE(group->isChecked());
      send_space(*group);
      EXPECT_TRUE(group->isChecked());

      InspectableCheckBox &control = *switches.front();
      control.setChecked(false);
      auto option = control.indicator_option();
      const QRect indicator = control.style()->subElementRect(
          QStyle::SE_CheckBoxIndicator, &option, &control);
      int toggle_count = 0;
      const auto connection = QObject::connect(
          &control, &QCheckBox::toggled, &panel,
          [&toggle_count]() { ++toggle_count; });
      send_click(control, indicator.center());
      EXPECT_TRUE(control.isChecked());
      EXPECT_EQ(toggle_count, 1);
      send_space(control);
      EXPECT_FALSE(control.isChecked());
      EXPECT_EQ(toggle_count, 2);
      control.setEnabled(false);
      send_click(control, indicator.center());
      send_space(control);
      EXPECT_FALSE(control.isChecked());
      EXPECT_EQ(toggle_count, 2);
      control.setEnabled(true);
      QObject::disconnect(connection);
      InspectableCheckBox &mixed = *switches[2];
      mixed.setCheckState(Qt::Unchecked);
      send_space(mixed);
      EXPECT_EQ(mixed.checkState(), Qt::PartiallyChecked);
      send_space(mixed);
      EXPECT_EQ(mixed.checkState(), Qt::Checked);
      send_space(mixed);
      EXPECT_EQ(mixed.checkState(), Qt::Unchecked);
      mixed.setCheckState(Qt::PartiallyChecked);

      QTreeWidgetItem *item = tree->topLevelItem(0);
      item->setCheckState(0, Qt::Unchecked);
      tree->setCurrentItem(item);
      send_space(*tree);
      EXPECT_EQ(item->checkState(0), Qt::Checked);
      item->setCheckState(0, Qt::Unchecked);
      QStyleOptionViewItem item_option;
      item_option.initFrom(tree->viewport());
      item_option.rect = tree->visualItemRect(item);
      item_option.features = QStyleOptionViewItem::HasCheckIndicator;
      item_option.checkState = Qt::Unchecked;
      const QRect item_indicator = tree->style()->subElementRect(
          QStyle::SE_ItemViewItemCheckIndicator, &item_option, tree);
      EXPECT_EQ(item_indicator.size(), metrics.size(QSize(26, 12)));
      EXPECT_TRUE(item_option.rect.contains(item_indicator));
      send_click(*tree->viewport(), item_indicator.center());
      EXPECT_EQ(item->checkState(0), Qt::Checked);
      tree->setCurrentItem(nullptr);
      item->setCheckState(0, Qt::Unchecked);
      switches[1]->setFocus(Qt::TabFocusReason);
      QApplication::processEvents();
      if (!review_directory.isEmpty()) {
        EXPECT_TRUE(panel.grab().save(QDir(review_directory).filePath(
            QStringLiteral("slim-switch-controls-%1-%2-%3.png")
                .arg(scale_pass).arg(qRound(factor * 100.0F))
                .arg(direction == Qt::LeftToRight ? "ltr" : "rtl"))));
      }
    }
    ++scale_pass;
  }
  QPalette light_palette(fusion_palette);
  light_palette.setColor(QPalette::Window, Qt::white);
  light_palette.setColor(QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::Text, Qt::black);
  light_palette.setColor(QPalette::Button, Qt::white);
  light_palette.setColor(QPalette::ButtonText, Qt::black);
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  for (const bool high_contrast : {false, true}) {
    quader::apply_qt_application_theme_for_contrast_preference(
        fusion_palette, high_contrast ? Qt::ContrastPreference::HighContrast
                                     : Qt::ContrastPreference::NoPreference);
    panel.setPalette(high_contrast ? QPalette{} : light_palette);
    panel.setLayoutDirection(Qt::LeftToRight);
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();
    for (InspectableCheckBox *control : switches) {
      auto option = control->indicator_option();
      EXPECT_FALSE(quader::qt_application_style_active(&option));
      option.rect = QRect(QPoint{}, QSize(26, 12));
      for (const auto element : {QStyle::PE_IndicatorCheckBox,
                                 QStyle::PE_IndicatorItemViewItemCheck}) {
        EXPECT_EQ(render_primitive(*control->style(), element, option, control),
                  render_primitive(*fusion, element, option, control));
      }
    }
    if (!review_directory.isEmpty()) {
      EXPECT_TRUE(panel.grab().save(QDir(review_directory).filePath(
          high_contrast ? QStringLiteral("slim-switch-controls-high-contrast.png")
                        : QStringLiteral("slim-switch-controls-light.png"))));
    }
  }
}

TEST_F(QtApplicationStyleTests,
       SquareEditorSurfacesRequireAnExplicitPropertyOnEachEditor) {
  QWidget host;
  host.setProperty(quader::kQtSquareEditorSurfaceProperty, true);
  QLineEdit line(&host);
  quader::QtDoubleSpinBox spin(&host);
  QComboBox combo(&host);
  QComboBox editable_combo(&host);
  editable_combo.setEditable(true);
  const std::array<QWidget *, 4> editors{&line, &spin, &combo, &editable_combo};
  const std::array<QStyle::State, 4> states{
      kEnabledState, kEnabledState | QStyle::State_MouseOver,
      kEnabledState | QStyle::State_HasFocus, QStyle::State_None};
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    for (QWidget *editor : editors) {
      for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        for (const QStyle::State state : states) {
          SCOPED_TRACE(::testing::Message()
                       << factor << ' ' << editor->metaObject()->className()
                       << ' ' << direction << ' ' << state.toInt());
          const auto render_editor = [&]() {
            if (editor == &line) {
              QStyleOptionFrame option;
              option.rect = QRect(QPoint{}, metrics.size(QSize(120, 28)));
              option.palette = QApplication::palette();
              option.state = state;
              option.direction = direction;
              option.lineWidth = 1;
              return render_primitive(style, QStyle::PE_PanelLineEdit, option,
                                      editor);
            }
            if (editor == &spin) {
              QStyleOptionSpinBox option;
              option.rect = QRect(QPoint{}, metrics.size(QSize(120, 28)));
              option.palette = QApplication::palette();
              option.state = state;
              option.direction = direction;
              option.frame = true;
              option.subControls = QStyle::SC_SpinBoxFrame;
              return render_complex(style, QStyle::CC_SpinBox, option, editor);
            }
            QStyleOptionComboBox option;
            option.rect = QRect(QPoint{}, metrics.size(QSize(120, 28)));
            option.palette = QApplication::palette();
            option.state = state;
            option.direction = direction;
            option.frame = true;
            option.editable = editor == &editable_combo;
            option.subControls = QStyle::SC_ComboBoxFrame;
            return render_complex(style, QStyle::CC_ComboBox, option, editor);
          };
          editor->setProperty(quader::kQtSquareEditorSurfaceProperty, QVariant{});
          const QImage rounded = render_editor();
          editor->setProperty(quader::kQtSquareEditorSurfaceProperty, true);
          const QImage square = render_editor();
          if (state == kEnabledState) {
            EXPECT_EQ(square.pixelColor(0, square.height() / 2),
                      QApplication::palette().color(QPalette::Active,
                                                     QPalette::Mid));
          }
          for (const QPoint corner :
               {square.rect().topLeft(), square.rect().topRight(),
                square.rect().bottomLeft(), square.rect().bottomRight()}) {
            EXPECT_GT(square.pixelColor(corner).alpha(),
                      rounded.pixelColor(corner).alpha());
          }
          EXPECT_EQ(square.pixelColor(square.rect().center()),
                    rounded.pixelColor(rounded.rect().center()));
          editor->setProperty(quader::kQtSquareEditorSurfaceProperty, QVariant{});
          EXPECT_EQ(render_editor(), rounded);
        }
      }
    }
  }
}

TEST_F(QtApplicationStyleTests,
       AxisStripsStayInsideSquareFieldsWithoutReplacingTheirFocusBoundary) {
  QWidget host;
  host.setProperty(quader::kQtEditorAxisProperty, 0);
  QLineEdit line(&host);
  quader::QtDoubleSpinBox spin(&host);
  const std::array<QStyle::State, 5> states{
      kEnabledState, kEnabledState | QStyle::State_MouseOver,
      kEnabledState | QStyle::State_HasFocus, QStyle::State_Enabled,
      QStyle::State_None};
  using quader_editor_core::ViewAxis;
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    for (QWidget *editor : std::array<QWidget *, 2>{&line, &spin}) {
      editor->setProperty(quader::kQtSquareEditorSurfaceProperty, true);
      for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        for (const auto state : states) {
          const auto render_editor = [&]() {
            if (editor == &line) {
              QStyleOptionFrame option;
              option.rect = QRect(QPoint{}, metrics.size(QSize(120, 28)));
              option.palette = QApplication::palette();
              option.state = state;
              option.direction = direction;
              option.lineWidth = 1;
              return render_primitive(style, QStyle::PE_PanelLineEdit, option, editor);
            }
            QStyleOptionSpinBox option;
            option.rect = QRect(QPoint{}, metrics.size(QSize(120, 28)));
            option.palette = QApplication::palette();
            option.state = state;
            option.direction = direction;
            option.frame = true;
            option.subControls = QStyle::SC_SpinBoxFrame;
            return render_complex(style, QStyle::CC_SpinBox, option, editor);
          };
          editor->setProperty(quader::kQtEditorAxisProperty, QVariant{});
          const QImage ordinary = render_editor();
          editor->setProperty(quader::kQtEditorAxisProperty, -1);
          EXPECT_EQ(render_editor(), ordinary);
          for (const auto axis : {ViewAxis::X, ViewAxis::Y, ViewAxis::Z}) {
            SCOPED_TRACE(::testing::Message() << factor << ' ' << direction
                         << ' ' << state.toInt() << ' ' << static_cast<int>(axis));
            editor->setProperty(quader::kQtEditorAxisProperty, static_cast<int>(axis));
            const QImage marked = render_editor();
            const int strip_width = metrics.px(2);
            const int strip_left = direction == Qt::RightToLeft
                                       ? marked.width() - 1 - strip_width : 1;
            EXPECT_EQ(changed_pixels(ordinary, marked),
                      QRect(strip_left, 1, strip_width, marked.height() - 2));
            const QColor marker = marked.pixelColor(strip_left, marked.height() / 2);
            if (state.testFlag(QStyle::State_Enabled)) {
              EXPECT_EQ(marker, quader::qt_semantic_axis_color(axis));
            } else {
              EXPECT_NE(marker, quader::qt_semantic_axis_color(axis));
              EXPECT_NE(marker, ordinary.pixelColor(strip_left, marked.height() / 2));
            }
          }
          editor->setProperty(quader::kQtEditorAxisProperty, QVariant{});
        }
      }
    }
  }
}

TEST_F(QtApplicationStyleTests,
       AxisLineEditorsKeepScaledInnerPaddingAndNativeFallbackGeometry) {
  quader::QtUiScaleApplicationController controller(*application_);
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  QLineEdit line(QStringLiteral("Mixed"));
  line.setProperty(quader::kQtSquareEditorSurfaceProperty, true);
  line.setProperty(quader::kQtEditorAxisProperty,
      static_cast<int>(quader_editor_core::ViewAxis::X));
  line.setAttribute(Qt::WA_DontShowOnScreen, true);
  line.show();
  const auto option_for_line = [&] {
    QStyleOptionFrame option;
    option.initFrom(&line);
    option.lineWidth = line.style()->pixelMetric(QStyle::PM_DefaultFrameWidth, &option, &line);
    return option;
  };
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    const auto &metrics = controller.metrics();
    line.resize(metrics.size(QSize(120, 28)));
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      line.setLayoutDirection(direction);
      for (const bool enabled : {false, true}) {
        line.setEnabled(enabled);
        line.setReadOnly(!enabled);
        if (enabled) line.setFocus(Qt::TabFocusReason);
        const auto option = option_for_line();
        const QRect native = fusion->subElementRect(QStyle::SE_LineEditContents, &option, &line);
        const QRect padded = line.style()->subElementRect(QStyle::SE_LineEditContents, &option, &line);
        EXPECT_EQ(padded, native.adjusted(metrics.px(6), 0, -metrics.px(6), 0));
        EXPECT_GE(padded.width(), line.fontMetrics().horizontalAdvance(line.text()));
        EXPECT_EQ(line.textMargins(), QMargins{});
        line.setProperty(quader::kQtEditorAxisProperty, -1);
        EXPECT_EQ(line.style()->subElementRect(QStyle::SE_LineEditContents, &option, &line), native);
        line.setProperty(quader::kQtEditorAxisProperty,
            static_cast<int>(quader_editor_core::ViewAxis::X));
      }
    }
  }
  for (const bool high_contrast : {false, true}) {
    if (high_contrast) QApplication::setPalette(QPalette{});
    line.setPalette(high_contrast ? QPalette{} : QPalette(QColor(239, 239, 239)));
    const auto option = option_for_line();
    ASSERT_FALSE(quader::qt_application_style_active(&option));
    EXPECT_EQ(line.style()->subElementRect(QStyle::SE_LineEditContents, &option, &line),
        fusion->subElementRect(QStyle::SE_LineEditContents, &option, &line));
  }
}

TEST_F(QtApplicationStyleTests,
       FramelessEditorsCanRetainTheirOwnersPaintedSurface) {
  quader::QtUiScaleProxyStyle style;
  QLineEdit line;
  QStyleOptionFrame option;
  option.rect = QRect(0, 0, 120, 28);
  option.palette = QApplication::palette();
  option.state = kEnabledState;
  option.lineWidth = 0;
  const QImage ordinary =
      render_primitive(style, QStyle::PE_PanelLineEdit, option, &line);
  EXPECT_EQ(ordinary.pixelColor(option.rect.center()),
            option.palette.color(QPalette::Active, QPalette::Button));
  line.setProperty(quader::kQtParentPaintedEditorSurfaceProperty, true);
  const QImage retained =
      render_primitive(style, QStyle::PE_PanelLineEdit, option, &line);
  QImage transparent(retained.size(), retained.format());
  transparent.fill(Qt::transparent);
  EXPECT_EQ(retained, transparent);
  line.setProperty(quader::kQtParentPaintedEditorSurfaceProperty, QVariant{});
  EXPECT_EQ(render_primitive(style, QStyle::PE_PanelLineEdit, option, &line),
            ordinary);
}

TEST_F(QtApplicationStyleTests,
       SquareEditorSurfacesPreserveNativeLightAndHighContrastPainting) {
  quader::QtUiScaleProxyStyle style;
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  QPalette light_palette = fusion->standardPalette();
  light_palette.setColor(QPalette::Window, Qt::white);
  light_palette.setColor(QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::Text, Qt::black);
  light_palette.setColor(QPalette::Button, Qt::white);
  light_palette.setColor(QPalette::ButtonText, Qt::black);
  QLineEdit line;
  line.setProperty(quader::kQtParentPaintedEditorSurfaceProperty, true);
  quader::QtDoubleSpinBox spin;
  QComboBox combo;
  for (QWidget *editor : std::array<QWidget *, 3>{&line, &spin, &combo}) {
    editor->setProperty(quader::kQtSquareEditorSurfaceProperty, true);
    editor->setProperty(quader::kQtEditorAxisProperty,
                        static_cast<int>(quader_editor_core::ViewAxis::X));
  }
  for (const bool high_contrast : {false, true}) {
    SCOPED_TRACE(high_contrast);
    if (high_contrast) {
      QApplication::setPalette(QPalette{});
    }
    const QPalette palette = high_contrast ? QApplication::palette()
                                          : light_palette;
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      QStyleOptionFrame line_option;
      line_option.rect = QRect(0, 0, 120, 28);
      line_option.palette = palette;
      line_option.state = kEnabledState | QStyle::State_HasFocus;
      line_option.direction = direction;
      line_option.lineWidth = 1;
      ASSERT_FALSE(quader::qt_application_style_active(&line_option));
      EXPECT_EQ(render_primitive(style, QStyle::PE_PanelLineEdit, line_option,
                                &line),
                render_primitive(*fusion, QStyle::PE_PanelLineEdit, line_option,
                                 &line));
      line_option.lineWidth = 0;
      EXPECT_EQ(render_primitive(style, QStyle::PE_PanelLineEdit, line_option,
                                &line),
                render_primitive(*fusion, QStyle::PE_PanelLineEdit, line_option,
                                 &line));
      QStyleOptionSpinBox spin_option;
      spin_option.rect = line_option.rect;
      spin_option.palette = palette;
      spin_option.state = line_option.state;
      spin_option.direction = direction;
      spin_option.frame = true;
      spin_option.subControls = QStyle::SC_All;
      spin_option.stepEnabled = QAbstractSpinBox::StepUpEnabled;
      EXPECT_EQ(render_complex(style, QStyle::CC_SpinBox, spin_option, &spin),
                render_complex(*fusion, QStyle::CC_SpinBox, spin_option, &spin));
      for (const bool editable : {false, true}) {
        QStyleOptionComboBox combo_option;
        combo_option.rect = line_option.rect;
        combo_option.palette = palette;
        combo_option.state = line_option.state;
        combo_option.direction = direction;
        combo_option.frame = true;
        combo_option.subControls = QStyle::SC_All;
        combo_option.editable = editable;
        EXPECT_EQ(render_complex(style, QStyle::CC_ComboBox, combo_option, &combo),
                  render_complex(*fusion, QStyle::CC_ComboBox, combo_option,
                                 &combo));
      }
    }
  }
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
      const QRect off_switch = changed_pixels(blank, unchecked);
      EXPECT_GE(off_switch.width(), metrics.px(24));
      EXPECT_LE(off_switch.height(), metrics.px(12));
      option.checked = true;
      const QImage checked = render_menu_item(style, option, menu);
      const QRect check = changed_pixels(unchecked, checked);
      ASSERT_FALSE(check.isEmpty());
      EXPECT_GE(check.width(), metrics.px(24));
      EXPECT_LE(check.height(), metrics.px(12));
      EXPECT_EQ(check.center().x() < option.rect.center().x(),
                  direction == Qt::LeftToRight);
      option.checkType = QStyleOptionMenuItem::Exclusive;
      EXPECT_NE(checked, render_menu_item(style, option, menu));
      for (const QStyle::State state : {
               QStyle::State(kEnabledState),
               QStyle::State(kEnabledState | QStyle::State_Selected),
               QStyle::State(QStyle::State_Active)}) {
        option.state = state;
        option.checked = true;
        const QImage exclusive_on = render_menu_item(style, option, menu);
        option.checked = false;
        const QImage exclusive_off = render_menu_item(style, option, menu);
        const QRect marker = changed_pixels(exclusive_off, exclusive_on);
        ASSERT_FALSE(marker.isEmpty());
        EXPECT_LE(marker.width(), metrics.px(6));
        EXPECT_LE(marker.height(), metrics.px(6));
        EXPECT_EQ(marker.center().x() < option.rect.center().x(),
                  direction == Qt::LeftToRight);
      }

      option.checkType = QStyleOptionMenuItem::NotCheckable;
      option.checked = false;
      option.state = kEnabledState;
      option.icon = menu_state_icon();
      const QImage icon = render_menu_item(style, option, menu);
      EXPECT_TRUE(contains_color(icon, QColor(225, 95, 75)));
      option.checked = true;
      option.checkType = QStyleOptionMenuItem::NonExclusive;
      const QImage checked_icon = render_menu_item(style, option, menu);
      option.checkType = QStyleOptionMenuItem::Exclusive;
      EXPECT_EQ(checked_icon, render_menu_item(style, option, menu))
          << "exclusive choices retain the authored checked-icon backplate";
      option.checked = false;
      option.checkType = QStyleOptionMenuItem::NotCheckable;
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
       ExclusiveCameraMenusKeepCompactMarkersAndNativeSelection) {
  quader::apply_qt_obsidian_application_theme(
      QApplication::style()->standardPalette());
  quader::QtUiScaleApplicationController controller(*application_);
  InspectableMenu menu;
  menu.setAttribute(Qt::WA_DontShowOnScreen, true);
  QActionGroup cameras(&menu);
  cameras.setExclusive(true);
  std::array<QAction *, 4> actions{};
  const std::array<QString, 4> labels{
      QStringLiteral("Perspective"), QStringLiteral("Top"),
      QStringLiteral("Bottom"), QStringLiteral("Front")};
  for (std::size_t index = 0; index < labels.size(); ++index) {
    actions[index] = menu.addAction(labels[index]);
    actions[index]->setCheckable(true);
    cameras.addAction(actions[index]);
  }
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  for (const float factor : {1.0F, 2.0F}) {
    controller.apply_factor(factor);
    const auto &metrics = controller.metrics();
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      SCOPED_TRACE(::testing::Message() << factor << ' ' << direction);
      menu.setLayoutDirection(direction);
      actions.front()->setChecked(true);
      menu.popup(QPoint(20, 20));
      QApplication::processEvents();
      const auto capture = [&](const char *state) {
        if (!review_directory.isEmpty()) {
          EXPECT_TRUE(menu.grab().save(QDir(review_directory).filePath(
              QStringLiteral("camera-menu-%1-%2-%3.png")
                  .arg(qRound(factor * 100.0F))
                  .arg(direction == Qt::LeftToRight ? "ltr" : "rtl")
                  .arg(QString::fromLatin1(state)))));
        }
      };
      for (const auto state : {QStyle::State(kEnabledState),
                                QStyle::State(kEnabledState | QStyle::State_Selected),
                                QStyle::State(QStyle::State_Active)}) {
        auto option = menu.action_option(actions.front());
        option.rect = QRect(QPoint{}, option.rect.size());
        option.text.clear();
        option.state = state;
        option.checked = false;
        const QImage off = render_menu_item(*menu.style(), option, menu);
        option.checked = true;
        const QImage on = render_menu_item(*menu.style(), option, menu);
        const QRect marker = changed_pixels(off, on);
        ASSERT_FALSE(marker.isEmpty());
        EXPECT_LE(marker.width(), metrics.px(6));
        EXPECT_LE(marker.height(), metrics.px(6));
        EXPECT_TRUE(option.rect.contains(marker));
        EXPECT_EQ(marker.center().x() < option.rect.center().x(),
                  direction == Qt::LeftToRight);
      }
      menu.setActiveAction(actions[1]);
      capture("hover");
      actions.front()->setEnabled(false);
      capture("disabled-current");
      actions.front()->setEnabled(true);
      menu.setActiveAction(actions.front());
      send_menu_key(menu, Qt::Key_Down);
      EXPECT_EQ(menu.activeAction(), actions[1]);
      send_menu_key(menu, Qt::Key_Return);
      EXPECT_TRUE(actions[1]->isChecked());
      EXPECT_FALSE(actions.front()->isChecked());
      EXPECT_EQ(cameras.checkedAction(), actions[1]);
      EXPECT_FALSE(menu.isVisible());
      menu.popup(QPoint(20, 20));
      QApplication::processEvents();
      capture("top-selected");
      menu.close();
    }
  }
}

TEST_F(QtApplicationStyleTests,
       ExclusiveMenuMarkersAlignWithNativeLabelInkAcrossScaleAndState) {
  quader::apply_qt_obsidian_application_theme(
      QApplication::style()->standardPalette());
  quader::QtUiScaleApplicationController controller(*application_);
  const auto label_without_shortcut_or_mnemonic = [](const QString &text) {
    const QString label = text.section(QLatin1Char('\t'), 0, 0);
    QString visible;
    for (qsizetype index = 0; index < label.size(); ++index) {
      if (label[index] != QLatin1Char('&')) {
        visible += label[index];
      } else if (index + 1 < label.size() &&
                 label[index + 1] == QLatin1Char('&')) {
        // Re-encode the literal ampersand for Qt's native mnemonic painter.
        visible += QStringLiteral("&&");
        ++index;
      }
    }
    return visible;
  };
  struct MenuCase final {
    const char *name;
    std::array<QString, 4> labels;
  };
  const std::array<MenuCase, 2> menus{{
      {"camera", {QStringLiteral("Perspective"), QStringLiteral("&Top"),
                   QStringLiteral("Bottom"), QStringLiteral("Front")}},
      {"shading", {QStringLiteral("&Shaded\tF6"),
                    QStringLiteral("&Wireframe\tF4"),
                    QStringLiteral("Shaded && Wireframe"),
                    QStringLiteral("Flat")}},
  }};
  struct ScaleCase final {
    float factor;
    const char *name;
  };
  constexpr std::array<ScaleCase, 4> kScales{{
      {1.0F, "100"}, {1.5F, "150"}, {2.0F, "200"}, {1.0F, "scale-down"},
  }};
  const QString review_directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!review_directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(review_directory));
  }
  for (const auto &menu_case : menus) {
    InspectableMenu menu;
    menu.setAttribute(Qt::WA_DontShowOnScreen, true);
    QActionGroup choices(&menu);
    choices.setExclusive(true);
    std::array<QAction *, 4> actions{};
    for (std::size_t index = 0; index < actions.size(); ++index) {
      actions[index] = menu.addAction(menu_case.labels[index]);
      actions[index]->setCheckable(true);
      choices.addAction(actions[index]);
    }
    actions.front()->setChecked(true);
    menu.setDefaultAction(actions.back());
    for (const auto &scale : kScales) {
      controller.apply_factor(scale.factor);
      for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        menu.setLayoutDirection(direction);
        menu.popup(QPoint(20, 20));
        QApplication::processEvents();
        for (QAction *action : actions) {
          for (const auto state : {
                   QStyle::State(kEnabledState),
                   QStyle::State(kEnabledState | QStyle::State_Selected),
                   QStyle::State(QStyle::State_Active)}) {
            SCOPED_TRACE(::testing::Message()
                         << menu_case.name << ' ' << scale.name << ' '
                         << direction << ' ' << action->text().toStdString()
                         << ' ' << state.toInt());
            auto option = menu.action_option(action);
            option.rect = QRect(QPoint{}, option.rect.size());
            option.state = state;
            option.checked = false;
            const QImage off = render_menu_item(*menu.style(), option, menu);
            option.checked = true;
            const QImage on = render_menu_item(*menu.style(), option, menu);
            const QRect marker_ink = changed_pixels(off, on);
            ASSERT_FALSE(marker_ink.isEmpty());
            option.checked = false;
            option.text = label_without_shortcut_or_mnemonic(option.text);
            const QImage label = render_menu_item(*menu.style(), option, menu);
            option.text.clear();
            const QImage blank = render_menu_item(*menu.style(), option, menu);
            const QRect label_ink = changed_pixels(blank, label);
            ASSERT_FALSE(label_ink.isEmpty());
            EXPECT_NEAR(QRectF(label_ink).center().y(),
                        QRectF(option.rect).center().y(), 1.0)
                << "the visible label is vertically centered within its menu row";
            EXPECT_NEAR(QRectF(marker_ink).center().y(),
                        QRectF(label_ink).center().y(), 1.0)
                << "the choice marker aligns with the visible label, excluding its shortcut";
          }
        }
        const auto capture = [&](const char *state) {
          if (!review_directory.isEmpty()) {
            EXPECT_TRUE(menu.grab().save(QDir(review_directory).filePath(
                QStringLiteral("%1-menu-alignment-%2-%3-%4.png")
                    .arg(QString::fromLatin1(menu_case.name),
                         QString::fromLatin1(scale.name),
                         direction == Qt::LeftToRight ? QStringLiteral("ltr")
                                                      : QStringLiteral("rtl"),
                         QString::fromLatin1(state)))));
          }
        };
        menu.setActiveAction(nullptr);
        capture("rest");
        menu.setActiveAction(actions.front());
        capture("hover");
        actions.front()->setEnabled(false);
        capture("disabled-current");
        actions.front()->setEnabled(true);
        menu.close();
      }
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
      QAction *unchecked = menu.addAction(QStringLiteral("Show Wireframe"));
      unchecked->setCheckable(true);
      QAction *disabled = menu.addAction(QStringLiteral("Unavailable operation"));
      disabled->setCheckable(true);
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
      EXPECT_EQ(menu.activeAction(), unchecked);
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

      int toggle_count = 0;
      QObject::connect(unchecked, &QAction::toggled, &menu,
                       [&toggle_count]() { ++toggle_count; });
      menu.setActiveAction(unchecked);
      send_menu_key(menu, Qt::Key_Return);
      EXPECT_TRUE(unchecked->isChecked());
      EXPECT_EQ(toggle_count, 1);
      EXPECT_FALSE(menu.isVisible());
      menu.popup(QPoint(20, 20));
      QCoreApplication::processEvents();
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
       FlatSectionHeadersFillTheirWidthWithOnePhysicalDivider) {
  quader::QtPanelSectionHeader header;
  header.setAttribute(Qt::WA_DontShowOnScreen, true);
  header.setText(QStringLiteral("Transform"));
  header.setArrowType(Qt::DownArrow);
  EXPECT_EQ(header.presentation(),
            quader::QtPanelSectionHeader::Presentation::Rounded);
  header.set_presentation(quader::QtPanelSectionHeader::Presentation::Flat);
  const QFont base_font = header.font();
  const auto render_header = [&]() {
    const qreal ratio = header.devicePixelRatioF();
    QImage image(header.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    header.render(&image);
    return image;
  };
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    header.setFont(metrics.font(base_font));
    header.apply_ui_scale(metrics);
    header.resize(metrics.px(320), header.sizeHint().height());
    EXPECT_GE(header.height(), metrics.px(30));
    EXPECT_GE(header.height(), header.fontMetrics().height() + metrics.px(12));
    EXPECT_EQ(header.content_rect(), header.rect());
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      header.setLayoutDirection(direction);
      for (const bool enabled : {true, false}) {
        header.setEnabled(enabled);
        const auto group = enabled ? QPalette::Inactive : QPalette::Disabled;
        const QColor surface = previous_flat_header_hover_color(header.palette(),
                                                                group);
        const QColor divider = header.palette().color(group, QPalette::Mid);
        for (const bool expanded : {true, false}) {
          SCOPED_TRACE(::testing::Message()
                       << factor << ' ' << direction << ' ' << enabled << ' '
                       << expanded);
          header.setChecked(expanded);
          header.setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
          const QImage image = render_header();
          for (const int x : {0, image.width() / 2, image.width() - 1}) {
            EXPECT_EQ(image.pixelColor(x, 0), surface);
            EXPECT_EQ(image.pixelColor(x, image.height() - 2), surface);
            EXPECT_EQ(image.pixelColor(x, image.height() - 1), divider);
          }
          EXPECT_EQ(image.pixelColor(image.width() - 1, image.height() / 2),
                    surface);
        }
      }
    }
  }
  header.setEnabled(true);
  const QImage flat = render_header();
  header.setAttribute(Qt::WA_UnderMouse, true);
  const QImage hovered = render_header();
  EXPECT_NE(hovered, flat);
  header.setDown(true);
  EXPECT_NE(render_header(), hovered);
  header.setDown(false);
  header.setAttribute(Qt::WA_UnderMouse, false);
  header.set_presentation(quader::QtPanelSectionHeader::Presentation::Rounded);
  EXPECT_NE(render_header(), flat)
      << "other section owners retain the existing rounded presentation";
}

TEST_F(QtApplicationStyleTests,
       FlatSectionLabelsUseContentEmphasisWithinTheDockHierarchy) {
  quader::QtPanelSectionHeader header;
  header.setAttribute(Qt::WA_DontShowOnScreen, true);
  header.set_presentation(quader::QtPanelSectionHeader::Presentation::Flat);
  header.setArrowType(Qt::DownArrow);
  header.setChecked(true);
  const QFont base_font = header.font();
  const auto render_header = [&]() {
    const qreal ratio = header.devicePixelRatioF();
    QImage image(header.size() * ratio, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(ratio);
    image.fill(Qt::transparent);
    header.render(&image);
    return image;
  };
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    header.setFont(metrics.font(base_font));
    header.apply_ui_scale(metrics);
    header.resize(metrics.px(320), header.sizeHint().height());
    const QColor surface = previous_flat_header_hover_color(
        header.palette(), QPalette::Inactive);
    const QColor foreground = header.palette().color(QPalette::Inactive,
                                                     QPalette::Text);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      SCOPED_TRACE(::testing::Message() << factor << ' ' << direction);
      header.setLayoutDirection(direction);
      header.setText(QStringLiteral(" "));
      const QImage blank = render_header();
      header.setText(QStringLiteral("Transform"));
      const QImage actual = render_header();
      const QRect actual_ink = changed_pixels(blank, actual);
      ASSERT_FALSE(actual_ink.isEmpty());

      // Use the native label renderer for both images; its text raster can
      // differ from raw QPainter text even with the same font and palette.
      QFont reference_font = header.font();
      reference_font.setWeight(QFont::Medium);
      QStyleOptionToolButton label;
      label.initFrom(&header);
      label.rect = header.rect().adjusted(metrics.px(8), 0, -metrics.px(8), 0);
      label.state = QStyle::State_Enabled | QStyle::State_Raised;
      label.font = reference_font;
      label.fontMetrics = QFontMetrics(reference_font);
      label.iconSize = header.iconSize();
      label.toolButtonStyle = Qt::ToolButtonTextBesideIcon;
      label.arrowType = Qt::DownArrow;
      label.features = QStyleOptionToolButton::Arrow;
      for (const auto group : {QPalette::Active, QPalette::Inactive,
                              QPalette::Disabled}) {
        label.palette.setColor(group, QPalette::ButtonText,
                               header.palette().color(group, QPalette::Text));
      }
      const auto render_reference = [&](const QString &text) {
        QImage image(actual.size(), QImage::Format_ARGB32_Premultiplied);
        image.setDevicePixelRatio(actual.devicePixelRatio());
        image.fill(surface);
        QPainter painter(&image);
        painter.setFont(reference_font);
        painter.setLayoutDirection(header.layoutDirection());
        label.text = text;
        header.style()->drawControl(QStyle::CE_ToolButtonLabel, &label,
                                     &painter, &header);
        return image;
      };
      const QImage reference_blank = render_reference(QStringLiteral(" "));
      const QImage reference = render_reference(header.text());
      const QRect reference_ink = changed_pixels(reference_blank, reference);
      ASSERT_FALSE(reference_ink.isEmpty());
      EXPECT_EQ(actual.copy(actual_ink), reference.copy(reference_ink));

      QDockWidget dock;
      dock.setFont(header.font());
      QStyleOptionDockWidget option;
      option.rect = header.rect();
      option.palette = header.palette();
      option.state = QStyle::State_Enabled;
      option.direction = direction;
      option.title = QStringLiteral("Inspector");
      const QImage title = render_dock_title(style, option, dock);
      const QColor title_surface = option.palette.color(QPalette::Inactive,
                                                        QPalette::AlternateBase);
      const QColor title_foreground = option.palette.color(QPalette::Inactive,
                                                           QPalette::Text);
      EXPECT_TRUE(contains_color(title, title_surface));
      EXPECT_TRUE(contains_color(title, title_foreground));
      EXPECT_NE(surface, header.palette().color(QPalette::Inactive,
                                                QPalette::Base));
      EXPECT_EQ(foreground, title_foreground);
    }
  }
}

TEST_F(QtApplicationStyleTests,
       FlatSectionHeadersPreserveNativeLightAndHighContrastPainting) {
  quader::QtPanelSectionHeader header;
  header.setAttribute(Qt::WA_DontShowOnScreen, true);
  header.setText(QStringLiteral("Visibility"));
  header.setArrowType(Qt::DownArrow);
  header.setChecked(true);
  header.resize(320, header.sizeHint().height());
  const QPalette light_palette = QApplication::style()->standardPalette();
  for (const bool high_contrast : {false, true}) {
    SCOPED_TRACE(high_contrast);
    if (high_contrast) {
      QApplication::setPalette(QPalette{});
      header.setPalette(QPalette{});
    } else {
      header.setPalette(light_palette);
    }
    QStyleOption option;
    option.initFrom(&header);
    ASSERT_FALSE(quader::qt_application_style_active(&option));
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      header.setLayoutDirection(direction);
      for (const bool enabled : {true, false}) {
        header.setEnabled(enabled);
        QImage rounded(header.size(), QImage::Format_ARGB32_Premultiplied);
        rounded.fill(Qt::transparent);
        header.set_presentation(quader::QtPanelSectionHeader::Presentation::Rounded);
        header.render(&rounded);
        QImage flat(header.size(), QImage::Format_ARGB32_Premultiplied);
        flat.fill(Qt::transparent);
        header.set_presentation(quader::QtPanelSectionHeader::Presentation::Flat);
        header.render(&flat);
        EXPECT_EQ(flat, rounded);
      }
    }
  }
}

TEST_F(QtApplicationStyleTests,
       FlatDockTitlesFillTheHeaderAcrossStateScaleAndDirection) {
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
          const QColor surface = option.palette.color(group, QPalette::AlternateBase);
          EXPECT_TRUE(contains_color(title, surface));
          EXPECT_TRUE(contains_color(title,
                                     option.palette.color(group, QPalette::Text)));
          EXPECT_TRUE(contains_color(title,
                                     option.palette.color(group, QPalette::Mid)));
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
          EXPECT_EQ(normalized.pixelColor(normalized.width() * 3 / 4,
                                           normalized.height() / 2), surface);
          EXPECT_EQ(normalized.pixelColor(normalized.width() - metrics.px(3),
                                           normalized.height() / 4), surface);
          EXPECT_EQ(normalized.pixelColor(normalized.width() / 2,
                                           normalized.height() - 1),
                    option.palette.color(group, QPalette::Mid));
          EXPECT_EQ(normalized.pixelColor(normalized.width() / 2,
                                           normalized.height() - 2), surface)
              << "the header divider stays one physical pixel at either scale";
          QStyleOptionDockWidget empty = option;
          empty.title.clear();
          EXPECT_NE(title, render_dock_title(style, empty, dock));
        }
      }
    }
  }
}

TEST_F(QtApplicationStyleTests,
       AuthoredPanelIconsLeadNativeTitlesAcrossScaleDirectionAndContrast) {
  const QPalette fusion_palette = QApplication::style()->standardPalette();
  quader::apply_qt_obsidian_application_theme(fusion_palette);
  quader::QtUiScaleApplicationController controller(*application_);
  QApplication::setWindowIcon(menu_state_icon());
  QMainWindow window;
  window.setAttribute(Qt::WA_DontShowOnScreen, true);
  window.setCentralWidget(new QWidget(&window));
  constexpr std::array<std::pair<const char *, const char *>, 4> kPanels{{
      {"Outliner", ":/quader/icons/outliner_icon.png"},
      {"Commands Library", ":/quader/icons/command_library_icon.png"},
      {"Tool Settings", ":/quader/icons/tool_settings_icon.png"},
      {"Inspector", ":/quader/icons/inspector_icon.png"}}};
  std::array<std::unique_ptr<InspectableDock>, kPanels.size()> docks;
  InspectableDock unassigned(QStringLiteral("Console"), &window);
  quader::install_palette_aware_dock_title_presentation(unassigned);
  unassigned.hide();
  EXPECT_FALSE(unassigned.windowIcon().isNull());
  EXPECT_TRUE(quader::qt_dock_title_panel_icon(unassigned, unassigned.palette(),
                  QPalette::Active).isNull());
  for (std::size_t index = 0; index < docks.size(); ++index) {
    docks[index] = std::make_unique<InspectableDock>(
        QString::fromUtf8(kPanels[index].first), &window);
    auto &dock = *docks[index];
    quader::install_palette_aware_dock_title_presentation(dock);
    quader::set_palette_aware_dock_title_icon(dock, QString::fromUtf8(kPanels[index].second));
    dock.setWidget(new QWidget(&dock));
    window.addDockWidget(Qt::LeftDockWidgetArea, &dock);
    if (index > 0) window.splitDockWidget(docks[index - 1].get(), &dock, Qt::Vertical);
    ASSERT_FALSE(dock.windowIcon().isNull());
  }
  const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!directory.isEmpty()) ASSERT_TRUE(QDir().mkpath(directory));
  const auto settle_and_capture = [&](const QString &name) {
    window.show();
    for (int pass = 0; pass < 3; ++pass) {
      QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
      QApplication::processEvents();
      window.layout()->activate();
    }
    EXPECT_FALSE(unassigned.isVisible());
    if (!directory.isEmpty()) EXPECT_TRUE(window.grab().save(QDir(directory).filePath(name)));
  };
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    const auto &metrics = controller.metrics();
    window.resize(metrics.size(QSize(300, 360)));
    for (auto &dock : docks) {
      quader::apply_ui_scale_to_palette_aware_dock_title_presentation(*dock, metrics);
    }
    window.resizeDocks({docks.front().get()}, {metrics.px(200)}, Qt::Horizontal);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      window.setLayoutDirection(direction);
      settle_and_capture(QStringLiteral("panel-title-icons-%1%2.png")
          .arg(qRound(factor * 100)).arg(direction == Qt::RightToLeft ? "-rtl" : ""));
      for (auto &dock : docks) {
        for (const bool vertical : {false, true}) {
          SCOPED_TRACE(::testing::Message() << dock->windowTitle().toStdString()
                       << ' ' << factor << ' ' << direction << ' ' << vertical);
          auto option = dock->title_option();
          option.rect = QRect(QPoint{}, metrics.size(vertical ? QSize(32, 200) : QSize(200, 32)));
          option.state = kEnabledState;
          option.verticalTitleBar = vertical;
          option.palette.setColor(QPalette::Active, QPalette::WindowText, QColor(123, 162, 201));
          const auto normalize = [&](QImage image) {
            if (vertical) image = image.transformed(QTransform().rotate(90));
            return direction == Qt::RightToLeft ? image.flipped(Qt::Horizontal) : image;
          };
          const QImage complete = normalize(render_dock_title(controller.style(), option, *dock));
          auto empty = option;
          empty.title.clear();
          const QImage icon_only = normalize(render_dock_title(controller.style(), empty, *dock));
          const QImage blank = normalize(render_dock_title(controller.style(), empty, unassigned));
          const QRect icon = changed_pixels(blank, icon_only);
          const QRect text = changed_pixels(icon_only, complete);
          ASSERT_FALSE(icon.isEmpty());
          ASSERT_FALSE(text.isEmpty());
          EXPECT_LT(icon.right(), text.left());
          EXPECT_LE(icon.width(), metrics.px(16));
          EXPECT_LE(icon.height(), metrics.px(16));
          EXPECT_NEAR(QRectF(icon).center().y(), complete.height() * 0.5, metrics.px(2) + 1);
          EXPECT_NEAR(QRectF(text).center().y(), QRectF(icon).center().y(), metrics.px(3));
          EXPECT_FALSE(contains_color(blank, QColor(225, 95, 75)));
          auto roomy = option;
          if (vertical) roomy.rect.setHeight(metrics.px(400));
          else roomy.rect.setWidth(metrics.px(400));
          auto roomy_empty = roomy;
          roomy_empty.title.clear();
          EXPECT_EQ(text.width(), changed_pixels(
              normalize(render_dock_title(controller.style(), roomy_empty, *dock)),
              normalize(render_dock_title(controller.style(), roomy, *dock))).width());
        }
      }
    }
  }
  window.setLayoutDirection(Qt::LeftToRight);
  for (const bool high_contrast : {false, true}) {
    quader::apply_qt_application_theme_for_contrast_preference(fusion_palette,
        high_contrast ? Qt::ContrastPreference::HighContrast : Qt::ContrastPreference::NoPreference);
    const QPalette palette = high_contrast ? QPalette{} : QPalette(QColor(239, 239, 239));
    window.setPalette(palette);
    for (auto &dock : docks) {
      dock->setPalette(palette);
      auto option = dock->title_option();
      ASSERT_FALSE(quader::qt_application_style_active(&option));
      const auto group = option.state.testFlag(QStyle::State_Active)
          ? QPalette::Active : QPalette::Inactive;
      EXPECT_GE(contrast_ratio(option.palette.color(group, QPalette::WindowText),
          window.palette().color(group, QPalette::Window)), 4.5);
      auto empty = option;
      empty.title.clear();
      EXPECT_FALSE(changed_pixels(render_dock_title(controller.style(), empty, *dock),
          render_dock_title(controller.style(), option, *dock)).isEmpty());
    }
    settle_and_capture(QStringLiteral("panel-title-icons-%1.png").arg(high_contrast ? "high-contrast" : "light"));
    const auto image = window.grab().toImage();
    const QPointF sample = window.centralWidget()->geometry().center() * image.devicePixelRatio();
    EXPECT_EQ(image.pixelColor(sample.toPoint()),
        window.palette().color(QPalette::Window));
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
       FlatDocksKeepNativeLayoutBesideRetainedSectionsAtNarrowAndScaledWidths) {
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
        QStringLiteral("flat-header-review.section"), body);
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
        EXPECT_EQ(window.style()->pixelMetric(
                      QStyle::PM_DockWidgetSeparatorExtent, nullptr, &window), 1);
        const QRect viewport_rect = viewport->geometry();
        for (const auto *dock : {&outliner, &face_texture}) {
          const QRect dock_rect = dock->geometry();
          const int gap = std::max(viewport_rect.left(), dock_rect.left()) -
                          std::min(viewport_rect.right(), dock_rect.right()) - 1;
          EXPECT_EQ(gap, 1) << "panels meet the viewport across one divider";
        }
        EXPECT_EQ(inspector.geometry().top() - outliner.geometry().bottom() - 1, 1);
        if (!narrow && direction == Qt::LeftToRight) {
          const int original_width = outliner.width();
          const int original_right_width = face_texture.width();
          const QPoint start(outliner.geometry().right() + 1,
                             outliner.geometry().center().y());
          const QPoint end = start + QPoint(metrics.px(12), 0);
          const auto send_resize_event = [&](QEvent::Type type, QPoint position,
                                               Qt::MouseButton button,
                                               Qt::MouseButtons buttons) {
            QMouseEvent event(type, QPointF(position),
                              QPointF(window.mapToGlobal(position)),
                              button, buttons, Qt::NoModifier);
            QApplication::sendEvent(&window, &event);
          };
          send_resize_event(QEvent::MouseMove, start, Qt::NoButton, Qt::NoButton);
          send_resize_event(QEvent::MouseButtonPress, start,
                            Qt::LeftButton, Qt::LeftButton);
          send_resize_event(QEvent::MouseMove, end, Qt::NoButton, Qt::LeftButton);
          // Native dock drags apply the coalesced move on Qt's timer turn.
          settle();
          send_resize_event(QEvent::MouseButtonRelease, end,
                            Qt::LeftButton, Qt::NoButton);
          settle();
          EXPECT_GT(outliner.width(), original_width)
              << "the thin divider retains native dock resizing";
          window.resizeDocks({&outliner, &face_texture},
                             {original_width, original_right_width}, Qt::Horizontal);
          settle();
        }
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
        capture(QStringLiteral("flat-docks-%1-%2-%3-%4")
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
          capture(QStringLiteral("flat-docks-%1-%2-%3-long-title")
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
    std::unique_ptr<QStyle> native(QStyleFactory::create(QStringLiteral("fusion")));
    ASSERT_NE(native, nullptr);
    EXPECT_EQ(window.style()->pixelMetric(
                  QStyle::PM_DockWidgetSeparatorExtent, nullptr, &window),
              native->pixelMetric(QStyle::PM_DockWidgetSeparatorExtent,
                                   nullptr, &window));
    for (InspectableDock *dock : docks) {
      const auto option = dock->title_option();
      EXPECT_FALSE(quader::qt_application_style_active(&option));
    }
    capture(QStringLiteral("flat-docks-fallback-%1")
                .arg(high_contrast ? "high-contrast" : "light"));
  }
}

TEST_F(QtApplicationStyleTests,
       FlatDockAndSceneTabsKeepNativeSelectionAcrossScaleDirectionAndPosition) {
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
      for (const auto position : {QTabWidget::North, QTabWidget::South,
                                  QTabWidget::West, QTabWidget::East}) {
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
        const QImage dock_tab = render_tab_shape(*tabs->style(), option, *tabs);
        const QImage ordinary = render_tab_shape(*tabs->style(), option,
                                                 ordinary_tabs);
        EXPECT_TRUE(contains_color(dock_tab, option.palette.color(QPalette::Accent)));
        EXPECT_TRUE(contains_color(ordinary, option.palette.color(QPalette::Accent)));
        EXPECT_EQ(dock_tab, ordinary);
        QImage normalized = dock_tab;
        switch (position) {
        case QTabWidget::South:
          normalized = normalized.flipped(Qt::Vertical);
          break;
        case QTabWidget::West:
          normalized = normalized.transformed(QTransform().rotate(90));
          break;
        case QTabWidget::East:
          normalized = normalized.transformed(QTransform().rotate(-90));
          break;
        default:
          break;
        }
        if (direction == Qt::RightToLeft) {
          normalized = normalized.flipped(Qt::Horizontal);
        }
        EXPECT_EQ(normalized.pixelColor(normalized.width() - metrics.px(3),
                                         normalized.height() / 4),
                  option.palette.color(QPalette::AlternateBase));
        EXPECT_EQ(normalized.pixelColor(metrics.px(3), normalized.height() / 4),
                  option.palette.color(QPalette::AlternateBase));
        EXPECT_EQ(normalized.pixelColor(normalized.width() - 1,
                                         normalized.height() / 2),
                  option.palette.color(QPalette::Mid));
        EXPECT_EQ(normalized.pixelColor(normalized.width() - 2,
                                         normalized.height() / 2),
                  option.palette.color(QPalette::AlternateBase))
            << "tab dividers stay one physical pixel at either scale";
        EXPECT_EQ(normalized.pixelColor(normalized.width() / 2,
                                         normalized.height() - 1),
                  option.palette.color(QPalette::Accent));
        EXPECT_EQ(normalized.pixelColor(normalized.width() / 2,
                                         normalized.height() - metrics.px(2)),
                  option.palette.color(QPalette::Accent));
        EXPECT_EQ(normalized.pixelColor(normalized.width() / 2,
                                         normalized.height() - metrics.px(2) - 1),
                  option.palette.color(QPalette::AlternateBase));
        option.shape = QTabBar::RoundedNorth;
        option.rect = QRect(QPoint{}, metrics.size(QSize(160, 32)));
        scene_tabs.resize(option.rect.size());
        const QImage connected = render_tab_shape(*tabs->style(), option,
                                                  scene_tabs);
        EXPECT_EQ(connected.pixelColor(metrics.px(3), connected.height() / 4),
                  option.palette.color(QPalette::AlternateBase));
        EXPECT_EQ(connected.pixelColor(connected.width() - metrics.px(3),
                                        connected.height() / 4),
                  option.palette.color(QPalette::AlternateBase));
        EXPECT_EQ(connected.pixelColor(connected.width() / 2,
                                        connected.height() / 4),
                  option.palette.color(QPalette::AlternateBase));
        if (!review_directory.isEmpty()) {
          EXPECT_TRUE(window.grab().save(QDir(review_directory).filePath(
              QStringLiteral("flat-tabified-%1-%2-%3.png")
                  .arg(qRound(factor * 100.0F))
                  .arg(direction == Qt::LeftToRight ? "ltr" : "rtl")
                  .arg(position == QTabWidget::North ? "north"
                       : position == QTabWidget::South ? "south"
                       : position == QTabWidget::West ? "west" : "east"))));
        }
      }
    }
  }
}

TEST_F(QtApplicationStyleTests,
       FlatTabFacesKeepSelectedHoverInactiveAndDisabledPaletteStates) {
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    QTabBar tabs;
    QTabBar scene_tabs;
    scene_tabs.setProperty(quader::kQtConnectedTabSurfaceProperty, true);
    for (const auto group : {QPalette::Active, QPalette::Inactive,
                            QPalette::Disabled}) {
      for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
        for (const bool selected : {false, true}) {
          for (const bool hover : {false, true}) {
            SCOPED_TRACE(::testing::Message()
                         << factor << ' ' << group << ' ' << direction << ' '
                         << selected << ' ' << hover);
            QStyleOptionTab option;
            option.rect = QRect(QPoint{}, metrics.size(QSize(160, 32)));
            option.palette = QApplication::palette();
            option.palette.setCurrentColorGroup(group);
            option.direction = direction;
            option.shape = QTabBar::RoundedNorth;
            option.state = group == QPalette::Disabled ? QStyle::State_None
                                                        : QStyle::State_Enabled;
            option.state.setFlag(QStyle::State_Active, group == QPalette::Active);
            option.state.setFlag(QStyle::State_Selected, selected);
            option.state.setFlag(QStyle::State_MouseOver, hover);
            scene_tabs.resize(option.rect.size());
            const QImage image = render_tab_shape(style, option, tabs);
            EXPECT_EQ(image, render_tab_shape(style, option, scene_tabs));
            const QPalette::ColorRole surface_role =
                selected ? QPalette::AlternateBase
                         : hover && group != QPalette::Disabled
                               ? QPalette::Button : QPalette::Window;
            const QColor surface = option.palette.color(group, surface_role);
            EXPECT_EQ(image.pixelColor(image.width() / 2, image.height() / 2),
                      surface);
            EXPECT_EQ(image.pixelColor(metrics.px(3), image.height() / 4), surface);
            EXPECT_EQ(image.pixelColor(image.width() - metrics.px(3),
                                        image.height() / 4), surface);
            EXPECT_EQ(image.pixelColor(image.width() / 2, image.height() - 1),
                      option.palette.color(group, selected ? QPalette::Accent
                                                            : QPalette::Mid));
            EXPECT_EQ(contains_color(image, option.palette.color(group,
                                                                  QPalette::Accent)),
                      selected);
          }
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

TEST_F(QtApplicationStyleTests,
       MicroDotSliderTracksNativeHandlesAcrossScaleDirectionAndRanges) {
  quader::QtUiScaleApplicationController controller(*application_);
  InspectableSlider slider;
  slider.setRange(0, 100);
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    const auto &metrics = controller.metrics();
    for (const auto orientation : {Qt::Horizontal, Qt::Vertical}) {
      slider.setOrientation(orientation);
      for (const int length : {64, 240}) {
        slider.resize(metrics.size(orientation == Qt::Horizontal
                                       ? QSize(length, 32)
                                       : QSize(32, length)));
        for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
          slider.setLayoutDirection(direction);
          for (const bool inverted : {false, true}) {
            slider.setInvertedAppearance(inverted);
            for (const int value : {0, 37, 100}) {
              slider.setValue(value);
              auto option = slider.slider_option();
              option.state = kEnabledState;
              option.subControls = QStyle::SC_SliderHandle;
              const QRect handle = slider.style()->subControlRect(
                  QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider);
              EXPECT_EQ(slider.style()->hitTestComplexControl(
                            QStyle::CC_Slider, &option, handle.center(), &slider),
                        QStyle::SC_SliderHandle);
              auto complete = option;
              complete.subControls = QStyle::SC_All;
              EXPECT_EQ(contains_color(
                            render_slider(*slider.style(), complete, slider),
                            complete.palette.color(QPalette::Active,
                                                   QPalette::Accent)),
                        value != slider.minimum())
                  << "the accented rail represents progress from the minimum";
              for (const qreal ratio : {1.0, 1.5, 2.0}) {
                SCOPED_TRACE(::testing::Message()
                             << factor << ' ' << orientation << ' ' << length
                             << ' ' << direction << ' ' << inverted << ' '
                             << value << ' ' << ratio);
                const QImage image = render_slider(*slider.style(), option,
                                                    slider, ratio);
                QImage empty(image.size(), image.format());
                empty.fill(Qt::transparent);
                const QRect dot = changed_pixels(image, empty);
                ASSERT_FALSE(dot.isEmpty());
                const qreal expected_diameter = metrics.px(8) * ratio;
                EXPECT_NEAR(dot.width(), expected_diameter, 2.0);
                EXPECT_NEAR(dot.height(), expected_diameter, 2.0);
                EXPECT_NEAR((dot.left() + dot.right() + 1) / (2.0 * ratio),
                            QRectF(handle).center().x(), 1.0);
                EXPECT_NEAR((dot.top() + dot.bottom() + 1) / (2.0 * ratio),
                            QRectF(handle).center().y(), 1.0);
                EXPECT_TRUE(image.rect().contains(dot));
              }
            }
          }
        }
      }
    }
    slider.setOrientation(Qt::Horizontal);
    slider.resize(metrics.size(QSize(240, 32)));
    slider.setRange(7, 7);
    auto option = slider.slider_option();
    option.state = kEnabledState;
    option.subControls = QStyle::SC_All;
    const QImage fixed_range = render_slider(*slider.style(), option, slider);
    QImage empty(fixed_range.size(), fixed_range.format());
    empty.fill(Qt::transparent);
    const QRect rail = changed_pixels(fixed_range, empty);
    EXPECT_GT(rail.width(), metrics.px(200));
    EXPECT_LE(rail.height(), metrics.px(10));
    const QRect handle = slider.style()->subControlRect(
        QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider);
    EXPECT_GT(fixed_range.pixelColor(handle.center()).alpha(), 0);
    slider.setRange(0, 100);
  }
}

TEST_F(QtApplicationStyleTests,
       MicroDotSliderUsesAuthoredPaletteStatesAndKeepsNativeTicks) {
  quader::QtUiScaleProxyStyle style;
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  InspectableSlider slider(Qt::Horizontal);
  slider.resize(240, 32);
  slider.setRange(0, 100);
  slider.setValue(40);
  auto option = slider.slider_option();
  option.state = kEnabledState;
  option.subControls = QStyle::SC_All;
  option.palette.setColor(QPalette::Active, QPalette::Text, QColor(228, 231, 239));
  option.palette.setColor(QPalette::Inactive, QPalette::Text, QColor(180, 210, 220));
  option.palette.setColor(QPalette::Disabled, QPalette::Text, QColor(105, 110, 120));
  option.palette.setColor(QPalette::Active, QPalette::Accent, QColor(173, 95, 225));
  const QRect handle = style.subControlRect(
      QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider);
  for (const auto group : {QPalette::Active, QPalette::Inactive,
                           QPalette::Disabled}) {
    option.state = group == QPalette::Disabled ? QStyle::State_None
                                               : QStyle::State_Enabled;
    option.state.setFlag(QStyle::State_Active, group == QPalette::Active);
    option.palette.setCurrentColorGroup(group);
    const QImage resting = render_slider(style, option, slider);
    EXPECT_EQ(resting.pixelColor(handle.center()),
              option.palette.color(group, QPalette::Text));
  }
  option.state = kEnabledState;
  option.palette.setCurrentColorGroup(QPalette::Active);
  option.subControls = QStyle::SC_SliderHandle;
  const QImage resting = render_slider(style, option, slider);
  QImage empty(resting.size(), resting.format());
  empty.fill(Qt::transparent);
  const QRect resting_bounds = changed_pixels(resting, empty);
  for (const auto state : {QStyle::State_MouseOver, QStyle::State_Sunken}) {
    option.state = kEnabledState | state;
    option.activeSubControls = QStyle::SC_SliderHandle;
    const QImage interaction = render_slider(style, option, slider);
    EXPECT_EQ(changed_pixels(interaction, empty), resting_bounds)
        << "hover and press must not enlarge the dot or shift its position";
    EXPECT_NE(interaction, resting);
  }
  option.state = kEnabledState | QStyle::State_HasFocus;
  const QImage focused = render_slider(style, option, slider);
  EXPECT_NE(focused, resting);
  const QRect focused_bounds = changed_pixels(focused, empty);
  EXPECT_TRUE(focused_bounds.contains(resting_bounds));
  EXPECT_GT(focused_bounds.width(), resting_bounds.width());
  EXPECT_GT(focused_bounds.height(), resting_bounds.height());
  option.palette.setColor(QPalette::Active, QPalette::Accent,
                           QColor(50, 190, 165));
  EXPECT_NE(focused, render_slider(style, option, slider))
      << "the antialiased focus ring follows the live accent color";

  for (const auto orientation : {Qt::Horizontal, Qt::Vertical}) {
    slider.setOrientation(orientation);
    slider.resize(orientation == Qt::Horizontal ? QSize(240, 48)
                                               : QSize(48, 240));
    slider.setTickPosition(QSlider::TicksBothSides);
    slider.setTickInterval(10);
    option = slider.slider_option();
    option.state = kEnabledState;
    option.subControls = QStyle::SC_SliderTickmarks;
    EXPECT_EQ(render_slider(style, option, slider),
              render_slider(*fusion, option, slider));
  }
}

TEST_F(QtApplicationStyleTests,
       MicroDotSliderRetainsNativePointerKeyboardAndAccessibleValues) {
  quader::QtUiScaleApplicationController controller(*application_);
  InspectableSlider slider(Qt::Horizontal);
  slider.setAttribute(Qt::WA_DontShowOnScreen, true);
  slider.setAccessibleName(QStringLiteral("Fly Speed"));
  slider.setRange(0, 100);
  slider.setSingleStep(1);
  slider.setPageStep(10);
  slider.resize(240, 32);
  slider.show();
  QCoreApplication::processEvents();
  auto *accessible = QAccessible::queryAccessibleInterface(&slider);
  ASSERT_NE(accessible, nullptr);
  EXPECT_EQ(accessible->role(), QAccessible::Slider);
  EXPECT_EQ(accessible->text(QAccessible::Name), QStringLiteral("Fly Speed"));
  auto *accessible_value = accessible->valueInterface();
  ASSERT_NE(accessible_value, nullptr);
  EXPECT_EQ(accessible_value->minimumValue().toInt(), 0);
  EXPECT_EQ(accessible_value->maximumValue().toInt(), 100);
  for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
    slider.setLayoutDirection(direction);
    for (const bool inverted : {false, true}) {
      slider.setInvertedAppearance(inverted);
      slider.setValue(40);
      auto option = slider.slider_option();
      const QRect handle = slider.style()->subControlRect(
          QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, &slider);
      send_slider_mouse(slider, QEvent::MouseButtonPress, handle.center(),
                        Qt::LeftButton, Qt::LeftButton);
      ASSERT_TRUE(slider.isSliderDown());
      send_slider_mouse(slider, QEvent::MouseMove,
                        QPoint(slider.width() - 1, handle.center().y()),
                        Qt::NoButton, Qt::LeftButton);
      EXPECT_EQ(slider.value(), option.upsideDown ? 0 : 100);
      send_slider_mouse(slider, QEvent::MouseButtonRelease,
                        QPoint(slider.width() - 1, handle.center().y()),
                        Qt::LeftButton, Qt::NoButton);
      EXPECT_FALSE(slider.isSliderDown());
      send_slider_key(slider, Qt::Key_Home);
      EXPECT_EQ(slider.value(), 0);
      send_slider_key(slider, Qt::Key_PageUp);
      EXPECT_EQ(slider.value(), 10);
      send_slider_key(slider, Qt::Key_End);
      EXPECT_EQ(slider.value(), 100);
      EXPECT_EQ(accessible_value->currentValue().toInt(), 100);
      accessible_value->setCurrentValue(25);
      EXPECT_EQ(slider.value(), 25);
    }
  }
  slider.setEnabled(false);
  EXPECT_TRUE(accessible->state().disabled);
}

TEST_F(QtApplicationStyleTests,
       MicroDotSliderNativeReviewIncludesCompactStatesAndPaletteFallbacks) {
  const QPalette fusion_palette = QApplication::style()->standardPalette();
  quader::apply_qt_obsidian_application_theme(fusion_palette);
  quader::QtUiScaleApplicationController controller(*application_);
  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
  QWidget panel;
  panel.setAttribute(Qt::WA_DontShowOnScreen, true);
  panel.setAutoFillBackground(true);
  panel.setBackgroundRole(QPalette::AlternateBase);
  auto *layout = new QVBoxLayout(&panel);
  auto *title = new QLabel(QStringLiteral("Micro dot sliders"), &panel);
  layout->addWidget(title);
  const std::array<QString, 7> labels{
      QStringLiteral("Normal"), QStringLiteral("Hover"),
      QStringLiteral("Pressed"), QStringLiteral("Keyboard focus"),
      QStringLiteral("Disabled"), QStringLiteral("Minimum"),
      QStringLiteral("Maximum")};
  std::array<InspectableSlider *, labels.size()> sliders{};
  for (std::size_t index = 0; index < labels.size(); ++index) {
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(labels[index], &panel));
    row->addStretch();
    row->addWidget(new QLabel(index == 5 ? QStringLiteral("0")
                             : index == 6 ? QStringLiteral("100")
                                          : QStringLiteral("25"), &panel));
    layout->addLayout(row);
    auto *slider = new InspectableSlider(Qt::Horizontal, &panel);
    slider->setRange(0, 100);
    slider->setValue(index == 5 ? 0 : index == 6 ? 100 : 25);
    slider->setAccessibleName(labels[index]);
    layout->addWidget(slider);
    sliders[index] = slider;
  }
  sliders[4]->setEnabled(false);
  layout->addWidget(new QLabel(QStringLiteral("Stepped / tick marks"), &panel));
  auto *ticked = new InspectableSlider(Qt::Horizontal, &panel);
  ticked->setRange(0, 10);
  ticked->setValue(4);
  ticked->setTickPosition(QSlider::TicksBelow);
  ticked->setTickInterval(1);
  layout->addWidget(ticked);
  auto *vertical_row = new QHBoxLayout;
  vertical_row->addWidget(new QLabel(QStringLiteral("Vertical"), &panel));
  auto *vertical = new InspectableSlider(Qt::Vertical, &panel);
  vertical->setRange(0, 100);
  vertical->setValue(50);
  vertical_row->addWidget(vertical);
  auto *vertical_ticked = new InspectableSlider(Qt::Vertical, &panel);
  vertical_ticked->setRange(0, 10);
  vertical_ticked->setValue(3);
  vertical_ticked->setTickPosition(QSlider::TicksBothSides);
  vertical_ticked->setTickInterval(1);
  vertical_row->addWidget(vertical_ticked);
  layout->addLayout(vertical_row);
  const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
  if (!directory.isEmpty()) {
    ASSERT_TRUE(QDir().mkpath(directory));
  }
  const auto capture = [&](const QString &name, int design_width) {
    const auto &metrics = controller.metrics();
    layout->setContentsMargins(metrics.margins(QMargins(12, 10, 12, 10)));
    layout->setSpacing(metrics.px(5));
    for (auto *slider : sliders) {
      slider->setFixedHeight(metrics.px(20));
      slider->setAttribute(Qt::WA_UnderMouse, false);
    }
    ticked->setFixedHeight(metrics.px(28));
    vertical->setFixedSize(metrics.size(QSize(32, 76)));
    vertical_ticked->setFixedSize(metrics.size(QSize(48, 76)));
    panel.resize(metrics.px(design_width), panel.sizeHint().height());
    panel.show();
    QApplication::setActiveWindow(&panel);
    QApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
    QApplication::processEvents();
    layout->activate();
    sliders[1]->setAttribute(Qt::WA_UnderMouse, true);
    const auto hover_option = sliders[1]->slider_option();
    const QPointF hover_position = sliders[1]->style()->subControlRect(
        QStyle::CC_Slider, &hover_option, QStyle::SC_SliderHandle,
        sliders[1]).center();
    QHoverEvent hover(QEvent::HoverMove, hover_position,
                       sliders[1]->mapToGlobal(hover_position.toPoint()),
                       QPointF(-1, -1));
    QApplication::sendEvent(sliders[1], &hover);
    EXPECT_TRUE(sliders[1]->slider_option().activeSubControls.testFlag(
        QStyle::SC_SliderHandle));
    const auto pressed_option = sliders[2]->slider_option();
    const QRect handle = sliders[2]->style()->subControlRect(
        QStyle::CC_Slider, &pressed_option, QStyle::SC_SliderHandle, sliders[2]);
    send_slider_mouse(*sliders[2], QEvent::MouseButtonPress, handle.center(),
                      Qt::LeftButton, Qt::LeftButton);
    sliders[3]->setFocus(Qt::TabFocusReason);
    EXPECT_TRUE(sliders[2]->isSliderDown());
    EXPECT_TRUE(sliders[2]->slider_option().state.testFlag(QStyle::State_Sunken));
    EXPECT_TRUE(sliders[3]->hasFocus());
    for (auto *slider : sliders) {
      const auto option = slider->slider_option();
      const QRect bounds = slider->style()->subControlRect(
          QStyle::CC_Slider, &option, QStyle::SC_SliderHandle, slider);
      EXPECT_TRUE(slider->rect().contains(bounds));
    }
    if (!directory.isEmpty()) {
      EXPECT_TRUE(panel.grab().save(QDir(directory).filePath(name +
                                                            QStringLiteral(".png"))));
    }
    send_slider_mouse(*sliders[2], QEvent::MouseButtonRelease, handle.center(),
                      Qt::LeftButton, Qt::NoButton);
  };
  int pass = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      panel.setLayoutDirection(direction);
      for (const int width : {340, 220}) {
        capture(QStringLiteral("micro-dot-sliders-%1-%2-%3-%4")
                    .arg(pass).arg(qRound(factor * 100.0F))
                    .arg(direction == Qt::LeftToRight ? "ltr" : "rtl")
                    .arg(width), width);
      }
    }
    ++pass;
  }
  QPalette light_palette(fusion_palette);
  light_palette.setColor(QPalette::Window, Qt::white);
  light_palette.setColor(QPalette::WindowText, Qt::black);
  light_palette.setColor(QPalette::Base, Qt::white);
  light_palette.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
  light_palette.setColor(QPalette::Text, Qt::black);
  light_palette.setColor(QPalette::Button, QColor(225, 225, 225));
  light_palette.setColor(QPalette::ButtonText, Qt::black);
  for (const bool high_contrast : {false, true}) {
    quader::apply_qt_application_theme_for_contrast_preference(
        fusion_palette, high_contrast ? Qt::ContrastPreference::HighContrast
                                     : Qt::ContrastPreference::NoPreference);
    panel.setPalette(high_contrast ? QPalette{} : light_palette);
    panel.setLayoutDirection(Qt::LeftToRight);
    for (auto *slider : sliders) {
      const auto option = slider->slider_option();
      EXPECT_FALSE(quader::qt_application_style_active(&option));
      EXPECT_EQ(render_slider(*slider->style(), option, *slider),
                render_slider(*fusion, option, *slider));
    }
    capture(QStringLiteral("micro-dot-sliders-%1")
                .arg(high_contrast ? "high-contrast" : "light"), 340);
  }
}
