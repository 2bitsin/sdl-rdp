#pragma once
#include <sdl-rdp/freerdp-facade/advanced-input-channel-events.hpp>
#include <sdl-rdp/freerdp-facade/assignment-sink.hpp>
#include <sdl-rdp/freerdp-facade/channel-manager.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <memory>
#include <optional>

struct s_ainput_server_context;

namespace sdl_rdp::freerdp_facade::detail::advanced_input_channel {
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Releases;

// abi: the release step of an ainput server context, handed the context.
auto ReleaseAdvancedInput(s_ainput_server_context* context) noexcept -> void;
using AdvancedInputContext = std::unique_ptr<s_ainput_server_context, Releases<ReleaseAdvancedInput>>;
// The ainput dynamic channel: opened once the dynamic channels are ready, mouse events to its events.
class AdvancedInputChannel : private Pinned {
public:
       AdvancedInputChannel(ChannelManager& channels, Connection& connection, AdvancedInputChannelEvents& events,
                            AssignmentSink& assignee) noexcept;
  auto Open()         -> bool;
  auto Pump()         -> bool;
  auto Handle() const -> std::optional<WaitHandle>;
  auto Activate()     -> bool;

private:
  class Slots;
  // The unit test drives the slots through the context.
  friend class AdvancedInputChannelProbe;
  auto Started()       -> bool;
  auto Context() const -> s_ainput_server_context&;
  ChannelManager&             _channels;
  Connection&                 _connection;
  AdvancedInputChannelEvents& _events;
  AssignmentSink&             _assignee;
  AdvancedInputContext        _context;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::advanced_input_channel::AdvancedInputChannel;
}
