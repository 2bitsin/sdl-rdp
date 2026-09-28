#include <sdl-rdp/video/graphics-link.hpp>

#include <sdl-rdp/auth/account.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/trace-queue.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/headless-client.test/backend/contract-run.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/picture/frame-store.hpp>
#include <sdl-rdp/utilities/socket.hpp>
#include <sdl-rdp/video/encoder.hpp>
#include <sdl-rdp/video/frame/pacing.hpp>
#include <sdl-rdp/video/frame/statistics.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <memory>
#include <utility>

namespace sdl_rdp::integration::video_test::detail::graphics_link {
using oxbox::platform::ScratchArea;
using sdl_rdp::auth::Account;
using sdl_rdp::configuration::Configuration;
using sdl_rdp::configuration::Setup;
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::diagnostics::TraceQueue;
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::headless_client_test::backend::ContractRun;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::link::Activation;
using sdl_rdp::link::DynamicChannel;
using sdl_rdp::link::EventQueue;
using sdl_rdp::link::PeerLink;
using sdl_rdp::picture::FrameStore;
using sdl_rdp::utilities::ConnectedSockets;
using sdl_rdp::utilities::SocketPair;
using sdl_rdp::video::Encoder;
using sdl_rdp::video::GraphicsLink;
using sdl_rdp::video::frame::FramePacing;
using sdl_rdp::video::frame::FrameStatistics;
using sdl_rdp::video::gfx::GfxChannel;
namespace {
constexpr int Continued = 3;
auto Config(ScratchArea const& certificates) -> Setup {
  return { .cert_dir = certificates.Path(), .width = 320, .height = 200 };
}
class PeerParts : public testing::Test {
protected:
  ScratchArea const   _certificates { "graphics-link", "sdl-rdp"               };
  Logs                _logs;
  Account             _account      { Config(_certificates)                    };
  Diagnostics const   _diagnostics  { _logs, false                             };
  EventQueue          _events;
  Configuration const _configuration{ Config(_certificates), _account          };
  FrameStore          _store        { { .width = 320, .height = 200 }, { }     };
  SocketPair          _sockets      { ConnectedSockets()                       };
  PeerLink            _link         { Connection{ std::move(_sockets.server) } };
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
