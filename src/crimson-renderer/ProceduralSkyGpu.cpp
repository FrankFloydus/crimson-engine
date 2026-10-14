#include "ProceduralSkyGpu.h"
#include "ShaderPreflight.h"

#include "cs_procedural_filter.h"
#include <algorithm>

namespace crimson {
namespace {
constexpr auto Format=bgfx::TextureFormat::RGBA16F;
constexpr std::uint64_t Flags=BGFX_TEXTURE_COMPUTE_WRITE|BGFX_SAMPLER_U_CLAMP|BGFX_SAMPLER_V_CLAMP|BGFX_SAMPLER_W_CLAMP;
template<class Handle> void destroy(Handle& handle) {
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
    handle=BGFX_INVALID_HANDLE;
}
}

ProceduralSkyGpu::~ProceduralSkyGpu() {
    for (auto& cube:cubes_) {destroy(cube.diffuse);destroy(cube.specular);}
    destroy(program_);destroy(skyUniform_);destroy(filterUniform_);
    for (auto& uniform:samplesUniform_) destroy(uniform);
}

bool ProceduralSkyGpu::initialize() {
    const auto* caps=bgfx::getCaps();
    if (!caps || !(caps->supported&BGFX_CAPS_COMPUTE) ||
        !(caps->formats[Format]&BGFX_CAPS_FORMAT_TEXTURE_IMAGE_WRITE) ||
        !bgfx::isTextureValid(0,true,1,Format,Flags)) {
        status_="Procedural sky requires RGBA16F cubemap compute support.";return false;
    }
    if (!bgfx::isValid(skyUniform_)) skyUniform_=bgfx::createUniform("u_proceduralSky",bgfx::UniformType::Vec4,7);
    if (!bgfx::isValid(filterUniform_)) filterUniform_=bgfx::createUniform("u_proceduralFilter",bgfx::UniformType::Vec4);
    constexpr std::array names{"u_proceduralSamples0","u_proceduralSamples1","u_proceduralSamples2","u_proceduralSamples3"};
    for (std::size_t i=0;i<samplesUniform_.size();++i)
        if (!bgfx::isValid(samplesUniform_[i])) samplesUniform_[i]=bgfx::createUniform(names[i],bgfx::UniformType::Vec4,128);
    if (!bgfx::isValid(skyUniform_) || !bgfx::isValid(filterUniform_) ||
        std::any_of(samplesUniform_.begin(),samplesUniform_.end(),[](auto handle){return !bgfx::isValid(handle);})) {
        status_="Procedural sky uniform allocation failed.";return false;
    }
    if (!bgfx::isValid(program_)) {
        const auto shader=bgfx::createShader(bgfx::copy(cc_cs_procedural_filter,sizeof(cc_cs_procedural_filter)));
        if (bgfx::isValid(shader)) program_=bgfx::createProgram(shader,true);
        if (!bgfx::isValid(program_)) {status_="Procedural sky compute program creation failed.";return false;}
    }
    return true;
}

bool ProceduralSkyGpu::allocate(Cubes& target) {
    if (bgfx::isValid(target.diffuse) && bgfx::isValid(target.specular)) return true;
    Cubes candidate;
    candidate.diffuse=bgfx::createTextureCube(procedural::DiffuseSize,false,1,Format,Flags);
    candidate.specular=bgfx::createTextureCube(procedural::SpecularSize,true,1,Format,Flags);
    if (!bgfx::isValid(candidate.diffuse) || !bgfx::isValid(candidate.specular)) {
        destroy(candidate.diffuse);destroy(candidate.specular);
        status_="Procedural sky cubemap allocation failed.";return false;
    }
    bgfx::setName(candidate.diffuse,"Procedural sky irradiance/pi");
    bgfx::setName(candidate.specular,"Procedural sky GGX environment");
    destroy(target.diffuse);destroy(target.specular);target=candidate;
    return true;
}

bool ProceduralSkyGpu::prepare(const ProceduralSkySettings& settings,const ProceduralSun& sun,bgfx::ViewId view) {
    const auto candidate=procedural::parameters(settings,sun);
    const auto candidateKey=procedural::cacheKey(candidate);
    if (valid_ && !dirty_ && candidateKey==key_) {status_.clear();return true;}
    if (!initialize()) return false;
    if (view>=bgfx::getCaps()->limits.maxViews) {status_="No renderer view is available for procedural sky convolution.";return false;}
    const unsigned next=valid_ ? 1-ready_ : ready_;
    if (!allocate(cubes_[next])) return false;
    bgfx::setViewName(view,"Procedural sky environment update");
    bgfx::setViewMode(view,bgfx::ViewMode::Sequential);
    const auto dispatch=[&](bgfx::TextureHandle target,std::uint8_t mip,std::uint16_t size,const procedural::FilterKernel& kernel) {
        const Vec4 filter{size,kernel.count,kernel.diffuse?1.f:0.f,0};
        bgfx::setUniform(skyUniform_,candidate.values.data(),static_cast<std::uint16_t>(candidate.values.size()));
        bgfx::setUniform(filterUniform_,filter.data());
        for (std::size_t block=0;block<samplesUniform_.size();++block)
            bgfx::setUniform(samplesUniform_[block],kernel.samples.data()+block*128,128);
        bgfx::setImage(0,target,mip,bgfx::Access::Write,Format);
        bgfx::dispatch(view,program_,(size+7u)/8u,(size+7u)/8u,6);
    };
    dispatch(cubes_[next].diffuse,0,procedural::DiffuseSize,procedural::diffuseKernel());
    for (std::uint8_t mip=0;mip<procedural::SpecularMipCount;++mip)
        dispatch(cubes_[next].specular,mip,static_cast<std::uint16_t>(procedural::SpecularSize>>mip),procedural::specularKernel(mip));
    // View ordering finishes the complete candidate before any reader samples it.
    ready_=next;parameters_=candidate;key_=candidateKey;valid_=true;dirty_=false;status_.clear();return true;
}

bgfx::ProgramHandle ProceduralSkyGpu::prepareProgramReload(std::span<const std::uint8_t> bytes,std::string& diagnostic) const {
    if (!preflightComputeShader(bytes,cc_cs_procedural_filter,diagnostic)) return BGFX_INVALID_HANDLE;
    const auto shader=bgfx::createShader(bgfx::copy(bytes.data(),static_cast<std::uint32_t>(bytes.size())));
    if (!bgfx::isValid(shader)) {diagnostic="Procedural sky compute shader creation failed.";return BGFX_INVALID_HANDLE;}
    const auto candidate=bgfx::createProgram(shader,true);
    if (!bgfx::isValid(candidate)) diagnostic="Procedural sky compute program creation failed.";
    return candidate;
}

void ProceduralSkyGpu::publishProgramReload(bgfx::ProgramHandle program) noexcept {
    if (!bgfx::isValid(program)) return;
    destroy(program_);program_=program;dirty_=true;
}

void ProceduralSkyGpu::bindBackground() const {
    if (!valid_ || !bgfx::isValid(skyUniform_)) return;
    bgfx::setUniform(skyUniform_,parameters_.values.data(),static_cast<std::uint16_t>(parameters_.values.size()));
}

std::size_t ProceduralSkyGpu::residentBytes() const {
    std::size_t result=0;
    for (const auto& cube:cubes_) {
        if (bgfx::isValid(cube.diffuse)) result+=std::size_t(procedural::DiffuseSize)*procedural::DiffuseSize*6*8;
        if (bgfx::isValid(cube.specular)) for (std::uint8_t mip=0;mip<procedural::SpecularMipCount;++mip) {
            const std::size_t size=procedural::SpecularSize>>mip;result+=size*size*6*8;
        }
    }
    return result;
}
} // namespace crimson
