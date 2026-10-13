#include "Renderer.h"
#include "Mesh.h"
#include "Environment.h"

#include <SDL3/SDL.h>
#include <bgfx/platform.h>
#include <bx/math.h>

#include "vs_pbr.h"
#include "fs_pbr.h"
#include "vs_present.h"
#include "fs_present.h"
#include "vs_grid.h"
#include "fs_grid.h"
#include "fs_encode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace crimson {
namespace {
float finiteOr(float value, float fallback) { return std::isfinite(value) ? value : fallback; }
float bounded(float value, float fallback, float lo, float hi) { return std::clamp(finiteOr(value, fallback), lo, hi); }
float toLinear(float value) {
    const float c = bounded(value, 0.0f, 0.0f, 1.0f);
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
template<class Handle> void destroyHandle(Handle& handle) noexcept {
    if (bgfx::isValid(handle)) bgfx::destroy(handle);
    handle = BGFX_INVALID_HANDLE;
}

// GPU ownership stays out of the CPU mesh generator.
struct GpuMesh {
    bgfx::VertexBufferHandle vertices{BGFX_INVALID_HANDLE};
    bgfx::IndexBufferHandle indices{BGFX_INVALID_HANDLE};
    void upload(const MeshData& data) {
        if (data.vertices.empty() || data.indices.empty()) throw std::invalid_argument("Cannot upload an empty mesh.");
        bgfx::VertexLayout layout;
        layout.begin().add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Normal, 3, bgfx::AttribType::Float)
            .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
            .add(bgfx::Attrib::Tangent, 4, bgfx::AttribType::Float).end();
        vertices = bgfx::createVertexBuffer(bgfx::copy(data.vertices.data(), static_cast<std::uint32_t>(data.vertices.size() * sizeof(Vertex))), layout);
        indices = bgfx::createIndexBuffer(bgfx::copy(data.indices.data(), static_cast<std::uint32_t>(data.indices.size() * sizeof(std::uint16_t))));
        if (!bgfx::isValid(vertices) || !bgfx::isValid(indices)) {
            reset(); throw std::runtime_error("GPU mesh buffer creation failed.");
        }
    }
    void bind() const { bgfx::setVertexBuffer(0, vertices); bgfx::setIndexBuffer(indices); }
    void reset() noexcept { destroyHandle(vertices); destroyHandle(indices); }
};
template<std::size_t V, std::size_t F>
bgfx::ProgramHandle program(const std::uint8_t (&vs)[V], const std::uint8_t (&fs)[F], const char* name) {
    auto vertex = bgfx::createShader(bgfx::copy(vs, static_cast<std::uint32_t>(V)));
    auto fragment = bgfx::createShader(bgfx::copy(fs, static_cast<std::uint32_t>(F)));
    if (!bgfx::isValid(vertex) || !bgfx::isValid(fragment)) {
        destroyHandle(vertex); destroyHandle(fragment);
        throw std::runtime_error(std::string("Failed to create shader: ") + name);
    }
    bgfx::setName(vertex, name); bgfx::setName(fragment, name);
    const auto result = bgfx::createProgram(vertex, fragment, true);
    if (!bgfx::isValid(result)) throw std::runtime_error(std::string("Failed to link shader: ") + name);
    return result;
}
bgfx::UniformHandle uniform(const char* name, bgfx::UniformType::Enum type = bgfx::UniformType::Vec4, std::uint16_t count = 1) {
    const auto handle = bgfx::createUniform(name, type, count);
    if (!bgfx::isValid(handle)) throw std::runtime_error(std::string("Failed to create uniform: ") + name);
    return handle;
}

// Framebuffers borrow textures: HDR and the grid composite share one depth target.
struct ViewportTargets {
    bgfx::TextureHandle hdr{BGFX_INVALID_HANDLE}, composite{BGFX_INVALID_HANDLE};
    bgfx::TextureHandle display{BGFX_INVALID_HANDLE}, depth{BGFX_INVALID_HANDLE};
    bgfx::FrameBufferHandle hdrBuffer{BGFX_INVALID_HANDLE}, compositeBuffer{BGFX_INVALID_HANDLE}, displayBuffer{BGFX_INVALID_HANDLE};
    ~ViewportTargets() {
        destroyHandle(hdrBuffer); destroyHandle(compositeBuffer); destroyHandle(displayBuffer);
        destroyHandle(hdr); destroyHandle(composite); destroyHandle(display); destroyHandle(depth);
    }
    void create(std::uint32_t w, std::uint32_t h) {
        const auto maxSize = std::min<std::uint32_t>(bgfx::getCaps()->limits.maxTextureSize, std::numeric_limits<std::uint16_t>::max());
        if (!w || !h || w > maxSize || h > maxSize) throw std::runtime_error("Viewport exceeds the GPU texture size limit.");
        const auto ww = static_cast<std::uint16_t>(w), hh = static_cast<std::uint16_t>(h);
        constexpr std::uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP |
            BGFX_SAMPLER_MIN_POINT | BGFX_SAMPLER_MAG_POINT | BGFX_SAMPLER_MIP_POINT;
        hdr = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::RGBA16F, flags);
        composite = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::RGBA16F, flags);
        // Encoded bytes in ordinary RGBA8; ImGui must not decode them again.
        display = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::RGBA8, flags);
        depth = bgfx::createTexture2D(ww, hh, false, 1, bgfx::TextureFormat::D24S8, BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY);
        if (!bgfx::isValid(hdr) || !bgfx::isValid(composite) || !bgfx::isValid(display) || !bgfx::isValid(depth))
            throw std::runtime_error("Failed to create viewport color/depth targets.");
        const bgfx::TextureHandle hdrAttachments[]{hdr, depth};
        const bgfx::TextureHandle compositeAttachments[]{composite, depth};
        hdrBuffer = bgfx::createFrameBuffer(2, hdrAttachments, false);
        compositeBuffer = bgfx::createFrameBuffer(2, compositeAttachments, false);
        displayBuffer = bgfx::createFrameBuffer(1, &display, false);
        if (!bgfx::isValid(hdrBuffer) || !bgfx::isValid(compositeBuffer) || !bgfx::isValid(displayBuffer))
            throw std::runtime_error("Failed to create viewport framebuffers.");
        bgfx::setName(hdr, "Viewport HDR scene");
        bgfx::setName(composite, "Linear tone map and grid");
        bgfx::setName(display, "Encoded viewport image");
        bgfx::setName(depth, "Shared scene and grid depth");
    }
};
} // namespace

