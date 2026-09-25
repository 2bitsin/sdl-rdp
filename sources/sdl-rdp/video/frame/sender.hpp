#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

#include <stop_token>

namespace Backend {
class Activation;
class FrameCapture;
class FrameGate;
class GraphicsLink;
class LegacyFrame;
class PeerLink;
class PointerSender;
class SessionAccess;
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
