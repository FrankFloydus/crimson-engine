module;

#include <backend/BufferDescriptor.h>
#include <backend/DriverEnums.h>
#include <backend/PixelBufferDescriptor.h>
#include <filament/Box.h>
#include <filament/Camera.h>
#include <filament/Color.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/LightManager.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/RenderTarget.h>
#include <filament/Renderer.h>
#include <filament/Scene.h>
#include <filament/Skybox.h>
#include <filament/Texture.h>
#include <filament/TransformManager.h>
#include <filament/VertexBuffer.h>
#include <filament/View.h>
#include <filament/Viewport.h>
#include <geometry/SurfaceOrientation.h>
#include <math/mat4.h>
#include <math/vec2.h>
#include <math/vec3.h>
#include <math/vec4.h>
#include <utils/EntityManager.h>
#include <tracy/Tracy.hpp>

#include "generated/QuaderLitPbrMaterial.inc"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <stdexcept>
#include <vector>

module quader.filament.preview_scene;

import quader.math.types;

namespace quader {
namespace {

struct CubeVertex {
    filament::math::float3 position;
    filament::math::short4 tangent_frame;
};

struct CubeSourceVertex {
    filament::math::float3 position;
    filament::math::float3 normal;
};

template<typename Value>
filament::backend::BufferDescriptor make_filament_buffer_copy(std::span<const Value> values)
{
    const size_t byte_count = values.size_bytes();
    auto* bytes = new std::byte[byte_count];
    std::memcpy(bytes, values.data(), byte_count);

    return filament::backend::BufferDescriptor(
        bytes,
        byte_count,
        [](void* buffer, size_t, void*) {
            delete[] static_cast<std::byte*>(buffer);
        });
}

template<typename Value, size_t Size>
filament::backend::BufferDescriptor make_filament_buffer_copy(const std::array<Value, Size>& values)
{
    return make_filament_buffer_copy<Value>(std::span<const Value>(values.data(), values.size()));
}

filament::math::float3 to_filament_color(LinearColor color)
{
    return filament::math::float3 { color.r, color.g, color.b };
}

filament::math::float4 to_filament_premultiplied_color(LinearColor color, float alpha)
{
    const LinearRgba rgba = premultiplied_linear_rgba(color, alpha);
    return filament::math::float4 { rgba.r, rgba.g, rgba.b, rgba.a };
}

filament::math::float3 to_filament_direction(const DirectionalLightSettings& settings)
{
    const Vec3 direction = normalized_direction_from_yaw_pitch(settings.yaw_degrees, settings.pitch_degrees);
    return filament::math::float3 { direction.x, direction.y, direction.z };
}

std::array<CubeSourceVertex, 24> cube_source_vertices()
{
    constexpr float half_extent = 1.0f;

    return {
        CubeSourceVertex { { -half_extent, -half_extent, half_extent }, { 0.0f, 0.0f, 1.0f } },
        CubeSourceVertex { { half_extent, -half_extent, half_extent }, { 0.0f, 0.0f, 1.0f } },
        CubeSourceVertex { { half_extent, half_extent, half_extent }, { 0.0f, 0.0f, 1.0f } },
        CubeSourceVertex { { -half_extent, half_extent, half_extent }, { 0.0f, 0.0f, 1.0f } },

        CubeSourceVertex { { half_extent, -half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f } },
        CubeSourceVertex { { -half_extent, -half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f } },
        CubeSourceVertex { { -half_extent, half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f } },
        CubeSourceVertex { { half_extent, half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f } },

        CubeSourceVertex { { -half_extent, -half_extent, -half_extent }, { -1.0f, 0.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, -half_extent, half_extent }, { -1.0f, 0.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, half_extent, half_extent }, { -1.0f, 0.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, half_extent, -half_extent }, { -1.0f, 0.0f, 0.0f } },

        CubeSourceVertex { { half_extent, -half_extent, half_extent }, { 1.0f, 0.0f, 0.0f } },
        CubeSourceVertex { { half_extent, -half_extent, -half_extent }, { 1.0f, 0.0f, 0.0f } },
        CubeSourceVertex { { half_extent, half_extent, -half_extent }, { 1.0f, 0.0f, 0.0f } },
        CubeSourceVertex { { half_extent, half_extent, half_extent }, { 1.0f, 0.0f, 0.0f } },

        CubeSourceVertex { { -half_extent, half_extent, half_extent }, { 0.0f, 1.0f, 0.0f } },
        CubeSourceVertex { { half_extent, half_extent, half_extent }, { 0.0f, 1.0f, 0.0f } },
        CubeSourceVertex { { half_extent, half_extent, -half_extent }, { 0.0f, 1.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, half_extent, -half_extent }, { 0.0f, 1.0f, 0.0f } },

        CubeSourceVertex { { -half_extent, -half_extent, -half_extent }, { 0.0f, -1.0f, 0.0f } },
        CubeSourceVertex { { half_extent, -half_extent, -half_extent }, { 0.0f, -1.0f, 0.0f } },
        CubeSourceVertex { { half_extent, -half_extent, half_extent }, { 0.0f, -1.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, -half_extent, half_extent }, { 0.0f, -1.0f, 0.0f } },
    };
}

std::array<uint16_t, 36> cube_index_data()
{
    return {
        0, 1, 2, 0, 2, 3,
        4, 5, 6, 4, 6, 7,
        8, 9, 10, 8, 10, 11,
        12, 13, 14, 12, 14, 15,
        16, 17, 18, 16, 18, 19,
        20, 21, 22, 20, 22, 23,
    };
}

std::array<filament::math::ushort3, 12> cube_orientation_triangles()
{
    return {
        filament::math::ushort3 { 0, 1, 2 },
        filament::math::ushort3 { 0, 2, 3 },
        filament::math::ushort3 { 4, 5, 6 },
        filament::math::ushort3 { 4, 6, 7 },
        filament::math::ushort3 { 8, 9, 10 },
        filament::math::ushort3 { 8, 10, 11 },
        filament::math::ushort3 { 12, 13, 14 },
        filament::math::ushort3 { 12, 14, 15 },
        filament::math::ushort3 { 16, 17, 18 },
        filament::math::ushort3 { 16, 18, 19 },
        filament::math::ushort3 { 20, 21, 22 },
        filament::math::ushort3 { 20, 22, 23 },
    };
}

std::vector<CubeVertex> build_cube_vertices()
{
    const std::array<CubeSourceVertex, 24> source_vertices = cube_source_vertices();
    const std::array<filament::math::ushort3, 12> triangles = cube_orientation_triangles();

    std::array<filament::math::float3, 24> positions {};
    std::array<filament::math::float3, 24> normals {};
    for (size_t index = 0; index < source_vertices.size(); ++index) {
        positions[index] = source_vertices[index].position;
        normals[index] = source_vertices[index].normal;
    }

    std::unique_ptr<filament::geometry::SurfaceOrientation> orientation(
        filament::geometry::SurfaceOrientation::Builder()
            .vertexCount(source_vertices.size())
            .positions(positions.data())
            .normals(normals.data())
            .triangleCount(triangles.size())
            .triangles(triangles.data())
            .build());

    if (!orientation) {
        throw std::runtime_error("Filament failed to generate cube tangent frames.");
    }

    std::array<filament::math::short4, 24> tangent_frames {};
    orientation->getQuats(tangent_frames.data(), tangent_frames.size());

    std::vector<CubeVertex> vertices;
    vertices.reserve(source_vertices.size());
    for (size_t index = 0; index < source_vertices.size(); ++index) {
        vertices.push_back(CubeVertex {
            source_vertices[index].position,
            tangent_frames[index],
        });
    }

    return vertices;
}

} // namespace

struct FilamentPreviewScene::Resources {
    explicit Resources(filament::Engine& engine_ref)
        : engine(engine_ref)
    {
    }

