#include <sdl-rdp/session/handle.hpp>

#include <sdl-rdp/auth/certificate.hpp>
#include <sdl-rdp/peer/peer.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace sdl_rdp::session::detail::handle {
using sdl_rdp::auth::Credentials;
using sdl_rdp::auth::EnsureCertificate;

namespace {
auto Ensured(std::filesystem::path const& directory) -> Credentials {
  Credentials credentials{ directory };
  EnsureCertificate(credentials);
  return credentials;
}
}
auto SetError(sdlrdp_handle& handle, std::string text) -> void {
  handle.Diagnostics().Fail(std::move(text));
}
}

using sdl_rdp::clipboard::ClipboardStore;
using sdl_rdp::drive::DriveFiles;
using sdl_rdp::freerdp_facade::PeerHandle;
using sdl_rdp::input::MouseMode;
using sdl_rdp::link::EventQueue;
using sdl_rdp::peer::Peer;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::session::AudioOutput;
using sdl_rdp::session::OnCurrent;
using sdl_rdp::session::Presenter;
using sdl_rdp::session::detail::handle::Ensured;

sdlrdp_handle::sdlrdp_handle(sdlrdp_config const& config, bool tracing)
    : _diagnostics{ config, tracing }, _configuration{ config },
      _frames{ { .width = config.width, .height = config.height }, config.aspect }, _session{ _frames, _events },
      _presenter{ _diagnostics, _frames, _session, _pointer, _configuration },
      _audio    { _session, _presenter, _configuration                      },
      _listener{ _configuration, Ensured(_configuration.CertificateDirectory()), _diagnostics, _session,
                 [this](PeerHandle accepted) {
                   return std::make_unique<Peer>(std::move(accepted), _diagnostics, _events, _configuration, _frames,
                                                 _pointer, _clipboard, _session);
                 } } { }
auto sdlrdp_handle::Port() const noexcept -> std::uint32_t {
  return _listener.Port();
}
auto sdlrdp_handle::Diagnostics() noexcept -> sdl_rdp::diagnostics::Diagnostics& {
  return _diagnostics;
}
auto sdlrdp_handle::Events() noexcept -> EventQueue& {
  return _events;
}
auto sdlrdp_handle::Presentation() noexcept -> Presenter& {
  return _presenter;
}
auto sdlrdp_handle::Audio() noexcept -> AudioOutput& {
  return _audio;
}
auto sdlrdp_handle::Session() noexcept -> sdl_rdp::session::Session& {
  return _session;
}
auto sdlrdp_handle::Clipboard() noexcept -> ClipboardStore& {
  return _clipboard;
}
auto sdlrdp_handle::Frames() noexcept -> FrameStore& {
  return _frames;
}
auto sdlrdp_handle::Drive() -> DriveFiles {
  return DriveFiles{ OnCurrent(_session, [](Peer& peer) { return peer.Redirected().Drive(); }) };
}
auto sdlrdp_handle::SetClipboardText(std::string_view utf8) -> void {
  auto const held = _session.Lock();
  std::ignore = _clipboard.Replace(std::string{ utf8 });
  OnCurrent(_session, [](Peer& current) { current.Signal(); });
}
auto sdlrdp_handle::ClipboardText() -> std::string const& {
  auto const held = _session.Lock();
  return _clipboard.Export();
}
auto sdlrdp_handle::HasClipboardText() -> bool {
  auto const held = _session.Lock();
  return !_clipboard.Text().empty();
}
auto sdlrdp_handle::SetRelativeMouse(bool relative) -> void {
  auto const mode = relative ? MouseMode::Relative : MouseMode::Absolute;
  OnCurrent(_session, [mode](Peer& current) { current.Point(mode); });
}
