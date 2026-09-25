#pragma once
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/factory.hpp>

#include <winpr/wtypes.h>
#include <cstddef>
#include <memory>
#include <span>

namespace Backend {
inline constexpr std::size_t RedirectionHandleLimit = 3;
class Activation;
class AudioChannel;
class ClipboardChannel;
class PeerLink;
class SessionAccess;
class Redirection {
public:
       Redirection(Redirection const&)                               = delete;
       Redirection(Redirection&&)                                    = delete;
       Redirection(PeerLink& link, Activation const& activation, SessionAccess& session,
                   Factory<std::unique_ptr<AudioChannel>> sound, Factory<std::unique_ptr<ClipboardChannel>> clipboard,
                   Factory<std::shared_ptr<sdl_rdp::drive::DriveChannel>> drive) noexcept;
       ~Redirection();
  auto operator=(Redirection const&)                 -> Redirection& = delete;
  auto operator=(Redirection&&)                      -> Redirection& = delete;
  auto OpenStatic(std::span<WaitHandle const> ready) -> bool;
  auto Sound(std::span<WaitHandle const> ready)      -> void;
  auto Audio() const noexcept                        -> AudioChannel*;
  auto Drive() const                                 -> std::shared_ptr<sdl_rdp::drive::DriveChannel>;
  auto LogAudio() const                              -> void;
  auto Disconnect()                                  -> void;
  auto Handles(std::span<WaitHandle> out) const      -> std::span<WaitHandle>;

private:
  auto OpenClipboard() -> bool;
  auto OpenDrive()     -> void;
  auto OpenSound()     -> bool;
  auto EndAudio()      -> void;
  PeerLink&                                              _link;
  Activation const&                                      _activation;
  SessionAccess&                                         _session;
  Factory<std::unique_ptr<AudioChannel>>                 _make_sound;
  Factory<std::unique_ptr<ClipboardChannel>>             _make_clipboard;
  Factory<std::shared_ptr<sdl_rdp::drive::DriveChannel>> _make_drive;
  std::unique_ptr<AudioChannel>                          _sound;
  std::unique_ptr<ClipboardChannel>                      _clipboard;
  std::shared_ptr<sdl_rdp::drive::DriveChannel>          _drive;
  bool                                                   _sound_attempted{ };
};
}
