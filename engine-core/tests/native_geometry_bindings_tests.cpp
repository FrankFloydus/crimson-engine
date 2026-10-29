#include "integration/native_geometry_sources.hpp"
#include "assets/AssetLibrary.h"
#include "decals/DecalCache.h"
#include "editor/DecalResources.h"
#include <QTemporaryDir>
#include <gtest/gtest.h>
#include <atomic>
#include <chrono>
#include <thread>

namespace crimson::integration {
namespace {
struct NativeFixture {
    std::shared_ptr<ModelPrimitive> original=std::make_shared<ModelPrimitive>();
    std::shared_ptr<ModelAsset> geometry=std::make_shared<ModelAsset>();
    std::shared_ptr<RenderWorldSnapshot> render=std::make_shared<RenderWorldSnapshot>();
    std::shared_ptr<ModelingWorldSnapshot> modeling=std::make_shared<ModelingWorldSnapshot>();
    std::shared_ptr<NativeSceneRenderBindings> bindings=std::make_shared<NativeSceneRenderBindings>();
    std::shared_ptr<std::atomic_uint> original_reads=std::make_shared<std::atomic_uint>();
    std::vector<ModelMaterialTextures> textures{1};
    std::string uuid=newAssetId();
    Scene scene;
    NativeFixture() {
        original->material=0;original->uvMask=1;original->vertices.resize(4);
        const std::array<Vec3,4> positions{{{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}}};
        for(std::size_t i=0;i<positions.size();++i){original->vertices[i].position=positions[i];original->vertices[i].normal={0,0,1};math::include(original->bounds,positions[i]);}
        original->indices={0,1,2,0,2,3};geometry->id="immutable-native-geometry";geometry->bounds=original->bounds;
        ModelPrimitive metadata;metadata.material=0;metadata.uvMask=1;metadata.bounds=original->bounds;metadata.sourceVertexCount=4;metadata.sourceIndexCount=6;
        geometry->primitives.push_back(metadata);geometry->draws.push_back({0});geometry->materials.push_back(defaultMaterialAsset().surface);
        EntityRecord entity;entity.id=71;entity.name="Same native mesh";entity.nativeGeometry=true;entity.local=math::translation({5,0,0})*math::scaling({-2,3,1});scene.apply({entity},{});
        NativeEntityRenderBinding binding;binding.entity=71;binding.native_geometry=true;binding.authored_world=binding.presented_world=entity.local;bindings->entities.emplace(uuid,binding);bindings->revision=11;
        for(std::uint64_t index=0;index<2;++index) {
            RenderModelInstance instance;instance.entityId=900+index;instance.sourceInstanceId=101+index;instance.model={geometry,entity.local};instance.materials=&geometry->materials;instance.textures=&textures;instance.resourceRevision=37;
            render->shadowInstances.push_back(instance);
            NativeModelingInstance native;native.index=index;native.entity_uuid=uuid;native.source_instance_id=instance.sourceInstanceId;native.source_slots={9};
            native.member_uuid=newAssetId();native.index_count=6;
            if(index)native.entity_from_local=math::translation({3,1,0})*math::scaling({.5f,2,1});
            native.original_lod=[source=original,reads=original_reads](std::uint32_t primitive,std::stop_token stop)->std::shared_ptr<const ModelPrimitive>{if(primitive||stop.stop_requested())return {};reads->fetch_add(1);return source;};
            modeling->native_instances.push_back(std::move(native));
        }
        modeling->generation=7;modeling->render=render;
    }
};

TEST(NativeGeometryBindings, SparseSlotOverridesRetainNativeGeometryAndImmutablePacketLifetime) {
    NativeFixture fixture;
    auto materials=std::make_shared<NativeEntityMaterialBindings>();NativeSlotMaterial replacement;replacement.source_slot=9;replacement.ready=replacement.current=true;
    replacement.surface.baseColor={.1f,.8f,.2f,1};replacement.asset=std::make_shared<const MaterialAsset>(defaultMaterialAsset());materials->slot_materials.push_back(replacement);
    fixture.bindings->entities.at(fixture.uuid).materials=materials;
    auto frame=std::make_shared<PreparedRenderFrame>();frame->world=std::make_shared<RenderWorldSnapshot>();frame->views.resize(1);
    const auto combined=append_modeling_world(frame,fixture.modeling,false,fixture.bindings);
    ASSERT_EQ(combined->world->shadowInstances.size(),2u);ASSERT_EQ(combined->views[0].instances.size(),2u);
    for(const auto& instance:combined->views[0].instances){EXPECT_EQ(instance.model.asset,fixture.geometry);EXPECT_EQ(instance.entityId,71u);ASSERT_NE(instance.materials,nullptr);EXPECT_EQ((*instance.materials)[0].baseColor,replacement.surface.baseColor);}
    EXPECT_NE(fixture.geometry->materials[0].baseColor,replacement.surface.baseColor);
    EXPECT_EQ(native_material_source_slot(*fixture.modeling,fixture.uuid,102,0),std::optional<std::uint32_t>{9});
    EXPECT_FALSE(native_material_source_slot(*fixture.modeling,fixture.uuid,999,0));
    std::weak_ptr<const NativeEntityMaterialBindings> retained=materials;materials.reset();fixture.bindings.reset();
    EXPECT_FALSE(retained.expired());EXPECT_EQ((*combined->views[0].instances[1].materials)[0].baseColor,replacement.surface.baseColor);
}

TEST(NativeGeometryBindings, NativeMaterialPreparationReusesWarmPacketsWithoutRegisteringGeometry) {
    QTemporaryDir temporary;ASSERT_TRUE(temporary.isValid());AssetLibrary assets(std::filesystem::path(temporary.path().toStdWString()));
    const auto material=assets.createMaterial("Native surface");NativeFixture fixture;
    auto record=*fixture.scene.find(71);record.modelMaterials.resize(10);record.modelMaterials[9].id=material->id;fixture.scene.apply({record},{});
    decorate_native_materials(*fixture.bindings,fixture.scene,assets);
    const auto packet=fixture.bindings->entities.at(fixture.uuid).materials;ASSERT_NE(packet,nullptr);ASSERT_EQ(packet->slot_materials.size(),1u);EXPECT_TRUE(packet->slot_materials[0].ready);
    EXPECT_EQ(refresh_native_materials(fixture.bindings,fixture.scene,assets),fixture.bindings);
    std::string diagnostic;EXPECT_TRUE(native_material_can_apply(*material,*fixture.geometry,0,assets,diagnostic))<<diagnostic;
    for(const auto& asset:storedAssetPaths(assets.projectRoot()))EXPECT_NE(asset.id,fixture.geometry->id);
    auto sources=native_geometry_sources(fixture.modeling,fixture.bindings,fixture.scene);ASSERT_NE(sources,nullptr);
    EXPECT_EQ(native_geometry_sources(fixture.modeling,fixture.bindings,fixture.scene,sources),sources);
    ASSERT_EQ(sources->meshes.size(),2u);EXPECT_EQ(sources->meshes[0].model,fixture.geometry);
}

TEST(NativeGeometryBindings, NativeDecalKeepsMemberIdentityAndEntityLocalSeedWithoutAssetImports) {
    QTemporaryDir temporary;ASSERT_TRUE(temporary.isValid());const std::filesystem::path root(temporary.path().toStdWString());
    ResourceManager resources;AssetLibrary assets(root,&resources);DecalResources decals(assets,resources);NativeFixture fixture;
    const auto sources=native_geometry_sources(fixture.modeling,fixture.bindings,fixture.scene);ASSERT_NE(sources,nullptr);decals.setExternalMeshes(sources);
    const auto point=math::transformPoint(fixture.bindings->entities.at(fixture.uuid).presented_world,{3,1,0});
    DecalPlacementResult placement;const auto deadline=std::chrono::steady_clock::now()+std::chrono::seconds{5};
    do{resources.pump();placement=decals.resolvePlacement(fixture.scene,71,0,0,37,point,102);ASSERT_FALSE(placement.failed)<<placement.diagnostic;if(placement.ready)break;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(std::chrono::steady_clock::now()<deadline);
    ASSERT_TRUE(placement.ready)<<placement.diagnostic;EXPECT_EQ(placement.seed.receiver,71u);EXPECT_EQ(placement.seed.sourceInstanceId,102u);EXPECT_TRUE(placement.seed.modelId.empty());
    ASSERT_TRUE(placement.seed.nativeSource);EXPECT_EQ(placement.seed.nativeSource->memberUuid,fixture.modeling->native_instances[1].member_uuid);EXPECT_EQ(placement.seed.nativeSource->indexOffset,0u);EXPECT_EQ(placement.seed.nativeSource->indexCount,6u);
    EXPECT_LT(math::length(placement.seed.localPoint-Vec3{3,1,0}),1.e-4f);EXPECT_TRUE(fixture.geometry->primitives[0].vertices.empty());EXPECT_GT(fixture.original_reads->load(),0u);
    EXPECT_TRUE(decals.resolvePlacement(fixture.scene,71,0,0,37,point,999).failed);
    decals::DecalSurfaceSnapshot snapshot;
    for(const auto& source:sources->meshes){DecalSourceResult prepared;do{resources.pump();prepared=decals.modelSurface(source.occurrence);if(prepared.current)break;std::this_thread::sleep_for(std::chrono::milliseconds{1});}while(std::chrono::steady_clock::now()<deadline);ASSERT_TRUE(prepared.current)<<prepared.diagnostic;
        for(const auto& draw:prepared.surface->draws){auto receiver=draw.surface;receiver.id.entity=71;receiver.id.sourceInstanceId=source.occurrence.sourceInstanceId;receiver.localToWorld=source.occurrence.world*receiver.localToWorld;snapshot.receivers.push_back(std::move(receiver));}}
    decals::DecalBuildRequest request;request.seed={{71,0,0,102},placement.seed.triangle,placement.seed.barycentric};request.frame=placement.frame;
    const auto patch=decals::buildDecalPatch(snapshot,request);ASSERT_TRUE(patch.usable())<<patch.message;ASSERT_FALSE(patch.chunks.empty());
    for(const auto& chunk:patch.chunks)EXPECT_EQ(chunk.receiver.sourceInstanceId,102u);
    const auto cache=decals::decalCachePath(root,123);ASSERT_TRUE(decals::writeDecalCache(cache,123,patch,request.limits));
    const auto restored=decals::readDecalCache(cache,123,snapshot,request);ASSERT_TRUE(restored);EXPECT_EQ(restored->chunks[0].receiver.sourceInstanceId,102u);
    for(const auto& stored:storedAssetPaths(root))EXPECT_NE(stored.id,fixture.geometry->id);
    resources.stop();
}
}
}
