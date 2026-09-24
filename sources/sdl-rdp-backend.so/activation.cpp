#include "_detail/activation.hpp"

#include "_detail/contract.hpp"
#include "_detail/event-queue.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/refresh.hpp"

#include <utility>

namespace Backend {
Activation::Activation(EventQueue& events, PeerLink& link) noexcept : _events{ events }, _link{ link } { }
auto Activation::Activate() -> void {
  _active       = true;
  _activated    = true;
  _activated_at = Clock::now();
}
auto Activation::Deactivate() noexcept -> bool {
  return _active.exchange(false);
}
auto Activation::Active() const noexcept -> bool {
  return _active.load();
}
auto Activation::Activated() const noexcept -> bool {
  return _activated;
}
auto Activation::Finish() noexcept -> void {
  _finished = true;
}
auto Activation::Finished() const noexcept -> bool {
  return _finished.load();
}
auto Activation::ActivatedAt() const noexcept -> Activation::Clock::time_point {
  return _activated_at;
}
auto Activation::Hold(sdlrdp_event connection, sdlrdp_event screen) -> void {
  Expects(connection.type == SDLRDP_CONNECTED, "held event announces a connection");
  Expects(screen.type == SDLRDP_SCREEN, "held screen event describes the client screen");
  _connection = connection;
  _screen     = screen;
}
auto Activation::Holding() const noexcept -> bool {
  return _connection.has_value();
}
auto Activation::Announce(sdlrdp_codec codec, unsigned refresh_hz) -> void {
  auto connection = std::exchange(_connection, std::nullopt);
  if (!connection) return;
  connection->connected.codec              = codec;
  connection->connected.refresh_millihertz = refresh_hz * MillihertzPerHz;
  _events.Push(*connection);
  _events.Push(_screen);
  _link.Signal();
}
auto Activation::CodecChanged(sdlrdp_codec codec) -> void {
  if (Active() && !Holding()) _events.Push({ .type = SDLRDP_CODEC_CHANGED, .codec_changed = { codec } });
}
auto Activation::Suppress() noexcept -> void {
  _suppressed = true;
}
auto Activation::Resume() noexcept -> void {
  _suppressed = false;
}
auto Activation::Suppressed() const noexcept -> bool {
  return _suppressed;
}
}
