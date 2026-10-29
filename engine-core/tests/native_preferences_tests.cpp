#include "integration/settings/LevelPreferences.h"
#include "integration/LevelSession.h"
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QMainWindow>
#include <QPushButton>
#include <QTemporaryDir>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string read(const std::filesystem::path& file){std::ifstream input(file,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}};}
void nativePreferenceDrafts() {
    QTemporaryDir temporary;require(temporary.isValid(),"Cannot create isolated native preference storage.");
    const auto root=std::filesystem::path(temporary.path().toStdWString());const auto file=root/"preferences.json";
    EditorPreferences initial(file);auto existing=initial.values();existing.graphicsBackend=GraphicsBackend::Direct3D11;
    require(initial.setValues(existing),"Cannot prepare the retained backend preference fixture.");
    const auto original=read(file);QMainWindow main;unsigned changes{};
    integration::LevelPreferences preferences(main,[&]{++changes;},file);
    require(read(file)==original&&!preferences.dialog(),"Loading native preferences wrote settings or opened a shell.");
    preferences.open();auto dialog=preferences.dialog();require(dialog,"The supplied main window did not own the preference dialog.");
    require(dialog->parentWidget()==&main,"Preferences created an unrelated application/window owner.");
    auto backend=dialog->findChild<QComboBox*>(QStringLiteral("CrimsonPrefsBackend"));
    require(backend&&backend->count()==2&&backend->findData(int(GraphicsBackend::Direct3D11))>=0&&backend->findData(int(GraphicsBackend::Direct3D12))>=0&&backend->currentData().toInt()==int(GraphicsBackend::Direct3D11),"The native renderer selector did not retain the accepted DX11 preference.");
    auto aa=dialog->findChild<QComboBox*>(QStringLiteral("CrimsonPrefsAA"));
    auto browser=dialog->findChild<QComboBox*>(QStringLiteral("CrimsonPrefsBrowser"));
    auto shadow=dialog->findChild<QComboBox*>(QStringLiteral("CrimsonPrefsShadows"));
    auto width=dialog->findChild<QDoubleSpinBox*>(QStringLiteral("CrimsonPrefsGridWidth0"));
    auto buttons=dialog->findChild<QDialogButtonBox*>(QStringLiteral("CrimsonPrefsButtons"));
    require(aa&&browser&&shadow&&width&&buttons,"A retained Engine preference is absent from the native view.");
    const auto choose=[](QComboBox* combo,int value){const auto index=combo->findData(value);require(index>=0,"The native preference omitted a supported value.");combo->setCurrentIndex(index);QMetaObject::invokeMethod(combo,"activated",Qt::DirectConnection,Q_ARG(int,index));};
    choose(backend,int(GraphicsBackend::Direct3D12));
    choose(aa,int(AntiAliasing::Off));choose(browser,16);choose(shadow,512);width->setValue(1.25);
    require(changes==0&&read(file)==original&&preferences.values().antiAliasing==AntiAliasing::Smaa,
        "An unfinished preference draft changed live or saved presentation.");
    buttons->button(QDialogButtonBox::Cancel)->click();
    require(read(file)==original&&preferences.values().appearance==existing.appearance,"Rejecting native preferences retained a draft.");
    require(preferences.values().graphicsBackend==GraphicsBackend::Direct3D11,"Cancel published a backend change before restart.");
    preferences.open();choose(aa,int(AntiAliasing::Off));choose(browser,16);choose(shadow,512);width->setValue(1.25);
    buttons->button(QDialogButtonBox::Apply)->click();
    require(changes==1&&preferences.values().antiAliasing==AntiAliasing::Off&&preferences.values().browserPreviewSize==16&&
        preferences.values().pointShadowResolution==512&&preferences.values().appearance.gridMinorWidth==1.25f,
        "Apply failed to publish the complete preference draft once.");
    require(EditorPreferences(file).values().graphicsBackend==GraphicsBackend::Direct3D11,
        "Native presentation preferences overwrote the retained backend compatibility value.");
    integration::LevelViewRequest frame;preferences.apply(frame);
    require(frame.antiAliasing==AntiAliasing::Off&&frame.shadowResolution==512&&frame.appearance==preferences.values().appearance,
        "Native preferences did not propagate through the actual Level frame contract.");
    const auto saved=read(file);width->setValue(2);buttons->button(QDialogButtonBox::Cancel)->click();
    require(read(file)==saved&&preferences.values().appearance.gridMinorWidth==1.25f,"Cancel reverted an accepted Apply or committed a later draft.");
    preferences.open();dialog->findChild<QPushButton*>(QStringLiteral("CrimsonPrefsReset"))->click();
    require(read(file)==saved,"Reset Defaults wrote before explicit Apply.");buttons->button(QDialogButtonBox::Ok)->click();
    require(preferences.values().graphicsBackend==GraphicsBackend::Direct3D11&&preferences.values().antiAliasing==AntiAliasing::Smaa,
        "Default presentation reset changed the retained backend value.");
    preferences.open();integration::LevelViewRequest before;preferences.apply(before);choose(backend,int(GraphicsBackend::Direct3D12));buttons->button(QDialogButtonBox::Apply)->click();
    require(EditorPreferences(file).values().graphicsBackend==GraphicsBackend::Direct3D12,"Explicit backend Apply did not persist the next-launch renderer.");
    integration::LevelViewRequest after;preferences.apply(after);require(before.appearance==after.appearance&&before.antiAliasing==after.antiAliasing&&before.shadowResolution==after.shadowResolution,"A restart-only backend choice changed the current presentation contract.");
    choose(backend,int(GraphicsBackend::Direct3D11));buttons->button(QDialogButtonBox::Cancel)->click();require(EditorPreferences(file).values().graphicsBackend==GraphicsBackend::Direct3D12,"Cancel reverted the accepted backend or saved an unfinished later draft.");
}
void failedPersistenceRetainsAcceptedValues() {
    QTemporaryDir temporary;require(temporary.isValid(),"Cannot create failed preference persistence fixture.");
    const auto blocker=std::filesystem::path(temporary.path().toStdWString())/"file";
    {std::ofstream out(blocker);out<<"preserve";}
    EditorPreferences preferences(blocker/"preferences.json");const auto accepted=preferences.values();
    auto changed=accepted;changed.browserPreviewSize=256;
    require(!preferences.setValues(changed)&&!preferences.status().empty()&&preferences.values().browserPreviewSize==accepted.browserPreviewSize&&read(blocker)=="preserve",
        "Failed native preference persistence replaced accepted values or an unrelated file.");
}
}
int main(int argc,char** argv){qputenv("QT_QPA_PLATFORM","minimal");QApplication app(argc,argv);try{nativePreferenceDrafts();failedPersistenceRetainsAcceptedValues();std::cout<<"Native Engine preferences drafts/Apply/Cancel, original backend compatibility, renderer frame propagation and atomic failure preservation passed.\n";return 0;}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
