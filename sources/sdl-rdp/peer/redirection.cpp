#include <sdl-rdp/peer/redirection.hpp>

#include <sdl-rdp/audio/channel.hpp>
#include <sdl-rdp/clipboard/channel.hpp>
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/link/session-access.hpp>

#include <freerdp/channels/rdpdr.h>
#include <algorithm>
#include <array>
#include <optional>
#include <ranges>
#include <utility>

namespace sdl_rdp::peer::detail::redirection {
using sdl_rdp::utilities::Expects;

namespace {
template <class ChannelTy> auto EventOf(ChannelTy const& channel) -> std::optional<WaitHandle> {
  if (!channel) return std::nullopt;
  return channel->Event();
}
}

Redirection::Redirection(PeerLink& link, Activation const& activation, SessionAccess& session, MakeSound sound,
                         MakeClipboard clipboard, MakeDrive drive) noexcept
    : _link{ link }, _activation{ activation }, _session{ session }, _make_sound{ std::move(sound) },
      _make_clipboard{ std::move(clipboard) }, _make_drive{ std::move(drive) } { }
Redirection::~Redirection() {
  Disconnect();
}
auto Redirection::OpenClipboard() -> bool {
  if (_clipboard || !_link.Channels().Joined(CLIPRDR_SVC_CHANNEL_NAME)) return true;
  _link.Invalidate();
  _clipboard = _make_clipboard();
  return _clipboard->Open();
}
auto Redirection::OpenDrive() -> void {
  if (_drive || !_link.Channels().Joined(RDPDR_SVC_CHANNEL_NAME)) return;
  _link.Invalidate();
  _drive = _make_drive();
  _drive->Open();
}
auto Redirection::OpenStatic(Signalled const& ready) -> bool {
  if (!OpenClipboard()) return false;
  OpenDrive();
  if (_drive) _drive->Pump(ready);
  return !_clipboard || _clipboard->Pump(ready);
}
auto Redirection::OpenSound() -> bool {
  if (std::exchange(_sound_attempted, true) || !_link.Channels().Joined(RDPSND_CHANNEL_NAME)) return true;
  _link.Invalidate();
  _sound = _make_sound();
  return _sound->Initialize();
}
auto Redirection::Sound(Signalled const& ready) -> void {
  if (!_activation.Active()) return;
  auto healthy = OpenSound();
  if (!_sound) return;
  if (healthy && ready.Contains(_sound->Event())) healthy = _sound->Pump();
  if (!healthy) EndAudio();
}
auto Redirection::EndAudio() -> void {
  _link.Invalidate();
  _sound.reset();
  _session.AudioGone();
}
auto Redirection::Audio() const noexcept -> std::optional<std::reference_wrapper<AudioChannel>> {
  if (!_sound) return std::nullopt;
  return std::ref(*_sound);
}
auto Redirection::Drive() const -> std::shared_ptr<DriveChannel> {
  return _drive;
}
auto Redirection::LogAudio() const -> void {
  if (_sound) _sound->LogAudio();
}
auto Redirection::Disconnect() -> void {
  if (_drive) _drive->Disconnect();
}
auto Redirection::Handles(std::span<WaitHandle> out) const -> std::span<WaitHandle> {
  Expects(out.size() >= RedirectionHandleLimit, "handle span has room for the redirection channels");
  auto const events = std::array{ EventOf(_drive), EventOf(_clipboard), EventOf(_sound) };
  auto const next   = std::ranges::copy(events | std::views::join, out.begin()).out;
  return { next, out.end() };
}
}
