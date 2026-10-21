#include "crimson-renderer/Environment.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
constexpr double Pi=3.14159265358979323846;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void near(double actual,double expected,double tolerance=1e-5) {
    if (!std::isfinite(actual) || std::abs(actual-expected)>tolerance)
        throw std::runtime_error(std::to_string(actual)+" differs from "+std::to_string(expected));
}
crimson::ibl::Image constant(const crimson::Vec3& color) { return {16,8,std::vector<crimson::Vec3>(128,color)}; }
void same(const crimson::Vec3& a,const crimson::Vec3& b,double tolerance=1e-5) {
    for (size_t i=0;i<3;++i) near(a[i],b[i],tolerance);
}
void cubeConvention() {
    using crimson::ibl::cubeDirection;
    const std::array<crimson::Vec3,6> axes{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    for (uint8_t face=0;face<6;++face) same(cubeDirection(face,.5f,.5f),axes[face]);
    // Adjacent edges coincide in world space despite different face UV axes.
    for (const float coordinate : {0.0f,.2f,.5f,.8f,1.0f}) {
        same(cubeDirection(0,0,coordinate),cubeDirection(4,1,coordinate));
        same(cubeDirection(0,1,coordinate),cubeDirection(5,0,coordinate));
        same(cubeDirection(4,coordinate,0),cubeDirection(2,coordinate,1));
        same(cubeDirection(4,coordinate,1),cubeDirection(3,coordinate,0));
        same(cubeDirection(0,coordinate,0),cubeDirection(2,1,1-coordinate));
    }
}
void sourceColorDecoding() {
    // Uncompressed, top-origin 2x2 RGB TGA. 128 sRGB is about .216 linear.
    std::vector<uint8_t> tga(18+2*2*3,128);
    std::fill_n(tga.begin(),18,uint8_t{0});
    tga[2]=2; tga[12]=2; tga[14]=2; tga[16]=24; tga[17]=0x20;
    const auto ldr=crimson::ibl::decode(tga);
    require(ldr.width==2 && ldr.height==2,"LDR source dimensions changed");
    for (const auto& pixel : ldr.pixels) same(pixel,{.2158605f,.2158605f,.2158605f});
    // Radiance RGBE (uncompressed short scanlines); E=132 gives scale 16/256.
    const std::string header="#?RADIANCE\nFORMAT=32-bit_rle_rgbe\n\n-Y 2 +X 2\n";
    std::vector<uint8_t> hdr(header.begin(),header.end());
    for (int i=0;i<4;++i) hdr.insert(hdr.end(),{192,160,128,132});
    const auto linear=crimson::ibl::decode(hdr);
    require(linear.width==2 && linear.height==2,"HDR source dimensions changed");
    for (const auto& pixel : linear.pixels) same(pixel,{12,10,8});
}
void constantNormalization() {
    const crimson::Vec3 color{.2f,1,4};
    const auto image=constant(color);
    for (const auto& normal : {crimson::Vec3{1,0,0},crimson::Vec3{0,1,0},crimson::Vec3{0,-1,0},crimson::Vec3{.3f,.4f,.5f}}) {
        same(crimson::ibl::integrateDiffuse(image,normal,1024),color,1e-5);
        for (const float roughness : {.045f,.1f,.5f,1.0f})
            same(crimson::ibl::prefilterSpecular(image,normal,roughness,1024),color,1e-5);
    }
    const crimson::ibl::Settings settings{2,4,8,128,128,128};
    const auto baked=crimson::ibl::bake(image,settings);
    require(baked.specular.size()==3,"Specular cache must contain a complete mip chain");
    for (const auto& level : baked.specular) for (size_t i=0;i<level.rgba.size();i+=4) {
        same({level.rgba[i],level.rgba[i+1],level.rgba[i+2]},color,1e-5);
        near(level.rgba[i+3],1);
    }
}
void reflectionRoughness() {
    auto image=constant({0,0,0}); image.width=256; image.height=128; image.pixels.resize(256*128);
    for (uint32_t y=0;y<image.height;++y) for (uint32_t x=0;x<image.width;++x) {
        const double theta=Pi*(y+.5)/image.height, phi=2*Pi*((x+.5)/image.width-.5);
        const float radiance=static_cast<float>(8*std::pow(std::max(0.0,std::sin(theta)*std::cos(phi)),128));
        image.pixels[static_cast<size_t>(y)*image.width+x]={radiance,radiance,radiance};
    }
    const auto sharp=crimson::ibl::prefilterSpecular(image,{1,0,0},.045f,4096);
    const auto rough=crimson::ibl::prefilterSpecular(image,{1,0,0},1,4096);
    require(sharp[0]>7 && rough[0]>0 && rough[0]<sharp[0]*.1f,"Roughness must broaden and dim the reflection of a narrow source");
    const auto wrapA=image.sample({-1,0,.000001f}), wrapB=image.sample({-1,0,-.000001f});
    same(wrapA,wrapB,1e-5);
}

void sourceResolutionAndSharpRadiance() {
    using namespace crimson;
    require(ibl::source_cube_size(512,256)==128,"Studio source cube size changed");
    require(ibl::source_cube_size(2048,1024)==512,"2k panorama lost its source-resolution radiance cube");
    require(ibl::source_cube_size(4096,2048)==1024,"4k panorama lost its source-resolution radiance cube");
    require(ibl::source_cube_size(8192,4096)==2048,"8k panorama did not reach the bounded radiance size");
    require(ibl::source_cube_size(16384,8192)==2048,"Radiance cube size exceeded its memory bound");
    require(ibl::source_cube_size(2049,1025)==1024,"Odd source dimensions rounded down and lost detail");
    require(ibl::source_cube_size(1,1)==1,"Tiny source radiance size is invalid");
    bool rejected=false;try{(void)ibl::source_cube_size(0,1024);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"An empty source selected a usable radiance cube");

    // Eight-source-pixel checker features are several native cube pixels wide,
    // but were reduced to about one pixel in the former fixed 128-face bake.
    // Interior feature samples must retain source radiance rather than inherit
    // the GGX convolution used for rougher reflection levels.
    ibl::Image image{2048,1024,std::vector<Vec3>(2048*1024)};
    for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x){
        const float value=((x/8+y/8)&1) ? 4.f : 0.f;
        image.pixels[size_t(y)*image.width+x]={value,value*.5f,value*.25f};
    }
    ibl::Settings settings{2,ibl::source_cube_size(image.width,image.height),2,1,8,1,16};
    const auto baked=ibl::bake(image,settings);
    require(baked.specular.front().size==512&&baked.specular.size()==10,"Adaptive radiance cube has the wrong native mip chain");
    unsigned bright=0,dark=0;
    for(uint32_t y=220;y<292;++y)for(uint32_t x=220;x<292;++x){
        const auto direction=ibl::cubeDirection(0,(x+.5f)/512,(y+.5f)/512);
        const double sx=(std::atan2(direction[2],direction[0])/(2*Pi)+.5)*image.width;
        const double sy=std::acos(std::clamp(double(direction[1]),-1.,1.))/Pi*image.height;
        const double cellX=std::fmod(sx,8.),cellY=std::fmod(sy,8.);
        if(cellX<3||cellX>5||cellY<3||cellY>5)continue;
        const auto expected=image.sample(direction);
        const size_t offset=(size_t(y)*512+x)*4;
        same({baked.specular[0].rgba[offset],baked.specular[0].rgba[offset+1],baked.specular[0].rgba[offset+2]},expected,1e-5);
        if(expected[0]>3.99f)++bright;else if(expected[0]<.01f)++dark;
    }
    require(bright>50&&dark>50,"Directional checker did not exercise both sharp radiance feature values");
    for(const auto& level:baked.specular)for(size_t i=0;i<level.rgba.size();i+=4){
        require(std::isfinite(level.rgba[i])&&level.rgba[i]>=0&&level.rgba[i]<=4.00001f,"A filtered radiance mip became nonfinite or gained energy");
        near(level.rgba[i+1],level.rgba[i]*.5,1e-5);near(level.rgba[i+2],level.rgba[i]*.25,1e-5);near(level.rgba[i+3],1);
    }
}

