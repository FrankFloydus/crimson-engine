#include "VertexPaintTool.h"
#include "assets/AssetLibrary.h"
#include "assets/MaterialAsset.h"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_panel_section_header.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDockWidget>
#include <QFormLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMainWindow>
#include <QPointer>
#include <QSignalBlocker>
#include <QVBoxLayout>
#include <algorithm>

namespace crimson {
struct VertexPaintTool::QtControls {
    QObject connections;
    QPointer<QDockWidget> dock;
    QComboBox* slotSelector{};
    QListWidget* layers{};
    QDoubleSpinBox *radius{},*strength{},*hardness{};
    QCheckBox* subdivisions{};
    QLabel* diagnostic{};
    std::uint64_t sourceRevision{};
    EntityId entity{};
    std::string palette;
    ~QtControls(){delete dock.data();}
};

VertexPaintTool::~VertexPaintTool(){qtControls_.reset();}

bool VertexPaintTool::controlsOwnInput() const {
    if(!qtControls_||!qtControls_->dock||!qtControls_->dock->isVisible())return false;
    const auto dock=qtControls_->dock.data();
    const auto focus=QApplication::focusWidget();
    const auto popup=QApplication::activePopupWidget();
    return (focus&&(focus==dock||dock->isAncestorOf(focus)))||
        (popup&&(popup==dock||dock->isAncestorOf(popup)));
}

void VertexPaintTool::hideControls() {
    previewMaterials_.clear();
    if(qtControls_&&qtControls_->dock)qtControls_->dock->hide();
}

void VertexPaintTool::drawBrushSettings() {
    if(!qtControls_||!qtControls_->dock)return;
    auto& ui=*qtControls_;
    const QSignalBlocker a(ui.radius),b(ui.strength),c(ui.hardness),d(ui.subdivisions);
    ui.radius->setValue(brush_.radius);
    ui.strength->setValue(brush_.strength);
    ui.hardness->setValue(brush_.hardness);
    ui.subdivisions->setChecked(showSubdivisions_);
}

void VertexPaintTool::drawControls(QMainWindow& window,SceneDocument& document,AssetLibrary& assets,Renderer& renderer) {
    previewMaterials_.clear();
    if(!enabled_){hideControls();return;}
    if(!qtControls_||!qtControls_->dock) {
        qtControls_=std::make_shared<QtControls>();
        auto& ui=*qtControls_;
        ui.dock=new QDockWidget(QStringLiteral("Vertex Paint Tool"),&window);
        ui.dock->setObjectName(QStringLiteral("CrimsonVertexPaintTool"));
        ui.dock->setFeatures(QDockWidget::DockWidgetMovable|QDockWidget::DockWidgetFloatable);
        ui.dock->setAllowedAreas(Qt::LeftDockWidgetArea|Qt::RightDockWidgetArea);
        auto body=new QWidget(ui.dock);
        auto layout=new QVBoxLayout(body);
        layout->setContentsMargins(8,8,8,8);
        const auto section=[&](const QString& title) {
            auto header=new quader::QtPanelSectionHeader(body);
            header->setText(title);header->setChecked(true);
            header->setArrowType(Qt::DownArrow);layout->addWidget(header);
            auto content=new QWidget(body);layout->addWidget(content);
            QObject::connect(header,&QToolButton::toggled,&ui.connections,[header,content](bool open){
                header->setArrowType(open?Qt::DownArrow:Qt::RightArrow);content->setVisible(open);
            });
            return content;
        };
        auto materials=section(QStringLiteral("Materials"));
        auto materialLayout=new QVBoxLayout(materials);
        materialLayout->setContentsMargins(0,0,0,0);
        auto materialForm=new QFormLayout;
        materialLayout->addLayout(materialForm);
        ui.slotSelector=new QComboBox(materials);
        materialForm->addRow(QStringLiteral("Material Slot"),ui.slotSelector);
        ui.layers=new QListWidget(materials);
        ui.layers->setIconSize(QSize(48,48));
        materialLayout->addWidget(ui.layers);
        auto brush=section(QStringLiteral("Brush"));
        auto form=new QFormLayout(brush);
        form->setContentsMargins(0,0,0,0);
        const auto spin=[&](const char* name,double minimum,double maximum,double step){
            auto value=new quader::QtDoubleSpinBox(brush);
            value->setDecimals(3);value->setRange(minimum,maximum);value->setSingleStep(step);
            form->addRow(QString::fromUtf8(name),value);return value;
        };
        ui.radius=spin("Radius (m)",.01,1000,.05);
        ui.strength=spin("Strength",0,1,.05);
        ui.hardness=spin("Hardness",0,1,.05);
        ui.subdivisions=new QCheckBox(QStringLiteral("Show Subdivisions"),body);
        layout->addWidget(ui.subdivisions);
        ui.diagnostic=new QLabel(body);ui.diagnostic->setWordWrap(true);
        layout->addWidget(ui.diagnostic);
        ui.dock->setWidget(body);
        window.addDockWidget(Qt::LeftDockWidgetArea,ui.dock);
        const auto changed=[this]{if(requestFrame_)requestFrame_();};
        QObject::connect(ui.radius,&QDoubleSpinBox::valueChanged,&ui.connections,[this,changed](double value){brush_.radius=float(value);changed();});
        QObject::connect(ui.strength,&QDoubleSpinBox::valueChanged,&ui.connections,[this,changed](double value){brush_.strength=float(value);changed();});
        QObject::connect(ui.hardness,&QDoubleSpinBox::valueChanged,&ui.connections,[this,changed](double value){brush_.hardness=float(value);changed();});
        QObject::connect(ui.subdivisions,&QCheckBox::toggled,&ui.connections,[this,changed](bool value){showSubdivisions_=value;changed();});
        QObject::connect(ui.layers,&QListWidget::currentRowChanged,&ui.connections,[this,changed](int row){
            if(row>=0)if(auto item=qtControls_->layers->item(row))selectLayer(item->data(Qt::UserRole).toUInt());
            changed();
        });
        QObject::connect(ui.slotSelector,&QComboBox::activated,&ui.connections,[this,changed](int){
            paletteSlot_=qtControls_->slotSelector->currentData().toUInt();qtControls_->palette.clear();changed();
        });
    }
    auto& ui=*qtControls_;
    ui.dock->show();
    struct Palette{std::uint32_t slot;std::shared_ptr<const MaterialAsset> material;};
    std::vector<Palette> palettes;
    const auto record=document.scene().find(document.selection().primary);
    if(record&&record->model)if(const auto model=assets.findModel(record->model->id,record->primitive))
        for(std::size_t slot=0;slot<model->materials.size();++slot) {
            const auto id=slot<record->modelMaterials.size()&&!record->modelMaterials[slot].empty()?record->modelMaterials[slot].id:
                slot<model->materialIds.size()&&!model->materialIds[slot].empty()?model->materialIds[slot]:importedMaterialId(model->id,slot);
            const auto material=assets.findMaterial(id);
            if(material&&material->shading==MaterialShading::BlendPbr)palettes.push_back({std::uint32_t(slot),material});
        }
    if(paletteEntity_!=document.selection().primary) {
        blendControls_.finish(document);paletteEntity_=document.selection().primary;
        paletteSlot_=palettes.empty()?0:palettes.front().slot;
    }
    if(paletteHit_&&paletteHit_->first==paletteEntity_)
        for(const auto& palette:palettes)if(palette.slot==paletteHit_->second&&palette.material->id==paletteMaterial_)paletteSlot_=palette.slot;
    auto current=std::find_if(palettes.begin(),palettes.end(),[&](const auto& palette){return palette.slot==paletteSlot_;});
    if(current==palettes.end())current=palettes.begin();
    const auto palette=current==palettes.end()?std::shared_ptr<const MaterialAsset>{}:current->material;
    if(current!=palettes.end())paletteSlot_=current->slot;
    if(ui.entity!=paletteEntity_||ui.sourceRevision!=assets.catalogRevision()||ui.palette!=(palette?palette->id:std::string{})) {
        ui.entity=paletteEntity_;ui.sourceRevision=assets.catalogRevision();ui.palette=palette?palette->id:std::string{};
        const QSignalBlocker slots(ui.slotSelector),layers(ui.layers);
        ui.slotSelector->clear();
        for(const auto& value:palettes)ui.slotSelector->addItem(QStringLiteral("%1: %2").arg(value.slot+1).arg(QString::fromStdString(value.material->name)),value.slot);
        ui.slotSelector->setCurrentIndex(ui.slotSelector->findData(paletteSlot_));
        ui.layers->clear();
        constexpr std::array names{"Base","R","G","B"};
        constexpr std::array pins{MaterialPin::Base,MaterialPin::Red,MaterialPin::Green,MaterialPin::Blue};
        for(unsigned layer=0;layer<4;++layer) {
            if(palette&&palette->blendDefinition&&!palette->blendDefinition->layers[layer].enabled)continue;
            std::string sourceId;
            if(palette&&palette->blendDefinition) {
                const auto& definition=palette->blendDefinition->layers[layer];
                if(definition.source==BlendMaterialSource::Existing)sourceId=definition.material.id;
            } else if(palette)if(const auto* link=materialInputLink(palette->graph,materialOutputNode(palette->graph).id,pins[layer]))
                if(const auto* node=findMaterialNode(palette->graph,link->fromNode);node&&node->kind==MaterialNodeKind::MaterialReference)sourceId=node->material.id;
            const auto source=sourceId.empty()?std::shared_ptr<const MaterialAsset>{}:assets.findMaterial(sourceId);
            const auto name=source?source->name:sourceId.empty()?"Inline":assets.displayName(sourceId);
            auto item=new QListWidgetItem(QStringLiteral("%1 — %2").arg(QString::fromUtf8(names[layer]),QString::fromStdString(name)),ui.layers);
            item->setData(Qt::UserRole,layer);item->setData(Qt::UserRole+1,QString::fromStdString(sourceId));
            if(layer==brush_.layer)ui.layers->setCurrentItem(item);
        }
    }
    for(int row=0;row<ui.layers->count();++row) {
        auto item=ui.layers->item(row);
        const auto id=item->data(Qt::UserRole+1).toString().toStdString();
        if(id.empty())continue;
        previewMaterials_.push_back(id);
        if(thumbnail_)if(const auto image=thumbnail_(id);!image.isNull())item->setIcon(QPixmap::fromImage(image));
    }
    drawBrushSettings();
    ui.diagnostic->setText(QString::fromStdString(diagnostic_));ui.diagnostic->setVisible(!diagnostic_.empty());
    (void)renderer;
}
}
