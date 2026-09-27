#pragma once
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::failure_sink {
using sdl_rdp::utilities::OperationName;
using sdl_rdp::utilities::Pinned;

// A C caller cannot unwind: a handler's throw is reported here and its slot returns the failure to FreeRDP.
class FailureSink : private Pinned {
public:
  virtual      ~FailureSink()                                                          = default;
  virtual auto Failed(OperationName operation, std::string_view failure) const -> void = 0;
};
// A slot's failure projection: the owner's sink, told which operation failed.
inline constexpr auto SinkFailures = [](FailureSink const& sink, OperationName operation) noexcept {
  return [&sink, operation](std::string_view failure) { sink.Failed(operation, failure); };
};
}

namespace sdl_rdp::freerdp_facade {
using detail::failure_sink::FailureSink;
using detail::failure_sink::SinkFailures;
}
