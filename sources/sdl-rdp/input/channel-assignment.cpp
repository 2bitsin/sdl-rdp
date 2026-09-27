#include <sdl-rdp/input/channel-assignment.hpp>

#include <sdl-rdp/link/peer-link.hpp>

namespace sdl_rdp::input::detail::channel_assignment {
ChannelAssignment::ChannelAssignment(PeerLink& link, Diagnostics const& diagnostics, DynamicChannel& owner) noexcept
    : LoggedFailures{ diagnostics }, _link{ link }, _owner{ owner } { }
auto ChannelAssignment::ChannelAssigned(std::uint32_t id) -> void {
  _assignment.emplace(_link.Dynamic().Assign(id, _owner));
}
}
