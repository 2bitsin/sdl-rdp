#pragma once
#include "pinned.hpp"

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
  bool     Drain();
  Delivery Encode(std::stop_token const& quit);

private:
  bool Prepare();
  bool Transmit(EncodeState kind);
  bool SendLegacy();
  void Transition(EncodeState next);
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
