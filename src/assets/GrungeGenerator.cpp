#include "GrungeGenerator.h"
#include <crimson/renderer/forward/MaterialGraphImageBake.h>
#include <bit>
#include <cmath>
#include <stdexcept>

namespace crimson {
namespace {
void validate(const MaterialGrungeNode& g,std::uint32_t edge) {
    const auto unit=[](float x){return std::isfinite(x)&&x>=0&&x<=1;};
    if(edge<8||edge>GrungeMaximumTextureEdge)throw std::runtime_error("Grunge image edge must be in 8-2048.");
    if(g.seed<0||g.seed>65535||g.tiling<1||g.tiling>64||!unit(g.coverage)||!unit(g.disorder)||!unit(g.detail)||
        !unit(g.baseNoisiness)||!unit(g.dirtSpecksOpacity)||!unit(g.scratchesOpacity)||!unit(g.sharpen)||!unit(g.largeVariationIntensity)||!unit(g.stainsOpacity)||!unit(g.leaksOpacity))
        throw std::runtime_error("Grunge image settings exceed their finite supported ranges.");
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
    return generateMaterialGrungeImage(g,textureEdge);
}
}
