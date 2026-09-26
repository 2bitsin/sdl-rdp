#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <string_view>

namespace sdl_rdp::diagnostics::detail::log_sink {
using sdl_rdp::utilities::Pinned;
enum class LogLevel : std::uint8_t { Error, Warn, Info };
class LogSink : private Pinned {
public:
  virtual      ~LogSink()                                         = default;
  virtual auto Log(LogLevel level, std::string_view text) -> void = 0;
};
}

namespace sdl_rdp::diagnostics {
using detail::log_sink::LogLevel;
using detail::log_sink::LogSink;
}
