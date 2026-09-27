////////////////////////////////////////////////////////////////////////////////
// Copyright (c) 2026 Francesco Di Blasi. All rights reserved.
// This file is part of Quader and is protected proprietary source code.
// No permission is granted to use, copy, modify, distribute, or sublicense this
// file except through prior written authorization from Francesco Di Blasi.
////////////////////////////////////////////////////////////////////////////////
#include "render/overlay/overlay_stencil_reset.hpp"

#include "render/filament_buffer_descriptor_factory.hpp"
#include "render/overlay/overlay_face_fill_composition.hpp"
#include "render/render_layers.hpp"

#include <backend/DriverEnums.h>
#include <filament/Box.h>
#include <filament/Engine.h>
#include <filament/IndexBuffer.h>
#include <filament/Material.h>
#include <filament/MaterialInstance.h>
#include <filament/RenderableManager.h>
#include <filament/Scene.h>
#include <filament/VertexBuffer.h>
#include <math/vec3.h>
#include <utils/EntityManager.h>

#include <array>
#include <cstdint>

namespace quader {
namespace {

/** Releases partial initialization unless ownership has moved to the reset. */
struct StencilResetCreationGuard final {
  filament::Engine &engine;
  filament::Scene &scene;
  filament::VertexBuffer *vertex_buffer = nullptr;
  filament::IndexBuffer *index_buffer = nullptr;
  filament::MaterialInstance *material_instance = nullptr;
  utils::Entity entity;
  bool scene_added = false;
  bool released = false;

  ~StencilResetCreationGuard() {
    if (released) {
      return;
    }
    if (entity) {
      if (scene_added) {
        scene.remove(entity);
      }
      engine.getRenderableManager().destroy(entity);
    }
    if (material_instance != nullptr) {
      engine.destroy(material_instance);
    }
    if (vertex_buffer != nullptr) {
      engine.destroy(vertex_buffer);
    }
    if (index_buffer != nullptr) {
      engine.destroy(index_buffer);
    }
    if (entity) {
      utils::EntityManager::get().destroy(entity);
    }
  }
};

} // namespace

OverlayStencilReset::~OverlayStencilReset() { shutdown(); }

void OverlayStencilReset::initialize(filament::Engine &engine,
                                      filament::Scene &scene,
                                      filament::Material &material,
                                      std::uint8_t layer_mask) {
  shutdown();
  StencilResetCreationGuard creation{engine, scene};
  const std::array<filament::math::float3, 3u> vertices{{
      {-1.0f, -1.0f, 0.0f},
      {3.0f, -1.0f, 0.0f},
      {-1.0f, 3.0f, 0.0f},
  }};
  const std::array<std::uint16_t, 3u> indices{{0u, 1u, 2u}};
  creation.vertex_buffer =
      filament::VertexBuffer::Builder()
          .vertexCount(static_cast<std::uint32_t>(vertices.size()))
          .bufferCount(1u)
          .attribute(filament::VertexAttribute::POSITION, 0u,
                     filament::backend::ElementType::FLOAT3, 0u,
                     sizeof(filament::math::float3))
          .build(engine);
  if (creation.vertex_buffer == nullptr) {
    return;
  }
  creation.vertex_buffer->setBufferAt(
      engine, 0u, FilamentBufferDescriptorFactory::bytes(
                      vertices.data(), sizeof(vertices)));

  creation.index_buffer =
      filament::IndexBuffer::Builder()
          .indexCount(static_cast<std::uint32_t>(indices.size()))
          .bufferType(filament::IndexBuffer::IndexType::USHORT)
          .build(engine);
  if (creation.index_buffer == nullptr) {
    return;
  }
  creation.index_buffer->setBuffer(
      engine, FilamentBufferDescriptorFactory::bytes(indices.data(),
                                                     sizeof(indices)));

  creation.material_instance = material.createInstance();
  if (creation.material_instance == nullptr) {
    return;
  }
  using MaterialInstance = filament::MaterialInstance;
  MaterialInstance &instance = *creation.material_instance;
  instance.setColorWrite(false);
  instance.setDepthWrite(false);
  instance.setDepthCulling(false);
  instance.setDepthFunc(MaterialInstance::DepthFunc::A);
  instance.setCullingMode(MaterialInstance::CullingMode::NONE);
  instance.setTransparencyMode(MaterialInstance::TransparencyMode::DEFAULT);
  instance.setStencilWrite(true);
  instance.setStencilCompareFunction(MaterialInstance::StencilCompareFunc::A);
  instance.setStencilReferenceValue(0u);
  instance.setStencilReadMask(kOverlayCompositionStencilMask);
  instance.setStencilWriteMask(kOverlayCompositionStencilMask);
  instance.setStencilOpStencilFail(MaterialInstance::StencilOperation::KEEP);
  instance.setStencilOpDepthFail(MaterialInstance::StencilOperation::KEEP);
  instance.setStencilOpDepthStencilPass(MaterialInstance::StencilOperation::ZERO);

  creation.entity = utils::EntityManager::get().create();
  if (!creation.entity) {
    return;
  }
  filament::Box bounds;
  bounds.set({-1.0f, -1.0f, -0.001f}, {3.0f, 3.0f, 0.001f});
  const auto result = filament::RenderableManager::Builder(1u)
      .geometry(0u, filament::RenderableManager::PrimitiveType::TRIANGLES,
                creation.vertex_buffer, creation.index_buffer)
      .material(0u, creation.material_instance)
      .channel(kViewportDefaultRenderableChannel)
      .priority(4u)
      .blendOrder(0u, 0u)
      .globalBlendOrderEnabled(0u, true)
      .layerMask(kViewportAllLayerMask, layer_mask)
      .boundingBox(bounds)
      .culling(false)
      .castShadows(false)
      .receiveShadows(false)
      .build(engine, creation.entity);
  if (result != filament::RenderableManager::Builder::Success) {
    return;
  }
  scene.addEntity(creation.entity);
  creation.scene_added = true;

  engine_ = &engine;
  scene_ = &scene;
  vertex_buffer_ = creation.vertex_buffer;
  index_buffer_ = creation.index_buffer;
  material_instance_ = creation.material_instance;
  entity_ = creation.entity;
  creation.released = true;
}

void OverlayStencilReset::shutdown() {
  if (engine_ == nullptr) {
    return;
  }
  if (entity_) {
    if (scene_ != nullptr) {
      scene_->remove(entity_);
    }
    engine_->getRenderableManager().destroy(entity_);
  }
  if (material_instance_ != nullptr) {
    engine_->destroy(material_instance_);
    material_instance_ = nullptr;
  }
  if (vertex_buffer_ != nullptr) {
    engine_->destroy(vertex_buffer_);
    vertex_buffer_ = nullptr;
  }
  if (index_buffer_ != nullptr) {
    engine_->destroy(index_buffer_);
    index_buffer_ = nullptr;
  }
  if (entity_) {
    utils::EntityManager::get().destroy(entity_);
    entity_.clear();
  }
  engine_ = nullptr;
  scene_ = nullptr;
}

} // namespace quader
