module;

#include <backend/BufferDescriptor.h>
#include <backend/PixelBufferDescriptor.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <vector>

module quader.filament.buffer_upload;

namespace quader {
namespace {

[[nodiscard]] size_t upload_capacity_for(size_t required)
{
    size_t capacity = 256;
    while (capacity < required) {
        capacity *= 2;
    }
    return capacity;
}

void delete_buffer_copy(void* buffer, size_t, void*) noexcept
{
    delete[] static_cast<std::byte*>(buffer);
}

} // namespace

struct FilamentBufferUploadPool::Impl {
    struct Page {
        std::vector<std::byte> bytes;
        bool in_flight = false;
    };

    std::vector<std::unique_ptr<Page>> pages;
};

FilamentBufferUploadPool::FilamentBufferUploadPool()
    : impl_(std::make_unique<Impl>())
{
}

filament::backend::BufferDescriptor make_filament_buffer_copy(std::span<const std::byte> bytes)
{
    const size_t byte_count = bytes.size_bytes();
    auto* copied_bytes = new std::byte[byte_count];
    if (byte_count > 0) {
        std::memcpy(copied_bytes, bytes.data(), byte_count);
    }

    return filament::backend::BufferDescriptor(
        copied_bytes,
        byte_count,
        delete_buffer_copy);
}

filament::backend::PixelBufferDescriptor make_filament_pixel_buffer_copy(
    std::span<const uint8_t> values,
    filament::backend::PixelDataFormat format)
{
    const size_t byte_count = values.size_bytes();
    auto* bytes = new std::byte[byte_count];
    if (byte_count > 0) {
        std::memcpy(bytes, values.data(), byte_count);
    }

    return filament::backend::PixelBufferDescriptor(
        bytes,
        byte_count,
        format,
        filament::backend::PixelDataType::UBYTE,
        1,
        0,
        0,
        0,
        delete_buffer_copy);
}

FilamentBufferUploadPool::~FilamentBufferUploadPool() = default;

filament::backend::BufferDescriptor FilamentBufferUploadPool::copy(std::span<const std::byte> bytes)
{
    const size_t byte_count = bytes.size_bytes();
    Impl::Page* selected_page = nullptr;
    for (const std::unique_ptr<Impl::Page>& page : impl_->pages) {
        if (!page->in_flight && page->bytes.capacity() >= byte_count) {
            selected_page = page.get();
            break;
        }
    }

    if (selected_page == nullptr) {
        auto page = std::make_unique<Impl::Page>();
        page->bytes.reserve(upload_capacity_for(byte_count));
        selected_page = page.get();
        impl_->pages.push_back(std::move(page));
    }

    selected_page->bytes.resize(byte_count);
    if (byte_count > 0) {
        std::memcpy(selected_page->bytes.data(), bytes.data(), byte_count);
    }
    selected_page->in_flight = true;

    return filament::backend::BufferDescriptor(
        selected_page->bytes.data(),
        byte_count,
        [](void*, size_t, void* user) noexcept {
            auto* page = static_cast<Impl::Page*>(user);
            page->in_flight = false;
        },
        selected_page);
}

void FilamentBufferUploadPool::clear_after_gpu_idle()
{
    impl_->pages.clear();
}

} // namespace quader
