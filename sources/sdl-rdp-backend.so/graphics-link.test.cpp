#include "_detail/graphics-link.hpp"
#include "_detail/activation.hpp"
#include "_detail/configuration.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/encoder.hpp"
#include "_detail/event-queue.hpp"
#include "_detail/frame-pacing.hpp"
#include "_detail/frame-statistics.hpp"
#include "_detail/frame-store.hpp"
#include "_detail/peer-link.hpp"
#include "_detail/socket-pair.hpp"
#include "_detail/test-certificate-directory.hpp"
#include "_detail/test-logs.hpp"
#include "_detail/trace-queue.hpp"
#include "support.test/contract-run.hpp"

#include <freerdp/channels/channels.h>
#include <gtest/gtest.h>
#include <winpr/wtsapi.h>
#include <memory>

namespace {
constexpr int Continued = 3;
auto Config(BackendGate::CertificateDirectory const& certificates, Headless::Logs& logs) -> sdlrdp_config {
  return { .cert_dir = certificates.Path().c_str(),
           .width    = 320,
           .height   = 200,
           .log      = Headless::Logs::Collect,
           .log_user = &logs };
}
auto AcceptedPeer(Backend::SocketPair& sockets) -> Backend::PeerHandle {
  WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
  return Backend::PeerHandle{ freerdp_peer_new(sockets.TakeServer().Release()) };
}
class PeerParts : public testing::Test {
protected:
  BackendGate::CertificateDirectory const _certificates;
  Headless::Logs                          _logs;
  sdlrdp_config const                     _config       { Config(_certificates, _logs)         };
  Backend::Diagnostics const              _diagnostics  { _config, false                       };
  Backend::EventQueue                     _events;
  Backend::Configuration const            _configuration{ _config                              };
  Backend::FrameStore                     _store        { { .width = 320, .height = 200 }, { } };
  Backend::SocketPair                     _sockets;
  Backend::PeerLink                       _link         { AcceptedPeer(_sockets)               };
};
class GraphicsLinkParts : public PeerParts {
protected:
  auto RejectWithoutChannel() -> int {
    _graphics.Reject();
    return _logs.Contains("GFX channel rejected") ? Continued : 0;
  }

private:
  Backend::TraceQueue      _traces    { _diagnostics   };
  Backend::Activation      _activation{ _events, _link };
  Backend::FrameStatistics _statistics;
  Backend::FramePacing     _pacing    {
    _diagnostics, _events, _configuration, _store, _link, _activation, _traces, _statistics
  };
  Backend::Encoder         _encoder;
  Backend::GraphicsLink    _graphics  {
    _link, _diagnostics, _activation,
    _pacing, _encoder, [](Backend::DynamicChannel&) { return std::unique_ptr<Backend::GfxChannel>{ }; }
  };
};
}
TEST_F(GraphicsLinkParts, RejectionWithoutAChannelFailsTheContractAndAbandonsNothing) {
  Headless::ContractRun{ [this] { return RejectWithoutChannel(); } }.ExpectBroken("a rejected graphics channel is open",
                                                                                  Continued);
}
