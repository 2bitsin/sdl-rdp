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
  bool                  Pump(std::span<HANDLE const> ready);
  void                  ExpireConfirmation();
  bool                  Confirmed() const;
  unsigned              Capacity() const;
  GfxChannel&           Channel() const;
  std::span<HANDLE>     Handles(std::span<HANDLE> out) const;
  void                  Rejected(UINT32 channel_id);
  GraphicsTiming const* Timing() const noexcept;

private:
  void Abandon(char const* reason);
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
