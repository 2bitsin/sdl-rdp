#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/update.h>
#include <cstdint>
#include <vector>

namespace sdl_rdp::headless_client_test::frame::detail::observer {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Clock;
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::utilities::Pinned;

class FrameObserver : private Pinned {
public:
  explicit FrameObserver(Client& client);
           ~FrameObserver();

  auto Ack()                            -> bool;
  auto Frames() const                   -> std::vector<std::uint32_t> const&;
  auto ReceivedAt() const               -> std::vector<Clock::time_point> const&;
  auto AckFrame(std::uint32_t id) const -> bool;
  auto Acknowledgements() const         -> std::vector<Clock::time_point> const&;
  auto Coherent() const                 -> bool;
  auto Clear()                          -> void;

private:
  auto Receive(rdpContext const& context, SURFACE_FRAME_MARKER const& marker) -> void;
  rdpUpdate&                     update;
  pSurfaceFrameMarker            original;
  std::vector<std::uint32_t>     ids;
  std::vector<Clock::time_point> received;
  bool                           coherent   = true;
  std::vector<Clock::time_point> ack_times;
  Membership<FrameObserver>      membership;
};
}

namespace sdl_rdp::headless_client_test::frame {
using detail::observer::FrameObserver;
}
