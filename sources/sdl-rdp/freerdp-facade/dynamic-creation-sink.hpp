#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>

#include <cstdint>

namespace sdl_rdp::freerdp_facade::detail::dynamic_creation_sink {
// What a channel manager's dynamic creation slot reports: the client's answer to a create request (MS-RDPEDYC 2.2.2.2).
class DynamicCreationSink : public FailureSink {
public:
  virtual auto Created(std::uint32_t channel_id, std::int32_t status) -> bool = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::dynamic_creation_sink::DynamicCreationSink;
}
