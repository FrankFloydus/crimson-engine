#include "integration/LevelPanels.h"
#include "integration/LevelSession.h"
#include "integration/widgets/QtInspector.h"
#include "crimson-renderer/Renderer.h"
#include "core/release_features.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "render/crimson/crimson_render_host.hpp"
#include "ui/inspector_panel.hpp"
#include <QAction>
#include <QApplication>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QMainWindow>
#include <QMenuBar>
#include <QMenu>
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <filesystem>

namespace {
struct PlayFixture {
    QTemporaryDir temporary;
    std::filesystem::path directory{temporary.path().toStdWString()},root{directory/"Project"};
    quader::ProjectSession project{directory/"recents.json"};quader::QuaderEditorApi editor{project};
    std::unique_ptr<quader::RenderHost> host{quader::create_crimson_render_host()};QMainWindow window;crimson::Renderer cpuRenderer;
    std::unique_ptr<crimson::integration::LevelSession> runtime;std::unique_ptr<crimson::integration::LevelPanels> panels;
    unsigned requests{},beforePlay{};
    PlayFixture(){
        if(!temporary.isValid())throw std::runtime_error("Cannot create isolated Play workspace.");
        std::filesystem::create_directory(root);project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR)/"resources/modeling_assets");
        const auto created=editor.project().create(root);if(!created.ok())throw std::runtime_error(created.error_message());
        const auto mode=editor.mode().set_workspace_mode(quader::QuaderWorkspaceMode::Default);if(!mode.ok())throw std::runtime_error(mode.error_message());
        runtime=std::make_unique<crimson::integration::LevelSession>(root);
        panels=std::make_unique<crimson::integration::LevelPanels>(window,*host,editor,project,[this]{++requests;});panels->bind(runtime.get());
        panels->bindScene(project.active_scene()->session.get(),std::string(project.active_scene()->scene_id.str()));panels->setBeforePlay([this]{++beforePlay;});
    }
    QAction* action(const char* name){return window.findChild<QAction*>(name);}
    QDockWidget* game(){return window.findChild<QDockWidget*>("GameViewport");}
    QLabel* status(){return window.findChild<QLabel*>("enginePlayStatus");}
};
}

TEST(SingleSceneWorkspace, PublicWorkspaceCreatesItsOwnEngineCreationMenu){
    PlayFixture fixture;QMenu* create=nullptr;for(auto* action:fixture.window.menuBar()->actions())if(action->menu()&&action->menu()->objectName()=="menu_create")create=action->menu();ASSERT_NE(create,nullptr);
    EXPECT_TRUE(create->menuAction()->isVisible());
    for(const char* name:{"Player","Enemy","Point Light","Directional Light","Spot Light","Camera","Environment","Collider","Navigation Volume"}){
        QAction* found=nullptr;for(auto* action:create->actions())if(action->text()==name)found=action;
        ASSERT_NE(found,nullptr)<<name;EXPECT_TRUE(found->isVisible())<<name;EXPECT_TRUE(found->isEnabled())<<name;
        const auto before=fixture.project.active_scene()->session->scene().entities().size();found->trigger();
        EXPECT_GT(fixture.project.active_scene()->session->scene().entities().size(),before)<<name;
    }
    fixture.panels.reset();EXPECT_EQ(fixture.window.findChild<QMenu*>("menu_create"),nullptr);
}

TEST(SingleSceneWorkspace, PublicPlayStartsCleanSceneAndShowsGameWithoutGpuInitialization){
    EXPECT_EQ(quader::current_product_release(),quader::kPublicRelease01);EXPECT_FALSE(quader::release_has_feature(quader::ReleaseFeature::Materials));
    PlayFixture fixture;ASSERT_NE(fixture.action("engineActionPlay"),nullptr);ASSERT_NE(fixture.game(),nullptr);EXPECT_TRUE(fixture.game()->isHidden());
    const auto native_revision=fixture.project.active_scene()->session->scene().content_revision();fixture.action("engineActionPlay")->trigger();
    EXPECT_TRUE(fixture.runtime->playing())<<fixture.status()->text().toStdString();EXPECT_FALSE(fixture.runtime->playPending());EXPECT_TRUE(fixture.game()->isVisibleTo(&fixture.window));
    EXPECT_EQ(fixture.beforePlay,1u);EXPECT_GT(fixture.requests,0u);EXPECT_FALSE(fixture.action("engineActionPlay")->isEnabled());EXPECT_TRUE(fixture.action("engineActionStop")->isEnabled());
    EXPECT_TRUE(fixture.status()->text().contains("Camera",Qt::CaseInsensitive));EXPECT_FALSE(fixture.cpuRenderer.initialized());
    EXPECT_EQ(fixture.project.active_scene()->session->scene().content_revision(),native_revision);
    fixture.action("engineActionStop")->trigger();EXPECT_FALSE(fixture.runtime->playing());EXPECT_TRUE(fixture.action("engineActionPlay")->isEnabled());
}

