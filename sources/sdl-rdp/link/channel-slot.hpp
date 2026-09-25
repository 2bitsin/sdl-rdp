#pragma once
#include <sdl-rdp/link/dynamic-channels.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <optional>

namespace sdl_rdp::link::detail::channel_slot {
using sdl_rdp::utilities::Pinned;

class ChannelSlot : private Pinned {
public:
       ChannelSlot(DynamicChannels& registry, DynamicChannel& owner) noexcept;
  auto Assign(std::uint32_t id) -> bool;

private:
  DynamicChannels&                           _registry;
  DynamicChannel&                            _owner;
  std::optional<DynamicChannels::Assignment> _assignment;
};
}

namespace sdl_rdp::link {
using detail::channel_slot::ChannelSlot;
}
