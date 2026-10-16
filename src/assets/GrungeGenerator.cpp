#include "GrungeGenerator.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace crimson {
namespace {
using Plane=std::vector<float>;
constexpr float Pi=3.14159265358979323846f;
float saturate(float x) {return std::clamp(x,0.f,1.f);}
float smooth(float x) {x=saturate(x);return x*x*(3.f-2.f*x);}
std::uint32_t hash32(std::uint32_t x) {x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
float random(std::uint32_t seed,std::uint32_t index) {return static_cast<float>(hash32(seed^hash32(index+0x9e3779b9u))>>8)*(1.f/16777216.f);}
int wrap(int x,int edge) {const int r=x%edge;return r<0?r+edge:r;}
std::size_t pixel(int x,int y,int edge) {return static_cast<std::size_t>(wrap(y,edge))*edge+wrap(x,edge);}
void validate(const MaterialGrungeNode& g,std::uint32_t edge) {
    const auto unit=[](float x){return std::isfinite(x)&&x>=0&&x<=1;};
    if(edge<8||edge>GrungeMaximumTextureEdge)throw std::runtime_error("Grunge image edge must be in 8-2048.");
    if(g.seed<0||g.seed>65535||g.tiling<1||g.tiling>64||!unit(g.coverage)||!unit(g.disorder)||!unit(g.detail)||
        !unit(g.baseNoisiness)||!unit(g.dirtSpecksOpacity)||!unit(g.scratchesOpacity)||!unit(g.sharpen)||!unit(g.largeVariationIntensity)||!unit(g.stainsOpacity)||!unit(g.leaksOpacity))
        throw std::runtime_error("Grunge image settings exceed their finite supported ranges.");
}

// The corner lattice wraps, while interpolation coordinates stay continuous.
// Precomputed lattice/axis tables avoid hashing every octave at every pixel.
void addNoise(Plane& destination,int edge,int cellsX,int cellsY,std::uint32_t seed,float amplitude) {
    cellsX=std::clamp(cellsX,1,edge/2);cellsY=std::clamp(cellsY,1,edge/2);
    Plane lattice(static_cast<std::size_t>(cellsX)*cellsY);
    for(std::size_t i=0;i<lattice.size();++i)lattice[i]=random(seed,static_cast<std::uint32_t>(i))-.5f;
    struct Axis {int a,b;float t;};std::vector<Axis> xAxis(edge),yAxis(edge);
    const auto axis=[&](std::vector<Axis>& table,int cells) {
        for(int i=0;i<edge;++i) {
            const float position=(static_cast<float>(i)+.5f)*static_cast<float>(cells)/static_cast<float>(edge);
            const int cell=static_cast<int>(position);const float f=position-static_cast<float>(cell);
            table[i]={cell%cells,(cell+1)%cells,f*f*f*(f*(f*6.f-15.f)+10.f)};
        }
    };
    axis(xAxis,cellsX);axis(yAxis,cellsY);
    for(int y=0;y<edge;++y) {
        const auto& ay=yAxis[y];const auto rowA=static_cast<std::size_t>(ay.a)*cellsX,rowB=static_cast<std::size_t>(ay.b)*cellsX;
        for(int x=0;x<edge;++x) {
            const auto& ax=xAxis[x];const float a=lattice[rowA+ax.a],b=lattice[rowA+ax.b],c=lattice[rowB+ax.a],d=lattice[rowB+ax.b];
            destination[static_cast<std::size_t>(y)*edge+x]+=(a+(b-a)*ax.t+((c+(d-c)*ax.t)-(a+(b-a)*ax.t))*ay.t)*amplitude;
        }
    }
}
float sample(const Plane& source,int edge,float x,float y) {
    const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));const float tx=x-static_cast<float>(ix),ty=y-static_cast<float>(iy);
    const float a=source[pixel(ix,iy,edge)],b=source[pixel(ix+1,iy,edge)],c=source[pixel(ix,iy+1,edge)],d=source[pixel(ix+1,iy+1,edge)];
    return a+(b-a)*tx+((c+(d-c)*tx)-(a+(b-a)*tx))*ty;
}

