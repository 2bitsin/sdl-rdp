#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/graphics/observer.hpp>
#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>
#include <sdl-rdp/headless-client.test/utilities/published.hpp>
#include <sdl-rdp/utilities/extent.hpp>

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <vector>

namespace sdl_rdp::integration::video_test::detail::resize {
using sdl_rdp::headless_client_test::backend::Presented;
using sdl_rdp::headless_client_test::backend::RequiredStatus;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::DisplayClient;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::graphics::RoundFive;
using sdl_rdp::headless_client_test::utilities::Published;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Required;

class ResizeProbe {
public:
           ResizeProbe(ResizeProbe const&) = delete;
           ResizeProbe(ResizeProbe&&)      = delete;
  explicit ResizeProbe(sdlrdp_handle& handle)
      : _handle{ handle }, _client{ CurrentClient(handle) }, _original{ _client.context->update->DesktopResize },
        _capabilities{ _client.ClientCapabilities } {
    auto const held     = _handle.Session().Lock();
    auto const existing = active.Peek();
    Expects(!existing.has_value(), "one resize probe exists");
    active.Publish(*this);
    // abi: pDesktopResize and psPeerClientCapabilities, BOOL is int
    _client.context->update->DesktopResize = [](rdpContext* context) -> int {
      Expects(context != nullptr, "resize names its peer context");
      EXPECT_TRUE(freerdp_is_active_state(context));
      auto& probe = active.Get();
      ++probe._calls;
      return probe._original(context);
    };
    // FreeRDP enters finalization right after ClientCapabilities, before the peer releases the session lock.
    _client.ClientCapabilities = [](freerdp_peer* client) -> int {
      Expects(client != nullptr, "capabilities name their peer");
      auto&      probe    = active.Get();
      auto const accepted = probe._capabilities == nullptr || probe._capabilities(client);
      probe._changed.notify_all();
      return accepted;
    };
  }
  ~ResizeProbe() {
    auto const held = _handle.Session().Lock();
    _client.context->update->DesktopResize = _original;
    _client.ClientCapabilities             = _capabilities;
    active.Withdraw();
  }
  auto operator=(ResizeProbe const&) -> ResizeProbe& = delete;
  auto operator=(ResizeProbe&&)      -> ResizeProbe& = delete;
  auto AwaitFinalizing()             -> bool {
    auto held = _handle.Session().Lock();
    return _changed.wait_for(held, std::chrono::seconds(10), [&] { return InFinalization(); });
  }
  auto Finalizing() -> bool {
    auto const held = _handle.Session().Lock();
    return InFinalization();
  }
  auto MatchingLayout() -> void {
    auto const held   = _handle.Session().Lock();
    auto const status = RequiredStatus(_handle);
    Expects(status.resizing, "peer has an in-flight resize");
    DISPLAY_CONTROL_MONITOR_LAYOUT monitor{ };
    monitor.Flags  = DISPLAY_CONTROL_MONITOR_PRIMARY;
    monitor.Width  = status.desktop.w;
    monitor.Height = status.desktop.h;
    DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const layout  { sizeof(monitor), 1, &monitor };
    auto&                                    display = Required(status.display, "peer has a display channel").get();
    EXPECT_EQ(display.DispMonitorLayout(&display, &layout), CHANNEL_RC_OK);
  }
  auto ConfirmActiveCallback() -> void {
    auto const held = _handle.Session().Lock();
    ASSERT_FALSE(freerdp_is_active_state(_client.context));
    ASSERT_TRUE(_client.Activate(&_client));
    EXPECT_TRUE(RequiredStatus(_handle).resizing);
  }
  auto Calls() -> std::size_t {
    auto const held = _handle.Session().Lock();
    return _calls;
  }

private:
  static auto CurrentClient(sdlrdp_handle& handle) -> freerdp_peer& {
    return RequiredStatus(handle).client;
  }
  auto InFinalization() const -> bool {
    auto const current = freerdp_get_state(_client.context);
    return current >= CONNECTION_STATE_FINALIZATION_SYNC && current <= CONNECTION_STATE_FINALIZATION_FONT_LIST;
  }
  // abi: pDesktopResize and ClientCapabilities carry only the peer, whose ContextExtra the backend owns.
  inline static Published<ResizeProbe> active;
  sdlrdp_handle&                       _handle;
  freerdp_peer&                        _client;
  pDesktopResize                       _original;
  psPeerClientCapabilities             _capabilities;
  std::condition_variable_any          _changed;
  std::size_t                          _calls        = 0;
};
class ResizeStorm : public RoundFive {
protected:
  auto ConnectDisplay(Client& client) -> void {
    ASSERT_NO_FATAL_FAILURE(Connect(client, false));
    ASSERT_TRUE(client.Until([&] { return DisplayClient::Of(client, &DisplayClient::Ready); }));
    std::ignore = backend.Poll();
  }
  auto ThenQuietResize(ResizeProbe& probe) -> void {
    EXPECT_FALSE(logs.Contains("Unexpected client message")) << logs.Text(true);
    EXPECT_FALSE(std::ranges::any_of(backend.Poll(), [](auto event) { return event.type == SDLRDP_SCREEN; }));
    RecordProperty("DesktopResize_calls", probe.Calls());
    RecordProperty("SDLRDP_SCREEN_events", 0);
  }
  static auto ThenResizeCounts(DisplayClient& display, ResizeProbe& probe, std::size_t expected) -> void {
    EXPECT_EQ(probe.Calls(), expected);
    EXPECT_EQ(display.Observed().desktops, expected);
    EXPECT_EQ(display.Observed().echoes, display.Observed().echo_resize ? expected : 0u);
  }
  static auto ThenFinalDesktop(Client& client, Extent last) -> void {
    EXPECT_EQ(client.Instance()->context->gdi->width, static_cast<std::int32_t>(last.width));
    EXPECT_EQ(client.Instance()->context->gdi->height, static_cast<std::int32_t>(last.height));
    EXPECT_FALSE(freerdp_shall_disconnect_context(client.Instance()->context));
  }
  auto WhenResizeBurst(ResizeProbe& probe, Extent last) -> void {
    std::array const burst{ Extent{ .width = 1600, .height = 900 }, Extent{ .width = 1920, .height = 1080 }, last };
    for (auto size : burst) {
      ASSERT_EQ(sdlrdp_resize(&*backend, size.width, size.height), 0);
      EXPECT_EQ(probe.Calls(), 1u);
      EXPECT_TRUE(probe.Finalizing());
    }
  }
  auto DuringFinalization(ResizeProbe& probe, Extent last) -> void {
    ASSERT_TRUE(probe.AwaitFinalizing());
    ASSERT_NO_FATAL_FAILURE(probe.ConfirmActiveCallback());
    ASSERT_NO_FATAL_FAILURE(WhenResizeBurst(probe, last));
    probe.MatchingLayout();
    EXPECT_TRUE(probe.Finalizing());
  }
  auto ThenFinalLayout(Client& client, DisplayClient& display, ResizeProbe& probe, Extent last, std::size_t expected)
      -> void {
    Pixels pixels(static_cast<std::size_t>(last.width) * last.height, 0);
    ASSERT_TRUE(client.Until([&] { return display.Observed().desktops && client.Matches(pixels); }))
        << "server calls=" << probe.Calls() << " client calls=" << display.Observed().desktops
        << " GDI=" << client.Instance()->context->gdi->width << "x" << client.Instance()->context->gdi->height << "\n"
        << logs.Text(true);
    for (std::size_t i = 0; i < 20; ++i) ASSERT_TRUE(client.Pump(5));
    ASSERT_NO_FATAL_FAILURE(ThenFinalDesktop(client, last));
    ASSERT_NO_FATAL_FAILURE(ThenResizeCounts(display, probe, expected));
    ThenQuietResize(probe);
  }
  auto Run(Extent last, std::size_t expected) -> void {
    ASSERT_NO_FATAL_FAILURE(Open(640, 480, { }, SDLRDP_CODEC_PLANAR));
    Client        client(sdlrdp_port(&*backend), true, 640, 480);
    DisplayClient display(client);
    display.Observed().echo_resize = expected == 1;
    ASSERT_NO_FATAL_FAILURE(ConnectDisplay(client));
    ResizeProbe probe(*backend);
    display.Observed().finalizing = [&] {
      if (display.Observed().desktops == 1) DuringFinalization(probe, last);
    };
    ASSERT_EQ(sdlrdp_resize(&*backend, 1280, 800), 0);
    ThenFinalLayout(client, display, probe, last, expected);
  }
};
namespace {
auto ThenSingleScreen(std::vector<sdlrdp_event> const& events, std::uint32_t width, std::uint32_t height) -> void {
  auto screens = events | std::views::filter([](auto event) { return event.type == SDLRDP_SCREEN; });
  ASSERT_EQ(std::ranges::distance(screens), 1);
  EXPECT_EQ(screens.front().screen.width, width);
  EXPECT_EQ(screens.front().screen.height, height);
}
auto ThenOriginalPicture(Client& client) -> void {
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
  EXPECT_EQ(client.Instance()->context->gdi->height, 480);
}
}
TEST_F(ResizeStorm, CoalescesThreeSizesDuringFinalization) {
  Run({ .width = 1024, .height = 768 }, 2);
}
TEST_F(ResizeStorm, AlternatingAppSizesWithLayoutEcho) {
  Run({ .width = 1280, .height = 800 }, 1);
}
TEST_F(ResizeStorm, EqualLayoutDoesNotChangePicture) {
  ASSERT_NO_FATAL_FAILURE(Open());
  Client              client(sdlrdp_port(&*backend), true, 640, 480);
  DisplayClient const display(client);
  ASSERT_NO_FATAL_FAILURE(ConnectDisplay(client));
  auto presented = Presented(*backend);
  ASSERT_TRUE(display.Layout(640, 480));
  ASSERT_TRUE(display.Layout(800, 600));
  auto events = UntilEvent(client, SDLRDP_SCREEN, false);
  ASSERT_NO_FATAL_FAILURE(ThenSingleScreen(events, 800, 600));
  EXPECT_EQ(Presented(*backend), presented);
  RecordProperty("equal_layout_screen_events", 0);
  RecordProperty("equal_layout_picture_resizes", 0);
}
TEST_F(RoundFive, ResizeDesktop) {
  ASSERT_NO_FATAL_FAILURE(Open());
  Client client(sdlrdp_port(&*backend), true, 1024, 768);
  // abi: pDesktopResize, BOOL is int
  client.Instance()->context->update->DesktopResize = [](rdpContext* context) -> int {
    Expects(context != nullptr, "resize names its client context");
    return gdi_resize(context->gdi, freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                      freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
  };
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
  ASSERT_EQ(sdlrdp_resize(&*backend, 800, 600), 0);
  Pixels pixels(800uz * 600, 0x123456);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 800, 600));
  ASSERT_TRUE(client.Until([&] { return client.Instance()->context->gdi->width == 800 && client.Matches(pixels); }))
      << logs.Text();
  EXPECT_EQ(client.Instance()->context->gdi->height, 600);
}
TEST_F(RoundFive, PictureSizeReactivatesDesktop) {
  for (bool const graphics : { false, true }) {
    ASSERT_NO_FATAL_FAILURE(RunPictureSizes(graphics));
  }
}

TEST_F(RoundFive, ClientScreenNeverResizesPicture) {
  ASSERT_NO_FATAL_FAILURE(Open());
  Client              client(sdlrdp_port(&*backend), true, 1024, 768);
  DisplayClient const display(client);
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[1].screen.width, 1024u);
  EXPECT_EQ(events[1].screen.height, 768u);
  ASSERT_TRUE(client.Until([&] { return DisplayClient::Of(client, &DisplayClient::Ready); })) << logs.Text();
  ASSERT_TRUE(DisplayClient::Of(client, [](auto const& display) { return display.Layout(1920, 1080, 500); }));
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait(&*backend, 0) == 1; })) << logs.Text();
  events = backend.Poll();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_SCREEN);
  EXPECT_EQ(events[0].screen.width, 1920u);
  EXPECT_EQ(events[0].screen.height, 1080u);
  ThenOriginalPicture(client);
}
}
