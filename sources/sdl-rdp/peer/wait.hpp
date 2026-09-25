#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/peer/channel-set.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <winpr/wtypes.h>
#include <cstdint>
#include <span>

namespace Backend {
inline constexpr std::uint32_t LoopHandleCount     = 2;
inline constexpr std::uint32_t AppendedHandleCount = ChannelHandleLimit + LoopHandleCount;
class Activation;
class FramePacing;
class GraphicsLink;
class PeerLink;
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
