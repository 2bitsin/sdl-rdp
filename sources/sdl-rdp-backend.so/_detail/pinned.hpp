#pragma once

namespace Backend {
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
} // namespace Backend
