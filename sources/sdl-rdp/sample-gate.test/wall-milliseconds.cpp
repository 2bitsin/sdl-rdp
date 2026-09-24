#include <sdl-rdp/sample-gate.test/wall-milliseconds.hpp>

#include <chrono>

namespace SampleGate {
auto WallMilliseconds() -> int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}
