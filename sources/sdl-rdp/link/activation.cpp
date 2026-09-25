#include <sdl-rdp/link/activation.hpp>

#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/peer-link.hpp>

#include <cstdint>
#include <utility>

namespace sdl_rdp::link::detail::activation {
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::MillihertzPerHz;

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
auto Activation::Hold(Connected connection, ScreenChanged screen) -> void {
  _connection = std::move(connection);
  _screen     = screen;
}
auto Activation::Holding() const noexcept -> bool {
  return _connection.has_value();
}
auto Activation::Announce(Codec codec, std::uint32_t refresh_hz) -> void {
  auto connection = std::exchange(_connection, std::nullopt);
  if (!connection) return;
  connection->codec              = codec;
  connection->refresh_millihertz = refresh_hz * MillihertzPerHz;
  _events.Push(std::move(*connection));
  _events.Push(_screen);
  _link.Signal();
}
auto Activation::CodecChanged(Codec codec) -> void {
  if (Active() && !Holding()) _events.Push(sdl_rdp::link::CodecChanged{ codec });
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
