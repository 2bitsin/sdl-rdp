#pragma once
#include <sdl-rdp/clipboard/store.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
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

struct sdlrdp_handle : private Backend::Pinned {
public:
       sdlrdp_handle(sdlrdp_config const& config, bool tracing);
  auto Port() const noexcept   -> std::uint32_t;
  auto Diagnostics() noexcept  -> Backend::Diagnostics&;
  auto Events() noexcept       -> Backend::EventQueue&;
  auto Presentation() noexcept -> Backend::Presenter&;
  auto Audio() noexcept        -> Backend::AudioOutput&;
  auto Session() noexcept      -> Backend::Session&;
  auto Clipboard() noexcept    -> Backend::ClipboardStore&;
  auto Frames() noexcept       -> Backend::FrameStore&;

private:
  Backend::Diagnostics    _diagnostics;
  Backend::EventQueue     _events;
  Backend::Configuration  _configuration;
  Backend::FrameStore     _frames;
  Backend::PointerStore   _pointer;
  Backend::ClipboardStore _clipboard;
  Backend::Session        _session;
  Backend::Presenter      _presenter;
  Backend::AudioOutput    _audio;
  Backend::Listener       _listener;
};
namespace Backend {
auto SetError(sdlrdp_handle& handle, std::string text) -> void;
}
