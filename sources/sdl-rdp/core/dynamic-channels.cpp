#include <sdl-rdp/core/dynamic-channels.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <cstdint>
#include <utility>

namespace Backend {
DynamicChannels::Assignment::Assignment(DynamicChannels& registry, std::uint32_t id, DynamicChannel& owner) noexcept
    : _registry{ registry }, _owner{ owner }, _id{ id } { }
DynamicChannels::Assignment::Assignment(Assignment&& other) noexcept
    : _registry{ other._registry }, _owner{ other._owner }, _id{ other._id },
      _live{ std::exchange(other._live, false) } { }
DynamicChannels::Assignment::~Assignment() {
  if (_live) _registry.get().Forget(_id, _owner);
}
auto DynamicChannels::Assign(std::uint32_t id, DynamicChannel& owner) -> Assignment {
  auto const assigned = _owners.try_emplace(id, owner).second;
  Expects(assigned, "channel id is assigned once");
  return Assignment{ *this, id, owner };
}
auto DynamicChannels::Activate(std::uint32_t id) -> bool {
  auto const owner = Owner(id);
  return owner != _owners.end() && owner->second.get().Activate();
}
auto DynamicChannels::Reject(std::uint32_t id) -> void {
  auto const owner = Owner(id);
  if (owner == _owners.end()) return;
  auto& channel = owner->second.get();
  channel.Reject();
}
auto DynamicChannels::Forget(std::uint32_t id, DynamicChannel const& owner) noexcept -> void {
  std::erase_if(_owners, [&](auto const& entry) { return entry.first == id && &entry.second.get() == &owner; });
}
auto DynamicChannels::Owner(std::uint32_t id) -> Owners::iterator {
  auto const owner = _owners.find(id);
  Expects(owner != _owners.end(), "channel id was assigned at open");
  return owner;
}
}