TEST(SingleSceneWorkspace, PlayCommitsActiveInspectorEditAndRepeatedNoopFinishStillStarts){
    PlayFixture fixture;QAction* player=nullptr;for(auto* menu:fixture.window.findChildren<QMenu*>())for(auto* action:menu->actions())if(action->text()=="Player")player=action;
    ASSERT_NE(player,nullptr);player->trigger();auto& document=fixture.runtime->document();const auto id=document.scene().properties().playerCharacter;ASSERT_NE(id,crimson::NoEntity)<<document.status();
    auto* inspector=static_cast<crimson::QtInspector*>(fixture.panels->componentExtension());inspector->synchronize(document,fixture.runtime->assets(),fixture.cpuRenderer,id);
    auto* wrapper=inspector->findChild<QWidget*>("SceneField/movement/jumpHeight");ASSERT_NE(wrapper,nullptr);auto* field=wrapper->findChild<QDoubleSpinBox*>();ASSERT_NE(field,nullptr);
    field->setValue(2.0);ASSERT_TRUE(document.transactionActive());const auto history=fixture.project.active_scene()->session->history().undo_count();
    fixture.action("engineActionPlay")->trigger();ASSERT_TRUE(fixture.runtime->playing())<<fixture.status()->text().toStdString();EXPECT_FALSE(document.transactionActive());EXPECT_TRUE(fixture.game()->isVisibleTo(&fixture.window));
    EXPECT_EQ(fixture.project.active_scene()->session->history().undo_count(),history+1);EXPECT_EQ(document.scene().find(document.scene().properties().playerCharacter)->movement->jumpHeight,2.f);
    fixture.action("engineActionStop")->trigger();ASSERT_TRUE(inspector->finish());ASSERT_TRUE(inspector->finish());fixture.game()->hide();
    fixture.action("engineActionPlay")->trigger();EXPECT_TRUE(fixture.runtime->playing())<<fixture.status()->text().toStdString();EXPECT_TRUE(fixture.game()->isVisibleTo(&fixture.window));EXPECT_EQ(fixture.beforePlay,2u);
}

TEST(SingleSceneWorkspace, PendingPlayShowsGameAndRetriesWithoutAnotherClickAndCanBeStopped){
    PlayFixture fixture;fixture.runtime->setModelingWorld({},false);fixture.action("engineActionPlay")->trigger();
    EXPECT_FALSE(fixture.runtime->playing());ASSERT_TRUE(fixture.runtime->playPending());EXPECT_TRUE(fixture.game()->isVisibleTo(&fixture.window));EXPECT_TRUE(fixture.status()->text().contains("Preparing"));EXPECT_TRUE(fixture.action("engineActionStop")->isEnabled());
    fixture.runtime->setModelingWorld({},true);fixture.runtime->update(0,true);fixture.panels->update(0,false);
    EXPECT_TRUE(fixture.runtime->playing())<<fixture.status()->text().toStdString();EXPECT_FALSE(fixture.runtime->playPending());EXPECT_TRUE(fixture.game()->isVisibleTo(&fixture.window));EXPECT_EQ(fixture.beforePlay,1u);
    fixture.action("engineActionStop")->trigger();fixture.runtime->setModelingWorld({},false);fixture.action("engineActionPlay")->trigger();ASSERT_TRUE(fixture.runtime->playPending());
    fixture.action("engineActionStop")->trigger();fixture.runtime->setModelingWorld({},true);fixture.runtime->update(0,true);fixture.panels->update(0,false);
    EXPECT_FALSE(fixture.runtime->playing());EXPECT_FALSE(fixture.runtime->playPending());EXPECT_FALSE(fixture.status()->text().contains("Preparing"));
}

