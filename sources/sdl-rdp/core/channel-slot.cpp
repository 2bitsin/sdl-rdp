#include <sdl-rdp/core/channel-slot.hpp>

namespace Backend {
ChannelSlot::ChannelSlot(DynamicChannels& registry, DynamicChannel& owner) noexcept
    : _registry{ registry }, _owner{ owner } { }
auto ChannelSlot::Assign(std::uint32_t id) -> void {
  _assignment.emplace(_registry.Assign(id, _owner));
}
}
