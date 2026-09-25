#include <sdl-rdp/link/channel-slot.hpp>

#include <cstdint>

namespace Backend {
ChannelSlot::ChannelSlot(DynamicChannels& registry, DynamicChannel& owner) noexcept
    : _registry{ registry }, _owner{ owner } { }
auto ChannelSlot::Assign(std::uint32_t id) -> bool {
  _assignment.emplace(_registry.Assign(id, _owner));
  return true;
}
}
