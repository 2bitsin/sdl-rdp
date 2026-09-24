#pragma once
#include "extent.hpp"
#include "pinned.hpp"
#include "pointer-shape.hpp"
#include "refresh.hpp"
#include "sdl-rdp-backend.h"

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
  void Present(std::span<BYTE const> pixels, unsigned pitch, Extent size, std::span<sdlrdp_rect const> damage);
  void Resize(Extent size);
  void SetAspect(sdlrdp_aspect value);
  void EnsurePicture();
  void SetRefresh(RefreshMode mode, unsigned ceiling);
  void SetCodec(sdlrdp_codec codec);
  void SetPointer(PointerShape shape);
  int  WaitFrame(int timeout);

private:
  std::shared_ptr<std::vector<BYTE>> Acquire(Extent size);
  void Publish(std::shared_ptr<std::vector<BYTE> const> next, Extent size, std::span<sdlrdp_rect const> damage);
  std::mutex                                      _producer;
  std::vector<std::shared_ptr<std::vector<BYTE>>> _pool;
  Diagnostics const&                              _diagnostics;
  FrameStore&                                     _frames;
  Session&                                        _session;
  PointerStore&                                   _pointer;
  Configuration&                                  _configuration;
};
}
