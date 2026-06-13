#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_set>

import quader.render.engine_factory;
import quader.render.viewport_renderer;

namespace {

class FakeViewportRenderer final : public quader::IViewportRenderer {
public:
    [[nodiscard]] quader::RendererStatus status() const override
    {
        return quader::RendererStatus {
            .initialized = true,
            .backend_name = "Fake",
            .message = "Fake viewport renderer is available for contract tests.",
        };
    }

    [[nodiscard]] quader::RendererCapabilities capabilities() const override
    {
        quader::RendererCapabilities capabilities = quader::default_renderer_capabilities();
        capabilities.engine_name = "Fake";
        return capabilities;
    }

    [[nodiscard]] std::optional<quader::ViewportId> try_create_viewport() override
    {
        const quader::ViewportId id { next_viewport_id_++ };
        live_viewports_.insert(id.value);
        return id;
    }

    void release_viewport(quader::ViewportId viewport_id) override
    {
        live_viewports_.erase(viewport_id.value);
    }

    [[nodiscard]] bool request_viewport_frame(const quader::ViewportRenderRequest& request) override
    {
        return request.is_valid() && live_viewports_.contains(request.viewport_id.value);
    }

    [[nodiscard]] std::optional<quader::ViewportRenderFrame> try_consume_viewport_frame(
        quader::ViewportId viewport_id) override
    {
        (void)viewport_id;
        return std::nullopt;
    }

    [[nodiscard]] std::optional<quader::ViewportSampledGpuImage> resolve_viewport_gpu_image(
        const quader::ViewportGpuImageHandle& handle) override
    {
        (void)handle;
        return std::nullopt;
    }

    [[nodiscard]] bool has_retired_viewport_gpu_resources() const noexcept override
    {
        return false;
    }

    void collect_retired_viewport_gpu_resources() override
    {
        ++collect_retired_calls_;
    }

    [[nodiscard]] uint32_t collect_retired_calls() const noexcept
    {
        return collect_retired_calls_;
    }

private:
    uint64_t next_viewport_id_ = 1;
    uint32_t collect_retired_calls_ = 0;
    std::unordered_set<uint64_t> live_viewports_;
};

class FakeOpenGlTextureAllocator final : public quader::IOpenGlTexture2DAllocator {
public:
    [[nodiscard]] quader::OpenGlTexture2D create_color_texture(quader::RenderExtent extent) override
    {
        return quader::OpenGlTexture2D {
            .id = 1,
            .resource_id = 1,
            .generation = 1,
            .extent = extent,
        };
    }

    void destroy_texture(quader::OpenGlTexture2D texture) noexcept override
    {
        (void)texture;
        ++destroy_texture_calls_;
    }

    [[nodiscard]] uint32_t destroy_texture_calls() const noexcept
    {
        return destroy_texture_calls_;
    }

private:
    uint32_t destroy_texture_calls_ = 0;
};

class FakeRenderEngineFactory final : public quader::IRenderEngineFactory {
public:
    [[nodiscard]] quader::RenderEngineKind engine_kind() const noexcept override
    {
        return quader::RenderEngineKind::unavailable;
    }

    [[nodiscard]] std::unique_ptr<quader::IViewportRenderer> create_vulkan_viewport_renderer(
        const quader::VulkanSharedDeviceContext& shared_device_context) const override
    {
        (void)shared_device_context;
        ++vulkan_create_calls_;
        return std::make_unique<FakeViewportRenderer>();
    }

    [[nodiscard]] std::unique_ptr<quader::IViewportRenderer> create_opengl_viewport_renderer(
        quader::OpenGlSharedContextHandle shared_context,
        quader::IOpenGlTexture2DAllocator& texture_allocator) const override
    {
        (void)shared_context;
        (void)texture_allocator;
        ++opengl_create_calls_;
        return std::make_unique<FakeViewportRenderer>();
    }

    [[nodiscard]] uint32_t vulkan_create_calls() const noexcept
    {
        return vulkan_create_calls_;
    }

