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
bool Redirection::OpenClipboard() {
  if (_clipboard || !Joined(_link, CLIPRDR_SVC_CHANNEL_NAME)) return true;
  _link.Invalidate();
  _clipboard = _make_clipboard();
  return _clipboard->Open();
}
void Redirection::OpenDrive() {
  if (_drive || !Joined(_link, RDPDR_SVC_CHANNEL_NAME)) return;
  _link.Invalidate();
  _drive = _make_drive();
  _drive->Open();
}
bool Redirection::OpenStatic(std::span<HANDLE const> ready) {
  if (!OpenClipboard()) return false;
  OpenDrive();
  if (_drive) _drive->Pump(ready);
  return !_clipboard || _clipboard->Pump(ready);
}
bool Redirection::OpenSound() {
  if (std::exchange(_sound_attempted, true) || !Joined(_link, RDPSND_CHANNEL_NAME)) return true;
  _link.Invalidate();
  _sound = _make_sound();
  return _sound->Initialize();
}
void Redirection::Sound(std::span<HANDLE const> ready) {
  if (!_activation.Active()) return;
  auto healthy = OpenSound();
  if (!_sound) return;
  if (healthy && std::ranges::contains(ready, _sound->Event())) healthy = _sound->Pump();
  if (!healthy) EndAudio();
}
void Redirection::EndAudio() {
  _link.Invalidate();
  _sound.reset();
  _session.AudioGone();
}
AudioChannel* Redirection::Audio() const noexcept {
  return _sound.get();
}
std::shared_ptr<DriveChannel> Redirection::Drive() const {
  return _drive;
}
void Redirection::LogAudio() const {
  if (_sound) _sound->LogAudio();
}
void Redirection::Disconnect() {
  if (_drive) _drive->Disconnect();
}
std::span<HANDLE> Redirection::Handles(std::span<HANDLE> out) const {
  Expects(out.size() >= RedirectionHandleLimit, "handle span has room for the redirection channels");
  auto next = out.begin();
  if (_drive && _drive->Event()) *next++ = _drive->Event();
  if (_clipboard) *next++ = _clipboard->Event();
  if (_sound) *next++ = _sound->Event();
  return { next, out.end() };
}
}
