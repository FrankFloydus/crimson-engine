#include "QtMaterialGraph.h"
#include "editor/MaterialGraphActions.h"
#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QColorDialog>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QFocusEvent>
#include <QGraphicsEllipseItem>
#include <QGraphicsPathItem>
#include <QGraphicsPixmapItem>
#include <QGraphicsProxyWidget>
#include <QGraphicsRectItem>
#include <QGraphicsSimpleTextItem>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainterPath>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <nlohmann/json.hpp>
#include <map>

namespace crimson {
namespace {
constexpr int NodeRole=Qt::UserRole+1,PinRole=Qt::UserRole+2,InputRole=Qt::UserRole+3,LinkRole=Qt::UserRole+4,PreviewRole=Qt::UserRole+5;
QColor pinColor(MaterialValueType type){switch(type){case MaterialValueType::Scalar:return QColor(175,175,185);case MaterialValueType::Color:case MaterialValueType::Color4:return QColor(225,185,75);case MaterialValueType::Mapping:return QColor(160,95,205);case MaterialValueType::Normal:return QColor(90,140,235);case MaterialValueType::Vector2:return QColor(90,205,130);case MaterialValueType::Surface:return QColor(215,110,85);}return Qt::white;}
QPainterPath curve(QPointF a,QPointF b){QPainterPath path(a);const auto bend=std::max(45.,std::abs(b.x()-a.x())*.5);path.cubicTo(a+QPointF(bend,0),b-QPointF(bend,0),b);return path;}
class NodeSearchField final : public QLineEdit {
public:
    NodeSearchField(QListWidget& list,QWidget* parent):QLineEdit(parent),list_(list){}
protected:
    void keyPressEvent(QKeyEvent* event)override{if(event->key()==Qt::Key_Up||event->key()==Qt::Key_Down){if(list_.count())list_.setCurrentRow(std::clamp(list_.currentRow()+(event->key()==Qt::Key_Down?1:-1),0,list_.count()-1));event->accept();return;}QLineEdit::keyPressEvent(event);}
private:QListWidget& list_;
};
}
QtMaterialGraph::QtMaterialGraph(MaterialDocument& document,QWidget* parent):QGraphicsView(parent),document_(document){
    setScene(new QGraphicsScene(this));setRenderHint(QPainter::Antialiasing);setDragMode(QGraphicsView::RubberBandDrag);setTransformationAnchor(QGraphicsView::AnchorUnderMouse);setBackgroundBrush(palette().brush(QPalette::Base));setMinimumSize(350,250);setAcceptDrops(true);setMouseTracking(true);
    QObject::connect(scene(),&QGraphicsScene::selectionChanged,this,[this]{if(selectionChanged)selectionChanged();});
}
QtMaterialGraph::~QtMaterialGraph(){
    cancelInteraction();selectionChanged={};previewChanged={};nodeImage={};
    QObject::disconnect(scene(),nullptr,this,nullptr);
    for(auto* item:scene()->items())if(auto* proxy=dynamic_cast<QGraphicsProxyWidget*>(item);proxy&&proxy->widget()){
        QObject::disconnect(proxy->widget(),nullptr,this,nullptr);for(auto* child:proxy->widget()->findChildren<QWidget*>())QObject::disconnect(child,nullptr,this,nullptr);
    }
    scene()->clear();releaseImages();
}
MaterialNodeId QtMaterialGraph::selectedNode()const{for(auto* item:scene()->selectedItems())if(item->data(NodeRole).isValid()&&!item->data(PinRole).isValid())return item->data(NodeRole).toULongLong();return 0;}
std::optional<QPoint> QtMaterialGraph::pinPosition(MaterialGraphEndpoint endpoint)const{for(auto* item:scene()->items())if(item->data(PinRole).isValid()&&item->data(NodeRole).toULongLong()==endpoint.node&&item->data(PinRole).toInt()==int(endpoint.pin)&&item->data(InputRole).toBool()==endpoint.input)return mapFromScene(item->mapToScene(item->boundingRect().center()));return {};}
void QtMaterialGraph::selectNodes(std::span<const MaterialNodeId> ids){for(auto* item:scene()->items())if(item->flags().testFlag(QGraphicsItem::ItemIsSelectable)&&item->data(NodeRole).isValid()&&!item->data(PinRole).isValid())item->setSelected(std::find(ids.begin(),ids.end(),item->data(NodeRole).toULongLong())!=ids.end());}
std::vector<MaterialNodeId> QtMaterialGraph::selectedNodes()const{std::vector<MaterialNodeId> ids;for(auto* item:scene()->selectedItems())if(item->data(NodeRole).isValid()&&!item->data(PinRole).isValid())ids.push_back(item->data(NodeRole).toULongLong());return ids;}
void QtMaterialGraph::refresh(){
    if(!document_.isOpen()||revision_==document_.previewRevision()||moving_||connecting_||inlineEditing_)return;
    if(preview_&&!findMaterialNode(document_.definition().graph,preview_->node)){preview_.reset();if(previewChanged)previewChanged(preview_);}
    std::erase_if(images_,[&](const auto& entry){return !findMaterialNode(document_.definition().graph,entry.first);});
    const auto selected=selectedNodes();QSignalBlocker block(scene());scene()->clear();temporary_=nullptr;
    const auto& graph=document_.definition().graph;const auto& output=materialOutputNode(graph).output;
    std::map<std::tuple<MaterialNodeId,MaterialPin,bool>,QPointF> positions;
    for(const auto& node:graph.nodes){
        const auto inputs=materialNodeInputs(graph,node,output.shading,output.blend,true),outputs=materialNodeOutputs(graph,node);
        const bool image=!outputs.empty()&&outputs.front().type!=MaterialValueType::Mapping&&outputs.front().type!=MaterialValueType::Normal&&node.kind!=MaterialNodeKind::Time&&node.kind!=MaterialNodeKind::ParticleColor;
        const bool numeric=node.kind==MaterialNodeKind::Scalar||node.kind==MaterialNodeKind::Vector2||node.kind==MaterialNodeKind::Color;
        const qreal width=280,pinHeight=58+26*std::max(inputs.size(),outputs.size()),height=pinHeight+(numeric?30:0)+(image?138:0);
        auto* body=scene()->addRect(0,0,width,height,QPen(palette().color(QPalette::Midlight)),palette().brush(QPalette::Window));body->setPos(node.position[0],node.position[1]);body->setData(NodeRole,qulonglong(node.id));body->setFlags(QGraphicsItem::ItemIsSelectable);body->setFlag(QGraphicsItem::ItemIsMovable,document_.editable());body->setSelected(std::find(selected.begin(),selected.end(),node.id)!=selected.end());body->setZValue(1);
        auto* title=new QGraphicsSimpleTextItem(QString::fromStdString(node.name.empty()?materialNodeKindName(node.kind):node.name),body);title->setBrush(palette().brush(QPalette::Text));title->setPos(10,8);
        const auto pins=[&](const auto& definitions,bool input){for(std::size_t i=0;i<definitions.size();++i){const auto& pin=definitions[i];const qreal y=48+26*i,x=input?0:width;auto* socket=new QGraphicsEllipseItem(x-5,y-5,10,10,body);socket->setBrush(pinColor(pin.type));socket->setPen(Qt::NoPen);socket->setData(NodeRole,qulonglong(node.id));socket->setData(PinRole,int(pin.pin));socket->setData(InputRole,input);socket->setToolTip(QString::fromStdString(std::string(pin.name)+" · "+materialValueTypeName(pin.type)));auto* label=new QGraphicsSimpleTextItem(pin.name,body);label->setBrush(palette().brush(QPalette::WindowText));label->setPos(input?12:width-12-label->boundingRect().width(),y-9);positions[{node.id,pin.pin,input}]=body->pos()+QPointF(x,y);
            if(input&&!materialInputLink(graph,node.id,pin.pin)){
                const auto fallback=materialInputDefault(node,pin.pin);auto probe=node;
                if(fallback.type==MaterialValueType::Scalar&&setMaterialInputDefault(probe,pin.pin,fallback)){
                    auto* field=new QDoubleSpinBox;field->setEnabled(document_.editable());field->setDecimals(4);field->setRange(-1.e9,1.e9);field->setSingleStep(.01);field->setValue(fallback.value[0]);field->setKeyboardTracking(false);field->setFixedWidth(82);auto* proxy=scene()->addWidget(field);proxy->setParentItem(body);proxy->setPos(112,y-12);
                    QObject::connect(field,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,id=node.id,pin=pin.pin,fallback](double value){auto next=fallback;next.value[0]=float(value);editNode(id,"Change material input",[&](auto& target){setMaterialInputDefault(target,pin,next);});});QObject::connect(field,&QDoubleSpinBox::editingFinished,this,[this]{finishInteraction();});
                }
            }
        }};
        pins(inputs,true);pins(outputs,false);
        if(numeric){
            const auto count=node.kind==MaterialNodeKind::Scalar?1:node.kind==MaterialNodeKind::Vector2?2:4;
            auto* row=new QWidget;row->setEnabled(document_.editable());auto* layout=new QHBoxLayout(row);layout->setContentsMargins(0,0,0,0);layout->setSpacing(2);
            for(int component=0;component<count;++component){auto* field=new QDoubleSpinBox(row);field->setDecimals(4);field->setRange(-1.e9,1.e9);field->setSingleStep(.01);field->setKeyboardTracking(false);field->setValue(node.kind==MaterialNodeKind::Scalar?node.scalar:node.kind==MaterialNodeKind::Vector2?node.vector2[std::size_t(component)]:node.color[std::size_t(component)]);field->setFixedWidth((int(width)-24)/count);layout->addWidget(field);QObject::connect(field,qOverload<double>(&QDoubleSpinBox::valueChanged),this,[this,id=node.id,kind=node.kind,component](double value){editNode(id,"Change material value",[&](auto& target){if(kind==MaterialNodeKind::Scalar)target.scalar=float(value);else if(kind==MaterialNodeKind::Vector2)target.vector2[std::size_t(component)]=float(value);else target.color[std::size_t(component)]=float(value);});});QObject::connect(field,&QDoubleSpinBox::editingFinished,this,[this]{finishInteraction();});}
            auto* proxy=scene()->addWidget(row);proxy->setParentItem(body);proxy->setPos(12,pinHeight-5);
        }
        if(image){auto* picture=new QGraphicsPixmapItem(body);picture->setData(PreviewRole,qulonglong(node.id));picture->setData(PinRole,int(outputs.front().pin));picture->setPos((width-128)/2,pinHeight+(numeric?30:0));if(const auto found=images_.find(node.id);found!=images_.end()){picture->setPixmap(QPixmap::fromImage(found->second));picture->setScale(128./std::max(1,found->second.width()));}}
    }
    for(const auto& link:graph.links){auto from=positions.find({link.fromNode,link.fromPin,false}),to=positions.find({link.toNode,link.toPin,true});if(from==positions.end()||to==positions.end())continue;auto* item=scene()->addPath(curve(from->second,to->second),QPen(pinColor(materialOutputType(graph,link.fromNode,link.fromPin)),2));item->setData(LinkRole,qulonglong(link.id));item->setFlags(QGraphicsItem::ItemIsSelectable);}
    scene()->setSceneRect(scene()->itemsBoundingRect().adjusted(-500,-500,500,500));revision_=document_.previewRevision();
}
std::optional<MaterialGraphEndpoint> QtMaterialGraph::pinAt(const QPoint& point)const{
    std::optional<MaterialGraphEndpoint> result;double nearest=9;
    for(auto* item:scene()->items())if(item->data(PinRole).isValid()&&item->data(NodeRole).isValid()&&item->data(InputRole).isValid()){
        const auto center=mapFromScene(item->mapToScene(item->boundingRect().center()));const auto distance=std::hypot(double(center.x()-point.x()),double(center.y()-point.y()));
        if(distance<nearest){nearest=distance;result=MaterialGraphEndpoint{item->data(NodeRole).toULongLong(),static_cast<MaterialPin>(item->data(PinRole).toInt()),item->data(InputRole).toBool()};}
    }return result;
}
void QtMaterialGraph::finishInteraction(){if(inlineEditing_&&document_.transactionActive()&&!document_.commit())document_.cancel();inlineEditing_=false;}
void QtMaterialGraph::cancelInteraction(){if(inlineEditing_&&document_.transactionActive())document_.cancel();inlineEditing_=false;moving_=false;connecting_.reset();if(temporary_){delete temporary_;temporary_=nullptr;}revision_=UINT64_MAX;}
void QtMaterialGraph::editNode(MaterialNodeId id,const std::string& label,const std::function<void(MaterialNode&)>& mutation,bool final){if(!document_.editable())return;if(!document_.transactionActive()&&!document_.begin(label))return;inlineEditing_=true;if(!document_.update([&](auto& asset){if(auto* node=findMaterialNode(asset.graph,id))mutation(*node);})){document_.cancel();inlineEditing_=false;}if(final)finishInteraction();}
void QtMaterialGraph::refreshImages(){if(!nodeImage||!isVisible())return;const auto visible=mapToScene(viewport()->rect()).boundingRect();for(auto* item:scene()->items(visible,Qt::IntersectsItemBoundingRect))if(item->data(PreviewRole).isValid()){
    const auto id=item->data(PreviewRole).toULongLong();QImage image;try{image=nodeImage({id,static_cast<MaterialPin>(item->data(PinRole).toInt())});}catch(...){continue;}if(image.isNull())continue;auto old=images_.find(id);if(old!=images_.end()&&old->second.cacheKey()==image.cacheKey())continue;images_[id]=image;if(auto* picture=dynamic_cast<QGraphicsPixmapItem*>(item)){picture->setPixmap(QPixmap::fromImage(image));picture->setScale(128./std::max(1,image.width()));}
}}
void QtMaterialGraph::togglePreview(MaterialNodeId id,std::optional<MaterialPin> pin){const auto* node=findMaterialNode(document_.definition().graph,id);if(!node||node->kind==MaterialNodeKind::Output||(preview_&&preview_->node==id&&(!pin||preview_->pin==*pin)))preview_.reset();else{const auto outputs=materialNodeOutputs(document_.definition().graph,*node);if(outputs.empty())return;preview_=MaterialGraphPreviewTarget{id,pin?*pin:outputs.front().pin};}if(previewChanged)previewChanged(preview_);}
void QtMaterialGraph::disconnect(std::optional<MaterialGraphEndpoint> endpoint){finishInteraction();const auto selected=selectedNodes();document_.edit("Break material links",[&](auto& asset){std::erase_if(asset.graph.links,[&](const auto& link){if(endpoint)return endpoint->input?link.toNode==endpoint->node&&link.toPin==endpoint->pin:link.fromNode==endpoint->node&&link.fromPin==endpoint->pin;return std::find(selected.begin(),selected.end(),link.fromNode)!=selected.end()||std::find(selected.begin(),selected.end(),link.toNode)!=selected.end();});});refresh();}
void QtMaterialGraph::search(QPointF position,std::optional<MaterialGraphEndpoint> from){
    if(!document_.editable())return;finishInteraction();QDialog popup(this,Qt::Popup);auto* layout=new QVBoxLayout(&popup);auto* list=new QListWidget(&popup);auto* filter=new NodeSearchField(*list,&popup);filter->setPlaceholderText("Search nodes");layout->addWidget(filter);list->setMinimumSize(260,320);layout->addWidget(list);
    const auto choices=from?materialGraphNodeChoices(document_.definition().graph,*from):std::vector<MaterialGraphNodeChoice>{};
    const auto populate=[&]{list->clear();if(from){for(std::size_t i=0;i<choices.size();++i){const auto& choice=choices[i];QString name=QString::fromUtf8(materialNodeKindName(choice.kind));if(!name.contains(filter->text(),Qt::CaseInsensitive)&&!(choice.kind==MaterialNodeKind::Clamp&&QStringLiteral("Saturate").contains(filter->text(),Qt::CaseInsensitive)))continue;auto* item=new QListWidgetItem(name+" · "+QString::fromUtf8(materialPinName(choice.pin)),list);item->setData(Qt::UserRole,qulonglong(i));}}else for(auto kind:materialGraphNodeKinds()){const auto name=QString::fromUtf8(materialNodeKindName(kind));if(!name.contains(filter->text(),Qt::CaseInsensitive)&&!(kind==MaterialNodeKind::Clamp&&QStringLiteral("Saturate").contains(filter->text(),Qt::CaseInsensitive)))continue;auto* item=new QListWidgetItem(name,list);item->setData(Qt::UserRole,int(kind));}if(list->count())list->setCurrentRow(0);};
    QObject::connect(filter,&QLineEdit::textChanged,&popup,populate);QObject::connect(list,&QListWidget::itemActivated,&popup,[&](QListWidgetItem*){popup.accept();});QObject::connect(filter,&QLineEdit::returnPressed,&popup,[&]{if(list->currentItem())popup.accept();});populate();popup.move(mapToGlobal(mapFromScene(position)));filter->setFocus();
    if(popup.exec()!=QDialog::Accepted||!list->currentItem())return;const auto choice=list->currentItem()->data(Qt::UserRole).toULongLong();MaterialNodeId added{};const Vec2 point{float(position.x()),float(position.y())};
    if(document_.edit("Create material node",[&](auto& asset){added=from?addConnectedMaterialGraphNode(asset.graph,*from,choices.at(std::size_t(choice)),point):addMaterialNode(asset.graph,static_cast<MaterialNodeKind>(choice),point).id;})){refresh();const std::array<MaterialNodeId,1> selected{added};selectNodes(selected);}
}
void QtMaterialGraph::arrange(int axis,int alignment,bool distribute){
    struct Item{MaterialNodeId id;QPointF position;QSizeF size;};std::vector<Item> entries;for(auto* item:scene()->selectedItems())if(item->data(NodeRole).isValid()&&!item->data(PinRole).isValid())entries.push_back({item->data(NodeRole).toULongLong(),item->pos(),item->boundingRect().size()});if(entries.size()<(distribute?3u:2u))return;
    const auto coordinate=[axis](const Item& item){return axis?item.position.y():item.position.x();};const auto extent=[axis](const Item& item){return axis?item.size.height():item.size.width();};std::map<MaterialNodeId,Vec2> positions;
    if(distribute){std::sort(entries.begin(),entries.end(),[&](const auto& a,const auto& b){const auto ca=coordinate(a)+extent(a)*.5,cb=coordinate(b)+extent(b)*.5;return ca!=cb?ca<cb:a.id<b.id;});const auto first=coordinate(entries.front())+extent(entries.front())*.5,last=coordinate(entries.back())+extent(entries.back())*.5;for(std::size_t i=1;i+1<entries.size();++i){auto p=entries[i].position;const auto value=first+(last-first)*i/(entries.size()-1)-extent(entries[i])*.5;if(axis)p.setY(value);else p.setX(value);positions[entries[i].id]={float(p.x()),float(p.y())};}}
    else{double minimum=coordinate(entries.front()),maximum=minimum+extent(entries.front());for(const auto& entry:entries){minimum=std::min(minimum,coordinate(entry));maximum=std::max(maximum,coordinate(entry)+extent(entry));}const auto target=alignment==0?minimum:alignment==1?(minimum+maximum)*.5:maximum;for(const auto& entry:entries){auto p=entry.position;const auto value=target-extent(entry)*alignment*.5;if(axis)p.setY(value);else p.setX(value);positions[entry.id]={float(p.x()),float(p.y())};}}
    document_.edit(distribute?"Distribute material nodes":"Align material nodes",[&](auto& asset){for(auto& node:asset.graph.nodes)if(auto found=positions.find(node.id);found!=positions.end())node.position=found->second;});refresh();
}
void QtMaterialGraph::updateConnections(){for(auto* item:scene()->items())if(item->data(LinkRole).isValid()){const auto id=item->data(LinkRole).toULongLong();auto link=std::find_if(document_.definition().graph.links.begin(),document_.definition().graph.links.end(),[id](const auto& value){return value.id==id;});if(link==document_.definition().graph.links.end())continue;const auto from=pinPosition({link->fromNode,link->fromPin,false}),to=pinPosition({link->toNode,link->toPin,true});if(from&&to)if(auto* path=dynamic_cast<QGraphicsPathItem*>(item))path->setPath(curve(mapToScene(*from),mapToScene(*to)));}}
void QtMaterialGraph::contextMenuEvent(QContextMenuEvent* event){
    if(panned_){panned_=false;return;}if(!document_.editable())return;finishInteraction();
    auto* hit=itemAt(event->pos());for(auto* parent=hit;parent;parent=parent->parentItem())if(parent->parentItem()==nullptr&&parent->data(NodeRole).isValid()&&!parent->isSelected()){scene()->clearSelection();parent->setSelected(true);break;}
    const auto endpoint=pinAt(event->pos());const auto position=mapToScene(event->pos());QMenu menu(this);
    menu.addAction(endpoint?"Create Connected Node…":"Search / Create Node…",this,[this,position,endpoint]{search(position,endpoint);});
    if(endpoint){if(!endpoint->input)menu.addAction(preview_&&preview_->node==endpoint->node&&preview_->pin==endpoint->pin?"Stop Previewing":"Start Previewing",this,[this,endpoint]{togglePreview(endpoint->node,endpoint->pin);});menu.addAction("Break Links",this,[this,endpoint]{disconnect(endpoint);});}
    else if(const auto id=selectedNode();id)menu.addAction(preview_&&preview_->node==id?"Stop Previewing":"Start Previewing",this,[this,id]{togglePreview(id);});
    if(hit&&hit->data(LinkRole).isValid()){const auto id=hit->data(LinkRole).toULongLong();menu.addAction("Disconnect",this,[this,id]{document_.edit("Disconnect material nodes",[&](auto& asset){removeMaterialLink(asset.graph,id);});refresh();});}
    menu.addSeparator();menu.addAction("Copy",QKeySequence::Copy,this,[this]{copy();});menu.addAction("Cut",QKeySequence::Cut,this,[this]{copy();deleteSelection();});menu.addAction("Paste",QKeySequence::Paste,this,[this]{paste();});menu.addAction("Duplicate",QKeySequence(Qt::CTRL|Qt::Key_D),this,[this]{const auto ids=selectedNodes();std::vector<MaterialNodeId> added;document_.edit("Duplicate material nodes",[&](auto& asset){added=duplicateMaterialGraphSelection(asset.graph,ids);});refresh();selectNodes(added);});menu.addAction("Delete",QKeySequence::Delete,this,[this]{deleteSelection();});
    menu.addAction("Break All Links",this,[this]{disconnect();});menu.addAction("Straighten Connections",QKeySequence(Qt::Key_Q),this,[this]{straighten();});menu.addAction("Frame Selection",QKeySequence(Qt::Key_F),this,[this]{QRectF bounds;for(auto* item:scene()->selectedItems())bounds|=item->sceneBoundingRect();fitInView(bounds.isEmpty()?scene()->itemsBoundingRect():bounds,Qt::KeepAspectRatio);});
    auto* align=menu.addMenu("Align");align->setEnabled(selectedNodes().size()>=2);const char* labels[]{"Left","Center","Right","Top","Middle","Bottom"};for(int i=0;i<6;++i)align->addAction(labels[i],this,[this,i]{arrange(i/3,i%3,false);});auto* distribute=menu.addMenu("Distribute");distribute->setEnabled(selectedNodes().size()>=3);distribute->addAction("Horizontally",this,[this]{arrange(0,0,true);});distribute->addAction("Vertically",this,[this]{arrange(1,0,true);});menu.exec(event->globalPos());
}
void QtMaterialGraph::mousePressEvent(QMouseEvent* event){
    if(event->button()==Qt::RightButton){panning_=true;panned_=false;panPoint_=event->pos();setCursor(Qt::ClosedHandCursor);event->accept();return;}
    for(auto* item=itemAt(event->pos());item;item=item->parentItem())if(dynamic_cast<QGraphicsProxyWidget*>(item)){moving_=false;QGraphicsView::mousePressEvent(event);return;}
    if(event->button()==Qt::LeftButton&&document_.editable()){
        if(auto pin=pinAt(event->pos())){if(event->modifiers().testFlag(Qt::AltModifier)){disconnect(pin);event->accept();return;}finishInteraction();press_=mapToScene(event->pos());connecting_=pin;temporary_=scene()->addPath({},QPen(palette().color(QPalette::Accent),2,Qt::DashLine));temporary_->setZValue(3);event->accept();return;}
        if(!itemAt(event->pos())){std::optional<MaterialNodeKind> kind;if(held_.contains(Qt::Key_1))kind=MaterialNodeKind::Scalar;else if(held_.contains(Qt::Key_2))kind=MaterialNodeKind::Vector2;else if(held_.contains(Qt::Key_3))kind=MaterialNodeKind::Color;else if(held_.contains(Qt::Key_M))kind=MaterialNodeKind::Multiply;else if(held_.contains(Qt::Key_T))kind=MaterialNodeKind::Texture2D;if(kind){const auto point=mapToScene(event->pos());MaterialNodeId added{};finishInteraction();document_.edit("Create material node",[&](auto& asset){added=addMaterialNode(asset.graph,*kind,{float(point.x()),float(point.y())}).id;});refresh();const std::array<MaterialNodeId,1> ids{added};selectNodes(ids);creationGesture_=true;event->accept();return;}}
        if(auto* image=itemAt(event->pos());image&&image->data(PreviewRole).isValid()&&chooseTexture){const auto id=image->data(PreviewRole).toULongLong();const auto* node=findMaterialNode(document_.definition().graph,id);if(node&&(node->kind==MaterialNodeKind::Texture2D||node->kind==MaterialNodeKind::ImageInput)){chooseTexture(id);event->accept();return;}}
    }
    press_=mapToScene(event->pos());moving_=event->button()==Qt::LeftButton;QGraphicsView::mousePressEvent(event);
}
void QtMaterialGraph::mouseMoveEvent(QMouseEvent* event){
    if(panning_){const auto delta=event->pos()-panPoint_;if(delta.manhattanLength()>1)panned_=true;horizontalScrollBar()->setValue(horizontalScrollBar()->value()-delta.x());verticalScrollBar()->setValue(verticalScrollBar()->value()-delta.y());panPoint_=event->pos();event->accept();return;}
    if(connecting_){temporary_->setPath(connecting_->input?curve(mapToScene(event->pos()),press_):curve(press_,mapToScene(event->pos())));for(auto* item:scene()->items())if(item->data(InputRole).isValid()&&item->data(NodeRole).isValid()){MaterialGraphEndpoint target{item->data(NodeRole).toULongLong(),static_cast<MaterialPin>(item->data(PinRole).toInt()),item->data(InputRole).toBool()};bool valid=false;if(target.input!=connecting_->input){const auto source=target.input?*connecting_:target,destination=target.input?target:*connecting_;valid=inspectMaterialGraphConnection(document_.definition().graph,source.node,source.pin,destination.node,destination.pin).kind!=MaterialGraphConnectionKind::Invalid;}item->setOpacity(valid?1:.25);}event->accept();return;}
    QGraphicsView::mouseMoveEvent(event);if(moving_)updateConnections();
}
void QtMaterialGraph::mouseReleaseEvent(QMouseEvent* event){
    if(event->button()==Qt::RightButton&&panning_){panning_=false;unsetCursor();event->accept();return;}
    if(creationGesture_){creationGesture_=false;event->accept();return;}
    if(connecting_){auto from=*connecting_;const auto target=pinAt(event->pos());connecting_.reset();if(temporary_){delete temporary_;temporary_=nullptr;}for(auto* item:scene()->items())if(item->data(InputRole).isValid())item->setOpacity(1);if(target&&from.input!=target->input){auto to=*target;if(from.input)std::swap(from,to);document_.edit("Connect material nodes",[&](auto& asset){connectMaterialGraphAuthoring(asset.graph,from.node,from.pin,to.node,to.pin);});}else if(!target&&!itemAt(event->pos()))search(mapToScene(event->pos()),from);refresh();event->accept();return;}
    QGraphicsView::mouseReleaseEvent(event);if(moving_&&document_.editable()){std::map<MaterialNodeId,Vec2> positions;for(auto* item:scene()->items())if(item->data(NodeRole).isValid()&&!item->data(PinRole).isValid()&&item->parentItem()==nullptr)positions[item->data(NodeRole).toULongLong()]={float(item->pos().x()),float(item->pos().y())};document_.edit("Move material nodes",[&](auto& asset){for(auto& node:asset.graph.nodes)if(auto p=positions.find(node.id);p!=positions.end())node.position=p->second;});}moving_=false;refresh();
}
void QtMaterialGraph::mouseDoubleClickEvent(QMouseEvent* event){
    if(auto pin=pinAt(event->pos());pin&&!pin->input){togglePreview(pin->node,pin->pin);return;}
    auto* item=itemAt(event->pos());if(dynamic_cast<QGraphicsProxyWidget*>(item)){QGraphicsView::mouseDoubleClickEvent(event);return;}for(auto* parent=item;parent;parent=parent->parentItem())if(parent->data(NodeRole).isValid()&&!parent->data(PinRole).isValid()){togglePreview(parent->data(NodeRole).toULongLong());return;}QGraphicsView::mouseDoubleClickEvent(event);
}
void QtMaterialGraph::deleteSelection(){finishInteraction();const auto ids=selectedNodes();std::vector<MaterialLinkId> links;for(auto* item:scene()->selectedItems())if(item->data(LinkRole).isValid())links.push_back(item->data(LinkRole).toULongLong());document_.edit("Delete material selection",[&](auto& asset){deleteMaterialGraphSelection(asset.graph,ids);for(auto id:links)removeMaterialLink(asset.graph,id);});refresh();}
void QtMaterialGraph::copy(){const auto ids=selectedNodes();QApplication::clipboard()->setText(QString::fromStdString(copyMaterialGraphSelection(document_.definition().graph,ids)));}
void QtMaterialGraph::paste(){finishInteraction();const auto center=mapToScene(viewport()->mapFromGlobal(QCursor::pos()));std::vector<MaterialNodeId> ids;document_.edit("Paste material nodes",[&](auto& asset){ids=pasteMaterialGraphSelection(asset.graph,QApplication::clipboard()->text().toStdString(),{float(center.x()),float(center.y())});});refresh();selectNodes(ids);}
void QtMaterialGraph::keyPressEvent(QKeyEvent* event){
    if(event->key()==Qt::Key_Escape){cancelInteraction();refresh();event->accept();return;}
    if(dynamic_cast<QGraphicsProxyWidget*>(scene()->focusItem())){QGraphicsView::keyPressEvent(event);return;}
    held_.insert(event->key());if(event->matches(QKeySequence::Copy)){copy();return;}if(event->matches(QKeySequence::Paste)){paste();return;}if(event->matches(QKeySequence::Cut)){copy();deleteSelection();return;}
    if(event->matches(QKeySequence::SelectAll)){std::vector<MaterialNodeId> ids;for(const auto& node:document_.definition().graph.nodes)ids.push_back(node.id);selectNodes(ids);return;}
    if(event->modifiers().testFlag(Qt::ControlModifier)&&event->key()==Qt::Key_D){const auto ids=selectedNodes();std::vector<MaterialNodeId> added;document_.edit("Duplicate material nodes",[&](auto& asset){added=duplicateMaterialGraphSelection(asset.graph,ids);});refresh();selectNodes(added);return;}
    if(event->modifiers().testFlag(Qt::ControlModifier)&&event->key()==Qt::Key_P){const auto ids=selectedNodes();if(ids.size()==1)togglePreview(ids.front());else{preview_.reset();if(previewChanged)previewChanged(preview_);}return;}
    if(event->key()==Qt::Key_Delete){deleteSelection();return;}if(event->key()==Qt::Key_Space){search(mapToScene(viewport()->mapFromGlobal(QCursor::pos())));return;}if(event->key()==Qt::Key_Q){straighten();return;}
    if(event->key()==Qt::Key_F||event->key()==Qt::Key_Home){QRectF bounds;if(event->key()==Qt::Key_F)for(auto* item:scene()->selectedItems())bounds|=item->sceneBoundingRect();fitInView(bounds.isEmpty()?scene()->itemsBoundingRect():bounds,Qt::KeepAspectRatio);return;}QGraphicsView::keyPressEvent(event);
}
void QtMaterialGraph::keyReleaseEvent(QKeyEvent* event){if(!event->isAutoRepeat())held_.erase(event->key());QGraphicsView::keyReleaseEvent(event);}
void QtMaterialGraph::focusOutEvent(QFocusEvent* event){held_.clear();if(inlineEditing_)finishInteraction();if(moving_||connecting_)cancelInteraction();QGraphicsView::focusOutEvent(event);}
void QtMaterialGraph::wheelEvent(QWheelEvent* event){const auto factor=std::pow(1.0015,event->angleDelta().y());const auto zoom=transform().m11();if(zoom*factor>.15&&zoom*factor<3)scale(factor,factor);event->accept();}
void QtMaterialGraph::dragEnterEvent(QDragEnterEvent* event){if(document_.editable()&&event->mimeData()->hasFormat("application/x-crimson-assets"))event->acceptProposedAction();}
void QtMaterialGraph::dragMoveEvent(QDragMoveEvent* event){if(document_.editable()&&event->mimeData()->hasFormat("application/x-crimson-assets"))event->acceptProposedAction();}
void QtMaterialGraph::dropEvent(QDropEvent* event){
    if(!document_.editable()||!validTexture)return;try{const auto payload=nlohmann::json::parse(event->mimeData()->data("application/x-crimson-assets").toStdString());if(!payload.is_array()||payload.empty())return;std::vector<std::string> ids;for(const auto& row:payload){const auto id=row.at("id").get<std::string>();if(row.value("folder",false)||!validTexture(id))return;ids.push_back(id);}finishInteraction();auto* hit=itemAt(event->position().toPoint());MaterialNodeId target{};for(auto* item=hit;item;item=item->parentItem())if(item->data(NodeRole).isValid())target=item->data(NodeRole).toULongLong();if(!target&&hit&&hit->data(PreviewRole).isValid())target=hit->data(PreviewRole).toULongLong();const auto* node=findMaterialNode(document_.definition().graph,target);
        if(ids.size()==1&&node&&(node->kind==MaterialNodeKind::Texture2D||node->kind==MaterialNodeKind::ImageInput))document_.edit("Assign material texture",[&](auto& asset){findMaterialNode(asset.graph,target)->texture.image.id=ids.front();});
        else{const auto position=mapToScene(event->position().toPoint());std::vector<MaterialNodeId> created;document_.edit("Create texture nodes",[&](auto& asset){if(asset.graph.nodes.size()+ids.size()>MaterialGraph::MaximumNodes)throw std::runtime_error("Material node limit exceeded");for(std::size_t i=0;i<ids.size();++i){auto& added=addMaterialNode(asset.graph,MaterialNodeKind::Texture2D,{float(position.x()+i*32),float(position.y()+i*32)});added.texture.image.id=ids[i];created.push_back(added.id);}});refresh();selectNodes(created);}refresh();event->acceptProposedAction();
    }catch(const std::exception&){ }
}
void QtMaterialGraph::straighten(){const auto& graph=document_.definition().graph;const auto selected=selectedNodes();
    using Neighbors=std::map<MaterialNodeId,std::vector<std::pair<float,float>>>;
    using Connections=std::map<MaterialNodeId,Neighbors>;
    Connections outputs,inputs;
    std::map<MaterialNodeId,Vec2> positions;
    for(auto id:selected)if(const auto* node=findMaterialNode(graph,id))positions.emplace(id,node->position);
    const auto pinY=[&](MaterialNodeId id,MaterialPin semantic,bool input)->std::optional<float> {for(auto* item:scene()->items())if(item->data(NodeRole).toULongLong()==id&&item->data(PinRole).isValid()&&item->data(InputRole).isValid()&&item->data(PinRole).toInt()==int(semantic)&&item->data(InputRole).toBool()==input)return float(item->mapToScene(item->boundingRect().center()).y())-positions.at(id)[1];return {};};
    for(const auto& link:graph.links) {
        if(!positions.contains(link.fromNode) || !positions.contains(link.toNode))continue;
        const auto from=pinY(link.fromNode,link.fromPin,false),to=pinY(link.toNode,link.toPin,true);
        if(!from || !to)continue; // A dormant model pin is not on this canvas.
        outputs[link.fromNode][link.toNode].emplace_back(*from,*to);
        inputs[link.toNode][link.fromNode].emplace_back(*to,*from);
    }
    // Like Unreal's Straighten Connections: favor the middle of a connected
    // chain, then walk selected neighbors and match their mean pin height.
    // Conflicting branches are resolved by deterministic traversal, not by
    // changing horizontal spacing or moving disconnected selected nodes.
    const auto chainLength=[](const Connections& connections,MaterialNodeId node,std::map<MaterialNodeId,std::size_t>& cache,auto&& self)->std::size_t {
        if(const auto it=cache.find(node);it!=cache.end())return it->second;
        cache[node]=0;
        if(const auto it=connections.find(node);it!=connections.end() && it->second.size()==1)
            cache[node]=1+self(connections,it->second.begin()->first,cache,self);
        return cache[node];
    };
    std::map<MaterialNodeId,std::size_t> outputChains,inputChains;
    std::vector<std::pair<std::size_t,MaterialNodeId>> order;
    for(const auto& [id,position]:positions) {
        (void)position;std::size_t rank{};
        if(const auto it=outputs.find(id);it!=outputs.end())for(const auto& [neighbor,pins]:it->second){(void)pins;rank+=1+chainLength(outputs,neighbor,outputChains,chainLength);}
        if(const auto it=inputs.find(id);it!=inputs.end())for(const auto& [neighbor,pins]:it->second){(void)pins;rank+=1+chainLength(inputs,neighbor,inputChains,chainLength);}
        order.emplace_back(rank,id);
    }
    std::sort(order.begin(),order.end(),[](const auto& a,const auto& b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
    std::set<MaterialNodeId> visited;
    const auto align=[&](MaterialNodeId anchor,const Connections& connections,auto&& self)->void {
        const auto it=connections.find(anchor);if(it==connections.end())return;
        for(const auto& [neighbor,pins]:it->second) {
            if(visited.contains(neighbor))continue;
            float delta{};for(const auto& [from,to]:pins)delta+=(positions.at(anchor)[1]+from)-(positions.at(neighbor)[1]+to);
            positions.at(neighbor)[1]+=delta/static_cast<float>(pins.size());
            visited.insert(anchor);visited.insert(neighbor);self(neighbor,connections,self);
        }
    };
    for(const auto& [rank,id]:order){(void)rank;align(id,outputs,align);align(id,inputs,align);}
    std::vector<std::pair<MaterialNodeId,Vec2>> changed;
    for(const auto& [id,position]:positions)if(std::abs(position[1]-findMaterialNode(graph,id)->position[1])>.001f)changed.emplace_back(id,position);
    document_.edit("Straighten connections",[&](auto& asset){for(const auto& [id,position]:changed)if(auto* node=findMaterialNode(asset.graph,id))node->position=position;});refresh();
}
}