    [[nodiscard]] uint32_t opengl_create_calls() const noexcept
    {
        return opengl_create_calls_;
    }

private:
    mutable uint32_t vulkan_create_calls_ = 0;
    mutable uint32_t opengl_create_calls_ = 0;
};

} // namespace

TEST(FakeViewportRendererContract, SatisfiesViewportRendererContractWithoutBackend)
{
    std::unique_ptr<quader::IViewportRenderer> renderer =
        std::make_unique<FakeViewportRenderer>();

    const quader::RendererStatus status = renderer->status();
    EXPECT_TRUE(status.initialized);
    EXPECT_EQ("Fake", status.backend_name);

    const quader::ViewportId viewport_id = renderer->create_viewport();
    EXPECT_TRUE(viewport_id.is_valid());

    quader::ViewportRenderRequest request;
    request.viewport_id = viewport_id;
    request.generation = quader::ViewportGeneration { 1 };
    request.request_serial = 1;
    request.extent = quader::RenderExtent { 320, 180 };
    EXPECT_TRUE(renderer->request_viewport_frame(request));
    EXPECT_FALSE(renderer->try_consume_viewport_frame(viewport_id).has_value());

    renderer->release_viewport(viewport_id);
    EXPECT_FALSE(renderer->request_viewport_frame(request));
    EXPECT_FALSE(renderer->has_retired_viewport_gpu_resources());

    auto& fake_renderer = static_cast<FakeViewportRenderer&>(*renderer);
    fake_renderer.collect_retired_viewport_gpu_resources();
    EXPECT_EQ(1U, fake_renderer.collect_retired_calls());
}

TEST(RendererCapabilitiesContract, DefaultCapabilitiesExposeNeutralOptionsAndDefaults)
{
    const quader::RendererCapabilities capabilities = quader::default_renderer_capabilities();

    EXPECT_STREQ("Renderer", capabilities.engine_name);
    EXPECT_EQ(2U, capabilities.post_processing.bloom.blend_modes.count);
    EXPECT_EQ(quader::BloomBlendMode::add, capabilities.post_processing.bloom.blend_modes.entries[0].value);
    EXPECT_STREQ("Add", capabilities.post_processing.bloom.blend_modes.entries[0].label);
    EXPECT_EQ(2U, capabilities.post_processing.ambient_occlusion.algorithms.count);
    EXPECT_EQ(
        quader::AmbientOcclusionAlgorithm::ground_truth,
        capabilities.post_processing.ambient_occlusion.algorithms.entries[1].value);
    EXPECT_STREQ("Ground Truth", capabilities.post_processing.ambient_occlusion.algorithms.entries[1].label);
    EXPECT_STREQ("Transparent", capabilities.shadows.directional_modes.entries[2].label);
    EXPECT_TRUE(capabilities.material.refraction.supported);
    EXPECT_TRUE(capabilities.environment_lighting.feature.supported);
    EXPECT_FALSE(capabilities.defaults.post_processing.bloom.enabled);
}

TEST(RenderEngineFactoryContract, DispatchesDriverSpecificCreationWithoutBackendModules)
{
    FakeRenderEngineFactory factory;
    FakeOpenGlTextureAllocator texture_allocator;

    std::unique_ptr<quader::IViewportRenderer> vulkan_renderer =
        factory.create_vulkan_viewport_renderer(quader::VulkanSharedDeviceContext {});
    std::unique_ptr<quader::IViewportRenderer> opengl_renderer =
        factory.create_opengl_viewport_renderer(
            quader::OpenGlSharedContextHandle {},
            texture_allocator);

    EXPECT_EQ(quader::RenderEngineKind::unavailable, factory.engine_kind());
    ASSERT_NE(nullptr, vulkan_renderer);
    ASSERT_NE(nullptr, opengl_renderer);
    EXPECT_EQ(1U, factory.vulkan_create_calls());
    EXPECT_EQ(1U, factory.opengl_create_calls());
    EXPECT_EQ("filament", quader::render_engine_kind_token(quader::RenderEngineKind::filament));
    EXPECT_EQ("Filament", quader::render_engine_kind_display_name(quader::RenderEngineKind::filament));
}
