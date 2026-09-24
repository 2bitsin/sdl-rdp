#include "_detail/frame-sender.hpp"

#include "_detail/activation.hpp"
#include "_detail/frame-capture.hpp"
#include "_detail/frame-gate.hpp"
#include "_detail/graphics-link.hpp"
#include "_detail/legacy-frame.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/pointer-sender.hpp"
#include "_detail/session-access.hpp"

namespace Backend {
FrameSender::FrameSender(PeerLink& link, Activation const& activation, SessionAccess& session, FrameGate& gate,
                         FrameCapture& capture, PointerSender& pointer, GraphicsLink& graphics,
                         LegacyFrame& legacy) noexcept
    : _link { link }, _activation{ activation }, _session{ session }, _gate{ gate }, _capture{ capture },
      _pointer{ pointer }, _graphics{ graphics }, _legacy{ legacy } { }
bool FrameSender::Drain() {
  if (!_activation.Active()) return true;
  auto& client = _link.Client();
  if (client.DrainOutputBuffer(&client) < 0) return false;
  if (!_gate.Admit() || _activation.Holding()) return true;
  if (!_pointer.Send()) return false;
  auto const captured = _capture.Next();
  switch (captured) {
  case CaptureState::Failed:
    return false;
  case CaptureState::Idle:
    return true;
  case CaptureState::Captured:
    return Prepare();
  default:
    utilities::Unreachable(captured);
  }
}
Delivery FrameSender::Encode(std::stop_token const& quit) {
  auto const kind = _state;
  if (kind == EncodeState::Idle || kind == EncodeState::LegacyReady) return Delivery::Healthy;
  auto const encoded = kind == EncodeState::Legacy ? _legacy.Encode() : _graphics.Channel().Encode();
  auto const session = _session.Lock();
  Transition(kind == EncodeState::Legacy ? EncodeState::LegacyReady : EncodeState::Idle);
  if (quit.stop_requested()) return Delivery::Stopped;
  if (!_activation.Active()) return Delivery::Healthy;
  return encoded && Transmit(kind) ? Delivery::Healthy : Delivery::Failed;
}
bool FrameSender::Transmit(EncodeState kind) {
  return kind == EncodeState::Legacy ? SendLegacy() : _graphics.Channel().Send();
}
bool FrameSender::Prepare() {
  if (_state == EncodeState::LegacyReady) return SendLegacy();
  auto const confirmed = _graphics.Confirmed();
  if (!(confirmed ? _graphics.Channel().Prepare() : _legacy.Prepare())) return false;
  Transition(confirmed ? EncodeState::Graphics : EncodeState::Legacy);
  return true;
}
bool FrameSender::SendLegacy() {
  if (!_legacy.Send()) return false;
  if (_legacy.Delivered()) Transition(EncodeState::Idle);
  return true;
}
void FrameSender::Transition(EncodeState next) {
  switch (_state) {
  case EncodeState::Idle:
    Expects(next != EncodeState::LegacyReady, "encoding precedes legacy writes");
    break;
  case EncodeState::Legacy:
    Expects(next == EncodeState::LegacyReady, "legacy encoding produces packets");
    break;
  case EncodeState::Graphics:
  case EncodeState::LegacyReady:
    Expects(next == EncodeState::Idle, "completed encoding returns to idle");
    break;
  default:
    utilities::Unreachable(_state);
  }
  _state = next;
}
}