    Resources(const Resources&) = delete;
    Resources& operator=(const Resources&) = delete;

    ~Resources()
    {
        engine.flushAndWait();

        if (scene != nullptr) {
            if (cube_entity) {
                scene->remove(cube_entity);
            }
            if (light_entity) {
                scene->remove(light_entity);
            }
        }

        if (cube_entity) {
            engine.getRenderableManager().destroy(cube_entity);
            engine.getTransformManager().destroy(cube_entity);
            utils::EntityManager::get().destroy(cube_entity);
        }

        if (light_entity) {
            engine.getLightManager().destroy(light_entity);
            utils::EntityManager::get().destroy(light_entity);
        }

        if (camera_entity) {
            engine.destroyCameraComponent(camera_entity);
            utils::EntityManager::get().destroy(camera_entity);
        }

        if (skybox != nullptr) {
            engine.destroy(skybox);
        }

        if (material_instance != nullptr) {
            engine.destroy(material_instance);
        }

        if (material != nullptr) {
            engine.destroy(material);
        }

        if (cube_indices != nullptr) {
            engine.destroy(cube_indices);
        }

        if (cube_vertices != nullptr) {
            engine.destroy(cube_vertices);
        }

        destroy_preview_target();

        if (view != nullptr) {
            engine.destroy(view);
        }

        if (scene != nullptr) {
            engine.destroy(scene);
        }

        if (renderer != nullptr) {
            engine.destroy(renderer);
        }
    }

