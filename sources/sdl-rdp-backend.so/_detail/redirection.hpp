#pragma once
#include "factory.hpp"

#include <memory>
#include <span>
#include <winpr/wtypes.h>

namespace Backend {
inline constexpr unsigned RedirectionHandleLimit = 3;
class Activation;
class AudioChannel;
class ClipboardChannel;
class DriveChannel;
class PeerLink;
class SessionAccess;
class Redirection {
public:
                                Redirection(Redirection const&) = delete;
                                Redirection(Redirection&&)      = delete;
  Redirection(PeerLink& link, Activation const& activation, SessionAccess& session,
              Factory<std::unique_ptr<AudioChannel>> sound, Factory<std::unique_ptr<ClipboardChannel>> clipboard,
              Factory<std::shared_ptr<DriveChannel>> drive) noexcept;
                                ~Redirection();
  Redirection&                  operator = (Redirection const&) = delete;
  Redirection&                  operator = (Redirection&&)      = delete;
  bool                          OpenStatic(std::span<HANDLE const> ready);
  void                          Sound(std::span<HANDLE const> ready);
  AudioChannel*                 Audio() const                   noexcept;
  std::shared_ptr<DriveChannel> Drive() const;
  void                          LogAudio() const;
  void                          Disconnect();
  std::span<HANDLE>             Handles(std::span<HANDLE> out) const;

private:
  bool OpenClipboard();
  void OpenDrive();
  bool OpenSound();
  void EndAudio();
  PeerLink&                                  _link;
  Activation const&                          _activation;
  SessionAccess&                             _session;
  Factory<std::unique_ptr<AudioChannel>>     _make_sound;
  Factory<std::unique_ptr<ClipboardChannel>> _make_clipboard;
  Factory<std::shared_ptr<DriveChannel>>     _make_drive;
  std::unique_ptr<AudioChannel>              _sound;
  std::unique_ptr<ClipboardChannel>          _clipboard;
  std::shared_ptr<DriveChannel>              _drive;
  bool                                       _sound_attempted{ };
};
}
