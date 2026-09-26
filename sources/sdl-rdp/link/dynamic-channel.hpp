#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

namespace sdl_rdp::link::detail::dynamic_channel {
using sdl_rdp::utilities::Pinned;
class DynamicChannel : private Pinned {
public:
  // cppcoreguidelines-virtual-class-destructor flags a protected one at every implementer's forward declaration.
  virtual      ~DynamicChannel()  = default;
  virtual auto Activate() -> bool = 0;
  virtual auto Reject()   -> void = 0;
};
}

namespace sdl_rdp::link {
using detail::dynamic_channel::DynamicChannel;
}
