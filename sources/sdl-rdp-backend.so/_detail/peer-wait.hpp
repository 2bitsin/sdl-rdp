#pragma once
#include "channel-set.hpp"
#include "pinned.hpp"

#include <span>
#include <winpr/wtypes.h>

namespace Backend {
inline constexpr DWORD LoopHandleCount     = 2;
inline constexpr DWORD AppendedHandleCount = ChannelHandleLimit + LoopHandleCount;
class Activation;
class FramePacing;
class GraphicsLink;
class PeerLink;
struct WaitPlan {
  DWORD count  { };
  DWORD timeout{ };
};
class PeerWait : private Pinned {
public:
       PeerWait(PeerLink& link, ChannelSet const& channels, Activation const& activation, FramePacing& pacing,
                GraphicsLink& graphics) noexcept;
  auto Plan(std::span<HANDLE> handles) -> WaitPlan;

private:
  auto Collect(std::span<HANDLE> handles) -> DWORD;
  auto Timeout() const                    -> DWORD;
  PeerLink&         _link;
  ChannelSet const& _channels;
  Activation const& _activation;
  FramePacing&      _pacing;
  GraphicsLink&     _graphics;
};
}
