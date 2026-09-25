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
#include <QMenu>
#include <QMainWindow>
#include <QPainter>
#include <QPixmap>
#include <QPushButton>
#include <QStyleFactory>
#include <QStyleOption>
#include <QTabBar>
#include <QToolButton>

#include <algorithm>
#include <array>
#include <cmath>
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

QImage render_primitive(const QStyle &style, QStyle::PrimitiveElement element,
                        const QStyleOption &option) {
  QImage image(option.rect.size(), QImage::Format_ARGB32_Premultiplied);
  image.fill(Qt::transparent);
  QPainter painter(&image);
  style.drawPrimitive(element, &option, &painter);
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
  style.drawControl(QStyle::CE_DockWidgetTitle, &option, &painter, &dock);
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
  QTabBar tabs;
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
  int scale_pass = 0;
  for (const float factor : {1.0F, 2.0F, 1.0F}) {
    controller.apply_factor(factor);
    for (const auto direction : {Qt::LeftToRight, Qt::RightToLeft}) {
      SCOPED_TRACE(::testing::Message() << factor << ' ' << direction);
      tabs.setLayoutDirection(direction);
      // Native tab-button placement refreshes on style changes, not direction
      // changes. Settle the fixture before inspecting its mirrored layout.
      QEvent style_change(QEvent::StyleChange);
      QApplication::sendEvent(&tabs, &style_change);
      tabs.resize(controller.metrics().px(300), tabs.sizeHint().height());
      tabs.show();
      QCoreApplication::processEvents();
      auto *close = qobject_cast<QAbstractButton *>(
          tabs.tabButton(1, QTabBar::RightSide));
      if (close == nullptr) {
        close = qobject_cast<QAbstractButton *>(
            tabs.tabButton(1, QTabBar::LeftSide));
      }
      ASSERT_NE(close, nullptr);
      EXPECT_TRUE(close->rect().isValid());
      EXPECT_TRUE(tabs.rect().contains(close->geometry()));
      EXPECT_TRUE(tabs.tabRect(1).contains(close->geometry()));

      QStyleOption option;
      option.initFrom(close);
      option.rect = close->rect();
      option.state = kEnabledState;
      const QImage idle = render_primitive(*tabs.style(), QStyle::PE_IndicatorTabClose,
                                            option);
      EXPECT_EQ(idle.pixelColor(0, 0).alpha(), 0);
      EXPECT_TRUE(contains_color(idle, option.palette.color(QPalette::Active,
                                                            QPalette::WindowText)));
      for (const auto state : {QStyle::State_MouseOver, QStyle::State_Sunken,
                                QStyle::State_Selected}) {
        option.state = kEnabledState | state;
        const QImage active = render_primitive(*tabs.style(),
                                               QStyle::PE_IndicatorTabClose, option);
        EXPECT_NE(idle, active);
        EXPECT_TRUE(contains_color(active, option.palette.color(QPalette::Active,
                                                                QPalette::Text)));
      }
      option.state = QStyle::State_Active;
      const QImage disabled = render_primitive(*tabs.style(),
                                               QStyle::PE_IndicatorTabClose, option);
      EXPECT_NE(idle, disabled);
      EXPECT_TRUE(contains_color(disabled, option.palette.color(QPalette::Disabled,
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

  std::unique_ptr<QStyle> fusion(QStyleFactory::create(QStringLiteral("fusion")));
  ASSERT_NE(fusion, nullptr);
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
      EXPECT_EQ(style.sizeFromContents(QStyle::CT_MenuItem, &option,
                                         QSize(150, 20), &menu),
                  fusion->sizeFromContents(QStyle::CT_MenuItem, &option,
                                             QSize(150, 20), &menu));
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
       DockTitlesUsePrimaryTextAndASeparateSurfaceAcrossStateScaleAndDirection) {
  for (const float factor : {1.0F, 2.0F}) {
    const quader::QtUiScaleMetrics metrics(factor);
    quader::QtUiScaleProxyStyle style(metrics);
    QDockWidget dock;
    QFont font = dock.font();
    font.setPointSizeF(10.0);
    dock.setFont(metrics.font(font));
    for (const auto group : {QPalette::Active, QPalette::Inactive}) {
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
          option.state = QStyle::State_Enabled |
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
          EXPECT_GE(contrast_ratio(option.palette.color(group, QPalette::Text),
                                   surface), 4.5);
          QStyleOptionDockWidget empty = option;
          empty.title.clear();
          EXPECT_NE(title, render_dock_title(style, empty, dock));
        }
      }
    }
  }
}

TEST_F(QtApplicationStyleTests,
       DockGlyphsSurviveNativeFeatureDirectionStyleAndFloatingChanges) {
  quader::QtUiScaleProxyStyle style;
  QMainWindow window;
  QDockWidget dock(QStringLiteral("Outliner"), &window);
  dock.setStyle(&style);
  dock.setWidget(new QWidget(&dock));
  window.addDockWidget(Qt::LeftDockWidgetArea, &dock);
  quader::install_palette_aware_dock_title_presentation(dock);
  window.resize(360, 260);
  window.show();
  QCoreApplication::processEvents();

  const auto expect_readable_buttons = [&dock, &style]() {
    for (const auto glyph : {quader::QtDockTitleGlyph::Close,
                             quader::QtDockTitleGlyph::Float}) {
      QAbstractButton *button = dock.findChild<QAbstractButton *>(
          glyph == quader::QtDockTitleGlyph::Close
              ? QStringLiteral("qt_dockwidget_closebutton")
              : QStringLiteral("qt_dockwidget_floatbutton"),
          Qt::FindDirectChildrenOnly);
      ASSERT_NE(button, nullptr);
      const QIcon expected = quader::qt_dock_title_glyph_icon(glyph, dock.palette());
      EXPECT_EQ(button->icon().pixmap(QSize(24, 24)).toImage(),
                expected.pixmap(QSize(24, 24)).toImage());
      EXPECT_FALSE(button->accessibleName().isEmpty());
      EXPECT_EQ(style.styleHint(QStyle::SH_DockWidget_ButtonsHaveFrame,
                                 nullptr, button), 0);
    }
  };
  expect_readable_buttons();
  dock.setFeatures(QDockWidget::DockWidgetClosable |
                   QDockWidget::DockWidgetFloatable);
  expect_readable_buttons();
  dock.setLayoutDirection(Qt::RightToLeft);
  expect_readable_buttons();
  QEvent style_change(QEvent::StyleChange);
  QApplication::sendEvent(&dock, &style_change);
  expect_readable_buttons();
  dock.setFloating(true);
  dock.setFloating(false);
  QCoreApplication::processEvents();
  expect_readable_buttons();
  window.close();
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
