#include <sdl-rdp/headless-client.test/utilities/wall-milliseconds.hpp>

#include <chrono>

namespace Headless {
auto WallMilliseconds() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}
