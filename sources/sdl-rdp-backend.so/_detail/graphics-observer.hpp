#pragma once
#include "client.hpp"

#include <cstddef>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/event.h>
#include <utility>
#include <vector>

namespace Headless {
struct GraphicsCapture {
  struct Reset {
    UINT32                   width    = 0;
    UINT32                   height   = 0;
    std::vector<MONITOR_DEF> monitors;
    std::size_t              desktops = 0;
    std::size_t              frames   = 0;
  };
  std::vector<RDPGFX_FRAME_ACKNOWLEDGE_PDU> frames;
  bool                                      automatic           = true;
  bool                                      advertise           = true;
  std::vector<RDPGFX_CREATE_SURFACE_PDU>    surfaces;
  std::vector<unsigned>                     avc_nals;
  std::vector<RECTANGLE_16>                 avc_rects;
  std::vector<RDPGFX_H264_QUANT_QUALITY>    avc_quality;
  std::vector<Reset>                        resets;
  std::vector<std::pair<UINT32, UINT32>>    desktops;
  unsigned                                  deleted             = 0;
  unsigned                                  progressive_headers = 0;
  unsigned                                  commands            = 0;
};
class GraphicsObserver {
public:
  using Reset = GraphicsCapture::Reset;
           GraphicsObserver(GraphicsObserver const&)                      = delete;
           GraphicsObserver(GraphicsObserver&&)                           = delete;
  explicit GraphicsObserver(Client& target);
           ~GraphicsObserver();
  auto     operator = (GraphicsObserver const&)      -> GraphicsObserver& = delete;
  auto     operator = (GraphicsObserver&&)           -> GraphicsObserver& = delete;
  auto     Ack(UINT32 depth = 0)                     -> bool;
  auto     AckFrame(std::size_t index, UINT32 depth) -> bool;
  auto     Channel() const                           -> RdpgfxClientContext*;
  auto     Observed()                                -> GraphicsCapture&;
  auto     Observed() const                          -> GraphicsCapture const&;

private:
  static auto Connected(void* /*unused*/, ChannelConnectedEventArgs const* event) -> void;
  auto        ObserveAvc(RDPGFX_SURFACE_COMMAND const& command)                   -> void;
  auto        ObserveFrameLifecycle()                                             -> void;
  auto        ObserveFrames()                                                     -> void;

  RdpgfxClientContext*                         channel        = nullptr;
  GraphicsCapture                              observed;
  inline static thread_local GraphicsObserver* active         = nullptr;
  Client&                                      client;
  pcRdpgfxFrameAcknowledge                     original       = nullptr;
  pcRdpgfxEndFrame                             end            = nullptr;
  pcRdpgfxSurfaceCommand                       surface        = nullptr;
  pcRdpgfxCreateSurface                        create         = nullptr;
  pcRdpgfxDeleteSurface                        remove         = nullptr;
  pcRdpgfxResetGraphics                        reset          = nullptr;
  pDesktopResize                               desktop_resize = nullptr;
};
}
