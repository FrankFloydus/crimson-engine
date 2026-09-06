////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "ui/qt_application_theme.hpp"
#include "ui/qt_status_message.hpp"

#include <gtest/gtest.h>

#include <QApplication>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QLayout>
#include <QPalette>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>

#include <memory>

namespace {

class QtStatusMessageTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
  }

private:
  int argc_ = 1;
  char application_name_[28] = "quader-status-message-tests";
  char *argv_[2] = {application_name_, nullptr};
  std::unique_ptr<QApplication> application_;
};

TEST_F(QtStatusMessageTests, DisplaysPlainTextWithErrorContrastInEveryState) {
  quader::QtStatusMessage message;
  message.present(QStringLiteral("Cannot combine <face> & <face>"), true);

  EXPECT_EQ(message.text(),
            QStringLiteral("Cannot combine <face> & <face>"));
  EXPECT_EQ(message.textFormat(), Qt::PlainText);
  EXPECT_TRUE(message.is_error());
  EXPECT_TRUE(message.autoFillBackground());
  for (const QPalette::ColorGroup group :
       {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
    EXPECT_EQ(message.palette().color(group, message.backgroundRole()),
              QColor(179, 38, 30));
    EXPECT_EQ(message.palette().color(group, message.foregroundRole()),
              QColor(Qt::white));
  }
}

TEST_F(QtStatusMessageTests, PaintsErrorAndRestoresTheThemedStatusBarSurface) {
  std::unique_ptr<QStyle> fusion_style(
      QStyleFactory::create(QStringLiteral("Fusion")));
  ASSERT_NE(fusion_style, nullptr);
  QApplication::setStyle(fusion_style.release());
  QApplication::setPalette(quader::qt_obsidian_application_palette(
      QApplication::style()->standardPalette()));

  QStatusBar status_bar;
  status_bar.setSizeGripEnabled(false);
  quader::QtStatusMessage message(&status_bar);
  status_bar.addPermanentWidget(&message, 1);
  message.present(QStringLiteral("Cannot combine faces: invalid geometry"),
                  true);
  status_bar.resize(600, status_bar.sizeHint().height() + 5);
  status_bar.ensurePolished();
  ASSERT_NE(status_bar.layout(), nullptr);
  (void)status_bar.layout()->activate();

  const auto render_status_bar = [&status_bar]() {
    QImage image(status_bar.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    status_bar.render(&image);
    return image;
  };
  const auto background_sample = [&message, &status_bar]() {
    return message.mapTo(&status_bar, QPoint(2, message.height() / 2));
  };

  const QImage error_image = render_status_bar();
  ASSERT_GT(message.width(), 12);
  ASSERT_GT(message.height(), 0);
  EXPECT_EQ(error_image.pixelColor(background_sample()), QColor(179, 38, 30));

  bool painted_white_text = false;
  const QRect message_rect(message.mapTo(&status_bar, QPoint{}), message.size());
  for (int y = message_rect.top(); y <= message_rect.bottom(); ++y) {
    for (int x = message_rect.left(); x <= message_rect.right(); ++x) {
      const QColor pixel = error_image.pixelColor(x, y);
      if (pixel.alpha() == 255 && pixel.red() > 240 &&
          pixel.green() > 240 && pixel.blue() > 240) {
        painted_white_text = true;
      }
    }
  }
  EXPECT_TRUE(painted_white_text)
      << "Error glyphs must render white, including under the app theme";

  const QColor normal_background =
      status_bar.palette().color(QPalette::Window);
  message.present(QStringLiteral("Ready"), false);
  EXPECT_EQ(render_status_bar().pixelColor(background_sample()),
            normal_background);
  message.present(QString{}, false);
  EXPECT_EQ(render_status_bar().pixelColor(background_sample()),
            normal_background);
}

TEST_F(QtStatusMessageTests, SameTextCanChangeSeverityAndThenClear) {
  quader::QtStatusMessage message;
  const QString text = QStringLiteral("Operation stopped");
  message.present(text, false);
  EXPECT_FALSE(message.is_error());
  EXPECT_FALSE(message.autoFillBackground());

  message.present(text, true);
  EXPECT_EQ(message.text(), text);
  EXPECT_TRUE(message.is_error());
  EXPECT_TRUE(message.autoFillBackground());
  const qint64 retained_palette_key = message.palette().cacheKey();
  message.present(text, true);
  EXPECT_EQ(message.palette().cacheKey(), retained_palette_key);

  message.present(text, false);
  EXPECT_EQ(message.text(), text);
  EXPECT_FALSE(message.is_error());
  EXPECT_FALSE(message.autoFillBackground());
  message.present(QString{}, false);
  EXPECT_TRUE(message.text().isEmpty());
  EXPECT_FALSE(message.is_error());
}

TEST_F(QtStatusMessageTests, NormalTextResumesLiveApplicationPalette) {
  quader::QtStatusMessage message;
  message.present(QStringLiteral("Error"), true);

  QPalette palette = QApplication::palette();
  palette.setColor(QPalette::All, QPalette::WindowText, QColor(40, 210, 70));
  palette.setColor(QPalette::All, QPalette::Window, QColor(35, 40, 45));
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();
  EXPECT_EQ(message.palette().color(QPalette::WindowText), QColor(Qt::white));

  message.present(QStringLiteral("Ready"), false);
  EXPECT_EQ(message.palette().color(QPalette::WindowText), QColor(40, 210, 70));
  EXPECT_EQ(message.palette().color(QPalette::Window), QColor(35, 40, 45));
  EXPECT_FALSE(message.testAttribute(Qt::WA_SetPalette));

  palette.setColor(QPalette::All, QPalette::WindowText, QColor(220, 190, 160));
  QApplication::setPalette(palette);
  QCoreApplication::processEvents();
  EXPECT_EQ(message.palette().color(QPalette::WindowText),
            QColor(220, 190, 160));
}

TEST_F(QtStatusMessageTests, AnnouncesOnlyExplicitErrorEvents) {
  quader::QtStatusMessage message;
  int announcements = 0;
  message.set_error_sound_callback([&announcements]() { ++announcements; });

  message.present(QStringLiteral("Ready"), false, true);
  message.present(QStringLiteral("Error"), true);
  EXPECT_EQ(announcements, 0);

  message.present(QStringLiteral("Error"), true, true);
  EXPECT_EQ(announcements, 1);
  message.present(QStringLiteral("Error"), true);
  EXPECT_EQ(announcements, 1);
  message.present(QStringLiteral("Error"), true, true);
  EXPECT_EQ(announcements, 2);

  message.present(QString{}, false, true);
  EXPECT_EQ(announcements, 2);
  message.set_error_sound_callback({});
  message.present(QStringLiteral("Muted error"), true, true);
  EXPECT_EQ(announcements, 2);
}

} // namespace
