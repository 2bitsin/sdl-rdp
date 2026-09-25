#pragma once
#include <sdl-rdp/link/dynamic-channel.hpp>

#include <functional>

namespace sdl_rdp::input::detail::activated_channel {
using sdl_rdp::link::DynamicChannel;

class ActivatedChannel final : public DynamicChannel {
public:
  explicit ActivatedChannel(std::move_only_function<auto()->bool> activate) noexcept;
  auto     Activate() -> bool                                               override;
  auto     Reject()   -> void                                               override;

private:
  std::move_only_function<auto()->bool> _activate;
};
}

namespace sdl_rdp::input {
using detail::activated_channel::ActivatedChannel;
}
