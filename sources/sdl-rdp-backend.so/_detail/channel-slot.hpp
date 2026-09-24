#pragma once
#include "dynamic-channels.hpp"
#include "pinned.hpp"

#include <cstdint>
#include <optional>

namespace Backend {
class ChannelSlot : private Pinned {
public:
       ChannelSlot(DynamicChannels& registry, DynamicChannel& owner) noexcept;
  auto Assign(std::uint32_t id) -> void;

private:
  DynamicChannels&                           _registry;
  DynamicChannel&                            _owner;
  std::optional<DynamicChannels::Assignment> _assignment;
};
}
