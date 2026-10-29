#pragma once
#include <atomic>
#include <cstdint>
namespace quader {
// Opaque process identity for Qt allocations and logical singleton bindings.
// Native addresses and per-widget counters may both be reused after teardown.
inline std::uint64_t next_crimson_copy_identity()noexcept{
    static std::atomic<std::uint64_t> next{1};return next.fetch_add(1,std::memory_order_relaxed);
}
}
