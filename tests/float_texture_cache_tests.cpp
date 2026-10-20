#include "resources/TextureMipCache.h"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using namespace crimson;
void check(bool value,const char* text){if(!value)throw std::runtime_error(text);}
float channel(const DecodedImage& image,std::size_t index) {
    const auto bits=std::uint16_t(image.rgba.at(index*2))|(std::uint16_t(image.rgba.at(index*2+1))<<8);
    const auto sign=(bits&0x8000u)?-1.f:1.f;const auto exponent=(bits>>10)&31u;const auto mantissa=bits&1023u;
    return sign*(exponent?std::ldexp(1.f+float(mantissa)/1024.f,int(exponent)-15):std::ldexp(float(mantissa),-24));
}
template<class F> void rejects(F action){try{action();}catch(const std::exception&){return;}throw std::runtime_error("Invalid float cache input was accepted.");}
}
int main(){try {
    FloatImage image{5,3,{}};image.rgba.resize(5*3*4);
    for(std::size_t first=0;first<image.rgba.size();first+=4){image.rgba[first]=.5009765625f;image.rgba[first+1]=2.f;image.rgba[first+2]=-.5f;image.rgba[first+3]=.25f;}
    const auto cache=streaming::generatedFloatTextureMipCache(123,image);
    check(cache.format==ImagePixelFormat::RGBA16F&&cache.memory.size()==3,"Float cache did not retain its rectangular mip chain and RGBA16F format.");
    check(cache.memory[0].width==5&&cache.memory[0].height==3&&cache.memory[1].width==2&&cache.memory[1].height==1&&cache.memory[2].width==1,"Rectangular float mip extents are incorrect.");
    for(const auto& mip:cache.memory) {
        check(mip.format==ImagePixelFormat::RGBA16F&&mip.rgba.size()==std::size_t(mip.width)*mip.height*8,"Float mip bytes were counted as RGBA8.");
        check(channel(mip,0)==.5009765625f&&channel(mip,1)==2.f&&channel(mip,2)==-.5f&&channel(mip,3)==.25f,"Float mips quantized, gamma-converted, clamped or premultiplied the linear source.");
    }
    auto tail=streaming::generatedFloatTextureMipCache(124,FloatImage{3,1,{.25f,0,0,1,.5f,0,0,1,.75f,0,0,1}});
    check(channel(tail.memory.back(),0)==.5f,"NPOT float mip filtering excluded source samples.");
    rejects([&]{streaming::writeTextureMipCache("unused-float-cache.mips",cache.key,streaming::TextureRole::Data,cache.memory);});
    image.rgba[0]=std::numeric_limits<float>::quiet_NaN();rejects([&]{streaming::generatedFloatTextureMipCache(1,image);});
    image.rgba[0]=65536.f;rejects([&]{streaming::generatedFloatTextureMipCache(1,image);});
    std::stop_source canceled;canceled.request_stop();image.rgba[0]=0;rejects([&]{streaming::generatedFloatTextureMipCache(1,image,canceled.get_token());});
    const auto rgba8=streaming::generatedTextureMipCache(1,DecodedImage{1,1,{128,128,128,255}});
    check(rgba8.format==ImagePixelFormat::RGBA8&&rgba8.memory[0].rgba.size()==4,"Existing RGBA8 generation changed format.");
    std::cout<<"Linear HDR float texture precision, rectangular mip and cancellation checks passed.\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
