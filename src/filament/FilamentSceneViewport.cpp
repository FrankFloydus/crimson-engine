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
#include <filament/TextureSampler.h>
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

#include "diagnostics/LogMacros.h"
#include "generated/QuaderLitPbrMaterial.inc"

#include <array>
#include <cmath>
#include <exception>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <variant>
#include <vector>

module quader.filament.scene_viewport;

import quader.assets.image;
import quader.diagnostics.logger;
import quader.math.types;

namespace quader {
namespace {

struct CubeVertex {
    filament::math::float3 position;
    filament::math::short4 tangent_frame;
    filament::math::float2 uv0;
};

struct CubeSourceVertex {
    filament::math::float3 position;
    filament::math::float3 normal;
    filament::math::float2 uv0;
};

enum class PreviewTextureEncoding : uint8_t {
    srgb_rgba,
    linear_red,
};

struct MaterialTextureBinding {
    filament::Texture* texture = nullptr;
    bool use_source_texture = false;
};

struct CachedMaterialTexture {
    filament::Texture* texture = nullptr;
};

struct CubeRenderable {
    utils::Entity entity;
    filament::MaterialInstance* material_instance = nullptr;
};

struct DirectionalLightRenderable {
    utils::Entity entity;
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

filament::backend::PixelBufferDescriptor make_filament_pixel_buffer_copy(
    std::span<const uint8_t> values,
    filament::backend::PixelDataFormat format)
{
    const size_t byte_count = values.size_bytes();
    auto* bytes = new std::byte[byte_count];
    std::memcpy(bytes, values.data(), byte_count);

    return filament::backend::PixelBufferDescriptor(
        bytes,
        byte_count,
        format,
        filament::backend::PixelDataType::UBYTE,
        1,
        0,
        0,
        0,
        [](void* buffer, size_t, void*) {
            delete[] static_cast<std::byte*>(buffer);
        });
}

template<size_t Size>
filament::backend::PixelBufferDescriptor make_filament_pixel_buffer_copy(
    const std::array<uint8_t, Size>& values,
    filament::backend::PixelDataFormat format)
{
    return make_filament_pixel_buffer_copy(std::span<const uint8_t>(values.data(), values.size()), format);
}

template<typename Pointer>
Pointer require_filament_allocation(Pointer pointer, const char* resource_name)
{
    if (pointer == nullptr) {
        throw std::runtime_error("Filament failed to allocate " + std::string(resource_name) + ".");
    }

    return pointer;
}

filament::math::float3 to_filament_color(LinearColor color)
{
    return filament::math::float3 { color.r, color.g, color.b };
}

filament::math::float3 to_filament_float3(Vec3 value)
{
    return filament::math::float3 { value.x, value.y, value.z };
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

filament::math::double3 to_filament_point(Vec3 position)
{
    return filament::math::double3 { position.x, position.y, position.z };
}

filament::math::double3 to_filament_direction(Vec3 direction)
{
    return filament::math::double3 { direction.x, direction.y, direction.z };
}

filament::math::double3 to_filament_up(Vec3 up)
{
    return filament::math::double3 { up.x, up.y, up.z };
}

float degrees_to_radians(float degrees)
{
    constexpr float pi = 3.14159265358979323846f;
    return degrees * pi / 180.0f;
}

filament::math::mat4f scene_mesh_transform(const SceneRenderMesh& mesh)
{
    const filament::math::mat4f translation =
        filament::math::mat4f::translation(to_filament_float3(mesh.position));
    const filament::math::mat4f rotation_x =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.x), filament::math::float3 { 1.0f, 0.0f, 0.0f });
    const filament::math::mat4f rotation_y =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.y), filament::math::float3 { 0.0f, 1.0f, 0.0f });
    const filament::math::mat4f rotation_z =
        filament::math::mat4f::rotation(degrees_to_radians(mesh.rotation_degrees.z), filament::math::float3 { 0.0f, 0.0f, 1.0f });
    const filament::math::mat4f scale =
        filament::math::mat4f::scaling(to_filament_float3(mesh.scale));

