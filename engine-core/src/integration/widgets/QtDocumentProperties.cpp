#include "QtDocumentProperties.h"
#include "math/Math.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QColorDialog>
#include <QDoubleSpinBox>
#include <QHeaderView>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <array>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QStatusBar>
#include <QToolBar>
#include <QTimer>
#include <chrono>
#include <cmath>
#include <set>
#include <cstdio>

namespace crimson {
namespace {
QString caption(const std::string& text) {
    QString result;
    for(char c:text) {
        if(c=='_')result+=' ';
        else {if(c>='A'&&c<='Z'&&!result.isEmpty())result+=' ';result+=QChar(c);}
    }
    if(!result.isEmpty())result[0]=result[0].toUpper();
    return result;
}
bool immutable(const std::string& key) {
    static const std::set<std::string> keys{"id","version","schema","schemaVersion","originModel","bindingSignature","sourceClipIdentity","sourceModel","sourceClip","skin","parent","nextNodeId","nextLinkId","nextStopId","nextKeyId","aliasOrigin","kind","referenceNode","heightExpression","expressions","renderer","colorSpace"};
    return keys.contains(key);
}
QStringList numericChoices(const std::string& path,const std::string& key) {
    if(key=="shape")return {"Capsule","Box","Sphere"};
    if(key=="rootLockReference")return {"First Frame","Reference Pose","Zero"};
    if(key=="shading")return {"Lit (PBR)","Unlit","Decal"};
    if(key=="blend")return {"Opaque","Masked","Translucent","Additive"};
    if(key=="role")return {"Color (sRGB)","Linear Data","Normal"};
    if(key=="source")return {"Inline","Existing Material"};
    if(key=="component")return {"R","G","B","A","Luminance"};
    if(key=="generatedFrom")return {"Base Color","Roughness","Metallic","Normal","Occlusion","Height","Emission"};
    if(key=="mode"&&(path.find("/uv/")!=std::string::npos||path.find("/mapping/")!=std::string::npos))return {"UV0","UV1","Local Triplanar","World Triplanar"};
    if(key=="mode"&&path.find("/channels/")!=std::string::npos)return {"Value","Texture","Generated"};
    if(key=="filter")return {"Nearest","Linear","Area"};
    if(key=="interpolation")return {"Linear","Constant","Smooth"};
    return {};
}
QStringList stringChoices(const std::string& path,const std::string& key) {
    if(key=="shape")return {"point","cone","sphere","box"};
    if(key=="space")return {"world","local"};
    if(key=="velocityMode")return {"vector","direction-speed"};
    if(key=="directionMode")return {"fixed","random-cone","outward"};
    if(key=="billboardMode")return {"disabled","enabled","fixed-y"};
    if(key=="interpolation")return {"linear","constant"};
    if(key=="mode"&&path.find("/appearanceTracks/color/")!=std::string::npos)return {"constant","gradient"};
    if(key=="mode"&&path.find("/appearanceTracks/")!=std::string::npos)return {"constant","random-range","curve"};
    if(key=="durationPolicy")return {"instant","hasDuration","infinite"};
    if(key=="operation")return {"addBase","multiplyAdditive","divideAdditive","multiplyCompound","addFinal","override"};
    if(key=="type"&&path.find("/stacking/")!=std::string::npos)return {"none","aggregateBySource","aggregateByTarget"};
    if(key=="durationRefresh")return {"refreshOnSuccessfulApplication","neverRefresh","extendDuration"};
    if(key=="periodReset")return {"resetOnSuccessfulApplication","neverReset"};
    if(key=="expiration")return {"clearEntireStack","removeSingleStackAndRefreshDuration","refreshDuration"};
    return {};
}
std::string pointerToken(const std::string& token) {
    std::string result;for(char c:token){if(c=='~')result+="~0";else if(c=='/')result+="~1";else result+=c;}return result;
}
class QtDocumentTrackControl final : public QWidget {
public:
    using Json=nlohmann::json;
    using Write=std::function<void(const Json&,bool)>;
    QtDocumentTrackControl(Json values,Json interpolation,bool gradient,Write write,std::function<void()> cancel,QWidget* parent)
        :QWidget(parent),values_(std::move(values)),interpolation_(std::move(interpolation)),gradient_(gradient),write_(std::move(write)),cancel_(std::move(cancel)) {
        setMinimumHeight(gradient?60:130);setMinimumWidth(140);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);
        const auto member=positionMember();for(const auto& point:values_)if(point.contains("value")){minimum_=std::min(minimum_,point.at("value").get<double>());maximum_=std::max(maximum_,point.at("value").get<double>());}
        if(maximum_-minimum_<1.e-6)maximum_=minimum_+1;
        (void)member;
    }
    void detach(){dragging_=false;write_=[](const Json&,bool){};cancel_=[]{ };}
protected:
    void paintEvent(QPaintEvent*)override {
        QPainter painter(this);painter.setRenderHint(QPainter::Antialiasing);painter.fillRect(rect(),palette().brush(QPalette::Base));
        const auto bounds=plotRect();painter.setPen(palette().color(QPalette::Mid));painter.drawRect(bounds);
        if(values_.empty())return;
        if(gradient_){
            for(int y=int(bounds.top());y<int(bounds.bottom());y+=8)for(int x=int(bounds.left());x<int(bounds.right());x+=8)painter.fillRect(QRect(x,y,8,8),((x+y)/8)%2?QColor(85,85,85):QColor(120,120,120));
            for(int x=int(bounds.left());x<=int(bounds.right());++x){const auto sample=sampleColor((x-bounds.left())/std::max(1.,bounds.width()));painter.setPen(sample);painter.drawLine(x,int(bounds.top()),x,int(bounds.bottom()));}
        }else{
            painter.setPen(palette().color(QPalette::Mid));for(int step=1;step<4;++step){const auto x=bounds.left()+bounds.width()*step/4.;const auto y=bounds.top()+bounds.height()*step/4.;painter.drawLine(QPointF(x,bounds.top()),QPointF(x,bounds.bottom()));painter.drawLine(QPointF(bounds.left(),y),QPointF(bounds.right(),y));}
            QPainterPath path;bool first=true;for(const auto& point:values_){const auto position=keyPosition(point);if(first){path.moveTo(position);first=false;}else if(constant()){path.lineTo(position.x(),path.currentPosition().y());path.lineTo(position);}else path.lineTo(position);}painter.setPen(QPen(palette().color(QPalette::Accent),2));painter.drawPath(path);
        }
        for(std::size_t i=0;i<values_.size();++i){auto position=keyPosition(values_[i]);painter.setPen(QPen(i==selected_?palette().color(QPalette::BrightText):palette().color(QPalette::WindowText),1));painter.setBrush(i==selected_?palette().color(QPalette::Accent):palette().color(QPalette::Button));painter.drawEllipse(position,4,4);}
    }
    void mousePressEvent(QMouseEvent* event)override {
        if(event->button()!=Qt::LeftButton)return;setFocus(Qt::MouseFocusReason);selected_=closest(event->position());if(selected_>=values_.size())return;baseline_=values_;dragging_=true;event->accept();update();
    }
    void mouseMoveEvent(QMouseEvent* event)override {
        if(!dragging_||selected_>=values_.size())return;
        const auto bounds=plotRect();auto candidate=baseline_;const auto member=positionMember();
        const double low=selected_?candidate[selected_-1].at(member).get<double>()+.0001:0,high=selected_+1<candidate.size()?candidate[selected_+1].at(member).get<double>()-.0001:1;
        candidate[selected_][member]=std::clamp((event->position().x()-bounds.left())/std::max(1.,bounds.width()),low,high);
        if(!gradient_)candidate[selected_]["value"]=minimum_+std::clamp((bounds.bottom()-event->position().y())/std::max(1.,bounds.height()),0.,1.)*(maximum_-minimum_);
        values_=std::move(candidate);write_(values_,false);update();event->accept();
    }
    void mouseReleaseEvent(QMouseEvent* event)override {if(dragging_&&event->button()==Qt::LeftButton){dragging_=false;write_(values_,true);event->accept();}}
    void mouseDoubleClickEvent(QMouseEvent* event)override {
        if(event->button()!=Qt::LeftButton)return;dragging_=false;const auto index=closest(event->position());
        if(gradient_&&index<values_.size()){
            const auto old=values_[index].at("color");double peak=1;for(unsigned i=0;i<3;++i)peak=std::max(peak,old[i].get<double>());
            const auto encode=[](double x){return std::clamp(x<=.0031308?12.92*x:1.055*std::pow(x,1/2.4)-.055,0.,1.);};
            const auto color=QColor::fromRgbF(encode(old[0].get<double>()/peak),encode(old[1].get<double>()/peak),encode(old[2].get<double>()/peak),old.size()>3?old[3].get<double>():1);
            const auto chosen=QColorDialog::getColor(color,this,"Gradient Stop",old.size()>3?QColorDialog::ShowAlphaChannel:QColorDialog::ColorDialogOptions{});if(!chosen.isValid())return;
            const auto linear=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};auto next=old;next[0]=linear(chosen.redF())*peak;next[1]=linear(chosen.greenF())*peak;next[2]=linear(chosen.blueF())*peak;if(old.size()>3)next[3]=chosen.alphaF();values_[index]["color"]=next;write_(values_,true);update();return;
        }
        if(values_.empty()||values_.size()>=32)return;const auto bounds=plotRect();const auto member=positionMember();const double age=std::clamp((event->position().x()-bounds.left())/std::max(1.,bounds.width()),0.,1.);
        for(const auto& point:values_)if(std::abs(point.at(member).get<double>()-age)<.0001)return;
        auto entry=values_.front();entry[member]=age;std::uint64_t next=1;const bool hex=entry.at("id").is_string();for(const auto& point:values_){const auto id=hex?std::stoull(point.at("id").get<std::string>(),nullptr,16):point.at("id").get<std::uint64_t>();next=std::max(next,id+1);}if(hex){char text[17];std::snprintf(text,sizeof(text),"%016llx",static_cast<unsigned long long>(next));entry["id"]=text;}else entry["id"]=next;
        if(!gradient_)entry["value"]=minimum_+std::clamp((bounds.bottom()-event->position().y())/std::max(1.,bounds.height()),0.,1.)*(maximum_-minimum_);
        else{const auto sample=linearColor(age);entry["color"]=sample;}
        values_.push_back(entry);std::sort(values_.begin(),values_.end(),[member](const auto& a,const auto& b){return a.at(member).template get<double>()<b.at(member).template get<double>();});write_(values_,true);update();
    }
    void keyPressEvent(QKeyEvent* event)override {
        if(event->key()==Qt::Key_Escape){if(dragging_){values_=baseline_;dragging_=false;}cancel_();update();return;}
        if(event->key()==Qt::Key_Delete&&selected_<values_.size()&&values_.size()>2){values_.erase(values_.begin()+selected_);selected_=values_.size();write_(values_,true);update();return;}QWidget::keyPressEvent(event);
    }
    void focusOutEvent(QFocusEvent* event)override {if(dragging_){dragging_=false;write_(values_,true);}QWidget::focusOutEvent(event);}
private:
    Json values_,baseline_,interpolation_;
    bool gradient_{},dragging_{};
    Write write_;
    std::function<void()> cancel_;
    std::size_t selected_{SIZE_MAX};
    double minimum_{},maximum_{1};
    QRectF plotRect()const{return QRectF(rect()).adjusted(8,8,-8,-12);}
    const char* positionMember()const{return !values_.empty()&&values_.front().contains("position")?"position":"age";}
    bool constant()const{return interpolation_=="constant"||interpolation_==1;}
    QPointF keyPosition(const Json& point)const{const auto bounds=plotRect();return {bounds.left()+point.at(positionMember()).get<double>()*bounds.width(),gradient_?bounds.bottom()+3:bounds.bottom()-(point.at("value").get<double>()-minimum_)/(maximum_-minimum_)*bounds.height()};}
    std::size_t closest(QPointF point)const{std::size_t result=values_.size();double best=11;for(std::size_t i=0;i<values_.size();++i){const auto at=keyPosition(values_[i]);const auto distance=std::hypot(at.x()-point.x(),at.y()-point.y());if(distance<best){best=distance;result=i;}}return result;}
    Json linearColor(double age)const{const auto member=positionMember();if(age<=values_.front().at(member).get<double>())return values_.front().at("color");for(std::size_t i=1;i<values_.size();++i)if(age<=values_[i].at(member).get<double>()){auto result=values_[i-1].at("color");double t=constant()?0:(age-values_[i-1].at(member).get<double>())/(values_[i].at(member).get<double>()-values_[i-1].at(member).get<double>());if(interpolation_==2)t=t*t*(3-2*t);for(std::size_t c=0;c<result.size();++c)result[c]=result[c].get<double>()+(values_[i].at("color")[c].get<double>()-result[c].get<double>())*t;return result;}return values_.back().at("color");}
    QColor sampleColor(double age)const{const auto rgba=linearColor(age);double peak=1;for(unsigned c=0;c<3;++c)peak=std::max(peak,rgba[c].get<double>());const auto encode=[](double v){return std::clamp(v<=.0031308?12.92*v:1.055*std::pow(v,1/2.4)-.055,0.,1.);};return QColor::fromRgbF(encode(rgba[0].get<double>()/peak),encode(rgba[1].get<double>()/peak),encode(rgba[2].get<double>()/peak),rgba.size()>3?rgba[3].get<double>():1);}
};
}
QtDocumentProperties::QtDocumentProperties(QWidget* parent):QTreeWidget(parent) {
    setColumnCount(2);setHeaderLabels({"Property","Value"});setAlternatingRowColors(false);
    header()->setSectionResizeMode(0,QHeaderView::ResizeToContents);
    header()->setSectionResizeMode(1,QHeaderView::Stretch);setMinimumWidth(280);
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this,&QWidget::customContextMenuRequested,this,[this](const QPoint& pos){
        auto* item=itemAt(pos);if(!item||!read_||!write_)return;
        const auto path=item->data(0,Qt::UserRole).toString().toStdString();if(path.empty())return;
        auto value=read_();const Json::json_pointer pointer(path);auto& selected=value.at(pointer);
        QMenu menu(this);
        const auto mutableArray=[](const Json::json_pointer& path){static const std::set<std::string> kinds{"keys","stops","modifiers","parameters","assetTags","grantedTags","sourceRequired","sourceBlocked","targetRequired","targetBlocked","legacyBursts"};return !path.empty()&&kinds.contains(path.back());};
        if(selected.is_array()&&mutableArray(pointer)) {
            auto* add=menu.addAction("Add");
            connect(add,&QAction::triggered,this,[this,pointer]{auto data=read_();auto& list=data.at(pointer);Json entry=list.empty()?Json::object():list.back();if(entry.is_object()&&entry.contains("id")){std::uint64_t next=1;const bool hex=entry["id"].is_string();for(auto& v:list){const auto id=hex?std::stoull(v["id"].get<std::string>(),nullptr,16):v["id"].get<std::uint64_t>();next=std::max(next,id+1);}if(hex){char buffer[17];std::snprintf(buffer,sizeof(buffer),"%016llx",static_cast<unsigned long long>(next));entry["id"]=buffer;}else entry["id"]=next;}if(pointer.back()=="modifiers"&&entry.empty())entry={{"attribute","Stats.MaxHealth"},{"operation","addBase"},{"magnitude",{{"literal",0},{"calculation",""},{"parameters",Json::array()}}}};if(pointer.back()=="parameters"&&entry.empty())entry={{"name","value"},{"value",0}};if(entry.empty()&&pointer.back()!="keys"&&pointer.back()!="stops"&&pointer.back()!="modifiers"&&pointer.back()!="parameters"&&pointer.back()!="legacyBursts")entry="Stats.MaxHealth";if(entry.is_object()&&(entry.contains("age")||entry.contains("position"))){const auto member=entry.contains("age")?"age":"position";float age=.5f;std::vector<float> positions;for(auto& point:list)positions.push_back(point.at(member).get<float>());std::sort(positions.begin(),positions.end());float gap=positions.empty()?1:positions.front();age=gap*.5f;for(std::size_t i=0;i<positions.size();++i){const auto next=i+1<positions.size()?positions[i+1]:1.f;if(next-positions[i]>gap){gap=next-positions[i];age=(next+positions[i])*.5f;}}entry[member]=age;}list.push_back(entry);if(entry.is_object()&&(entry.contains("age")||entry.contains("position"))){const auto member=entry.contains("age")?"age":"position";std::sort(list.begin(),list.end(),[member](const auto& a,const auto& b){return a.at(member).template get<float>()<b.at(member).template get<float>();});}write_(data,"Add item",true);revision_=UINT64_MAX;refresh(revision_-1);});
        }
        auto parent=pointer.parent_pointer();
        if(!parent.empty()&&value.at(parent).is_array()&&mutableArray(parent)) {
        auto* remove=menu.addAction("Remove");
        if((parent.back()=="keys"||parent.back()=="stops")&&value.at(parent).size()<=2)remove->setEnabled(false);
            connect(remove,&QAction::triggered,this,[this,pointer,parent]{auto data=read_();auto& list=data.at(parent);list.erase(list.begin()+std::stoll(pointer.back()));write_(data,"Remove item",true);revision_=UINT64_MAX;refresh(revision_-1);});
        }
        if(!menu.isEmpty())menu.exec(viewport()->mapToGlobal(pos));
    });
}
void QtDocumentProperties::bind(Read read,Write write,std::function<void()> finish,std::function<void()> cancel) {
    read_=std::move(read);write_=std::move(write);finish_=std::move(finish);cancel_=std::move(cancel);revision_=UINT64_MAX;
}
QtDocumentProperties::~QtDocumentProperties(){rebuilding_=true;controlsLifetime_.reset();detach();for(auto* child:findChildren<QWidget*>()){child->removeEventFilter(this);if(auto* track=dynamic_cast<QtDocumentTrackControl*>(child))track->detach();}clear();}
void QtDocumentProperties::finish(){if(rebuilding_)return;if(finish_)finish_();editing_=false;}
void QtDocumentProperties::cancel(){if(rebuilding_)return;if(cancel_)cancel_();editing_=false;revision_=UINT64_MAX;}
bool QtDocumentProperties::eventFilter(QObject* object,QEvent* event) {
    if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){cancel();if(auto* widget=qobject_cast<QWidget*>(object))widget->clearFocus();return true;}
    if(event->type()==QEvent::FocusOut){finish();}
    return QTreeWidget::eventFilter(object,event);
}
void QtDocumentProperties::updateValue(const Json::json_pointer& path,const Json& value,const std::string& label,bool final) {
    if(rebuilding_||!read_||!write_)return;
    auto data=read_();data[path]=value;editing_=!final;write_(data,"Change "+label,final);
}
void QtDocumentProperties::refresh(std::uint64_t revision,bool editable) {
    setEnabled(editable);if(revision_==revision||editing_||!read_)return;
    std::set<QString> expanded;std::function<void(QTreeWidgetItem*)> remember=[&](QTreeWidgetItem* item){if(item->isExpanded())expanded.insert(item->data(0,Qt::UserRole).toString());for(int i=0;i<item->childCount();++i)remember(item->child(i));};for(int i=0;i<topLevelItemCount();++i)remember(topLevelItem(i));
    rebuilding_=true;
    // Item widgets can emit editingFinished during removal and are otherwise
    // deferred by the item view. Disconnect before clearing so old controls
    // cannot finish a newer document gesture or survive as stale descendants.
    std::vector<QPointer<QWidget>> oldControls;
    controlsLifetime_.reset();
    for(auto* child:findChildren<QWidget*>())if(child->property("crimsonDocumentEditor").toBool()){
        oldControls.push_back(child);child->removeEventFilter(this);for(auto* nested:child->findChildren<QWidget*>())nested->removeEventFilter(this);if(auto* track=dynamic_cast<QtDocumentTrackControl*>(child))track->detach();
    }
    clear();for(auto& child:oldControls)if(child)delete child.data();
    controlsLifetime_=std::make_unique<QObject>();const auto data=read_();
    if(data.is_object()){if(data.contains("name"))append(nullptr,data.at("name"),Json::json_pointer("/name"),"name");for(auto it=data.begin();it!=data.end();++it)if(it.key()!="name"&&it.key()!="type"&&!immutable(it.key())&&(!filter_||filter_(it.key())))append(nullptr,it.value(),Json::json_pointer("/"+pointerToken(it.key())),it.key());}
    else append(nullptr,data,Json::json_pointer{},"Properties");
    std::function<void(QTreeWidgetItem*)> restore=[&](QTreeWidgetItem* item){if(expanded.contains(item->data(0,Qt::UserRole).toString()))item->setExpanded(true);for(int i=0;i<item->childCount();++i)restore(item->child(i));};for(int i=0;i<topLevelItemCount();++i){auto* item=topLevelItem(i);restore(item);if(expanded.empty())item->setExpanded(true);}
    revision_=revision;rebuilding_=false;
}
void QtDocumentProperties::append(QTreeWidgetItem* parent,const Json& value,const Json::json_pointer& path,const std::string& key) {
    if(immutable(key))return;
    if((key=="node"&&value.is_number())||(key=="shading"&&value==3))return;
    auto* item=parent?new QTreeWidgetItem(parent):new QTreeWidgetItem(this);item->setText(0,caption(key));item->setData(0,Qt::UserRole,QString::fromStdString(path.to_string()));
    if(value.is_object()){for(auto it=value.begin();it!=value.end();++it)append(item,it.value(),Json::json_pointer(path.to_string()+"/"+pointerToken(it.key())),it.key());return;}
    if(value.is_array()){
        if(key=="rotation"&&value.size()==4&&value[0].is_number()){
            const Quat quaternion{value[0].get<float>(),value[1].get<float>(),value[2].get<float>(),value[3].get<float>()};
            auto degrees=std::make_shared<std::array<double,3>>();if(const auto fields=math::affineTransformFields(math::rotation(quaternion)))for(unsigned i=0;i<3;++i)(*degrees)[i]=fields->rotationRadians[i]*(180./3.14159265358979323846);
            for(unsigned axis=0;axis<3;++axis){auto* row=new QTreeWidgetItem(item);row->setText(0,QString(QChar(char('X'+axis))));auto* field=new QDoubleSpinBox(this);field->setRange(-100000,100000);field->setDecimals(3);field->setSuffix("°");field->setKeyboardTracking(false);field->setValue((*degrees)[axis]);connect(field,qOverload<double>(&QDoubleSpinBox::valueChanged),controlsLifetime_.get(),[this,path,key,degrees,axis](double value){(*degrees)[axis]=value;const double unit=3.14159265358979323846/360.,x=(*degrees)[0]*unit,y=(*degrees)[1]*unit,z=(*degrees)[2]*unit,cx=std::cos(x),sx=std::sin(x),cy=std::cos(y),sy=std::sin(y),cz=std::cos(z),sz=std::sin(z);updateValue(path,Json::array({sx*cy*cz-cx*sy*sz,cx*sy*cz+sx*cy*sz,cx*cy*sz-sx*sy*cz,cx*cy*cz+sx*sy*sz}),key,false);});connect(field,&QDoubleSpinBox::editingFinished,controlsLifetime_.get(),[this]{finish();});field->setProperty("crimsonDocumentEditor",true);field->installEventFilter(this);setItemWidget(row,1,field);}return;
        }
        if((key=="keys"||key=="stops")&&!value.empty()&&value.front().contains("id")&&(value.front().contains("age")||value.front().contains("position"))){
            const auto owner=read_().at(path.parent_pointer());auto* plot=new QtDocumentTrackControl(value,owner.value("interpolation",Json{}),key=="stops",[this,path,key](const Json& changed,bool final){updateValue(path,changed,key,final);},[this]{cancel();},this);plot->setProperty("crimsonDocumentEditor",true);setItemWidget(item,1,plot);item->setSizeHint(1,QSize(150,key=="stops"?60:130));
        }
        const bool color=(key=="color"||key=="baseColor"||key=="emission"||key=="attenuationColor"||key=="colorTint"||key=="colorKey"||key=="tint")&&value.size()>=3&&value[0].is_number();
        if(color){auto* swatch=new QPushButton(this);double peak=1;for(unsigned i=0;i<3;++i)peak=std::max(peak,value[i].get<double>());const auto encoded=[](double v){return std::clamp(v<=.0031308?v*12.92:1.055*std::pow(v,1/2.4)-.055,0.,1.);};const QColor hue=QColor::fromRgbF(encoded(value[0].get<double>()/peak),encoded(value[1].get<double>()/peak),encoded(value[2].get<double>()/peak));swatch->setStyleSheet("background-color: "+hue.name()+"; min-height: 16px;");swatch->setProperty("crimsonDocumentEditor",true);setItemWidget(item,1,swatch);connect(swatch,&QPushButton::clicked,controlsLifetime_.get(),[this,path,key,value,hue,peak]{const QColor chosen=QColorDialog::getColor(hue,this,QString::fromStdString(key));if(!chosen.isValid())return;auto color=value;const auto linear=[](double v){return v<=.04045?v/12.92:std::pow((v+.055)/1.055,2.4);};color[0]=linear(chosen.redF())*peak;color[1]=linear(chosen.greenF())*peak;color[2]=linear(chosen.blueF())*peak;updateValue(path,color,key,true);revision_=UINT64_MAX;});}
        for(std::size_t i=0;i<value.size();++i){std::string label=std::to_string(i+1);if(value.size()<=4&&value[i].is_number())label=std::array<const char*,4>{"X","Y","Z","W"}[i];if(color&&i<4)label=std::array<const char*,4>{"R","G","B","A"}[i];if(key=="layers"&&i<4)label=std::array<const char*,4>{"Base","Red","Green","Blue"}[i];if(key=="channels"&&i<7)label=std::array<const char*,7>{"Base Color","Roughness","Metallic","Normal","Occlusion","Height","Emission"}[i];append(item,value[i],Json::json_pointer(path.to_string()+"/"+std::to_string(i)),label);}return;
    }
    QWidget* editor=nullptr;
    if(value.is_boolean()) {
        auto* field=new QCheckBox(this);field->setChecked(value.get<bool>());connect(field,&QCheckBox::toggled,controlsLifetime_.get(),[this,path,key](bool v){updateValue(path,v,key,true);});editor=field;
    }else if(value.is_number()) {
        auto options=numericChoices(path.to_string(),key);const auto named=key=="rootNode"&&choices_?choices_(path.to_string()):QStringList{};if(!named.empty())options=named;
        if(!options.empty()){auto* field=new QComboBox(this);for(qsizetype i=0;i<options.size();++i){const auto split=options[i].indexOf('\n');field->addItem(split<0?options[i]:options[i].left(split),split<0?qlonglong(i):options[i].mid(split+1).toLongLong());}field->setCurrentIndex(field->findData(qlonglong(value.get<std::int64_t>())));connect(field,qOverload<int>(&QComboBox::currentIndexChanged),controlsLifetime_.get(),[this,path,key,field](int index){if(index>=0)updateValue(path,field->itemData(index).toLongLong(),key,true);});editor=field;}
        else {
        auto* field=new QDoubleSpinBox(this);field->setRange(value.is_number_unsigned()?0:-1.e12,1.e12);field->setDecimals(value.is_number_integer()?0:6);field->setSingleStep(value.is_number_integer()?1:.01);field->setValue(value.get<double>());field->setKeyboardTracking(false);
        connect(field,qOverload<double>(&QDoubleSpinBox::valueChanged),controlsLifetime_.get(),[this,path,key,integer=value.is_number_integer(),unsignedValue=value.is_number_unsigned()](double v){if(integer){if(unsignedValue)updateValue(path,std::uint64_t(v),key,false);else updateValue(path,std::int64_t(v),key,false);}else updateValue(path,v,key,false);});connect(field,&QDoubleSpinBox::editingFinished,controlsLifetime_.get(),[this]{finish();});editor=field;
        }
    }else if(value.is_string()) {
        auto options=stringChoices(path.to_string(),key);if(options.empty()&&choices_)options=choices_(path.to_string());
        if(!options.empty()) {auto* field=new QComboBox(this);field->setEditable(true);for(const auto& option:options){const auto split=option.indexOf('\n');field->addItem(split<0?caption(option.toStdString()):option.left(split),split<0?option:option.mid(split+1));}const auto current=QString::fromStdString(value.get<std::string>());int index=field->findData(current);if(index<0){field->addItem(current,current);index=field->count()-1;}field->setCurrentIndex(index);connect(field,qOverload<int>(&QComboBox::currentIndexChanged),controlsLifetime_.get(),[this,path,key,field](int index){if(index>=0)updateValue(path,field->itemData(index).toString().toStdString(),key,true);});connect(field->lineEdit(),&QLineEdit::editingFinished,controlsLifetime_.get(),[this,path,key,field]{if(field->currentIndex()<0)updateValue(path,field->currentText().toStdString(),key,true);});editor=field;}
        else {auto* field=new QLineEdit(QString::fromStdString(value.get<std::string>()),this);connect(field,&QLineEdit::textEdited,controlsLifetime_.get(),[this,path,key](const QString& text){updateValue(path,text.toStdString(),key,false);});connect(field,&QLineEdit::editingFinished,controlsLifetime_.get(),[this]{finish();});editor=field;}
    }else {item->setText(1,"None");return;}
    editor->setProperty("crimsonDocumentEditor",true);editor->installEventFilter(this);setItemWidget(item,1,editor);
}
}
