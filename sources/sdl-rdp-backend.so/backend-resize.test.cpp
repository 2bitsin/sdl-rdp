#include "_detail/test-backend.hpp"

#include <cstddef>

namespace BackendGate {
struct ResizeProbe {
public:
           ResizeProbe(ResizeProbe const&) = delete;
           ResizeProbe(ResizeProbe&&)      = delete;
  explicit ResizeProbe(sdlrdp_handle& handle)
      : _handle{ handle }, _client{ CurrentClient(handle) }, _original{ _client.context->update->DesktopResize } {
    auto const held = _handle.Session().Lock();
    Expects(active == nullptr, "one resize probe exists");
    active = this;

    _client.context->update->DesktopResize = [](rdpContext* context) -> BOOL {
      EXPECT_TRUE(freerdp_is_active_state(context));
      ++active->_calls;
      return active->_original(context);
    };
  }
  ~ResizeProbe() {
    auto const held = _handle.Session().Lock();
    _client.context->update->DesktopResize = _original;
    active                                 = nullptr;
  }
  ResizeProbe& operator = (ResizeProbe const&) = delete;
  ResizeProbe& operator = (ResizeProbe&&)      = delete;
  bool         Finalizing() {
    auto const held    = _handle.Session().Lock();
    auto const current = freerdp_get_state(_client.context);
    return current >= CONNECTION_STATE_FINALIZATION_SYNC && current <= CONNECTION_STATE_FINALIZATION_FONT_LIST;
  }
  void MatchingLayout() {
    auto const held   = _handle.Session().Lock();
    auto const status = RequiredStatus(_handle);
    Expects(status.resizing, "peer has an in-flight resize");
    Expects(status.display != nullptr, "peer has a display channel");
    DISPLAY_CONTROL_MONITOR_LAYOUT monitor{ };
    monitor.Flags  = DISPLAY_CONTROL_MONITOR_PRIMARY;
    monitor.Width  = status.desktop.w;
    monitor.Height = status.desktop.h;
    DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const layout{ sizeof(monitor), 1, &monitor };
    EXPECT_EQ(status.display->DispMonitorLayout(status.display, &layout), CHANNEL_RC_OK);
  }
  void ConfirmActiveCallback() {
    auto const held = _handle.Session().Lock();
    ASSERT_FALSE(freerdp_is_active_state(_client.context));
    ASSERT_TRUE(_client.Activate(&_client));
    EXPECT_TRUE(RequiredStatus(_handle).resizing);
  }
  unsigned Calls() {
    auto const held = _handle.Session().Lock();
    return _calls;
  }

private:
  static freerdp_peer& CurrentClient(sdlrdp_handle& handle) {
    return *RequiredStatus(handle).client;
  }
  inline static ResizeProbe* active    = nullptr;
  sdlrdp_handle&             _handle;
  freerdp_peer&              _client;
  pDesktopResize             _original;
  unsigned                   _calls    = 0;
};
class ResizeStorm : public RoundFive {
protected:
  void ThenQuietResize(ResizeProbe& probe) {
    EXPECT_FALSE(logs.Contains("Unexpected client message")) << logs.Text(true);
    EXPECT_FALSE(std::ranges::any_of(Events(), [](auto event) { return event.type == SDLRDP_SCREEN; }));
    RecordProperty("DesktopResize_calls", probe.Calls());
    RecordProperty("SDLRDP_SCREEN_events", 0);
  }
  void ThenResizeCounts(Headless::DisplayClient& display, ResizeProbe& probe, unsigned expected) const {
    EXPECT_EQ(intervening, 3u);
    EXPECT_EQ(probe.Calls(), expected);
    EXPECT_EQ(display.Observed().desktops, expected);
    EXPECT_EQ(display.Observed().echoes, display.Observed().echo_resize ? expected : 0u);
  }
  static void ThenFinalDesktop(Client const& client, unsigned last_width, unsigned last_height) {
    EXPECT_EQ(client.Instance()->context->gdi->width, int(last_width));
    EXPECT_EQ(client.Instance()->context->gdi->height, int(last_height));
    EXPECT_FALSE(freerdp_shall_disconnect_context(client.Instance()->context));
  }
  void WhenResizeBurst(ResizeProbe& probe, unsigned last_width, unsigned last_height) {
    for (auto [w, h] : { std::pair{ 1600u, 900u }, { 1920u, 1080u }, { last_width, last_height } }) {
      ASSERT_EQ(sdlrdp_resize(backend.get(), w, h), 0);
      ++intervening;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(probe.Calls(), 1u);
      EXPECT_TRUE(probe.Finalizing());
    }
  }
  void DuringFinalization(ResizeProbe& probe, unsigned last_width, unsigned last_height) {
    auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!probe.Finalizing() && Clock::now() < deadline)
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(probe.Finalizing());
    probe.ConfirmActiveCallback();
    WhenResizeBurst(probe, last_width, last_height);
    if (::testing::Test::HasFatalFailure()) return;
    probe.MatchingLayout();
    EXPECT_LT(Clock::now() - started, std::chrono::milliseconds(200));
  }
  void ThenFinalLayout(Client& client, Headless::DisplayClient& display, ResizeProbe& probe, unsigned last_width,
                       unsigned last_height, unsigned expected) {
    std::vector<UINT32> pixels(static_cast<std::size_t>(last_width) * last_height, 0);
    ASSERT_TRUE(client.Until([&] { return display.Observed().desktops && client.Matches(pixels); }))
        << "server calls=" << probe.Calls() << " client calls=" << display.Observed().desktops
        << " GDI=" << client.Instance()->context->gdi->width << "x" << client.Instance()->context->gdi->height << "\n"
        << logs.Text(true);
    for (unsigned i = 0; i < 20; ++i)
      ASSERT_TRUE(client.Pump(5));
    ThenFinalDesktop(client, last_width, last_height);
    if (::testing::Test::HasFatalFailure()) return;
    ThenResizeCounts(display, probe, expected);
    if (::testing::Test::HasFatalFailure()) return;
    ThenQuietResize(probe);
  }
  void Run(unsigned last_width, unsigned last_height, unsigned expected) {
    Open(640, 480, { }, SDLRDP_CODEC_PLANAR);
    Client                  client(sdlrdp_port(backend.get()), true, 640, 480);
    Headless::DisplayClient display(client);
    display.Observed().echo_resize        = expected == 1;
    display.Observed().finalization_delay = std::chrono::milliseconds(20);
    Connect(client, false);
    ASSERT_TRUE(client.Until([&] { return Headless::DisplayClient::Ready(); }));
    Events();
    ResizeProbe probe(*backend);
    display.Observed().finalizing = [&] {
      if (display.Observed().desktops == 1) DuringFinalization(probe, last_width, last_height);
    };
    started                       = Clock::now();
    ASSERT_EQ(sdlrdp_resize(backend.get(), 1280, 800), 0);
    ThenFinalLayout(client, display, probe, last_width, last_height, expected);
  }
  Clock::time_point started;
  unsigned          intervening = 0;
};
namespace {
void ThenOriginalPicture(Client& client) {
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
  EXPECT_EQ(client.Instance()->context->gdi->height, 480);
}
}
TEST_F(ResizeStorm, CoalescesThreeSizesDuringFinalization) {
  Run(1024, 768, 2);
}
TEST_F(ResizeStorm, AlternatingAppSizesWithLayoutEcho) {
  Run(1280, 800, 1);
}
TEST_F(ResizeStorm, EqualLayoutDoesNotChangePicture) {
  Open();
  Client                        client(sdlrdp_port(backend.get()), true, 640, 480);
  Headless::DisplayClient const display(client);
  Connect(client, false);
  ASSERT_TRUE(client.Until([&] { return Headless::DisplayClient::Ready(); }));
  Events();
  auto presented = Presented(*backend);
  ASSERT_TRUE(display.Layout(640, 480));
  ASSERT_TRUE(display.Layout(800, 600));
  auto events  = EventsUntil(
      [](auto const& events) {
        return std::ranges::any_of(events, [](auto event) { return event.type == SDLRDP_SCREEN; });
      },
      false, &client);
  auto screens = events | std::views::filter([](auto event) { return event.type == SDLRDP_SCREEN; });
  ASSERT_EQ(std::ranges::distance(screens), 1);
  EXPECT_EQ(screens.front().screen.width, 800u);
  EXPECT_EQ(screens.front().screen.height, 600u);
  EXPECT_EQ(Presented(*backend), presented);
  RecordProperty("equal_layout_screen_events", 0);
  RecordProperty("equal_layout_picture_resizes", 0);
}
TEST_F(RoundFive, ResizeDesktop) {
  Open();
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  client.Instance()->context->update->DesktopResize = [](rdpContext* context) -> BOOL {
    return gdi_resize(context->gdi, freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                      freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
  };
  Connect(client, false);
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
  ASSERT_EQ(sdlrdp_resize(backend.get(), 800, 600), 0);
  std::vector<UINT32> pixels(800uz * 600, 0x123456);
  Present(pixels, 800, 600);
  ASSERT_TRUE(client.Until([&] { return client.Instance()->context->gdi->width == 800 && client.Matches(pixels); }))
      << logs.Text();
  EXPECT_EQ(client.Instance()->context->gdi->height, 600);
}
TEST_F(RoundFive, PictureSizeReactivatesDesktop) {
  for (bool const graphics : { false, true }) {
    Open();
    Client client(sdlrdp_port(backend.get()), true, 640, 480);
    if (graphics) client.EnableGraphics();
    Headless::GraphicsObserver observer(client);
    Connect(client, false);
    std::vector<UINT32> pixels(640uz * 480, 0x123456);
    Present(pixels, 640, 480);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
    for (auto [w, h] : { std::pair{ 320u, 200u }, std::pair{ 640u, 480u } }) {
      ResizePicture(client, observer, pixels, w, h, graphics);
      if (::testing::Test::HasFatalFailure()) return;
    }
  }
}

TEST_F(RoundFive, ClientScreenNeverResizesPicture) {
  Open();
  Client              client(sdlrdp_port(backend.get()), true, 1024, 768);
  DisplayClient const display(client);
  Connect(client, false);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[1].screen.width, 1024u);
  EXPECT_EQ(events[1].screen.height, 768u);
  ASSERT_TRUE(client.Until([&] { return Headless::DisplayClient::Ready(); })) << logs.Text();
  auto monitor = Headless::DisplayClient::Monitor(1920, 1080, 500);
  ASSERT_EQ(Headless::DisplayClient::Channel()->SendMonitorLayout(Headless::DisplayClient::Channel(), 1, &monitor),
            CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait(backend.get(), 0) == 1; })) << logs.Text();
  events = Events();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_SCREEN);
  EXPECT_EQ(events[0].screen.width, 1920u);
  EXPECT_EQ(events[0].screen.height, 1080u);
  ThenOriginalPicture(client);
}
}
