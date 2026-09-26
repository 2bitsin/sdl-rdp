#pragma once
#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/utilities/observer-set.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/client/rdpgfx.h>
#include <freerdp/event.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <utility>
#include <vector>

namespace sdl_rdp::headless_client_test::graphics::detail::observer {
using sdl_rdp::headless_client_test::client::ChannelSubscription;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::utilities::Membership;
using sdl_rdp::utilities::Pinned;

struct GraphicsCapture {
  struct Reset {
    std::uint32_t            width    = 0;
    std::uint32_t            height   = 0;
    std::vector<MONITOR_DEF> monitors;
    std::size_t              desktops = 0;
    std::size_t              frames   = 0;
  };
  std::vector<RDPGFX_FRAME_ACKNOWLEDGE_PDU>            frames;
  bool                                                 automatic           = true;
  bool                                                 advertise           = true;
  bool                                                 decode              = true;
  std::vector<RDPGFX_CREATE_SURFACE_PDU>               surfaces;
  std::vector<std::uint32_t>                           avc_nals;
  std::vector<RECTANGLE_16>                            avc_rects;
  std::vector<RDPGFX_H264_QUANT_QUALITY>               avc_quality;
  std::vector<Reset>                                   resets;
  std::vector<std::pair<std::uint32_t, std::uint32_t>> desktops;
  std::size_t                                          deleted             = 0;
  std::size_t                                          progressive_headers = 0;
  std::size_t                                          commands            = 0;
};
class GraphicsObserver : private Pinned {
public:
  using Reset = GraphicsCapture::Reset;
  explicit GraphicsObserver(Client& target);
           ~GraphicsObserver();
  auto     Ack(std::uint32_t depth = 0)                     -> bool;
  auto     AckFrame(std::size_t index, std::uint32_t depth) -> bool;
  auto     Channel() const                                  -> RdpgfxClientContext&;
  auto     Observed()                                       -> GraphicsCapture&;
  auto     Observed() const                                 -> GraphicsCapture const&;

private:
  auto Attach(RdpgfxClientContext& context)              -> void;
  auto Resize(rdpContext& context)                       -> bool;
  auto ObserveAvc(RDPGFX_SURFACE_COMMAND const& command) -> void;
  auto ObserveFrameLifecycle()                           -> void;
  auto ObserveResets()                                   -> void;
  auto ObserveFrames()                                   -> void;
  auto ObserveEndFrames(RdpgfxClientContext& connected)  -> void;

  std::optional<std::reference_wrapper<RdpgfxClientContext>>              channel;
  GraphicsCapture                                                         observed;
  Client&                                                                 client;
  pcRdpgfxFrameAcknowledge                                                original       = nullptr;
  pcRdpgfxEndFrame                                                        end            = nullptr;
  pcRdpgfxSurfaceCommand                                                  surface        = nullptr;
  pcRdpgfxCreateSurface                                                   create         = nullptr;
  pcRdpgfxDeleteSurface                                                   remove         = nullptr;
  pcRdpgfxResetGraphics                                                   reset          = nullptr;
  pDesktopResize                                                          desktop_resize = nullptr;
  Membership<GraphicsObserver>                                            membership;
  ChannelSubscription<RDPGFX_DVC_CHANNEL_NAME, &GraphicsObserver::Attach> connections;
};
}

namespace sdl_rdp::headless_client_test::graphics {
using detail::observer::GraphicsObserver;
}
