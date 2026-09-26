#pragma once
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/link/dynamic-channel.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/utilities/operation-name.hpp>
#include <sdl-rdp/video/forward.hpp>
#include <sdl-rdp/video/frame/forward.hpp>
#include <sdl-rdp/video/gfx/channel.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <span>
#include <string_view>

namespace sdl_rdp::video::detail::graphics_link {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::FailureLog;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::Activation;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::OperationName;
using sdl_rdp::video::frame::FramePacing;
using sdl_rdp::video::gfx::GfxChannel;
using sdl_rdp::video::gfx::GraphicsTiming;

using MakeGfx = std::move_only_function<auto(DynamicChannel&)->std::unique_ptr<GfxChannel>>;

inline constexpr auto        GraphicsConnectionWait = std::chrono::seconds(3);
inline constexpr std::size_t GraphicsHandleLimit    = 1;
class GraphicsLink final : public DynamicChannel {
public:
       GraphicsLink(PeerLink& link, Diagnostics const& diagnostics, Activation& activation, FramePacing const& pacing,
                    Encoder const& encoder, MakeGfx make) noexcept;
  auto Pump(Signalled const& ready)                     -> bool;
  auto ExpireConfirmation()                             -> void;
  auto Confirmed() const                                -> bool;
  auto Capacity() const                                 -> std::size_t;
  auto Channel() const                                  -> GfxChannel&;
  auto Handles(std::span<WaitHandle> out) const         -> std::span<WaitHandle>;
  auto Activate()                                       -> bool override;
  auto Reject()                                         -> void override;
  auto Timing() const noexcept                          -> std::optional<std::reference_wrapper<GraphicsTiming const>>;
  auto Failures(OperationName operation) const noexcept -> FailureLog;

private:
  auto Abandon(std::string_view reason) -> void;
  PeerLink&                   _link;
  Diagnostics const&          _diagnostics;
  Activation&                 _activation;
  FramePacing const&          _pacing;
  Encoder const&              _encoder;
  MakeGfx                     _make;
  std::unique_ptr<GfxChannel> _channel;
  bool                        _attempted  { };
};
}

namespace sdl_rdp::video {
using detail::graphics_link::GraphicsConnectionWait;
using detail::graphics_link::GraphicsHandleLimit;
using detail::graphics_link::GraphicsLink;
using detail::graphics_link::MakeGfx;
}