    return translation * rotation_z * rotation_y * rotation_x * scale;
}

const char* texture_slot_name(PbrMaterialTextureSlot slot)
{
    switch (slot) {
    case PbrMaterialTextureSlot::albedo:
        return "albedo";
    case PbrMaterialTextureSlot::roughness:
        return "roughness";
    case PbrMaterialTextureSlot::metallic:
        return "metallic";
    }

    return "unknown";
}

std::string material_texture_cache_key(
    std::string_view source_path,
    PbrMaterialTextureSlot slot,
    PreviewTextureEncoding encoding)
{
    return std::to_string(static_cast<uint32_t>(slot)) + "|" +
        std::to_string(static_cast<uint32_t>(encoding)) + "|" +
        std::string(source_path);
}

std::array<CubeSourceVertex, 24> cube_source_vertices()
{
    constexpr float half_extent = 1.0f;

    return {
        CubeSourceVertex { { -half_extent, -half_extent, half_extent }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 0.0f } },
        CubeSourceVertex { { half_extent, -half_extent, half_extent }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 0.0f } },
        CubeSourceVertex { { half_extent, half_extent, half_extent }, { 0.0f, 0.0f, 1.0f }, { 1.0f, 1.0f } },
        CubeSourceVertex { { -half_extent, half_extent, half_extent }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f } },

        CubeSourceVertex { { half_extent, -half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, -half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f }, { 1.0f, 1.0f } },
        CubeSourceVertex { { half_extent, half_extent, -half_extent }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f } },

        CubeSourceVertex { { -half_extent, -half_extent, -half_extent }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, -half_extent, half_extent }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
        CubeSourceVertex { { -half_extent, half_extent, half_extent }, { -1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } },
        CubeSourceVertex { { -half_extent, half_extent, -half_extent }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } },

        CubeSourceVertex { { half_extent, -half_extent, half_extent }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f } },
        CubeSourceVertex { { half_extent, -half_extent, -half_extent }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 0.0f } },
        CubeSourceVertex { { half_extent, half_extent, -half_extent }, { 1.0f, 0.0f, 0.0f }, { 1.0f, 1.0f } },
        CubeSourceVertex { { half_extent, half_extent, half_extent }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f } },

        CubeSourceVertex { { -half_extent, half_extent, half_extent }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f } },
        CubeSourceVertex { { half_extent, half_extent, half_extent }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 0.0f } },
        CubeSourceVertex { { half_extent, half_extent, -half_extent }, { 0.0f, 1.0f, 0.0f }, { 1.0f, 1.0f } },
        CubeSourceVertex { { -half_extent, half_extent, -half_extent }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 1.0f } },

        CubeSourceVertex { { -half_extent, -half_extent, -half_extent }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 0.0f } },
        CubeSourceVertex { { half_extent, -half_extent, -half_extent }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 0.0f } },
        CubeSourceVertex { { half_extent, -half_extent, half_extent }, { 0.0f, -1.0f, 0.0f }, { 1.0f, 1.0f } },
        CubeSourceVertex { { -half_extent, -half_extent, half_extent }, { 0.0f, -1.0f, 0.0f }, { 0.0f, 1.0f } },
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
            source_vertices[index].uv0,
        });
    }

    return vertices;
}

} // namespace