void boundedFilterEnergyAndCubeSeams() {
    using namespace crimson;
    const ibl::Settings settings{2,64,4,32,256,32,8};
    const auto uniform=ibl::bake(constant({.25f,2,17}),settings);
    for(const auto& level:uniform.specular)for(size_t i=0;i<level.rgba.size();i+=4)
        same({level.rgba[i],level.rgba[i+1],level.rgba[i+2]},{.25f,2,17},2e-5);

    // A directional radiance ramp has a continuous convolution across cube
    // faces. Enlarging a clamped eight-face grid would expose its first/last
    // texel centers as unrelated values along the higher-resolution seams.
    ibl::Image image{512,256,std::vector<Vec3>(512*256)};
    for(uint32_t y=0;y<image.height;++y)for(uint32_t x=0;x<image.width;++x){
        const double theta=Pi*(y+.5)/image.height,phi=2*Pi*((x+.5)/image.width-.5);
        image.pixels[size_t(y)*image.width+x]={float(2+.9*std::sin(theta)*std::cos(phi)),
            float(2+.9*std::cos(theta)),float(2+.9*std::sin(theta)*std::sin(phi))};
    }
    const auto directional=ibl::bake(image,settings);
    const auto& level=directional.specular[1];require(level.size>settings.specularFilterSize,"Seam fixture bypassed bounded-grid reconstruction");
    const auto texel=[&](unsigned face,unsigned x,unsigned y){const size_t offset=((size_t(face)*level.size+y)*level.size+x)*4;
        return Vec3{level.rgba[offset],level.rgba[offset+1],level.rgba[offset+2]};};
    const unsigned last=level.size-1;
    for(unsigned p=1;p<last;++p){
        same(texel(0,0,p),texel(4,last,p),.055);
        same(texel(0,last,p),texel(5,0,p),.055);
        same(texel(4,p,0),texel(2,p,last),.055);
        same(texel(0,p,0),texel(2,last,last-p),.055);
    }
}

