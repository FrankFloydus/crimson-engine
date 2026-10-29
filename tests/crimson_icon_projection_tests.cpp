#include "render/crimson/crimson_editor_projection.hpp"
#include "render/overlay/overlay_icon_image.hpp"
#include "render/render_scene_snapshot.hpp"
#include "viewport/viewport_settings.hpp"
#include <gtest/gtest.h>
#include <algorithm>

namespace quader {
namespace {
using crimson::shared::EditorPrimitive;
auto icon(const crimson::shared::EditorFrame& frame){return std::ranges::find(frame.batches,EditorPrimitive::Icon,&crimson::shared::EditorBatch::primitive);}

TEST(CrimsonIconProjectionTests, OriginalArtworkKeepsAlphaPixelSizeCropAndIndependentStreamOwners){
    ViewportOverlayCamera camera;camera.eye={0,0,10};camera.viewport_size={800,600};camera.projection=ViewportOverlayProjection::Orthographic;camera.orthographic_size=20;
    ViewportOverlayFrame source;source.icons.push_back({.position={0,0,0},.color=rgba8(255,255,255,180),.size_pixels=28});
    CrimsonEditorProjection projection;projection.submit_dynamic_overlay(0,4,source,camera);
    const auto first=projection.frame(4,{}, {60,80,800,600},{1024,1024},{},true,1);ASSERT_TRUE(first);
    const auto batch=icon(*first);ASSERT_NE(batch,first->batches.end());ASSERT_EQ(batch->geometry.positions.values.size(),4u);ASSERT_EQ(batch->geometry.indices.values.size(),6u);
    const auto& artwork=overlay_icon_image(ViewportOverlayIconKind::PointLight);
    EXPECT_EQ(batch->image.rgba8.pin,artwork.rgba8);EXPECT_EQ(batch->image.extent.width,1024u);EXPECT_TRUE(batch->image.srgb);
    ASSERT_EQ(batch->image.rgba8.values.size(),1024u*1024u*4u);
    bool transparent=false,partial=false,opaque=false;for(std::size_t at=3;at<batch->image.rgba8.values.size();at+=4){const auto alpha=batch->image.rgba8.values[at];transparent|=alpha==0;partial|=alpha>0&&alpha<255;opaque|=alpha==255;}
    EXPECT_TRUE(transparent&&partial&&opaque);
    const auto& positions=batch->geometry.positions.values;
    EXPECT_NEAR((positions[1].position[0]-positions[0].position[0])*.5f*800,28,1.e-4f);
    EXPECT_NEAR((positions[2].position[1]-positions[1].position[1])*.5f*600,28,1.e-4f);
    EXPECT_FLOAT_EQ(batch->world_from_local.values[0],800.f/1024);EXPECT_FLOAT_EQ(batch->world_from_local.values[5],600.f/1024);
    EXPECT_FLOAT_EQ(batch->world_from_local.values[12],920.f/1024-1);EXPECT_FLOAT_EQ(batch->world_from_local.values[13],1-760.f/1024);
    EXPECT_EQ(batch->geometry.vertices.values[0].uv,(crimson::shared::Float2{0,0}));EXPECT_EQ(batch->geometry.vertices.values[2].uv,(crimson::shared::Float2{1,1}));
    EXPECT_EQ(batch->geometry.vertices.values[0].rgba>>24,180u);EXPECT_FALSE(batch->depth_write);
    const auto before=projection.counters();camera.eye.x=2;projection.update_overlay_cameras(0,4,camera);
    const auto moved=projection.frame(4,{}, {60,80,800,600},{1024,1024},{},true,1);ASSERT_TRUE(moved);const auto next=icon(*moved);ASSERT_NE(next,moved->batches.end());
    EXPECT_NE(next->geometry.positions.pin,batch->geometry.positions.pin);EXPECT_EQ(next->geometry.vertices.pin,batch->geometry.vertices.pin);EXPECT_EQ(next->geometry.indices.pin,batch->geometry.indices.pin);EXPECT_EQ(next->image.rgba8.pin,batch->image.rgba8.pin);
    EXPECT_EQ(projection.counters().attribute_packs,before.attribute_packs);EXPECT_EQ(projection.counters().index_packs,before.index_packs);
    EXPECT_EQ(projection.frame(4,{}, {60,80,800,600},{1024,1024},{},true,1),moved);
    const auto hidden=projection.frame(4,{}, {60,80,800,600},{1024,1024},{},false,1);ASSERT_TRUE(hidden);EXPECT_EQ(icon(*hidden),hidden->batches.end());
}

TEST(CrimsonIconProjectionTests, PointPreviewSuppressesItsOriginalAndSpotKeepsItsSourceDirectionGizmo){
    RenderOverlaySelectionSnapshot scene;scene.scene_revision=1;scene.point_light_gizmos.push_back({.entity=701,.type=LightType::Point});
    auto preview=std::make_shared<RenderToolPreviewSceneSnapshot>();preview->revision=2;preview->suppression.entities={701};
    RenderToolPreviewLightRecord point;point.source_entity=701;point.light.type=LightType::Point;point.suppress_source=true;point.instance_transforms.resize(1);point.instance_transforms[0].values[12]=5;preview->lights.push_back(point);
    auto spot=point;spot.source_entity=702;spot.light.type=LightType::Spot;spot.instance_transforms[0].values[12]=8;preview->lights.push_back(spot);
    CrimsonEditorProjection projection;projection.set_preview(preview);ViewportOverlaySubmissionRequest request;request.viewport_index=0;request.layer_mask=4;request.camera.eye={0,0,10};request.camera.viewport_size={800,600};request.camera.projection=ViewportOverlayProjection::Orthographic;request.camera.orthographic_size=20;request.overlays_visible=true;request.scene_overlay_requested=true;request.point_light_gizmos_visible=true;request.selection_overlays_visible=false;request.selection_dimensions_visible=false;request.scene_overlay_snapshot=&scene;
    const auto settings=ViewportSettings::defaults();projection.submit(request,settings);const auto result=projection.frame(4,{}, {0,0,800,600},{800,600},{},true,1);ASSERT_TRUE(result);
    unsigned icons=0,points=0,ribbons=0;for(const auto& batch:result->batches){if(batch.primitive==EditorPrimitive::Icon){++icons;ASSERT_EQ(batch.geometry.positions.values.size(),4u);const auto center=(batch.geometry.positions.values[0].position[0]+batch.geometry.positions.values[2].position[0])*.5f;EXPECT_GT(center,.3f);}points+=batch.primitive==EditorPrimitive::Point;ribbons+=batch.primitive==EditorPrimitive::Ribbon;}
    EXPECT_EQ(icons,1u);EXPECT_GT(points,0u);EXPECT_GT(ribbons,0u);
    projection.set_preview({});projection.submit(request,settings);const auto restored=projection.frame(4,{}, {0,0,800,600},{800,600},{},true,2);ASSERT_TRUE(restored);const auto original=icon(*restored);ASSERT_NE(original,restored->batches.end());EXPECT_NEAR(original->geometry.positions.values[0].position[0]+original->geometry.positions.values[2].position[0],0,1.e-6f);
    request.point_light_gizmos_visible=false;projection.submit(request,settings);const auto disabled=projection.frame(4,{}, {0,0,800,600},{800,600},{},true,3);ASSERT_TRUE(disabled);EXPECT_EQ(icon(*disabled),disabled->batches.end());
}
}
}
