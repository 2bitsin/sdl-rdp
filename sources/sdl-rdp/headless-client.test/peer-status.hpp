#pragma once
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/session/peer.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <cstdint>
#include <optional>

namespace BackendGate {
inline auto CurrentStatus(sdlrdp_handle& handle) -> std::optional<Backend::PeerStatus> {
  auto const  session = handle.Session().Lock();
  auto const  frame   = handle.Frames().Lock();
  auto const* current = handle.Session().Current(frame);
  return current ? std::optional{ current->Status(frame) } : std::nullopt;
}
inline auto RequiredStatus(sdlrdp_handle& handle) -> Backend::PeerStatus {
  auto status = CurrentStatus(handle);
  utilities::Expects(status.has_value(), "a client is current");
  return status.value_or(Backend::PeerStatus{ });
}
inline auto RequiredGraphics(sdlrdp_handle& handle) -> Backend::GraphicsTiming {
  auto const graphics = RequiredStatus(handle).graphics;
  utilities::Expects(graphics.has_value(), "the current client has a graphics channel");
  return graphics.value_or(Backend::GraphicsTiming{ });
}
inline auto Presented(sdlrdp_handle& handle) -> std::uint64_t {
  return handle.Frames().Read(
      [](Backend::FrameStore const& frames, Backend::FrameLock const& held) { return frames.Presented(held); });
}
inline auto AllAcknowledged(sdlrdp_handle& handle, Backend::FrameLock const& held) -> bool {
  auto const* current = handle.Session().Current(held);
  return current != nullptr && current->Status(held).acknowledged >= handle.Frames().Presented(held);
}
inline auto AllAcknowledged(sdlrdp_handle& handle) -> bool {
  auto const frame = handle.Frames().Lock();
  return AllAcknowledged(handle, frame);
}
}
