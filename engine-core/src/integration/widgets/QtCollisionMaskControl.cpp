#include "QtCollisionMaskControl.h"
#include <QAbstractItemView>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QStandardItemModel>
#include <QStylePainter>
#include <bit>

namespace crimson {
QtCollisionMaskControl::QtCollisionMaskControl(QWidget* parent):QComboBox(parent){
    auto* rows=new QStandardItemModel(this);setModel(rows);setMaxVisibleItems(16);
    for(unsigned i=0;i<32;++i){auto* item=new QStandardItem;item->setCheckable(true);item->setData(i,Qt::UserRole);rows->appendRow(item);}
    view()->viewport()->installEventFilter(this);view()->installEventFilter(this);
    itemConnection_=connect(rows,&QStandardItemModel::itemChanged,this,[this](QStandardItem* item){if(syncing_)return;const auto bit=std::uint32_t{1}<<item->data(Qt::UserRole).toUInt();if(!(editable_&bit)){syncing_=true;item->setCheckState(value_&bit?Qt::Checked:Qt::Unchecked);syncing_=false;return;}publish(item->checkState()==Qt::Checked?value_|bit:value_&~bit);});
    synchronize(0,defaultCollisionLayerNames());
}
QtCollisionMaskControl::~QtCollisionMaskControl(){syncing_=true;changed={};QObject::disconnect(itemConnection_);view()->removeEventFilter(this);view()->viewport()->removeEventFilter(this);}
void QtCollisionMaskControl::synchronize(std::uint32_t value,const CollisionLayerNames& names,std::uint32_t editableBits){
    syncing_=true;value_=value;editable_=editableBits;auto* rows=static_cast<QStandardItemModel*>(model());
    for(unsigned i=0;i<32;++i){auto* item=rows->item(int(i));const auto bit=std::uint32_t{1}<<i;item->setText(QString::number(i+1)+": "+QString::fromStdString(names[i].empty()?"Layer "+std::to_string(i+1):names[i]));item->setCheckState(value&bit?Qt::Checked:Qt::Unchecked);item->setEnabled(bool(editableBits&bit));}
    syncing_=false;update();
}
void QtCollisionMaskControl::publish(std::uint32_t value){value=(value&editable_)|(value_&~editable_);if(value==value_)return;value_=value;update();if(changed)changed(value_);}
void QtCollisionMaskControl::resetTo(std::uint32_t defaults){publish(defaults);syncing_=true;auto* rows=static_cast<QStandardItemModel*>(model());for(unsigned i=0;i<32;++i)rows->item(int(i))->setCheckState(value_&(std::uint32_t{1}<<i)?Qt::Checked:Qt::Unchecked);syncing_=false;}
bool QtCollisionMaskControl::eventFilter(QObject* object,QEvent* event){
    if(object==view()->viewport()&&(event->type()==QEvent::MouseButtonPress||event->type()==QEvent::MouseButtonRelease)){
        auto* mouse=static_cast<QMouseEvent*>(event);if(mouse->button()!=Qt::LeftButton)return QComboBox::eventFilter(object,event);const auto index=view()->indexAt(mouse->position().toPoint());if(index.isValid()&&event->type()==QEvent::MouseButtonRelease&&index.flags().testFlag(Qt::ItemIsEnabled)){auto* rows=static_cast<QStandardItemModel*>(model());auto* item=rows->itemFromIndex(index);item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);}return true;
    }
    if(object==view()&&event->type()==QEvent::KeyPress){auto* key=static_cast<QKeyEvent*>(event);if(key->key()==Qt::Key_Space){const auto index=view()->currentIndex();if(index.isValid()&&index.flags().testFlag(Qt::ItemIsEnabled)){auto* item=static_cast<QStandardItemModel*>(model())->itemFromIndex(index);item->setCheckState(item->checkState()==Qt::Checked?Qt::Unchecked:Qt::Checked);}return true;}}
    return QComboBox::eventFilter(object,event);
}
void QtCollisionMaskControl::paintEvent(QPaintEvent*){QStylePainter painter(this);QStyleOptionComboBox option;initStyleOption(&option);const auto count=std::popcount(value_);option.currentText=count==0?QStringLiteral("None"):count==32?QStringLiteral("All"):QString::number(count)+" selected";painter.drawComplexControl(QStyle::CC_ComboBox,option);painter.drawControl(QStyle::CE_ComboBoxLabel,option);}
}
