#pragma once
#include "factory.hpp"
#include "gfx.hpp"
#include "pinned.hpp"

#include <chrono>
#include <memory>
#include <span>
#include <winpr/wtypes.h>

namespace Backend {
inline constexpr auto     GraphicsConnectionWait = std::chrono::seconds(3);
inline constexpr unsigned GraphicsHandleLimit    = 1;
class Activation;
class Diagnostics;
class Encoder;
class FramePacing;
class PeerLink;
class GraphicsLink : private Pinned {
public:
       GraphicsLink(PeerLink& link, Diagnostics const& diagnostics, Activation& activation, FramePacing const& pacing,
                    Encoder const& encoder, Factory<std::unique_ptr<GfxChannel>> make) noexcept;
  auto Pump(std::span<HANDLE const> ready)  -> bool;
  auto ExpireConfirmation()                 -> void;
  auto Confirmed() const                    -> bool;
  auto Capacity() const                     -> unsigned;
  auto Channel() const                      -> GfxChannel&;
  auto Handles(std::span<HANDLE> out) const -> std::span<HANDLE>;
  auto Rejected(UINT32 channel_id)          -> void;
  auto Timing() const noexcept              -> GraphicsTiming const*;

private:
  auto Abandon(char const* reason) -> void;
  PeerLink&                            _link;
  Diagnostics const&                   _diagnostics;
  Activation&                          _activation;
  FramePacing const&                   _pacing;
  Encoder const&                       _encoder;
  Factory<std::unique_ptr<GfxChannel>> _make;
  std::unique_ptr<GfxChannel>          _channel;
  bool                                 _attempted  { };
};
}
