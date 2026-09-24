#include "_detail/redirection.hpp"

#include "_detail/activation.hpp"
#include "_detail/audio.hpp"
#include "_detail/clipboard.hpp"
#include "_detail/drive-channel.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/session-access.hpp"

#include <algorithm>
#include <freerdp/channels/rdpdr.h>
#include <utility>

namespace Backend {
Redirection::Redirection(PeerLink& link, Activation const& activation, SessionAccess& session,
                         Factory<std::unique_ptr<AudioChannel>> sound,
                         Factory<std::unique_ptr<ClipboardChannel>> clipboard,
                         Factory<std::shared_ptr<DriveChannel>> drive) noexcept
    : _link { link }, _activation{ activation }, _session{ session }, _make_sound{ std::move(sound) },
      _make_clipboard{ std::move(clipboard) }, _make_drive{ std::move(drive) } { }
Redirection::~Redirection() {
  Disconnect();
}
auto Redirection::OpenClipboard() -> bool {
  if (_clipboard || !Joined(_link, CLIPRDR_SVC_CHANNEL_NAME)) return true;
  _link.Invalidate();
  _clipboard = _make_clipboard();
  return _clipboard->Open();
}
auto Redirection::OpenDrive() -> void {
  if (_drive || !Joined(_link, RDPDR_SVC_CHANNEL_NAME)) return;
  _link.Invalidate();
  _drive = _make_drive();
  _drive->Open();
}
auto Redirection::OpenStatic(std::span<HANDLE const> ready) -> bool {
  if (!OpenClipboard()) return false;
  OpenDrive();
  if (_drive) _drive->Pump(ready);
  return !_clipboard || _clipboard->Pump(ready);
}
auto Redirection::OpenSound() -> bool {
  if (std::exchange(_sound_attempted, true) || !Joined(_link, RDPSND_CHANNEL_NAME)) return true;
  _link.Invalidate();
  _sound = _make_sound();
  return _sound->Initialize();
}
auto Redirection::Sound(std::span<HANDLE const> ready) -> void {
  if (!_activation.Active()) return;
  auto healthy = OpenSound();
  if (!_sound) return;
  if (healthy && std::ranges::contains(ready, _sound->Event())) healthy = _sound->Pump();
  if (!healthy) EndAudio();
}
auto Redirection::EndAudio() -> void {
  _link.Invalidate();
  _sound.reset();
  _session.AudioGone();
}
auto Redirection::Audio() const noexcept -> AudioChannel* {
  return _sound.get();
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
auto Redirection::Handles(std::span<HANDLE> out) const -> std::span<HANDLE> {
  Expects(out.size() >= RedirectionHandleLimit, "handle span has room for the redirection channels");
  auto next = out.begin();
  if (_drive && _drive->Event()) *next++ = _drive->Event();
  if (_clipboard) *next++ = _clipboard->Event();
  if (_sound) *next++ = _sound->Event();
  return { next, out.end() };
}
}
