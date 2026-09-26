#pragma once
#include <sdl-rdp/clipboard/store.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/configuration/credential-check.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/drive/files.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/session/audio-output.hpp>
#include <sdl-rdp/session/listener.hpp>
#include <sdl-rdp/session/presenter.hpp>
#include <sdl-rdp/session/session.hpp>
#include <sdl-rdp/utilities/generational.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/pointer/shape.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace sdl_rdp::session::detail::backend {
using sdl_rdp::clipboard::ClipboardStore;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::configuration::CredentialCheck;
using sdl_rdp::configuration::Setup;
using sdl_rdp::diagnostics::LogSink;
using sdl_rdp::drive::DriveFiles;
using sdl_rdp::link::EventQueue;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::Generational;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::pointer::PointerShape;

// The composition root: one listener, its session and everything a driver reaches.
class Backend : private Pinned {
public:
       Backend(Setup const& setup, LogSink& log, CredentialCheck const& check);
  auto Port() const noexcept                   -> std::uint32_t;
  auto Diagnostics() noexcept                  -> diagnostics::Diagnostics&;
  auto Events() noexcept                       -> EventQueue&;
  auto Presentation() noexcept                 -> Presenter&;
  auto Audio() noexcept                        -> AudioOutput&;
  auto Session() noexcept                      -> session::Session&;
  auto Clipboard() noexcept                    -> ClipboardStore&;
  auto Frames() noexcept                       -> FrameStore&;
  auto Drive()                                 -> DriveFiles;
  auto SetClipboardText(std::string_view utf8) -> void;
  auto ClipboardText()                         -> std::string;
  auto HasClipboardText()                      -> bool;
  auto SetRelativeMouse(bool relative)         -> void;

private:
  diagnostics::Diagnostics   _diagnostics;
  EventQueue                 _events;
  Configuration              _configuration;
  FrameStore                 _frames;
  Generational<PointerShape> _pointer;
  ClipboardStore             _clipboard;
  session::Session           _session;
  Presenter                  _presenter;
  AudioOutput                _audio;
  Listener                   _listener;
};
}

namespace sdl_rdp::session {
using detail::backend::Backend;
}
