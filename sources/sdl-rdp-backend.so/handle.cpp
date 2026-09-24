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
auto sdlrdp_handle::Port() const noexcept -> unsigned {
  return _listener.Port();
}
auto sdlrdp_handle::Diagnostics() noexcept -> Backend::Diagnostics& {
  return _diagnostics;
}
auto sdlrdp_handle::Events() noexcept -> Backend::EventQueue& {
  return _events;
}
auto sdlrdp_handle::Presentation() noexcept -> Backend::Presenter& {
  return _presenter;
}
auto sdlrdp_handle::Audio() noexcept -> Backend::AudioOutput& {
  return _audio;
}
auto sdlrdp_handle::Session() noexcept -> Backend::Session& {
  return _session;
}
auto sdlrdp_handle::Clipboard() noexcept -> Backend::ClipboardStore& {
  return _clipboard;
}
auto sdlrdp_handle::Frames() noexcept -> Backend::FrameStore& {
  return _frames;
}
namespace Backend {
auto SetError(sdlrdp_handle* handle, std::string text) -> void {
  if (handle)
    handle->Diagnostics().Fail(std::move(text));
  else
    ErrorStore::Publish(nullptr, std::move(text));
}
}
