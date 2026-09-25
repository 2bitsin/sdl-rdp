#pragma once
#include <sdl-rdp/clipboard/store.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/drive/files.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/session/audio-output.hpp>
#include <sdl-rdp/session/listener.hpp>
#include <sdl-rdp/session/presenter.hpp>
#include <sdl-rdp/session/session.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/pointer/store.hpp>

#include <cstdint>
#include <string>
#include <string_view>

struct sdlrdp_handle : private sdl_rdp::utilities::Pinned {
public:
       sdlrdp_handle(sdlrdp_config const& config, bool tracing);
  auto Port() const noexcept                   -> std::uint32_t;
  auto Diagnostics() noexcept                  -> sdl_rdp::diagnostics::Diagnostics&;
  auto Events() noexcept                       -> sdl_rdp::link::EventQueue&;
  auto Presentation() noexcept                 -> sdl_rdp::session::Presenter&;
  auto Audio() noexcept                        -> sdl_rdp::session::AudioOutput&;
  auto Session() noexcept                      -> sdl_rdp::session::Session&;
  auto Clipboard() noexcept                    -> sdl_rdp::clipboard::ClipboardStore&;
  auto Frames() noexcept                       -> sdl_rdp::picture::FrameStore&;
  auto Drive()                                 -> sdl_rdp::drive::DriveFiles;
  auto SetClipboardText(std::string_view utf8) -> void;
  auto ClipboardText()                         -> std::string const&;
  auto HasClipboardText()                      -> bool;
  auto SetRelativeMouse(bool relative)         -> void;

private:
  sdl_rdp::diagnostics::Diagnostics     _diagnostics;
  sdl_rdp::link::EventQueue             _events;
  sdl_rdp::configuration::Configuration _configuration;
  sdl_rdp::picture::FrameStore          _frames;
  sdl_rdp::video::pointer::PointerStore _pointer;
  sdl_rdp::clipboard::ClipboardStore    _clipboard;
  sdl_rdp::session::Session             _session;
  sdl_rdp::session::Presenter           _presenter;
  sdl_rdp::session::AudioOutput         _audio;
  sdl_rdp::session::Listener            _listener;
};
namespace sdl_rdp::session::detail::handle {
auto SetError(sdlrdp_handle& handle, std::string text) -> void;
}

namespace sdl_rdp::session {
using detail::handle::SetError;
}
