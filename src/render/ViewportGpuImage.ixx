module;

#include <cstddef>
#include <cstdint>

export module quader.render.viewport_gpu_image;

export import quader.render.viewport_types;

export namespace quader {

struct ViewportTexturePayloadKind {
    uint64_t value = 0;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return value != 0;
    }

    [[nodiscard]] friend bool operator==(
        ViewportTexturePayloadKind left,
        ViewportTexturePayloadKind right) noexcept
    {
        return left.value == right.value;
    }
};

[[nodiscard]] consteval ViewportTexturePayloadKind viewport_texture_payload_kind(const char* name) noexcept
{
    uint64_t hash = 14695981039346656037ull;
    for (std::size_t index = 0; name[index] != '\0'; ++index) {
        hash ^= static_cast<uint64_t>(static_cast<unsigned char>(name[index]));
        hash *= 1099511628211ull;
    }

    return ViewportTexturePayloadKind { hash == 0 ? 1 : hash };
}

struct ViewportTextureTokenPayload {
    uint64_t word0 = 0;
    uint64_t word1 = 0;
    uint64_t word2 = 0;

    [[nodiscard]] bool has_value() const noexcept
    {
        return word0 != 0 || word1 != 0 || word2 != 0;
    }

    [[nodiscard]] friend bool operator==(
        ViewportTextureTokenPayload left,
        ViewportTextureTokenPayload right) noexcept
    {
        return left.word0 == right.word0 &&
            left.word1 == right.word1 &&
            left.word2 == right.word2;
    }
};

struct ViewportTextureToken {
    ViewportGpuImageHandle handle;
    ViewportTexturePayloadKind payload_kind;
    ViewportTextureTokenPayload payload;
    bool uv_origin_bottom_left = false;

    [[nodiscard]] bool is_valid() const noexcept
    {
        return handle.is_ready() && payload_kind.is_valid() && payload.has_value();
    }

    [[nodiscard]] friend bool operator==(const ViewportTextureToken& left, const ViewportTextureToken& right) noexcept
    {
        return left.handle.viewport_id == right.handle.viewport_id &&
            left.handle.generation == right.handle.generation &&
            left.handle.slot_index == right.handle.slot_index &&
            left.handle.image_generation == right.handle.image_generation &&
            left.handle.resource_id == right.handle.resource_id &&
            left.handle.extent.width == right.handle.extent.width &&
            left.handle.extent.height == right.handle.extent.height &&
            left.handle.readiness == right.handle.readiness &&
            left.payload_kind == right.payload_kind &&
            left.payload == right.payload &&
            left.uv_origin_bottom_left == right.uv_origin_bottom_left;
    }
};

struct ViewportSampledGpuImage {
    ViewportTextureToken texture;

    [[nodiscard]] ViewportTextureToken texture_token() const noexcept
    {
        return texture;
    }

    [[nodiscard]] bool is_valid() const noexcept
    {
        return texture.is_valid();
    }
};

} // namespace quader
