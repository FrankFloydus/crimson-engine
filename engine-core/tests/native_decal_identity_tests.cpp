#include "integration/LevelSession.h"
#include "integration/native_scene_authoring.hpp"
#include "integration/native_scene_projection.hpp"
#include "api/quader_editor_api.hpp"
#include "editor/session/project_session.hpp"
#include "editor/session/scene_session.hpp"
#include "io/qdr/qdr_document_reader.hpp"
#include "io/qdr/qdr_document_writer.hpp"
#include "render/native_instance_identity.hpp"
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <algorithm>

namespace crimson::integration {
namespace {
TEST(NativeDecalIdentity, CopiedInstanceRetainsExactIdenticalMemberAfterQdrReopen) {
    QTemporaryDir temporary;ASSERT_TRUE(temporary.isValid());
    const auto root=std::filesystem::path(temporary.path().toStdWString())/"Project";std::filesystem::create_directory(root);
    quader::ProjectSession project{std::filesystem::path(temporary.path().toStdWString())/"recents.json"};quader::QuaderEditorApi editor{project};
    project.set_resource_root(std::filesystem::path(QUADER_SOURCE_DIR)/"resources/modeling_assets");const auto created=editor.project().create(root);ASSERT_TRUE(created.ok())<<created.error_message();
    LevelSession runtime(root);NativeSceneAuthoring authoring(runtime,project);authoring.bind(project.active_scene()->session.get(),std::string(project.active_scene()->scene_id.str()));
    auto& session=*project.active_scene()->session;auto& native=session.scene();
    ASSERT_TRUE(editor.mode().set_workspace_mode(quader::QuaderWorkspaceMode::Modeling).ok());
    const auto first=editor.modeling().create_box({.min={-1,-1,-1},.max={1,1,1}},"First member");ASSERT_TRUE(first.ok())<<first.error_message();
    const auto second=editor.modeling().create_box({.min={-1,-1,-1},.max={1,1,1}},"Identical member");ASSERT_TRUE(second.ok())<<second.error_message();
    const auto definition=native.create_empty("Definition");ASSERT_TRUE(native.set_parent(first.value().entity,definition,false));ASSERT_TRUE(native.set_parent(second.value().entity,definition,false));
    ASSERT_TRUE(native.set_instance_definition(definition));const auto occurrence=native.create_empty("Occurrence");ASSERT_TRUE(native.set_instance_occurrence(occurrence,{.definition=definition}));authoring.synchronize();
    ASSERT_TRUE(editor.mode().set_workspace_mode(quader::QuaderWorkspaceMode::Default).ok());
    const auto receiver_uuid=native.uuid(occurrence),member_uuid=native.uuid(second.value().entity),other_member=native.uuid(first.value().entity);
    const auto* member_mesh=session.assets().mesh(native.mesh(second.value().entity)->mesh);ASSERT_NE(member_mesh,nullptr);ASSERT_FALSE(member_mesh->geometry().primitives.empty());const auto range=member_mesh->geometry().primitives.front();
    const auto& projection=runtime.nativeProjection();const auto receiver=projection.entity_id(receiver_uuid);const auto native_record=runtime.document().scene().find(receiver);ASSERT_TRUE(native_record&&native_record->nativeGeometry);
    EntityRecord decal;decal.id=10001;decal.name="Exact member decal";decal.decal.emplace();decal.decal->seed.receiver=receiver;
    decal.decal->seed.nativeSource=SceneDecalNativeSource{member_uuid,range.index_offset,range.index_count};decal.decal->seed.sourceInstanceId=quader::native_source_instance_id(receiver_uuid,member_uuid,range.index_offset,range.index_count);
    const auto envelope=projection.engine_components(decal);const auto source=native.create_empty("Exact member decal");ASSERT_TRUE(native.set_engine_components(source,envelope));
    EXPECT_EQ(envelope.text().find(member_uuid),std::string_view::npos);EXPECT_EQ(envelope.text().find("sourceInstance"),std::string_view::npos);
    ASSERT_TRUE(editor.scene().select({occurrence,source}).ok());const auto history=session.history().undo_count();
    const auto duplicated=editor.scene().duplicate_selected_hierarchy();ASSERT_TRUE(duplicated.ok())<<duplicated.error_message();ASSERT_EQ(duplicated.value().root_entities.size(),2u);
    const auto receiver_copy=duplicated.value().root_entities[0].id,decal_copy=duplicated.value().root_entities[1].id;const auto receiver_copy_uuid=native.uuid(receiver_copy),decal_copy_uuid=native.uuid(decal_copy);
    EXPECT_EQ(session.history().undo_count(),history+1);ASSERT_NE(native.engine_components(decal_copy),nullptr);
    const auto bytes=quader::QdrDocumentWriter::serialize_project_document_bytes(session.document());const auto reopened=quader::QdrDocumentReader::deserialize_project_document(bytes);ASSERT_TRUE(reopened.ok)<<reopened.message;
    NativeSceneProjection restored;const auto& snapshot=restored.prepare(reopened.document.scene(),newAssetId(),1,root);
    const auto copied_id=restored.entity_id(decal_copy_uuid);const auto found=std::find_if(snapshot.records.begin(),snapshot.records.end(),[&](const auto& value){return value.id==copied_id;});ASSERT_NE(found,snapshot.records.end());ASSERT_TRUE(found->decal&&found->decal->seed.nativeSource);
    const auto& seed=found->decal->seed;EXPECT_EQ(seed.receiver,restored.entity_id(receiver_copy_uuid));EXPECT_EQ(seed.nativeSource->memberUuid,member_uuid);EXPECT_EQ(seed.nativeSource->indexOffset,range.index_offset);EXPECT_EQ(seed.nativeSource->indexCount,range.index_count);
    EXPECT_EQ(seed.sourceInstanceId,quader::native_source_instance_id(receiver_copy_uuid,member_uuid,range.index_offset,range.index_count));
    EXPECT_NE(seed.sourceInstanceId,decal.decal->seed.sourceInstanceId);EXPECT_NE(seed.sourceInstanceId,quader::native_source_instance_id(receiver_copy_uuid,other_member,range.index_offset,range.index_count));
    ASSERT_TRUE(session.history().undo().ok);EXPECT_FALSE(native.exists(receiver_copy));ASSERT_TRUE(session.history().redo().ok);EXPECT_EQ(native.uuid(receiver_copy),receiver_copy_uuid);
}
}
}
