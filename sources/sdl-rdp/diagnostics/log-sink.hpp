#pragma once
#include <sdl-rdp/diagnostics/log-level.hpp>

#include <string_view>

namespace sdl_rdp::diagnostics::detail::log_sink {
class LogSink {
public:
               LogSink()                                              = default;
               LogSink(LogSink const&)                                = delete;
               LogSink(LogSink&&)                                     = delete;
  virtual      ~LogSink();
  auto         operator=(LogSink const&)                  -> LogSink& = delete;
  auto         operator=(LogSink&&)                       -> LogSink& = delete;
  virtual auto Log(LogLevel level, std::string_view text) -> void     = 0;
};
}

namespace sdl_rdp::diagnostics {
using detail::log_sink::LogSink;
}
