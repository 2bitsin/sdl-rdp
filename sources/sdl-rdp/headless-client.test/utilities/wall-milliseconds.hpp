#pragma once
#include <cstdint>

namespace sdl_rdp::headless_client_test::utilities::detail::wall_milliseconds {
auto WallMilliseconds() -> std::int64_t;
}

namespace sdl_rdp::headless_client_test::utilities {
using detail::wall_milliseconds::WallMilliseconds;
}