struct Renderer::Impl {
    bool initialized{false}, windowDrawable{false}, viewportDrawable{false};
    std::uint32_t windowWidth{1}, windowHeight{1}, width{1}, height{1};
    GpuMesh sphere, fullscreen, grid;
    std::unique_ptr<ViewportTargets> targets;
    std::unique_ptr<EnvironmentLighting> environment;
    bgfx::TextureHandle white{BGFX_INVALID_HANDLE};
    bgfx::ProgramHandle pbr{BGFX_INVALID_HANDLE}, present{BGFX_INVALID_HANDLE}, gridProgram{BGFX_INVALID_HANDLE}, encode{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle baseColorMetallic{BGFX_INVALID_HANDLE}, roughness{BGFX_INVALID_HANDLE}, textureFlags{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle cameraPos{BGFX_INVALID_HANDLE}, direction{BGFX_INVALID_HANDLE}, directionColor{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle pointPosition{BGFX_INVALID_HANDLE}, pointColor{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle presentParams{BGFX_INVALID_HANDLE}, hdrSampler{BGFX_INVALID_HANDLE}, compositeSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle albedoSampler{BGFX_INVALID_HANDLE}, roughnessSampler{BGFX_INVALID_HANDLE}, metallicSampler{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle normalSampler{BGFX_INVALID_HANDLE}, irradianceSampler{BGFX_INVALID_HANDLE}, prefilteredSampler{BGFX_INVALID_HANDLE}, brdfSampler{BGFX_INVALID_HANDLE}, environmentParams{BGFX_INVALID_HANDLE};
    bgfx::UniformHandle gridMinor{BGFX_INVALID_HANDLE}, gridMajor{BGFX_INVALID_HANDLE}, gridAxisU{BGFX_INVALID_HANDLE}, gridAxisV{BGFX_INVALID_HANDLE}, gridParams{BGFX_INVALID_HANDLE};
    ~Impl() {
        if (!initialized) return;
        sphere.reset(); fullscreen.reset(); grid.reset(); targets.reset();
        environment.reset();
        destroyHandle(white);
        destroyHandle(pbr); destroyHandle(present); destroyHandle(gridProgram); destroyHandle(encode);
        destroyHandle(baseColorMetallic); destroyHandle(roughness); destroyHandle(textureFlags);
        destroyHandle(cameraPos); destroyHandle(direction); destroyHandle(directionColor);
        destroyHandle(pointPosition); destroyHandle(pointColor);
        destroyHandle(presentParams); destroyHandle(hdrSampler); destroyHandle(compositeSampler);
        destroyHandle(albedoSampler); destroyHandle(roughnessSampler); destroyHandle(metallicSampler);
        destroyHandle(normalSampler); destroyHandle(irradianceSampler); destroyHandle(prefilteredSampler); destroyHandle(brdfSampler); destroyHandle(environmentParams);
        destroyHandle(gridMinor); destroyHandle(gridMajor); destroyHandle(gridAxisU); destroyHandle(gridAxisV); destroyHandle(gridParams);
        bgfx::shutdown();
    }
};

Renderer::Renderer(SDL_Window* window, bgfx::CallbackI* callback) : impl_(std::make_unique<Impl>()) {
    if (!window) throw std::invalid_argument("Renderer requires an SDL window.");
#if defined(_WIN32)
    void* hwnd = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (!hwnd) throw std::runtime_error(std::string("SDL did not provide a native HWND: ") + SDL_GetError());
#else
    throw std::runtime_error("This renderer MVP requires Windows and Direct3D 11.");
#endif
    int w = 1, h = 1;
    if (!SDL_GetWindowSizeInPixels(window, &w, &h)) throw std::runtime_error(std::string("Unable to query window pixels: ") + SDL_GetError());
    impl_->windowWidth = static_cast<std::uint32_t>(std::max(1,w));
    impl_->windowHeight = static_cast<std::uint32_t>(std::max(1,h));
    bgfx::renderFrame(); // Single-thread rendering on the initialization thread.
    bgfx::Init init;
    init.type = bgfx::RendererType::Direct3D11;
    init.platformData.nwh = hwnd;
    init.callback = callback;
    init.resolution.width = impl_->windowWidth;
    init.resolution.height = impl_->windowHeight;
    init.resolution.reset = BGFX_RESET_VSYNC;
    if (!bgfx::init(init)) throw std::runtime_error("bgfx could not initialize Direct3D 11.");
    impl_->initialized = true;
    if (bgfx::getRendererType() != bgfx::RendererType::Direct3D11) throw std::runtime_error("Direct3D 11 was requested but another backend was selected.");
    if (!bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA16F,BGFX_TEXTURE_RT) ||
        !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_RT) ||
        !bgfx::isTextureValid(0,false,1,bgfx::TextureFormat::D24S8,BGFX_TEXTURE_RT | BGFX_TEXTURE_RT_WRITE_ONLY))
        throw std::runtime_error("This adapter cannot create the required viewport targets.");

    impl_->pbr = program(cc_vs_pbr,cc_fs_pbr,"Normal mapped GGX PBR with split-sum IBL");
    impl_->present = program(cc_vs_present,cc_fs_present,"Linear Reinhard tone map");
    impl_->gridProgram = program(cc_vs_grid,cc_fs_grid,"Quader analytic construction grid");
    impl_->encode = program(cc_vs_present,cc_fs_encode,"Viewport sRGB encoding");
    impl_->baseColorMetallic = uniform("u_baseColorMetallic");
    impl_->roughness = uniform("u_roughness");
    impl_->textureFlags = uniform("u_textureFlags");
    impl_->cameraPos = uniform("u_cameraPos");
    impl_->direction = uniform("u_direction");
    impl_->directionColor = uniform("u_directionColor");
    impl_->pointPosition = uniform("u_pointPosition",bgfx::UniformType::Vec4,2);
    impl_->pointColor = uniform("u_pointColor",bgfx::UniformType::Vec4,2);
    impl_->presentParams = uniform("u_presentParams");
    impl_->hdrSampler = uniform("s_hdrColor",bgfx::UniformType::Sampler);
    impl_->compositeSampler = uniform("s_compositeColor",bgfx::UniformType::Sampler);
    impl_->albedoSampler = uniform("s_albedo",bgfx::UniformType::Sampler);
    impl_->roughnessSampler = uniform("s_roughness",bgfx::UniformType::Sampler);
    impl_->metallicSampler = uniform("s_metallic",bgfx::UniformType::Sampler);
    impl_->normalSampler = uniform("s_normal",bgfx::UniformType::Sampler);
    impl_->irradianceSampler = uniform("s_irradiance",bgfx::UniformType::Sampler);
    impl_->prefilteredSampler = uniform("s_prefiltered",bgfx::UniformType::Sampler);
    impl_->brdfSampler = uniform("s_brdfLut",bgfx::UniformType::Sampler);
    impl_->environmentParams = uniform("u_environment");
    impl_->environment = std::make_unique<EnvironmentLighting>();
    impl_->gridMinor = uniform("u_gridMinor");
    impl_->gridMajor = uniform("u_gridMajor");
    impl_->gridAxisU = uniform("u_gridAxisU");
    impl_->gridAxisV = uniform("u_gridAxisV");
    impl_->gridParams = uniform("u_gridParams");
    constexpr std::uint8_t whitePixel[]{255,255,255,255};
    impl_->white = bgfx::createTexture2D(1,1,false,1,bgfx::TextureFormat::RGBA8,BGFX_TEXTURE_NONE,bgfx::copy(whitePixel,sizeof whitePixel));
    if (!bgfx::isValid(impl_->white)) throw std::runtime_error("Failed to create material fallback texture.");
    impl_->sphere.upload(makeUvSphere());
    impl_->fullscreen.upload({
        {{-1,-1,0,0,0,1,0,1}, {3,-1,0,0,0,1,2,1}, {-1,3,0,0,0,1,0,-1}}, {0,1,2}
    });
    impl_->grid.upload({
        {{-512,0,-512,0,1,0,0,0}, {512,0,-512,0,1,0,1,0},
         {512,0,512,0,1,0,1,1}, {-512,0,512,0,1,0,0,1}}, {0,2,1,0,3,2}
    });
    impl_->windowDrawable = w > 0 && h > 0;
    resizeViewport(impl_->windowWidth,impl_->windowHeight);
    bgfx::setViewName(SceneView,"00: PBR scene - linear HDR");
    bgfx::setViewName(PresentView,"01: Linear exposure and Reinhard");
    bgfx::setViewName(GridView,"02: Neutral analytic grid overlay");
    bgfx::setViewName(EncodeView,"03: Viewport sRGB image");
    const bgfx::ViewId order[]{SceneView,PresentView,GridView,EncodeView,ToolsView};
    bgfx::setViewOrder(SceneView,5,order);
    for (const auto id : order) bgfx::setViewMode(id,bgfx::ViewMode::Sequential);
}
Renderer::~Renderer() = default;

bool Renderer::prepareEnvironment(const std::filesystem::path& projectRoot, const std::string& sourceId) {
    return impl_->environment->prepare(projectRoot, sourceId);
}
const std::string& Renderer::environmentStatus() const { return impl_->environment->status(); }

void Renderer::resize(std::uint32_t width, std::uint32_t height) {
    if (!width || !height) { impl_->windowDrawable = false; return; }
    if (width != impl_->windowWidth || height != impl_->windowHeight) {
        bgfx::reset(width,height,BGFX_RESET_VSYNC);
        impl_->windowWidth = width; impl_->windowHeight = height;
    }
    impl_->windowDrawable = true;
}
void Renderer::resizeViewport(std::uint32_t width, std::uint32_t height) {
    if (!width || !height) { impl_->viewportDrawable = false; return; }
    if (impl_->targets && width == impl_->width && height == impl_->height) { impl_->viewportDrawable = true; return; }
    auto next = std::make_unique<ViewportTargets>();
    next->create(width,height); // Old targets survive allocation failure.
    impl_->targets = std::move(next);
    impl_->width = width; impl_->height = height; impl_->viewportDrawable = true;
}

void Renderer::render(const SceneSettings& settings, const MaterialTextures& textures) {
    if (!drawable()) return;
    const auto* caps = bgfx::getCaps();
    const auto w = static_cast<std::uint16_t>(impl_->width), h = static_cast<std::uint16_t>(impl_->height);
    bgfx::setViewFrameBuffer(SceneView,impl_->targets->hdrBuffer);
    bgfx::setViewRect(SceneView,0,0,w,h);
    bgfx::setViewClear(SceneView,BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,0x030405ffu,1.0f,0);
    float view[16], projection[16], model[16];
    const auto eye = settings.camera.eye(), forward = settings.camera.forward(), right = settings.camera.right();
    const Vec3 up{right[1]*forward[2]-right[2]*forward[1],right[2]*forward[0]-right[0]*forward[2],right[0]*forward[1]-right[1]*forward[0]};
    bx::mtxLookAt(view,{eye[0],eye[1],eye[2]}, {eye[0]+forward[0],eye[1]+forward[1],eye[2]+forward[2]}, {up[0],up[1],up[2]},bx::Handedness::Right);
    const float aspect = static_cast<float>(w)/static_cast<float>(h);
    bx::mtxProj(projection,settings.camera.verticalFov(aspect),aspect,0.01f,2048.0f,caps->homogeneousDepth,bx::Handedness::Right);
    bgfx::setViewTransform(SceneView,view,projection);
    bx::mtxTranslate(model,0,1,0); // Radius-one sphere rests on the ground plane.
    bgfx::setTransform(model);

    const auto& m = settings.material;
    const float base[]{toLinear(m.baseColor[0]),toLinear(m.baseColor[1]),toLinear(m.baseColor[2]),bounded(m.metallic,0,0,1)};
    const float rough[]{bounded(m.roughness,.35f,.045f,1),bounded(m.normalStrength,1,0,2),0,0};
    const float camera[]{eye[0],eye[1],eye[2],0};
    const float mapFlags[]{bgfx::isValid(textures.albedo)?1.0f:0.0f,bgfx::isValid(textures.roughness)?1.0f:0.0f,bgfx::isValid(textures.metallic)?1.0f:0.0f,bgfx::isValid(textures.normal)?1.0f:0.0f};
    const float environment[]{settings.environment.enabled ? bounded(settings.environment.intensity,1,0,10) : 0,
        bounded(settings.environment.rotation,0,-180,180) * std::numbers::pi_v<float> / 180.0f,
        impl_->environment->maxSpecularLod(),0};
    const auto& d = settings.directional;
    float dir[]{bounded(d.direction[0],-.5f,-1000,1000),bounded(d.direction[1],-.8f,-1000,1000),bounded(d.direction[2],-.6f,-1000,1000),
        d.enabled?bounded(d.intensity,0,0,10000):0};
    if (dir[0]*dir[0]+dir[1]*dir[1]+dir[2]*dir[2] < 1e-8f) {dir[0]=-.5f;dir[1]=-.8f;dir[2]=-.6f;}
    const float dirColor[]{toLinear(d.color[0]),toLinear(d.color[1]),toLinear(d.color[2]),0};
    float pointPos[8]{},pointColors[8]{};
    for (std::size_t i=0;i<settings.points.size();++i) {
        const auto& p=settings.points[i];
        for (std::size_t j=0;j<3;++j) {pointPos[i*4+j]=bounded(p.position[j],0,-1000,1000);pointColors[i*4+j]=toLinear(p.color[j]);}
        pointPos[i*4+3]=p.enabled?bounded(p.intensity,0,0,10000):0;
    }
    bgfx::setUniform(impl_->baseColorMetallic,base); bgfx::setUniform(impl_->roughness,rough); bgfx::setUniform(impl_->textureFlags,mapFlags);
    bgfx::setUniform(impl_->cameraPos,camera); bgfx::setUniform(impl_->direction,dir); bgfx::setUniform(impl_->directionColor,dirColor);
    bgfx::setUniform(impl_->pointPosition,pointPos,2); bgfx::setUniform(impl_->pointColor,pointColors,2);
    bgfx::setUniform(impl_->environmentParams,environment);
    bgfx::setTexture(0,impl_->albedoSampler,bgfx::isValid(textures.albedo)?textures.albedo:impl_->white);
    bgfx::setTexture(1,impl_->roughnessSampler,bgfx::isValid(textures.roughness)?textures.roughness:impl_->white);
    bgfx::setTexture(2,impl_->metallicSampler,bgfx::isValid(textures.metallic)?textures.metallic:impl_->white);
    bgfx::setTexture(3,impl_->normalSampler,bgfx::isValid(textures.normal)?textures.normal:impl_->white);
    bgfx::setTexture(4,impl_->irradianceSampler,impl_->environment->diffuse());
    bgfx::setTexture(5,impl_->prefilteredSampler,impl_->environment->specular());
    bgfx::setTexture(6,impl_->brdfSampler,impl_->environment->brdf());
    impl_->sphere.bind();
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_CULL_CW);
    bgfx::submit(SceneView,impl_->pbr);

    const float params[]{bounded(settings.exposure,1,0,100),caps->originBottomLeft?1.0f:0.0f,0,0};
    bgfx::setViewFrameBuffer(PresentView,impl_->targets->compositeBuffer);
    bgfx::setViewRect(PresentView,0,0,w,h);
    bgfx::setViewClear(PresentView,BGFX_CLEAR_NONE);
    bgfx::setViewTransform(PresentView,nullptr,nullptr);
    bgfx::setUniform(impl_->presentParams,params);
    bgfx::setTexture(0,impl_->hdrSampler,impl_->targets->hdr);
    impl_->fullscreen.bind(); bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(PresentView,impl_->present);

    bgfx::setViewFrameBuffer(GridView,impl_->targets->compositeBuffer);
    bgfx::setViewRect(GridView,0,0,w,h);
    bgfx::setViewClear(GridView,BGFX_CLEAR_NONE); // Preserve scene depth and the tone-mapped color.
    bgfx::setViewTransform(GridView,view,projection);
    bx::mtxIdentity(model); bgfx::setTransform(model);
    const float minor[]{toLinear(76.0f/255),toLinear(76.0f/255),toLinear(76.0f/255),1};
    const float major[]{toLinear(114.0f/255),toLinear(114.0f/255),toLinear(114.0f/255),1};
    const float axisU[]{toLinear(1),toLinear(.239f),toLinear(0),.72f};
    const float axisV[]{toLinear(.059f),toLinear(.612f),toLinear(1),.72f};
    const float gridParams[]{1,1,.005f,caps->homogeneousDepth?1.0f:0.0f};
    bgfx::setUniform(impl_->gridMinor,minor); bgfx::setUniform(impl_->gridMajor,major);
    bgfx::setUniform(impl_->gridAxisU,axisU); bgfx::setUniform(impl_->gridAxisV,axisV); bgfx::setUniform(impl_->gridParams,gridParams);
    impl_->grid.bind();
    bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LEQUAL |
        BGFX_STATE_BLEND_FUNC(BGFX_STATE_BLEND_ONE,BGFX_STATE_BLEND_INV_SRC_ALPHA));
    bgfx::submit(GridView,impl_->gridProgram);

    bgfx::setViewFrameBuffer(EncodeView,impl_->targets->displayBuffer);
    bgfx::setViewRect(EncodeView,0,0,w,h);
    bgfx::setViewClear(EncodeView,BGFX_CLEAR_NONE);
    bgfx::setViewTransform(EncodeView,nullptr,nullptr);
    bgfx::setUniform(impl_->presentParams,params);
    bgfx::setTexture(0,impl_->compositeSampler,impl_->targets->composite);
    impl_->fullscreen.bind(); bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
    bgfx::submit(EncodeView,impl_->encode);
}
bgfx::TextureHandle Renderer::viewportTexture() const noexcept {
    if (impl_->targets) return impl_->targets->display;
    return BGFX_INVALID_HANDLE;
}
std::uint32_t Renderer::width() const noexcept {return impl_->width;}
std::uint32_t Renderer::height() const noexcept {return impl_->height;}
bool Renderer::drawable() const noexcept {return impl_->windowDrawable && impl_->viewportDrawable && impl_->targets != nullptr;}
const char* Renderer::backendName() const noexcept {return bgfx::getRendererName(bgfx::getRendererType());}

} // namespace crimson
