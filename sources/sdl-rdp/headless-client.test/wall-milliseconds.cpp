#include <sdl-rdp/headless-client.test/wall-milliseconds.hpp>

#include <chrono>

namespace Headless {
auto WallMilliseconds() -> int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}
