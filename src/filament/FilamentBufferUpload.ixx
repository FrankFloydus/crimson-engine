module;

#include <backend/BufferDescriptor.h>
#include <backend/PixelBufferDescriptor.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <stdexcept>
#include <vector>

export module quader.filament.buffer_upload;

export namespace quader {

[[nodiscard]] filament::backend::BufferDescriptor make_filament_buffer_copy(std::span<const std::byte> bytes);

template<typename Value>
[[nodiscard]] filament::backend::BufferDescriptor make_filament_buffer_copy(std::span<const Value> values)
{
    return make_filament_buffer_copy(std::as_bytes(values));
}

template<typename Value, size_t Size>
[[nodiscard]] filament::backend::BufferDescriptor make_filament_buffer_copy(const std::array<Value, Size>& values)
{
    return make_filament_buffer_copy<Value>(std::span<const Value>(values.data(), values.size()));
}

[[nodiscard]] filament::backend::PixelBufferDescriptor make_filament_pixel_buffer_copy(
    std::span<const uint8_t> values,
    filament::backend::PixelDataFormat format);

template<size_t Size>
[[nodiscard]] filament::backend::PixelBufferDescriptor make_filament_pixel_buffer_copy(
    const std::array<uint8_t, Size>& values,
    filament::backend::PixelDataFormat format)
{
    return make_filament_pixel_buffer_copy(std::span<const uint8_t>(values.data(), values.size()), format);
}

template<typename Pointer>
[[nodiscard]] Pointer require_filament_allocation(Pointer pointer, const char* resource_name)
{
    if (pointer == nullptr) {
        throw std::runtime_error("Filament failed to allocate " + std::string(resource_name) + ".");
    }

    return pointer;
}

class FilamentBufferUploadPool final {
public:
    struct Impl;

    FilamentBufferUploadPool();
    FilamentBufferUploadPool(const FilamentBufferUploadPool&) = delete;
    FilamentBufferUploadPool& operator=(const FilamentBufferUploadPool&) = delete;
    ~FilamentBufferUploadPool();

    [[nodiscard]] filament::backend::BufferDescriptor copy(std::span<const std::byte> bytes);

    template<typename Value>
    [[nodiscard]] filament::backend::BufferDescriptor copy(std::span<const Value> values)
    {
        return copy(std::as_bytes(values));
    }

    void clear_after_gpu_idle();

private:
    std::unique_ptr<Impl> impl_;
};

} // namespace quader
