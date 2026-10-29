#include "LevelPreferences.h"
#include "integration/LevelSession.h"
#include "ui/qt_color_swatch_button.hpp"
#include "ui/qt_double_spin_box.hpp"
#include "ui/qt_settings_control_widgets.hpp"
#include <QApplication>
#include <QColorDialog>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QMainWindow>
#include <QPointer>
#include <QPushButton>
#include <QSignalBlocker>
#include <QVBoxLayout>

namespace crimson::integration {
struct LevelPreferences::Impl {
    QObject connections;
    QPointer<QMainWindow> parent;
    QPointer<QDialog> window;
    std::function<void()> changed;
    EditorPreferences preferences;
    EditorPreferenceValues draft;
    QComboBox *aa{},*shadows{},*browser{},*backend{};
    std::array<quader::QtColorSwatchButton*,3> colors{};
    std::array<quader::QtDoubleSpinBox*,2> widths{};
    QLabel* status{};
    Impl(QMainWindow& main,std::function<void()> frame,std::filesystem::path file)
        :parent(&main),changed(std::move(frame)),preferences(std::move(file)),draft(preferences.values()){}
    ~Impl(){delete window.data();}
    void synchronize() {
        const QSignalBlocker a(aa),b(shadows),c(browser),d(backend);
        backend->setCurrentIndex(backend->findData(int(draft.graphicsBackend)));
        aa->setCurrentIndex(aa->findData(int(draft.antiAliasing)));
        shadows->setCurrentIndex(shadows->findData(draft.pointShadowResolution));
        browser->setCurrentIndex(browser->findData(draft.browserPreviewSize));
        for(unsigned field=0;field<colors.size();++field) {
            const auto value=field==0?Vec4{draft.appearance.backgroundSrgb[0],draft.appearance.backgroundSrgb[1],draft.appearance.backgroundSrgb[2],1}:
                field==1?draft.appearance.gridMinorSrgb:draft.appearance.gridMajorSrgb;
            colors[field]->set_swatch_color(QColor::fromRgbF(value[0],value[1],value[2],value[3]));
        }
        for(unsigned field=0;field<widths.size();++field){const QSignalBlocker block(widths[field]);widths[field]->setValue(field?draft.appearance.gridMajorWidth:draft.appearance.gridMinorWidth);}
        status->setText(QString::fromStdString(preferences.status()));status->setVisible(!preferences.status().empty());
    }
    bool apply() {
        for(auto width:widths)width->interpretText();
        const bool committed=preferences.setValues(draft);
        status->setText(QString::fromStdString(preferences.status()));status->setVisible(!preferences.status().empty());
        if(!committed&&!preferences.status().empty())return false;
        draft=preferences.values();if(committed&&changed)changed();return true;
    }
    void create() {
        if(window)return;
        window=new QDialog(parent);window->setObjectName(QStringLiteral("CrimsonEnginePreferences"));
        window->setWindowTitle(QStringLiteral("Engine Preferences"));window->resize(480,540);
        auto layout=new QVBoxLayout(window);
        const auto section=[&](const char* name,const QString& title){
            auto section=new quader::qt_settings_detail::QtSettingsSection(QString::fromUtf8(name),window);
            section->set_title(title);layout->addWidget(section);return new QFormLayout(section->content());
        };
        auto rendering=section("CrimsonPreferences.Rendering",QStringLiteral("Rendering"));
        backend=new QComboBox(window);backend->setObjectName(QStringLiteral("CrimsonPrefsBackend"));backend->addItem(QStringLiteral("Direct3D 12"),int(GraphicsBackend::Direct3D12));backend->addItem(QStringLiteral("Direct3D 11"),int(GraphicsBackend::Direct3D11));backend->setToolTip(QStringLiteral("Used when the application next starts."));rendering->addRow(QStringLiteral("Renderer (restart required)"),backend);
        QObject::connect(backend,&QComboBox::activated,&connections,[this]{draft.graphicsBackend=static_cast<GraphicsBackend>(backend->currentData().toInt());});
        aa=new QComboBox(window);aa->setObjectName(QStringLiteral("CrimsonPrefsAA"));
        aa->addItem(QStringLiteral("Off"),int(AntiAliasing::Off));aa->addItem(QStringLiteral("SMAA"),int(AntiAliasing::Smaa));
        rendering->addRow(QStringLiteral("Antialiasing"),aa);
        shadows=new QComboBox(window);shadows->setObjectName(QStringLiteral("CrimsonPrefsShadows"));
        for(auto resolution:PointShadowResolutions)shadows->addItem(QString::number(resolution),resolution);
        rendering->addRow(QStringLiteral("Shadow Resolution"),shadows);
        browser=new QComboBox(window);browser->setObjectName(QStringLiteral("CrimsonPrefsBrowser"));
        for(auto edge:BrowserPreviewSizes)browser->addItem(QString::number(edge),edge);
        rendering->addRow(QStringLiteral("Browser Preview Size"),browser);
        QObject::connect(aa,&QComboBox::activated,&connections,[this]{draft.antiAliasing=static_cast<AntiAliasing>(aa->currentData().toInt());});
        QObject::connect(shadows,&QComboBox::activated,&connections,[this]{draft.pointShadowResolution=shadows->currentData().toInt();});
        QObject::connect(browser,&QComboBox::activated,&connections,[this]{draft.browserPreviewSize=browser->currentData().toInt();});
        auto viewport=section("CrimsonPreferences.Viewport",QStringLiteral("Viewport"));
        constexpr std::array colorNames{"Background","Minor Grid Color","Major Grid Color"};
        for(unsigned field=0;field<colors.size();++field) {
            auto button=new quader::QtColorSwatchButton(window);colors[field]=button;
            button->setObjectName(QStringLiteral("CrimsonPrefsColor%1").arg(field));viewport->addRow(QString::fromUtf8(colorNames[field]),button);
            QObject::connect(button,&QPushButton::clicked,&connections,[this,field]{
                const auto color=QColorDialog::getColor(colors[field]->swatch_color(),window,QStringLiteral("Choose Color"),
                    field?QColorDialog::ShowAlphaChannel:QColorDialog::ColorDialogOptions{});
                if(!color.isValid())return;
                if(field==0)draft.appearance.backgroundSrgb={float(color.redF()),float(color.greenF()),float(color.blueF())};
                else (field==1?draft.appearance.gridMinorSrgb:draft.appearance.gridMajorSrgb)={float(color.redF()),float(color.greenF()),float(color.blueF()),float(color.alphaF())};
                colors[field]->set_swatch_color(color);
            });
        }
        for(unsigned field=0;field<widths.size();++field) {
            auto width=new quader::QtDoubleSpinBox(window);widths[field]=width;
            width->setObjectName(QStringLiteral("CrimsonPrefsGridWidth%1").arg(field));width->setDecimals(3);width->setRange(.25,8);width->setSingleStep(.25);
            viewport->addRow(field?QStringLiteral("Major Grid Width"):QStringLiteral("Minor Grid Width"),width);
            QObject::connect(width,&QDoubleSpinBox::valueChanged,&connections,[this,field](double value){
                (field?draft.appearance.gridMajorWidth:draft.appearance.gridMinorWidth)=float(value);
            });
        }
        auto reset=new QPushButton(QStringLiteral("Reset Defaults"),window);reset->setObjectName(QStringLiteral("CrimsonPrefsReset"));layout->addWidget(reset);
        QObject::connect(reset,&QPushButton::clicked,&connections,[this]{
            const auto retainedBackend=draft.graphicsBackend;draft=EditorPreferenceValues{};draft.graphicsBackend=retainedBackend;synchronize();
        });
        status=new QLabel(window);status->setWordWrap(true);layout->addWidget(status);layout->addStretch(1);
        auto buttons=new QDialogButtonBox(QDialogButtonBox::Ok|QDialogButtonBox::Apply|QDialogButtonBox::Cancel,window);
        buttons->setObjectName(QStringLiteral("CrimsonPrefsButtons"));layout->addWidget(buttons);
        QObject::connect(buttons->button(QDialogButtonBox::Apply),&QPushButton::clicked,&connections,[this]{apply();});
        QObject::connect(buttons,&QDialogButtonBox::accepted,&connections,[this]{if(apply())window->accept();});
        QObject::connect(buttons,&QDialogButtonBox::rejected,window,&QDialog::reject);
        QObject::connect(window,&QDialog::finished,&connections,[this]{draft=preferences.values();});
    }
};
LevelPreferences::LevelPreferences(QMainWindow& main,std::function<void()> changed,std::filesystem::path file)
    :impl_(std::make_unique<Impl>(main,std::move(changed),std::move(file))){}
LevelPreferences::~LevelPreferences()=default;
const EditorPreferenceValues& LevelPreferences::values()const{return impl_->preferences.values();}
void LevelPreferences::open(){auto& state=*impl_;state.create();if(!state.window->isVisible()){state.draft=state.preferences.values();state.synchronize();}state.window->show();state.window->raise();state.window->activateWindow();}
bool LevelPreferences::ownsInput()const {
    const auto dialog=impl_->window.data();if(!dialog)return false;
    const auto focus=QApplication::focusWidget();const auto popup=QApplication::activePopupWidget();const auto modal=QApplication::activeModalWidget();
    return (focus&&(focus==dialog||dialog->isAncestorOf(focus)))||(popup&&(popup==dialog||dialog->isAncestorOf(popup)))||
        (modal&&(modal==dialog||dialog->isAncestorOf(modal)));
}
void LevelPreferences::apply(LevelViewRequest& frame)const{const auto& values=impl_->preferences.values();frame.appearance=values.appearance;frame.antiAliasing=values.antiAliasing;frame.shadowResolution=std::uint16_t(values.pointShadowResolution);}
QDialog* LevelPreferences::dialog()const{return impl_->window.data();}
}