double sphericalEnergy(const crimson::ibl::Image& image,size_t channel) {
    double energy=0;
    for (uint32_t y=0;y<image.height;++y) {
        // Exact spherical area, independently derived from the latitude bounds.
        const double area=2*Pi/image.width*(std::cos(Pi*y/image.height)-std::cos(Pi*(y+1)/image.height));
        for (uint32_t x=0;x<image.width;++x) energy+=image.pixels[static_cast<size_t>(y)*image.width+x][channel]*area;
    }
    return energy;
}

void sourcePyramidConservation() {
    const crimson::Vec3 color{.25f,2,17};
    const crimson::ibl::SourcePyramid flat({17,9,std::vector<crimson::Vec3>(17*9,color)});
    require(flat.levels.back().width==1 && flat.levels.back().height==1,"Source pyramid does not reach a spherical average");
    for (const auto& level : flat.levels) for (const auto& pixel : level.pixels) same(pixel,color);

    // Odd dimensions, both poles, and the longitudinal seam exercise area
    // weighting and partial source texels independently of GGX convolution.
    for (const uint32_t emitterRow : {0u,4u,8u}) {
        crimson::ibl::Image image{17,9,std::vector<crimson::Vec3>(17*9)};
        image.pixels[emitterRow*17]={30,10,2};
        image.pixels[emitterRow*17+16]={30,10,2};
        const crimson::ibl::SourcePyramid pyramid(image);
        for (size_t channel=0;channel<3;++channel) {
            const double expected=sphericalEnergy(image,channel);
            for (const auto& level : pyramid.levels)
                near(sphericalEnergy(level,channel),expected,std::max(1e-6,expected*2e-6));
            for (const auto& direction : {crimson::Vec3{1,0,0},crimson::Vec3{0,1,0},crimson::Vec3{0,-1,0}})
                near(pyramid.sample(direction,static_cast<float>(4*Pi))[channel],expected/(4*Pi),1e-5);
        }
        for (const float footprint : {0.0f,.1f,1.0f})
            same(pyramid.sample({-1,0,.0000001f},footprint),pyramid.sample({-1,0,-.0000001f},footprint),1e-4);
    }
}

