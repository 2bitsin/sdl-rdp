#pragma once
#include <sdl-rdp/diagnostics/logged-failures.hpp>
#include <sdl-rdp/freerdp-facade/channel-manager.hpp>
#include <sdl-rdp/freerdp-facade/dynamic-creation-sink.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/input/input.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/peer/redirection.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/graphics-link.hpp>

#include <cstddef>
#include <cstdint>
#include <span>

namespace sdl_rdp::peer::detail::channel_set {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::LoggedFailures;
using sdl_rdp::freerdp_facade::CreationRegistration;
using sdl_rdp::freerdp_facade::DynamicCreationSink;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::input::Input;
using sdl_rdp::input::InputHandleLimit;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::video::DisplayControl;
using sdl_rdp::video::GraphicsHandleLimit;
using sdl_rdp::video::GraphicsLink;

inline constexpr std::size_t ChannelHandleLimit = InputHandleLimit + RedirectionHandleLimit + GraphicsHandleLimit;
class ChannelSet final : public LoggedFailures<DynamicCreationSink> {
public:
       ChannelSet(PeerLink& link, Activation const& activation, GraphicsLink& graphics, DisplayControl& display,
                  Redirection& redirection, Input& input, Diagnostics const& diagnostics);
  auto Pump(Signalled const& ready)             -> bool;
  auto Handles(std::span<WaitHandle> out) const -> std::span<WaitHandle>;

private:
  auto Created(std::uint32_t channel_id, std::int32_t status) -> bool override;
  PeerLink&            _link;
  Activation const&    _activation;
  GraphicsLink&        _graphics;
  DisplayControl&      _display;
  Redirection&         _redirection;
  Input&               _input;
  CreationRegistration _registration;
};
}

namespace sdl_rdp::peer {
using detail::channel_set::ChannelHandleLimit;
using detail::channel_set::ChannelSet;
}
