#include "_detail/handle.hpp"

#include "_detail/peer.hpp"

#include <memory>
#include <utility>

sdlrdp_handle::sdlrdp_handle(sdlrdp_config const& config, bool tracing)
    : _diagnostics { config, tracing }, _configuration{ config },
      _frames{ { .width = config.width, .height = config.height }, config.aspect },
      _session{ _frames, _events }, _presenter{ _diagnostics, _frames, _session, _pointer, _configuration },
      _audio{ _session, _presenter, _configuration },
      _listener{ _configuration, _diagnostics, _session, [this](Backend::PeerHandle accepted) {
                  return std::make_unique<Backend::Peer>(std::move(accepted), _diagnostics, _events, _configuration,
                                                         _frames, _pointer, _clipboard, _session);
                } } { }
unsigned sdlrdp_handle::Port() const noexcept {
  return _listener.Port();
}
Backend::Diagnostics& sdlrdp_handle::Diagnostics() noexcept {
  return _diagnostics;
}
Backend::EventQueue& sdlrdp_handle::Events() noexcept {
  return _events;
}
Backend::Presenter& sdlrdp_handle::Presentation() noexcept {
  return _presenter;
}
Backend::AudioOutput& sdlrdp_handle::Audio() noexcept {
  return _audio;
}
Backend::Session& sdlrdp_handle::Session() noexcept {
  return _session;
}
Backend::ClipboardStore& sdlrdp_handle::Clipboard() noexcept {
  return _clipboard;
}
Backend::FrameStore& sdlrdp_handle::Frames() noexcept {
  return _frames;
}
namespace Backend {
void SetError(sdlrdp_handle* handle, std::string text) {
  if (handle)
    handle->Diagnostics().Fail(std::move(text));
  else
    ErrorStore::Publish(nullptr, std::move(text));
}
}
