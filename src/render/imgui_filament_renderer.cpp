////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/imgui_filament_renderer.hpp"

#include "core/logger.hpp"
#include "diagnostics/profile.hpp"
#include "render/filament_buffer_descriptor_factory.hpp"

#include <backend/DriverEnums.h>
#include <filament/RenderableManager.h>
#include <filament/TextureSampler.h>
#include <filament/Viewport.h>
#include <math/mat4.h>
#include <utils/EntityManager.h>

#include <algorithm>
#include <cstddef>
#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

namespace quader {
namespace {

using AttributeType = filament::backend::ElementType;

/**
 * Represents an UI Vertex value used by the render extraction and Filament resource layer.
 */
struct UIVertex {
    filament::math::float3 position;
    filament::math::float2 uv;
    std::uint32_t color;
};

/**
 * Represents an UI Primitive value used by the render extraction and Filament resource layer.
 */
struct UIPrimitive {
  std::uint32_t index_offset = 0;
  std::uint32_t index_count = 0;
  filament::Texture *texture = nullptr;
  TextureSampleColorSpace sample_color_space =
      TextureSampleColorSpace::DisplaySrgb;
  std::uint32_t scissor_left = 0;
  std::uint32_t scissor_bottom = 0;
  std::uint32_t scissor_width = 0;
  std::uint32_t scissor_height = 0;
};

std::uint32_t grow_capacity(std::uint32_t required) {
  std::uint32_t capacity = 1u;
  while (capacity < required) {
    capacity *= 2u;
  }
  return capacity;
}

std::size_t grow_capacity(std::size_t required) {
  std::size_t capacity = 1u;
  while (capacity < required) {
    capacity *= 2u;
  }
  return capacity;
}

} // namespace

ImGuiFilamentRenderer::~ImGuiFilamentRenderer() {
    shutdown();
}

void ImGuiFilamentRenderer::initialize(filament::Engine& engine, filament::Material& material, TextureRegistry& textures) {
    engine_ = &engine;
    material_ = &material;
    textures_ = &textures;

    camera_entity_ = utils::EntityManager::get().create();
    camera_ = engine_->createCamera(camera_entity_);

    clear_scene_ = engine_->createScene();
    clear_skybox_ = filament::Skybox::Builder()
                        .color({0.035f, 0.038f, 0.045f, 1.0f})
                        .build(*engine_);
    clear_scene_->setSkybox(clear_skybox_);
    clear_view_ = engine_->createView();
    clear_view_->setName("ImGui Clear");
    clear_view_->setScene(clear_scene_);
    clear_view_->setCamera(camera_);
    clear_view_->setPostProcessingEnabled(false);
    clear_view_->setBlendMode(filament::View::BlendMode::OPAQUE);

    LOG_INFO("Creating ImGui Filament scene/view");
    scene_ = engine_->createScene();
    view_ = engine_->createView();
    view_->setName("ImGui Overlay");
    view_->setScene(scene_);
    view_->setPostProcessingEnabled(false);
    view_->setBlendMode(filament::View::BlendMode::TRANSLUCENT);
    view_->setCamera(camera_);

    LOG_INFO("Creating ImGui font texture");
    create_font_texture();
}

void ImGuiFilamentRenderer::shutdown() {
    if (!engine_) {
        return;
    }

    destroy_all_frame_resources();

    if (font_texture_id_ != ImTextureID_Invalid) {
      textures_->unregister_texture(font_texture_id_);
      font_texture_id_ = ImTextureID_Invalid;
    }
    if (font_texture_) {
      engine_->destroy(font_texture_);
      font_texture_ = nullptr;
    }
    if (view_) {
        view_->setCamera(nullptr);
        view_->setScene(nullptr);
        engine_->destroy(view_);
        view_ = nullptr;
    }
    if (clear_view_) {
      clear_view_->setCamera(nullptr);
      clear_view_->setScene(nullptr);
      engine_->destroy(clear_view_);
      clear_view_ = nullptr;
    }
    if (scene_) {
        engine_->destroy(scene_);
        scene_ = nullptr;
    }
    if (clear_scene_) {
      clear_scene_->setSkybox(nullptr);
    }
    if (clear_skybox_) {
      engine_->destroy(clear_skybox_);
      clear_skybox_ = nullptr;
    }
    if (clear_scene_) {
      engine_->destroy(clear_scene_);
      clear_scene_ = nullptr;
    }
    if (camera_) {
      engine_->destroyCameraComponent(camera_entity_);
      camera_ = nullptr;
    }
    if (camera_entity_) {
      utils::EntityManager::get().destroy(camera_entity_);
      camera_entity_.clear();
    }

    engine_ = nullptr;
    material_ = nullptr;
    textures_ = nullptr;
}

void ImGuiFilamentRenderer::prepare(ImDrawData *draw_data,
                                    std::uint32_t framebuffer_width,
                                    std::uint32_t framebuffer_height) {
  QDR_PROFILE_FUNCTION();
  if (!engine_ || !draw_data || draw_data->CmdListsCount == 0 ||
      framebuffer_width == 0 || framebuffer_height == 0) {
    hide_unused_frame_resources(0u);
    return;
  }

  clear_view_->setViewport({0, 0, framebuffer_width, framebuffer_height});
  view_->setViewport({0, 0, framebuffer_width, framebuffer_height});
  camera_->setProjection(filament::Camera::Projection::ORTHO, 0.0,
                         static_cast<double>(framebuffer_width),
                         static_cast<double>(framebuffer_height), 0.0, 0.001,
                         10.0);
  camera_->lookAt({0.0, 0.0, 1.0}, {0.0, 0.0, 0.0}, {0.0, 1.0, 0.0});

  const ImVec2 clip_offset = draw_data->DisplayPos;
  const ImVec2 clip_scale = draw_data->FramebufferScale;
  const int framebuffer_width_int = static_cast<int>(std::min<std::uint32_t>(
      framebuffer_width,
      static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
  const int framebuffer_height_int = static_cast<int>(std::min<std::uint32_t>(
      framebuffer_height,
      static_cast<std::uint32_t>(std::numeric_limits<int>::max())));
  const filament::TextureSampler sampler(
      filament::TextureSampler::MinFilter::LINEAR,
      filament::TextureSampler::MagFilter::LINEAR,
      filament::TextureSampler::WrapMode::CLAMP_TO_EDGE);

  std::uint16_t draw_order = 0;
  std::size_t active_resource_count = 0;

  for (int list_index = 0; list_index < draw_data->CmdListsCount;
       ++list_index) {
    const ImDrawList *command_list = draw_data->CmdLists[list_index];
    if (command_list->VtxBuffer.Size == 0 ||
        command_list->IdxBuffer.Size == 0) {
      continue;
    }

    std::vector<UIVertex> vertices;
    vertices.reserve(command_list->VtxBuffer.Size);
    for (const ImDrawVert &vertex : command_list->VtxBuffer) {
      vertices.push_back({
          {vertex.pos.x, vertex.pos.y, 0.0f},
          {vertex.uv.x, vertex.uv.y},
          vertex.col,
      });
    }

    std::vector<std::uint32_t> indices;
    std::vector<UIPrimitive> primitives;
    indices.reserve(command_list->IdxBuffer.Size);
    primitives.reserve(command_list->CmdBuffer.Size);

    for (const ImDrawCmd &command : command_list->CmdBuffer) {
      if (command.UserCallback) {
        command.UserCallback(command_list, &command);
        continue;
      }
      if (command.ElemCount == 0 ||
          command.IdxOffset + command.ElemCount >
              static_cast<unsigned int>(command_list->IdxBuffer.Size) ||
          command.VtxOffset >=
              static_cast<unsigned int>(command_list->VtxBuffer.Size)) {
        LOG_WARN("Skipping invalid ImGui draw command: elem={} idxOffset={} "
                 "vtxOffset={} idxCount={} vtxCount={}",
                 command.ElemCount, command.IdxOffset, command.VtxOffset,
                 command_list->IdxBuffer.Size, command_list->VtxBuffer.Size);
        continue;
      }

      ImVec2 clip_min{
          (command.ClipRect.x - clip_offset.x) * clip_scale.x,
          (command.ClipRect.y - clip_offset.y) * clip_scale.y,
      };
      ImVec2 clip_max{
          (command.ClipRect.z - clip_offset.x) * clip_scale.x,
          (command.ClipRect.w - clip_offset.y) * clip_scale.y,
      };
      const int scissor_left = std::clamp(
          static_cast<int>(std::floor(clip_min.x)), 0, framebuffer_width_int);
      const int scissor_top = std::clamp(
          static_cast<int>(std::floor(clip_min.y)), 0, framebuffer_height_int);
      const int scissor_right = std::clamp(
          static_cast<int>(std::ceil(clip_max.x)), 0, framebuffer_width_int);
      const int scissor_bottom = std::clamp(
          static_cast<int>(std::ceil(clip_max.y)), 0, framebuffer_height_int);
      const int scissor_width = scissor_right - scissor_left;
      const int scissor_height = scissor_bottom - scissor_top;
      if (scissor_width <= 0 || scissor_height <= 0) {
        continue;
      }

      filament::Texture *texture = textures_->texture(command.GetTexID());
      if (!texture) {
        LOG_WARN("Skipping ImGui draw command with unregistered texture id {}",
                 static_cast<std::uint64_t>(command.GetTexID()));
        continue;
      }

      const std::uint32_t primitive_offset =
          static_cast<std::uint32_t>(indices.size());
      bool valid_indices = true;
      for (unsigned int n = 0; n < command.ElemCount; ++n) {
        const auto source_index = static_cast<std::uint32_t>(
            command_list->IdxBuffer[command.IdxOffset + n]);
        const std::uint32_t rebased_index =
            static_cast<std::uint32_t>(command.VtxOffset) + source_index;
        if (rebased_index >= static_cast<std::uint32_t>(vertices.size())) {
          valid_indices = false;
          break;
        }
        indices.push_back(rebased_index);
      }
      if (!valid_indices) {
        indices.resize(primitive_offset);
        LOG_WARN(
            "Skipping ImGui draw command with out-of-range rebased indices");
        continue;
      }

      primitives.push_back({
          .index_offset = primitive_offset,
          .index_count = command.ElemCount,
          .texture = texture,
          .sample_color_space =
              textures_->sample_color_space(command.GetTexID()),
          .scissor_left = static_cast<std::uint32_t>(scissor_left),
          .scissor_bottom = static_cast<std::uint32_t>(framebuffer_height_int -
                                                       scissor_bottom),
          .scissor_width = static_cast<std::uint32_t>(scissor_width),
          .scissor_height = static_cast<std::uint32_t>(scissor_height),
      });
    }

    if (primitives.empty() || indices.empty()) {
      continue;
    }

    DrawResource &resource = ensure_draw_resource(
        active_resource_count++, static_cast<std::uint32_t>(vertices.size()),
        static_cast<std::uint32_t>(indices.size()), primitives.size(),
        framebuffer_width, framebuffer_height);
    resource.vertex_buffer->setBufferAt(
        *engine_, 0, FilamentBufferDescriptorFactory::vector(vertices));
    resource.index_buffer->setBuffer(
        *engine_, FilamentBufferDescriptorFactory::vector(indices));

    auto &renderable_manager = engine_->getRenderableManager();
    filament::RenderableManager::Instance instance =
        renderable_manager.getInstance(resource.entity);
    renderable_manager.setAxisAlignedBoundingBox(
        instance, {{static_cast<float>(framebuffer_width) * 0.5f,
                    static_cast<float>(framebuffer_height) * 0.5f, 0.0f},
                   {static_cast<float>(framebuffer_width) * 0.5f,
                    static_cast<float>(framebuffer_height) * 0.5f, 1.0f}});
    renderable_manager.setLayerMask(instance, 0xffu, 0xffu);
    for (std::size_t primitive_index = 0; primitive_index < primitives.size();
         ++primitive_index) {
      const UIPrimitive &primitive = primitives[primitive_index];
      filament::MaterialInstance *material_instance =
          resource.material_instances[primitive_index];
      material_instance->setParameter("albedo", primitive.texture, sampler);
      const float encode_to_srgb =
          primitive.sample_color_space == TextureSampleColorSpace::LinearSrgb
              ? 1.0f
              : 0.0f;
      material_instance->setParameter("encodeToSrgb", encode_to_srgb);
      const float premultiplied_alpha =
          primitive.sample_color_space ==
                  TextureSampleColorSpace::DisplaySrgbPremultipliedAlpha
              ? 1.0f
              : 0.0f;
      material_instance->setParameter("premultipliedAlpha",
                                      premultiplied_alpha);
      material_instance->setScissor(
          primitive.scissor_left, primitive.scissor_bottom,
          primitive.scissor_width, primitive.scissor_height);
      renderable_manager.setMaterialInstanceAt(instance, primitive_index,
                                               material_instance);
      renderable_manager.setGeometryAt(
          instance, primitive_index,
          filament::RenderableManager::PrimitiveType::TRIANGLES,
          resource.vertex_buffer, resource.index_buffer, primitive.index_offset,
          primitive.index_count);
      renderable_manager.setBlendOrderAt(instance, primitive_index,
                                         draw_order++);
      renderable_manager.setGlobalBlendOrderEnabledAt(instance, primitive_index,
                                                      true);
    }
    for (std::size_t primitive_index = primitives.size();
         primitive_index < resource.primitive_capacity; ++primitive_index) {
      if (filament::MaterialInstance *material_instance =
              resource.material_instances[primitive_index]) {
        material_instance->unsetScissor();
      }
      renderable_manager.setGeometryAt(
          instance, primitive_index,
          filament::RenderableManager::PrimitiveType::TRIANGLES,
          resource.vertex_buffer, resource.index_buffer, 0u, 0u);
    }
  }

  hide_unused_frame_resources(active_resource_count);
}

void ImGuiFilamentRenderer::render(filament::Renderer& renderer) {
    QDR_PROFILE_FUNCTION();
    if (!view_) {
        return;
    }
    filament::Renderer::ClearOptions clear_options;
    clear_options.clear = true;
    clear_options.discard = false;
    clear_options.clearColor = {0.0f, 0.0f, 0.0f, 1.0f};
    renderer.setClearOptions(clear_options);
    if (clear_view_) {
      renderer.render(clear_view_);
    }
    renderer.render(view_);
}

void ImGuiFilamentRenderer::create_font_texture() {
  ImGuiIO &io = ImGui::GetIO();
  unsigned char *source_pixels = nullptr;
  int width = 0;
  int height = 0;
  io.Fonts->GetTexDataAsRGBA32(&source_pixels, &width, &height);

  const std::size_t row_byte_count = static_cast<std::size_t>(width) * 4u;
  const std::size_t byte_count =
      row_byte_count * static_cast<std::size_t>(height);
  std::vector<unsigned char> pixels(byte_count);
  for (int y = 0; y < height; ++y) {
    const auto source_offset = static_cast<std::size_t>(y) * row_byte_count;
    const auto target_offset =
        static_cast<std::size_t>(height - 1 - y) * row_byte_count;
    std::memcpy(pixels.data() + target_offset, source_pixels + source_offset,
                row_byte_count);
  }
  const ImVec2 white_uv = io.Fonts->TexUvWhitePixel;
  const int white_x = std::clamp(
      static_cast<int>(white_uv.x * static_cast<float>(width)), 0, width - 1);
  const int white_y =
      height - 1 -
      std::clamp(static_cast<int>(white_uv.y * static_cast<float>(height)), 0,
                 height - 1);
  for (int y = std::max(0, white_y - 1); y <= std::min(height - 1, white_y + 1);
       ++y) {
    for (int x = std::max(0, white_x - 1);
         x <= std::min(width - 1, white_x + 1); ++x) {
      const std::size_t offset =
          (static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
           static_cast<std::size_t>(x)) *
          4u;
      pixels[offset + 0u] = 255u;
      pixels[offset + 1u] = 255u;
      pixels[offset + 2u] = 255u;
      pixels[offset + 3u] = 255u;
    }
  }

  font_texture_ = filament::Texture::Builder()
                      .width(static_cast<std::uint32_t>(width))
                      .height(static_cast<std::uint32_t>(height))
                      .levels(1)
                      .format(filament::Texture::InternalFormat::RGBA8)
                      .usage(filament::Texture::Usage::UPLOADABLE |
                             filament::Texture::Usage::SAMPLEABLE)
                      .build(*engine_);

  font_texture_->setImage(*engine_, 0,
                          FilamentBufferDescriptorFactory::rgba_pixels(
                              pixels.data(), pixels.size()));
  font_texture_id_ = textures_->register_texture(font_texture_);
  io.Fonts->SetTexID(font_texture_id_);
  LOG_INFO("ImGui font atlas uploaded as Filament texture {}x{}", width,
           height);
}

void ImGuiFilamentRenderer::release_frame_resources() {
  QDR_PROFILE_FUNCTION();
}

ImGuiFilamentRenderer::DrawResource &
ImGuiFilamentRenderer::ensure_draw_resource(std::size_t slot,
                                            std::uint32_t vertex_count,
                                            std::uint32_t index_count,
                                            std::size_t primitive_count,
                                            std::uint32_t framebuffer_width,
                                            std::uint32_t framebuffer_height) {
  QDR_PROFILE_FUNCTION();
  if (slot >= frame_resources_.size()) {
    frame_resources_.resize(slot + 1u);
  }

  DrawResource &resource = frame_resources_[slot];
  const bool needs_rebuild = !resource.entity ||
                             resource.vertex_buffer == nullptr ||
                             resource.index_buffer == nullptr ||
                             resource.vertex_capacity < vertex_count ||
                             resource.index_capacity < index_count ||
                             resource.primitive_capacity < primitive_count;
  if (!needs_rebuild) {
    return resource;
  }

  if (resource.entity || resource.vertex_buffer != nullptr ||
      resource.index_buffer != nullptr) {
    QDR_PROFILE_SCOPE("ImGui rebuild undersized frame resource");
    destroy_draw_resource(resource);
  }

  resource.vertex_capacity = grow_capacity(vertex_count);
  resource.index_capacity = grow_capacity(index_count);
  resource.primitive_capacity = grow_capacity(primitive_count);
  resource.entity = utils::EntityManager::get().create();
  resource.vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(resource.vertex_capacity)
          .bufferCount(1)
          .attribute(filament::VertexAttribute::POSITION, 0,
                     AttributeType::FLOAT3, offsetof(UIVertex, position),
                     sizeof(UIVertex))
          .attribute(filament::VertexAttribute::UV0, 0, AttributeType::FLOAT2,
                     offsetof(UIVertex, uv), sizeof(UIVertex))
          .attribute(filament::VertexAttribute::COLOR, 0, AttributeType::UBYTE4,
                     offsetof(UIVertex, color), sizeof(UIVertex))
          .normalized(filament::VertexAttribute::COLOR)
          .build(*engine_);
  resource.index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(resource.index_capacity)
          .bufferType(filament::IndexBuffer::IndexType::UINT)
          .build(*engine_);

  resource.material_instances.reserve(resource.primitive_capacity);
  filament::RenderableManager::Builder builder(resource.primitive_capacity);
  builder
      .boundingBox({{static_cast<float>(framebuffer_width) * 0.5f,
                     static_cast<float>(framebuffer_height) * 0.5f, 0.0f},
                    {static_cast<float>(framebuffer_width) * 0.5f,
                     static_cast<float>(framebuffer_height) * 0.5f, 1.0f}})
      .culling(false)
      .castShadows(false)
      .receiveShadows(false)
      .priority(0);
  for (std::size_t primitive_index = 0;
       primitive_index < resource.primitive_capacity; ++primitive_index) {
    filament::MaterialInstance *material_instance = material_->createInstance();
    resource.material_instances.push_back(material_instance);
    builder
        .geometry(primitive_index,
                  filament::RenderableManager::PrimitiveType::TRIANGLES,
                  resource.vertex_buffer, resource.index_buffer, 0u, 0u)
        .material(primitive_index, material_instance)
        .blendOrder(primitive_index, 0u)
        .globalBlendOrderEnabled(primitive_index, true);
  }
  builder.build(*engine_, resource.entity);
  scene_->addEntity(resource.entity);
  return resource;
}

void ImGuiFilamentRenderer::destroy_draw_resource(DrawResource &resource) {
  if (resource.entity) {
    scene_->remove(resource.entity);
    engine_->getRenderableManager().destroy(resource.entity);
  }
  for (filament::MaterialInstance *material_instance :
       resource.material_instances) {
    if (material_instance) {
      engine_->destroy(material_instance);
    }
  }
  resource.material_instances.clear();
  if (resource.vertex_buffer) {
    engine_->destroy(resource.vertex_buffer);
    resource.vertex_buffer = nullptr;
  }
  if (resource.index_buffer) {
    engine_->destroy(resource.index_buffer);
    resource.index_buffer = nullptr;
  }
  if (resource.entity) {
    utils::EntityManager::get().destroy(resource.entity);
    resource.entity.clear();
  }
  resource.vertex_capacity = 0;
  resource.index_capacity = 0;
  resource.primitive_capacity = 0;
}

void ImGuiFilamentRenderer::destroy_all_frame_resources() {
  QDR_PROFILE_FUNCTION();
  for (DrawResource &resource : frame_resources_) {
    destroy_draw_resource(resource);
  }
  frame_resources_.clear();
}

void ImGuiFilamentRenderer::hide_unused_frame_resources(
    std::size_t first_unused_slot) {
  if (!engine_) {
    return;
  }

  auto &renderable_manager = engine_->getRenderableManager();
  for (std::size_t slot = first_unused_slot; slot < frame_resources_.size();
       ++slot) {
    DrawResource &resource = frame_resources_[slot];
    if (!resource.entity) {
      continue;
    }
    filament::RenderableManager::Instance instance =
        renderable_manager.getInstance(resource.entity);
    if (instance) {
      renderable_manager.setLayerMask(instance, 0xffu, 0x00u);
    }
  }
}

} // namespace quader
