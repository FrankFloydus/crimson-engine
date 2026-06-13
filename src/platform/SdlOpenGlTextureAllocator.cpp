module;
#include <SDL3/SDL_opengl.h>

#include <algorithm>
#include <stdexcept>
#include <string>

module quader.platform.sdl_opengl_texture_allocator;

namespace quader {
namespace {

[[nodiscard]] std::runtime_error gl_error(const char* operation)
{
    return std::runtime_error(std::string(operation) + " failed with OpenGL error " + std::to_string(glGetError()) + ".");
}

void delete_gl_texture(SdlOpenGlContext& context, OpenGlTexture2D texture) noexcept
{
    if (texture.id == 0) {
        return;
    }

    (void)context.try_make_current();
    const GLuint gl_texture = texture.id;
    glDeleteTextures(1, &gl_texture);
}

} // namespace

SdlOpenGlTextureAllocator::SdlOpenGlTextureAllocator(SdlOpenGlContext& context)
    : context_(context)
{
}

SdlOpenGlTextureAllocator::~SdlOpenGlTextureAllocator()
{
    for (OpenGlTexture2D texture : ready_to_delete_) {
        delete_gl_texture(context_, texture);
    }
    for (OpenGlTexture2D texture : deferred_delete_) {
        delete_gl_texture(context_, texture);
    }
}

OpenGlTexture2D SdlOpenGlTextureAllocator::create_color_texture(RenderExtent extent)
{
    if (!extent.is_valid()) {
        return {};
    }

    context_.make_current();

    GLuint texture = 0;
    glGenTextures(1, &texture);
    if (texture == 0) {
        throw gl_error("glGenTextures");
    }

    glBindTexture(GL_TEXTURE_2D, texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        static_cast<GLsizei>(extent.width),
        static_cast<GLsizei>(extent.height),
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);

    const GLenum error = glGetError();
    glBindTexture(GL_TEXTURE_2D, 0);
    if (error != GL_NO_ERROR) {
        const GLuint texture_to_delete = texture;
        glDeleteTextures(1, &texture_to_delete);
        throw std::runtime_error("glTexImage2D failed with OpenGL error " + std::to_string(error) + ".");
    }

    return OpenGlTexture2D {
        .id = texture,
        .resource_id = next_resource_id_++,
        .generation = next_generation_++,
        .extent = extent,
    };
}

void SdlOpenGlTextureAllocator::destroy_texture(OpenGlTexture2D texture) noexcept
{
    if (texture.is_valid()) {
        deferred_delete_.push_back(texture);
    }
}

bool SdlOpenGlTextureAllocator::has_retired_textures() const noexcept
{
    return !ready_to_delete_.empty() || !deferred_delete_.empty();
}

void SdlOpenGlTextureAllocator::collect_retired_textures() noexcept
{
    for (OpenGlTexture2D texture : ready_to_delete_) {
        delete_gl_texture(context_, texture);
    }
    ready_to_delete_.clear();

    ready_to_delete_.swap(deferred_delete_);
}

} // namespace quader
