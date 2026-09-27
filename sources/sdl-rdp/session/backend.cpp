#include <sdl-rdp/session/backend.hpp>

#include <sdl-rdp/auth/certificate.hpp>
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/utilities/deadline.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <tuple>
#include <utility>

namespace sdl_rdp::session::detail::backend {
using sdl_rdp::auth::Credentials;
using sdl_rdp::auth::EnsureCertificate;
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::input::MouseMode;
using sdl_rdp::peer::Peer;
using sdl_rdp::utilities::Deadline;

namespace {
auto Ensured(std::filesystem::path const& directory) -> Credentials {
  Credentials credentials{ directory };
  EnsureCertificate(credentials);
  return credentials;
}
}
Backend::Backend(Setup const& setup, LogSink& log, CredentialCheck const& check)
    : _diagnostics{ log, setup.tracing }, _configuration{ setup, check },
      _frames{ { .width = setup.width, .height = setup.height }, setup.aspect }, _session{ _frames, _events },
      _presenter{ _diagnostics, _frames, _session, _pointer, _configuration },
      _audio    { _session, _presenter, _configuration                      },
      _listener{ _configuration, Ensured(_configuration.CertificateDirectory()), _diagnostics, _session,
                 [this](Connection accepted) {
                   return std::make_unique<Peer>(std::move(accepted), _diagnostics, _events, _configuration, _frames,
                                                 _pointer, _clipboard, _session);
                 } } {
  if (setup.wait_for_client) _events.Wait(Deadline::max());
}
auto Backend::Port() const noexcept -> std::uint32_t {
  return _listener.Port();
}
auto Backend::Diagnostics() noexcept -> diagnostics::Diagnostics& {
  return _diagnostics;
}
auto Backend::Events() noexcept -> EventQueue& {
  return _events;
}
auto Backend::Presentation() noexcept -> Presenter& {
  return _presenter;
}
auto Backend::Audio() noexcept -> AudioOutput& {
  return _audio;
}
auto Backend::Session() noexcept -> session::Session& {
  return _session;
}
auto Backend::Clipboard() noexcept -> ClipboardStore& {
  return _clipboard;
}
auto Backend::Frames() noexcept -> FrameStore& {
  return _frames;
}
auto Backend::Drive() -> DriveFiles {
  return DriveFiles{ OnCurrent(_session, [](Peer& peer) { return peer.Redirected().Drive(); }) };
}
auto Backend::SetClipboardText(std::string_view utf8) -> void {
  auto const held = _session.Lock();
  std::ignore = _clipboard.Replace(std::string{ utf8 });
  OnCurrent(_session, [](Peer& current) { current.Signal(); });
}
auto Backend::ClipboardText() -> std::string {
  auto const held = _session.Lock();
  return _clipboard.Text();
}
auto Backend::HasClipboardText() -> bool {
  auto const held = _session.Lock();
  return !_clipboard.Text().empty();
}
auto Backend::SetRelativeMouse(bool relative) -> void {
  auto const mode = relative ? MouseMode::Relative : MouseMode::Absolute;
  OnCurrent(_session, [mode](Peer& current) { current.Point(mode); });
}
}
