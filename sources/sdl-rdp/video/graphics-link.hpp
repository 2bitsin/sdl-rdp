#pragma once
#include <sdl-rdp/core/dynamic-channel.hpp>
#include <sdl-rdp/core/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/utilities/factory.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/video/gfx.hpp>

#include <winpr/wtypes.h>
#include <chrono>
#include <cstddef>
#include <memory>
#include <span>

namespace Backend {
inline constexpr auto        GraphicsConnectionWait = std::chrono::seconds(3);
inline constexpr std::size_t GraphicsHandleLimit    = 1;
class Activation;
class Diagnostics;
class Encoder;
class FramePacing;
class PeerLink;
class GraphicsLink final : public DynamicChannel {
public:
       GraphicsLink(PeerLink& link, Diagnostics const& diagnostics, Activation& activation, FramePacing const& pacing,
                    Encoder const& encoder, Factory<std::unique_ptr<GfxChannel>, DynamicChannel&> make) noexcept;
  auto Pump(std::span<WaitHandle const> ready)          -> bool;
  auto ExpireConfirmation()                             -> void;
  auto Confirmed() const                                -> bool;
  auto Capacity() const                                 -> std::size_t;
  auto Channel() const                                  -> GfxChannel&;
  auto Handles(std::span<WaitHandle> out) const         -> std::span<WaitHandle>;
  auto Activate()                                       -> bool override;
  auto Reject()                                         -> void override;
  auto Timing() const noexcept                          -> GraphicsTiming const*;
  auto Failures(OperationName operation) const noexcept -> FailureLog;

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
