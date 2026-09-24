#pragma once
#include <sdl-rdp/utilities/factory.hpp>

#include <winpr/wtypes.h>
#include <memory>
#include <span>

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
       Redirection(Redirection const&)                           = delete;
       Redirection(Redirection&&)                                = delete;
       Redirection(PeerLink& link, Activation const& activation, SessionAccess& session,
                   Factory<std::unique_ptr<AudioChannel>> sound, Factory<std::unique_ptr<ClipboardChannel>> clipboard,
                   Factory<std::shared_ptr<DriveChannel>> drive) noexcept;
       ~Redirection();
  auto operator=(Redirection const&)             -> Redirection& = delete;
  auto operator=(Redirection&&)                  -> Redirection& = delete;
  auto OpenStatic(std::span<HANDLE const> ready) -> bool;
  auto Sound(std::span<HANDLE const> ready)      -> void;
  auto Audio() const noexcept                    -> AudioChannel*;
  auto Drive() const                             -> std::shared_ptr<DriveChannel>;
  auto LogAudio() const                          -> void;
  auto Disconnect()                              -> void;
  auto Handles(std::span<HANDLE> out) const      -> std::span<HANDLE>;

private:
  auto OpenClipboard() -> bool;
  auto OpenDrive()     -> void;
  auto OpenSound()     -> bool;
  auto EndAudio()      -> void;
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