struct EmitterSample {
    std::array<double,3> direction;
    double radianceSolidAngle;
};

std::vector<EmitterSample> emitterReferenceSamples(uint32_t width,uint32_t height) {
    std::vector<EmitterSample> samples;
    constexpr uint32_t Subdivisions=8;
    for (uint32_t y=height/2-1;y<=height/2;++y) for (uint32_t x=width/2-1;x<=width/2;++x)
        for (uint32_t sy=0;sy<Subdivisions;++sy) for (uint32_t sx=0;sx<Subdivisions;++sx) {
            const double theta0=Pi*(y+static_cast<double>(sy)/Subdivisions)/height;
            const double theta1=Pi*(y+static_cast<double>(sy+1)/Subdivisions)/height;
            const double theta=(theta0+theta1)*.5;
            const double phi=2*Pi*((x+(sx+.5)/Subdivisions)/width-.5);
            const double solidAngle=2*Pi/(width*Subdivisions)*(std::cos(theta0)-std::cos(theta1));
            samples.push_back({{std::sin(theta)*std::cos(phi),std::cos(theta),std::sin(theta)*std::sin(phi)},40000*solidAngle});
        }
    return samples;
}

double referenceEmitterPrefilter(const std::vector<EmitterSample>& emitter,const crimson::Vec3& normal,double roughness) {
    const double alphaSquared=std::pow(roughness,4);
    // With V=N, the reflected GGX half-vector PDF is D/4. Integrate its
    // NoL-weighted normalization analytically over the entire hemisphere.
    const double b=alphaSquared-1;
    const double normalization=2*alphaSquared/(b*b)*
        (std::log(2*alphaSquared/(1+alphaSquared))+(1+alphaSquared)/(2*alphaSquared)-1);
    double result=0;
    for (const auto& sample : emitter) {
        const double noL=normal[0]*sample.direction[0]+normal[1]*sample.direction[1]+normal[2]*sample.direction[2];
        if (noL<=0) continue;
        const double noHSquared=(1+noL)*.5;
        const double denominator=noHSquared*(alphaSquared-1)+1;
        const double distribution=alphaSquared/(Pi*denominator*denominator);
        result+=sample.radianceSolidAngle*noL*distribution*.25;
    }
    return result/normalization;
}

void concentratedHdrEmitter() {
    // A tiny, intensely bright emitter makes isolated GGX sample hits visible:
    // sampling only base-level pixels used to create unrelated bright glints.
    crimson::ibl::Image image{512,256,std::vector<crimson::Vec3>(512*256)};
    for (uint32_t y=127;y<=128;++y) for (uint32_t x=255;x<=256;++x)
        image.pixels[static_cast<size_t>(y)*image.width+x]={40000,20000,10000};
    const crimson::ibl::SourcePyramid source(image);
    const auto reference=emitterReferenceSamples(image.width,image.height);
    for (const float roughness : {.2f,.35f,.6f}) {
        for (int angle=8;angle<=70;angle+=2) for (int meridian=0;meridian<16;++meridian) {
            const double theta=angle*Pi/180, phi=(meridian+.137)*2*Pi/16;
            const crimson::Vec3 normal{static_cast<float>(std::cos(theta)),static_cast<float>(std::sin(theta)*std::cos(phi)),
                                      static_cast<float>(std::sin(theta)*std::sin(phi))};
            const auto actual=crimson::ibl::prefilterSpecular(source,normal,roughness,256);
            const double expected=referenceEmitterPrefilter(reference,normal,roughness);
            // Finite-sample source footprints intentionally trade a little bias
            // for stability. A disconnected bright hit is orders of magnitude
            // above this independently integrated single-lobe response.
            if (!std::isfinite(actual[0]) || std::abs(actual[0]-expected)>std::max(.1,expected*.75))
                throw std::runtime_error("HDR emitter roughness="+std::to_string(roughness)+
                    " offset="+std::to_string(angle)+" meridian="+std::to_string(meridian)+
                    " produced "+std::to_string(actual[0])+"; reference="+std::to_string(expected));
            near(actual[1],actual[0]*.5,1e-4);
            near(actual[2],actual[0]*.25,1e-4);
        }
    }
}

