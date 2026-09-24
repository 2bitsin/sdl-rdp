#pragma once
#include "audio-output.hpp"
#include "clipboard-store.hpp"
#include "configuration.hpp"
#include "diagnostics.hpp"
#include "event-queue.hpp"
#include "frame-store.hpp"
#include "listener.hpp"
#include "pinned.hpp"
#include "pointer-store.hpp"
#include "presenter.hpp"
#include "session.hpp"

#include <string>

struct sdlrdp_handle : private Backend::Pinned {
public:
       sdlrdp_handle(sdlrdp_config const& config, bool tracing);
  auto Port() const noexcept   -> unsigned;
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
auto SetError(sdlrdp_handle* handle, std::string text) -> void;
}
