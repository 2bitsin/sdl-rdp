#pragma once
#include <sdl-rdp/link/dynamic-channels.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <optional>

namespace Backend {
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
