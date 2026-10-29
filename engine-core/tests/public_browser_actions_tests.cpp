#include "integration/browser/LevelAssetBrowser.h"
#include "integration/LevelSession.h"
#include "assets/AssetLibrary.h"
#include "assets/AtomicFile.h"
#include "resources/Paths.h"
#include "ui/qt_asset_browser.hpp"
#include "core/release_features.hpp"
#include <QAction>
#include <QApplication>
#include <QListView>
#include <QMainWindow>
#include <QMenu>
#include <QTemporaryDir>
#include <QTimer>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace {
using namespace crimson;
using namespace crimson::integration;
using Action=quader::QtAssetBrowserAction;
using Kind=quader::QtAssetBrowserKind;

std::filesystem::path existing_project_folder(const QTemporaryDir& temporary){
    if(!temporary.isValid())throw std::runtime_error("Cannot create isolated browser project.");
    const auto root=std::filesystem::path(temporary.path().toStdWString())/"Project";
    std::filesystem::create_directory(root);return root;
}

std::filesystem::path write_rig_source(const std::filesystem::path& folder){
    std::filesystem::create_directories(folder);std::vector<std::uint8_t> bytes;
    const auto append=[&](const auto& values){const auto* first=reinterpret_cast<const std::uint8_t*>(values.data());bytes.insert(bytes.end(),first,first+sizeof(values));};
    append(std::array<float,9>{0,0,0,.2f,0,0,0,.5f,0});append(std::array<std::uint8_t,12>{0,0,0,0,0,0,0,0,1,0,0,0});append(std::array<float,12>{1,0,0,0,1,0,0,0,1,0,0,0});
    std::ofstream buffer(folder/"rig.bin",std::ios::binary);buffer.write(reinterpret_cast<const char*>(bytes.data()),std::streamsize(bytes.size()));buffer.close();
    using Json=nlohmann::json;
    const Json gltf{{"asset",{{"version","2.0"}}},{"scene",0},{"scenes",Json::array({{{"nodes",{0,2}}}})},
        {"nodes",Json::array({{{"name","Base"},{"children",{1}}},{{"name","End"},{"translation",{0,.5,0}}},{{"name","Surface"},{"mesh",0},{"skin",0}}})},
        {"skins",Json::array({{{"name","Rig"},{"joints",{0,1}},{"skeleton",0}}})},
        {"meshes",Json::array({{{"primitives",Json::array({{{"attributes",{{"POSITION",0},{"JOINTS_0",1},{"WEIGHTS_0",2}}}}})}}})},
        {"buffers",Json::array({{{"uri","rig.bin"},{"byteLength",bytes.size()}}})},
        {"bufferViews",Json::array({{{"buffer",0},{"byteOffset",0},{"byteLength",36}},{{"buffer",0},{"byteOffset",36},{"byteLength",12}},{{"buffer",0},{"byteOffset",48},{"byteLength",48}}})},
        {"accessors",Json::array({{{"bufferView",0},{"componentType",5126},{"count",3},{"type","VEC3"},{"min",{0,0,0}},{"max",{.2,.5,0}}},
            {{"bufferView",1},{"componentType",5121},{"count",3},{"type","VEC4"}},{{"bufferView",2},{"componentType",5126},{"count",3},{"type","VEC4"}}})}};
    writeFileAtomically(folder/"rig.gltf",gltf.dump());return folder/"rig.gltf";
}

struct BrowserFixture {
    QTemporaryDir temporary;
    std::filesystem::path root{existing_project_folder(temporary)};
    QMainWindow window;
    LevelSession session{root};
    LevelAssetBrowser controller{window,[]{},false};
    quader::QtAssetBrowser view;
    std::shared_ptr<quader::QtAssetBrowserExtension> extension;
    std::uint64_t revision{};
    unsigned imports{},fallbacks{};
    BrowserFixture(){
        if(!temporary.isValid())throw std::runtime_error("Cannot create isolated browser project.");
        controller.bind(&session);extension=controller.extension();view.set_extension(extension);view.set_material_authoring_available(false);
        view.set_action_callback([this](auto request){
            if(request.action==Action::Import){++imports;return;} // The real dialog belongs to the interactive caller.
            if(!extension->handle_action(request))++fallbacks;
        });
    }
    void publish(std::vector<quader::QtAssetBrowserItem> physical={}){
        quader::QtAssetBrowserSnapshot snapshot;snapshot.generation=11;snapshot.revision=++revision;snapshot.items=std::move(physical);
        extension->append_snapshot(snapshot);snapshot.dependency_revision=extension->revision();view.set_snapshot(std::move(snapshot));
    }
    quader::QtAssetBrowserItem item(const std::string& asset)const{
        const auto& items=view.snapshot().items;const auto found=std::find_if(items.begin(),items.end(),[&](const auto& value){return value.extension_id==QString::fromStdString(asset);});
        if(found==items.end())throw std::runtime_error("The shared browser omitted an Engine asset.");return *found;
    }
    QStringList context_labels(bool selected){
        auto* grid=view.findChild<QListView*>(QStringLiteral("assetBrowser.grid"));if(!grid)throw std::runtime_error("Missing actual browser grid.");
        QStringList result;
        QTimer::singleShot(0,[&]{auto* menu=qobject_cast<QMenu*>(QApplication::activePopupWidget());if(!menu)menu=view.findChild<QMenu*>();if(menu){
            const auto visit=[&](auto&& self,QMenu* current)->void{for(auto* action:current->actions()){result.push_back(action->text());if(action->menu())self(self,action->menu());}};
            visit(visit,menu);menu->close();
        }});
        const QPoint at=selected?grid->visualRect(grid->currentIndex()).center():QPoint{-2,-2};
        QMetaObject::invokeMethod(grid,"customContextMenuRequested",Qt::DirectConnection,Q_ARG(QPoint,at));return result;
    }
};
}

