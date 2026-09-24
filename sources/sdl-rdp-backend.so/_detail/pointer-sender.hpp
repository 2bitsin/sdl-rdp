#pragma once
#include "pinned.hpp"

#include <cstdint>

namespace Backend {
class Diagnostics;
class PeerLink;
class PointerStore;
class PointerSender : private Pinned {
public:
       PointerSender(PointerStore& pointer, PeerLink& link, Diagnostics const& diagnostics) noexcept;
  auto Send() -> bool;

private:
  PointerStore&      _pointer;
  PeerLink&          _link;
  Diagnostics const& _diagnostics;
  uint64_t           _generation { };
};
}
