#include "_detail/frame-pacing.hpp"

#include "_detail/activation.hpp"
#include "_detail/configuration.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/event-queue.hpp"
#include "_detail/peer-frames.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/trace-queue.hpp"

#include <format>

namespace Backend {
namespace {
using Clock        = AcknowledgementWindow::Clock;
using Milliseconds = std::chrono::duration<double, std::milli>;
void WarnUnmeasured(RefreshTracker& refresh, Diagnostics const& diagnostics, WireSample const& wire) {
  if (wire.available || refresh.Mode() != RefreshMode::Sender || refresh.TestAndSetUnavailableLogged()) return;
  diagnostics.Log(SDLRDP_LOG_WARN, "auto-sender TCP measurements unavailable; adapting only to blocked writes.");
}
}
FramePacing::FramePacing(Diagnostics const& diagnostics, EventQueue& events, Configuration const& configuration,
                         FrameStore& store, PeerLink& link, Activation const& activation, TraceQueue& traces,
                         FrameStatistics& statistics) noexcept
    : _diagnostics { diagnostics }, _events{ events }, _configuration{ configuration }, _store{ store }, _link{ link },
      _activation{ activation }, _traces{ traces }, _statistics{ statistics } { }
void FramePacing::Adjust(std::invocable<Refresh&> auto step) {
  if (!_refresh.Adjust(step) || !_activation.Active()) return;
  _events.Push({ .type = SDLRDP_REFRESH, .refresh = { _refresh.Effective() * MillihertzPerHz } });
  _diagnostics.Line("refresh", [&] { return std::format("hz={}", _refresh.Effective()); });
}
void FramePacing::Restart(FrameLock const& held) {
  Expects(_store.Holds(held), "restarting pacing holds the frame lock");
  _window.Clear();
  Adjust([&](Refresh& rate) {
    rate = _configuration.RefreshPolicy();
    rate.Restart();
  });
}
void FramePacing::Blocked() {
  _diagnostics.Line("wire-blocked");
  Adjust([](Refresh& rate) { rate.Blocked(Clock::now()); });
}
void FramePacing::Drained() {
  if (_refresh.AwaitingEmpty()) Adjust([&](Refresh& rate) { rate.Drained(SampleWire(_link.Socket())); });
}
void FramePacing::Sent(PeerFrames& frames, FrameCost const& cost) {
  auto const held = _store.Lock();
  auto const now  = Clock::now();
  auto const wire = SampleWire(_link.Socket());
  _traces.Defer("frame", [&] {
    return std::format("id={} bytes={} outq={} unacked={} tcp_rtt={}", _window.Frame(), cost.bytes, wire.outq,
                       wire.unacked, wire.rtt);
  });
  WarnUnmeasured(_refresh, _diagnostics, wire);
  Adjust([&](Refresh& rate) {
    if (cost.bytes) rate.Written(wire, cost.bytes);
  });
  _window.Record(frames.Sequence(), now);
  _statistics.Sent(cost, wire.outq);
  frames.Complete(held);
}
void FramePacing::Accept(UINT32 id) {
  auto const held    = _store.Lock();
  auto const settled = _window.Accept(id);
  if (settled.empty()) return;
  auto const now = Clock::now();
  _traces.Defer("ack",
                [&] { return std::format("id={} age={:.1f}", id, Milliseconds(settled.back().Age(now)).count()); });
  for (auto const& sent : settled) {
    _statistics.Acknowledged(sent.Age(now));
    if (_refresh.Mode() == RefreshMode::Average && &sent != &settled.back()) continue;
    Adjust([&](Refresh& rate) { rate.Acknowledge(now, sent.Age(now)); });
  }
  _store.Notify();
  _link.Signal();
}
void FramePacing::Acknowledgements(AcknowledgementMode mode) {
  {
    auto const held = _store.Lock();
    switch (mode) {
    case AcknowledgementMode::Suspended:
      _window.Disable();
      break;
    case AcknowledgementMode::Restarted:
      _window.Clear();
      [[fallthrough]];
    case AcknowledgementMode::Tracking:
      _window.Enable();
      break;
    default:
      utilities::Unreachable(mode);
    }
  }
  _store.Notify();
  _link.Signal();
}
DWORD FramePacing::Timeout() {
  auto const held = _store.Lock();
  return _window.Remaining(Clock::now());
}
unsigned FramePacing::Effective() const noexcept {
  return _refresh.Effective();
}
UINT32 FramePacing::Frame() const noexcept {
  return _window.Frame();
}
void FramePacing::Begin() noexcept {
  std::ignore = _window.Next();
}
bool FramePacing::Settled(FrameLock const& held, uint64_t target) const {
  Expects(_store.Holds(held), "reading acknowledgements holds the frame lock");
  return _window.Settled(target);
}
uint64_t FramePacing::Acknowledged(FrameLock const& held) const {
  Expects(_store.Holds(held), "reading acknowledgements holds the frame lock");
  return _window.Acknowledged();
}
}
