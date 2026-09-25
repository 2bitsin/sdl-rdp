#pragma once
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <string_view>
namespace sdl3::rdp::detail::log_relay {
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::diagnostics::LogSink;

class LogRelay final : public LogSink {
public:
  auto Log(LogLevel level, std::string_view text) -> void override;
};
}

namespace sdl3::rdp {
using detail::log_relay::LogRelay;
}
