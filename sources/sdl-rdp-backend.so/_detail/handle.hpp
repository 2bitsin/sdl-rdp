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
  unsigned                 Port() const   noexcept;
  Backend::Diagnostics&    Diagnostics()  noexcept;
  Backend::EventQueue&     Events()       noexcept;
  Backend::Presenter&      Presentation() noexcept;
  Backend::AudioOutput&    Audio()        noexcept;
  Backend::Session&        Session()      noexcept;
  Backend::ClipboardStore& Clipboard()    noexcept;
  Backend::FrameStore&     Frames()       noexcept;

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
void SetError(sdlrdp_handle* handle, std::string text);
}