    void initialize(const PbrMaterialSettings& material_settings, const DirectionalLightSettings& light_settings)
    {
        renderer = engine.createRenderer();
        scene = engine.createScene();
        view = engine.createView();

        if (renderer == nullptr || scene == nullptr || view == nullptr) {
            throw std::runtime_error("Filament failed to allocate renderer, scene, or view for the preview.");
        }

        view->setScene(scene);
        view->setPostProcessingEnabled(true);
        view->setAntiAliasing(filament::View::AntiAliasing::FXAA);

        filament::Renderer::ClearOptions clear_options;
        clear_options.clear = true;
        clear_options.clearColor = { 0.055, 0.060, 0.070, 1.0 };
        renderer->setClearOptions(clear_options);

        skybox = filament::Skybox::Builder()
            .color({ 0.055f, 0.060f, 0.070f, 1.0f })
            .build(engine);
        scene->setSkybox(skybox);

        create_camera();
        create_cube(material_settings);
        create_directional_light(light_settings);
    }

    void create_camera()
    {
        camera_entity = utils::EntityManager::get().create();
        camera = engine.createCamera(camera_entity);
        view->setCamera(camera);
        camera->lookAt(
            filament::math::double3 { 3.0, 2.1, 3.5 },
            filament::math::double3 { 0.0, 0.05, 0.0 },
            filament::math::double3 { 0.0, 1.0, 0.0 });
    }

    void create_cube(const PbrMaterialSettings& settings)
    {
        const std::vector<CubeVertex> vertices = build_cube_vertices();
        const std::array<uint16_t, 36> indices = cube_index_data();

        cube_vertices = filament::VertexBuffer::Builder()
            .vertexCount(static_cast<uint32_t>(vertices.size()))
            .bufferCount(1)
            .attribute(
                filament::VertexAttribute::POSITION,
                0,
                filament::VertexBuffer::AttributeType::FLOAT3,
                offsetof(CubeVertex, position),
                sizeof(CubeVertex))
            .attribute(
                filament::VertexAttribute::TANGENTS,
                0,
                filament::VertexBuffer::AttributeType::SHORT4,
                offsetof(CubeVertex, tangent_frame),
                sizeof(CubeVertex))
            .normalized(filament::VertexAttribute::TANGENTS)
            .build(engine);

        cube_vertices->setBufferAt(engine, 0, make_filament_buffer_copy<CubeVertex>(vertices));

        cube_indices = filament::IndexBuffer::Builder()
            .indexCount(static_cast<uint32_t>(indices.size()))
            .bufferType(filament::IndexBuffer::IndexType::USHORT)
            .build(engine);

        cube_indices->setBuffer(engine, make_filament_buffer_copy<uint16_t>(indices));

        material = filament::Material::Builder()
            .package(
                filament_assets::quader_lit_pbr_material,
                filament_assets::quader_lit_pbr_material_size)
            .build(engine);
        material_instance = material->createInstance("Quader cube material");
        apply_material_settings(settings);

        cube_entity = utils::EntityManager::get().create();
        filament::RenderableManager::Builder(1)
            .boundingBox({ { 0.0f, 0.0f, 0.0f }, { 1.75f, 1.75f, 1.75f } })
            .geometry(
                0,
                filament::RenderableManager::PrimitiveType::TRIANGLES,
                cube_vertices,
                cube_indices,
                0,
                indices.size())
            .material(0, material_instance)
            .castShadows(false)
            .receiveShadows(false)
            .build(engine, cube_entity);

        engine.getTransformManager().create(cube_entity);
        engine.getTransformManager().setTransform(
            engine.getTransformManager().getInstance(cube_entity),
            filament::math::mat4f());
        scene->addEntity(cube_entity);
    }

    void create_directional_light(const DirectionalLightSettings& settings)
    {
        light_entity = utils::EntityManager::get().create();
        filament::LightManager::Builder(filament::LightManager::Type::DIRECTIONAL)
            .color(to_filament_color(settings.color))
            .intensity(settings.intensity)
            .direction(to_filament_direction(settings))
            .castShadows(false)
            .build(engine, light_entity);
        scene->addEntity(light_entity);
    }

    void apply_material_settings(PbrMaterialSettings settings) const
    {
        settings = clamp_pbr_material_settings(settings);
        material_instance->setParameter(
            "albedo",
            filament::RgbaType::PREMULTIPLIED_LINEAR,
            to_filament_premultiplied_color(settings.albedo, 1.0f));
        material_instance->setParameter("roughness", settings.roughness);
        material_instance->setParameter("metallic", settings.metallic);
        material_instance->setParameter("reflectance", settings.reflectance);
        material_instance->setParameter("specularFactor", settings.specular_factor);
        material_instance->setParameter(
            "specularColorFactor",
            filament::RgbType::LINEAR,
            to_filament_color(settings.specular_color_factor));
        material_instance->setParameter("clearCoat", settings.clear_coat);
        material_instance->setParameter("clearCoatRoughness", settings.clear_coat_roughness);
    }

