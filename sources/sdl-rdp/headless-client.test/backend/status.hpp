#pragma once
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <cstdint>
#include <optional>

namespace sdl_rdp::headless_client_test::backend::detail::status {
using sdl_rdp::peer::PeerStatus;
using sdl_rdp::picture::FrameLock;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::session::Backend;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Required;
using sdl_rdp::video::gfx::GraphicsTiming;

inline auto CurrentStatus(Backend& backend) -> std::optional<PeerStatus> {
  auto const frame   = backend.Frames().Lock();
  auto const current = backend.Session().Current(frame);
  return current ? std::optional{ current->get().Status(frame) } : std::nullopt;
}
inline auto RequiredStatus(Backend& backend) -> PeerStatus {
  return Required(CurrentStatus(backend), "a client is current");
}
inline auto RequiredGraphics(Backend& backend) -> GraphicsTiming {
  auto const graphics = RequiredStatus(backend).graphics;
  Expects(graphics.has_value(), "the current client has a graphics channel");
  return graphics.value_or(GraphicsTiming{ });
}
inline auto Presented(Backend& backend) -> std::uint64_t {
  return backend.Frames().Read([](FrameStore const& frames, FrameLock const& held) { return frames.Presented(held); });
}
inline auto AllAcknowledged(Backend& backend, FrameLock const& held) -> bool {
  auto const current = backend.Session().Current(held);
  return current && current->get().Status(held).acknowledged >= backend.Frames().Presented(held);
}
inline auto AllAcknowledged(Backend& backend) -> bool {
  auto const frame = backend.Frames().Lock();
  return AllAcknowledged(backend, frame);
}
}

namespace sdl_rdp::headless_client_test::backend {
using detail::status::AllAcknowledged;
using detail::status::CurrentStatus;
using detail::status::Presented;
using detail::status::RequiredGraphics;
using detail::status::RequiredStatus;
}
