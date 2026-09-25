#include <sdl-rdp/video/graphics-link.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/trace-queue.hpp>
#include <sdl-rdp/headless-client.test/backend/certificate-directory.hpp>
#include <sdl-rdp/headless-client.test/backend/contract-run.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/socket-pair.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/frame/statistics.hpp>

#include <freerdp/channels/channels.h>
#include <gtest/gtest.h>
#include <winpr/wtsapi.h>
#include <memory>

namespace sdl_rdp::integration::video_test::detail::graphics_link {
using sdl_rdp::configuration::Configuration;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::TraceQueue;
using sdl_rdp::freerdp_facade::PeerHandle;
using sdl_rdp::headless_client_test::backend::CertificateDirectory;
using sdl_rdp::headless_client_test::backend::ContractRun;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::link::Activation;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::SocketPair;
using sdl_rdp::video::Encoder;
using sdl_rdp::video::GraphicsLink;
using sdl_rdp::video::frame::FramePacing;
using sdl_rdp::video::frame::FrameStatistics;
using sdl_rdp::video::gfx::GfxChannel;
namespace {
constexpr int Continued = 3;
auto Config(CertificateDirectory const& certificates, Logs& logs) -> sdlrdp_config {
  return {
    .cert_dir = certificates.Path().c_str(), .width = 320, .height = 200, .log = Logs::Collect, .log_user = &logs
  };
}
auto AcceptedPeer(SocketPair& sockets) -> PeerHandle {
  WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
  return PeerHandle{ freerdp_peer_new(sockets.TakeServer().Release()) };
}
class PeerParts : public testing::Test {
protected:
  CertificateDirectory const _certificates;
  Logs                       _logs;
  Diagnostics const          _diagnostics  { Config(_certificates, _logs), false  };
  EventQueue                 _events;
  Configuration const        _configuration{ Config(_certificates, _logs)         };
  FrameStore                 _store        { { .width = 320, .height = 200 }, { } };
  SocketPair                 _sockets;
  PeerLink                   _link         { AcceptedPeer(_sockets)               };
};
class GraphicsLinkParts : public PeerParts {
protected:
  auto RejectWithoutChannel() -> int {
    _graphics.Reject();
    return _logs.Contains("GFX channel rejected") ? Continued : 0;
  }

private:
  TraceQueue      _traces    { _diagnostics   };
  Activation      _activation{ _events, _link };
  FrameStatistics _statistics;
  FramePacing _pacing{ _diagnostics, _events, _configuration, _store, _link, _activation, _traces, _statistics };
  Encoder         _encoder;
  GraphicsLink    _graphics  { _link, _diagnostics, _activation,
                               _pacing, _encoder, [](DynamicChannel&) { return std::unique_ptr<GfxChannel>{ }; } };
};
}
TEST_F(GraphicsLinkParts, RejectionWithoutAChannelFailsTheContractAndAbandonsNothing) {
  ContractRun{ [this] { return RejectWithoutChannel(); } }.ExpectBroken("a rejected graphics channel is open",
                                                                        Continued);
}
}
