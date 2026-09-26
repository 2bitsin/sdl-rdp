#pragma once
#include <sdl-rdp/audio/forward.hpp>
#include <sdl-rdp/clipboard/forward.hpp>
#include <sdl-rdp/drive/channel.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <functional>
#include <memory>
#include <span>

namespace sdl_rdp::peer::detail::redirection {
using sdl_rdp::audio::AudioChannel;
using sdl_rdp::clipboard::ClipboardChannel;
using sdl_rdp::drive::DriveChannel;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::utilities::Pinned;

using MakeSound     = std::move_only_function<auto()->std::unique_ptr<AudioChannel>>;
using MakeClipboard = std::move_only_function<auto()->std::unique_ptr<ClipboardChannel>>;
using MakeDrive     = std::move_only_function<auto()->std::shared_ptr<DriveChannel>>;

inline constexpr std::size_t RedirectionHandleLimit = 3;
class Redirection : private Pinned {
public:
       Redirection(PeerLink& link, Activation const& activation, SessionAccess& session, MakeSound sound,
                   MakeClipboard clipboard, MakeDrive drive) noexcept;
       ~Redirection();
  auto OpenStatic(Signalled const& ready)       -> bool;
  auto Sound(Signalled const& ready)            -> void;
  auto Audio() const noexcept                   -> std::optional<std::reference_wrapper<AudioChannel>>;
  auto Drive() const                            -> std::shared_ptr<DriveChannel>;
  auto LogAudio() const                         -> void;
  auto Disconnect()                             -> void;
  auto Handles(std::span<WaitHandle> out) const -> std::span<WaitHandle>;

private:
  auto OpenClipboard() -> bool;
  auto OpenDrive()     -> void;
  auto OpenSound()     -> bool;
  auto EndAudio()      -> void;
  PeerLink&                         _link;
  Activation const&                 _activation;
  SessionAccess&                    _session;
  MakeSound                         _make_sound;
  MakeClipboard                     _make_clipboard;
  MakeDrive                         _make_drive;
  std::unique_ptr<AudioChannel>     _sound;
  std::unique_ptr<ClipboardChannel> _clipboard;
  std::shared_ptr<DriveChannel>     _drive;
  bool                              _sound_attempted{ };
};
}

namespace sdl_rdp::peer {
using detail::redirection::MakeClipboard;
using detail::redirection::MakeDrive;
using detail::redirection::MakeSound;
using detail::redirection::Redirection;
using detail::redirection::RedirectionHandleLimit;
}
