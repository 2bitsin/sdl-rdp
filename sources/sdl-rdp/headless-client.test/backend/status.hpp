#pragma once
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <cstdint>
#include <optional>

namespace sdl_rdp::headless_client_test::backend::detail::status {
using sdl_rdp::peer::PeerStatus;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Required;
using sdl_rdp::video::GraphicsTiming;

inline auto CurrentStatus(sdlrdp_handle& handle) -> std::optional<PeerStatus> {
  auto const frame   = handle.Frames().Lock();
  auto const current = handle.Session().Current(frame);
  return current ? std::optional{ current->get().Status(frame) } : std::nullopt;
}
inline auto RequiredStatus(sdlrdp_handle& handle) -> PeerStatus {
  return Required(CurrentStatus(handle), "a client is current");
}
inline auto RequiredGraphics(sdlrdp_handle& handle) -> GraphicsTiming {
  auto const graphics = RequiredStatus(handle).graphics;
  Expects(graphics.has_value(), "the current client has a graphics channel");
  return graphics.value_or(GraphicsTiming{ });
}
inline auto Presented(sdlrdp_handle& handle) -> std::uint64_t {
  return handle.Frames().Read([](FrameStore const& frames, FrameLock const& held) { return frames.Presented(held); });
}
inline auto AllAcknowledged(sdlrdp_handle& handle, FrameLock const& held) -> bool {
  auto const current = handle.Session().Current(held);
  return current && current->get().Status(held).acknowledged >= handle.Frames().Presented(held);
}
inline auto AllAcknowledged(sdlrdp_handle& handle) -> bool {
  auto const frame = handle.Frames().Lock();
  return AllAcknowledged(handle, frame);
}
}

namespace sdl_rdp::headless_client_test::backend {
using detail::status::AllAcknowledged;
using detail::status::CurrentStatus;
using detail::status::Presented;
using detail::status::RequiredGraphics;
using detail::status::RequiredStatus;
}
