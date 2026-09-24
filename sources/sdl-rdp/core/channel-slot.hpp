#pragma once
#include <sdl-rdp/core/dynamic-channels.hpp>
#include <sdl-rdp/core/failure-log.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <optional>

namespace Backend {
class ChannelSlot : private Pinned {
public:
       ChannelSlot(DynamicChannels& registry, DynamicChannel& owner) noexcept;
  auto Assign(std::uint32_t id)                                        -> void;
  auto Assigned(std::uint32_t id, FailureLog const& failures) noexcept -> bool;

private:
  DynamicChannels&                           _registry;
  DynamicChannel&                            _owner;
  std::optional<DynamicChannels::Assignment> _assignment;
};
}
