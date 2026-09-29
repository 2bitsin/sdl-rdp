#pragma once
#include <sdl-rdp/link/dynamic-channel.hpp>

#include <oxbox/utilities/function.hpp>

namespace sdl_rdp::input::detail::activated_channel {
using oxbox::utilities::MoveOnlyFunction;
using sdl_rdp::link::DynamicChannel;

using Activator = MoveOnlyFunction<auto()->bool>;
class ActivatedChannel final : public DynamicChannel {
public:
  explicit ActivatedChannel(Activator activate) noexcept;
  auto     Activate() -> bool                   override;
  auto     Reject()   -> void                   override;

private:
  Activator _activate;
};
}

namespace sdl_rdp::input {
using detail::activated_channel::ActivatedChannel;
}
