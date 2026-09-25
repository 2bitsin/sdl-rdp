#pragma once
namespace sdl3::rdp::input::detail::mouse {
auto InitMouse() -> void;
}

namespace sdl3::rdp::input {
using detail::mouse::InitMouse;
}
