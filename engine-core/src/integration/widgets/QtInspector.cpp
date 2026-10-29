#include "QtInspector.h"
#include "QtCollisionMaskControl.h"
#include "ui/qt_panel_section_header.hpp"
#include "quader/QuaderBridge.h"
#include "resources/Paths.h"
#include <QFileDialog>
#include <QPointer>
#include "assets/AssetLibrary.h"
#include "scene/SceneComponents.h"
#include "scene/SceneOutlinerRows.h"
#include "tools/ScenePanels.h"
#include "math/Json.h"
#include "editor/DecalAuthoring.h"
#include "scene/CameraAuthoring.h"
#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QInputDialog>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <utility>

namespace crimson {
namespace {
using Json=nlohmann::json;
struct CameraTemplateCatalog {bool loaded{};bool available{true};std::vector<CameraTemplate> values;std::string status;};
class InspectorSection final : public QWidget {
public:
    QToolButton* header;
    QWidget* content;
    QFormLayout* fields;
    InspectorSection(const QString& title,bool open,QWidget* parent):QWidget(parent){auto layout=new QVBoxLayout(this);layout->setContentsMargins(0,0,0,0);header=new quader::QtPanelSectionHeader(this);header->setText(title);header->setChecked(open);header->setArrowType(open?Qt::DownArrow:Qt::RightArrow);layout->addWidget(header);content=new QWidget(this);fields=new QFormLayout(content);fields->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);fields->setRowWrapPolicy(QFormLayout::WrapLongRows);layout->addWidget(content);content->setVisible(open);QObject::connect(header,&QToolButton::toggled,this,[this](bool expanded){header->setArrowType(expanded?Qt::DownArrow:Qt::RightArrow);content->setVisible(expanded);});}
};
std::unordered_map<SceneDocument*,QtInspector*> inspectors;
std::unordered_map<SceneDocument*,MeshPaintSubdivisionQuery> subdivisionQueries;
QString label(const std::string& key) {
    if(key=="ssgi")return "Global Illumination";if(key=="ssao")return "Ambient Occlusion";if(key=="ssr")return "Screen-Space Reflections";if(key=="controller")return "Character Controller";if(key=="hdrScale")return "HDR Scale";if(key=="specularAA")return "Specular AA";
    if(key=="stairSmoothingTime")return "Stair Smoothing (s)";
    if(key=="verticalFov")return "Vertical FOV";
    if(key=="hitImpulse")return "Hit Impulse";
    if(key=="upwardImpulse")return "Upward Impulse";
    QString out;for(auto c:key){if(c=='_'){out+=' ';continue;}if(std::isupper(static_cast<unsigned char>(c))&&!out.isEmpty())out+=' ';out+=QChar(c);}
    if(!out.isEmpty())out[0]=out[0].toUpper();return out;
}
QString fieldLabel(const std::string& key,const std::string& path){
    if(path=="/meshShading")return "Shading";
    if(path=="/environment/environment/glow/radius")return "Spread";
    if(path=="/environment/environment/glow/luminanceCap")return "HDR Luminance Cap";
    if(path=="/environment/environment/glow/strength"||path=="/environment/environment/ssgi/strength")return "Intensity";
    if(path=="/environment/environment/ssgi/maxDistance")return "Reach (m)";
    if(path=="/environment/environment/ssgi/temporalStability")return "Stability";
    if(path=="/environment/environment/ssgi/rays")return "Samples Per Pixel";
    if(path=="/environment/environment/ssgi/steps")return "Ray Steps";
    if(path=="/environment/environment/ssgi/rayBias")return "Ray Bias (m)";
    if(path=="/environment/environment/ssgi/thickness")return "Thickness (m)";
    if(path.starts_with("/modelMaterials/"))return "Slot "+QString::number(std::stoul(key)+1);
    if(path.starts_with("/environment/environment/glow/levels/"))return "Level "+QString::number(std::stoul(key)+1);
    if(path=="/modelAnimation/loopOverride")return "Playback";
    return label(key);
}
std::string escaped(std::string key){std::string out;for(auto c:key)out+=c=='~'?"~0":c=='/'?"~1":std::string(1,c);return out;}
Json snapshotJson(const SceneDocument& document) {
    SceneSnapshot snapshot; snapshot.id=document.assetId();snapshot.name=document.displayName();
    snapshot.records=document.scene().records();snapshot.properties=document.scene().properties();
    return Json::parse(serializeSceneSnapshot(snapshot));
}
Json entityFields(const SceneDocument& document,EntityId id) {
    auto snapshot=snapshotJson(document);
    for(auto& entity:snapshot["entities"])if(std::stoull(entity["id"].get<std::string>(),nullptr,16)==id) {
        auto fields=entity;
        const auto record=document.scene().find(id);
        if(record&&record->model) {
            fields["receiveDecals"]=record->receiveDecals;
            if(!fields.contains("modelMaterials"))fields["modelMaterials"]=Json::array();
            if(!fields["modelAnimation"].contains("loopOverride"))fields["modelAnimation"]["loopOverride"]=nullptr;
            if(!fields.contains("modelRagdoll"))fields["modelRagdoll"]={{"asset",""},{"startSimulated",false}};
        }
        // Optional flicker fields remain available when disabled. The ordinary
        // serializer still writes its sparse representation on actual Save.
        if(record&&(record->point||record->spot||record->directional)) {
            SceneSnapshot full;full.name=document.displayName();full.properties=document.scene().properties();full.records=document.scene().records();
            for(auto& r:full.records)if(r.id==id){if(r.point)r.point->flicker.enabled=true;if(r.spot)r.spot->flicker.enabled=true;if(r.directional)r.directional->flicker.enabled=true;}
            const auto expanded=Json::parse(serializeSceneSnapshot(full));
            for(const auto& e:expanded["entities"])if(e["id"]==entity["id"])for(auto kind:{"point","spot","directional"})if(e.contains(kind)) {
                fields[kind]["flicker"]=e[kind]["flicker"];
                fields[kind]["flicker"]["enabled"]=kind==std::string("point")?record->point->flicker.enabled:kind==std::string("spot")?record->spot->flicker.enabled:record->directional->flicker.enabled;
                fields[kind]["colorMode"]=e[kind].value("colorMode",std::string("Color"));
                fields[kind]["temperatureKelvin"]=e[kind].value("temperatureKelvin",6500.f);
            }
        }
        return fields;
    }
    return Json::object();
}
std::optional<AssetKind> fieldKind(const std::string& path) {
    if(path=="/model"||path=="/collider/source")return AssetKind::Model;
    if(path=="/particle")return AssetKind::Particle;
    if(path.find("modelMaterials/")!=std::string::npos)return AssetKind::Material;
    if(path=="/modelAnimation/asset")return AssetKind::Animation;
    if(path=="/modelRagdoll/asset")return AssetKind::Ragdoll;
    if(path=="/decal/asset")return AssetKind::Decal;
    if(path=="/controller/profile"||path.find("/profiles/")!=std::string::npos)return AssetKind::NavigationAgentProfile;
    if(path.find("/effects/starting/")!=std::string::npos)return AssetKind::GameplayEffect;
    if(path.ends_with("/map"))return AssetKind::Environment;
    return {};
}
}
struct QtInspector::Impl {
    QObject connections;
    QtInspector& owner;
    SceneDocument* document{};AssetLibrary* assets{};Renderer* renderer{};
    QVBoxLayout* layout{};QScrollArea* scroll{};QWidget* body{};QLabel* status{};
    EntityId entity{};std::uint64_t generation{},revision{},transaction{},catalogRevision{};std::shared_ptr<const ModelAsset> metadata;
    std::vector<EntityId> selection;Json fields,defaults;EditorFrame actions;EntityId decalPlacement{};
    std::string* navigationProfile{};std::function<std::string(const std::string&)> navigationStatus;QLabel* navigationLabel{};
    QuaderBridge* quader{};QLabel* quaderLabel{};QuaderProjectCatalog quaderCatalog;std::string inspectedProject,quaderError;
    const CollisionLayerNames* collisionNames{};bool embedded{};std::vector<QPointer<QtInspector>> parts;
    std::shared_ptr<CameraTemplateCatalog> templates{std::make_shared<CameraTemplateCatalog>()};
    bool rebuilding{};std::unordered_map<std::string,bool> expanded;
    explicit Impl(QtInspector& widget):owner(widget) {
        layout=new QVBoxLayout(&owner);layout->setContentsMargins(4,4,4,4);
        scroll=new QScrollArea(&owner);scroll->setWidgetResizable(true);layout->addWidget(scroll);
        status=new QLabel(&owner);status->setWordWrap(true);status->hide();layout->addWidget(status);
    }
    bool valid()const{return document&&document->documentGeneration()==generation&&document->scene().contains(entity);}
    bool owned()const{return valid()&&transaction&&document->transactionId()==transaction;}
    bool finish(){if(!owned()){transaction=0;return false;}const auto result=document->commit();transaction=0;return result;}
    void cancel(){if(owned())document->cancel();transaction=0;revision=0;}
    void error(const std::string& text){status->setText(QString::fromStdString(text));status->setVisible(!text.empty());}
    bool mutate(const std::string& path,Json value,bool commit=true) {
        if(rebuilding||!valid())return false;
        if(document->transactionActive()&&!owned())return false;
        try {
            auto candidate=snapshotJson(*document);const auto id=entity;
            auto authored=entityFields(*document,id);
            authored[Json::json_pointer(path)]=std::move(value);
            if(path=="/collider/mode")if(const auto current=document->scene().find(id);current&&current->collider){const auto filter=collisionFilterForMode(current->collider->collision,authored["collider"]["mode"]=="solid"?ColliderMode::Solid:ColliderMode::Trigger);authored["collider"]["collision"]={{"layers",filter.layers},{"mask",filter.mask}};if(authored["collider"]["mode"]=="trigger"&&authored["collider"]["shape"]=="trimesh")authored["collider"]["shape"]="box";}
            if(path=="/environment/environment/ssgi/rays"||path=="/environment/environment/ssgi/steps")if(const auto current=document->scene().find(id);current&&current->environment){const auto budget=ssgiQualityBudget(current->environment->environment.ssgi);auto& gi=authored["environment"]["environment"]["ssgi"];gi["quality"]=int(SsgiQuality::Custom);if(path.ends_with("/rays"))gi["steps"]=budget.steps;else gi["rays"]=budget.rays;}
            if(path=="/collider/shape"&&authored["collider"]["shape"]=="capsule")authored["collider"]["height"]=std::max(authored["collider"]["height"].get<float>(),2*authored["collider"]["radius"].get<float>());

            for(auto& e:candidate["entities"])if(e["id"]==authored["id"]){e=authored;break;}
            if(authored.contains("modelAnimation")&&authored["modelAnimation"].contains("loopOverride")&&authored["modelAnimation"]["loopOverride"].is_null()){authored["modelAnimation"].erase("loopOverride");for(auto& e:candidate["entities"])if(e["id"]==authored["id"]){e=authored;break;}}
            auto parsed=parseSceneSnapshot(candidate.dump(),document->projectRoot(),document->assetId());
            auto record=std::find_if(parsed.records.begin(),parsed.records.end(),[&](const auto& r){return r.id==id;});
            if(record==parsed.records.end())return false;
            if(!owned()){if(!document->begin("Edit "+path.substr(path.find_last_of('/')+1)))return false;transaction=document->transactionId();}
            const auto replacement=*record;
            if(!document->update([&](auto& records,auto&){for(auto& r:records)if(r.id==id){r=replacement;break;}})){error(document->status());cancel();return false;}
            fields=std::move(authored);error({});if(commit)finish();return true;
        }catch(const std::exception& exception){error(exception.what());return false;}
    }
    void openAsset(const std::string& id,AssetKind kind) {
        if(kind==AssetKind::Material)actions.openMaterial=id;else if(kind==AssetKind::Particle)actions.openParticle=id;
        else if(kind==AssetKind::Animation)actions.openAnimation=id;else if(kind==AssetKind::Ragdoll)actions.openRagdoll=id;
        else if(kind==AssetKind::Decal)actions.openDecal=id;else if(kind==AssetKind::NavigationAgentProfile)actions.openNavigationAgentProfile=id;
        else if(kind==AssetKind::GameplayEffect)actions.openGameplayEffect=id;
    }
    QWidget* row(const std::string& key,const std::string& path,const Json& value,QWidget* parent) {
        auto wrapper=new QWidget(parent);wrapper->setObjectName(QString::fromStdString("SceneField"+path));auto line=new QHBoxLayout(wrapper);line->setContentsMargins(0,0,0,0);line->setSpacing(3);
        const auto reset=[&,path,key,wrapper,line](QWidget* field) {
            line->addWidget(field,1);auto button=new QToolButton(wrapper);button->setText(QString::fromUtf8("↺"));button->setToolTip("Reset "+label(key));line->addWidget(button);
            QObject::connect(button,&QToolButton::clicked,&connections,[this,path]{if(defaults.contains(Json::json_pointer(path)))mutate(path,defaults.at(Json::json_pointer(path)));else if(fields.contains(Json::json_pointer(path))){const auto& v=fields.at(Json::json_pointer(path));mutate(path,v.is_boolean()?Json(false):v.is_number()?Json(0):v.is_array()?Json::array():Json("") );}revision=0;});
            field->installEventFilter(&owner);return wrapper;
        };
        if(value.is_number_unsigned()&&collisionNames&&(path=="/controller/collision/layers"||path=="/controller/collision/mask"||path=="/collider/collision/layers"||path=="/collider/collision/mask"||path=="/playerInput/traceTest/collisionMask")) {
            const auto record=document->scene().find(entity);const bool solid=record&&record->collider&&record->collider->mode==ColliderMode::Solid;
            const auto bits=path=="/collider/collision/layers"?~WorldCollisionLayer:path=="/collider/collision/mask"&&solid?0u:AllCollisionLayers;
            auto control=new QtCollisionMaskControl(wrapper);control->synchronize(value.get<std::uint32_t>(),*collisionNames,bits);control->changed=[this,path](std::uint32_t bits){mutate(path,bits);};return reset(control);
        }
        if(value.is_number_integer()&&(path.find("/roles/")!=std::string::npos||path.find("/lightingScopes/")!=std::string::npos)) {
            auto controls=new QWidget(wrapper);auto columns=new QHBoxLayout(controls);columns->setContentsMargins(0,0,0,0);
            for(const auto& scope:std::initializer_list<std::pair<const char*,std::uint32_t>>{{"World",lighting_scope::World},{"Character",lighting_scope::Character}}){auto box=new QCheckBox(scope.first,controls);box->setChecked((value.get<std::uint32_t>()&scope.second)!=0);columns->addWidget(box);QObject::connect(box,&QCheckBox::toggled,&connections,[this,path,bit=scope.second](bool checked){const auto current=fields.at(Json::json_pointer(path)).get<std::uint32_t>();mutate(path,checked?current|bit:current&~bit);});}return reset(controls);
        }
        if(path=="/modelAnimation/loopOverride") {
            auto combo=new QComboBox(wrapper);combo->addItem("Use Animation",-1);combo->addItem("Loop",1);combo->addItem("Once",0);combo->setCurrentIndex(combo->findData(value.is_null()?-1:value.get<bool>()?1:0));QObject::connect(combo,&QComboBox::activated,&connections,[this,path,combo]{const auto choice=combo->currentData().toInt();mutate(path,choice<0?Json{}:Json(choice!=0));});line->addWidget(combo);return wrapper;
        }
        if(value.is_number_integer()) {
            QStringList choices;
            if(path=="/meshShading")choices={"Use Mesh Normals","Flat","Smooth"};
            else if(path=="/environment/environment/toneMapping")choices={"Reinhard (legacy)","Linear","ACES","AgX"};
            else if(path=="/environment/environment/source")choices={"HDRI / IBL","Procedural Sky"};
            else if(path=="/environment/environment/ambientLight/source")choices={"Disabled","Color","Sky"};
            else if(path=="/environment/environment/fog/mode")choices={"Exponential","Depth"};
            else if(path=="/environment/environment/ssgi/quality")choices={"Low","Medium","High","Custom"};
            else if(path=="/environment/environment/ssao/quality"||path=="/environment/environment/ssr/quality")choices={"Low","Medium","High"};
            else if(path=="/environment/environment/glow/quality")choices={"Low","High"};
            if(!choices.empty()){auto combo=new QComboBox(wrapper);for(int index=0;index<choices.size();++index)combo->addItem(choices[index],index);combo->setCurrentIndex(combo->findData(value.get<int>()));QObject::connect(combo,&QComboBox::activated,&connections,[this,path,combo]{mutate(path,combo->currentData().toInt());revision=0;});return reset(combo);}
        }
        if(value.is_boolean()) {
            auto control=new QCheckBox(wrapper);control->setChecked(value.get<bool>());
            QObject::connect(control,&QCheckBox::toggled,&connections,[this,path](bool checked){mutate(path,checked);});return reset(control);
        }
        if(const auto kind=fieldKind(path);kind&&value.is_string()) {
            auto picker=new QComboBox(wrapper);picker->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);picker->setMinimumContentsLength(10);
            picker->addItem("None",QString{});const auto id=value.get<std::string>();
            for(const auto& asset:assets->catalog())if(asset.kind==*kind)picker->addItem(QString::fromStdString(asset.name),QString::fromStdString(asset.id));
            auto index=picker->findData(QString::fromStdString(id));if(index<0){picker->addItem(QString::fromStdString(assets->displayName(id)),QString::fromStdString(id));index=picker->count()-1;}picker->setCurrentIndex(index);
            QObject::connect(picker,&QComboBox::activated,&connections,[this,path,picker](int){mutate(path,picker->currentData().toString().toStdString());});
            line->addWidget(picker,1);auto open=new QToolButton(wrapper);open->setText("…");open->setToolTip("Open asset");line->addWidget(open);QObject::connect(open,&QToolButton::clicked,&connections,[this,picker,kind]{openAsset(picker->currentData().toString().toStdString(),*kind);});return wrapper;
        }
        if(value.is_string()&&(path=="/cameraFollow/target"||path=="/environment/sun")) {
            auto picker=new QComboBox(wrapper);picker->addItem("None",QString(16,'0'));
            for(const auto& record:document->scene().records())if(record.id!=entity&&(path!="/environment/sun"||record.directional))picker->addItem(QString::fromStdString(record.name),QString("%1").arg(qulonglong(record.id),16,16,QChar('0')));
            picker->setCurrentIndex(picker->findData(QString::fromStdString(value.get<std::string>())));
            QObject::connect(picker,&QComboBox::activated,&connections,[this,path,picker]{mutate(path,picker->currentData().toString().toStdString());});return reset(picker);
        }
        if(value.is_number()) {
            auto control=new QDoubleSpinBox(wrapper);control->setDecimals(value.is_number_integer()?0:4);control->setRange(value.is_number_unsigned()?0:-1e12,1e12);control->setSingleStep(value.is_number_integer()?1:.05);control->setKeyboardTracking(false);double shown=value.get<double>();if(path=="/environment/environment/ssgi/rays"||path=="/environment/environment/ssgi/steps")if(const auto current=document->scene().find(entity);current&&current->environment){const auto budget=ssgiQualityBudget(current->environment->environment.ssgi);shown=path.ends_with("/rays")?budget.rays:budget.steps;}control->setValue(shown);
            QObject::connect(control,&QDoubleSpinBox::valueChanged,&connections,[this,path,control,value](double v){mutate(path,value.is_number_unsigned()?Json(static_cast<std::uint64_t>(v)):value.is_number_integer()?Json(static_cast<std::int64_t>(v)):Json(v),false);});
            QObject::connect(control,&QDoubleSpinBox::editingFinished,&connections,[this]{finish();});return reset(control);
        }
        if(value.is_string()) {
            QStringList choices;
            if(path=="/environment/environment/glow/blendMode")choices={"Additive","Screen","SoftLight","Replace","Mix"};
            else if(path=="/environment/environment/dither/pattern")choices={"Bayer2x2","Bayer4x4","Bayer8x8","InterleavedGradientNoise"};
            else if(key=="projection")choices={"surface","box"};
            else if(key=="shape")choices={"box","sphere","cylinder","capsule","trimesh"};
            else if(key=="mode"&&path.starts_with("/collider"))choices={"solid","trigger"};
            else if(key=="behaviour")choices={"idle","chasePlayer","randomMove"};
            else if(key=="colorMode")choices={"Color","Temperature"};
            else if(key=="playback")choices={"Loop","Once"};
            else if(key=="behavior")for(unsigned i=0;i<static_cast<unsigned>(LightFlickerBehavior::Count);++i)choices<<QString::fromStdString(std::string(lightFlickerBehaviorId(static_cast<LightFlickerBehavior>(i))));
            if(!choices.empty()) {auto combo=new QComboBox(wrapper);for(const auto& choice:choices){auto display=label(choice.toStdString());if(choice=="SoftLight")display="Soft Light";else if(choice=="Bayer2x2")display="Bayer 2x2";else if(choice=="Bayer4x4")display="Bayer 4x4";else if(choice=="Bayer8x8")display="Bayer 8x8";else if(choice=="InterleavedGradientNoise")display="Interleaved Gradient Noise";else if(choice=="chasePlayer")display="Chase Player";else if(choice=="randomMove")display="Random Move";combo->addItem(display,choice);}combo->setCurrentIndex(combo->findData(QString::fromStdString(value.get<std::string>())));QObject::connect(combo,&QComboBox::activated,&connections,[this,path,combo]{mutate(path,combo->currentData().toString().toStdString());revision=0;});return reset(combo);}
            auto edit=new QLineEdit(QString::fromStdString(value.get<std::string>()),wrapper);
            QObject::connect(edit,&QLineEdit::editingFinished,&connections,[this,path,edit]{mutate(path,edit->text().toStdString());});return reset(edit);
        }
        if(value.is_array()&&value.size()>=2&&value.size()<=4&&std::all_of(value.begin(),value.end(),[](const auto& v){return v.is_number();})) {
            const bool color=key.find("olor")!=std::string::npos||key.find("Tint")!=std::string::npos||key=="tint";
            for(std::size_t axis=0;axis<value.size();++axis) {auto spin=new QDoubleSpinBox(wrapper);spin->setMinimumWidth(0);spin->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);spin->setDecimals(4);spin->setRange(-1e9,1e9);spin->setKeyboardTracking(false);spin->setSingleStep(.05);spin->setValue(value[axis].get<double>());line->addWidget(spin);spin->installEventFilter(&owner);
                QObject::connect(spin,&QDoubleSpinBox::valueChanged,&connections,[this,path,axis](double v){auto values=fields.at(Json::json_pointer(path));values[axis]=v;mutate(path,std::move(values),false);});QObject::connect(spin,&QDoubleSpinBox::editingFinished,&connections,[this]{finish();});}
            if(color){auto button=new QToolButton(wrapper);button->setText("…");button->setToolTip("Choose color");line->addWidget(button);QObject::connect(button,&QToolButton::clicked,&connections,[this,path]{auto values=fields.at(Json::json_pointer(path));const QColor initial=QColor::fromRgbF(std::clamp(values[0].get<double>(),0.,1.),std::clamp(values[1].get<double>(),0.,1.),std::clamp(values[2].get<double>(),0.,1.),values.size()>3?std::clamp(values[3].get<double>(),0.,1.):1.);QColorDialog dialog(initial,&owner);dialog.setOption(QColorDialog::ShowAlphaChannel,values.size()>3);QObject::connect(&dialog,&QColorDialog::currentColorChanged,&connections,[this,path,values](const QColor& c)mutable{values[0]=c.redF();values[1]=c.greenF();values[2]=c.blueF();if(values.size()>3)values[3]=c.alphaF();mutate(path,values,false);});if(dialog.exec()==QDialog::Accepted)finish();else cancel();revision=0;});}
            return wrapper;
        }
        line->addWidget(new QLabel(QString::fromStdString(value.dump()),wrapper));return wrapper;
    }
    void form(QFormLayout* layout,const Json& object,const std::string& prefix,QWidget* parent,unsigned depth=0) {
        if(depth>10)return;
        for(auto it=object.begin();it!=object.end();++it) {
            const auto key=object.is_array()?std::to_string(it-object.begin()):it.key();const auto path=prefix+"/"+escaped(key);const auto& value=it.value();
            if(key=="id"||key=="parent"||key=="order"||key=="local"||key=="mesh"||key=="meshPaint"||key=="quaderGenerated"||(key=="seed"&&prefix=="/decal")||key=="saveKey"||key=="blendSettings"||(key=="source"&&prefix.ends_with("/glow")))continue;
            const bool vector=value.is_array()&&value.size()>=2&&value.size()<=4&&std::all_of(value.begin(),value.end(),[](const auto& v){return v.is_number();});
            if(value.is_object()||(value.is_array()&&!vector)) {
                auto group=new InspectorSection(label(key),expanded.contains(path)?expanded[path]:depth<1,parent);auto nested=group->fields;form(nested,value,path,group->content,depth+1);
                QObject::connect(group->header,&QToolButton::toggled,&connections,[this,path](bool open){expanded[path]=open;});
                if(value.is_array()) {auto add=new QPushButton("Add",group);nested->addRow(add);QObject::connect(add,&QPushButton::clicked,&connections,[this,path,value]{auto next=value;next.push_back(value.empty()?Json(""):value.back());mutate(path,std::move(next));revision=0;});for(std::size_t i=0;i<value.size();++i){auto remove=new QPushButton("Remove "+QString::number(i+1),group);nested->addRow(remove);QObject::connect(remove,&QPushButton::clicked,&connections,[this,path,i]{auto next=fields.at(Json::json_pointer(path));next.erase(next.begin()+i);mutate(path,std::move(next));revision=0;});}}
                layout->addRow(group);
            }else if(vector){layout->addRow(new QLabel(fieldLabel(key,path),parent));layout->addRow(row(key,path,value,parent));}else layout->addRow(fieldLabel(key,path),row(key,path,value,parent));
        }
    }
    void transform(QFormLayout* layout,const EntityRecord& record) {
        const auto affine=math::affineTransformFields(record.local);if(!affine)return;
        const bool surface=record.decal&&record.decal->projection==SceneDecalProjection::Surface;
        const bool box=record.decal&&record.decal->projection==SceneDecalProjection::Box;
        const auto frame=surface?decalEditFrame(document->scene(),record.id):box?document->scene().worldTransform(record.id):std::optional<Mat4>{};
        auto group=new QGroupBox("Transform",body);auto form=new QFormLayout(group);
        const auto apply=[this,surface,box](unsigned field,unsigned axis,double v,bool commit){
            if(!valid())return;const auto r=document->scene().find(entity);if(!r)return;auto f=math::affineTransformFields(r->local);if(!f)return;
            std::optional<Mat4> result;
            if(field==0){auto position=f->position;position[axis]=float(v);result=math::withAffinePosition(r->local,position);}
            else if(surface){const auto current=decalEditFrame(document->scene(),entity);const auto parent=r->parent?document->scene().worldTransform(r->parent):std::optional{Mat4{}};const auto inverse=parent?math::inverse(*parent):std::nullopt;if(!current||!inverse)return;auto size=decals::decalSize(*current);if(field==2)size[axis]=float(v);const auto edited=field==1?decals::withDecalRotation(*current,float(v)):decals::withDecalSize(*current,size);if(edited)result=*inverse * *edited;}
            else if(box&&field==2){const auto current=document->scene().worldTransform(entity);const auto parent=r->parent?document->scene().worldTransform(r->parent):std::optional{Mat4{}};const auto inverse=parent?math::inverse(*parent):std::nullopt;const auto captured=current?math::affineTransformFields(*current):std::nullopt;if(!captured||!inverse)return;auto size=captured->scale;size[axis]=float(v);if(const auto edited=math::withAffineScale(*current,*captured,size))result=*inverse * *edited;}
            else if(field==1){auto rotation=f->rotationRadians;rotation[axis]=float(v*std::numbers::pi/180);result=math::withAffineRotation(r->local,*f,rotation);}
            else {auto scale=f->scale;scale[axis]=float(v);result=math::withAffineScale(r->local,*f,scale);}
            if(result)mutate("/local",Json(*result),commit);
        };
        for(unsigned field=0;field<3;++field) {
            auto widget=new QWidget(group);auto row=new QHBoxLayout(widget);row->setContentsMargins(0,0,0,0);
            const unsigned count=surface&&field>0?(field==1?1:2):3;
            for(unsigned column=0;column<count;++column){const unsigned axis=surface&&field==1?2:column;auto spin=new QDoubleSpinBox(widget);spin->setMinimumWidth(0);spin->setSizePolicy(QSizePolicy::Ignored,QSizePolicy::Fixed);spin->setDecimals(4);spin->setRange(-1e9,1e9);spin->setKeyboardTracking(false);spin->setSingleStep(field==1?.5:.05);
                if(!(surface&&field==1))spin->setPrefix(surface&&field==2?(axis==0?"W ":"H "):QString("%1 ").arg("XYZ"[axis]));
                double value=field==0?affine->position[axis]:field==1?affine->rotationRadians[axis]*180/std::numbers::pi:affine->scale[axis];
                if(surface&&frame&&field==1)value=decals::decalRotationDegrees(*frame);if(surface&&frame&&field==2)value=decals::decalSize(*frame)[axis];
                if(box&&frame&&field==2)value=math::length(math::transformDirection(*frame,axis==0?Vec3{1,0,0}:axis==1?Vec3{0,1,0}:Vec3{0,0,1}));
                spin->setValue(value);spin->setEnabled(!surface||field==0||bool(frame));row->addWidget(spin);spin->installEventFilter(&owner);
                QObject::connect(spin,&QDoubleSpinBox::valueChanged,&connections,[apply,field,axis](double v){apply(field,axis,v,false);});QObject::connect(spin,&QDoubleSpinBox::editingFinished,&connections,[this]{finish();});
            }
            auto reset=new QToolButton(widget);reset->setText(QString::fromUtf8("↺"));reset->setToolTip("Reset");row->addWidget(reset);QObject::connect(reset,&QToolButton::clicked,&connections,[this,apply,field,count,surface]{for(unsigned column=0;column<count;++column)apply(field,surface&&field==1?2:column,field==2?1:0,false);finish();revision=0;});form->addRow(new QLabel(field==0?"Position":field==1?"Rotation (deg)":surface||box?"Size (m)":"Scale",group));form->addRow(widget);
        }layout->addRow(group);
    }
    void cameraTemplatesSection(QFormLayout* layout,const EntityRecord& record) {
        if(!templates->loaded){templates->loaded=true;try{templates->values=loadCameraTemplates(document->projectRoot());}catch(const std::exception& exception){templates->status=exception.what();templates->available=false;}}
        auto section=new InspectorSection("Camera Templates",true,body);auto combo=new QComboBox(section->content);combo->addItem("Custom",-2);const auto builtin=defaultPlayerCameraTemplate();combo->addItem(QString::fromStdString(builtin.name),-1);for(std::size_t i=0;i<templates->values.size();++i)combo->addItem(QString::fromStdString(templates->values[i].name),int(i));
        const auto current=captureCameraTemplate(document->scene(),entity,record.cameraFollow?record.cameraFollow->target:NoEntity);const auto matches=[&](const CameraTemplate& value){return current&&value.offset==current->offset&&value.worldBasis==current->worldBasis&&value.lens==current->lens;};int selected=matches(builtin)?1:0;for(std::size_t i=0;i<templates->values.size();++i)if(matches(templates->values[i]))selected=int(i)+2;combo->setCurrentIndex(selected);section->fields->addRow("Template",combo);
        QObject::connect(combo,&QComboBox::activated,&connections,[this,combo,builtin]{if(!valid())return;const auto index=combo->currentData().toInt();if(index==-2)return;finish();const auto value=index==-1?builtin:templates->values.at(std::size_t(index));if(!updateCameraAuthoring(*document,entity,value,"Apply camera template"))error(document->status());revision=0;});
        auto save=new QPushButton("Save Template",section->content);save->setEnabled(templates->available);section->fields->addRow(save);QObject::connect(save,&QPushButton::clicked,&connections,[this]{if(!valid()||!templates->available)return;finish();bool ok=false;const auto name=QInputDialog::getText(&owner,"Save Camera Template","Name",QLineEdit::Normal,{},&ok);if(!ok)return;const auto record=document->scene().find(entity);if(!record)return;const auto value=captureCameraTemplate(document->scene(),entity,record->cameraFollow?record->cameraFollow->target:NoEntity,name.toStdString());if(!value){error("The camera cannot be captured as a template.");return;}try{saveCameraTemplate(document->projectRoot(),templates->values,*value);templates->status.clear();revision=0;}catch(const std::exception& exception){error(exception.what());}});
        if(!templates->status.empty()){auto status=new QLabel(QString::fromStdString(templates->status),section->content);status->setWordWrap(true);section->fields->addRow(status);}layout->addRow(section);
    }
    void quaderSection(QFormLayout* layout,const EntityRecord& record) {
        auto section=new InspectorSection("Quader",true,body);auto form=section->fields;auto draft=std::make_shared<QuaderLink>(*record.quader);
        auto path=new QLineEdit(QString::fromStdString(draft->projectPath),section->content);auto row=new QWidget(section->content);row->setObjectName("SceneField/quader/projectPath");auto pathLayout=new QHBoxLayout(row);pathLayout->setContentsMargins(0,0,0,0);pathLayout->addWidget(path,1);auto browse=new QPushButton("Browse",row);pathLayout->addWidget(browse);auto clear=new QPushButton("Clear",row);pathLayout->addWidget(clear);form->addRow("Project",row);
        auto scenes=new QComboBox(section->content);form->addRow("Scene",scenes);auto scale=new QDoubleSpinBox(section->content);scale->setRange(.000001,1000000);scale->setDecimals(4);scale->setKeyboardTracking(false);scale->setValue(draft->importScale);form->addRow("Import Scale",scale);
        auto request=new QPushButton("Import",section->content);auto cancel=new QPushButton("Cancel",section->content);auto buttons=new QWidget(section->content);auto buttonLayout=new QHBoxLayout(buttons);buttonLayout->setContentsMargins(0,0,0,0);buttonLayout->addWidget(request);buttonLayout->addWidget(cancel);form->addRow(buttons);quaderLabel=new QLabel(section->content);quaderLabel->setWordWrap(true);form->addRow(quaderLabel);
        auto inspect=std::make_shared<std::function<void()>>();*inspect=[this,draft,path,scenes,request]{draft->projectPath=path->text().toStdString();if(inspectedProject!=draft->projectPath){inspectedProject=draft->projectPath;quaderCatalog={};quaderError.clear();if(!inspectedProject.empty())try{quaderCatalog=inspectQuaderProject(pathFromUtf8(inspectedProject));}catch(const std::exception& exception){quaderError=exception.what();}}draft->projectId=quaderCatalog.id;const QSignalBlocker blocked(scenes);scenes->clear();for(const auto& entry:quaderCatalog.scenes)scenes->addItem(QString::fromStdString(entry.path),QString::fromStdString(entry.id));auto index=scenes->findData(QString::fromStdString(draft->sceneId));if(index<0)index=scenes->findData(QString::fromStdString(quaderCatalog.defaultScene));if(index<0&&scenes->count())index=0;scenes->setCurrentIndex(index);draft->sceneId=scenes->currentData().toString().toStdString();request->setEnabled(quader&&quaderError.empty()&&!draft->projectPath.empty()&&!draft->sceneId.empty()&&!document->transient());};
        QObject::connect(path,&QLineEdit::editingFinished,&connections,[this,inspect]{inspectedProject.clear();(*inspect)();});QObject::connect(browse,&QPushButton::clicked,&connections,[this,path,inspect]{const auto selected=QFileDialog::getOpenFileName(&owner,"Quader Project",path->text(),"Quader Project (*.qproj)");if(!selected.isEmpty()){path->setText(selected);inspectedProject.clear();(*inspect)();}});QObject::connect(scenes,&QComboBox::activated,&connections,[draft,scenes]{draft->sceneId=scenes->currentData().toString().toStdString();});QObject::connect(scale,&QDoubleSpinBox::valueChanged,&connections,[draft](double value){draft->importScale=float(value);});QObject::connect(scale,&QDoubleSpinBox::editingFinished,&connections,[this,draft]{if(valid()&&quader&&!draft->projectId.empty()&&!document->scene().find(entity)->quader->projectId.empty()){finish();quader->requestImport(entity,*draft,document->documentGeneration());}});
        QObject::connect(request,&QPushButton::clicked,&connections,[this,draft]{if(!valid()||!quader)return;finish();quader->requestImport(entity,*draft,document->documentGeneration());});QObject::connect(cancel,&QPushButton::clicked,&connections,[this]{if(valid()&&quader)quader->cancel(entity);});QObject::connect(clear,&QPushButton::clicked,&connections,[this]{if(!valid())return;finish();auto r=*document->scene().find(entity);r.quader=QuaderLink{};document->updateEntity(entity,r,"Clear Quader source");if(quader)quader->cancel(entity);revision=0;});
        for(const auto& policy:std::initializer_list<std::pair<const char*,const char*>>{{"Auto Sync on Save","autoSync"},{"Sync Materials","refreshMaterials"},{"Sync Lights","syncLights"}}){auto control=new QCheckBox(policy.first,section->content);control->setChecked(fields["quader"][policy.second].get<bool>());form->addRow(control);QObject::connect(control,&QCheckBox::toggled,&connections,[this,draft,key=std::string(policy.second)](bool value){if(mutate("/quader/"+key,value)){if(key=="autoSync")draft->autoSync=value;else if(key=="refreshMaterials")draft->refreshMaterials=value;else draft->syncLights=value;}});}
        (*inspect)();section->setEnabled(!document->transient());layout->addRow(section);
    }
    void rebuild() {
        rebuilding=true;navigationLabel=nullptr;quaderLabel=nullptr;parts.clear();auto old=scroll->takeWidget();if(old){old->setParent(&owner);old->hide();old->deleteLater();}body=new QWidget;auto layout=new QFormLayout(body);layout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);scroll->setWidget(body);
        const auto record=document->scene().find(entity);if(!record){rebuilding=false;return;}
        fields=entityFields(*document,entity);if(record->model)if(const auto model=assets->findModel(record->model->id,record->primitive)){auto& overrides=fields["modelMaterials"];while(overrides.size()<model->materials.size())overrides.push_back("");}defaults=fields;
        // Defaults for leaf reset remain type-correct; neutral component values
        // are copied from their authoritative C++ aggregate defaults below.
        SceneSnapshot reset;reset.name=document->displayName();reset.properties=document->scene().properties();reset.records=document->scene().records();
        for(auto& r:reset.records)if(r.id==entity) {if(r.point){r.point=ScenePointLight{};r.point->flicker.enabled=true;}if(r.spot){r.spot=SceneSpotLight{};r.spot->flicker.enabled=true;}if(r.directional){r.directional=SceneDirectionalLight{};r.directional->flicker.enabled=true;}if(r.environment)r.environment=SceneEnvironment{};if(r.movement)r.movement=MovementComponent{};if(r.playerInput)r.playerInput=ScenePlayerInput{};if(r.collider){auto native=r.collider->mesh;auto source=r.collider->source;r.collider=SceneCollider{};r.collider->mesh=native;r.collider->source=source;}if(r.camera)r.camera=SceneCamera{};if(r.navigation)r.navigation=SceneNavigationAgent{};if(r.enemy)r.enemy=SceneEnemy{};}
        try{const auto encoded=Json::parse(serializeSceneSnapshot(reset));for(const auto& e:encoded["entities"])if(e["id"]==fields["id"]){defaults=e;for(auto kind:{"point","spot","directional"})if(defaults.contains(kind)){defaults[kind]["flicker"]["enabled"]=false;defaults[kind]["colorMode"]="Color";defaults[kind]["temperatureKelvin"]=6500;}break;}}catch(...){ }
        layout->addRow("Name",row("name","/name",fields["name"],body));
        transform(layout,*record);
        auto remaining=fields;remaining.erase("name");
        for(const auto key:{"movement","actor","controller","navigation","enemy","playerInput","camera","cameraFollow","model","modelMaterials","primitive","modelAnimation","modelRagdoll","meshShading","castShadows","receiveShadows","lightingScopes","receiveDecals","particle","decal","directional","point","spot","environment","collider","navigationVolume","tags","effects","visible","outlinerStyle"})if(remaining.contains(key)){Json section=Json::object();section[key]=remaining[key];form(layout,section,"",body);remaining.erase(key);}
        if(record->quader){quaderSection(layout,*record);remaining.erase("quader");}
        form(layout,remaining,"",body);
        auto add=new QPushButton("Add Component",body);layout->addRow(add);
        QObject::connect(add,&QPushButton::clicked,&connections,[this,add]{if(!valid())return;finish();QMenu menu(add);for(auto kind:AllSceneComponentKinds)if(editableSceneComponent(kind)&&!hasSceneComponent(*document->scene().find(entity),kind)){auto action=menu.addAction(sceneComponentName(kind));QObject::connect(action,&QAction::triggered,&connections,[this,kind]{addSceneComponent(*document,entity,kind);revision=0;});}menu.exec(add->mapToGlobal(QPoint(0,add->height())));});
        auto remove=new QPushButton("Remove Component",body);layout->addRow(remove);QObject::connect(remove,&QPushButton::clicked,&connections,[this,remove]{if(!valid())return;finish();QMenu menu(remove);for(auto kind:sceneComponents(*document->scene().find(entity)))if(editableSceneComponent(kind)){auto action=menu.addAction(sceneComponentName(kind));QObject::connect(action,&QAction::triggered,&connections,[this,kind]{removeSceneComponent(*document,entity,kind);revision=0;});}menu.exec(remove->mapToGlobal(QPoint(0,remove->height())));});
        if(record->point||record->spot||record->directional) {
            auto fixtures=new QComboBox(body);fixtures->addItem("Fixture preset");for(const auto& preset:lightFixturePresets())if(preset.directional==bool(record->directional))fixtures->addItem(QString::fromUtf8(preset.name.data(),preset.name.size()),QString::fromUtf8(preset.id.data(),preset.id.size()));layout->addRow(fixtures);
            QObject::connect(fixtures,&QComboBox::activated,&connections,[this,fixtures](int index){if(!index||!valid())return;finish();auto r=*document->scene().find(entity);const auto id=fixtures->currentData().toString().toStdString();if(r.point)applyLightFixturePreset(*r.point,id);if(r.spot)applyLightFixturePreset(*r.spot,id);if(r.directional)applyLightFixturePreset(*r.directional,id);document->updateEntity(entity,r,"Apply light preset");revision=0;});
            auto flicker=new QComboBox(body);flicker->addItem("Flicker preset");for(const auto& preset:lightFlickerPresets())flicker->addItem(QString::fromUtf8(preset.name.data(),preset.name.size()),QString::fromUtf8(preset.id.data(),preset.id.size()));layout->addRow(flicker);QObject::connect(flicker,&QComboBox::activated,&connections,[this,flicker](int index){if(!index||!valid())return;finish();auto r=*document->scene().find(entity);const auto id=flicker->currentData().toString().toStdString();if(r.point)applyLightFlickerPreset(r.point->flicker,id);if(r.spot)applyLightFlickerPreset(r.spot->flicker,id);if(r.directional)applyLightFlickerPreset(r.directional->flicker,id);document->updateEntity(entity,r,"Apply flicker preset");revision=0;});
        }
        if(record->model) {
            auto subdivision=new QComboBox(body);for(unsigned i=0;i<=5;++i)subdivision->addItem(QString::number(i),i);subdivision->setCurrentIndex(record->meshPaint?int(record->meshPaint->subdivisionLevel):0);layout->addRow("Subdivision Level",subdivision);QObject::connect(subdivision,&QComboBox::activated,&connections,[this,subdivision]{finish();const auto r=document->scene().find(entity);const auto found=subdivisionQueries.find(document);if(!r||found==subdivisionQueries.end())return;auto changed=*r;SceneMeshPaint paint;std::string diagnostic;if(found->second(changed,subdivision->currentData().toUInt(),paint,diagnostic)){changed.meshPaint=std::move(paint);document->updateEntity(entity,changed,"Change paint subdivision");}else error(diagnostic);revision=0;});
            auto create=new QPushButton(record->modelRagdoll.asset.empty()?"Create Ragdoll":"Open Ragdoll",body);layout->addRow(create);QObject::connect(create,&QPushButton::clicked,&connections,[this]{const auto r=document->scene().find(entity);if(!r)return;if(r->modelRagdoll.asset.empty())actions.createRagdollModel=r->model->id;else actions.openRagdoll=r->modelRagdoll.asset.id;});
            auto test=new QPushButton("Activate Ragdoll",body);layout->addRow(test);QObject::connect(test,&QPushButton::clicked,&connections,[this]{actions.activateRagdollEntity=entity;});auto resetRig=new QPushButton("Reset Ragdoll",body);layout->addRow(resetRig);QObject::connect(resetRig,&QPushButton::clicked,&connections,[this]{actions.resetRagdollEntity=entity;});
        }
        if(record->decal&&record->decal->projection==SceneDecalProjection::Surface){auto place=new QPushButton("Place on Surface",body);layout->addRow(place);QObject::connect(place,&QPushButton::clicked,&connections,[this]{finish();decalPlacement=entity;});}
        if(record->navigationVolume&&navigationProfile){auto preview=new QComboBox(body);preview->addItem("None",QString{});for(const auto& profile:record->navigationVolume->profiles)preview->addItem(QString::fromStdString(assets->displayName(profile.id)),QString::fromStdString(profile.id));preview->setCurrentIndex(std::max(0,preview->findData(QString::fromStdString(*navigationProfile))));layout->addRow("Preview Profile",preview);QObject::connect(preview,&QComboBox::activated,&connections,[this,preview]{*navigationProfile=preview->currentData().toString().toStdString();});navigationLabel=new QLabel(body);navigationLabel->setWordWrap(true);layout->addRow(navigationLabel);}
        if(record->particle){auto button=new QPushButton("Replay",body);layout->addRow(button);QObject::connect(button,&QPushButton::clicked,&connections,[this]{actions.playParticleEntity=entity;});}
        if(record->camera){cameraTemplatesSection(layout,*record);auto active=new QPushButton("Set Active Camera",body);layout->addRow(active);QObject::connect(active,&QPushButton::clicked,&connections,[this]{document->setActiveCamera(entity);});}
        if(record->environment){auto active=new QPushButton("Set Active Environment",body);layout->addRow(active);QObject::connect(active,&QPushButton::clicked,&connections,[this]{document->setActiveEnvironment(entity);});}
        if(!embedded)for(const auto part:sceneEntityParts(document->scene(),entity))if(part!=entity){const auto owned=document->scene().find(part);if(!owned)continue;auto section=new InspectorSection(QString::fromStdString(owned->name),true,body);auto projection=new QtInspector(section->content);parts.push_back(projection);projection->impl_->templates=templates;projection->setNavigationPreview(navigationProfile,navigationStatus);projection->setQuaderBridge(quader);if(collisionNames)projection->setCollisionLayerNames(*collisionNames);projection->synchronize(*document,*assets,*renderer,part);projection->setMinimumHeight(280);section->fields->addRow(projection);layout->addRow(section);}
        rebuilding=false;
    }
};
QtInspector::QtInspector(QWidget* parent):QWidget(parent),impl_(std::make_unique<Impl>(*this)){}
QtInspector::~QtInspector(){finish();if(impl_->document&&!impl_->embedded){const auto entry=inspectors.find(impl_->document);if(entry!=inspectors.end()&&entry->second==this){inspectors.erase(entry);subdivisionQueries.erase(impl_->document);}}delete impl_->scroll;impl_->scroll=nullptr;}
void QtInspector::synchronize(SceneDocument& document,AssetLibrary& assets,Renderer& renderer,EntityId target) {
    auto& p=*impl_;const bool embedded=target!=NoEntity;
    if(p.document!=&document||p.embedded!=embedded){finish();if(p.document&&!p.embedded){const auto previous=inspectors.find(p.document);if(previous!=inspectors.end()&&previous->second==this)inspectors.erase(previous);}p.document=&document;p.embedded=embedded;if(!embedded)inspectors[&document]=this;}
    p.assets=&assets;p.renderer=&renderer;if(p.quaderLabel){const auto text=QString::fromStdString(p.quaderError.empty()&&p.quader?p.quader->status(p.entity):p.quaderError);if(text!=p.quaderLabel->text())p.quaderLabel->setText(text);p.quaderLabel->setVisible(!text.isEmpty());}if(p.navigationLabel&&p.navigationProfile&&p.navigationStatus){const auto text=QString::fromStdString(p.navigationStatus(*p.navigationProfile));if(text!=p.navigationLabel->text())p.navigationLabel->setText(text);p.navigationLabel->setVisible(!text.isEmpty());}
    const auto selected=embedded?std::vector<EntityId>{target}:document.selection().ids;
    const bool changed=p.generation!=document.documentGeneration()||p.selection!=selected;
    if(changed){finish();p.generation=document.documentGeneration();p.selection=selected;p.entity=embedded?target:document.selection().primary;p.revision=0;}
    const auto record=document.scene().find(p.entity);const auto metadata=record&&record->model?assets.findModel(record->model->id,record->primitive):std::shared_ptr<const ModelAsset>{};const bool resourcesChanged=p.catalogRevision!=assets.catalogRevision()||p.metadata!=metadata;
    const auto focus=QApplication::focusWidget();const bool editing=focus&&isAncestorOf(focus)&&(qobject_cast<QLineEdit*>(focus)||qobject_cast<QAbstractSpinBox*>(focus));
    if(changed||(!editing&&!QApplication::activePopupWidget()&&!p.owned()&&(p.revision!=document.scene().revision()||resourcesChanged))){p.revision=document.scene().revision();p.catalogRevision=assets.catalogRevision();p.metadata=metadata;p.rebuild();}
    for(const auto& part:p.parts)if(part)part->synchronize(document,assets,renderer,part->impl_->entity);
}
void QtInspector::setQuaderBridge(QuaderBridge* bridge){impl_->quader=bridge;}
void QtInspector::setCollisionLayerNames(const CollisionLayerNames& names){impl_->collisionNames=&names;}
void QtInspector::setNavigationPreview(std::string* profile,std::function<std::string(const std::string&)> query){impl_->navigationProfile=profile;impl_->navigationStatus=std::move(query);}
EditorFrame QtInspector::takeActions(){auto result=std::exchange(impl_->actions,{});for(const auto& part:impl_->parts)if(part){auto value=part->takeActions();if(!value.openMaterial.empty())result.openMaterial=value.openMaterial;if(!value.openParticle.empty())result.openParticle=value.openParticle;if(!value.openAnimation.empty())result.openAnimation=value.openAnimation;if(!value.openRagdoll.empty())result.openRagdoll=value.openRagdoll;if(!value.createRagdollModel.empty())result.createRagdollModel=value.createRagdollModel;if(!value.openDecal.empty())result.openDecal=value.openDecal;if(!value.openNavigationAgentProfile.empty())result.openNavigationAgentProfile=value.openNavigationAgentProfile;if(!value.openGameplayEffect.empty())result.openGameplayEffect=value.openGameplayEffect;if(value.playParticleEntity)result.playParticleEntity=value.playParticleEntity;if(value.activateRagdollEntity)result.activateRagdollEntity=value.activateRagdollEntity;if(value.resetRagdollEntity)result.resetRagdollEntity=value.resetRagdollEntity;}return result;}
EntityId QtInspector::takeDecalPlacement(){auto result=std::exchange(impl_->decalPlacement,NoEntity);for(const auto& part:impl_->parts)if(part)if(const auto id=part->takeDecalPlacement())result=id;return result;}
bool QtInspector::finish(){bool result=impl_->finish();for(const auto& part:impl_->parts)if(part)result=part->finish()||result;return result;}
void QtInspector::cancel(){impl_->cancel();for(const auto& part:impl_->parts)if(part)part->cancel();}
void QtInspector::detach(){
    finish();auto& p=*impl_;for(const auto& part:p.parts)if(part)part->detach();
    if(p.document&&!p.embedded){const auto found=inspectors.find(p.document);if(found!=inspectors.end()&&found->second==this){inspectors.erase(found);subdivisionQueries.erase(p.document);}}
    p.rebuilding=true;p.parts.clear();p.document=nullptr;p.assets=nullptr;p.renderer=nullptr;p.quader=nullptr;p.collisionNames=nullptr;
    p.navigationProfile=nullptr;p.navigationStatus={};p.navigationLabel=nullptr;p.quaderLabel=nullptr;p.transaction=0;p.selection.clear();p.entity=NoEntity;p.generation=p.revision=p.catalogRevision=0;p.metadata.reset();p.templates=std::make_shared<CameraTemplateCatalog>();
    if(auto* old=p.scroll->takeWidget()){old->setParent(this);old->hide();old->deleteLater();}p.body=new QWidget;p.scroll->setWidget(p.body);p.status->clear();p.status->hide();p.rebuilding=false;
}
bool QtInspector::eventFilter(QObject* target,QEvent* event){if(event->type()==QEvent::KeyPress&&static_cast<QKeyEvent*>(event)->key()==Qt::Key_Escape){impl_->cancel();if(auto widget=qobject_cast<QWidget*>(target))widget->clearFocus();return true;}return QWidget::eventFilter(target,event);}
bool finishScenePanelEdit(SceneDocument& document){const auto found=inspectors.find(&document);return found!=inspectors.end()&&found->second->finish();}
void setMeshPaintSubdivisionQuery(SceneDocument& document,MeshPaintSubdivisionQuery query){subdivisionQueries[&document]=std::move(query);}
}
