#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/utilities/operation-name.hpp>

#include <functional>
#include <string_view>

namespace Backend {
class Diagnostics;
class FailureLog {
public:
       FailureLog(Diagnostics const& diagnostics, OperationName operation,
                  sdlrdp_log_level level = SDLRDP_LOG_ERROR) noexcept;
  auto operator()(std::string_view failure) const -> void;

private:
  std::reference_wrapper<Diagnostics const> _diagnostics;
  OperationName                             _operation;
  sdlrdp_log_level                          _level;
};
}
