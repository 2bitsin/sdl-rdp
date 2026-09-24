#pragma once

namespace Backend {
class Pinned {
public:
          Pinned(Pinned const&)      = delete;
          Pinned(Pinned&&)           = delete;
  Pinned& operator = (Pinned const&) = delete;
  Pinned& operator = (Pinned&&)      = delete;

protected:
  Pinned()  = default;
  ~Pinned() = default;
};
} // namespace Backend
