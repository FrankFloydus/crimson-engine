#include "render/crimson/crimson_rich_geometry.hpp"
#include <gtest/gtest.h>
#include "../thirdparty/crimson-renderer/src/modeling/ModelingPresentationMath.hpp"
#include <cmath>
#include <array>
#include <limits>
#include <memory>
#include <stop_token>
#include <stdexcept>

namespace quader {
namespace {
struct GeometryInput {
    std::vector<crimson::shared::Float3> positions{{0,0,0},{1,0,0},{1,1,0},{0,1,0}};
    std::vector<crimson::shared::VertexAttributes> attributes{4};
    std::vector<std::uint32_t> indices{0,1,2,0,2,3};
    std::vector<crimson::shared::DrawRange> ranges{{0,3,4},{3,3,9}};
    GeometryInput(){for(std::size_t index=0;index<attributes.size();++index){auto& value=attributes[index];value.normal={0,0,1};value.geometric_normal={0,0,1};value.uv0={positions[index][0],positions[index][1]};value.uv1={.25f,.75f};value.uv_carrier={1,0,0,1};value.rgba=0xff806020;value.interpolation_coordinates={1,2,3,4};value.interpolation_correction={5,6,7,8};value.interpolation_mode=2;}}
};
crimson::shared::MeshUpdate mesh(const std::shared_ptr<GeometryInput>& input){
    return {17,{input->positions,input,1},{input->attributes,input,2},
        {input->indices,input,3},{input->ranges,input,4}};
}

TEST(CrimsonRichGeometryTests, OriginalLodsRetainSlotsAndAllAnalyticAttributes){
    auto input=std::make_shared<GeometryInput>();
    auto geometry=build_crimson_rich_geometry(mesh(input),{},"immutable-model");
    ASSERT_EQ(geometry->uploads.size(),2u);ASSERT_EQ(geometry->source_slots,(std::vector<std::uint32_t>{4,9}));
    ASSERT_EQ(geometry->materials.size(),2u);ASSERT_EQ(geometry->primitives.size(),2u);ASSERT_TRUE(geometry->stream);
    EXPECT_EQ(geometry->stream->primitives.size(),2u);EXPECT_TRUE(geometry->primitives[0].vertices.empty());
    EXPECT_EQ(geometry->draws[0].primitive,0u);EXPECT_EQ(geometry->draws[1].primitive,1u);
    for(const auto& upload:geometry->uploads){
        EXPECT_TRUE(upload->validated);EXPECT_EQ(upload->lodIndex,0u);EXPECT_FALSE(upload->wideIndices);
        ASSERT_EQ(upload->geometry.indices.size(),3u);ASSERT_EQ(upload->indices16.size(),3u);
        for(const auto& vertex:upload->geometry.vertices){
            EXPECT_FLOAT_EQ(vertex.position[2],0);EXPECT_FLOAT_EQ(vertex.uv0[0],vertex.position[0]);EXPECT_FLOAT_EQ(vertex.uv0[1],vertex.position[1]);
            EXPECT_EQ(vertex.uv1,(crimson::Vec2{.25f,.75f}));EXPECT_EQ(vertex.uvInterpolationCoordinates,(crimson::Vec4{1,2,3,4}));
            EXPECT_EQ(vertex.uvInterpolationCorrection,(crimson::Vec4{5,6,7,8}));EXPECT_FLOAT_EQ(vertex.uvInterpolationMode,2);
            EXPECT_FLOAT_EQ(vertex.color[0],32.f/255);EXPECT_FLOAT_EQ(vertex.color[2],128.f/255);
            EXPECT_FLOAT_EQ(vertex.normal[2],1);EXPECT_TRUE(crimson::math::finite(vertex.tangent));
        }
    }
    input.reset();EXPECT_EQ(geometry->uploads[1]->geometry.indices.size(),3u);
}

TEST(CrimsonRichGeometryTests, OccurrenceMappingChangesSamplingAndPreservesPositions){
    auto input=std::make_shared<GeometryInput>();crimson::shared::Matrix4 sampling;
    sampling.values[0]=2;sampling.values[5]=3;sampling.values[12]=4;sampling.values[13]=5;
    const auto source=build_crimson_rich_geometry(mesh(input),{},"source");
    const auto mapped=build_crimson_rich_geometry(mesh(input),sampling,"mapped");
    ASSERT_EQ(source->uploads.size(),mapped->uploads.size());
    bool changed=false;
    for(std::size_t primitive=0;primitive<source->uploads.size();++primitive){
        const auto& before=source->uploads[primitive]->geometry;const auto& after=mapped->uploads[primitive]->geometry;
        EXPECT_EQ(before.indices,after.indices);ASSERT_EQ(before.vertices.size(),after.vertices.size());
        for(std::size_t index=0;index<before.vertices.size();++index){const auto& a=before.vertices[index];const auto& b=after.vertices[index];
            EXPECT_EQ(a.position,b.position);EXPECT_EQ(a.normal,b.normal);EXPECT_EQ(a.uvInterpolationCoordinates,b.uvInterpolationCoordinates);
            changed=changed||a.uv0!=b.uv0;EXPECT_TRUE(crimson::math::finite(b.tangent));
        }
    }
    EXPECT_TRUE(changed);EXPECT_FLOAT_EQ(input->attributes[0].uv0[0],0);
}

TEST(CrimsonRichGeometryTests, InvalidAndCanceledCandidatesDoNotMutateRetainedGeometry){
    auto input=std::make_shared<GeometryInput>();const auto retained=build_crimson_rich_geometry(mesh(input),{},"accepted");
    input->indices[0]=99;EXPECT_THROW(build_crimson_rich_geometry(mesh(input),{},"invalid"),std::runtime_error);
    EXPECT_EQ(retained->uploads[0]->geometry.indices[0],0u);input->indices[0]=0;
    input->positions[0][0]=std::numeric_limits<float>::quiet_NaN();EXPECT_THROW(build_crimson_rich_geometry(mesh(input),{},"nonfinite"),std::runtime_error);
    input->positions[0][0]=0;std::stop_source canceled;canceled.request_stop();
    EXPECT_THROW(build_crimson_rich_geometry(mesh(input),{},"canceled",canceled.get_token()),std::runtime_error);
    EXPECT_EQ(retained->uploads.size(),2u);
}
TEST(CrimsonRichGeometryTests, AdjacentEqualMaterialFacesCoalesceWithoutLosingGridProjection){
    auto input=std::make_shared<GeometryInput>();input->ranges[1].material_slot=4;
    const auto positions=input->positions;const auto attributes=input->attributes;
    input->positions={positions[0],positions[1],positions[2],positions[0],positions[2],positions[3]};
    input->attributes={attributes[0],attributes[1],attributes[2],attributes[0],attributes[2],attributes[3]};
    input->indices={0,1,2,3,4,5};
    const auto geometry=build_crimson_rich_geometry(mesh(input),{},"coalesced-faces");
    ASSERT_EQ(geometry->uploads.size(),1u);ASSERT_EQ(geometry->uploads[0]->geometry.indices.size(),6u);
    const auto& upload=*geometry->uploads[0];ASSERT_EQ(upload.modelingProjection.size(),upload.geometry.vertices.size());
    for(const auto& hint:upload.modelingProjection){EXPECT_FLOAT_EQ(hint[2],1);EXPECT_FLOAT_EQ(hint[3],1);}
    EXPECT_EQ(sizeof(crimson::ModelVertex),108u);
}
TEST(CrimsonRichGeometryTests, SharedFaceVerticesRetainOriginalSmoothProjectionFallback){
    auto input=std::make_shared<GeometryInput>();input->ranges[1].material_slot=4;
    const auto geometry=build_crimson_rich_geometry(mesh(input),{},"shared-face-corners");
    ASSERT_EQ(geometry->uploads.size(),1u);const auto& upload=*geometry->uploads[0];
    ASSERT_EQ(upload.modelingProjection.size(),4u);
    for(const auto& hint:upload.modelingProjection){EXPECT_FLOAT_EQ(hint[2],1);EXPECT_FLOAT_EQ(hint[3],0);}
}
TEST(CrimsonRichGeometryTests, EqualCountDistinctPrimitivesHaveDifferentResidentKeys){
    const auto input=std::make_shared<GeometryInput>();const auto geometry=build_crimson_rich_geometry(mesh(input),{},"distinct-primitives");
    ASSERT_EQ(geometry->stream->primitives.size(),2u);
    EXPECT_NE(geometry->stream->primitives[0].levels[0].key,geometry->stream->primitives[1].levels[0].key);
}
TEST(CrimsonModelingPresentationTests, FogMatchesIndependentLineIntegralAndPreservesCutoff){
    using namespace crimson;using namespace crimson::shared::detail;
    ModelingFog fog;fog.enabled=true;fog.height=1;fog.heightFalloff=.4f;fog.density=.07f;fog.distance=2;fog.color={.3f,.5f,.9f};
    const Vec3 ray{3,4,12};const float length=13;double integrated=0;
    // Independent midpoint quadrature along the full source height interval.
    for(int sample=0;sample<10000;++sample){const auto t=(sample+.5)/10000.;integrated+=fog.density*std::exp(-fog.heightFalloff*(2+t*4-fog.height))/10000.;}
    const double expected=1-std::exp(-integrated*(length-fog.distance));
    auto result=modeling_fog_sample(fog,ray,2,{1,1,1},.78125f);
    EXPECT_NEAR(result.opacity,expected,1.e-6);EXPECT_NEAR(result.radiance[1],.5*.78125*expected,1.e-6);
    fog.maximumOpacity=.1f;EXPECT_FLOAT_EQ(modeling_fog_sample(fog,ray,2,{1,1,1},1).opacity,.1f);
    fog.cutOffDistance=12;EXPECT_FLOAT_EQ(modeling_fog_sample(fog,ray,2,{1,1,1},1).opacity,0);
    fog.cutOffDistance=13;EXPECT_GT(modeling_fog_sample(fog,ray,2,{1,1,1},1).opacity,0);
    fog.distance=14;EXPECT_FLOAT_EQ(modeling_fog_sample(fog,ray,2,{1,1,1},1).opacity,0);
}
TEST(CrimsonModelingPresentationTests, FixedFilmicUsesSourceRec2020CurveAndLinearRetainsInRangeColor){
    using namespace crimson;using namespace crimson::shared::detail;
    const Vec3 color{.1f,.2f,.3f};const auto linear=modeling_tone_map(color,ModelingToneMapper::Linear);
    for(unsigned channel=0;channel<3;++channel)EXPECT_NEAR(linear[channel],color[channel],2.e-6);
    const auto filmic=modeling_tone_map({.18f,.18f,.18f},ModelingToneMapper::Filmic);
    for(const auto channel:filmic)EXPECT_NEAR(channel,.2668989f,2.e-4);
    for(const auto mode:{ModelingToneMapper::ACES,ModelingToneMapper::Filmic,ModelingToneMapper::AgX}){
        const auto low=modeling_tone_map({.03f,.03f,.03f},mode),high=modeling_tone_map({8,8,8},mode);
        EXPECT_TRUE(math::finite(low));EXPECT_TRUE(math::finite(high));for(unsigned channel=0;channel<3;++channel)EXPECT_GT(high[channel],low[channel]);
    }
}

}
}
