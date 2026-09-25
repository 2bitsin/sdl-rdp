#include <sdl-rdp/headless-client.test/utilities/wall-milliseconds.hpp>

#include <chrono>

namespace sdl_rdp::headless_client_test::utilities::detail::wall_milliseconds {
auto WallMilliseconds() -> std::int64_t {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}
