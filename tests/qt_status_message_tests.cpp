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
#include <QDir>
#include <QGraphicsOpacityEffect>
#include <QImage>
#include <QLayout>
#include <QPalette>
#include <QPointer>
#include <QPropertyAnimation>
#include <QStatusBar>
#include <QStyle>
#include <QStyleFactory>
#include <QTimer>

#include <memory>

namespace {

class QtStatusMessageTests : public testing::Test {
protected:
  void SetUp() override {
    ASSERT_EQ(QCoreApplication::instance(), nullptr);
    application_ = std::make_unique<QApplication>(argc_, argv_);
    application_->setQuitOnLastWindowClosed(false);
  }

  static void begin_fade(quader::QtStatusMessage &message) {
    QTimer *timer = message.findChild<QTimer *>();
    ASSERT_NE(timer, nullptr);
    timer->stop();
    ASSERT_TRUE(QMetaObject::invokeMethod(timer, "timeout",
                                        Qt::DirectConnection));
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

TEST_F(QtStatusMessageTests, HoldsForFiveSecondsThenFadesWithoutMoving) {
  QStatusBar status_bar;
  status_bar.setSizeGripEnabled(false);
  QPalette palette = status_bar.palette();
  palette.setColor(QPalette::Window, QColor(20, 20, 20));
  status_bar.setPalette(palette);
  status_bar.setAutoFillBackground(true);
  quader::QtStatusMessage message(&status_bar);
  status_bar.addPermanentWidget(&message, 1);
  message.present(QStringLiteral("Cannot complete operation"), true);
  status_bar.resize(600, status_bar.sizeHint().height() + 5);
  status_bar.ensurePolished();
  ASSERT_NE(status_bar.layout(), nullptr);
  (void)status_bar.layout()->activate();
  const QRect retained_geometry = message.geometry();
  const QSize retained_bar_size = status_bar.size();
  const QSize retained_size_hint = message.sizeHint();
  auto *timer = message.findChild<QTimer *>();
  auto *animation = message.findChild<QPropertyAnimation *>();
  auto *effect = qobject_cast<QGraphicsOpacityEffect *>(message.graphicsEffect());
  ASSERT_NE(timer, nullptr);
  ASSERT_NE(animation, nullptr);
  ASSERT_NE(effect, nullptr);
  int dismissals = 0;
  message.restart_dismissal([&dismissals]() { ++dismissals; });
  EXPECT_TRUE(timer->isActive());
  EXPECT_TRUE(timer->isSingleShot());
  EXPECT_EQ(timer->timerType(), Qt::PreciseTimer);
  EXPECT_EQ(timer->interval(), 5000);
  EXPECT_GT(timer->remainingTime(), 0);
  EXPECT_EQ(animation->state(), QAbstractAnimation::Stopped);
  EXPECT_FALSE(effect->isEnabled());
  EXPECT_DOUBLE_EQ(effect->opacity(), 1.0);
  EXPECT_TRUE(message.dismissal_active());

  const auto render_status = [&]() {
    QImage image(status_bar.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    status_bar.render(&image);
    return image;
  };
  const auto capture_phase = [&](const QString &phase) {
    const QString directory = qEnvironmentVariable("QUADER_UI_REVIEW_DIR");
    if (!directory.isEmpty()) {
      EXPECT_TRUE(QDir().mkpath(directory));
      EXPECT_TRUE(render_status().save(QDir(directory).filePath(
          QStringLiteral("status-fade-%1.png").arg(phase))));
    }
  };
  const auto background_pixel = [&]() {
    return render_status().pixelColor(
        message.mapTo(&status_bar, QPoint(2, message.height() / 2)));
  };
  EXPECT_EQ(background_pixel(), QColor(179, 38, 30));
  capture_phase(QStringLiteral("opaque"));
  begin_fade(message);
  EXPECT_EQ(animation->duration(), 200);
  EXPECT_EQ(animation->state(), QAbstractAnimation::Running);
  EXPECT_TRUE(effect->isEnabled());
  animation->setCurrentTime(100);
  EXPECT_NEAR(effect->opacity(), 0.5, 0.01);
  const QColor faded_pixel = background_pixel();
  EXPECT_GT(faded_pixel.red(), 20);
  EXPECT_LT(faded_pixel.red(), 179);
  EXPECT_EQ(message.geometry(), retained_geometry);
  EXPECT_EQ(message.sizeHint(), retained_size_hint);
  EXPECT_EQ(status_bar.size(), retained_bar_size);
  EXPECT_EQ(dismissals, 0);
  capture_phase(QStringLiteral("midpoint"));
  animation->setCurrentTime(animation->duration() - 1);
  capture_phase(QStringLiteral("almost-gone"));

  animation->setCurrentTime(animation->duration());
  EXPECT_EQ(dismissals, 1);
  EXPECT_FALSE(message.dismissal_active());
  EXPECT_FALSE(effect->isEnabled());
  EXPECT_DOUBLE_EQ(effect->opacity(), 1.0);
  EXPECT_EQ(message.geometry(), retained_geometry);
  animation->setCurrentTime(animation->duration());
  EXPECT_EQ(dismissals, 1);
}

TEST_F(QtStatusMessageTests, ReplacementDuringFadeStartsANewOpaqueHold) {
  quader::QtStatusMessage message;
  message.present(QStringLiteral("Old error"), true);
  int old_dismissals = 0;
  int new_dismissals = 0;
  message.restart_dismissal([&old_dismissals]() { ++old_dismissals; });
  begin_fade(message);
  auto *animation = message.findChild<QPropertyAnimation *>();
  auto *effect = qobject_cast<QGraphicsOpacityEffect *>(message.graphicsEffect());
  ASSERT_NE(animation, nullptr);
  ASSERT_NE(effect, nullptr);
  animation->setCurrentTime(100);
  message.present(QStringLiteral("New warning"), false);
  message.restart_dismissal([&new_dismissals]() { ++new_dismissals; });

  EXPECT_EQ(animation->state(), QAbstractAnimation::Stopped);
  EXPECT_FALSE(effect->isEnabled());
  EXPECT_DOUBLE_EQ(effect->opacity(), 1.0);
  EXPECT_TRUE(message.dismissal_active());
  EXPECT_EQ(old_dismissals, 0);
  EXPECT_EQ(new_dismissals, 0);
  begin_fade(message);
  animation->setCurrentTime(animation->duration());
  EXPECT_EQ(old_dismissals, 0);
  EXPECT_EQ(new_dismissals, 1);
  EXPECT_FALSE(message.dismissal_active());
}

TEST_F(QtStatusMessageTests, TextRefreshPreservesHoldDeadlineAndFadeProgress) {
  quader::QtStatusMessage message;
  message.present(QStringLiteral("Cannot complete operation"), true);
  int dismissals = 0;
  message.restart_dismissal([&dismissals]() { ++dismissals; });
  auto *timer = message.findChild<QTimer *>();
  auto *animation = message.findChild<QPropertyAnimation *>();
  auto *effect = qobject_cast<QGraphicsOpacityEffect *>(message.graphicsEffect());
  ASSERT_NE(timer, nullptr);
  ASSERT_NE(animation, nullptr);
  ASSERT_NE(effect, nullptr);
  const int timer_id = timer->timerId();
  const int remaining_time = timer->remainingTime();
  message.present(QStringLiteral("Impossibile completare l'operazione"), true);
  EXPECT_EQ(timer->timerId(), timer_id);
  EXPECT_LE(timer->remainingTime(), remaining_time);
  begin_fade(message);
  animation->setCurrentTime(100);
  message.present(QStringLiteral("Cannot complete operation"), true);
  EXPECT_EQ(animation->currentTime(), 100);
  EXPECT_EQ(animation->state(), QAbstractAnimation::Running);
  EXPECT_TRUE(effect->isEnabled());
  EXPECT_NEAR(effect->opacity(), 0.5, 0.01);
  animation->setCurrentTime(animation->duration());
  EXPECT_EQ(dismissals, 1);
}

TEST_F(QtStatusMessageTests, CancelClearAndDestructionRetireDismissalCallbacks) {
  auto message = std::make_unique<quader::QtStatusMessage>();
  int dismissals = 0;
  message->present(QStringLiteral("Error"), true);
  message->restart_dismissal([&dismissals]() { ++dismissals; });
  message->cancel_dismissal();
  EXPECT_FALSE(message->dismissal_active());
  EXPECT_EQ(dismissals, 0);

  message->restart_dismissal([&dismissals]() { ++dismissals; });
  begin_fade(*message);
  message->present(QString{}, false);
  EXPECT_FALSE(message->dismissal_active());
  EXPECT_EQ(dismissals, 0);
  auto *effect = qobject_cast<QGraphicsOpacityEffect *>(message->graphicsEffect());
  ASSERT_NE(effect, nullptr);
  EXPECT_FALSE(effect->isEnabled());
  EXPECT_DOUBLE_EQ(effect->opacity(), 1.0);

  message->present(QStringLiteral("Warning"), false);
  message->restart_dismissal([&dismissals]() { ++dismissals; });
  QPointer<QTimer> timer = message->findChild<QTimer *>();
  QPointer<QPropertyAnimation> animation =
      message->findChild<QPropertyAnimation *>();
  message.reset();
  EXPECT_TRUE(timer.isNull());
  EXPECT_TRUE(animation.isNull());
  QCoreApplication::processEvents();
  EXPECT_EQ(dismissals, 0);
}

TEST_F(QtStatusMessageTests, DismissalCallbackMayClearOrDestroyItsMessage) {
  quader::QtStatusMessage message;
  message.present(QStringLiteral("Error"), true);
  message.restart_dismissal([&message]() { message.present(QString{}, false); });
  begin_fade(message);
  auto *animation = message.findChild<QPropertyAnimation *>();
  ASSERT_NE(animation, nullptr);
  animation->setCurrentTime(animation->duration());
  EXPECT_TRUE(message.text().isEmpty());
  EXPECT_FALSE(message.dismissal_active());

  QPointer<quader::QtStatusMessage> disposable = new quader::QtStatusMessage;
  disposable->present(QStringLiteral("Warning"), false);
  disposable->restart_dismissal([&disposable]() { delete disposable.data(); });
  begin_fade(*disposable);
  animation = disposable->findChild<QPropertyAnimation *>();
  ASSERT_NE(animation, nullptr);
  animation->setCurrentTime(animation->duration());
  EXPECT_TRUE(disposable.isNull());
}

} // namespace
