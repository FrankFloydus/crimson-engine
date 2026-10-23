#include <crimson/renderer/forward/Ssr.h>
#include <array>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void settings(){
    SsrSettings value;
    check(!value.enabled&&value.strength==1&&value.quality==SsrQuality::Medium&&value.maxDistance==50&&value.depthTolerance==.05f&&
        value.maxRoughness==.6f&&value.bias==.002f&&validSsrSettings(value)&&!effectiveSsr(value),"SSR defaults are not valid, dormant authored settings.");
    value.enabled=true;check(effectiveSsr(value),"Enabled SSR is not scheduled.");
    value.strength=0;check(validSsrSettings(value)&&!effectiveSsr(value),"Zero SSR strength still schedules effect work.");
    value.strength=1;value.maxRoughness=0;check(validSsrSettings(value)&&!effectiveSsr(value),"Zero SSR receiver range still schedules work.");
    check(ssrIterationBudget(SsrQuality::Low)==64&&ssrIterationBudget(SsrQuality::Medium)==128&&ssrIterationBudget(SsrQuality::High)==256,
        "SSR quality no longer maps to bounded deterministic hierarchy traversal.");
    check(ssrRayCount(SsrQuality::Low)==1&&ssrRayCount(SsrQuality::Medium)==4&&ssrRayCount(SsrQuality::High)==12,
        "SSR quality lost its bounded angular rough-reflection sampling.");
    value=SsrSettings{};value.quality=static_cast<SsrQuality>(99);check(!validSsrSettings(value),"SSR accepted an invalid quality enum.");
    for(auto member:{&SsrSettings::strength,&SsrSettings::maxDistance,&SsrSettings::depthTolerance,&SsrSettings::maxRoughness,&SsrSettings::bias}){
        for(float invalid:{std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity(),-1.f}){
            value=SsrSettings{};value.*member=invalid;check(!validSsrSettings(value),"SSR accepted a negative or non-finite numeric setting.");
            check(validSsrSettings(sanitizedSsrSettings(value)),"SSR runtime sanitization retained an invalid numeric setting.");
        }
    }
    value=SsrSettings{};value.maxDistance=0;check(!validSsrSettings(value),"SSR accepted zero maximum ray distance.");
    value=SsrSettings{};value.depthTolerance=0;check(!validSsrSettings(value),"SSR accepted zero hit tolerance.");
    value=SsrSettings{};value.bias=0;check(validSsrSettings(value),"SSR rejected an explicitly zero ray bias.");
    value.strength=2;check(!validSsrSettings(value)&&sanitizedSsrSettings(value).strength==1,"SSR strength is not bounded to interpolation weights.");
    value=SsrSettings{};value.maxRoughness=2;check(!validSsrSettings(value)&&sanitizedSsrSettings(value).maxRoughness==1,"SSR accepted an invalid roughness cutoff.");
}
void allocation(){
    check(ssrMipCount(0,100)==0&&ssrAllocationBytes(0,100)==0,"Empty SSR dimensions allocate storage.");
    check(ssrMipCount(1,1)==1&&ssrAllocationBytes(1,1)==24,"One SSR pixel allocates nonexistent lower-mip scratch.");
    check(ssrMipCount(3,5)==3&&ssrAllocationBytes(3,5)==504,"Odd SSR extents lost floor-halved mip or independent scratch bytes.");
    check(ssrMipCount(1,65535)==16&&ssrMipCount(65535,1)==16&&ssrMipCount(65535,65535)==MaximumSsrMipLevels,
        "Wide/tall native chains lost trailing mips or overflowed supported mip capacity.");
    // Complete FP32 color (16 B) plus min/max depth (8 B) with each lower
    // level allocated again independently: fullsize24 + 2*(quarter24+pixel24).
    check(ssrAllocationBytes(4,4)==624,"SSR resident accounting omitted one chain or its independent scratch allocations.");
    check(ssrAllocationBytes(0,100,true)==0&&ssrAllocationBytes(1,1,true)==88,
        "SSR reconstruction allocated an empty view or omitted its exact full-resolution targets.");
    check(ssrAllocationBytes(4,4,true)==624+16*64&&ssrAllocationBytes(3,5,true)==504+15*64,
        "SSR reconstruction resource accounting lost raw radiance, receiver guides or detached filtering.");
    check(ssrAllocationBytes(65535,65535)>std::numeric_limits<std::uint32_t>::max(),"Large SSR allocations overflow 32-bit byte accounting.");
    check(SsrEmptyMinimum>1.0e20f&&std::isfinite(SsrEmptyMinimum),"SSR sky interval uses an infinite or ordinary geometry depth sentinel.");
}
}
int main(){try{settings();allocation();}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}std::cout<<"SSR settings, traversal budgets and resource allocation contracts passed.\n";return 0;}