    void apply_directional_light_settings(DirectionalLightSettings settings) const
    {
        settings = clamp_directional_light_settings(settings);
        filament::LightManager& lights = engine.getLightManager();
        const filament::LightManager::Instance light_instance = lights.getInstance(light_entity);
        lights.setColor(light_instance, to_filament_color(settings.color));
        lights.setIntensity(light_instance, settings.intensity);
        lights.setDirection(light_instance, to_filament_direction(settings));
    }

    void ensure_preview_size(RenderExtent extent)
    {
        if (!extent.is_valid()) {
            return;
        }

        if (preview_target != nullptr &&
            preview_extent.width == extent.width &&
            preview_extent.height == extent.height) {
            return;
        }

        engine.flushAndWait();
        destroy_preview_target();

        const filament::Texture::Usage color_usage =
            filament::Texture::Usage::COLOR_ATTACHMENT |
            filament::Texture::Usage::SAMPLEABLE |
            filament::Texture::Usage::BLIT_SRC;
        preview_color = filament::Texture::Builder()
            .width(extent.width)
            .height(extent.height)
            .levels(1)
            .usage(color_usage)
            .format(filament::Texture::InternalFormat::RGBA8)
            .build(engine);

        preview_depth = filament::Texture::Builder()
            .width(extent.width)
            .height(extent.height)
            .levels(1)
            .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
            .format(filament::Texture::InternalFormat::DEPTH24)
            .build(engine);

        preview_target = filament::RenderTarget::Builder()
            .texture(filament::RenderTarget::AttachmentPoint::COLOR, preview_color)
            .texture(filament::RenderTarget::AttachmentPoint::DEPTH, preview_depth)
            .build(engine);

        preview_extent = extent;
        view->setViewport(filament::Viewport { 0, 0, extent.width, extent.height });
        view->setRenderTarget(preview_target);
        camera->setProjection(52.0, static_cast<double>(extent.width) / static_cast<double>(extent.height), 0.1, 100.0);
    }

    void render_into(RenderExtent extent, RenderPreviewFrame& frame)
    {
        ZoneScopedN("Filament preview render");

        ensure_preview_size(extent);
        if (preview_target == nullptr) {
            return;
        }

        if (frame.extent.width != extent.width || frame.extent.height != extent.height) {
            frame.extent = extent;
            frame.rgba.assign(static_cast<size_t>(extent.width) * extent.height * 4, 0);
        }

        renderer->renderStandaloneView(view);
        renderer->readPixels(
            preview_target,
            0,
            0,
            frame.extent.width,
            frame.extent.height,
            filament::backend::PixelBufferDescriptor(
                frame.rgba.data(),
                frame.rgba.size(),
                filament::backend::PixelDataFormat::RGBA,
                filament::backend::PixelDataType::UBYTE));
        engine.flushAndWait();
    }

    void destroy_preview_target()
    {
        if (view != nullptr) {
            view->setRenderTarget(nullptr);
        }

        if (preview_target != nullptr) {
            engine.destroy(preview_target);
            preview_target = nullptr;
        }

        if (preview_depth != nullptr) {
            engine.destroy(preview_depth);
            preview_depth = nullptr;
        }

        if (preview_color != nullptr) {
            engine.destroy(preview_color);
            preview_color = nullptr;
        }

        preview_extent = {};
    }

    filament::Engine& engine;
    filament::Renderer* renderer = nullptr;
    filament::Scene* scene = nullptr;
    filament::View* view = nullptr;
    filament::Texture* preview_color = nullptr;
    filament::Texture* preview_depth = nullptr;
    filament::RenderTarget* preview_target = nullptr;
    filament::VertexBuffer* cube_vertices = nullptr;
    filament::IndexBuffer* cube_indices = nullptr;
    filament::Material* material = nullptr;
    filament::MaterialInstance* material_instance = nullptr;
    filament::Skybox* skybox = nullptr;
    filament::Camera* camera = nullptr;
    utils::Entity camera_entity;
    utils::Entity cube_entity;
    utils::Entity light_entity;
    RenderExtent preview_extent;
};

FilamentPreviewScene::FilamentPreviewScene(
    filament::Engine& engine,
    const PbrMaterialSettings& material_settings,
    const DirectionalLightSettings& light_settings)
    : resources_(std::make_unique<Resources>(engine))
{
    resources_->initialize(material_settings, light_settings);
}

FilamentPreviewScene::~FilamentPreviewScene() = default;

void FilamentPreviewScene::apply_material_settings(const PbrMaterialSettings& settings)
{
    resources_->apply_material_settings(settings);
}

void FilamentPreviewScene::apply_directional_light_settings(const DirectionalLightSettings& settings)
{
    resources_->apply_directional_light_settings(settings);
}

void FilamentPreviewScene::render_into(RenderExtent extent, RenderPreviewFrame& frame)
{
    resources_->render_into(extent, frame);
}

} // namespace quader
