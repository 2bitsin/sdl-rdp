#pragma once
#include <string_view>

namespace sdl_rdp::utilities::detail::system_call {
auto SystemCall(int result, std::string_view operation) -> int;
}

namespace sdl_rdp::utilities {
using detail::system_call::SystemCall;
}
