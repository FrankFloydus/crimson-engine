#include "LevelVertexPaint.h"
#include "crimson-renderer/Renderer.h"
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QMainWindow>
#include <QMouseEvent>
#include <QPainter>
#include <QWidget>
#include <algorithm>

namespace crimson::integration {
LevelVertexPaint::LevelVertexPaint(QMainWindow& window,std::function<void()> requestFrame)
    :window_(window),requestFrame_(std::move(requestFrame)) {
    tool_.setRequestFrame(requestFrame_);
}
LevelVertexPaint::~LevelVertexPaint(){bind(nullptr,nullptr,nullptr);}

void LevelVertexPaint::bind(SceneDocument* document,AssetLibrary* assets,Renderer* renderer) {
    if(document_==document&&assets_==assets&&renderer_==renderer)return;
    if(document_&&renderer_)tool_.setEnabled(false,*document_,*renderer_);
    tool_.hideControls();held_=false;frame_={};pointer_={};
    document_=document;assets_=assets;renderer_=renderer;
}
void LevelVertexPaint::setQueries(VertexPaintTargetQuery targets,VertexPaintSurfaceQuery surfaces) {
    cancel();tool_.setTargetQuery(std::move(targets));tool_.setSurfaceQuery(std::move(surfaces));
}
void LevelVertexPaint::setThumbnailProvider(std::function<QImage(const std::string&)> provider) {
    tool_.setThumbnailProvider(std::move(provider));
}
void LevelVertexPaint::setEnabled(bool enabled) {
    if(!document_||!renderer_)return;
    tool_.setEnabled(enabled,*document_,*renderer_);
    if(!enabled){held_=false;tool_.hideControls();}
    if(requestFrame_)requestFrame_();
}
void LevelVertexPaint::update(bool pressed,bool escape,const std::optional<PickResult>& pick) {
    if(!document_||!renderer_)return;
    tool_.update(*document_,*renderer_,pick,serial_,frame_.viewProjection,frame_.pixelWidth,frame_.pixelHeight,
        pointer_,frame_.hovered,frame_.focused,frame_.blocked,pressed,held_,escape);
}
void LevelVertexPaint::synchronize(const LevelVertexPaintFrame& frame,std::uint32_t rendererFrame) {
    frame_=frame;
    if(!document_||!renderer_||!assets_)return;
    prepareUi();
    const auto result=enabled()&&renderer_->initialized()?renderer_->pollPick(rendererFrame):std::optional<PickResult>{};
    update(false,false,result);
    tool_.drawControls(window_,*document_,*assets_,*renderer_);
}
void LevelVertexPaint::prepareUi(bool commandRequested,bool cancelRequested) {
    if(!document_||!renderer_)return;
    tool_.prepareUi(*document_,*renderer_,serial_,pointer_,frame_.hovered,held_,commandRequested,cancelRequested);
    tool_.prepareBlendSettingsUi(*document_,held_,commandRequested,cancelRequested);
}
bool LevelVertexPaint::event(QEvent& event,QWidget& viewport) {
    if(!enabled()||!document_||!renderer_)return false;
    if(event.type()==QEvent::FocusOut||event.type()==QEvent::WindowDeactivate||event.type()==QEvent::Hide) {
        cancel();return false;
    }
    if(event.type()==QEvent::KeyPress) {
        const auto& key=static_cast<QKeyEvent&>(event);
        if(key.key()==Qt::Key_Escape){cancel();return true;}
        return false;
    }
    if(event.type()==QEvent::Enter||event.type()==QEvent::Leave) {
        frame_.hovered=event.type()==QEvent::Enter;
        update();if(requestFrame_)requestFrame_();return false;
    }
    if(event.type()!=QEvent::MouseMove&&event.type()!=QEvent::MouseButtonPress&&event.type()!=QEvent::MouseButtonRelease)return false;
    const auto& mouse=static_cast<QMouseEvent&>(event);
    const auto position=mouse.position();
    const auto logicalWidth=frame_.logicalWidth>0?frame_.logicalWidth:float(viewport.width());
    const auto logicalHeight=frame_.logicalHeight>0?frame_.logicalHeight:float(viewport.height());
    pointer_={float(position.x())*frame_.pixelWidth/std::max(1.f,logicalWidth),
        float(position.y())*frame_.pixelHeight/std::max(1.f,logicalHeight)};
    frame_.hovered=viewport.rect().contains(position.toPoint());
    frame_.focused=viewport.hasFocus();
    const bool leftPress=event.type()==QEvent::MouseButtonPress&&mouse.button()==Qt::LeftButton;
    const bool leftRelease=event.type()==QEvent::MouseButtonRelease&&mouse.button()==Qt::LeftButton;
    const bool owned=held_;
    if(leftPress) {
        if(frame_.blocked)return false;
        held_=true;viewport.setFocus(Qt::MouseFocusReason);frame_.focused=true;
    } else if(leftRelease||!(mouse.buttons()&Qt::LeftButton))held_=false;
    prepareUi(false,false);
    update(leftPress);
    if(requestFrame_)requestFrame_();
    return leftPress||leftRelease||owned||held_;
}
void LevelVertexPaint::paint(QPainter& painter) const {
    tool_.drawCursor(painter,{0,0,frame_.logicalWidth,frame_.logicalHeight,frame_.hovered},pointer_,frame_.pixelWidth,frame_.pixelHeight);
}
void LevelVertexPaint::finishForCommand() {
    if(document_&&renderer_)tool_.finishForCommand(*document_,*renderer_);
    if(requestFrame_)requestFrame_();
}
void LevelVertexPaint::cancel() {
    if(document_&&renderer_)tool_.cancel(*document_,*renderer_);
    held_=false;
    if(requestFrame_)requestFrame_();
}
}
