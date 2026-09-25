#pragma once
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>
#include <sdl-rdp/video/pointer/forward.hpp>

#include <stop_token>

namespace sdl_rdp::video::frame::detail::sender {
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::pointer::PointerSender;

enum class EncodeState{ Idle, Legacy, Graphics, LegacyReady };
enum class Delivery   { Healthy, Stopped, Failed            };
class FrameSender : private Pinned {
public:
       FrameSender(PeerLink& link, Activation const& activation, SessionAccess& session, FrameGate& gate,
                   FrameCapture& capture, PointerSender& pointer, GraphicsLink& graphics, LegacyFrame& legacy) noexcept;
  auto Drain()                             -> bool;
  auto Encode(std::stop_token const& quit) -> Delivery;

private:
  auto Prepare()                    -> bool;
  auto Transmit(EncodeState kind)   -> bool;
  auto SendLegacy()                 -> bool;
  auto Transition(EncodeState next) -> void;
  PeerLink&         _link;
  Activation const& _activation;
  SessionAccess&    _session;
  FrameGate&        _gate;
  FrameCapture&     _capture;
  PointerSender&    _pointer;
  GraphicsLink&     _graphics;
  LegacyFrame&      _legacy;
  EncodeState       _state     { EncodeState::Idle };
};
}

namespace sdl_rdp::video::frame {
using detail::sender::Delivery;
using detail::sender::FrameSender;
}
