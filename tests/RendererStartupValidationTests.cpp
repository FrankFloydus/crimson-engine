#include <gtest/gtest.h>

import quader.render.startup_validation;

TEST(RendererStartupValidation, AcceptsInitializedRenderer)
{
    quader::RendererStatus status;
    status.initialized = true;
    status.backend_name = "Vulkan";
    status.message = "Filament is initialized.";

    EXPECT_FALSE(quader::describe_renderer_startup_failure(status).has_value());
    EXPECT_NO_THROW(quader::throw_if_renderer_failed_to_start(status));
}

TEST(RendererStartupValidation, ReportsExactStartupFailure)
{
    quader::RendererStatus status;
    status.initialized = false;
    status.backend_name = "Vulkan";
    status.message = "Filament Engine::create returned null.";

    const auto failure = quader::describe_renderer_startup_failure(status);

    ASSERT_TRUE(failure.has_value());
    EXPECT_EQ(
        "Render engine failed to initialize (Vulkan): Filament Engine::create returned null.",
        *failure);
    EXPECT_THROW(quader::throw_if_renderer_failed_to_start(status), std::runtime_error);
}
