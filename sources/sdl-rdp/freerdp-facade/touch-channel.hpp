#pragma once
#include <sdl-rdp/freerdp-facade/assignment-sink.hpp>
#include <sdl-rdp/freerdp-facade/channel-manager.hpp>
#include <sdl-rdp/freerdp-facade/touch-channel-events.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <memory>

struct s_rdpei_server_context;

namespace sdl_rdp::freerdp_facade::detail::touch_channel {
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Releases;

// abi: the release step of an rdpei server context, handed the context.
auto ReleaseTouch(s_rdpei_server_context* context) noexcept -> void;
using TouchContext = std::unique_ptr<s_rdpei_server_context, Releases<ReleaseTouch>>;
// The rdpei dynamic channel: opened once the dynamic channels are ready, touch contacts to its events.
class TouchChannel : private Pinned {
public:
       TouchChannel(ChannelManager& channels, TouchChannelEvents& events, AssignmentSink& assignee) noexcept;
  auto Open()         -> bool;
  auto Pump()         -> bool;
  auto Handle() const -> WaitHandle;
  auto Activate()     -> bool;

private:
  class Slots;
  // The unit test drives the slots through the context.
  friend class TouchChannelProbe;
  auto Context() const -> s_rdpei_server_context&;
  ChannelManager&     _channels;
  TouchChannelEvents& _events;
  AssignmentSink&     _assignee;
  TouchContext        _context;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::touch_channel::TouchChannel;
}