// Rasterization writes all periodic images through wrapped destination indices.
// Soft subpixel marks preserve their energy instead of snapping to solid pixels.
void spot(Plane& field,int edge,float cx,float cy,float rx,float ry,float angle,float strength,bool soft) {
    const float originalX=rx,originalY=ry;rx=std::max(rx,.55f);ry=std::max(ry,.55f);
    strength*=std::min(1.f,originalX/rx)*std::min(1.f,originalY/ry);
    const float c=std::cos(angle),s=std::sin(angle),radius=std::max(rx,ry)+1.f;
    const int x0=static_cast<int>(std::floor(cx-radius)),x1=static_cast<int>(std::ceil(cx+radius));
    const int y0=static_cast<int>(std::floor(cy-radius)),y1=static_cast<int>(std::ceil(cy+radius));
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
        const float dx=static_cast<float>(x)+.5f-cx,dy=static_cast<float>(y)+.5f-cy;
        const float u=(c*dx+s*dy)/rx,v=(-s*dx+c*dy)/ry,d2=u*u+v*v;if(d2>=1.f)continue;
        const float falloff=soft?(1.f-d2)*(1.f-d2):smooth((1.f-std::sqrt(d2))*std::min(rx,ry)+.5f);
        auto& target=field[pixel(x,y,edge)];target=std::max(target,strength*falloff);
    }
}
void dirtLayer(Plane& dirt,int edge,std::uint32_t seed,float disorder) {
    const float scale=static_cast<float>(edge)/1024.f;
    for(std::uint32_t i=0;i<14500;++i) {
        const auto layerSeed=hash32(seed+i*31u);const float size=random(layerSeed,0);
        const float radius=(.35f+size*size*size*3.25f)*scale;
        const float cx=random(layerSeed,1)*edge,cy=random(layerSeed,2)*edge;
        const float originalY=radius*(.45f+random(layerSeed,3)*(.45f+disorder));
        const float rx=std::max(.55f,radius),ry=std::max(.55f,originalY),angle=random(layerSeed,4)*Pi;
        const float strength=(.2f+.8f*random(layerSeed,5))*std::min(1.f,radius/rx)*std::min(1.f,originalY/ry);
        const float c=std::cos(angle),s=std::sin(angle),extent=std::max(rx,ry)*1.25f+1.f;
        // Intersect irregular half-planes instead of stamping smooth ellipses.
        // Tiny angular grains retain chipped edges without droplet-shaped rims.
        float nx[6],ny[6],boundary[6];
        for(unsigned side=0;side<6;++side) {
            const float direction=static_cast<float>(side)*Pi/3.f+(random(layerSeed,8+side)-.5f)*disorder*.5f;
            nx[side]=std::cos(direction);ny[side]=std::sin(direction);boundary[side]=.65f+.35f*random(layerSeed,16+side);
        }
        const int x0=static_cast<int>(std::floor(cx-extent)),x1=static_cast<int>(std::ceil(cx+extent));
        const int y0=static_cast<int>(std::floor(cy-extent)),y1=static_cast<int>(std::ceil(cy+extent));
        for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x) {
            const float dx=static_cast<float>(x)+.5f-cx,dy=static_cast<float>(y)+.5f-cy;
            const float u=(c*dx+s*dy)/rx,v=(-s*dx+c*dy)/ry;float distance{};
            for(unsigned side=0;side<6;++side)distance=std::max(distance,(u*nx[side]+v*ny[side])/boundary[side]);
            if(distance>=1.f)continue;
            const float opacity=smooth((1.f-distance)*std::min(rx,ry)+.25f);
            auto& target=dirt[pixel(x,y,edge)];target=std::max(target,strength*opacity);
        }
    }
}
void scratchesLayer(Plane& scratches,int edge,std::uint32_t seed,float disorder,float detail) {
    const float scale=static_cast<float>(edge)/1024.f;
    for(std::uint32_t i=0;i<440;++i) {
        const auto layerSeed=hash32(seed+i*71u);
        const float cx=random(layerSeed,0)*edge,cy=random(layerSeed,1)*edge;
        const float angle=(random(layerSeed,2)-.5f)*disorder*.45f;
        const float dx=std::cos(angle),dy=std::sin(angle),length=(12.f+random(layerSeed,3)*120.f)*scale;
        const float width=(.35f+random(layerSeed,4)*(.65f+detail*.7f))*scale;
        const float opacity=.25f+.7f*random(layerSeed,5),bend=(random(layerSeed,6)-.5f)*8.f*scale*disorder;
        const int steps=std::max(1,static_cast<int>(std::ceil(length/.7f)));
        for(int step=0;step<=steps;++step) {
            const float t=static_cast<float>(step)/steps;
            if((step/9)%5==2&&random(layerSeed,7)<disorder*.5f)continue;
            const float along=(t-.5f)*length,across=bend*std::sin(t*Pi);
            const float taper=.4f+.6f*std::sin(t*Pi);
            spot(scratches,edge,cx+dx*along-dy*across,cy+dy*along+dx*across,width*taper,width*taper,0,opacity,false);
        }
    }
}
void leaksLayer(Plane& leaks,int edge,std::uint32_t seed,float disorder) {
    const float scale=static_cast<float>(edge)/1024.f;
    for(std::uint32_t i=0;i<44;++i) {
        const auto layerSeed=hash32(seed+i*97u);
        const float cx=random(layerSeed,0)*edge,cy=random(layerSeed,1)*edge,length=(.12f+.70f*random(layerSeed,2))*edge;
        const float width=(.9f+7.f*std::pow(random(layerSeed,3),2.f))*scale,opacity=.3f+.65f*random(layerSeed,4);
        const float bend=(random(layerSeed,5)-.5f)*45.f*scale*disorder;
        const int steps=std::max(1,static_cast<int>(std::ceil(length/.8f)));
        for(int step=0;step<=steps;++step) {
            const float t=static_cast<float>(step)/steps,x=cx+bend*std::sin(t*Pi*2.f),y=cy+t*length;
            const float radius=width*(1.f-.85f*t);
            spot(leaks,edge,x,y,radius*2.8f,radius*2.8f,0,opacity*.18f,true);
            spot(leaks,edge,x,y,radius,radius,0,opacity*(1.f-.4f*t),true);
        }
    }
}
Plane wrappedBlur(const Plane& source,int edge,int radius) {
    Plane temporary(source.size()),result(source.size());const float inverse=1.f/static_cast<float>(radius*2+1);
    for(int y=0;y<edge;++y) {
        const auto row=static_cast<std::size_t>(y)*edge;float sum{};
        for(int x=-radius;x<=radius;++x)sum+=source[row+wrap(x,edge)];
        for(int x=0;x<edge;++x){temporary[row+x]=sum*inverse;sum+=source[row+wrap(x+radius+1,edge)]-source[row+wrap(x-radius,edge)];}
    }
    for(int x=0;x<edge;++x) {
        float sum{};for(int y=-radius;y<=radius;++y)sum+=temporary[pixel(x,y,edge)];
        for(int y=0;y<edge;++y){result[static_cast<std::size_t>(y)*edge+x]=sum*inverse;sum+=temporary[pixel(x,y+radius+1,edge)]-temporary[pixel(x,y-radius,edge)];}
    }
    return result;
}
}

