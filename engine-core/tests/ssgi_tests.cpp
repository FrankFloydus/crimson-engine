#include "crimson-renderer/SsgiHistory.h"
#include "crimson-renderer/SsgiSurface.h"
#include <iostream>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace {
using namespace crimson;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void budgetsAndDimensions() {
    check(!effectiveSsgi(SsgiSettings{}),"SSGI enabled itself in a default environment.");
    check(SsgiSettings{}.bounces==1,"Existing scenes did not retain the original one-bounce default.");
    SsgiSettings settings;settings.enabled=true;check(effectiveSsgi(settings),"Enabled SSGI did not become effective.");
    settings.strength=0;check(!effectiveSsgi(settings),"Zero-strength SSGI retained GPU work.");
    for(const auto [quality,rays,steps]:{std::tuple{SsgiQuality::Low,2u,12u},std::tuple{SsgiQuality::Medium,4u,16u},std::tuple{SsgiQuality::High,8u,24u}}) {
        const auto budget=ssgiQualityBudget(quality);check(budget.rays==rays&&budget.steps==steps,"SSGI quality exceeded its bounded work budget.");
    }
    settings.quality=SsgiQuality::Custom;settings.rays=16;settings.steps=64;
    check(ssgiQualityBudget(settings).rays==16&&ssgiQualityBudget(settings).steps==64,"Custom SSGI controls did not reach the requested bounded work budget.");
    check(ssgi::halfExtent(0)==0&&ssgi::halfExtent(1)==1&&ssgi::halfExtent(2)==1&&ssgi::halfExtent(3)==2&&
        ssgi::halfExtent(1081)==541&&ssgi::halfExtent(65535)==32768,"Half-resolution SSGI lost odd edge pixels or overflowed.");
}
void settingsValidation() {
    for(const auto member:{&SsgiSettings::strength,&SsgiSettings::maxDistance,&SsgiSettings::thickness,&SsgiSettings::temporalStability,&SsgiSettings::rayBias,&SsgiSettings::denoiseStrength,&SsgiSettings::radianceClamp}) {
        for(float invalid:{std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN(),-1.f}) {
            SsgiSettings settings;settings.*member=invalid;check(!validSsgiSettings(settings),"Invalid SSGI setting entered GPU parameters.");
        }
    }
    SsgiSettings settings;settings.quality=static_cast<SsgiQuality>(-1);check(!validSsgiSettings(settings),"Unknown SSGI quality was accepted.");
    for(int invalid:{-1,0,5,std::numeric_limits<int>::max()}){settings={};settings.bounces=invalid;check(!validSsgiSettings(settings),"Unbounded SSGI path length entered the trace shader.");}
    for(const auto [member,low,high]:{std::tuple{&SsgiSettings::rays,1,16},std::tuple{&SsgiSettings::steps,8,64},std::tuple{&SsgiSettings::denoiseRadius,0,4}}) {
        for(int invalid:{low-1,high+1,std::numeric_limits<int>::max()}){settings={};settings.*member=invalid;check(!validSsgiSettings(settings),"Unbounded custom sampling or denoising entered the GPU.");}
    }
    settings={};settings.radianceClamp=0;settings.denoiseStrength=0;settings.denoiseRadius=0;
    check(validSsgiSettings(settings),"Disabling optional radiance clamping or spatial denoising was rejected.");
}
void packedDonorResponse() {
    for(bool reactive:{false,true})for(unsigned channel=0;channel<3;++channel)for(unsigned quantized=0;quantized<128;++quantized) {
        Vec3 response;response[channel]=float(quantized)/127.f;
        const auto packed=ssgi::packSourceSurface(response,reactive);
        check(packed<=ssgi::MaximumPackedSurface&&std::uint32_t(float(packed))==packed,"SSGI source response lost exact float32 packing.");
        check(ssgi::sourceSurfaceFlags(float(packed))==(reactive?2u:1u),"SSGI source color changed validity/reactive flags.");
        const auto restored=ssgi::sourceDiffuseResponse(float(packed));
        for(unsigned c=0;c<3;++c)check(std::abs(restored[c]-response[c])<.000001f,"SSGI source diffuse response changed channels during packing.");
    }
    const auto white=ssgi::packSourceSurface({1,1,1},true);
    check(white<(1u<<23)&&ssgi::sourceSurfaceFlags(float(white))==2&&ssgi::sourceDiffuseResponse(float(white))==Vec3{1,1,1},
        "Maximum donor response exceeded the exact geometry-alpha payload.");
    const auto bounded=ssgi::sourceDiffuseResponse(float(ssgi::packSourceSurface({-1,2,std::numeric_limits<float>::quiet_NaN()},false)));
    check(bounded==Vec3{0,1,0},"Nonphysical donor response escaped bounded throughput.");
    check(ssgi::sourceSurfaceFlags(0)==0&&ssgi::sourceDiffuseResponse(0)==Vec3{}&&
        ssgi::sourceSurfaceFlags(std::numeric_limits<float>::infinity())==0,"Invalid captured geometry became a valid GI donor.");
}
void historyOwnershipAndDiscontinuities() {
    const ssgi::HistoryIdentity identity{17,23,42};SsgiSettings settings;settings.enabled=true;
    Camera camera;camera.position={0,4,6};camera.target={0,0,0};
    const auto reusable=[&](std::uint64_t frame,const ssgi::HistoryIdentity& next,const Camera& view,const SsgiSettings& options) {
        return ssgi::reusableHistory(100,frame,identity,next,camera,view,settings,options);
    };
    check(reusable(101,identity,camera,settings),"A stable consecutive view discarded its history.");
    check(!reusable(100,identity,camera,settings)&&!reusable(102,identity,camera,settings),"Repeated or skipped frame reused an unrelated history.");
    check(!ssgi::consecutiveFrame(UINT64_MAX,0),"Invalid history frame wrapped into a valid source.");
    for(unsigned field=0;field<3;++field) {
        auto next=identity;if(field==0)++next.world;else if(field==1)++next.camera;else ++next.environment;
        check(!reusable(101,next,camera,settings),"World, camera or environment replacement retained old GI.");
    }
    auto nextCamera=camera;nextCamera.position=nextCamera.position+Vec3{.1f,0,0};nextCamera.target=nextCamera.target+Vec3{.1f,0,0};
    check(reusable(101,identity,nextCamera,settings),"Ordinary camera movement discarded reprojection history.");
    nextCamera.position=nextCamera.position+Vec3{10,0,0};nextCamera.target=nextCamera.target+Vec3{10,0,0};
    check(!reusable(101,identity,nextCamera,settings),"A camera teleport retained GI history.");
    nextCamera=camera;nextCamera.target=nextCamera.position-camera.forward()*10;
    check(!reusable(101,identity,nextCamera,settings),"A reversed camera retained GI history.");
    nextCamera=camera;nextCamera.fov+=1;check(!reusable(101,identity,nextCamera,settings),"Projection change retained GI history.");
    nextCamera=camera;nextCamera.nearPlane*=2;check(!reusable(101,identity,nextCamera,settings),"Depth-convention change retained GI history.");
    nextCamera=camera;nextCamera.position[0]=std::numeric_limits<float>::quiet_NaN();check(!reusable(101,identity,nextCamera,settings),"Nonfinite camera retained GI history.");
    auto nextSettings=settings;nextSettings.maxDistance+=1;check(!reusable(101,identity,camera,nextSettings),"GI tuning retained an incompatible history.");
    nextSettings=settings;nextSettings.quality=SsgiQuality::Low;check(!reusable(101,identity,camera,nextSettings),"GI ray budget change retained an incompatible history.");
    nextSettings=settings;nextSettings.thickness*=2;check(!reusable(101,identity,camera,nextSettings),"GI hit thickness change retained an incompatible history.");
    nextSettings=settings;nextSettings.temporalStability=.5f;check(!reusable(101,identity,camera,nextSettings),"Temporal tuning retained an incompatible history.");
    nextSettings=settings;nextSettings.bounces=4;check(!reusable(101,identity,camera,nextSettings),"GI path-length change retained an incompatible history.");
    for(const auto member:{&SsgiSettings::rayBias,&SsgiSettings::denoiseStrength,&SsgiSettings::radianceClamp}) {
        nextSettings=settings;nextSettings.*member*=.5f;check(!reusable(101,identity,camera,nextSettings),"GI sampling/denoise tuning retained incompatible radiance history.");
    }
    for(const auto member:{&SsgiSettings::rays,&SsgiSettings::steps,&SsgiSettings::denoiseRadius}) {
        nextSettings=settings;nextSettings.*member+=member==&SsgiSettings::denoiseRadius?-1:1;check(!reusable(101,identity,camera,nextSettings),"GI sampling/denoise budget retained incompatible radiance history.");
    }
    nextSettings=settings;nextSettings.strength=3;check(reusable(101,identity,camera,nextSettings),"Output-only GI strength adjustment discarded unscaled history.");
}
}
int main() {
    try{budgetsAndDimensions();settingsValidation();packedDonorResponse();historyOwnershipAndDiscontinuities();std::cout<<"SSGI bounded budgets, settings, donor packing, odd dimensions and temporal ownership passed.\n";return 0;}
    catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
