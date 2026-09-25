#pragma once
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/picture/frame-layout.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/extent.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/pointer/layout.hpp>
#include <sdl-rdp/video/pointer/shape.hpp>

#include <cstdint>
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
  auto Present(std::span<std::uint8_t const> pixels, sdl_rdp::picture::FrameLayout const& layout,
               std::span<sdlrdp_rect const> damage) -> void;
  auto Resize(Extent requested)                                                                             -> void;
  auto SetAspect(sdlrdp_aspect value)                                                                       -> void;
  auto EnsurePicture()                                                                                      -> void;
  auto SetRefresh(std::uint32_t mode, std::uint32_t ceiling)                                                -> void;
  auto SetCodec(sdlrdp_codec codec)                                                                         -> void;
  auto SetPointer(sdl_rdp::video::pointer::PointerLayout const& layout, std::span<std::uint8_t const> argb) -> void;
  auto WaitFrame(Deadline deadline)                                                                         -> int;

private:
  auto Acquire(Extent size) -> std::shared_ptr<std::vector<std::uint8_t>>;
  auto Publish(std::shared_ptr<std::vector<std::uint8_t> const> next, Extent size, std::span<sdlrdp_rect const> damage)
      -> void;
  std::mutex                                              _producer;
  std::vector<std::shared_ptr<std::vector<std::uint8_t>>> _pool;
  Diagnostics const&                                      _diagnostics;
  FrameStore&                                             _frames;
  Session&                                                _session;
  PointerStore&                                           _pointer;
  Configuration&                                          _configuration;
};
}