// Integrates the actual direct-light BRDF over solid angle, with double precision
// midpoint quadrature. This deliberately does not use the GGX sampling estimator.
std::array<double,2> referenceBrdf(double noV,double roughness) {
    constexpr unsigned Elevations=512, Azimuths=1024;
    const double alphaSquared=std::pow(roughness,4), vx=std::sqrt(1-noV*noV);
    std::array<double,Azimuths> cosines{};
    for (unsigned j=0;j<Azimuths;++j) cosines[j]=std::cos(2*Pi*(j+.5)/Azimuths);
    std::array<double,2> result{};
    for (unsigned i=0;i<Elevations;++i) {
        const double noL=(i+.5)/Elevations, sine=std::sqrt(1-noL*noL);
        const double visibility=.5/std::max(noV*std::sqrt(noL*noL*(1-alphaSquared)+alphaSquared)+
                                           noL*std::sqrt(noV*noV*(1-alphaSquared)+alphaSquared),1e-6);
        for (const double cosine : cosines) {
            const double voL=vx*sine*cosine+noV*noL;
            const double halfLength=std::sqrt(2+2*voL);
            const double noH=(noV+noL)/halfLength, voH=(1+voL)/halfLength;
            const double denominator=noH*noH*(alphaSquared-1)+1;
            const double distribution=alphaSquared/std::max(Pi*denominator*denominator,1e-12);
            const double fc=std::pow(1-voH,5);
            const double weight=distribution*visibility*noL*(2*Pi/(Elevations*Azimuths));
            result[0]+=(1-fc)*weight; result[1]+=fc*weight;
        }
    }
    return result;
}
void correlatedBrdfReference() {
    for (const auto parameters : {std::array<float,2>{1,1}, {.5f,.4f}, {.1f,.7f}, {.5f,1}}) {
        const auto actual=crimson::ibl::integrateBrdf(parameters[0],parameters[1],32768);
        const auto expected=referenceBrdf(parameters[0],parameters[1]);
        near(actual[0],expected[0],.003); near(actual[1],expected[1],.003);
    }
    const auto rough=crimson::ibl::integrateBrdf(1,1,32768);
    near(rough[0]+rough[1],1-std::log(2.0),1e-4);
    const auto smooth=crimson::ibl::integrateBrdf(1,.045f,4096);
    near(smooth[0]+smooth[1],1,.001);
}
void finiteExtremesAndFloor() {
    const auto invalid=std::numeric_limits<float>::quiet_NaN();
    for (const float noV : {0.0f,.00001f,.01f,.5f,1.0f,invalid}) for (const float roughness : {0.0f,.045f,.1f,.5f,1.0f,invalid}) {
        const auto value=crimson::ibl::integrateBrdf(noV,roughness,4096);
        require(std::isfinite(value[0]) && std::isfinite(value[1]) && value[0]>=0 && value[1]>=0,"BRDF lookup is nonfinite or negative");
        require(value[0]+value[1]<1.1f,"BRDF white furnace exceeds the single-scattering energy bound");
    }
    for (const float noV : {.00001f,.1f,.5f,1.0f}) {
        const auto zero=crimson::ibl::integrateBrdf(noV,0,1024), floor=crimson::ibl::integrateBrdf(noV,.045f,1024);
        near(zero[0],floor[0],0); near(zero[1],floor[1],0);
    }
    auto bad=constant({invalid,std::numeric_limits<float>::infinity(),-1});
    same(bad.sample({0,0,0}),{0,0,0});
}
void cacheIdentityAndIntegrity() {
    const crimson::ibl::Settings settings{2,4,8,64,64,64};
    const std::array<uint8_t,4> source{1,2,3,4}, changed{1,2,3,5};
    const auto key=crimson::ibl::cacheKey(source,settings);
    require(key!=crimson::ibl::cacheKey(changed,settings),"Source contents missing from cache identity");
    require(key!=crimson::ibl::cacheKey(source,settings,crimson::ibl::PreprocessVersion+1),"Preprocess version missing from cache identity");
    require(key!=crimson::ibl::cacheKey(source,settings,crimson::ibl::PreprocessVersion,crimson::ibl::BrdfVersion+1),"BRDF version missing from cache identity");
    auto revised=settings; ++revised.specularSamples;
    require(key!=crimson::ibl::cacheKey(source,revised),"Sampling settings missing from cache identity");
    revised=settings;revised.specularFilterSize/=2;
    require(key!=crimson::ibl::cacheKey(source,revised),"Bounded GGX grid missing from cache identity");
    const auto directory=std::filesystem::temp_directory_path()/("crimson-ibl-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove_all(path,error); } } cleanup{directory};
    const auto path=directory/"test.ibl";
    const auto expected=crimson::ibl::bake(constant({1,2,3}),settings);
    require(crimson::ibl::writeCache(path,key,settings,expected),"Cache write failed");
    crimson::ibl::Bake restored;
    require(crimson::ibl::readCache(path,key,settings,restored),"Cache round trip failed");
    require(restored.diffuse.rgba==expected.diffuse.rgba && restored.brdf==expected.brdf,"Cache changed derived float values");
    require(restored.specular.size()==expected.specular.size(),"Cache changed the radiance mip count");
    for(size_t i=0;i<expected.specular.size();++i)
        require(restored.specular[i].size==expected.specular[i].size&&restored.specular[i].rgba==expected.specular[i].rgba,"Cache changed sharp or GGX radiance values");
    require(!crimson::ibl::readCache(path,key+1,settings,restored),"Stale cache identity accepted");
    {
        std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);
        file.seekg(40); char value{}; file.read(&value,1); value^=1; file.seekp(40); file.write(&value,1);
    }
    require(!crimson::ibl::readCache(path,key,settings,restored),"Corrupted float payload accepted");
    require(crimson::ibl::writeCache(path,key,settings,expected),"Corrupt cache replacement failed");
    std::filesystem::resize_file(path,48);
    require(!crimson::ibl::readCache(path,key,settings,restored),"Truncated cache accepted");
    require(restored.diffuse.rgba==expected.diffuse.rgba,"Failed cache read modified the previous usable result");
}
} // namespace

int main() {
    int failures=0;
    const auto run=[&](const char* name,auto function) {
        try { function(); std::cout << "PASS " << name << '\n'; }
        catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
    };
    run("D3D cubemap axes and contiguous edges",cubeConvention);
    run("managed LDR sRGB decoding and HDR radiance preservation",sourceColorDecoding);
    run("constant radiance normalization and full mip chain",constantNormalization);
    run("roughness reflection broadening and equirectangular wrap",reflectionRoughness);
    run("source-adaptive cube sizes and unblurred native radiance features",sourceResolutionAndSharpRadiance);
    run("bounded GGX grid constant energy and cross-face reconstruction",boundedFilterEnergyAndCubeSeams);
    run("spherical source mip energy, odd sizes, poles and seam continuity",sourcePyramidConservation);
    run("concentrated HDR emitter stays one lobe at production sample count",concentratedHdrEmitter);
    run("correlated Smith LUT versus independent BRDF quadrature",correlatedBrdfReference);
    run("finite extremes, white furnace and shared roughness floor",finiteExtremesAndFloor);
    run("cache source/version/settings identity and corruption recovery",cacheIdentityAndIntegrity);
    return failures ? 1 : 0;
}
