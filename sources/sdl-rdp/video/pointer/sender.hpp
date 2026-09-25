#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/pointer/forward.hpp>

#include <cstdint>

namespace sdl_rdp::video::pointer::detail::sender {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;

class PointerSender : private Pinned {
public:
       PointerSender(PointerStore& pointer, PeerLink& link, Diagnostics const& diagnostics) noexcept;
  auto Send() -> bool;

private:
  PointerStore&      _pointer;
  PeerLink&          _link;
  Diagnostics const& _diagnostics;
  std::uint64_t      _generation { };
};
}

namespace sdl_rdp::video::pointer {
using detail::sender::PointerSender;
}
