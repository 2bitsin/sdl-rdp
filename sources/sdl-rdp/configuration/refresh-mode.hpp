#pragma once
#include <_buildutil/reflect.hpp>
#include <cstdint>

namespace sdl_rdp::configuration::detail::refresh_mode {
enum class RefreshMode : std::uint8_t {
  Fixed = 0,
  Client _Label("auto-client")          = 1,
  Average _Label("auto-client-average") = 2,
  Sender _Label("auto-sender")          = 3
};
constexpr auto reflect_scheme(RefreshMode* tag);
}

namespace sdl_rdp::configuration {
using detail::refresh_mode::RefreshMode;
}
