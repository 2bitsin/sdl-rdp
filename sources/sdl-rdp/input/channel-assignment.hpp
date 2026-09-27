#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/freerdp-facade/assignment-sink.hpp>
#include <sdl-rdp/link/dynamic-channel.hpp>
#include <sdl-rdp/link/dynamic-channels.hpp>
#include <sdl-rdp/link/forward.hpp>

#include <cstdint>
#include <optional>

namespace sdl_rdp::input::detail::channel_assignment {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::freerdp_facade::AssignmentSink;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::link::DynamicChannels;
using sdl_rdp::link::PeerLink;

// An input channel's id, registered with the connection's dynamic channels so the client's answer activates it.
class ChannelAssignment final : public LoggedFailures<AssignmentSink> {
public:
       ChannelAssignment(PeerLink& link, Diagnostics const& diagnostics, DynamicChannel& owner) noexcept;
  auto ChannelAssigned(std::uint32_t id) -> void                                                override;

private:
  PeerLink&                                  _link;
  DynamicChannel&                            _owner;
  std::optional<DynamicChannels::Assignment> _assignment;
};
}

namespace sdl_rdp::input {
using detail::channel_assignment::ChannelAssignment;
}
