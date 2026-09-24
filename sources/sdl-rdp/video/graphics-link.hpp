#pragma once
#include <sdl-rdp/core/dynamic-channel.hpp>
#include <sdl-rdp/utilities/factory.hpp>
#include <sdl-rdp/video/gfx.hpp>

#include <winpr/wtypes.h>
#include <chrono>
#include <memory>
#include <span>

namespace Backend {
inline constexpr auto     GraphicsConnectionWait = std::chrono::seconds(3);
inline constexpr unsigned GraphicsHandleLimit    = 1;
class Activation;
class Diagnostics;
class Encoder;
class FramePacing;
class PeerLink;
class GraphicsLink final : public DynamicChannel {
public:
       GraphicsLink(PeerLink& link, Diagnostics const& diagnostics, Activation& activation, FramePacing const& pacing,
                    Encoder const& encoder, Factory<std::unique_ptr<GfxChannel>, DynamicChannel&> make) noexcept;
  auto Pump(std::span<HANDLE const> ready)  -> bool;
  auto ExpireConfirmation()                 -> void;
  auto Confirmed() const                    -> bool;
  auto Capacity() const                     -> unsigned;
  auto Channel() const                      -> GfxChannel&;
  auto Handles(std::span<HANDLE> out) const -> std::span<HANDLE>;
  auto Activate()                           -> bool override;
  auto Reject()                             -> void override;
  auto Timing() const noexcept              -> GraphicsTiming const*;

private:
  auto Abandon(char const* reason) -> void;
  PeerLink&                                             _link;
  Diagnostics const&                                    _diagnostics;
  Activation&                                           _activation;
  FramePacing const&                                    _pacing;
  Encoder const&                                        _encoder;
  Factory<std::unique_ptr<GfxChannel>, DynamicChannel&> _make;
  std::unique_ptr<GfxChannel>                           _channel;
  bool                                                  _attempted  { };
};
}
