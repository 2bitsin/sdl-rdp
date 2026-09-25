#pragma once
#include <_buildutil/reflect.hpp>
#include <cstdint>

namespace sdl_rdp::configuration::detail::auth_mode {
enum class AuthMode : std::uint8_t { None _Label("none") = 0, Tls _Label("tls") = 1, Nla _Label("nla") = 2 };
constexpr auto reflect_scheme(AuthMode* tag);
}

namespace sdl_rdp::configuration {
using detail::auth_mode::AuthMode;
}