struct FilamentSceneViewport::Resources {
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
            for (const CubeRenderable& cube : cubes) {
                if (cube.entity) {
                    scene->remove(cube.entity);
                }
            }
            for (const DirectionalLightRenderable& light : directional_lights) {
                if (light.entity) {
                    scene->remove(light.entity);
                }
            }
        }

        for (CubeRenderable& cube : cubes) {
            destroy_cube_renderable(cube);
        }
        cubes.clear();

        for (DirectionalLightRenderable& light : directional_lights) {
            destroy_directional_light_renderable(light);
        }
        directional_lights.clear();

        if (camera_entity) {
            engine.destroyCameraComponent(camera_entity);
            utils::EntityManager::get().destroy(camera_entity);
        }

        if (skybox != nullptr) {
            engine.destroy(skybox);
        }

        if (material != nullptr) {
            engine.destroy(material);
        }

        destroy_material_textures();

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

    void initialize()
    {
        renderer = require_filament_allocation(engine.createRenderer(), "viewport renderer");
        scene = require_filament_allocation(engine.createScene(), "viewport scene");
        view = require_filament_allocation(engine.createView(), "viewport view");

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
        skybox = require_filament_allocation(skybox, "viewport skybox");
        scene->setSkybox(skybox);

        create_camera();
    }

    void create_camera()
    {
        camera_entity = utils::EntityManager::get().create();
        camera = require_filament_allocation(engine.createCamera(camera_entity), "viewport camera");
        view->setCamera(camera);
    }

    filament::Texture* create_material_texture(
        uint32_t width,
        uint32_t height,
        filament::Texture::InternalFormat internal_format,
        filament::backend::PixelDataFormat pixel_format,
        std::span<const uint8_t> pixels)
    {
        filament::Texture* texture = filament::Texture::Builder()
            .width(width)
            .height(height)
            .levels(1)
            .sampler(filament::Texture::Sampler::SAMPLER_2D)
            .format(internal_format)
            .build(engine);
        texture = require_filament_allocation(texture, "material texture");

        texture->setImage(engine, 0, make_filament_pixel_buffer_copy(pixels, pixel_format));
        return texture;
    }

    filament::Texture* load_material_texture(
        std::string_view source_path,
        PreviewTextureEncoding encoding)
    {
        const ImageColorEncoding image_encoding =
            encoding == PreviewTextureEncoding::srgb_rgba
                ? ImageColorEncoding::srgb
                : ImageColorEncoding::linear;
        const Rgba8Image image = load_rgba8_image_from_file(source_path, image_encoding);
        if (!image.has_pixels()) {
            throw std::runtime_error("Image decoder returned no pixels for " + std::string(source_path));
        }

        if (encoding == PreviewTextureEncoding::srgb_rgba) {
            return create_material_texture(
                image.width,
                image.height,
                filament::Texture::InternalFormat::SRGB8_A8,
                filament::backend::PixelDataFormat::RGBA,
                std::span<const uint8_t>(image.rgba.data(), image.rgba.size()));
        }

        const size_t pixel_count = static_cast<size_t>(image.width) * static_cast<size_t>(image.height);
        std::vector<uint8_t> red_channel(pixel_count);
        for (size_t pixel_index = 0; pixel_index < pixel_count; ++pixel_index) {
            red_channel[pixel_index] = image.rgba[pixel_index * 4];
        }

        return create_material_texture(
            image.width,
            image.height,
            filament::Texture::InternalFormat::R8,
            filament::backend::PixelDataFormat::R,
            std::span<const uint8_t>(red_channel.data(), red_channel.size()));
    }

    void ensure_fallback_material_textures()
    {
        if (fallback_albedo_texture == nullptr) {
            constexpr std::array<uint8_t, 4> white_srgb_rgba { 255, 255, 255, 255 };
            fallback_albedo_texture = create_material_texture(
                1,
                1,
                filament::Texture::InternalFormat::SRGB8_A8,
                filament::backend::PixelDataFormat::RGBA,
                std::span<const uint8_t>(white_srgb_rgba.data(), white_srgb_rgba.size()));
        }

        if (fallback_scalar_texture == nullptr) {
            constexpr std::array<uint8_t, 1> white_linear_scalar { 255 };
            fallback_scalar_texture = create_material_texture(
                1,
                1,
                filament::Texture::InternalFormat::R8,
                filament::backend::PixelDataFormat::R,
                std::span<const uint8_t>(white_linear_scalar.data(), white_linear_scalar.size()));
        }
    }

    MaterialTextureBinding resolve_material_texture(
        const TextureAssetReference& reference,
        PbrMaterialTextureSlot slot,
        PreviewTextureEncoding encoding,
        filament::Texture* fallback_texture)
    {
        if (!reference.has_source()) {
            return MaterialTextureBinding { fallback_texture, false };
        }

        const std::string cache_key = material_texture_cache_key(reference.source_path, slot, encoding);
        auto [entry, inserted] = material_texture_cache.try_emplace(cache_key);
        if (inserted) {
            try {
                entry->second.texture = load_material_texture(reference.source_path, encoding);
            } catch (const std::exception& error) {
                QUADER_LOG_WARN(
                    "Failed to load {} material texture '{}': {}",
                    texture_slot_name(slot),
                    reference.source_path,
                    error.what());
            }
        }

        if (entry->second.texture == nullptr) {
            return MaterialTextureBinding { fallback_texture, false };
        }

        return MaterialTextureBinding { entry->second.texture, true };
    }

    void destroy_material_textures()
    {
        for (auto& [_, cached] : material_texture_cache) {
            if (cached.texture != nullptr) {
                engine.destroy(cached.texture);
                cached.texture = nullptr;
            }
        }
        material_texture_cache.clear();

        if (fallback_scalar_texture != nullptr) {
            engine.destroy(fallback_scalar_texture);
            fallback_scalar_texture = nullptr;
        }

        if (fallback_albedo_texture != nullptr) {
            engine.destroy(fallback_albedo_texture);
            fallback_albedo_texture = nullptr;
        }
    }

    void ensure_cube_resources()
    {
        if (cube_vertices != nullptr && cube_indices != nullptr && material != nullptr) {
            return;
        }

        if (cube_vertices == nullptr) {
            const std::vector<CubeVertex> vertices = build_cube_vertices();
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
                .attribute(
                    filament::VertexAttribute::UV0,
                    0,
                    filament::VertexBuffer::AttributeType::FLOAT2,
                    offsetof(CubeVertex, uv0),
                    sizeof(CubeVertex))
                .build(engine);
            cube_vertices = require_filament_allocation(cube_vertices, "viewport cube vertex buffer");

            cube_vertices->setBufferAt(engine, 0, make_filament_buffer_copy<CubeVertex>(vertices));
        }

        if (cube_indices == nullptr) {
            const std::array<uint16_t, 36> indices = cube_index_data();
            cube_indices = filament::IndexBuffer::Builder()
                .indexCount(static_cast<uint32_t>(indices.size()))
                .bufferType(filament::IndexBuffer::IndexType::USHORT)
                .build(engine);
            cube_indices = require_filament_allocation(cube_indices, "viewport cube index buffer");

            cube_indices->setBuffer(engine, make_filament_buffer_copy<uint16_t>(indices));
        }

        if (material == nullptr) {
            material = filament::Material::Builder()
                .package(
                    filament_assets::quader_lit_pbr_material,
                    filament_assets::quader_lit_pbr_material_size)
                .build(engine);
            material = require_filament_allocation(material, "viewport cube material");
        }
    }

    [[nodiscard]] CubeRenderable create_cube_renderable()
    {
        ensure_cube_resources();

        CubeRenderable cube;
        cube.entity = utils::EntityManager::get().create();
        cube.material_instance = material->createInstance("Quader cube material");
        cube.material_instance =
            require_filament_allocation(cube.material_instance, "viewport cube material instance");

        const std::array<uint16_t, 36> indices = cube_index_data();
        const filament::RenderableManager::Builder::Result renderable_result =
            filament::RenderableManager::Builder(1)
            .boundingBox({ { 0.0f, 0.0f, 0.0f }, { 1.75f, 1.75f, 1.75f } })
            .geometry(
                0,
                filament::RenderableManager::PrimitiveType::TRIANGLES,
                cube_vertices,
                cube_indices,
                0,
                indices.size())
            .material(0, cube.material_instance)
            .castShadows(false)
            .receiveShadows(false)
            .build(engine, cube.entity);
        if (renderable_result != filament::RenderableManager::Builder::Success) {
            throw std::runtime_error("Filament failed to create the viewport cube renderable.");
        }

        engine.getTransformManager().create(cube.entity);
        scene->addEntity(cube.entity);
        return cube;
    }

    void destroy_cube_renderable(CubeRenderable& cube)
    {
        if (!cube.entity) {
            return;
        }

        if (scene != nullptr) {
            scene->remove(cube.entity);
        }

        engine.getRenderableManager().destroy(cube.entity);
        engine.getTransformManager().destroy(cube.entity);
        if (cube.material_instance != nullptr) {
            engine.destroy(cube.material_instance);
            cube.material_instance = nullptr;
        }
        utils::EntityManager::get().destroy(cube.entity);
        cube.entity.clear();
    }

    [[nodiscard]] DirectionalLightRenderable create_directional_light_renderable()
    {
        DirectionalLightRenderable light;
        light.entity = utils::EntityManager::get().create();
        const filament::LightManager::Builder::Result light_result =
            filament::LightManager::Builder(filament::LightManager::Type::DIRECTIONAL)
            .castShadows(false)
            .build(engine, light.entity);
        if (light_result != filament::LightManager::Builder::Success) {
            throw std::runtime_error("Filament failed to create the viewport directional light.");
        }
        scene->addEntity(light.entity);
        return light;
    }

    void destroy_directional_light_renderable(DirectionalLightRenderable& light)
    {
        if (!light.entity) {
            return;
        }

        if (scene != nullptr) {
            scene->remove(light.entity);
        }

        engine.getLightManager().destroy(light.entity);
        utils::EntityManager::get().destroy(light.entity);
        light.entity.clear();
    }

    void set_cube_count(size_t count)
    {
        while (cubes.size() > count) {
            destroy_cube_renderable(cubes.back());
            cubes.pop_back();
        }
    }

    void set_directional_light_count(size_t count)
    {
        while (directional_lights.size() > count) {
            destroy_directional_light_renderable(directional_lights.back());
            directional_lights.pop_back();
        }
    }

    void ensure_cube_entity(size_t index, const SceneRenderMesh& mesh)
    {
        while (cubes.size() <= index) {
            cubes.push_back(create_cube_renderable());
        }

        CubeRenderable& cube = cubes[index];
        apply_material_settings(*cube.material_instance, mesh.material);
        engine.getTransformManager().setTransform(
            engine.getTransformManager().getInstance(cube.entity),
            scene_mesh_transform(mesh));
    }

    void ensure_directional_light_entity(size_t index, const DirectionalLightSettings& settings)
    {
        while (directional_lights.size() <= index) {
            directional_lights.push_back(create_directional_light_renderable());
        }

        apply_directional_light_settings(directional_lights[index].entity, settings);
    }

    void apply_scene_snapshot(const SceneRenderSnapshot& snapshot)
    {
        size_t cube_index = 0;
        for (const SceneRenderMesh& mesh : snapshot.meshes) {
            switch (mesh.mesh_kind) {
            case ViewportMeshKind::cube:
                ensure_cube_entity(cube_index++, mesh);
                break;
            }
        }
        set_cube_count(cube_index);

        size_t light_index = 0;
        for (const SceneRenderDirectionalLight& directional_light : snapshot.directional_lights) {
            ensure_directional_light_entity(light_index++, directional_light.settings);
        }
        set_directional_light_count(light_index);
    }

    void apply_material_settings(filament::MaterialInstance& instance, PbrMaterialSettings settings)
    {
        settings = clamp_pbr_material_settings(settings);
        ensure_fallback_material_textures();

        const MaterialTextureBinding albedo_texture = resolve_material_texture(
            settings.albedo_texture,
            PbrMaterialTextureSlot::albedo,
            PreviewTextureEncoding::srgb_rgba,
            fallback_albedo_texture);
        const MaterialTextureBinding roughness_texture = resolve_material_texture(
            settings.roughness_texture,
            PbrMaterialTextureSlot::roughness,
            PreviewTextureEncoding::linear_red,
            fallback_scalar_texture);
        const MaterialTextureBinding metallic_texture = resolve_material_texture(
            settings.metallic_texture,
            PbrMaterialTextureSlot::metallic,
            PreviewTextureEncoding::linear_red,
            fallback_scalar_texture);

        instance.setParameter(
            "albedo",
            filament::RgbaType::PREMULTIPLIED_LINEAR,
            to_filament_premultiplied_color(settings.albedo, 1.0f));
        instance.setParameter("albedoTexture", albedo_texture.texture, material_texture_sampler);
        instance.setParameter("useAlbedoTexture", albedo_texture.use_source_texture);
        instance.setParameter("roughness", settings.roughness);
        instance.setParameter("roughnessTexture", roughness_texture.texture, material_texture_sampler);
        instance.setParameter("useRoughnessTexture", roughness_texture.use_source_texture);
        instance.setParameter("metallic", settings.metallic);
        instance.setParameter("metallicTexture", metallic_texture.texture, material_texture_sampler);
        instance.setParameter("useMetallicTexture", metallic_texture.use_source_texture);
        instance.setParameter("reflectance", settings.reflectance);
        instance.setParameter("specularFactor", settings.specular_factor);
        instance.setParameter(
            "specularColorFactor",
            filament::RgbType::LINEAR,
            to_filament_color(settings.specular_color_factor));
        instance.setParameter("clearCoat", settings.clear_coat);
        instance.setParameter("clearCoatRoughness", settings.clear_coat_roughness);
    }

    void apply_directional_light_settings(utils::Entity light_entity, DirectionalLightSettings settings) const
    {
        settings = clamp_directional_light_settings(settings);
        filament::LightManager& lights = engine.getLightManager();
        const filament::LightManager::Instance light_instance = lights.getInstance(light_entity);
        lights.setColor(light_instance, to_filament_color(settings.color));
        lights.setIntensity(light_instance, settings.intensity);
        lights.setDirection(light_instance, to_filament_direction(settings));
    }

    void apply_camera(ViewportCamera camera_settings, ViewportProjection projection_settings)
    {
        camera_state = camera_settings;
        projection_state = projection_settings;

        const filament::math::double3 eye = to_filament_point(camera_state.position);
        const filament::math::double3 target = eye + to_filament_direction(camera_state.forward);
        camera->lookAt(eye, target, to_filament_up(camera_state.up));

        apply_camera_projection();
    }

    void apply_camera_projection()
    {
        if (camera == nullptr || !preview_extent.is_valid()) {
            return;
        }

        const double aspect =
            static_cast<double>(preview_extent.width) / static_cast<double>(preview_extent.height);

        if (projection_state.kind == ViewportProjectionKind::orthographic) {
            const double half_height = static_cast<double>(projection_state.orthographic_size) * 0.5;
            const double half_width = half_height * aspect;
            camera->setProjection(
                filament::Camera::Projection::ORTHO,
                -half_width,
                half_width,
                -half_height,
                half_height,
                projection_state.near_plane,
                projection_state.far_plane);
            return;
        }

        camera->setProjection(
            projection_state.vertical_fov_degrees,
            aspect,
            projection_state.near_plane,
            projection_state.far_plane);
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
        preview_color = require_filament_allocation(preview_color, "viewport color texture");

        preview_depth = filament::Texture::Builder()
            .width(extent.width)
            .height(extent.height)
            .levels(1)
            .usage(filament::Texture::Usage::DEPTH_ATTACHMENT)
            .format(filament::Texture::InternalFormat::DEPTH24)
            .build(engine);
        preview_depth = require_filament_allocation(preview_depth, "viewport depth texture");

        preview_target = filament::RenderTarget::Builder()
            .texture(filament::RenderTarget::AttachmentPoint::COLOR, preview_color)
            .texture(filament::RenderTarget::AttachmentPoint::DEPTH, preview_depth)
            .build(engine);
        preview_target = require_filament_allocation(preview_target, "viewport render target");

        preview_extent = extent;
        view->setViewport(filament::Viewport { 0, 0, extent.width, extent.height });
        view->setRenderTarget(preview_target);
        apply_camera_projection();
    }

    void render_into(const ViewportRenderRequest& request, ViewportRenderFrame& frame)
    {
        ZoneScopedN("Filament scene viewport render");

        SceneRenderSnapshot snapshot;
        std::visit(
            [&snapshot](const auto& subject) {
                using Subject = std::decay_t<decltype(subject)>;
                if constexpr (std::is_same_v<Subject, SceneViewportSubject>) {
                    snapshot = subject.snapshot;
                } else if constexpr (std::is_same_v<Subject, PreviewViewportSubject>) {
                    snapshot.meshes.push_back(SceneRenderMesh {
                        .mesh_kind = ViewportMeshKind::cube,
                        .material = subject.material,
                    });
                    snapshot.directional_lights.push_back(SceneRenderDirectionalLight {
                        .settings = subject.light,
                    });
                    snapshot.revision = subject.revision;
                }
            },
            request.subject);

        if (!snapshot.has_renderables()) {
            return;
        }

        apply_scene_snapshot(snapshot);
        apply_camera(request.camera, request.projection);
        ensure_preview_size(request.extent);
        if (preview_target == nullptr) {
            return;
        }

        TemporaryRgba8ReadbackFrame& readback = frame.payload.temporary_readback.emplace();
        if (readback.extent.width != request.extent.width || readback.extent.height != request.extent.height) {
            readback.extent = request.extent;
            readback.rgba.assign(static_cast<size_t>(request.extent.width) * request.extent.height * 4, 0);
        }
        frame.extent = request.extent;

        renderer->renderStandaloneView(view);
        renderer->readPixels(
            preview_target,
            0,
            0,
            readback.extent.width,
            readback.extent.height,
            filament::backend::PixelBufferDescriptor(
                readback.rgba.data(),
                readback.rgba.size(),
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
    std::vector<CubeRenderable> cubes;
    filament::Texture* fallback_albedo_texture = nullptr;
    filament::Texture* fallback_scalar_texture = nullptr;
    std::unordered_map<std::string, CachedMaterialTexture> material_texture_cache;
    filament::TextureSampler material_texture_sampler {
        filament::TextureSampler::MinFilter::LINEAR,
        filament::TextureSampler::MagFilter::LINEAR,
        filament::TextureSampler::WrapMode::REPEAT,
    };
    filament::Skybox* skybox = nullptr;
    filament::Camera* camera = nullptr;
    utils::Entity camera_entity;
    std::vector<DirectionalLightRenderable> directional_lights;
    RenderExtent preview_extent;
    ViewportCamera camera_state;
    ViewportProjection projection_state;
};

FilamentSceneViewport::FilamentSceneViewport(filament::Engine& engine)
    : resources_(std::make_unique<Resources>(engine))
{
    resources_->initialize();
}

FilamentSceneViewport::~FilamentSceneViewport() = default;

void FilamentSceneViewport::render_into(const ViewportRenderRequest& request, ViewportRenderFrame& frame)
{
    resources_->render_into(request, frame);
}

} // namespace quader
