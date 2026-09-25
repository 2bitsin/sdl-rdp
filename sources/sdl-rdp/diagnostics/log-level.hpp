#pragma once
#include <cstdint>

namespace sdl_rdp::diagnostics::detail::log_level {
enum class LogLevel : std::uint8_t { Error, Warn, Info };
}

namespace sdl_rdp::diagnostics {
using detail::log_level::LogLevel;
}
