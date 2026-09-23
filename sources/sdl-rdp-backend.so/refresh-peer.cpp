#include "_detail/state.hpp"
namespace Backend {
void State::SetRefresh(RefreshMode mode, unsigned ceiling)
{
  Expects(ceiling > 0, "declared refresh is positive");
  std::scoped_lock lock(peers_guard, frame_guard);
  refresh.mode = mode;
  refresh.ceiling = ceiling;
  refresh.Restart();
  for (auto const& peer : peers) peer->RestartRefresh();
}
void Peer::RestartRefresh()
{
  Expects(client != nullptr, "peer exists");
  auto previous = refresh.rate;
  refresh = owner.refresh;
  refresh.Restart();
  pending.clear();
  PublishRefresh(previous);
}
void Peer::PublishRefresh(unsigned previous)
{
  Expects(refresh.rate > 0, "effective refresh is positive");
  effective_refresh.store(refresh.rate);
  if (refresh.rate == previous || owner.current != this) return;
  owner.Push({.type = SDLRDP_REFRESH, .refresh = {refresh.rate * 1000}});
  owner.trace.Line("refresh", [&] { return std::format("hz={}", refresh.rate); });
}
void Peer::MeasureWire(std::size_t bytes)
{
  Expects(client != nullptr, "peer exists");
  auto wire = SampleWire(socket_descriptor);
  outq_total += wire.outq;
  outq_max = std::max(outq_max, wire.outq);
  if (owner.trace.enabled) trace_pending.push_back(owner.trace.Format("frame", [&] { return std::format("id={} bytes={} outq={} unacked={} tcp_rtt={}",
    frame_id, bytes, wire.outq, wire.unacked, wire.rtt); }));
  if (!wire.available && refresh.mode == RefreshMode::Sender && !wire_unavailable_logged) {
    owner.Log(SDLRDP_LOG_WARN, "auto-sender TCP measurements unavailable; adapting only to blocked writes.");
    wire_unavailable_logged = true;
  }
  auto previous = refresh.rate;
  if (bytes) refresh.Written(wire, bytes);
  PublishRefresh(previous);
}
}

namespace Backend {
void Peer::RecordAcknowledgements(std::deque<Pending>::iterator last, Clock::time_point now)
{
  Expects(last != pending.end(), "acknowledged frame is pending");
  for (auto frame = pending.begin(); frame != last + 1; ++frame) {
    RecordAcknowledgement(now - frame->sent);
    if (refresh.mode == RefreshMode::Average && frame != last) continue;
    auto previous = refresh.rate;
    refresh.Acknowledge(now, now - frame->sent);
    PublishRefresh(previous);
  }
}
}
