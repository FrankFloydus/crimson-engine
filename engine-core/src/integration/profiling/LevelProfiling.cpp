#include "LevelProfiling.h"
#include "profiling/CaptureController.h"
#include <crimson/renderer/core/Profiler.hpp>
#include <QAction>
#include <QApplication>
#include <QClipboard>
#include <QHBoxLayout>
#include <QMainWindow>
#include <QMenu>
#include <QMenuBar>
#include <QToolButton>
#include <cstdio>
#include <algorithm>

namespace crimson::integration {
namespace {
QString statusText(const profiling::CaptureStatus& state){
    switch(state.state){case profiling::CaptureState::Idle:return {};case profiling::CaptureState::Connecting:return "Connecting…";case profiling::CaptureState::Saving:return "Saving…";case profiling::CaptureState::Saved:return "Profile saved";case profiling::CaptureState::Failed:return "Capture failed";case profiling::CaptureState::Capturing:break;}
    const auto seconds=static_cast<unsigned long long>(std::max(0.,state.elapsedSeconds));char elapsed[40]{};std::snprintf(elapsed,sizeof(elapsed),"%02llu:%02llu",seconds/60,seconds%60);
    return QString(state.kind==profiling::CaptureKind::Editor?"Recording editor %1":"Recording %1").arg(elapsed);
}
}
struct LevelProfiling::Impl {
    QMainWindow& main;profiling::CaptureController& captures;QMenu* menu{};QAction *editor{},*gameplay{},*stop{},*detail{},*copy{};QWidget* feedback{};QToolButton *status{},*stopButton{};bool playing{};
    Impl(QMainWindow& window,profiling::CaptureController& owner):main(window),captures(owner){
        menu=main.menuBar()->addMenu("Profiling");menu->setObjectName("CrimsonProfilingMenu");editor=menu->addAction("Record editor / viewport");editor->setObjectName("CrimsonRecordEditor");gameplay=menu->addAction("Record gameplay");gameplay->setObjectName("CrimsonRecordGameplay");stop=menu->addAction("Stop and save");stop->setObjectName("CrimsonStopCapture");menu->addSeparator();detail=menu->addAction(QString{});detail->setEnabled(false);copy=menu->addAction("Copy trace path");
        QObject::connect(editor,&QAction::triggered,menu,[this]{if(CRIMSON_ENABLE_PROFILING&&!captures.active())captures.captureNow(profiling::CaptureKind::Editor,0);refresh();});
        QObject::connect(gameplay,&QAction::triggered,menu,[this]{if(CRIMSON_ENABLE_PROFILING&&playing&&!captures.active())captures.captureNow(profiling::CaptureKind::Gameplay,0);refresh();});QObject::connect(stop,&QAction::triggered,menu,[this]{captures.requestStop();refresh();});
        QObject::connect(copy,&QAction::triggered,menu,[this]{const auto value=captures.status();if(value.state==profiling::CaptureState::Saved)QApplication::clipboard()->setText(QString::fromStdWString(value.trace.wstring()));});QObject::connect(menu,&QMenu::aboutToShow,menu,[this]{refresh();});
        feedback=new QWidget(main.menuBar());feedback->setObjectName("CrimsonCaptureFeedback");auto* layout=new QHBoxLayout(feedback);layout->setContentsMargins(4,0,4,0);status=new QToolButton(feedback);status->setAutoRaise(true);status->setPopupMode(QToolButton::InstantPopup);status->setMenu(menu);layout->addWidget(status);stopButton=new QToolButton(feedback);stopButton->setAutoRaise(true);stopButton->setDefaultAction(stop);layout->addWidget(stopButton);main.menuBar()->setCornerWidget(feedback,Qt::TopRightCorner);refresh();
    }
    ~Impl(){main.menuBar()->setCornerWidget(nullptr,Qt::TopRightCorner);delete feedback;main.menuBar()->removeAction(menu->menuAction());delete menu;}
    void refresh(){const auto value=captures.status();const bool enabled=CRIMSON_ENABLE_PROFILING!=0,active=value.active();editor->setEnabled(enabled&&!active);gameplay->setEnabled(enabled&&playing&&!active);stop->setEnabled(active&&value.state!=profiling::CaptureState::Saving);editor->setToolTip(enabled?QString{}:QStringLiteral("Profiling is disabled in this build."));gameplay->setToolTip(!enabled?QStringLiteral("Profiling is disabled in this build."):!playing?QStringLiteral("Start Play to record gameplay."):QString{});const auto label=statusText(value);status->setText(label);status->setToolTip(QString::fromStdString(value.detail));detail->setText(label);detail->setToolTip(QString::fromStdString(value.detail));detail->setVisible(value.state!=profiling::CaptureState::Idle);copy->setEnabled(value.state==profiling::CaptureState::Saved&&!value.trace.empty());copy->setVisible(value.state==profiling::CaptureState::Saved);stopButton->setVisible(stop->isEnabled());feedback->setVisible(value.state!=profiling::CaptureState::Idle);}
};
LevelProfiling::LevelProfiling(QMainWindow& window,profiling::CaptureController& owner):impl_(std::make_unique<Impl>(window,owner)){}
LevelProfiling::~LevelProfiling()=default;
void LevelProfiling::update(bool playing){impl_->playing=playing;impl_->refresh();}
}
