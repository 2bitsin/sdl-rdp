#pragma once
#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <freerdp/update.h>
#include <cstdint>
#include <vector>

namespace sdl_rdp::headless_client_test::frame::detail::observer {
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::Clock;

struct FrameObserver {
public:
           FrameObserver(FrameObserver const&)               = delete;
           FrameObserver(FrameObserver&&)                    = delete;
  explicit FrameObserver(Client& client);
           ~FrameObserver();
  auto     operator=(FrameObserver const&) -> FrameObserver& = delete;
  auto     operator=(FrameObserver&&)      -> FrameObserver& = delete;

  auto Ack()                      -> bool;
  auto Frames() const             -> std::vector<std::uint32_t> const&;
  auto ReceivedAt() const         -> std::vector<Clock::time_point> const&;
  auto AckFrame(std::uint32_t id) -> bool;
  auto Acknowledgements() const   -> std::vector<Clock::time_point> const&;
  auto Coherent() const           -> bool;
  auto Installed() const          -> bool;
  auto Clear()                    -> void;

private:
  auto Receive(rdpContext const& context, SURFACE_FRAME_MARKER const& marker) -> void;
  inline static thread_local FrameObserver* active    = nullptr;
  rdpUpdate*                                update;
  pSurfaceFrameMarker                       original;
  std::vector<std::uint32_t>                ids;
  std::vector<Clock::time_point>            received;
  bool                                      coherent  = true;
  std::vector<Clock::time_point>            ack_times;
};
}

namespace sdl_rdp::headless_client_test::frame {
using detail::observer::FrameObserver;
}
