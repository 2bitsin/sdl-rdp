#include <sdl-rdp/input/activated-channel.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <utility>

namespace sdl_rdp::input::detail::activated_channel {
using sdl_rdp::utilities::Expects;

ActivatedChannel::ActivatedChannel(std::move_only_function<auto()->bool> activate) noexcept
    : _activate{ std::move(activate) } { }
auto ActivatedChannel::Activate() -> bool {
  Expects(static_cast<bool>(_activate), "an activated channel has its activation");
  return _activate();
}
auto ActivatedChannel::Reject() -> void { }
}
