#include "integration/LevelSession.h"
#include "runtime/PlaySession.h"
#include "assets/ModelAsset.h"
#include "ragdoll_fixture.h"
#include "scene/scene_document.hpp"
#include <gtest/gtest.h>

namespace {
using namespace crimson;
using namespace crimson::integration;
std::shared_ptr<const ModelingWorldSnapshot> publication(float height,std::uint64_t generation){
    auto result=std::make_shared<ModelingWorldSnapshot>();result->source_session=51;result->generation=generation;
    auto world=std::make_shared<RenderWorldSnapshot>();world->shadowWorldId=77;result->render=world;
    EntityRecord record;record.name="Native modeler floor";record.local=math::translation({0,height,0});record.collider.emplace();record.collider->shape=ColliderShape::Trimesh;
    record.collider->mesh=makeCollisionMesh({{-2,0,-2},{-2,0,2},{2,0,2},{2,0,-2}},{0,1,2,0,2,3});result->collision_records.push_back(std::move(record));return result;
}
TEST(ModelingWorld, PlayFreezesNativeCollisionWithoutAuthoredWrites){
    ragdoll_test::Project project;LevelSession session(project.root);
    ASSERT_TRUE(session.document().edit("Empty authored level",[](auto& records,auto& properties){records.clear();properties={};}));
    const auto history=session.document().history().size();const auto revision=session.document().scene().revision();
    auto first=publication(0,1),second=publication(3,2);const auto first_mesh=first->collision_records.front().collider->mesh;
    session.setModelingWorld(first);ASSERT_TRUE(session.play())<<session.status();
    ASSERT_EQ(session.runtime().document().scene().records().size(),1u);
    EXPECT_EQ(session.runtime().document().scene().records().front().collider->mesh,first_mesh);
    auto hit=session.runtime().raycast({{0,10,0},{0,-1,0}},20);ASSERT_TRUE(hit);EXPECT_NEAR(hit->position[1],0,1e-5f);
    session.setModelingWorld(second);ASSERT_TRUE(session.play());
    hit=session.runtime().raycast({{0,10,0},{0,-1,0}},20);ASSERT_TRUE(hit);EXPECT_NEAR(hit->position[1],0,1e-5f);
    EXPECT_TRUE(session.document().scene().records().empty());EXPECT_EQ(session.document().history().size(),history);EXPECT_EQ(session.document().scene().revision(),revision);
    EXPECT_TRUE(session.runtime().document().transient());EXPECT_FALSE(session.runtime().document().dirty());
    session.stop();ASSERT_TRUE(session.play());hit=session.runtime().raycast({{0,10,0},{0,-1,0}},20);ASSERT_TRUE(hit);EXPECT_NEAR(hit->position[1],3,1e-5f);
    session.stop();session.setModelingWorld(second,false);EXPECT_FALSE(session.play());EXPECT_TRUE(session.playPending());EXPECT_FALSE(session.playing());
    session.setModelingWorld(second,true);ASSERT_TRUE(session.play());EXPECT_TRUE(session.playing());
}
TEST(ModelingWorld, FrameAppendRetainsBothPublicationsAndCanonicalResourcePointers){
    auto frame=std::make_shared<PreparedRenderFrame>();auto world=std::make_shared<RenderWorldSnapshot>();frame->world=world;
    auto engine_pin=std::make_shared<const int>(1),modeling_pin=std::make_shared<const int>(2);std::weak_ptr<const int> engine_weak=engine_pin,modeling_weak=modeling_pin;world->pinned=engine_pin;
    frame->views.resize(2);frame->views[0].pinned=engine_pin;frame->views[1].pinned=engine_pin;
    auto model=std::make_shared<const ModelAsset>();RenderModelInstance instance;instance.entityId=501;instance.model.asset=model;
    auto native=std::make_shared<ModelingWorldSnapshot>();native->generation=12;auto external=std::make_shared<RenderWorldSnapshot>();external->pinned=modeling_pin;external->shadowInstances.push_back(instance);external->points.emplace_back();native->render=external;
    auto combined=append_modeling_world(frame,native);ASSERT_NE(combined,frame);ASSERT_EQ(combined->world->shadowInstances.size(),1u);ASSERT_EQ(combined->world->points.size(),1u);
    for(const auto& view:combined->views){ASSERT_EQ(view.instances.size(),1u);EXPECT_EQ(view.instances.front().model.asset,model);EXPECT_EQ(view.instances.front().entityId,501u);}
    EXPECT_TRUE(frame->world->shadowInstances.empty());EXPECT_TRUE(frame->views.front().instances.empty());
    engine_pin.reset();modeling_pin.reset();frame.reset();world.reset();native.reset();external.reset();EXPECT_FALSE(engine_weak.expired());EXPECT_FALSE(modeling_weak.expired());combined.reset();EXPECT_TRUE(engine_weak.expired());EXPECT_TRUE(modeling_weak.expired());
}
TEST(ModelingWorld, RuntimeNativePoseUsesSameGeometryAndEditorFrameHasNoDuplicateMesh){
    auto frame=std::make_shared<PreparedRenderFrame>();frame->world=std::make_shared<RenderWorldSnapshot>();frame->views.resize(2);frame->views[0].identity.purpose=RenderViewPurpose::Scene;frame->views[1].identity.purpose=RenderViewPurpose::Game;
    auto source=std::make_shared<ModelingWorldSnapshot>();auto world=std::make_shared<RenderWorldSnapshot>();auto geometry=std::make_shared<const ModelAsset>();RenderModelInstance instance;instance.entityId=5;instance.sourceInstanceId=771;instance.model.asset=geometry;world->shadowInstances.push_back(instance);source->render=world;source->native_instances.push_back({0,"native-occurrence",math::translation({1,0,0}),{0}});
    auto bindings=std::make_shared<NativeSceneRenderBindings>();bindings->entities.emplace("native-occurrence",NativeEntityRenderBinding{900,Mat4{},math::translation({2,0,0}),true});
    const auto combined=append_modeling_world(frame,source,false,bindings,true);EXPECT_TRUE(combined->views.front().instances.empty());ASSERT_EQ(combined->views.back().instances.size(),1u);const auto& posed=combined->views.back().instances.front();EXPECT_EQ(posed.entityId,900u);EXPECT_EQ(posed.sourceInstanceId,771u);EXPECT_EQ(posed.model.asset,geometry);EXPECT_EQ(posed.model.transform,math::translation({3,0,0}));EXPECT_EQ(world->shadowInstances.front().entityId,5u);
}
TEST(ModelingWorld, NativeCollisionStaysOnItsProjectedEntityAndHierarchy){
    ragdoll_test::Project project;LevelSession session(project.root);quader::SceneDocument native;NativeSceneProjection codec;
    const auto owner=native.create_entity("Owner"),floor=native.create_entity("Native floor");ASSERT_TRUE(native.set_parent(floor,owner,false));ASSERT_TRUE(native.set_mesh(floor,quader::MeshComponent{42}));
    EntityRecord actor;actor.actor.emplace();ASSERT_TRUE(native.set_engine_components(owner,codec.engine_components(actor)));quader::TransformComponent transform;transform.location={0,2,0};ASSERT_TRUE(native.set_transform(owner,transform));ASSERT_TRUE(session.publishNativeScene(native,"collision-source",1))<<session.status();
    auto source=std::make_shared<ModelingWorldSnapshot>(*publication(2,1));source->native_collisions.push_back({0,native.uuid(floor),Mat4{}});session.setModelingWorld(source);const auto id=session.nativeProjection().entity_id(native.uuid(floor)),parent=session.nativeProjection().entity_id(native.uuid(owner));
    ASSERT_TRUE(session.play())<<session.status();ASSERT_EQ(session.runtime().document().scene().records().size(),native.entities().size());const auto collider=session.runtime().document().scene().find(id);ASSERT_TRUE(collider);EXPECT_EQ(collider->parent,parent);ASSERT_TRUE(collider->collider);EXPECT_EQ(collider->collider->mesh,source->collision_records.front().collider->mesh);EXPECT_FALSE(session.document().scene().find(id)->collider);const auto hit=session.runtime().raycast({{0,10,0},{0,-1,0}},20);ASSERT_TRUE(hit);EXPECT_NEAR(hit->position[1],2,1e-5f);
}

}
