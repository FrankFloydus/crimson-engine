#include "assets/GrungeGenerator.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void require(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
template<class F> void rejects(F&& operation) {try{operation();}catch(const std::exception&){return;}throw std::runtime_error("Invalid Grunge generation settings were accepted.");}
double difference(const DecodedImage& a,const DecodedImage& b) {
    double result{};for(std::size_t i=0;i<a.rgba.size();i+=4)result+=std::abs(static_cast<int>(a.rgba[i])-static_cast<int>(b.rgba[i]));
    return result/static_cast<double>(a.width*a.height);
}
void deterministicImagesAndIdentity() {
    MaterialGrungeNode settings;
    const auto first=generateGrungeTexture(settings,96),second=generateGrungeTexture(settings,96);
    require(first.width==96&&first.height==96&&first.rgba.size()==96*96*4&&first.rgba==second.rgba,"Grunge generation was nondeterministic or returned malformed pixels.");
    const auto key=grungeTextureKey(settings,96);settings.tiling=64;
    require(grungeTextureKey(settings,96)==key&&generateGrungeTexture(settings,96).rgba==first.rgba,"Sampling Tiling rebaked the generated image.");
    require(grungeTextureKey(settings,64)!=key,"Image resolution was absent from the generated cache key.");
    settings.tiling=1;settings.coverage=-0.f;const auto negativeZero=grungeTextureKey(settings,96);settings.coverage=0.f;
    require(grungeTextureKey(settings,96)==negativeZero,"Equivalent signed zero controls generated different image keys.");
    const auto black=generateGrungeTexture(settings,96);
    for(std::size_t i=0;i<black.rgba.size();i+=4)require(black.rgba[i]==0&&black.rgba[i+1]==0&&black.rgba[i+2]==0&&black.rgba[i+3]==255,"Coverage zero did not generate an opaque black grayscale texture.");
}
void authoredControls() {
    MaterialGrungeNode original;const auto baseline=generateGrungeTexture(original,96);const auto key=grungeTextureKey(original,96);
    const auto changes=[&](const auto& mutation) {
        auto changed=original;mutation(changed);
        require(grungeTextureKey(changed,96)!=key,"An image-producing Grunge control was absent from the cache key.");
        require(difference(baseline,generateGrungeTexture(changed,96))>.002,"A Grunge image control did not change the generated pixels.");
    };
    changes([](auto& s){s.seed=17;});changes([](auto& s){s.coverage=.25f;});changes([](auto& s){s.disorder=0;});changes([](auto& s){s.detail=0;});
    changes([](auto& s){s.baseNoisiness=0;});changes([](auto& s){s.dirtSpecksOpacity=0;});changes([](auto& s){s.invertDirt=true;});
    changes([](auto& s){s.scratchesOpacity=0;});changes([](auto& s){s.sharpen=0;});changes([](auto& s){s.largeVariationIntensity=0;});
    changes([](auto& s){s.stainsOpacity=0;});changes([](auto& s){s.leaksOpacity=.7f;});
}
void layersAndPeriodicNeighborhoods() {
    for(const auto seed:{0,17}) {
        MaterialGrungeNode settings;settings.seed=seed;settings.leaksOpacity=.45f;const auto image=generateGrungeTexture(settings,128);
        double mean{},squared{},interior{},boundary{};std::uint8_t minimum=255,maximum{};
        for(std::uint32_t y=0;y<image.height;++y)for(std::uint32_t x=0;x<image.width;++x) {
            const auto i=(static_cast<std::size_t>(y)*image.width+x)*4;const auto value=image.rgba[i];
            require(value==image.rgba[i+1]&&value==image.rgba[i+2]&&image.rgba[i+3]==255,"Grunge pixels were not linear grayscale RGBA8.");
            mean+=value;squared+=static_cast<double>(value)*value;minimum=std::min(minimum,value);maximum=std::max(maximum,value);
            if(x+1<image.width)interior+=std::abs(static_cast<int>(value)-static_cast<int>(image.rgba[i+4]));
            if(y+1<image.height)interior+=std::abs(static_cast<int>(value)-static_cast<int>(image.rgba[i+image.width*4]));
            if(x==0)boundary+=std::abs(static_cast<int>(value)-static_cast<int>(image.rgba[i+(image.width-1)*4]));
            if(y==0)boundary+=std::abs(static_cast<int>(value)-static_cast<int>(image.rgba[i+(image.height-1)*image.width*4]));
        }
        const double pixels=static_cast<double>(image.width)*image.height;mean/=pixels;
        const double deviation=std::sqrt(std::max(0.,squared/pixels-mean*mean));
        require(mean>15&&mean<240&&deviation>6&&maximum-minimum>40,"A finished Grunge recipe collapsed to a flat/empty image.");
        // Pixel centers at opposite edges are adjacent, rather than duplicate
        // endpoints. Compare their gradients with ordinary interior neighbors.
        interior/=2.*image.width*(image.width-1);boundary/=2.*image.width;
        require(boundary<interior*3.+3.,"Grunge generation or its wrapped sharpening introduced a tile-edge seam.");
    }
    MaterialGrungeNode settings;const auto sharp=generateGrungeTexture(settings,128);settings.sharpen=0;
    require(difference(sharp,generateGrungeTexture(settings,128))>.1,"Sharpen did not perform an image-neighborhood operation.");
}
void independentLayerComposition() {
    MaterialGrungeNode base;base.baseNoisiness=0;base.largeVariationIntensity=0;base.detail=0;base.sharpen=0;
    base.dirtSpecksOpacity=base.stainsOpacity=base.scratchesOpacity=base.leaksOpacity=0;
    const auto background=generateGrungeTexture(base,96);std::vector<DecodedImage> layers;
    for(unsigned layer=0;layer<4;++layer) {
        auto settings=base;
        if(layer==0)settings.dirtSpecksOpacity=.1f;
        if(layer==1)settings.stainsOpacity=.1f;
        if(layer==2)settings.scratchesOpacity=.1f;
        if(layer==3)settings.leaksOpacity=.1f;
        layers.push_back(generateGrungeTexture(settings,96));
        require(difference(background,layers.back())>.002,"An independent Grunge layer could not be enabled.");
    }
    auto settings=base;settings.dirtSpecksOpacity=settings.stainsOpacity=settings.scratchesOpacity=settings.leaksOpacity=.1f;
    const auto combined=generateGrungeTexture(settings,96);
    for(std::size_t i=0;i<background.rgba.size();i+=4) {
        int expected=background.rgba[i];for(const auto& layer:layers)expected+=static_cast<int>(layer.rgba[i])-background.rgba[i];
        require(std::abs(expected-static_cast<int>(combined.rgba[i]))<=3,"Enabling a Grunge layer reshuffled another layer or selected a different generator.");
    }
    settings.invertDirt=true;const auto inverted=generateGrungeTexture(settings,96);
    settings.dirtSpecksOpacity=0;const auto withoutDirt=generateGrungeTexture(settings,96);
    for(std::size_t i=0;i<combined.rgba.size();i+=4)
        require(std::abs(static_cast<int>(combined.rgba[i])+inverted.rgba[i]-2*withoutDirt.rgba[i])<=2,"Invert Dirt changed independent stains, scratches or leaks.");
}
void validationAndDefaultWorkload() {
    MaterialGrungeNode settings;
    for(const auto edge:{0u,1u,7u,GrungeMaximumTextureEdge+1})rejects([&]{generateGrungeTexture(settings,edge);});
    const auto invalid=[&](const auto& mutation){auto bad=settings;mutation(bad);rejects([&]{grungeTextureKey(bad,64);});rejects([&]{generateGrungeTexture(bad,64);});};
    invalid([](auto& s){s.seed=-1;});invalid([](auto& s){s.seed=65536;});invalid([](auto& s){s.tiling=0;});
    invalid([](auto& s){s.baseNoisiness=std::numeric_limits<float>::infinity();});
    invalid([](auto& s){s.dirtSpecksOpacity=-.1f;});invalid([](auto& s){s.scratchesOpacity=1.1f;});invalid([](auto& s){s.sharpen=std::numeric_limits<float>::quiet_NaN();});
    invalid([](auto& s){s.stainsOpacity=-.1f;});invalid([](auto& s){s.leaksOpacity=1.1f;});
    const auto start=std::chrono::steady_clock::now();const auto full=generateGrungeTexture(settings);
    require(full.width==1024&&full.height==1024&&full.rgba.size()==1024*1024*4,"Default Grunge generation was not a complete 1024 square texture.");
    const auto milliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
    std::cout<<"Default 1024 Grunge CPU generation: "<<milliseconds<<" ms\n";
}
void writePreviews(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    constexpr const char* names[]{"base","dirt","stains","scratches","leaks","combined","combined_all"};
    for(unsigned variant=0;variant<7;++variant) {
        MaterialGrungeNode settings;
        if(variant<5) {
            settings.dirtSpecksOpacity=variant==1?.62f:0;
            settings.stainsOpacity=variant==2?.35f:0;
            settings.scratchesOpacity=variant==3?.84f:0;
            settings.leaksOpacity=variant==4?.55f:0;
        } else if(variant==6)settings.leaksOpacity=.55f;
        const auto image=generateGrungeTexture(settings);
        const auto path=directory/(std::string(names[variant])+".pgm");std::ofstream output(path,std::ios::binary);
        if(!output)throw std::runtime_error("Could not create Grunge preview image.");
        output<<"P5\n"<<image.width<<' '<<image.height<<"\n255\n";
        std::vector<char> gray(static_cast<std::size_t>(image.width)*image.height);
        for(std::size_t i=0;i<gray.size();++i)gray[i]=static_cast<char>(image.rgba[i*4]);
        output.write(gray.data(),static_cast<std::streamsize>(gray.size()));
        if(!output)throw std::runtime_error("Could not write the complete Grunge preview image.");
        std::cout<<"Grunge image preview: "<<path.string()<<'\n';
    }
}
}
int main(int argc,char** argv) {try{
    if(argc!=1&&(argc!=3||std::string(argv[1])!="--previews"))throw std::runtime_error("Usage: grunge_generator_tests [--previews <directory>]");
    deterministicImagesAndIdentity();authoredControls();layersAndPeriodicNeighborhoods();independentLayerComposition();validationAndDefaultWorkload();
    if(argc==3)writePreviews(std::filesystem::path(argv[2]));
    std::cout<<"Grunge image synthesis, controls, identities and periodic filtering passed.\n";return 0;}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
