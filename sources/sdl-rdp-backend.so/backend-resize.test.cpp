#include "_detail/test-backend.hpp"

namespace BackendGate {
struct ResizeProbe {
  inline static ResizeProbe* active = nullptr;
  Backend::State& state;
  Backend::Peer& peer;
  pDesktopResize original;
  unsigned calls = 0;
  explicit ResizeProbe(Backend::State& state) : state(state), peer(*state.current) {
    std::scoped_lock lock(state.session_guard);
    Expects(!active, "one resize probe exists");
    active = this;
    original = peer.client->context->update->DesktopResize;
    peer.client->context->update->DesktopResize = [](rdpContext* context) -> BOOL {
      EXPECT_TRUE(freerdp_is_active_state(context));
      ++active->calls;
      return active->original(context);
    };
  }
  ~ResizeProbe() {
    std::scoped_lock lock(state.session_guard);
    peer.client->context->update->DesktopResize = original;
    active = nullptr;
  }
  bool Finalizing() {
    std::scoped_lock lock(state.session_guard);
    auto current = freerdp_get_state(peer.client->context);
    return current >= CONNECTION_STATE_FINALIZATION_SYNC && current <= CONNECTION_STATE_FINALIZATION_FONT_LIST;
  }
  void MatchingLayout() {
    std::scoped_lock lock(state.session_guard);
    Expects(peer.resizing && peer.disp, "display layout targets the in-flight resize");
    DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
    monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
    monitor.Width = peer.desktop.w; monitor.Height = peer.desktop.h;
    DISPLAY_CONTROL_MONITOR_LAYOUT_PDU layout{sizeof(monitor), 1, &monitor};
    EXPECT_EQ(peer.disp->DispMonitorLayout(peer.disp.get(), &layout), CHANNEL_RC_OK);
  }
  void ConfirmActiveCallback() {
    std::scoped_lock lock(state.session_guard);
    ASSERT_FALSE(freerdp_is_active_state(peer.client->context));
    ASSERT_TRUE(peer.client->Activate(peer.client.get()));
    EXPECT_TRUE(peer.resizing);
  }
  unsigned Calls() {
    std::scoped_lock lock(state.session_guard);
    return calls;
  }
};
class ResizeStorm : public RoundFive {
protected:
  Clock::time_point started;
  unsigned intervening = 0;
  void DuringFinalization(ResizeProbe& probe, unsigned last_width, unsigned last_height) {
    auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!probe.Finalizing() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    ASSERT_TRUE(probe.Finalizing());
    probe.ConfirmActiveCallback();
    for (auto [w, h] : {std::pair{1600u, 900u}, {1920u, 1080u}, {last_width, last_height}}) {
      ASSERT_EQ(sdlrdp_resize(backend.get(), w, h), 0);
      ++intervening;
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      EXPECT_EQ(probe.Calls(), 1u);
      EXPECT_TRUE(probe.Finalizing());
    }
    probe.MatchingLayout();
    EXPECT_LT(Clock::now() - started, std::chrono::milliseconds(200));
  }
  void Run(unsigned last_width, unsigned last_height, unsigned expected) {
    Open(640, 480, {}, SDLRDP_CODEC_PLANAR);
    Client client(sdlrdp_port(backend.get()), true, 640, 480);
    Headless::DisplayClient display(client);
    display.echo_resize = expected == 1;
    display.finalization_delay = std::chrono::milliseconds(20);
    Connect(client, false);
    ASSERT_TRUE(client.Until([&] { return display.ready.load(); }));
    Events();
    ResizeProbe probe(*backend->state);
    display.finalizing = [&] { if (display.desktops == 1) DuringFinalization(probe, last_width, last_height); };
    started = Clock::now();
    ASSERT_EQ(sdlrdp_resize(backend.get(), 1280, 800), 0);
    std::vector<UINT32> pixels(last_width * last_height, 0);
    ASSERT_TRUE(client.Until([&] { return display.desktops && client.Matches(pixels); }))
      << "server calls=" << probe.Calls() << " client calls=" << display.desktops
      << " GDI=" << client.instance->context->gdi->width << "x" << client.instance->context->gdi->height << "\n" << logs.Text(true);
    for (unsigned i = 0; i < 20; ++i) ASSERT_TRUE(client.Pump(5));
    EXPECT_EQ(client.instance->context->gdi->width, int(last_width));
    EXPECT_EQ(client.instance->context->gdi->height, int(last_height));
    EXPECT_FALSE(freerdp_shall_disconnect_context(client.instance->context));
    EXPECT_EQ(intervening, 3u);
    EXPECT_EQ(probe.Calls(), expected);
    EXPECT_EQ(display.desktops, expected);
    EXPECT_EQ(display.echoes, display.echo_resize ? expected : 0u);
    EXPECT_FALSE(logs.Contains("Unexpected client message")) << logs.Text(true);
    EXPECT_FALSE(std::ranges::any_of(Events(), [](auto event) { return event.type == SDLRDP_SCREEN; }));
    RecordProperty("DesktopResize_calls", probe.Calls());
    RecordProperty("SDLRDP_SCREEN_events", 0);
  }
};
TEST_F(ResizeStorm, CoalescesThreeSizesDuringFinalization) {
  Run(1024, 768, 2);
}
TEST_F(ResizeStorm, AlternatingAppSizesWithLayoutEcho) {
  Run(1280, 800, 1);
}
TEST_F(ResizeStorm, EqualLayoutDoesNotChangePicture) {
  Open();
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  Headless::DisplayClient display(client);
  Connect(client, false);
  ASSERT_TRUE(client.Until([&] { return display.ready.load(); }));
  Events();
  uint64_t presented;
  { std::scoped_lock lock(backend->state->frame_guard); presented = backend->state->presented; }
  ASSERT_TRUE(display.Layout(640, 480));
  ASSERT_TRUE(display.Layout(800, 600));
  auto events = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto event) { return event.type == SDLRDP_SCREEN; });
  }, false, &client);
  auto screens = events | std::views::filter([](auto event) { return event.type == SDLRDP_SCREEN; });
  ASSERT_EQ(std::ranges::distance(screens), 1);
  EXPECT_EQ(screens.front().screen.width, 800u);
  EXPECT_EQ(screens.front().screen.height, 600u);
  { std::scoped_lock lock(backend->state->frame_guard); EXPECT_EQ(backend->state->presented, presented); }
  RecordProperty("equal_layout_screen_events", 0);
  RecordProperty("equal_layout_picture_resizes", 0);
}
TEST_F(RoundFive, ResizeDesktop) {
  Open();
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  client.instance->context->update->DesktopResize = [](rdpContext* context) -> BOOL {
    return gdi_resize(context->gdi, freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
      freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight));
  };
  Connect(client, false);
  EXPECT_EQ(client.instance->context->gdi->width, 640);
  ASSERT_EQ(sdlrdp_resize(backend.get(), 800, 600), 0);
  std::vector<UINT32> pixels(800 * 600, 0x123456);
  Present(pixels, 800, 600);
  ASSERT_TRUE(client.Until([&] { return client.instance->context->gdi->width == 800 && client.Matches(pixels); })) << logs.Text();
  EXPECT_EQ(client.instance->context->gdi->height, 600);
}
TEST_F(RoundFive, PictureSizeReactivatesDesktop) {
  for (bool graphics : {false, true}) {
    Open();
    Client client(sdlrdp_port(backend.get()), true, 640, 480);
    if (graphics) client.EnableGraphics();
    Headless::GraphicsObserver observer(client);
    Connect(client, false);
    std::vector<UINT32> pixels(640 * 480, 0x123456);
    Present(pixels, 640, 480);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
    for (auto [w, h] : {std::pair{320u, 200u}, std::pair{640u, 480u}}) {
      auto desktops = observer.desktops.size(), resets = observer.resets.size(), frames = observer.frames.size();
      pixels.assign(w * h, 0x654321);
      Present(pixels, w, h);
      ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
      ASSERT_GT(observer.desktops.size(), desktops);
      EXPECT_EQ(observer.desktops.back(), (std::pair{w, h}));
      EXPECT_EQ(client.instance->context->gdi->width, int(w));
      EXPECT_EQ(client.instance->context->gdi->height, int(h));
      if (!graphics) continue;
      ASSERT_EQ(observer.resets.size(), resets + 1);
      auto const& reset = observer.resets.back();
      EXPECT_EQ(reset.width, w); EXPECT_EQ(reset.height, h);
      EXPECT_EQ(reset.desktops, desktops + 1);
      EXPECT_EQ(reset.frames, frames);
      ASSERT_EQ(reset.monitors.size(), 1u);
      EXPECT_EQ(reset.monitors[0].left, 0); EXPECT_EQ(reset.monitors[0].top, 0);
      EXPECT_EQ(reset.monitors[0].right, int(w) - 1); EXPECT_EQ(reset.monitors[0].bottom, int(h) - 1);
      EXPECT_EQ(reset.monitors[0].flags, 1u);
      EXPECT_EQ(observer.frames.size(), frames + 1);
    }
  }
}

TEST_F(RoundFive, ClientScreenNeverResizesPicture) {
  Open();
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  DisplayClient display(client);
  Connect(client, false);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[1].screen.width, 1024u);
  EXPECT_EQ(events[1].screen.height, 768u);
  ASSERT_TRUE(client.Until([&] { return display.ready.load(); })) << logs.Text();
  DISPLAY_CONTROL_MONITOR_LAYOUT monitor{};
  monitor.Flags = DISPLAY_CONTROL_MONITOR_PRIMARY;
  monitor.Width = 1920; monitor.Height = 1080;
  monitor.PhysicalWidth = 500; monitor.PhysicalHeight = 300;
  monitor.DesktopScaleFactor = monitor.DeviceScaleFactor = 100;
  ASSERT_EQ(display.channel.load()->SendMonitorLayout(display.channel.load(), 1, &monitor), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait(backend.get(), 0) == 1; })) << logs.Text();
  events = Events();
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_SCREEN);
  EXPECT_EQ(events[0].screen.width, 1920u);
  EXPECT_EQ(events[0].screen.height, 1080u);
  EXPECT_EQ(client.instance->context->gdi->width, 640);
  EXPECT_EQ(client.instance->context->gdi->height, 480);
}
}
