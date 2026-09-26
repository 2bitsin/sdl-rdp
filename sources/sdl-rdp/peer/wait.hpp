#pragma once
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/peer/channel-set.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

#include <cstdint>
#include <span>

namespace sdl_rdp::peer::detail::wait {
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::Activation;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::GraphicsLink;
using sdl_rdp::video::frame::FramePacing;

inline constexpr std::uint32_t LoopHandleCount     = 2;
inline constexpr std::uint32_t AppendedHandleCount = ChannelHandleLimit + LoopHandleCount;
struct WaitPlan {
  std::uint32_t count  { };
  std::uint32_t timeout{ };
};
class PeerWait : private Pinned {
public:
       PeerWait(PeerLink& link, ChannelSet const& channels, Activation const& activation, FramePacing& pacing,
                GraphicsLink& graphics) noexcept;
  auto Plan(std::span<WaitHandle> handles) -> WaitPlan;

private:
  auto Collect(std::span<WaitHandle> handles) -> std::uint32_t;
  auto Timeout() const                        -> std::uint32_t;
  PeerLink&         _link;
  ChannelSet const& _channels;
  Activation const& _activation;
  FramePacing&      _pacing;
  GraphicsLink&     _graphics;
};
}

namespace sdl_rdp::peer {
using detail::wait::PeerWait;
}
