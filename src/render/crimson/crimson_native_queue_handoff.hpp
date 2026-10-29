#pragma once

#include <memory>

struct ID3D12Device;
struct ID3D12CommandQueue;

namespace quader {
// Qt gives each top-level RHI its own DIRECT queue. Renderer copies are queued
// on the primary borrowed queue; detached previews wait on this retained GPU
// fence before Qt submits sampling, without a CPU/device-idle wait.
class CrimsonNativeQueueHandoff final {
public:
  CrimsonNativeQueueHandoff();
  ~CrimsonNativeQueueHandoff();
  CrimsonNativeQueueHandoff(const CrimsonNativeQueueHandoff&)=delete;
  CrimsonNativeQueueHandoff& operator=(const CrimsonNativeQueueHandoff&)=delete;
  bool initialize(ID3D12Device*,ID3D12CommandQueue* primary);
  // Called before a producer copy. Capture every previously queued Qt read of
  // the destination, then order the producer after that consumer prefix.
  bool acquire(void* destination_queue);
  bool handoff(void* destination_queue);
  bool prepare_shutdown();
  bool healthy() const;
  // Caller first prepares shutdown, drains the renderer and leaves Qt frames. The
  // fence and its device/queue remain owned until that lifetime boundary.
  void reset();
private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
}
