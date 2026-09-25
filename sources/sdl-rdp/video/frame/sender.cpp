#include <sdl-rdp/video/frame/sender.hpp>

#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/video/frame/capture.hpp>
#include <sdl-rdp/video/frame/gate.hpp>
#include <sdl-rdp/video/graphics-link.hpp>
#include <sdl-rdp/video/legacy-frame.hpp>
#include <sdl-rdp/video/pointer/sender.hpp>

namespace sdl_rdp::video::frame::detail::sender {
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Unreachable;

FrameSender::FrameSender(PeerLink& link, Activation const& activation, SessionAccess& session, FrameGate& gate,
                         FrameCapture& capture, PointerSender& pointer, GraphicsLink& graphics,
                         LegacyFrame& legacy) noexcept
    : _link{ link }, _activation{ activation }, _session{ session }, _gate{ gate }, _capture{ capture },
      _pointer{ pointer }, _graphics{ graphics }, _legacy{ legacy } { }
auto FrameSender::Drain() -> bool {
  if (!_activation.Active()) return true;
  auto& client = _link.Client();
  if (client.DrainOutputBuffer(&client) < 0) return false;
  if (!_gate.Admit() || _activation.Holding()) return true;
  if (!_pointer.Send()) return false;
  auto const captured = _capture.Next();
  switch (captured) {
  case CaptureState::Failed:   return false;
  case CaptureState::Idle:     return true;
  case CaptureState::Captured: return Prepare();
  default:                     Unreachable(captured);
  }
}
auto FrameSender::Encode(std::stop_token const& quit) -> Delivery {
  auto const kind = _state;
  if (kind == EncodeState::Idle || kind == EncodeState::LegacyReady) return Delivery::Healthy;
  auto const encoded = kind == EncodeState::Legacy ? _legacy.Encode() : _graphics.Channel().Encode();
  auto const session = _session.Lock();
  Transition(kind == EncodeState::Legacy ? EncodeState::LegacyReady : EncodeState::Idle);
  if (quit.stop_requested()) return Delivery::Stopped;
  if (!_activation.Active()) return Delivery::Healthy;
  return encoded && Transmit(kind) ? Delivery::Healthy : Delivery::Failed;
}
auto FrameSender::Transmit(EncodeState kind) -> bool {
  return kind == EncodeState::Legacy ? SendLegacy() : _graphics.Channel().Send();
}
auto FrameSender::Prepare() -> bool {
  if (_state == EncodeState::LegacyReady) return SendLegacy();
  auto const confirmed = _graphics.Confirmed();
  if (!(confirmed ? _graphics.Channel().Prepare() : _legacy.Prepare())) return false;
  Transition(confirmed ? EncodeState::Graphics : EncodeState::Legacy);
  return true;
}
auto FrameSender::SendLegacy() -> bool {
  if (!_legacy.Send()) return false;
  if (_legacy.Delivered()) Transition(EncodeState::Idle);
  return true;
}
auto FrameSender::Transition(EncodeState next) -> void {
  switch (_state) {
  case EncodeState::Idle:   Expects(next != EncodeState::LegacyReady, "encoding precedes legacy writes"); break;
  case EncodeState::Legacy: Expects(next == EncodeState::LegacyReady, "legacy encoding produces packets"); break;
  case EncodeState::Graphics:
  case EncodeState::LegacyReady: Expects(next == EncodeState::Idle, "completed encoding returns to idle"); break;
  default:                       Unreachable(_state);
  }
  _state = next;
}
}
