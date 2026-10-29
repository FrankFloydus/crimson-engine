# Pinned Qt Direct3D 12 private header dependency

`D3D12MemAlloc.h` and `LICENSE.txt` are unchanged files from Qt 6.10.3's
`qtbase/src/3rdparty/D3D12MemoryAllocator` directory. Qt's installed
`qrhid3d12_p.h` includes this header, but the pinned vcpkg Qt package omits it
from its installed private include closure.

Header SHA-256: `f26ba5e184b9dd007f2428bac5d8370bbba457a4415ef8793432a915277e1ac6`.

The composition adapter uses the same pinned Qt private RHI boundary as Quader's
existing Vulkan adapter. This directory supplies only the exact matching header;
it does not compile or link a second allocator implementation. Qt retains every
resource and allocation owner. The allocator's MIT license is retained here.

Review the header, layout and adapter together when changing the Qt version.