TEST(SingleSceneWorkspace, DefaultLightingBindingFollowsWorkspaceWithoutAuthoredWrites){
    PlayFixture fixture;const auto revision=fixture.project.active_scene()->session->scene().content_revision();
    EXPECT_FALSE(fixture.panels->prepareFrame());const auto first=fixture.runtime->nativeRenderBindings();ASSERT_TRUE(first);EXPECT_TRUE(first->scene_lighting);
    EXPECT_FALSE(fixture.panels->prepareFrame());EXPECT_EQ(fixture.runtime->nativeRenderBindings(),first);
    ASSERT_TRUE(fixture.editor.mode().set_workspace_mode(quader::QuaderWorkspaceMode::Modeling).ok());EXPECT_FALSE(fixture.panels->prepareFrame());const auto modeling=fixture.runtime->nativeRenderBindings();ASSERT_TRUE(modeling);EXPECT_FALSE(modeling->scene_lighting);EXPECT_NE(modeling,first);
    ASSERT_TRUE(fixture.editor.mode().set_workspace_mode(quader::QuaderWorkspaceMode::Default).ok());EXPECT_FALSE(fixture.panels->prepareFrame());EXPECT_TRUE(fixture.runtime->nativeRenderBindings()->scene_lighting);
    EXPECT_EQ(fixture.project.active_scene()->session->scene().content_revision(),revision);EXPECT_EQ(fixture.project.active_scene()->session->history().undo_count(),0u);EXPECT_FALSE(quader::release_has_feature(quader::ReleaseFeature::RenderedViewport));
}

TEST(SingleSceneWorkspace, EngineControlsExtendTheNativeSceneWithoutAnotherEditor){
    QTemporaryDir temporary;ASSERT_TRUE(temporary.isValid());const auto directory=std::filesystem::path(temporary.path().toStdWString());const auto root=directory/"Project";std::filesystem::create_directory(root);
    quader::ProjectSession project(directory/"recents.json");project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR)/"resources/modeling_assets");quader::QuaderEditorApi editor(project);ASSERT_TRUE(editor.project().create(root).ok());
    auto host=quader::create_crimson_render_host();ASSERT_TRUE(host);QMainWindow window;
    auto* create=window.menuBar()->addMenu("Create");create->setObjectName("menu_create");
    auto* nativeInspector=new quader::InspectorPanel;
    const auto dock=[&](const char* name,QWidget* widget){auto* result=new QDockWidget(name,&window);result->setObjectName(name);result->setWidget(widget);window.addDockWidget(Qt::LeftDockWidgetArea,result);return result;};
    dock("dock_outliner",new QWidget);dock("dock_inspector",nativeInspector);dock("dock_asset_browser",new QWidget);
    crimson::integration::LevelSession runtime(root);
    {
        crimson::integration::LevelPanels panels(window,*host,editor,project,[]{});panels.bind(&runtime);panels.bindScene(project.active_scene()->session.get(),std::string(project.active_scene()->scene_id.str()));nativeInspector->set_component_extension(panels.componentExtension());
        for(const auto* name:{"EngineScene","EngineOutliner","EngineInspector","EngineAssets"})EXPECT_EQ(window.findChild<QDockWidget*>(name),nullptr)<<name;
        EXPECT_EQ(window.findChildren<QDockWidget*>("dock_outliner").size(),1);EXPECT_EQ(window.findChildren<QDockWidget*>("dock_inspector").size(),1);EXPECT_EQ(window.findChildren<QDockWidget*>("dock_asset_browser").size(),1);
        EXPECT_TRUE(nativeInspector->isAncestorOf(panels.componentExtension()));ASSERT_NE(window.findChild<QDockWidget*>("GameViewport"),nullptr);EXPECT_FALSE(window.findChild<QDockWidget*>("GameViewport")->isVisible());
        QAction* player=nullptr;for(auto* action:create->actions())if(action->text()=="Player")player=action;ASSERT_NE(player,nullptr);player->trigger();
        EXPECT_EQ(project.active_scene()->session->scene().entities().size(),2u);EXPECT_EQ(project.active_scene()->session->history().undo_count(),1u);EXPECT_TRUE(panels.confirmClose());
        nativeInspector->set_component_extension(nullptr);
    }
    EXPECT_EQ(window.findChild<QDockWidget*>("GameViewport"),nullptr);
    for(auto* action:create->actions())EXPECT_NE(action->text(),"Player");
}
int main(int argc,char** argv){qputenv("QT_QPA_PLATFORM","minimal:enable_fonts");QApplication application(argc,argv);testing::InitGoogleTest(&argc,argv);return RUN_ALL_TESTS();}
