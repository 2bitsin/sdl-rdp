#include "_detail/activation.hpp"

#include "_detail/contract.hpp"
#include "_detail/event-queue.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/refresh.hpp"

#include <utility>

namespace Backend {
Activation::Activation(EventQueue& events, PeerLink& link) noexcept : _events{ events }, _link{ link } { }
void Activation::Activate() {
  _active       = true;
  _activated    = true;
  _activated_at = Clock::now();
}
bool Activation::Deactivate() noexcept {
  return _active.exchange(false);
}
bool Activation::Active() const noexcept {
  return _active.load();
}
bool Activation::Activated() const noexcept {
  return _activated;
}
void Activation::Finish() noexcept {
  _finished = true;
}
bool Activation::Finished() const noexcept {
  return _finished.load();
}
Activation::Clock::time_point Activation::ActivatedAt() const noexcept {
  return _activated_at;
}
void Activation::Hold(sdlrdp_event connection, sdlrdp_event screen) {
  Expects(connection.type == SDLRDP_CONNECTED, "held event announces a connection");
  Expects(screen.type == SDLRDP_SCREEN, "held screen event describes the client screen");
  _connection = connection;
  _screen     = screen;
}
bool Activation::Holding() const noexcept {
  return _connection.has_value();
}
void Activation::Announce(sdlrdp_codec codec, unsigned refresh_hz) {
  auto connection = std::exchange(_connection, std::nullopt);
  if (!connection) return;
  connection->connected.codec              = codec;
  connection->connected.refresh_millihertz = refresh_hz * MillihertzPerHz;
  _events.Push(*connection);
  _events.Push(_screen);
  _link.Signal();
}
void Activation::CodecChanged(sdlrdp_codec codec) {
  if (Active() && !Holding()) _events.Push({ .type = SDLRDP_CODEC_CHANGED, .codec_changed = { codec } });
}
void Activation::Suppress() noexcept {
  _suppressed = true;
}
void Activation::Resume() noexcept {
  _suppressed = false;
}
bool Activation::Suppressed() const noexcept {
  return _suppressed;
}
}
