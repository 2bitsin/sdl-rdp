#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>

#include <cstdint>

namespace sdl_rdp::freerdp_facade::detail::assignment_sink {
// What a dynamic channel's id slot reports: the id its create request carries (MS-RDPEDYC 2.2.2.1).
class AssignmentSink : public FailureSink {
public:
  virtual auto ChannelAssigned(std::uint32_t id) -> void = 0;
};
// Every channel's id slot: the sink told its id, the slot answered.
inline constexpr auto Assigned = [](AssignmentSink& sink, std::uint32_t id) {
  sink.ChannelAssigned(id);
  return true;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::assignment_sink::Assigned;
using detail::assignment_sink::AssignmentSink;
}