std::uint64_t grungeTextureKey(const MaterialGrungeNode& g,std::uint32_t edge) {
    validate(g,edge);std::uint64_t key=14695981039346656037ull;
    const auto word=[&](std::uint32_t value){for(unsigned b=0;b<4;++b){key^=(value>>(b*8))&255u;key*=1099511628211ull;}};
    word(GrungeGeneratorRevision);word(edge);word(static_cast<std::uint32_t>(g.seed));
    for(const float value:{g.coverage,g.disorder,g.detail,g.baseNoisiness,g.dirtSpecksOpacity,g.scratchesOpacity,g.sharpen,g.largeVariationIntensity,g.stainsOpacity,g.leaksOpacity})
        word(std::bit_cast<std::uint32_t>(value==0?0.f:value));
    word(g.invertDirt?1u:0u);return key;
}
DecodedImage generateGrungeTexture(const MaterialGrungeNode& g,std::uint32_t textureEdge) {
    validate(g,textureEdge);const int edge=static_cast<int>(textureEdge);const auto count=static_cast<std::size_t>(edge)*edge;
    DecodedImage image{textureEdge,textureEdge,std::vector<std::uint8_t>(count*4,0)};
    for(std::size_t i=0;i<count;++i)image.rgba[i*4+3]=255;
    if(g.coverage==0)return image;
    const auto seed=hash32(static_cast<std::uint32_t>(g.seed)^0x43524745u);
    Plane broad(count),body(count),fine(count),dirt(count),scratches(count),stains(count),leaks(count),surface(count);
    addNoise(broad,edge,2,2,seed^0x1001u,.60f);addNoise(broad,edge,4,4,seed^0x1002u,.27f);addNoise(broad,edge,8,8,seed^0x1003u,.13f);
    constexpr int bodyCounts[]{8,16,32,64,128};constexpr float weights[]{.48f,.25f,.14f,.08f,.05f};
    for(unsigned i=0;i<5;++i)addNoise(body,edge,bodyCounts[i],bodyCounts[i],seed^(0x2001u+i),weights[i]);
    addNoise(fine,edge,64,64,seed^0x3001u,.25f);addNoise(fine,edge,192,192,seed^0x3002u,.45f);
    addNoise(fine,edge,384,384,seed^0x3003u,.30f);
    if(g.dirtSpecksOpacity>0)dirtLayer(dirt,edge,seed^0x4001u,g.disorder);
    if(g.scratchesOpacity>0)scratchesLayer(scratches,edge,seed^0x5001u,g.disorder,g.detail);
    if(g.leaksOpacity>0)leaksLayer(leaks,edge,seed^0x6001u,g.disorder);
    if(g.stainsOpacity>0) {
        addNoise(stains,edge,4,4,seed^0x8001u,.55f);addNoise(stains,edge,12,12,seed^0x8002u,.30f);
        addNoise(stains,edge,36,36,seed^0x8003u,.15f);
    }
    const float coverageShift=(g.coverage-.5f)*.65f,coverageGain=std::min(1.f,g.coverage*2.f);
    for(int y=0;y<edge;++y)for(int x=0;x<edge;++x) {
        const auto i=static_cast<std::size_t>(y)*edge+x;
        const float wx=broad[i]*g.disorder*.1f*edge,wy=broad[pixel(x+edge/3,y+edge/2,edge)]*g.disorder*.1f*edge;
        const float aggregate=sample(body,edge,static_cast<float>(x)+wx,static_cast<float>(y)+wy);
        const float grain=fine[i]+(random(seed^0x7001u,static_cast<std::uint32_t>(i))-.5f)*g.detail*.42f;
        const float dirtSign=g.invertDirt?1.f:-1.f;
        const float darkMottle=smooth((-.05f-aggregate-broad[i]*.45f)*4.f);
        const float stainCloud=sample(stains,edge,static_cast<float>(x)+wx*.45f,static_cast<float>(y)+wy*.45f);
        const float stainMask=smooth((stainCloud+.02f)*4.f);
        const float value=.40f+aggregate*g.baseNoisiness*1.1f+broad[i]*g.largeVariationIntensity*.80f+grain*(.22f+.45f*g.detail)
            -darkMottle*g.largeVariationIntensity*.22f+dirtSign*dirt[i]*g.dirtSpecksOpacity*.55f
            -stainMask*g.stainsOpacity*.35f-scratches[i]*g.scratchesOpacity*.25f-leaks[i]*g.leaksOpacity*.60f;
        surface[i]=saturate(value+coverageShift)*coverageGain;
    }
    // A real image neighborhood operation, with identical treatment at tile edges.
    const auto blurred=wrappedBlur(surface,edge,std::max(1,edge/256));
    for(std::size_t i=0;i<count;++i) {
        // Bound positive overshoot: sharpening a dark pit must not turn its
        // neighboring aggregate into a bright, smooth coffee-ring outline.
        const float detail=std::clamp(surface[i]-blurred[i],-.085f,.025f);
        const float value=saturate(surface[i]+detail*g.sharpen*1.1f);
        const auto channel=static_cast<std::uint8_t>(std::lround(value*255.f));
        image.rgba[i*4]=image.rgba[i*4+1]=image.rgba[i*4+2]=channel;
    }
    return image;
}
}
