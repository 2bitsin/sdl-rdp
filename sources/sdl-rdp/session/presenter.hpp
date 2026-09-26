#pragma once
#include <sdl-rdp/configuration/forward.hpp>
#include <sdl-rdp/configuration/refresh.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/picture/forward.hpp>
#include <sdl-rdp/picture/frame-layout.hpp>
#include <sdl-rdp/session/forward.hpp>
#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/generational.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/pointer/forward.hpp>
#include <sdl-rdp/video/pointer/layout.hpp>
#include <sdl-rdp/video/pointer/shape.hpp>

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <vector>

namespace sdl_rdp::session::detail::presenter {
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::RefreshMode;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::picture::FrameLayout;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Deadline;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Generational;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::utilities::Rect;
using sdl_rdp::video::pointer::PointerLayout;
using sdl_rdp::video::pointer::PointerShape;

class Presenter : private Pinned {
public:
  Presenter(Diagnostics const& diagnostics, FrameStore& frames, Session& session, Generational<PointerShape>& pointer,
            Configuration& configuration);
  auto Present(std::span<std::uint8_t const> pixels, FrameLayout const& layout, std::span<Rect const> damage) -> void;
  auto Resize(Extent requested)                                                                               -> void;
  auto SetAspect(std::optional<AspectRatio> value)                                                            -> void;
  auto EnsurePicture()                                                                                        -> void;
  auto SetRefresh(RefreshMode mode, std::uint32_t ceiling)                                                    -> void;
  auto SetCodec(Codec codec)                                                                                  -> void;
  auto SetPointer(PointerLayout const& layout, std::span<std::uint8_t const> argb)                            -> void;
  auto WaitFrame(Deadline deadline)                                                                           -> bool;

private:
  auto Acquire(Extent size) -> std::shared_ptr<std::vector<std::uint8_t>>;
  auto Publish(std::shared_ptr<std::vector<std::uint8_t> const> next, Extent size, std::span<Rect const> damage)
      -> void;
  std::mutex                                              _producer;
  std::vector<std::shared_ptr<std::vector<std::uint8_t>>> _pool;
  Diagnostics const&                                      _diagnostics;
  FrameStore&                                             _frames;
  Session&                                                _session;
  Generational<PointerShape>&                             _pointer;
  Configuration&                                          _configuration;
};
}

namespace sdl_rdp::session {
using detail::presenter::Presenter;
}