TEST(PublicBrowserActions, EngineCreatesAndImportRemainAvailableWithNativeMaterialAuthoringDisabled){
    ASSERT_EQ(quader::current_product_release(),quader::kPublicRelease01);ASSERT_FALSE(quader::release_has_feature(quader::ReleaseFeature::Materials));
    BrowserFixture fixture;fixture.publish();
    ASSERT_TRUE(fixture.extension->accepts_import);EXPECT_TRUE(fixture.extension->accepts_import({QStringLiteral("source.gltf"),QStringLiteral("source.png")}));EXPECT_FALSE(fixture.extension->accepts_import({QStringLiteral("source.exe")}));
    EXPECT_FALSE(fixture.view.handle_action(Action::CreateMaterial));EXPECT_TRUE(fixture.view.handle_action(Action::Import));EXPECT_EQ(fixture.imports,1u);
    const std::array actions{Action::CreateGraphMaterial,Action::CreateBlendMaterial,Action::CreateParticle,Action::CreateDecal,Action::CreateNavigationProfile,Action::CreateGameplayEffect};
    for(const auto action:actions){ASSERT_TRUE(fixture.view.handle_action(action));const auto pending=fixture.controller.takeActions();
        EXPECT_EQ(pending.createMaterial,action==Action::CreateGraphMaterial);EXPECT_EQ(pending.createBlendMaterial,action==Action::CreateBlendMaterial);
        EXPECT_EQ(pending.createParticle,action==Action::CreateParticle);EXPECT_EQ(pending.createDecal,action==Action::CreateDecal);
        EXPECT_EQ(pending.createNavigationAgentProfile,action==Action::CreateNavigationProfile);EXPECT_EQ(pending.createGameplayEffect,action==Action::CreateGameplayEffect);
    }
    const auto labels=fixture.context_labels(false);
    for(const auto* label:{"Import…","Material Graph","Blend Material","Particle Effect","Decal","Navigation Agent Profile","Gameplay Effect"})EXPECT_TRUE(labels.contains(QString::fromUtf8(label)))<<label;
    EXPECT_FALSE(labels.contains(QStringLiteral("Create Material")));EXPECT_EQ(fixture.fallbacks,0u);
}

TEST(PublicBrowserActions, EngineGraphAndBlendRowsOpenThroughTheSharedBrowser){
    BrowserFixture fixture;const auto graph=fixture.session.assets().createMaterial("Public graph"),blend=fixture.session.assets().createBlendMaterial("Public blend");ASSERT_TRUE(graph&&blend);
    fixture.publish();for(const auto& asset:{graph,blend}){const auto item=fixture.item(asset->id);ASSERT_EQ(item.kind,Kind::Material);ASSERT_TRUE(item.external);ASSERT_TRUE(fixture.view.reveal_item(item.id));
        const auto selected=fixture.view.selected_items();ASSERT_EQ(selected.size(),1u);EXPECT_EQ(selected.front().extension_id,QString::fromStdString(asset->id));
        ASSERT_TRUE(fixture.view.handle_action(Action::Open));EXPECT_EQ(fixture.controller.takeActions().openMaterial,asset->id);
    }
    EXPECT_FALSE(fixture.view.handle_action(Action::CreateRagdoll));
}

TEST(PublicBrowserActions, SelectedNativeModelPackageExposesAndRoutesRagdollCreation){
    BrowserFixture fixture;const auto source=write_rig_source(std::filesystem::path(fixture.temporary.path().toStdWString())/"Source");const auto model=fixture.session.assets().importModel(source);ASSERT_TRUE(model)<<fixture.session.assets().status();
    const auto package=resolveAssetPath(fixture.root,model->id).parent_path().lexically_relative(fixture.root);const auto relative=QString::fromStdWString(package.generic_wstring());
    quader::QtAssetBrowserItem folder;folder.id=QStringLiteral("folder:")+relative;folder.relative_path=relative;folder.name=QString::fromStdWString(package.filename().wstring());folder.kind=Kind::Folder;
    fixture.publish({folder});const auto item=fixture.item(model->id);EXPECT_EQ(item.kind,Kind::Folder);EXPECT_FALSE(item.external);ASSERT_TRUE(fixture.extension->can_create_ragdoll(item));
    EXPECT_EQ(std::count_if(fixture.view.snapshot().items.begin(),fixture.view.snapshot().items.end(),[&](const auto& value){return value.extension_id==QString::fromStdString(model->id);}),1);
    ASSERT_TRUE(fixture.view.reveal_item(item.id));EXPECT_TRUE(fixture.context_labels(true).contains(QStringLiteral("Create Ragdoll")));
    ASSERT_TRUE(fixture.view.handle_action(Action::CreateRagdoll));const auto pending=fixture.controller.takeActions();EXPECT_EQ(pending.createRagdollModel,model->id);EXPECT_EQ(pending.ragdollFolder,BrowserCatalog::RootFolder);EXPECT_EQ(fixture.fallbacks,0u);
}
