#pragma once

namespace sdl_rdp::link::detail::dynamic_channel {
class DynamicChannel {
public:
  DynamicChannel()                      = default;
  DynamicChannel(DynamicChannel const&) = delete;
  DynamicChannel(DynamicChannel&&)      = delete;
  // cppcoreguidelines-virtual-class-destructor flags a protected one at every implementer's forward declaration.
  virtual      ~DynamicChannel()                                   = default;
  auto         operator=(DynamicChannel const&) -> DynamicChannel& = delete;
  auto         operator=(DynamicChannel&&)      -> DynamicChannel& = delete;
  virtual auto Activate()                       -> bool            = 0;
  virtual auto Reject()                         -> void            = 0;
};
}

namespace sdl_rdp::link {
using detail::dynamic_channel::DynamicChannel;
}
