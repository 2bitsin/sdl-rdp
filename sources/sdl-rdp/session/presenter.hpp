#pragma once
#include <sdl-rdp/core/refresh.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/pointer-shape.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <memory>
#include <mutex>
#include <span>
#include <vector>

namespace Backend {
class Configuration;
class Diagnostics;
class FrameStore;
class PointerStore;
class Session;
class Presenter : private Pinned {
public:
       Presenter(Diagnostics const& diagnostics, FrameStore& frames, Session& session, PointerStore& pointer,
                 Configuration& configuration);
  auto Present(std::span<BYTE const> pixels, unsigned pitch, Extent size, std::span<sdlrdp_rect const> damage) -> void;
  auto Resize(Extent size)                                                                                     -> void;
  auto SetAspect(sdlrdp_aspect value)                                                                          -> void;
  auto EnsurePicture()                                                                                         -> void;
  auto SetRefresh(RefreshMode mode, unsigned ceiling)                                                          -> void;
  auto SetCodec(sdlrdp_codec codec)                                                                            -> void;
  auto SetPointer(PointerShape shape)                                                                          -> void;
  auto WaitFrame(Deadline deadline)                                                                            -> int;

private:
  auto Acquire(Extent size) -> std::shared_ptr<std::vector<BYTE>>;
  auto Publish(std::shared_ptr<std::vector<BYTE> const> next, Extent size, std::span<sdlrdp_rect const> damage) -> void;
  std::mutex                                      _producer;
  std::vector<std::shared_ptr<std::vector<BYTE>>> _pool;
  Diagnostics const&                              _diagnostics;
  FrameStore&                                     _frames;
  Session&                                        _session;
  PointerStore&                                   _pointer;
  Configuration&                                  _configuration;
};
}
