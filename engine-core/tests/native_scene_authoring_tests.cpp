#include "integration/LevelSession.h"
#include "integration/native_scene_authoring.hpp"
#include "integration/native_scene_projection.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "runtime/PlaySession.h"
#include "scene/SceneExtraction.h"
#include "core/release_features.hpp"
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <algorithm>
#include <filesystem>

namespace {
using namespace crimson;
using namespace crimson::integration;
struct Fixture final {
    QTemporaryDir temporary;
    std::filesystem::path root{std::filesystem::path(temporary.path().toStdWString())/"Project"};
    quader::ProjectSession project{std::filesystem::path(temporary.path().toStdWString())/"recents.json"};
    quader::QuaderEditorApi editor{project};
    std::unique_ptr<LevelSession> runtime;
    std::unique_ptr<NativeSceneAuthoring> authoring;
    Fixture(){
        std::filesystem::create_directory(root);
        project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR)/"resources/modeling_assets");
        const auto result=editor.project().create(root);if(!result.ok())throw std::runtime_error(result.error_message());
        runtime=std::make_unique<LevelSession>(root);authoring=std::make_unique<NativeSceneAuthoring>(*runtime,project);
        authoring->bind(project.active_scene()->session.get(),std::string(project.active_scene()->scene_id.str()));
    }
    quader::SceneSession& native(){return *project.active_scene()->session;}
};
TEST(NativeSceneAuthoring, ModelingCompositionAndPlayerShareOneDocumentAndHistory){
    Fixture fixture;auto& native=fixture.native();
    ASSERT_EQ(quader::current_product_release(),quader::kPublicRelease01);
    EXPECT_FALSE(quader::release_has_feature(quader::ReleaseFeature::UvEditing));
    ASSERT_TRUE(fixture.editor.mode().set_workspace_mode(quader::QuaderWorkspaceMode::Modeling).ok());
    const auto box=fixture.editor.modeling().create_box({.min={-2,-1,-2},.max={2,0,2}},"Floor");ASSERT_TRUE(box.ok())<<box.error_message();
    const auto floor_uuid=native.scene().uuid(box.value().entity);
    ASSERT_TRUE(fixture.editor.mode().set_workspace_mode(quader::QuaderWorkspaceMode::Default).ok());
    fixture.authoring->synchronize();
    const auto light=fixture.runtime->document().createPointLight();ASSERT_NE(light,NoEntity)<<fixture.runtime->document().status();
    fixture.authoring->synchronize();const auto before=native.scene().entities().size();const auto history=native.history().undo_count();
    const auto player=fixture.runtime->document().createPlayer();ASSERT_NE(player,NoEntity)<<fixture.runtime->document().status();
    EXPECT_EQ(native.scene().entities().size(),before+2);EXPECT_EQ(native.history().undo_count(),history+1);
    EXPECT_EQ(fixture.runtime->document().history().size(),0u);EXPECT_FALSE(fixture.runtime->document().dirty());
    const auto* settings=native.scene().engine_settings();ASSERT_NE(settings,nullptr);const auto player_uuid=settings->player_uuid;
    ASSERT_FALSE(player_uuid.empty());const auto player_native=native.scene().entity_for_uuid(player_uuid);ASSERT_NE(player_native,quader::kInvalidEntityId);
    EXPECT_NE(native.scene().engine_components(player_native),nullptr);EXPECT_EQ(native.scene().uuid(box.value().entity),floor_uuid);
    ASSERT_TRUE(native.history().undo().ok);EXPECT_EQ(native.scene().entities().size(),before);EXPECT_EQ(native.scene().entity_for_uuid(player_uuid),quader::kInvalidEntityId);
    ASSERT_TRUE(native.history().redo().ok);EXPECT_EQ(native.scene().entities().size(),before+2);EXPECT_EQ(native.scene().entity_for_uuid(player_uuid),player_native);
    fixture.authoring->synchronize();const auto& projection=fixture.runtime->nativeProjection().snapshot();
    EXPECT_EQ(projection.records.size(),native.scene().entities().size());EXPECT_NE(projection.properties.playerCharacter,NoEntity);
    EXPECT_TRUE(native.scene().mesh(box.value().entity));
    EXPECT_EQ(std::count_if(projection.records.begin(),projection.records.end(),[](const auto& record){return record.point.has_value();}),1);
}
TEST(NativeSceneAuthoring, SaveAndReopenRetainsGeometryLightPlayerAndCameraInOneQdr){
    Fixture fixture;ASSERT_TRUE(fixture.editor.modeling().create_box({.min={-2,-1,-2},.max={2,0,2}},"Floor").ok());
    fixture.authoring->synchronize();ASSERT_NE(fixture.runtime->document().createPointLight(),NoEntity);fixture.authoring->synchronize();
    ASSERT_NE(fixture.runtime->document().createPlayer(),NoEntity);fixture.authoring->synchronize();
    const auto count=fixture.native().scene().entities().size();const auto player_uuid=fixture.native().scene().engine_settings()->player_uuid;
    const auto scene_id=fixture.project.active_scene()->session_id;
    const auto path=quader::ProjectRelativePath::parse_portable("Scenes/Level.qdr");ASSERT_TRUE(path.ok());
    ASSERT_TRUE(fixture.editor.scene_documents().save_as(scene_id,path.value()).ok());
    EXPECT_FALSE(fixture.native().dirty());
    for(const auto& file:std::filesystem::recursive_directory_iterator(fixture.root))EXPECT_NE(file.path().extension(),".crimson-scene");
    ASSERT_TRUE(fixture.editor.scene_documents().close_active().ok());
    const auto reopened=fixture.editor.scene_documents().open(path.value());
    ASSERT_TRUE(reopened.ok())<<reopened.error_message();
    fixture.authoring->bind(fixture.project.active_scene()->session.get(),std::string(fixture.project.active_scene()->scene_id.str()));
    EXPECT_EQ(fixture.native().scene().entities().size(),count);ASSERT_NE(fixture.native().scene().engine_settings(),nullptr);
    EXPECT_EQ(fixture.native().scene().engine_settings()->player_uuid,player_uuid);
    EXPECT_NE(fixture.runtime->document().scene().properties().playerCharacter,NoEntity);
    EXPECT_EQ(fixture.runtime->document().scene().records().size(),count);
    EXPECT_EQ(fixture.runtime->nativeProjection().snapshot().records.size(),count);
    ASSERT_TRUE(fixture.runtime->play())<<fixture.runtime->status();EXPECT_EQ(fixture.runtime->runtime().document().scene().records().size(),count);
    EXPECT_TRUE(resolveGameCamera(fixture.runtime->runtime().presentationScene()).has_value());
    const auto saved_revision=fixture.native().scene().content_revision();fixture.runtime->update(1./30.,true);fixture.runtime->stop();
    EXPECT_EQ(fixture.native().scene().content_revision(),saved_revision);EXPECT_FALSE(fixture.native().dirty());
}
TEST(NativeSceneAuthoring, ConsecutiveComponentEditsAndNativeUndoKeepOneEntityIdentity){
    Fixture fixture;const auto player=fixture.runtime->document().createPlayer();ASSERT_NE(player,NoEntity);
    auto record=fixture.runtime->document().scene().find(player);ASSERT_TRUE(record&&record->movement);record->movement->jumpHeight=2.f;
    ASSERT_TRUE(fixture.runtime->document().updateEntity(player,*record,"Set jump height"));
    EXPECT_EQ(fixture.native().scene().entities().size(),2u);EXPECT_EQ(fixture.native().history().undo_count(),2u);
    fixture.authoring->synchronize();const auto mapped=fixture.runtime->document().scene().properties().playerCharacter;
    ASSERT_TRUE(fixture.runtime->document().scene().find(mapped)->movement);EXPECT_EQ(fixture.runtime->document().scene().find(mapped)->movement->jumpHeight,2.f);
    ASSERT_TRUE(fixture.native().history().undo().ok);fixture.authoring->synchronize();EXPECT_EQ(fixture.runtime->document().scene().find(mapped)->movement->jumpHeight,1.1f);
    const auto count=fixture.native().history().undo_count();const auto native_player=fixture.native().scene().entity_for_uuid(fixture.native().scene().engine_settings()->player_uuid);
    ASSERT_TRUE(fixture.editor.scene().rename(native_player,"Renamed Player").ok());
    record=fixture.runtime->document().scene().find(mapped);record->movement->jumpHeight=3.f;
    EXPECT_FALSE(fixture.runtime->document().updateEntity(mapped,*record,"Stale edit"));
    EXPECT_EQ(fixture.native().history().undo_count(),count+1);EXPECT_EQ(fixture.native().scene().name(native_player)->name,"Renamed Player");
    fixture.authoring->synchronize();EXPECT_EQ(fixture.runtime->document().scene().find(mapped)->movement->jumpHeight,1.1f);
}
}
