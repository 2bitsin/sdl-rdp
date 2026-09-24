#pragma once
#include "contract.hpp"
#include "handle.hpp"
#include "peer.hpp"

#include <cstdint>
#include <optional>

namespace BackendGate {
inline std::optional<Backend::PeerStatus> CurrentStatus(sdlrdp_handle& handle) {
  auto const  session = handle.Session().Lock();
  auto const  frame   = handle.Frames().Lock();
  auto const* current = handle.Session().Current(frame);
  return current ? std::optional{ current->Status(frame) } : std::nullopt;
}
inline Backend::PeerStatus RequiredStatus(sdlrdp_handle& handle) {
  auto status = CurrentStatus(handle);
  utilities::Expects(status.has_value(), "a client is current");
  return status.value_or(Backend::PeerStatus{ });
}
inline Backend::GraphicsTiming RequiredGraphics(sdlrdp_handle& handle) {
  auto const graphics = RequiredStatus(handle).graphics;
  utilities::Expects(graphics.has_value(), "the current client has a graphics channel");
  return graphics.value_or(Backend::GraphicsTiming{ });
}
inline uint64_t Presented(sdlrdp_handle& handle) {
  return handle.Frames().Read([](Backend::FrameStore const& frames, Backend::FrameLock const& held) {
    return frames.Presented(held);
  });
}
}
