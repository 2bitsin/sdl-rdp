#include <sdl-rdp/video/frame/pacing.hpp>

#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/diagnostics/trace-queue.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/wire.hpp>
#include <sdl-rdp/video/peer-frames.hpp>

#include <cstdint>
#include <format>
#include <utility>

namespace sdl_rdp::video::frame::detail::pacing {
using sdl_rdp::configuration::MillihertzPerHz;
using sdl_rdp::configuration::RefreshMode;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::link::RefreshChanged;
using sdl_rdp::link::SampleWire;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Unreachable;

namespace {
using Clock        = AcknowledgementWindow::Clock;
using Milliseconds = std::chrono::duration<double, std::milli>;
}
FramePacing::FramePacing(Diagnostics const& diagnostics, EventQueue& events, Configuration const& configuration,
                         FrameStore& store, PeerLink& link, Activation const& activation, TraceQueue& traces,
                         FrameStatistics& statistics) noexcept
    : _diagnostics{ diagnostics }, _events{ events }, _configuration{ configuration }, _store{ store }, _link{ link },
      _activation{ activation }, _traces{ traces }, _statistics{ statistics }, _effective{ _refresh.Rate() } { }
auto FramePacing::Adjust(std::invocable<Refresh&> auto step) -> void {
  auto const previous = _refresh.Rate();
  step(_refresh);
  auto const rate = _refresh.Rate();
  Expects(rate > 0, "effective refresh is positive");
  _effective.store(rate);
  if (rate == previous || !_activation.Active()) return;
  _events.Push(RefreshChanged{ rate * MillihertzPerHz });
  _diagnostics.Line("refresh", [&] { return std::format("hz={}", rate); });
}
auto FramePacing::Restart(FrameLock const& held) -> void {
  Expects(_store.Holds(held), "restarting pacing holds the frame lock");
  _window.Clear();
  Adjust([&](Refresh& rate) {
    rate = _configuration.RefreshPolicy();
    rate.Restart();
  });
}
auto FramePacing::Blocked() -> void {
  _diagnostics.Line("wire-blocked");
  Adjust([](Refresh& rate) { rate.Blocked(Clock::now()); });
}
auto FramePacing::Drained() -> void {
  if (_refresh.AwaitingEmpty())
    Adjust([&](Refresh& rate) { rate.Drained(SampleWire(_link.Connection().PeerSocket())); });
}
auto FramePacing::Sent(PeerFrames& frames, FrameCost const& cost) -> void {
  auto const held = _store.Lock();
  auto const now  = Clock::now();
  auto const wire = SampleWire(_link.Connection().PeerSocket());
  _traces.Defer("frame", [&] {
    return std::format("id={} bytes={} outq={} unacked={} tcp_rtt={}", _window.Frame(), cost.bytes, wire.outq,
                       wire.unacked, wire.rtt);
  });
  if (!wire.available && _refresh.Mode() == RefreshMode::Sender && !std::exchange(_unavailable_logged, true))
    _diagnostics.Log(LogLevel::Warn, "auto-sender TCP measurements unavailable; adapting only to blocked writes.");
  Adjust([&](Refresh& rate) {
    if (cost.bytes) rate.Written(wire, cost.bytes);
  });
  _window.Record(frames.Sequence(), now);
  _statistics.Sent(cost, wire.outq);
  frames.Complete(held);
}
auto FramePacing::Accept(std::uint32_t id) -> void {
  auto const held    = _store.Lock();
  auto const settled = _window.Accept(id);
  if (settled.empty()) return;
  auto const now = Clock::now();
  _traces.Defer("ack",
                [&] { return std::format("id={} age={:.1f}", id, Milliseconds{ now - settled.back().at }.count()); });
  for (auto const& sent : settled) {
    _statistics.Acknowledged(now - sent.at);
    if (_refresh.Mode() == RefreshMode::Average && &sent != &settled.back()) continue;
    Adjust([&](Refresh& rate) { rate.Acknowledge(now, now - sent.at); });
  }
  _store.Notify();
  _link.Signal();
}
auto FramePacing::Acknowledgements(AcknowledgementMode mode) -> void {
  {
    auto const held = _store.Lock();
    switch (mode) {
    case AcknowledgementMode::Suspended: _window.Disable(); break;
    case AcknowledgementMode::Restarted: _window.Clear(); [[fallthrough]];
    case AcknowledgementMode::Tracking:  _window.Enable(); break;
    default:                             Unreachable(mode);
    }
  }
  _store.Notify();
  _link.Signal();
}
auto FramePacing::Timeout() -> std::uint32_t {
  auto const held = _store.Lock();
  return _window.Remaining(Clock::now());
}
auto FramePacing::Effective() const noexcept -> std::uint32_t {
  return _effective.load();
}
auto FramePacing::Frame() const noexcept -> std::uint32_t {
  return _window.Frame();
}
auto FramePacing::Begin() noexcept -> void {
  std::ignore = _window.Next();
}
auto FramePacing::Settled(FrameLock const& held, std::uint64_t target) const -> bool {
  Expects(_store.Holds(held), "reading acknowledgements holds the frame lock");
  return _window.Settled(target);
}
auto FramePacing::Acknowledged(FrameLock const& held) const -> std::uint64_t {
  Expects(_store.Holds(held), "reading acknowledgements holds the frame lock");
  return _window.Acknowledged();
}
}
