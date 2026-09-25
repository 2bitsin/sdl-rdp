#pragma once

namespace sdl_rdp::utilities::detail::pinned {
class Pinned {
public:
       Pinned(Pinned const&)               = delete;
       Pinned(Pinned&&)                    = delete;
  auto operator=(Pinned const&) -> Pinned& = delete;
  auto operator=(Pinned&&)      -> Pinned& = delete;

protected:
  Pinned()  = default;
  ~Pinned() = default;
};
}

namespace sdl_rdp::utilities {
using detail::pinned::Pinned;
}
