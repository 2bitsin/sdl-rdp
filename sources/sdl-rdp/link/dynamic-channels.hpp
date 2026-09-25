#pragma once
#include <sdl-rdp/link/dynamic-channel.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstdint>
#include <functional>
#include <map>

namespace Backend {
class DynamicChannels : private Pinned {
public:
  class Assignment {
  public:
         Assignment(Assignment&& other)              noexcept;
         Assignment(Assignment const&)               = delete;
         ~Assignment();
    auto operator=(Assignment&&)      -> Assignment& = delete;
    auto operator=(Assignment const&) -> Assignment& = delete;

  private:
    friend class DynamicChannels;
    Assignment(DynamicChannels& registry, std::uint32_t id, DynamicChannel& owner) noexcept;
    std::reference_wrapper<DynamicChannels> _registry;
    std::reference_wrapper<DynamicChannel>  _owner;
    std::uint32_t                           _id;
    bool                                    _live    { true };
  };
                     DynamicChannels() = default;
  [[nodiscard]] auto Assign(std::uint32_t id, DynamicChannel& owner) -> Assignment;
  auto               Activate(std::uint32_t id)                      -> bool;
  auto               Reject(std::uint32_t id)                        -> void;

private:
  using Owners = std::map<std::uint32_t, std::reference_wrapper<DynamicChannel>>;
  auto Forget(std::uint32_t id, DynamicChannel const& owner) noexcept -> void;
  auto Owner(std::uint32_t id)                                        -> Owners::iterator;
  Owners _owners;
};
}
