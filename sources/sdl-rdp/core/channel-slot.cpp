#include <sdl-rdp/core/channel-slot.hpp>
#include <sdl-rdp/utilities/contained.hpp>

#include <cstdint>

namespace Backend {
ChannelSlot::ChannelSlot(DynamicChannels& registry, DynamicChannel& owner) noexcept
    : _registry{ registry }, _owner{ owner } { }
auto ChannelSlot::Assign(std::uint32_t id) -> void {
  _assignment.emplace(_registry.Assign(id, _owner));
}
auto ChannelSlot::Assigned(std::uint32_t id, FailureLog const& failures) noexcept -> bool {
  auto const assigned = [&] {
    Assign(id);
    return true;
  };
  return Contained(false, assigned, failures);
}
}
