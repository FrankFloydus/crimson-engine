#include "crimson-renderer/ClusteredLighting.h"
#include "crimson-renderer/ShadowLightSelection.h"
#include "crimson-renderer/PointLightRecord.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <numbers>
#include <random>
#include <stdexcept>

namespace {
using namespace crimson;
namespace clusters=clustered_lighting;

void check(bool condition,const char* message) { if(!condition)throw std::runtime_error(message); }

Camera cameraAtOrigin() {
    Camera camera;camera.position={};camera.target={0,0,-1};camera.up={0,1,0};
    camera.fov=70;camera.fovAxis=CameraFovAxis::Vertical;camera.nearPlane=.1f;camera.farPlane=100;
    return camera;
}

bool contains(const clusters::Grid& grid,std::uint32_t cluster,std::uint32_t source) {
    const auto list=grid.lightsForCluster(cluster);
    return std::find(list.begin(),list.end(),source)!=list.end();
}

void structuralChecks(const clusters::Grid& grid,std::span<const PointLight> lights) {
    check(grid.ready(),"Cluster grid was not ready.");
    check(grid.headers.size()==std::size_t(grid.tilesX)*grid.tilesY*grid.depthSlices,"Cluster dimensions disagree with headers.");
    std::size_t offset=0;std::uint32_t maximum=0,occupied=0;
    for(std::uint32_t cluster=0;cluster<grid.headers.size();++cluster) {
        const auto& header=grid.headers[cluster];
        check(header.offset==offset,"Cluster lists are not compact.");
        const auto list=grid.lightsForCluster(cluster);
        check(list.size()==header.count,"Cluster span has the wrong length.");
        for(std::size_t i=0;i<list.size();++i) {
            check(list[i]<lights.size(),"Cluster list lost its source-light mapping.");
            check(lights[list[i]].enabled,"Disabled light entered a cluster.");
            if(i>0)check(list[i-1]<list[i],"Cluster lights must be unique and deterministic in source order.");
        }
        offset+=header.count;maximum=std::max(maximum,header.count);if(header.count)++occupied;
    }
    check(offset==grid.indices.size() && offset==grid.stats.lightReferences,"Cluster reference accounting disagrees.");
    check(maximum==grid.stats.maxLightsPerCluster && occupied==grid.stats.occupiedClusters,"Cluster occupancy accounting disagrees.");
}

void emptyDenseAndReuse() {
    auto camera=cameraAtOrigin();clusters::Grid grid;clusters::Config config;config.tileSize=16;config.depthSlices=12;
    check(clusters::build(grid,camera,97,61,{},config),"Empty lighting failed.");
    structuralChecks(grid,{});
    check(grid.tilesX==7 && grid.tilesY==4 && grid.indices.empty(),"Partial edge tiles or empty output failed.");
    check(grid.depthSlice(camera.nearPlane)==0 && grid.depthSlice(camera.farPlane)==11,"Near/far depth slices are wrong.");
    check(grid.clusterForPixelDepth(96.9f,60.9f,5)==grid.clusterIndex(6,3,grid.depthSlice(5)),"Top-left pixel addressing lost edge tiles.");
    std::vector<PointLight> lights(128);
    for(std::size_t i=0;i<lights.size();++i) {lights[i].position={0,0,-5};lights[i].radius=2;lights[i].entityId=100+i;}
    lights[5].enabled=false;lights[17].intensity=0;
    check(clusters::build(grid,camera,97,61,lights,config),"Dense lighting failed.");structuralChecks(grid,lights);
    const auto center=grid.clusterForPixelDepth(48.5f,30.5f,5);
    check(grid.lightsForCluster(center).size()==126 && contains(grid,center,127),"Dense cluster was truncated at a legacy light limit.");
    check(!contains(grid,center,5) && !contains(grid,center,17),"Inactive source filtering changed index correspondence.");
    const auto headers=grid.headers;const auto indices=grid.indices;
    const auto headerCapacity=grid.headers.capacity(),indexCapacity=grid.indices.capacity();
    check(clusters::build(grid,camera,97,61,lights,config),"Repeated lighting failed.");
    check(grid.headers.capacity()==headerCapacity && grid.indices.capacity()==indexCapacity,"Repeated frame lost reusable storage.");
    check(indices==grid.indices && headers.size()==grid.headers.size(),"Repeated frame became nondeterministic.");
    for(std::size_t i=0;i<headers.size();++i)
        check(headers[i].offset==grid.headers[i].offset && headers[i].count==grid.headers[i].count,"Repeated frame changed cluster headers.");
    for(auto& light:lights){light.nearFalloffRadius=4;light.specularStrength=0;}
    check(clusters::build(grid,camera,97,61,lights,config)&&indices==grid.indices,"Near falloff/specular response changed the light's spatial coverage.");
    for(std::size_t i=0;i<headers.size();++i)
        check(headers[i].offset==grid.headers[i].offset&&headers[i].count==grid.headers[i].count,"Near falloff/specular response changed cluster headers.");
    // Five-texel records end halfway through an 8192-wide row. The reservation
    // must include that padding before masks, also when the atlas grows wider.
    check(point_lighting::pointLightRecordRows(4096,8192)==3&&point_lighting::pointLightRecordRows(4096,16384)==2,
        "Light-record reservation failed to include wide-atlas row padding.");
    for(std::uint32_t width:{4,64,4096,8192,16384}) {
        const auto reserved=point_lighting::pointLightRecordRows(4096,width)*width;
        check(reserved>=4096*CC_POINT_LIGHT_RECORD_TEXELS&&reserved%width==0,"Light-record reservation can overwrite the first mask row.");
    }
    check(clusters::build(grid,camera,17,9,{},config),"Reuse after resize failed.");
    structuralChecks(grid,{});check(grid.indices.empty() && grid.stats.activeLights==0,"Resized/empty build retained stale references.");

    lights.resize(4096);for(auto& light:lights){light.enabled=true;light.intensity=1;light.position={0,0,-5};light.radius=2;}
    config={};check(clusters::build(grid,camera,8,8,lights,config),"Full 4096-light capacity failed.");
    structuralChecks(grid,lights);
    check(grid.lightsForCluster(grid.clusterForPixelDepth(4,4,5)).size()==4096 &&
        contains(grid,grid.clusterForPixelDepth(4,4,5),4095),"Dense 4096-light cluster lost a light.");
    lights.emplace_back();
    check(!clusters::build(grid,camera,8,8,lights,config) && grid.status==clusters::Status::LightCapacityExceeded,
        "The source-light boundary did not reject light 4097 explicitly.");
}

void capacityFailures() {
    auto camera=cameraAtOrigin();clusters::Grid grid;clusters::Config config;config.tileSize=16;config.depthSlices=4;
    std::vector<PointLight> lights(40);for(auto& light:lights){light.position={0,0,-5};light.radius=2;}
    check(clusters::build(grid,camera,64,64,lights,config),"Capacity fixture failed.");
    config.maxIndices=1;
    check(!clusters::build(grid,camera,64,64,lights,config) && grid.status==clusters::Status::IndexCapacityExceeded,"Reference overflow was not explicit.");
    check(grid.headers.empty() && grid.indices.empty() && grid.visibleLightIndices.empty(),"Overflow published truncated lighting.");
    config={};config.maxLights=39;
    check(!clusters::build(grid,camera,64,64,lights,config) && grid.status==clusters::Status::LightCapacityExceeded,"Source capacity overflow was not explicit.");
    config={};config.maxClusters=1;
    check(!clusters::build(grid,camera,64,64,lights,config) && grid.status==clusters::Status::ClusterCapacityExceeded,"Cluster capacity overflow was not explicit.");
    config={};config.tileSize=0;
    check(!clusters::build(grid,camera,64,64,lights,config) && grid.status==clusters::Status::InvalidConfiguration,"Zero tile size was accepted.");
    config={};
    check(!clusters::build(grid,camera,0,64,lights,config),"Empty viewport was accepted.");
    check(!clusters::build(grid,camera,std::numeric_limits<std::uint32_t>::max(),std::numeric_limits<std::uint32_t>::max(),lights,config) &&
        grid.status==clusters::Status::ClusterCapacityExceeded,"Viewport multiplication overflow was not bounded.");
    check(clusters::build(grid,camera,64,64,lights,config),"Grid could not recover after a failed build.");
    structuralChecks(grid,lights);
}

void clippingAndCameraInside() {
    auto camera=cameraAtOrigin();clusters::Grid grid;
    const float tanY=std::tan(camera.fov*std::numbers::pi_v<float>/360);
    std::vector<PointLight> lights(6);
    lights[0].position={0,0,0};lights[0].radius=5; // Camera inside a sphere.
    lights[1].position={tanY*5+.2f,0,-5};lights[1].radius=.5f; // Center outside right edge.
    lights[2].position={0,0,.1f};lights[2].radius=.3f; // Center behind camera, sphere crosses near.
    lights[3].position={0,0,10};lights[3].radius=1; // Entirely behind.
    lights[4].position={0,0,-110};lights[4].radius=1; // Beyond far.
    lights[5].position={100,0,-5};lights[5].radius=1; // Entirely outside side.
    check(clusters::build(grid,camera,257,257,lights),"Clipping fixture failed.");structuralChecks(grid,lights);
    check(contains(grid,grid.clusterForPixelDepth(0,0,.2f),0) &&
        contains(grid,grid.clusterForPixelDepth(256,256,.2f),0),"Camera-inside light failed at view corners.");
    check(contains(grid,grid.clusterForPixelDepth(256,128,5),1),"Offscreen-center light touching the view was culled.");
    check(contains(grid,grid.clusterForPixelDepth(128,128,.15f),2),"Near-plane intersection was culled.");
    for(auto source:{3u,4u,5u})check(std::find(grid.visibleLightIndices.begin(),grid.visibleLightIndices.end(),source)==grid.visibleLightIndices.end(),"Wholly out-of-view light survived broad culling.");
    check(grid.stats.lightReferences<std::uint64_t(grid.headers.size())*lights.size()/2,"Cluster assignment retained every light in every cell.");
}

void sampledConservativeOracle() {
    std::mt19937 random(0xC1A57E2u);std::uniform_real_distribution<float> unit(0,1),signedUnit(-1,1);
    clusters::Grid grid;std::size_t checked=0;
    // Independent oracle: choose points in world space inside each light sphere,
    // project them with Camera's real matrices and require the fragment's cell
    // to contain that light. This detects missing light coverage without
    // repeating the sphere/cluster-plane implementation.
    for(std::uint32_t fixture=0;fixture<24;++fixture) {
        auto camera=cameraAtOrigin();
        camera.position={signedUnit(random)*30,signedUnit(random)*30,signedUnit(random)*30};
        camera.target=camera.position+math::normalized(Vec3{signedUnit(random),signedUnit(random),signedUnit(random)});
        camera.up=math::normalized(Vec3{signedUnit(random),1,signedUnit(random)});
        camera.fov=fixture%3==0?12.f:(fixture%3==1?75.f:145.f);
        camera.fovAxis=fixture%2?CameraFovAxis::Vertical:CameraFovAxis::ShorterDimension;
        camera.nearPlane=fixture%3==0?.001f:(fixture%3==1?.4f:3.f);
        camera.farPlane=fixture%3==0?5000.f:(fixture%3==1?170.f:21.f);
        const std::uint32_t width=fixture%2?193:73,height=fixture%2?67:211;
        const auto view=camera.viewMatrix(),projection=camera.projectionMatrix(float(width)/height,ClipDepth::ZeroToOne);
        const auto inverseView=math::inverse(view);check(inverseView.has_value(),"Oracle camera inverse failed.");
        std::vector<PointLight> lights(100);
        for(std::size_t index=0;index<lights.size();++index) {
            auto& light=lights[index];
            const float distance=std::exp2(std::log2(camera.nearPlane)+(std::log2(camera.farPlane/camera.nearPlane))*unit(random));
            const auto inView=Vec3{signedUnit(random)*distance*2,signedUnit(random)*distance*2,-distance};
            light.position=math::transformPoint(*inverseView,inView);
            light.radius=std::max(.01f,distance*(.05f+unit(random)*1.2f));
            light.rangeFalloffExponent=index%3==0?1.f:index%3==1?.25f:8.f;
            light.edgeSoftness=index%2?.05f:1.f;light.sourceRadius=index%4?0.f:3.f;
        }
        clusters::Config config;config.tileSize=fixture%2?16:32;config.depthSlices=fixture%3==0?8:24;
        check(clusters::build(grid,camera,width,height,lights,config),"Randomized oracle build failed.");structuralChecks(grid,lights);
        for(std::uint32_t source=0;source<lights.size();++source) {
            const auto& light=lights[source];
            for(std::uint32_t sample=0;sample<80;++sample) {
                const auto direction=math::normalized(Vec3{signedUnit(random),signedUnit(random),signedUnit(random)});
                const auto point=light.position+direction*(light.radius*std::cbrt(unit(random)));
                const auto inView=math::transformPoint(view,point);const float depth=-inView[2];
                if(depth<grid.nearPlane || depth>grid.farPlane)continue;
                const auto clip=math::transformHomogeneous(projection,Vec4{inView[0],inView[1],inView[2],1});
                if(clip[3]<=0)continue;
                const float x=(clip[0]/clip[3]+1)*.5f*width,y=(1-clip[1]/clip[3])*.5f*height;
                if(x<0 || y<0 || x>=width || y>=height)continue;
                ++checked;
                check(contains(grid,grid.clusterForPixelDepth(x,y,depth),source),"A visible sample inside a light sphere lost that light.");
            }
        }
    }
    check(checked>5000,"Randomized oracle did not cover enough visible samples.");
}

void responseCoverage() {
    auto camera=cameraAtOrigin();clusters::Grid ordinaryGrid,tunedGrid;
    PointLight point;point.position={0,0,-8};point.radius=8;
    check(clusters::build(ordinaryGrid,camera,256,256,std::span{&point,1}),"Finite point coverage fixture failed.");
    point.nearFalloffRadius=1.5f;point.specularStrength=0;point.rangeFalloffExponent=.25f;
    point.edgeSoftness=.05f;point.sourceRadius=100000;
    check(clusters::build(tunedGrid,camera,256,256,std::span{&point,1}),"Tuned finite point coverage fixture failed.");
    check(tunedGrid.indices==ordinaryGrid.indices&&tunedGrid.headers.size()==ordinaryGrid.headers.size(),
        "Point response or source radius changed its finite illumination sphere.");
    for(std::size_t i=0;i<tunedGrid.headers.size();++i)
        check(tunedGrid.headers[i].offset==ordinaryGrid.headers[i].offset&&tunedGrid.headers[i].count==ordinaryGrid.headers[i].count,
            "Point response changed conservative spatial coverage.");
    check(contains(tunedGrid,tunedGrid.clusterForPixelDepth(128,128,8),0),"Finite point missed its source center.");
    point.position[1]=40;
    check(clusters::build(tunedGrid,camera,256,256,std::span{&point,1})&&tunedGrid.indices.empty(),
        "Moving a finite point light did not clear its old coverage.");
}

void shadowSelectionChecks() {
    std::vector<PointLight> lights(96);for(std::size_t i=0;i<lights.size();++i) {
        lights[i].castShadows=true;lights[i].entityId=i+100;
    }
    for(std::size_t i=0;i<24;++i)lights[i].castShadows=false;
    lights[24].enabled=false;lights[25].intensity=0;lights[26].radius=0;
    lights[27].radius=std::numeric_limits<float>::infinity();
    lights[28].position[1]=std::numeric_limits<float>::quiet_NaN();
    lights[29].intensity=std::numeric_limits<float>::infinity();
    lights[30].entityId=0; // Renderer fixtures preserve an index-derived identity.
    const auto selection=selectPointShadowLights(lights);
    check(selection.count==16 && selection.eligibleCount==66 && selection.unadmittedCount==50,"Shadow budget accounting is incorrect.");
    for(std::uint32_t slot=0;slot<16;++slot) {
        check(selection.sourceIndices[slot]==30+slot && selection.slotForSource(30+slot)==static_cast<std::int32_t>(slot),"Shadow compaction lost original source correspondence.");
    }
    check(selection.slotForSource(0)==-1 && selection.slotForSource(46)==-1,"Unallocated light received a shadow slot.");
    lights[30].castShadows=false;
    const auto changed=selectPointShadowLights(lights);
    check(changed.sourceIndices[0]==31 && changed.sourceIndices[15]==46,"Shadow selection retained a disabled allocation or lost deterministic order.");
    check(selectPointShadowLights({}).count==0,"Empty shadow selection was not empty.");
}

void shadowAdmissionPriorityChecks() {
    std::vector<PointLight> lights(128);
    for(std::size_t index=0;index<lights.size();++index) {
        auto& light=lights[index];light.castShadows=true;light.entityId=1000+index;
        light.position={100.f+float(index),0,0};light.radius=1;
    }
    lights[100].position={.1f,0,0};lights[127].position={.2f,0,0};
    const auto legacy=selectPointShadowLights(lights);
    const auto prioritized=selectPointShadowLights(lights,Vec3{});
    check(legacy.sourceIndices.front()==0 && legacy.slotForSource(100)==-1,"Legacy shadow admission unexpectedly changed without a priority origin.");
    check(prioritized.count==16 && prioritized.eligibleCount==128 && prioritized.unadmittedCount==112,
        "Prioritized admission changed eligibility or shadow-residency accounting.");
    check(prioritized.sourceIndices[0]==100 && prioritized.sourceIndices[1]==127 && prioritized.slotForSource(15)==-1,
        "Nearby lights beyond source index one hundred lost residency to the first sixteen distant lights.");
    std::array<std::uint64_t,PointShadowLightBudget> expected{};
    for(std::size_t slot=0;slot<expected.size();++slot)expected[slot]=lights[prioritized.sourceIndices[slot]].entityId;
    std::mt19937 random(0x41444d49);
    for(unsigned iteration=0;iteration<40;++iteration) {
        std::shuffle(lights.begin(),lights.end(),random);
        const auto reordered=selectPointShadowLights(lights,Vec3{});
        for(std::size_t slot=0;slot<expected.size();++slot)
            check(lights[reordered.sourceIndices[slot]].entityId==expected[slot],"Source reordering changed prioritized persistent shadow identities.");
    }

    // A wider influence at the same normalized distance has equal priority;
    // persistent IDs resolve exact ties instead of extraction/atlas positions.
    lights.resize(3);
    for(auto& light:lights){light=PointLight{};light.castShadows=true;}
    lights[0].entityId=30;lights[0].position={10,0,0};lights[0].radius=20;
    lights[1].entityId=20;lights[1].position={1,0,0};lights[1].radius=1;
    lights[2].entityId=10;lights[2].position={5,0,0};lights[2].radius=10;
    const auto normalized=selectPointShadowLights(lights,Vec3{});
    check(normalized.sourceIndices[0]==2 && normalized.sourceIndices[1]==0 && normalized.sourceIndices[2]==1,
        "Shadow admission lost normalized influence or persistent identity tie-breaking.");
    lights[0].intensity=0;lights[1].enabled=false;lights[2].castShadows=false;
    const auto filtered=selectPointShadowLights(lights,Vec3{});
    check(filtered.count==0 && filtered.eligibleCount==0,"Priority admission bypassed ordinary light eligibility.");

    lights.resize(40);
    for(std::size_t index=0;index<lights.size();++index) {
        lights[index]=PointLight{};lights[index].entityId=100+index;lights[index].castShadows=true;
        lights[index].position={float(lights.size()-index),0,0};
    }
    const auto original=selectPointShadowLights(lights);
    for(const auto invalid:{Vec3{std::numeric_limits<float>::quiet_NaN(),0,0},Vec3{0,std::numeric_limits<float>::infinity(),0}}) {
        const auto fallback=selectPointShadowLights(lights,invalid);
        check(fallback.sourceIndices==original.sourceIndices && fallback.count==original.count && fallback.eligibleCount==original.eligibleCount,
            "A nonfinite priority origin produced NaN ordering instead of legacy fallback.");
    }
    // Extreme valid float inputs must not turn the score into float infinity
    // or NaN and discard a nearby influence merely because earlier ones overflow.
    lights.resize(2);lights[0].position={std::numeric_limits<float>::max(),0,0};lights[0].radius=std::numeric_limits<float>::min();
    lights[1].position={-std::numeric_limits<float>::max(),0,0};lights[1].radius=std::numeric_limits<float>::min();
    const auto extreme=selectPointShadowLights(lights,Vec3{-std::numeric_limits<float>::max(),0,0});
    check(extreme.count==2 && extreme.sourceIndices[0]==1,"Finite extreme light values corrupted prioritized shadow ordering.");
    check(selectPointShadowLights({},Vec3{}).count==0,"An empty prioritized world acquired shadow residency.");
}
}

int main() {
    try {
        emptyDenseAndReuse();capacityFailures();clippingAndCameraInside();sampledConservativeOracle();responseCoverage();shadowSelectionChecks();shadowAdmissionPriorityChecks();
        std::cout<<"Clustered lighting conservative coverage, capacity, reuse, and shadow selection checks passed.\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
