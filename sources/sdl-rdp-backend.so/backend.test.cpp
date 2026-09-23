#include "_detail/headless-client.hpp"
#include "_detail/headless-tls.hpp"
#include "_detail/headless-gfx.hpp"
#include "_detail/state.hpp"
#include "sdl-rdp-backend.h"
#include "_detail/contract.hpp"
#include <gtest/gtest.h>
#include <freerdp/client/disp.h>
#include <freerdp/client/channels.h>
#include <freerdp/addin.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/event.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <winpr/synch.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>

#include <memory>
#include <span>
#include <vector>
#include <unistd.h>
#include <future>
#include <random>
#include <regex>
#include <thread>
#include "_detail/test-io.hpp"
#include "_detail/test-logs.hpp"
#include "_detail/test-pattern.hpp"
#include <charconv>
#include <format>
#include <mutex>
#include <winpr/wlog.h>
#include <sys/wait.h>
#include <cstring>
#include <cerrno>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include "_detail/rect.hpp"
#include "_detail/copy-rows.hpp"
#include "_detail/encoder.hpp"

namespace {
using Clock = std::chrono::steady_clock;
using utilities::Expects;
using Headless::Client;
struct CertificateDirectory {
  std::filesystem::path path;
  CertificateDirectory() {
    std::array<char, 40> pattern{};
    std::ranges::copy(std::string("/tmp/sdlrdp-gate-XXXXXX"), pattern.begin());
    auto result = mkdtemp(pattern.data());
    Expects(result != nullptr, "temporary directory created");
    path = result;
  }
  ~CertificateDirectory() { std::filesystem::remove_all(path); }
};
std::size_t ResidentBytes()
{
  auto statm = Headless::ReadText("/proc/self/statm");
  std::size_t total = 0, resident = 0;
  auto first = std::from_chars(statm.data(), statm.data() + statm.size(), total);
  Expects(first.ec == std::errc() && first.ptr != statm.data() + statm.size(), "total pages readable");
  auto second = std::from_chars(first.ptr + 1, statm.data() + statm.size(), resident);
  Expects(second.ec == std::errc(), "resident pages readable");
  return resident * std::size_t(sysconf(_SC_PAGESIZE));
}
bool Listening(unsigned port)
{
  auto tcp = Headless::ReadText("/proc/net/tcp");
  auto address = std::format("0100007F:{:04X}", port);
  return std::ranges::any_of(tcp | std::views::split('\n'), [&](auto row) {
    std::string_view line(row.begin(), row.end());
    return line.contains(address) && line.contains(" 0A ");
  });
}
using Headless::Logs;

TEST(CopyRows, PaddedRows) {
  std::array<BYTE, 8> source{1, 2, 9, 9, 3, 4, 9, 9};
  std::array<BYTE, 6> destination{8, 8, 8, 8, 8, 8};
  Backend::CopyRows(source, 4, destination, 3, 2, 2);
  EXPECT_EQ(destination, (std::array<BYTE, 6>{1, 2, 8, 3, 4, 8}));
  Backend::CopyRows(source, 4, destination, 3, 2, 2, true);
  EXPECT_EQ(destination, (std::array<BYTE, 6>{3, 4, 8, 1, 2, 8}));
  Backend::CopyRows({}, 0, {}, 0, 0, 0);
}
TEST(Errors, WidthAndBind) {
  CertificateDirectory certificates;
  sdlrdp_config config{"192.0.2.1", 0, certificates.path.c_str(), 0, 200, 0};
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), -1);
  EXPECT_EQ(handle, nullptr);
  EXPECT_TRUE(std::string_view(sdlrdp_last_error()).contains("width"));
  config.width = 320;
  ASSERT_EQ(sdlrdp_open(&config, &handle), -1);
  EXPECT_TRUE(std::string_view(sdlrdp_last_error()).contains(std::strerror(EADDRNOTAVAIL)));
  auto other = std::async(std::launch::async, [] { return std::string(sdlrdp_last_error()); });
  EXPECT_TRUE(other.get().empty());
}
struct Socket {
  int descriptor = socket(AF_INET, SOCK_STREAM, 0);
  ~Socket() { if (descriptor >= 0) close(descriptor); }
};
void InitializeTls(sdlrdp_config config)
{
  // Issue 1: prime FreeRDP's lazy BIO method before the in-process client races it.
  config.log = nullptr;
  sdlrdp_handle* raw = nullptr;
  Expects(sdlrdp_open(&config, &raw) == 0, "TLS initialization listener opens");
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend(raw, sdlrdp_close);
  Headless::InitializeTls(sdlrdp_port(raw));
}
struct Mode {
  bool surface;
  sdlrdp_codec codec;
  operator bool() const { return surface; }
};
bool HasCookie(Client const& client)
{
  Expects(client.instance && client.instance->context, "client context exists");
  auto cookie = static_cast<ARC_SC_PRIVATE_PACKET const*>(freerdp_settings_get_pointer(
    client.instance->context->settings, FreeRDP_ServerAutoReconnectCookie));
  return cookie && cookie->cbLen == 28;
}
class FrameCounter {
  inline static thread_local FrameCounter* active = nullptr;
  rdpUpdate* update;
  pSurfaceBits surface;
  pBitmapUpdate bitmap;
public:
  unsigned frames = 0, bitmap_pdus = 0;
  explicit FrameCounter(Client& client) : update(client.instance->context->update),
    surface(update->SurfaceBits), bitmap(update->BitmapUpdate) {
    Expects(!active && surface && bitmap, "one observer with GDI installed");
    active = this;
    update->SurfaceBits = ReceiveSurface;
    update->BitmapUpdate = ReceiveBitmap;
  }
  ~FrameCounter() {
    update->SurfaceBits = surface;
    update->BitmapUpdate = bitmap;
    active = nullptr;
  }
  static BOOL ReceiveSurface(rdpContext* context, SURFACE_BITS_COMMAND const* command) {
    Expects(active && command, "surface observer exists");
    auto result = active->surface(context, command);
    if (result && command->destBottom == context->gdi->height) ++active->frames;
    return result;
  }
  static BOOL ReceiveBitmap(rdpContext* context, BITMAP_UPDATE const* command) {
    Expects(active && command, "bitmap observer exists");
    auto result = active->bitmap(context, command);
    ++active->bitmap_pdus;
    if (result && std::ranges::any_of(std::span(command->rectangles, command->number),
        [=](auto const& rectangle) { return rectangle.destBottom + 1 == context->gdi->height; }))
      ++active->frames;
    return result;
  }
};
// Both fixtures share the same bounded event accumulation; predicates inspect the
// whole sequence, so an early poll cannot lose half of a transition.
struct BackendEvents {
  CertificateDirectory certificates;
  Logs logs;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend{nullptr, sdlrdp_close};
  bool Acknowledged() {
    Expects(backend != nullptr, "backend exists");
    std::scoped_lock lock(backend->state->frame_guard);
    return backend->state->current && backend->state->current->acknowledged >= backend->state->presented;
  }
  std::vector<sdlrdp_event> Events() {
    std::array<sdlrdp_event, 256> batch{};
    auto count = sdlrdp_poll(backend.get(), batch.data(), batch.size());
    return {batch.begin(), batch.begin() + count};
  }
  std::vector<sdlrdp_event> EventsUntil(auto predicate, bool include_refresh = true, Client* client = nullptr) {
    std::vector<sdlrdp_event> result;
    auto deadline = Clock::now() + std::chrono::seconds(10);
    do {
      for (auto event : Events())
        if (include_refresh || event.type != SDLRDP_REFRESH) result.push_back(event);
      if (predicate(result)) break;
      if (client) { if (!client->Pump()) break; }
      else sdlrdp_wait(backend.get(), 50);
    } while (Clock::now() < deadline);
    return result;
  }
  std::vector<sdlrdp_event> Events(unsigned wanted) {
    return EventsUntil([=](auto const& events) { return events.size() >= wanted; }, false);
  }
};
class Gate : public testing::TestWithParam<Mode>, protected BackendEvents {
protected:
  std::vector<UINT32> pixels = std::vector<UINT32>(320 * 200);
  void SetUp() override {
    sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 320, 200, 0, Logs::Collect, &logs};
    config.codec = GetParam().codec;
    static std::once_flag tls_initialized;
    std::call_once(tls_initialized, [&] { InitializeTls(config); });
    std::filesystem::remove_all(certificates.path);
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    backend.reset(handle);
    ASSERT_NE(sdlrdp_port(handle), 0u);
    std::array<UINT32, 8> bars{0x00ffffff, 0x00ffff00, 0x0000ffff, 0x0000ff00,
                             0x00ff00ff, 0x00ff0000, 0x000000ff, 0};
    std::generate(pixels.begin(), pixels.end(), [&, index = 0u]() mutable {
      auto x = index % 320, y = index++ / 320;
      return x < 40 && y < 30 ? 0x00010101u : bars[x / 40]; });
  }
  void Frame(Client& client, sdlrdp_rect area) {
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) && client.Matches(pixels); })) << logs.Text();
  }
  void Input(Client& client) {
    auto input = client.instance->context->input;
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_DOWN, 0x1E));
    ASSERT_TRUE(freerdp_input_send_keyboard_event(input, KBD_FLAGS_RELEASE, 0x1E));
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_MOVE, 10, 20));
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1 | PTR_FLAGS_DOWN, 10, 20));
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_BUTTON1, 10, 20));
    ASSERT_TRUE(freerdp_input_send_mouse_event(input, PTR_FLAGS_WHEEL | 120, 0, 0));
    auto events = Events(6);
    ASSERT_EQ(events.size(), 6u);
    for (unsigned i = 0; i < 2; ++i) {
      EXPECT_EQ(events[i].type, SDLRDP_KEY);
      EXPECT_EQ(events[i].key.scancode, 0x1Eu);
      EXPECT_EQ(events[i].key.extended, 0);
      EXPECT_EQ(events[i].key.down, !i);
    }
    EXPECT_EQ(events[2].type, SDLRDP_MOUSE_MOVE);
    EXPECT_EQ(events[2].mouse_move.x, 10);
    EXPECT_EQ(events[2].mouse_move.y, 20);
    for (unsigned i = 3; i < 5; ++i) {
      EXPECT_EQ(events[i].type, SDLRDP_MOUSE_BUTTON);
      EXPECT_EQ(events[i].mouse_button.button, 1u);
      EXPECT_EQ(events[i].mouse_button.down, i == 3);
    }
    EXPECT_EQ(events[5].type, SDLRDP_MOUSE_WHEEL);
    EXPECT_EQ(events[5].mouse_wheel.dx, 0);
    EXPECT_EQ(events[5].mouse_wheel.dy, 1);
  }
};
TEST_P(Gate, FramesAndInput) {
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 320u);
  EXPECT_EQ(events[0].connected.height, 200u);
  EXPECT_EQ(events[0].connected.bpp, 32u);
  EXPECT_EQ(events[0].connected.codec, GetParam().codec);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  FrameCounter counter(client);
  auto bytes = client.Received();
  ASSERT_NO_FATAL_FAILURE(Frame(client, {0, 0, 320, 200}));
  if (GetParam().codec == SDLRDP_CODEC_PLANAR)
    EXPECT_LE(counter.bitmap_pdus, 1 + (client.Received() - bytes) / 0xFFFF);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
  RecordProperty("wire_bytes", std::to_string(client.Received() - bytes));
  RecordProperty("codec", std::to_string(GetParam().codec));
  RecordProperty("surface", bool(GetParam()) ? "true" : "false");
  for (unsigned y = 51; y < 81; ++y) std::fill_n(pixels.begin() + y * 320 + 73, 40, 0x00020202);
  ASSERT_NO_FATAL_FAILURE(Frame(client, {73, 51, 40, 30}));
  ASSERT_NO_FATAL_FAILURE(Input(client));
  ASSERT_TRUE(freerdp_disconnect(client.instance.get()));
  events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  backend.reset();
  EXPECT_TRUE(logs.Contains("accepted"));
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "disconnected"));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed")) << logs.Text();
}
TEST_P(Gate, ResizeAndWakeup) {
  backend.reset();
  sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 640, 480, 0};
  config.codec = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  Client client(sdlrdp_port(handle), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 640u);
  EXPECT_EQ(events[1].type, SDLRDP_SCREEN);
  EXPECT_EQ(events[1].screen.width, 320u);
  EXPECT_EQ(events[1].screen.height, 200u);
  EXPECT_EQ(sdlrdp_wait(handle, 1), 0);
  auto waiter = std::async(std::launch::async, [=] { return sdlrdp_wait(handle, 30000); });
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (waiter.wait_for(std::chrono::milliseconds(1)) != std::future_status::ready
         && Clock::now() < deadline) sdlrdp_wakeup(handle);
  EXPECT_EQ(waiter.wait_for(std::chrono::milliseconds(0)), std::future_status::ready);
  EXPECT_EQ(waiter.get(), 0);
  backend.reset();
}
TEST_P(Gate, LateClientAndBurst) {
  sdlrdp_rect area{0, 0, 320, 200};
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  auto before = ResidentBytes();
  for (unsigned frame = 0; frame < 200; ++frame) {
    std::ranges::fill(pixels, 0x00010101u * (frame + 1));
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  }
  auto after = ResidentBytes();
  RecordProperty("burst_rss_growth", std::to_string(std::int64_t(after) - std::int64_t(before)));
  EXPECT_LE(after, before + pixels.size() * 16);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
}
TEST_P(Gate, DesktopIsPicture) {
  backend.reset();
  sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 640, 480, 0, Logs::Collect, &logs};
  config.codec = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  std::vector<UINT32> frame(640 * 480);
  std::generate(frame.begin(), frame.end(), [index = 0u]() mutable {
    return (index++ * 2654435761u) & 0x00ffffff; });
  sdlrdp_rect area{0, 0, 640, 480};
  ASSERT_EQ(sdlrdp_present(handle, frame.data(), 640 * 4, 640, 480, &area, 1), 0);
  Client client(sdlrdp_port(handle), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return client.Matches(frame); })) << logs.Text();
  EXPECT_EQ(client.instance->context->gdi->width, 640);
  EXPECT_EQ(client.instance->context->gdi->height, 480);
  EXPECT_FALSE(logs.Contains("failed"));
}
TEST_P(Gate, WaitForClient) {
  auto port = sdlrdp_port(backend.get());
  backend.reset();
  sdlrdp_config config{"127.0.0.1", port, certificates.path.c_str(), 320, 200, 1};
  config.codec = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  auto opening = std::async(std::launch::async, [&] { return sdlrdp_open(&config, &handle); });
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!Listening(port) && Clock::now() < deadline) std::this_thread::yield();
  EXPECT_EQ(opening.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  Client client(port, GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_EQ(opening.wait_for(std::chrono::seconds(10)), std::future_status::ready);
  ASSERT_EQ(opening.get(), 0);
  backend.reset(handle);
  EXPECT_EQ(sdlrdp_wait(handle, 0), 1);
  sdlrdp_event event{};
  ASSERT_EQ(sdlrdp_poll(handle, &event, 1), 1u);
  EXPECT_EQ(event.type, SDLRDP_CONNECTED);
}
TEST_P(Gate, BlockedSinglePresent) {
  backend.reset();
  sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 2048, 1536, 0};
  config.codec = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  Client client(sdlrdp_port(handle), GetParam(), 2048, 1536);
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_EQ(events.front().type, SDLRDP_CONNECTED);
  pixels.resize(2048 * 1536);
  std::generate(pixels.begin(), pixels.end(), [index = 0u]() mutable {
    return (index++ * 2654435761u) & 0x00ffffff; });
  sdlrdp_rect area{0, 0, 2048, 1536};
  // Keep the client unpumped until the presenter completes. Completion therefore
  // cannot depend on the client draining output; ten seconds is a progress bound.
  auto presenting = std::async(std::launch::async, [&] {
    return sdlrdp_present(handle, pixels.data(), 2048 * 4, 2048, 1536, &area, 1);
  });
  auto ready = presenting.wait_for(std::chrono::seconds(10));
  EXPECT_EQ(ready, std::future_status::ready);
  if (ready != std::future_status::ready) freerdp_disconnect(client.instance.get());
  ASSERT_EQ(presenting.get(), 0);
  ASSERT_EQ(ready, std::future_status::ready);
  EXPECT_TRUE(client.Until([&] { return client.Matches(pixels); }))
    << "maximum channel error " << client.MaxError(pixels);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
}
TEST_P(Gate, NewestClientTakesOver) {
  Client first(sdlrdp_port(backend.get()), GetParam());
  ASSERT_TRUE(freerdp_connect(first.instance.get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  Client second(sdlrdp_port(backend.get()), GetParam(), 400, 240);
  ASSERT_TRUE(freerdp_connect(second.instance.get())) << logs.Text(true);
  auto events = Events(3);
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  EXPECT_EQ(events[1].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[1].connected.width, 320u);
  EXPECT_EQ(events[1].connected.height, 200u);
  EXPECT_EQ(events[2].type, SDLRDP_SCREEN);
  freerdp_input_send_keyboard_event(first.instance->context->input, KBD_FLAGS_DOWN, 0x30);
  auto deadline = Clock::now() + std::chrono::seconds(10);
  bool connected = true;
  while (connected && Clock::now() < deadline) connected = first.Pump();
  ASSERT_FALSE(connected);
  EXPECT_EQ(freerdp_get_last_error(first.instance->context), FREERDP_ERROR_DISCONNECTED_BY_OTHER_CONNECTION);
  ASSERT_NO_FATAL_FAILURE(Input(second));
  ASSERT_TRUE(freerdp_disconnect(second.instance.get()));
  events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  EXPECT_EQ(sdlrdp_wait(backend.get(), 0), 0);
}
TEST_P(Gate, LiveCodecChange) {
  Client client(sdlrdp_port(backend.get()), GetParam());
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  auto previous = GetParam().codec;
  for (auto codec : {SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX,
                     SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_AUTO}) {
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), codec), 0);
    client.tolerance = (codec == SDLRDP_CODEC_REMOTEFX || codec == SDLRDP_CODEC_AUTO) && GetParam().surface ? 40 : 0;
    std::ranges::fill(pixels, 0x00404040u + unsigned(codec) * 0x00040404u);
    ASSERT_NO_FATAL_FAILURE(Frame(client, {0, 0, 320, 200}));
    auto expected = (!GetParam().surface
      && (codec == SDLRDP_CODEC_REMOTEFX || codec == SDLRDP_CODEC_NSCODEC)) ? SDLRDP_CODEC_PLANAR
      : codec == SDLRDP_CODEC_AUTO ? (GetParam().surface ? SDLRDP_CODEC_REMOTEFX : SDLRDP_CODEC_PLANAR) : codec;

    if (expected != previous) {
      auto events = EventsUntil([](auto const& events) {
        return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_CODEC_CHANGED; });
      }, false);
      ASSERT_EQ(events.size(), 1u);
      EXPECT_EQ(events[0].type, SDLRDP_CODEC_CHANGED);
      EXPECT_EQ(events[0].codec_changed.codec, expected);
    } else {
      std::array<sdlrdp_event, 4> events{};
      auto count = sdlrdp_poll(backend.get(), events.data(), events.size());
      EXPECT_TRUE(std::ranges::all_of(std::span(events).first(count),
        [](auto const& event) { return event.type == SDLRDP_REFRESH; }));
    }
    previous = expected;
  }
  EXPECT_EQ(sdlrdp_set_codec(backend.get(), sdlrdp_codec(99)), -1);
}
TEST_P(Gate, ExactFlatColour) {
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX || GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_EQ(Events(2).size(), 2u);
  std::ranges::fill(pixels, 0x00ffffffu);
  ASSERT_NO_FATAL_FAILURE(Frame(client, {0, 0, 320, 200}));
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
}
TEST_P(Gate, TinyDamage) {
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  ASSERT_NO_FATAL_FAILURE(Frame(client, {0, 0, 320, 200}));
  pixels[51 * 320 + 73] = 0x000000ff;
  ASSERT_NO_FATAL_FAILURE(Frame(client, {73, 51, 1, 1}));
  pixels.back() = 0x00ffffff;
  ASSERT_NO_FATAL_FAILURE(Frame(client, {319, 199, 1, 1}));
}
TEST_P(Gate, ProbeClosesBeforeActivation) {
  {
    Socket socket;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(sdlrdp_port(backend.get()));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    ASSERT_EQ(connect(socket.descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)), 0);
  }
  auto deadline = Clock::now() + std::chrono::seconds(10);
  while (!logs.Contains(SDLRDP_LOG_INFO, "Connection closed before activation")
         && Clock::now() < deadline) std::this_thread::yield();
  {
    std::scoped_lock lock(logs.guard);
    EXPECT_TRUE(std::ranges::any_of(logs.lines, [](auto const& line) {
      return line.first == SDLRDP_LOG_INFO
        && line.second == "Connection closed before activation: ERRCONNECT_CONNECT_TRANSPORT_FAILED.";
    }));
  }
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed")) << logs.Text();
  EXPECT_EQ(sdlrdp_wait(backend.get(), 0), 0);
}
TEST(Measurement, FullFrames1024x768) {
  CertificateDirectory certificates;
  Logs logs;
  std::vector<UINT32> pixels(1024 * 768);
  std::generate(pixels.begin(), pixels.end(), [index = 0u]() mutable {
    return (index++ * 2654435761u) & 0x00ffffff; });
  for (auto codec : {SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC}) {
    sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 1024, 768, 0, Logs::Collect, &logs};
    config.codec = codec;
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend(handle, sdlrdp_close);
    Client client(sdlrdp_port(handle), true, 1024, 768);
    client.tolerance = codec == SDLRDP_CODEC_REMOTEFX ? 40 : codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
    ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
    FrameCounter counter(client);
    auto bytes = client.Received();
    auto started = Clock::now();
    sdlrdp_rect area{0, 0, 1024, 768};
    ASSERT_EQ(sdlrdp_present(handle, pixels.data(), 4096, 1024, 768, &area, 1), 0);
    ASSERT_TRUE(client.Until([&] { return counter.frames == 1; }));
    auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    EXPECT_TRUE(client.Matches(pixels)) << client.MaxError(pixels);
    auto name = std::to_string(codec);
    RecordProperty("codec_" + name + "_bytes", std::to_string(client.Received() - bytes));
    RecordProperty("codec_" + name + "_ms", std::to_string(elapsed));
    RecordProperty("codec_" + name + "_frames", std::to_string(counter.frames));
  }
}
TEST(Planar, SignedDelta64Rows) {
  constexpr unsigned width = 64, height = 64;
  std::vector<UINT32> pixels(width * height), decoded(pixels.size());
  std::generate(pixels.begin(), pixels.end(), [index = 0u]() mutable {
    return 0xff000000u | (200u - index++ / width) * 0x00010101u; });
  std::unique_ptr<BITMAP_PLANAR_CONTEXT, Backend::Releases<freerdp_bitmap_planar_context_free>>
    encoder(freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE, width, height)),
    decoder(freerdp_bitmap_planar_context_new(0, width, height));
  ASSERT_TRUE(encoder && decoder);
  freerdp_planar_topdown_image(encoder.get(), TRUE);
  std::vector<BYTE> compressed(pixels.size() * 4 + 1024);
  UINT32 size = compressed.size();
  ASSERT_NE(freerdp_bitmap_compress_planar(encoder.get(), reinterpret_cast<BYTE*>(pixels.data()),
    PIXEL_FORMAT_BGRA32, width, height, width * 4, compressed.data(), &size), nullptr);
  ASSERT_NE(compressed.front() & PLANAR_FORMAT_HEADER_RLE, 0);
  ASSERT_TRUE(planar_decompress(decoder.get(), compressed.data(), size, width, height,
    reinterpret_cast<BYTE*>(decoded.data()), PIXEL_FORMAT_BGRA32, width * 4,
    0, 0, width, height, FALSE));
  EXPECT_EQ(decoded.front(), pixels.front());
  // FreeRDP 3.15 planar.c:1477 tests unsigned s2c >= 0, misencoding negative deltas.
  EXPECT_NE(decoded, pixels);
  EXPECT_NE(decoded[width], pixels[width]);
}
TEST(Logging, ListenerCallback) {
  CertificateDirectory certificates;
  Logs logs;
  ASSERT_EQ(setenv("WLOG_LEVEL", "INFO", 1), 0);
  sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 320, 200, 0, Logs::Collect, &logs};
  sdlrdp_handle* raw = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &raw), 0);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend(raw, sdlrdp_close);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "Listening on socket"));
  EXPECT_EQ(unsetenv("WLOG_LEVEL"), 0);
}
TEST(Logging, NewestHandleRoutesAndClears) {
  CertificateDirectory certificates;
  Logs first, second;
  sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 320, 200, 0, Logs::Collect, &first};
  sdlrdp_handle* raw = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &raw), 0);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> a(raw, sdlrdp_close);
  config.log_user = &second;
  ASSERT_EQ(sdlrdp_open(&config, &raw), 0);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> b(raw, sdlrdp_close);
  WLog_Print(WLog_GetRoot(), WLOG_WARN, "latest handle marker");
  EXPECT_FALSE(first.Contains("latest handle marker"));
  EXPECT_TRUE(second.Contains(SDLRDP_LOG_WARN, "latest handle marker"));
  a.reset();
  WLog_Print(WLog_GetRoot(), WLOG_ERROR, "older close marker");
  EXPECT_TRUE(second.Contains(SDLRDP_LOG_ERROR, "older close marker"));
  b.reset();
  WLog_Print(WLog_GetRoot(), WLOG_WARN, "closed handle marker");
  EXPECT_FALSE(second.Contains("closed handle marker"));
}
TEST(Logging, NoFreerdpStdout) {
  std::array<int, 2> pipefd{};
  ASSERT_EQ(pipe(pipefd.data()), 0);
  auto child = fork();
  ASSERT_GE(child, 0);
  if (!child) {
    close(pipefd[0]);
    if (dup2(pipefd[1], STDOUT_FILENO) < 0) _exit(125);
    close(pipefd[1]);
    setenv("WLOG_LEVEL", "INFO", 1);
    execl("/proc/self/exe", "sdl-rdp-backend-tests", "--gtest_filter=Logging.ListenerCallback",
          "--gtest_repeat=1", "--gtest_output=", nullptr);
    _exit(126);
  }
  close(pipefd[1]);
  Headless::Descriptor input{pipefd[0]};
  auto output = Headless::ReadText(input.value);
  int status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << output;
  EXPECT_FALSE(output.contains("com.freerdp")) << output;
}
std::string ModeName(testing::TestParamInfo<Mode> const& info)
{
  Expects(info.param.codec >= SDLRDP_CODEC_AUTO && info.param.codec <= SDLRDP_CODEC_AVC420, "known codec");
  constexpr std::array names{"Auto", "Planar", "RemoteFX", "NSCodec", "Raw", "Progressive", "Avc420"};
  return std::string(names[info.param.codec]) + (info.param.surface ? "Surface" : "Bitmap");
}
INSTANTIATE_TEST_SUITE_P(Codec, Gate, testing::Values(
  Mode{true, SDLRDP_CODEC_RAW}, Mode{true, SDLRDP_CODEC_PLANAR},
  Mode{true, SDLRDP_CODEC_REMOTEFX}, Mode{true, SDLRDP_CODEC_NSCODEC},
  Mode{false, SDLRDP_CODEC_RAW}, Mode{false, SDLRDP_CODEC_PLANAR}), ModeName);
using Headless::FrameObserver;
class RoundFive : public testing::Test, protected BackendEvents {
protected:
  void Open(unsigned w = 640, unsigned h = 480, sdlrdp_aspect aspect = {}, sdlrdp_codec codec = SDLRDP_CODEC_RAW, unsigned audio_latency = 0) {
    sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), w, h, 0, Logs::Collect, &logs};
    config.aspect = aspect; config.codec = codec; config.audio_latency_ms = audio_latency;
    InitializeTls(config);
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0) << sdlrdp_last_error();
    backend.reset(handle);
  }
  void Present(std::vector<UINT32> const& pixels, unsigned w, unsigned h) {
    sdlrdp_rect full{0, 0, int(w), int(h)};
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), w * 4, w, h, &full, 1), 0);
  }
  void Connect(Client& client, bool ack = true) {
    ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_FrameAcknowledge, ack ? 2 : 0));
    ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  }
};
TEST_F(RoundFive, DelayedAcknowledgements) {
  Open();
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client);
  FrameObserver observer(client);
  std::vector<UINT32> pixels(640 * 480, 0x112233);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  observer.ids.clear(); // Test the negotiated window after ACK support is established.
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  std::ranges::fill(pixels, 0x223344);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 2; }));
  for (unsigned i = 0; i < 10; ++i) { std::ranges::fill(pixels, 0x334455 + i); Present(pixels, 640, 480); }
  EXPECT_EQ(observer.ids.size(), 2u);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 3; }));
  EXPECT_TRUE(client.Matches(pixels));
  ASSERT_TRUE(observer.Ack());
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  EXPECT_TRUE(observer.coherent);
}
TEST_F(RoundFive, SuppressOutput) {
  Open();
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client, false);
  FrameObserver observer(client);
  auto update = client.instance->context->update;
  ASSERT_TRUE(update->SuppressOutput(client.instance->context, 0, nullptr));
  ASSERT_EQ(Events(2).size(), 2u);
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
  auto suppressed = Events(1); // Input follows SuppressOutput on the same connection.
  ASSERT_EQ(suppressed.size(), 1u);
  ASSERT_EQ(suppressed.front().type, SDLRDP_KEY);
  auto bytes = client.Received();
  std::vector<UINT32> pixels(640 * 480, 0x123456);
  Present(pixels, 640, 480);
  std::ranges::fill(pixels, 0x654321);
  Present(pixels, 640, 480);
  // Probe for forbidden output after the ordered suppression barrier. No
  // required event or minimum amount of work depends on this observation span.
  for (unsigned i = 0; i < 10; ++i) ASSERT_TRUE(client.Pump(5));
  EXPECT_EQ(client.Received(), bytes);
  EXPECT_TRUE(observer.ids.empty());
  RECTANGLE_16 area{0, 0, 639, 479};
  ASSERT_TRUE(update->SuppressOutput(client.instance->context, 1, &area));
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  EXPECT_TRUE(client.Matches(pixels));
}
TEST_F(RoundFive, AspectAndMouse) {
  Open(640, 350, {4, 3});
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client, false);
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].connected.screen_width, 1024u);
  EXPECT_EQ(events[1].type, SDLRDP_SCREEN);
  EXPECT_EQ(events[1].screen.height, 768u);
  EXPECT_EQ(client.instance->context->gdi->width, 640);
  EXPECT_EQ(client.instance->context->gdi->height, 480);
  FrameObserver observer(client);
  std::vector<UINT32> pixels(640 * 350);
  std::fill_n(pixels.begin() + 175 * 640, 640, 0xffffff);
  Present(pixels, 640, 350);
  ASSERT_TRUE(client.Until([&] { return !observer.ids.empty(); }));
  auto actual = reinterpret_cast<UINT32*>(client.instance->context->gdi->primary_buffer);
  auto rows = std::views::iota(0, 480);
  auto brightest = std::ranges::max_element(rows, {}, [&](int y) { return actual[y * 640] & 255; });
  EXPECT_LE(std::abs(*brightest - 240), 1);
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.instance->context->input, PTR_FLAGS_MOVE, 639, 479));
  events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].mouse_move.x, 639);
  EXPECT_EQ(events[0].mouse_move.y, 349);
  ASSERT_EQ(sdlrdp_set_aspect(backend.get(), {0, 0}), 0);
  ASSERT_TRUE(client.Until([&] { return client.instance->context->gdi->height == 350 && client.Matches(pixels); }));
  EXPECT_EQ(client.instance->context->gdi->width, 640);
}
TEST_F(RoundFive, SparseRegions) {
  for (auto codec : {SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC}) {
    Open(1024, 768, {}, codec);
    Client client(sdlrdp_port(backend.get()), true, 1024, 768);
    Connect(client, false);
    FrameObserver observer(client);
    std::vector<UINT32> pixels(1024 * 768);
    std::generate(pixels.begin(), pixels.end(), [i = 0u]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    Present(pixels, 1024, 768);
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
    auto bytes = client.Received();
    Present(pixels, 1024, 768);
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 2; }));
    auto bounding = client.Received() - bytes;
    RecordProperty("bounding_bytes_" + std::to_string(codec), std::to_string(bounding));
    bytes = client.Received();
    std::array<sdlrdp_rect, 2> damage{{{0, 0, 8, 8}, {1016, 760, 8, 8}}};
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 4096, 1024, 768, damage.data(), 2), 0);
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 3; }));
    auto used = client.Received() - bytes;
    RecordProperty("region_bytes_" + std::to_string(codec), std::to_string(used));
    EXPECT_LT(used, bounding / 100);
  }
}
struct ProcessEnvironment {
  std::filesystem::path cwd = std::filesystem::current_path();
  std::optional<std::string> data;
  ProcessEnvironment() { if (auto value = getenv("XDG_DATA_HOME")) data = value; }
  ~ProcessEnvironment() {
    std::filesystem::current_path(cwd);
    if (data) setenv("XDG_DATA_HOME", data->c_str(), 1); else unsetenv("XDG_DATA_HOME");
  }
};
TEST(Certificate, StableDefaultAndPermissions) {
  CertificateDirectory temporary;
  ProcessEnvironment restore;
  auto data = temporary.path / "data";
  ASSERT_EQ(setenv("XDG_DATA_HOME", data.c_str(), 1), 0);
  std::string first;
  for (auto directory : {temporary.path / "one", temporary.path / "two"}) {
    std::filesystem::create_directory(directory);
    std::filesystem::current_path(directory);
    sdlrdp_config config{"127.0.0.1", 0, nullptr, 320, 200, 0};
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    sdlrdp_close(handle);
    auto certificate = Headless::ReadText((data / "sdl-rdp/server.crt").c_str());
    if (first.empty()) first = certificate; else EXPECT_EQ(first, certificate);
  }
  std::unique_ptr<BIO, Backend::Releases<BIO_free>> bio(BIO_new_mem_buf(first.data(), first.size()));
  std::unique_ptr<X509, Backend::Releases<X509_free>> cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  ASSERT_TRUE(cert);
  std::array<char, 256> hostname{};
  ASSERT_EQ(gethostname(hostname.data(), hostname.size()), 0);
  EXPECT_EQ(X509_check_host(cert.get(), hostname.data(), 0, 0, nullptr), 1);
  int days = 0, seconds = 0;
  ASSERT_TRUE(ASN1_TIME_diff(&days, &seconds, X509_get0_notBefore(cert.get()), X509_get0_notAfter(cert.get())));
  EXPECT_EQ(days, 3650);
  using Perm = std::filesystem::perms;
  EXPECT_EQ(std::filesystem::status(data / "sdl-rdp").permissions() & Perm::mask, Perm::owner_all);
  EXPECT_EQ(std::filesystem::status(data / "sdl-rdp/server.key").permissions() & Perm::mask,
    Perm::owner_read | Perm::owner_write);
}
TEST_F(RoundFive, WaitAndRefresh) {
  Open(320, 200);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  auto connected = Events(2);
  ASSERT_EQ(connected.size(), 2u);
  FrameObserver observer(client);
  std::vector<UINT32> pixels(320 * 200, 0x445566);
  unsigned refresh = 0;
  for (unsigned i = 1; i <= 20; ++i) {
    Present(pixels, 320, 200);
    auto waiting = std::async(std::launch::async, [&] {
      auto& state = *backend->state;
      std::unique_lock lock(state.frame_guard);
      return state.frame_changed.wait_for(lock, std::chrono::seconds(10), [&] {
        return state.current->acknowledged >= state.presented;
      });
    });
    ASSERT_TRUE(client.Until([&] { return observer.ids.size() == i; }));
    EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    ASSERT_TRUE(observer.Ack());
    ASSERT_EQ(waiting.get(), 1);
    observer.ack_processed.push_back(Clock::now());
    // WaitFrame establishes ACK processing, including any refresh event push.
    for (auto const& event : Events())
      if (event.type == SDLRDP_REFRESH) refresh = event.refresh.millihertz;
    if (i < 2) continue;
    auto [low, high] = observer.RefreshBounds();
    EXPECT_GE(refresh / 1000.0, low);
    EXPECT_LE(refresh / 1000.0, high);
  }
  EXPECT_GT(refresh, 0u);
}
TEST_F(RoundFive, NeverAcknowledges) {
  Open(320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  FrameObserver observer(client);
  std::vector<UINT32> pixels(320 * 200, 0x778899);
  auto start = Clock::now();
  Present(pixels, 320, 200);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  EXPECT_GE(Clock::now() - start, std::chrono::milliseconds(200));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
}
TEST_F(RoundFive, ColourDepths) {
  Open(320, 200);
  for (auto depth : {16u, 24u}) {
    Client client(sdlrdp_port(backend.get()), false);
    ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_ColorDepth, depth));
    Connect(client, false);
    client.tolerance = depth == 16 ? 7 : 0;
    EXPECT_EQ(freerdp_settings_get_uint32(client.instance->context->settings, FreeRDP_ColorDepth), depth);
    std::vector<UINT32> pixels(320 * 200);
    std::generate(pixels.begin(), pixels.end(), [i = 0u]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    Present(pixels, 320, 200);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  }
}
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
TEST_F(RoundFive, ProducerDoesNotStarveOrTear) {
  Open(1024, 768);
  Client client(sdlrdp_port(backend.get()), true, 1024, 768);
  Connect(client);
  FrameObserver observer(client);
  std::atomic<unsigned> presents = 0;
  std::jthread producer([&](std::stop_token stop) {
    std::vector<UINT32> pixels(1024 * 768);
    sdlrdp_rect area{0, 0, 1024, 768};
    while (!stop.stop_requested()) {
      auto sequence = presents.load() + 1;
      std::fill_n(pixels.begin(), 1024, sequence);
      std::fill_n(pixels.end() - 1024, 1024, sequence);
      Expects(sdlrdp_present(backend.get(), pixels.data(), 4096, 1024, 768, &area, 1) == 0,
              "concurrent present accepted");
      presents = sequence;
      std::this_thread::yield();
    }
  });
  for (unsigned i = 0; i < 20; ++i) {
    auto before = presents.load();
    auto acknowledged = observer.ids.size();
    ASSERT_TRUE(client.Until([&] {
      return presents.load() > before && observer.ids.size() > acknowledged;
    })) << "both producer and consumer must progress";
    ASSERT_TRUE(observer.Ack());
  }
  producer.request_stop();
  producer.join();
  std::vector<UINT32> final(1024 * 768);
  std::fill_n(final.begin(), 1024, presents.load());
  std::fill_n(final.end() - 1024, 1024, presents.load());
  ASSERT_TRUE(client.Until([&] { observer.Ack(); return client.Matches(final); }));
  EXPECT_TRUE(observer.coherent);
  EXPECT_LE(observer.ids.size(), presents.load());
  EXPECT_TRUE(std::ranges::is_sorted(observer.ids));
  RecordProperty("presents", std::to_string(presents.load()));
  RecordProperty("acknowledged_frames", std::to_string(observer.ids.size()));
}
using Headless::DisplayClient;
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
TEST(Planar, Noisy640Rows) {
  std::unique_ptr<rdpSettings, Backend::Releases<freerdp_settings_free>> settings(freerdp_settings_new(0));
  ASSERT_TRUE(freerdp_settings_set_uint32(settings.get(), FreeRDP_ColorDepth, 32));
  std::unique_ptr<BITMAP_PLANAR_CONTEXT, Backend::Releases<freerdp_bitmap_planar_context_free>>
    encoder(freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE | PLANAR_FORMAT_HEADER_NA, 1, 1));
  ASSERT_TRUE(freerdp_bitmap_planar_context_reset(encoder.get(), 640, 1));
  std::vector<BYTE> payload(640 * 4 + 1024);
  std::unique_ptr<BITMAP_PLANAR_CONTEXT, Backend::Releases<freerdp_bitmap_planar_context_free>>
    decoder(freerdp_bitmap_planar_context_new(0, 640, 1));
  std::vector<UINT32> pixels(640), decoded(640);
  for (unsigned y = 0; y < 480; ++y) {
    std::generate(pixels.begin(), pixels.end(), [i = y * 640]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    UINT32 size = payload.size();
    ASSERT_TRUE(freerdp_bitmap_compress_planar(encoder.get(), reinterpret_cast<BYTE const*>(pixels.data()),
      PIXEL_FORMAT_BGRA32, 640, 1, 2560, payload.data(), &size));
    ASSERT_TRUE(planar_decompress(decoder.get(), payload.data(), size, 640, 1,
      reinterpret_cast<BYTE*>(decoded.data()), PIXEL_FORMAT_BGRX32, 2560, 0, 0, 640, 1, TRUE)) << y;
  }
}
}

TEST_F(RoundFive, AutoPrefersRemoteFX) {
  Open(320, 200, {}, SDLRDP_CODEC_AUTO);
  Client client(sdlrdp_port(backend.get()), true);
  ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
  auto events = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_CONNECTED; });
  });
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end());
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_REMOTEFX);
}
TEST_F(RoundFive, ExpectedDisconnectLogLevels) {
  Open();
  auto peer = WLog_Get("com.freerdp.core.peer");
  for (auto name : {"ERRINFO_LOGOFF_BY_USER", "ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION", "ERRINFO_RPC_INITIATED_DISCONNECT"}) {
    WLog_Print(peer, WLOG_ERROR, "%s [0x00010000]", name);
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, name));
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, name));
  }
  for (auto [category, message] : std::array<std::pair<const char*, const char*>, 9>{{
      {"com.freerdp.core.peer", "ERRCONNECT_CONNECT_TRANSPORT_FAILED [0x0002000D]"},
      {"com.freerdp.core.transport", "BIO_read retries exceeded"},
      {"com.freerdp.core.transport", "BIO_read returned a system error 104: Connection reset by peer"},
      {"com.freerdp.core.transport", "BIO_should_retry returned an error: error:80000068:system library::Connection reset by peer"},
      {"com.freerdp.core.transport", "BIO_write returned a system error 32: Broken pipe"},
      {"com.freerdp.core.transport", "BIO_should_retry returned an error: error:80000020:system library::Broken pipe"},
      {"com.freerdp.core.transport", "BIO_read returned a system error 110: Connection timed out"},
      {"com.freerdp.core.transport", "BIO_read returned a system error 5: Input/output error"},
      {"com.freerdp.core", "ERRCONNECT_CONNECT_TRANSPORT_FAILED [0x0002000D]"}}}) {
    WLog_Print(WLog_Get(category), WLOG_ERROR, "%s", message);
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, message));
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, message));
  }
  auto failure = "BIO_write returned a system error 5: Input/output error";
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "%s", failure);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, failure));
  WLog_Print(peer, WLOG_ERROR, "%s", "BIO_read returned a system error 110: Connection timed out");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "BIO_read returned a system error 110"));
  WLog_Print(peer, WLOG_ERROR, "transport failure marker");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "transport failure marker"));
  WLog_Print(WLog_Get("com.freerdp.core.transport"), WLOG_ERROR, "ERRINFO_LOGOFF_BY_USER [0x0001000C]");
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_ERROR, "ERRINFO_LOGOFF_BY_USER"));
  RecordProperty("trace", logs.Text(true));
}

TEST_F(RoundFive, GraphicsDisconnectDuringWrite) {
  constexpr unsigned width = 2048, height = 2048;
  Open(width, height, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true, width, height);
  client.EnableGraphics();
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  Events();
  std::vector<UINT32> pixels(width * height);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, width, height));
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  UINT32 value = 1;
  for (auto& pixel : pixels) {
    value ^= value << 13; value ^= value >> 17; value ^= value << 5;
    pixel = value & 0xffffff;
  }
  ASSERT_NO_FATAL_FAILURE(Present(pixels, width, height));
  std::array<HANDLE, 64> handles{};
  auto count = freerdp_get_event_handles(client.instance->context, handles.data(), handles.size());
  ASSERT_GT(count, 0u);
  ASSERT_LT(WaitForMultipleObjects(count, handles.data(), FALSE, 10000), WAIT_OBJECT_0 + count);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(freerdp_disconnect(client.instance.get()));
  auto events = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_DISCONNECTED; });
  });
  EXPECT_NE(std::ranges::find(events, SDLRDP_DISCONNECTED, &sdlrdp_event::type), events.end());
  backend.reset();
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "")) << logs.Text(true);
  RecordProperty("trace", logs.Text(true));
}

#include "_detail/headless-audio.hpp"
using Headless::SoundClient;
class AudioGate : public RoundFive {
protected:
  void ConnectAudio(Client& client, SoundClient& audio) {
    Connect(client);
    ASSERT_TRUE(client.Until([&] { return audio.opened; }));
    auto events = EventsUntil([](auto const& events) {
      return std::ranges::any_of(events, [](auto const& event) {
        return event.type == SDLRDP_AUDIO && event.audio.connected;
      });
    }, true, &client);
    ASSERT_TRUE(std::ranges::any_of(events, [](auto const& event) {
      return event.type == SDLRDP_AUDIO && event.audio.connected;
    })) << logs.Text();
  }
  void RunRealtimeAudio(Client& client, SoundClient& audio) {
    Expects(backend && audio.opened, "audio connection exists");
    auto writing = std::async(std::launch::async, [&] {
      std::array<INT16, 480 * 2> pcm{};
      auto start = Clock::now();
      int written = 0;
      for (unsigned tick = 1; tick <= 200; ++tick) {
        std::this_thread::sleep_until(start + std::chrono::milliseconds(tick * 10));
        auto count = sdlrdp_audio_write(backend.get(), pcm.data(), 480);
        if (count != 480) return written;
        written += count;
      }
      return written;
    });
    auto deadline = Clock::now() + std::chrono::seconds(4);
    while (audio.confirmed_frames < 96000 && Clock::now() < deadline) {
      if (!client.Pump(2)) break;
      while (!audio.pending.empty() && Clock::now() - audio.pending.front().received >= std::chrono::milliseconds(150))
        if (!audio.Confirm()) break;
    }
    sdlrdp_audio_close(backend.get());
    EXPECT_EQ(writing.get(), 96000);
    EXPECT_EQ(audio.samples.size() / 2, 96000u);
    ASSERT_GT(audio.received.size(), 1u);
    double maximum_gap = 0;
    for (std::size_t i = 1; i < audio.received.size(); ++i)
      maximum_gap = std::max(maximum_gap, std::chrono::duration<double, std::milli>(audio.received[i] - audio.received[i - 1]).count());
    auto block_ms = 1000.0 * audio.samples.size() / 2 / audio.received.size() / audio.rate;
    RecordProperty("maximum_block_gap_ms", std::to_string(maximum_gap));
    EXPECT_LE(maximum_gap, 2 * block_ms + 10);
    EXPECT_EQ(audio.received.size(), 100u);
    EXPECT_FALSE(logs.Contains("Audio confirmation gate waiting"));
    EXPECT_EQ(audio.confirmed_frames, 96000u);
  }
  void CheckAudioStatistics(SoundClient const& audio) {
    Expects(!backend, "connection statistics have been flushed");
    auto text = logs.Text(true);
    std::smatch match;
    ASSERT_TRUE(std::regex_search(text, match, std::regex(
      R"(Audio: ([0-9]+) blocks sent; gap ([0-9.]+) ms mean, ([0-9.]+) ms max; ([0-9]+) gaps over 40 ms\.)"))) << text;
    EXPECT_EQ(std::stoull(match[1]), audio.received.size());
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Audio:"), 1u);
    EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
    EXPECT_TRUE(std::regex_search(text, std::regex(
      R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, [0-9]+ over 100 ms\.)"))) << text;
  }
  void EstablishConfirmations(Client& client, SoundClient& audio) {
    // Fill one latency window, then return its credit. This distinguishes a
    // slow confirming client from the deliberate no-confirmation fallback.
    std::vector<INT16> pcm(24000 * 2);
    auto automatic = audio.auto_confirm;
    audio.auto_confirm = true;
    ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 24000), 24000);
    ASSERT_TRUE(client.Until([&] { return audio.confirmed_frames == 24000; }));
    ASSERT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
    audio.auto_confirm = automatic;
    audio.samples.clear();
    audio.confirmed_frames = audio.maximum_pending_frames = 0;
  }

};
TEST_F(AudioGate, AudioAbsentDiscards) {
  sdlrdp_audio_close(nullptr);
  EXPECT_EQ(sdlrdp_audio_open(nullptr), -1);
  EXPECT_STREQ(sdlrdp_last_error(), "Invalid audio handle.");
  EXPECT_EQ(sdlrdp_audio_rate(nullptr), 0u);
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  std::vector<INT16> frames(48000 * 10 * 2, 1234);
  EXPECT_EQ(sdlrdp_audio_write(backend.get(), frames.data(), 480000), 480000);
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 1);
  sdlrdp_audio_close(backend.get());
}
TEST_F(AudioGate, AudioPcmAndReconnect) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  for (unsigned connection = 0; connection < 2; ++connection) {
    Client client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
    ASSERT_EQ(audio.server_formats.size(), 2u);
    EXPECT_EQ(audio.server_formats[0].nSamplesPerSec, 48000u);
    EXPECT_EQ(audio.server_formats[1].nSamplesPerSec, 44100u);
    std::array<INT16, 1920> pcm{};
    std::iota(pcm.begin(), pcm.end(), -480);
    ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 960), 960);
    ASSERT_TRUE(client.Until([&] { return audio.samples.size() >= pcm.size(); }));
    EXPECT_EQ(audio.samples.size(), pcm.size());
    EXPECT_EQ(audio.samples.front(), pcm.front());
    EXPECT_EQ(audio.samples.back(), pcm.back());
    EXPECT_TRUE(std::ranges::equal(audio.samples, pcm));
    EXPECT_EQ(audio.pending.size(), 0u);
  }
  RecordProperty("audio_diagnostics", logs.Text(true));
}
TEST_F(AudioGate, AudioFormatMissKeepsSessionAndReconnects) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  for (bool unmatched : {false, true}) {
    Client client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    audio.rate = 22050;
    audio.advertise_unmatched = unmatched;
    Connect(client);
    auto events = EventsUntil([](auto const& events) {
      return std::ranges::any_of(events, [](auto const& e) { return e.type == SDLRDP_AUDIO; });
    }, true, &client);
    auto event = std::ranges::find(events, SDLRDP_AUDIO, &sdlrdp_event::type);
    ASSERT_NE(event, events.end()) << logs.Text();
    EXPECT_EQ(event->audio.connected, 0u);
    EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 0u);
    {
      std::scoped_lock lock(logs.guard);
      EXPECT_EQ(std::ranges::count_if(logs.lines, [&](auto const& line) {
        return line.first == SDLRDP_LOG_WARN
          && line.second.contains(unmatched ? "rate=22050" : "client formats: none");
      }), 1);
    }
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "client doesn't support any format"));
    FrameObserver observer(client);
    Present(std::vector<UINT32>(320 * 200, 0x123456), 320, 200);
    ASSERT_TRUE(client.Until([&] { return !observer.ids.empty(); }));
    ASSERT_TRUE(observer.Ack());
    ASSERT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.instance->context->input, KBD_FLAGS_DOWN, 0x1e));
    events = EventsUntil([](auto const& events) {
      return std::ranges::any_of(events, [](auto const& e) { return e.type == SDLRDP_KEY; });
    }, true, &client);
    EXPECT_NE(std::ranges::find(events, SDLRDP_KEY, &sdlrdp_event::type), events.end());
    EXPECT_EQ(std::ranges::find(events, SDLRDP_DISCONNECTED, &sdlrdp_event::type), events.end());
  }
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 48000u);
}
TEST_F(AudioGate, AudioInitialVolume) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 0u);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.rate = 44100;
  audio.volume = 0x8000ffffu;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  EXPECT_EQ(sdlrdp_audio_rate(backend.get()), 44100u);
  std::vector<INT16> pcm(882 * 2);
  std::generate(pcm.begin(), pcm.end(), [i = 0]() mutable { return ++i % 2 ? -12000 : 12000; });
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 44), 44);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data() + 88, 838), 838);
  ASSERT_TRUE(client.Until([&] { return audio.samples.size() == pcm.size(); }));
  for (auto frame : audio.samples | std::views::chunk(2)) {
    EXPECT_EQ(frame[0], -12000);
    EXPECT_EQ(frame[1], 6000);
  }
  RecordProperty("volume_pcm", "44100 Hz; 44+838 frames; left=-12000 right=6000; volume=0x8000ffff");
}
TEST_F(AudioGate, AudioSlowConfirmsBoundTenSeconds) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ASSERT_NO_FATAL_FAILURE(EstablishConfirmations(client, audio));
  std::vector<INT16> pcm(480000 * 2, 1234);
  auto writing = std::async(std::launch::async, [&] {
    return sdlrdp_audio_write(backend.get(), pcm.data(), 480000);
  });
  auto deadline = Clock::now() + std::chrono::seconds(15);
  while (audio.confirmed_frames < 480000 && Clock::now() < deadline) {
    if (!client.Pump(2)) break;
    while (!audio.pending.empty() && Clock::now() - audio.pending.front().received >= std::chrono::milliseconds(80))
      if (!audio.Confirm()) break;
  }
  if (audio.confirmed_frames < 480000) sdlrdp_audio_close(backend.get());
  EXPECT_EQ(audio.confirmed_frames, 480000u);
  EXPECT_LE(audio.maximum_pending_frames, 24960u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "Audio confirmation gate waiting: client is 500.000 ms behind."));
  RecordProperty("audio_diagnostics", logs.Text(true));
  EXPECT_EQ(writing.get(), 480000);
  RecordProperty("maximum_unconfirmed_ms", std::to_string(audio.maximum_pending_frames / 48.0));
}
TEST_F(AudioGate, AudioPlaybackConfirmsKeepRealtimeStreamContinuous) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  RunRealtimeAudio(client, audio);
  freerdp_disconnect(client.instance.get());
  backend.reset();
  CheckAudioStatistics(audio);
}
TEST_F(AudioGate, AudioContinuousUnderProgressiveLoad) {
  Open(1280, 800, {}, SDLRDP_CODEC_PROGRESSIVE);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true, 1280, 800);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  SoundClient audio(client);
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  // Pixel decoding on the client pump thread would delay audio reception independently of server encoding.
  observer.channel->SurfaceCommand = [](RdpgfxClientContext* channel, RDPGFX_SURFACE_COMMAND const* command) -> UINT {
    Expects(channel && command && command->codecId == RDPGFX_CODECID_CAPROGRESSIVE, "progressive payload received");
    return CHANNEL_RC_OK;
  };
  auto presenting = std::async(std::launch::async, [&] {
    std::vector<UINT32> pixels(1280 * 800);
    sdlrdp_rect full{0, 0, 1280, 800};
    auto deadline = Clock::now() + std::chrono::seconds(2);
    unsigned presented = 0;
    while (Clock::now() < deadline) {
      if (!sdlrdp_wait_frame(backend.get(), 10)) continue;
      Headless::MovingTilePattern(pixels, 1280, 800, presented);
      if (sdlrdp_present(backend.get(), pixels.data(), 5120, 1280, 800, &full, 1)) break;
      ++presented;
    }
    return presented;
  });
  RunRealtimeAudio(client, audio);
  EXPECT_GE(presenting.get(), 10u);
  EXPECT_GE(observer.frames.size(), 10u);
  freerdp_disconnect(client.instance.get());
  backend.reset();
  CheckAudioStatistics(audio);
}
TEST_F(AudioGate, AudioNeverConfirmsUsesServerClock) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  std::vector<INT16> pcm(48000 * 2, 1234);
  auto started = Clock::now();
  auto writing = std::async(std::launch::async, [&] {
    return sdlrdp_audio_write(backend.get(), pcm.data(), 48000);
  });
  auto captured = client.Until([&] { return audio.samples.size() == pcm.size(); });
  if (!captured) sdlrdp_audio_close(backend.get());
  EXPECT_TRUE(captured);
  EXPECT_EQ(writing.get(), 48000);
  auto elapsed = std::chrono::duration<double>(Clock::now() - started).count();
  EXPECT_GE(elapsed, 0.9);
  EXPECT_TRUE(logs.Contains("500"));
  RecordProperty("never_confirms_one_second_elapsed", std::to_string(elapsed));
}
TEST_F(AudioGate, AudioDisconnectDuringBlockedWrite) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  for (bool reconnect : {false, true}) {
    Client client(sdlrdp_port(backend.get()), true);
    SoundClient audio(client);
    audio.auto_confirm = reconnect;
    ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
    ASSERT_NO_FATAL_FAILURE(EstablishConfirmations(client, audio));
    unsigned frames = reconnect ? 960 : 480000;
    std::vector<INT16> pcm(frames * 2, 1234);
    auto writing = std::async(std::launch::async, [&] {
      return sdlrdp_audio_write(backend.get(), pcm.data(), frames);
    });
    auto received = client.Until([&] { return audio.samples.size() >= (reconnect ? 1920u : 48000u); });
    EXPECT_TRUE(received);
    if (!reconnect) EXPECT_EQ(writing.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
    EXPECT_TRUE(freerdp_disconnect(client.instance.get()));
    EXPECT_EQ(writing.get(), frames);
    if (reconnect) EXPECT_EQ(audio.samples.size(), 1920u);
  }
}
TEST_F(AudioGate, AudioOneMillisecondPartialBlock) {
  Open(320, 200, {}, SDLRDP_CODEC_RAW, 1);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  std::array<INT16, 1920> pcm{};
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 48), 48);
  auto writing = std::async(std::launch::async, [&] {
    return sdlrdp_audio_write(backend.get(), pcm.data() + 96, 912);
  });
  auto captured = client.Until([&] { return audio.samples.size() == pcm.size(); });
  if (!captured) sdlrdp_audio_close(backend.get());
  EXPECT_TRUE(captured);
  EXPECT_EQ(writing.get(), 912);
}
TEST_F(AudioGate, AudioFallbackIdleDoesNotAccumulateCredit) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  std::vector<INT16> pcm(48000 * 2, 1234);
  for (unsigned burst = 1; burst <= 2; ++burst) {
    auto started = Clock::now();
    auto writing = std::async(std::launch::async, [&] {
      return sdlrdp_audio_write(backend.get(), pcm.data(), 48000);
    });
    auto captured = client.Until([&] { return audio.samples.size() == burst * pcm.size(); });
    if (!captured) sdlrdp_audio_close(backend.get());
    EXPECT_TRUE(captured);
    EXPECT_EQ(writing.get(), 48000);
    EXPECT_GE(Clock::now() - started, std::chrono::milliseconds(burst == 1 ? 950 : 450));
    if (burst == 1) std::this_thread::sleep_for(std::chrono::milliseconds(500));
  }
}

TEST_F(AudioGate, AudioReorderedConfirmsCreditOnlyTheirBlock) {
  Open(320, 200);
  ASSERT_EQ(sdlrdp_audio_open(backend.get()), 0);
  Client client(sdlrdp_port(backend.get()), true);
  SoundClient audio(client);
  audio.auto_confirm = false;
  ASSERT_NO_FATAL_FAILURE(ConnectAudio(client, audio));
  ASSERT_NO_FATAL_FAILURE(EstablishConfirmations(client, audio));
  std::vector<INT16> pcm(24000 * 2, 1234);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 24000), 24000);
  ASSERT_TRUE(client.Until([&] { return audio.pending.size() == 25; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 0);
  ASSERT_TRUE(audio.Confirm(24));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
  ASSERT_EQ(sdlrdp_audio_write(backend.get(), pcm.data(), 960), 960);
  ASSERT_TRUE(client.Until([&] { return audio.samples.size() == 49920; }));
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 0), 0);
  ASSERT_TRUE(audio.Confirm());
  EXPECT_EQ(sdlrdp_audio_wait(backend.get(), 10000), 1);
  std::scoped_lock lock(logs.guard);
  EXPECT_EQ(std::ranges::count_if(logs.lines, [](auto const& line) {
    return line.first == SDLRDP_LOG_WARN && line.second.contains("Audio confirmation gate waiting");
  }), 1);
}

namespace {
class GraphicsGate : public Gate {};
TEST_P(GraphicsGate, DecodesAndResizes) {
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  client.tolerance = GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  ASSERT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "GFX advertised"));
  EXPECT_TRUE(logs.Contains("GFX confirmed version=0x000a0701"));
  Frame(client, {0, 0, 320, 200});
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
  EXPECT_LE(client.MaxError(pixels), client.tolerance);
  EXPECT_EQ(observer.commands, GetParam().codec == SDLRDP_CODEC_PLANAR ? 200u : 1u);
  auto events = Events(2);
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end());
  EXPECT_EQ(connected->connected.codec, GetParam().codec);
  std::vector<UINT32> resized(352 * 224, 0x0055aaff);
  sdlrdp_rect full{0, 0, 352, 224};
  ASSERT_EQ(sdlrdp_present(backend.get(), resized.data(), 352 * 4, 352, 224, &full, 1), 0);
  ASSERT_TRUE(client.Until([&] { return client.Matches(resized); })) << logs.Text(true);
  EXPECT_EQ(client.instance->context->gdi->width, 352);
  ASSERT_EQ(observer.surfaces.size(), 2u);
  EXPECT_EQ(observer.deleted, 1u);
  EXPECT_EQ(observer.surfaces.back().width, 352);
  EXPECT_EQ(observer.surfaces.back().height, 224);
  EXPECT_EQ(observer.progressive_headers, GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 2u : 0u);
  RecordProperty("trace", logs.Text(true));
}
TEST_P(GraphicsGate, AcknowledgementPacingAndSuspend) {
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  sdlrdp_rect full{0, 0, 320, 200};
  for (unsigned count = 1; count <= 2; ++count) {
    std::ranges::fill(pixels, count * 0x00202020u);
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; })) << logs.Text(true);
  }
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  EXPECT_EQ(observer.frames.size(), 2u);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
  ASSERT_TRUE(observer.Ack(SUSPEND_FRAME_ACKNOWLEDGEMENT));
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 3; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Pump(20));
  for (unsigned count = 4; count <= 5; ++count) {
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; }));
  }
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
  RecordProperty("trace", "two unacknowledged frames exhaust the window; suspend releases third; resume waits; cumulative ack releases wait");
}
TEST_P(GraphicsGate, QueueDepthThrottlesBytes) {
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  sdlrdp_rect full{0, 0, 320, 200};
  for (unsigned count = 1; count <= 2; ++count) {
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; }));
  }
  ASSERT_TRUE(observer.AckFrame(0, 10000000));
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
  auto deadline = Clock::now() + std::chrono::milliseconds(80);
  while (Clock::now() < deadline) ASSERT_TRUE(client.Pump());
  EXPECT_EQ(observer.frames.size(), 2u);
  ASSERT_TRUE(observer.AckFrame(0, 0));
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 3; }));
  RecordProperty("trace", "ack frame 1 with 10000000 queued bytes holds frame 3 despite one free frame slot; queueDepth=0 releases it");
}
TEST_P(GraphicsGate, RejectedChannelUsesLegacy) {
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::DisplayClient display(client);
  client.tolerance = GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX channel rejected"); })) << logs.Text(true);
  Frame(client, {0, 0, 320, 200});
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  RecordProperty("trace", "GCC negotiates GFX; client registers only disp; graphics DVC is rejected; legacy frame decodes");
}
TEST_P(GraphicsGate, TakeoverWithLegacy) {
  Client graphics(sdlrdp_port(backend.get()), true);
  graphics.EnableGraphics();
  graphics.tolerance = GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0;
  ASSERT_TRUE(freerdp_connect(graphics.instance.get()));
  ASSERT_TRUE(graphics.Until([&] { return logs.Contains("GFX confirmed"); }));
  Frame(graphics, {0, 0, 320, 200});
  Client legacy(sdlrdp_port(backend.get()), true);
  legacy.tolerance = GetParam().codec == SDLRDP_CODEC_PROGRESSIVE ? 24 : 0;
  ASSERT_TRUE(freerdp_connect(legacy.instance.get()));
  ASSERT_TRUE(legacy.Until([&] { return legacy.Matches(pixels); }));
  EXPECT_FALSE(freerdp_settings_get_bool(legacy.instance->context->settings, FreeRDP_SupportGraphicsPipeline));
  Client next(sdlrdp_port(backend.get()), true);
  next.EnableGraphics();
  next.tolerance = graphics.tolerance;
  ASSERT_TRUE(freerdp_connect(next.instance.get()));
  ASSERT_TRUE(next.Until([&] { return next.Matches(pixels); })) << logs.Text(true);
  RecordProperty("trace", "pipeline frame -> legacy takeover frame -> fresh pipeline takeover frame");
}
INSTANTIATE_TEST_SUITE_P(Pipeline, GraphicsGate, testing::Values(
  Mode{true, SDLRDP_CODEC_PLANAR}, Mode{true, SDLRDP_CODEC_RAW}, Mode{true, SDLRDP_CODEC_PROGRESSIVE}), ModeName);
}

namespace {
std::vector<UINT32> GraphicsScene(unsigned frame, bool noise)
{
  std::vector<UINT32> pixels(640 * 480, 0x00010101);
  if (noise) {
    UINT32 value = frame + 1;
    for (auto& pixel : pixels) {
      value ^= value << 13; value ^= value >> 17; value ^= value << 5;
      pixel = value & 0xffffff;
    }
  } else for (unsigned y = 40; y < 72; ++y)
    for (unsigned x = frame % 640; x < std::min(frame % 640 + 32, 640u); ++x)
      pixels[y * 640 + x] = 0x0000ff00;
  return pixels;
}
class GraphicsMeasurement : public RoundFive {
protected:
  void RecordGraphicsTiming(Client& client, sdlrdp_codec codec) {
    if (codec != SDLRDP_CODEC_PROGRESSIVE) return;
    ASSERT_TRUE(client.Until([&] {
      std::scoped_lock lock(backend->state->session_guard);
      auto const& peer = *backend->state->current;
      return peer.graphics_qoe.frameId == peer.frame_id;
    }));
    std::scoped_lock lock(backend->state->session_guard);
    auto const& peer = *backend->state->current;
    RecordProperty("activation_to_gfx_ms", std::to_string(
      std::chrono::duration<double, std::milli>(peer.graphics_ready_time).count()));
    RecordProperty("client_decode_ms", peer.graphics_qoe.timeDiffSE);
    RecordProperty("client_render_ms", peer.graphics_qoe.timeDiffEDR);
    RecordProperty("client_qoe_frame", peer.graphics_qoe.frameId);
    EXPECT_FALSE(logs.Contains("GFX QoE"));
  }
  void Measure(sdlrdp_codec codec, bool noise) {
    Open(640, 480, {}, codec);
    Client client(sdlrdp_port(backend.get()), true, 640, 480);
    if (codec == SDLRDP_CODEC_PROGRESSIVE) client.EnableGraphics();
    ASSERT_TRUE(freerdp_settings_set_bool(client.instance->context->settings, FreeRDP_GfxSendQoeAck, TRUE));
    Connect(client);
    if (codec == SDLRDP_CODEC_PROGRESSIVE)
      ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
    Present(GraphicsScene(0, noise), 640, 480);
    ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
    auto encode = [&] {
      std::scoped_lock lock(backend->state->session_guard);
      return backend->state->current->encoder.encode_time;
    };
    auto initial_encode = encode();
    auto initial_bytes = client.Received();
    auto start = Clock::now();
    unsigned maximum_error = 0;
    double latency = 0;
    for (unsigned frame = 1; frame <= 20; ++frame) {
      auto pixels = GraphicsScene(frame, noise);
      auto presented = Clock::now();
      sdlrdp_rect damage = noise ? sdlrdp_rect{0, 0, 640, 480} : sdlrdp_rect{int(frame - 1), 40, 33, 32};
      ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 640 * 4, 640, 480, &damage, 1), 0);
      ASSERT_TRUE(client.Until([&] { return Acknowledged(); })) << logs.Text(true);
      latency += std::chrono::duration<double, std::milli>(Clock::now() - presented).count();
      maximum_error = std::max(maximum_error, client.MaxError(pixels));
    }
    auto elapsed = std::chrono::duration<double>(Clock::now() - start).count();
    auto bytes = client.Received() - initial_bytes;
    auto milliseconds = std::chrono::duration<double, std::milli>(encode() - initial_encode).count();
    RecordProperty("wire_MB_per_second", std::to_string(bytes / elapsed / 1000000));
    RecordProperty("wire_MB_per_second_at_60fps", std::to_string(bytes * 3.0 / 1000000));
    RecordProperty("encode_ms_per_frame", std::to_string(milliseconds / 20));
    RecordProperty("present_ack_ms_per_frame", std::to_string(latency / 20));
    RecordProperty("maximum_channel_error", maximum_error);
    RecordGraphicsTiming(client, codec);
    EXPECT_LE(maximum_error, noise ? 48u : 24u);
  }
};
TEST_F(GraphicsMeasurement, ProgressiveMovingBlock) { Measure(SDLRDP_CODEC_PROGRESSIVE, false); }
TEST_F(GraphicsMeasurement, RemoteFxMovingBlock) { Measure(SDLRDP_CODEC_REMOTEFX, false); }
TEST_F(GraphicsMeasurement, ProgressiveNoise) { Measure(SDLRDP_CODEC_PROGRESSIVE, true); }
TEST_F(GraphicsMeasurement, RemoteFxNoise) { Measure(SDLRDP_CODEC_REMOTEFX, true); }
}

namespace {
TEST_F(RoundFive, GraphicsAutoUsesProgressive) {
  Open(640, 480, {}, SDLRDP_CODEC_AUTO);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  auto events = Events();
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end());
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_PROGRESSIVE);
  auto pixels = GraphicsScene(3, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
  pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) && client.Matches(pixels); }));
  events = Events();
  auto changed = std::ranges::find(events, SDLRDP_CODEC_CHANGED, &sdlrdp_event::type);
  ASSERT_NE(changed, events.end());
  EXPECT_EQ(changed->codec_changed.codec, SDLRDP_CODEC_RAW);
  RecordProperty("trace", "auto connects as progressive; live raw preference produces exact RGB and CODEC_CHANGED raw");
}
}

namespace {
TEST_F(RoundFive, ProgressiveDamageAndQoe) {
  Open(640, 480, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  auto pixels = GraphicsScene(5, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  auto before = client.Received();
  sdlrdp_rect damage{17, 19, 7, 5};
  for (int y = damage.y; y < damage.y + damage.h; ++y)
    for (int x = damage.x; x < damage.x + damage.w; ++x) pixels[y * 640 + x] = 0x00ff0000;
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 640 * 4, 640, 480, &damage, 1), 0);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
  EXPECT_EQ(observer.progressive_headers, 1u);
  EXPECT_EQ(observer.surfaces.size(), 1u);
  EXPECT_LT(client.Received() - before, 4096u);
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU qoe{observer.frames.back().frameId, 1234, 7, 9};
  ASSERT_EQ(observer.channel->QoeFrameAcknowledge(observer.channel, &qoe), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] {
    std::scoped_lock lock(backend->state->session_guard);
    auto const& received = backend->state->current->graphics_qoe;
    return received.timestamp == qoe.timestamp && received.timeDiffSE == 7 && received.timeDiffEDR == 9;
  }));
  EXPECT_FALSE(logs.Contains("GFX QoE"));
  RecordProperty("damage_wire_bytes", std::to_string(client.Received() - before));
  RecordProperty("maximum_channel_error", client.MaxError(pixels));
  RecordProperty("trace", "7x5 damage at 17,19; one progressive header over two frames; QoE timestamp=1234 decode=7 render=9 retained without logging");
}
}

namespace {
TEST_F(RoundFive, GraphicsVersion101) {
  Open(640, 480, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  ASSERT_TRUE(freerdp_settings_set_uint32(client.instance->context->settings, FreeRDP_GfxCapsFilter, ((1u << 11) - 1) & ~(1u << 3)));
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed version=0x000a0100 flags=0x00000000"); }));
  auto pixels = GraphicsScene(3, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
  RecordProperty("trace", "10.1-only advertisement confirms its 16-byte reserved capability data with no flags; progressive decodes");
}
}

namespace {
TEST_F(RoundFive, GraphicsWithoutDynamicChannelsUsesLegacy) {
  Open(640, 480, {}, SDLRDP_CODEC_RAW);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  ASSERT_TRUE(freerdp_settings_set_bool(client.instance->context->settings, FreeRDP_SupportGraphicsPipeline, TRUE));
  client.instance->LoadChannels = [](freerdp*) -> BOOL { return TRUE; };
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  auto events = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto event) { return event.type == SDLRDP_CONNECTED; });
  }, false, &client);
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end()) << logs.Text();
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_RAW);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "GFX confirmation timed out"));
  {
    std::scoped_lock lock(backend->state->session_guard);
    auto const& peer = *backend->state->current;
    auto elapsed = Clock::now() - peer.activated_at;
    EXPECT_GE(elapsed, Backend::Peer::GraphicsConnectionWait);
    EXPECT_FALSE(peer.gfx);
    EXPECT_FALSE(peer.connection);
    RecordProperty("activation_to_legacy_ms", std::to_string(std::chrono::duration<double, std::milli>(elapsed).count()));
  }
  auto pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  RecordProperty("trace", "GFX setting on; no DRDYNVC/addin; deadline emits legacy connected; raw frame decodes");
}
TEST_F(RoundFive, GraphicsWithoutCapabilitiesUsesLegacy) {
  Open(640, 480, {}, SDLRDP_CODEC_RAW);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.advertise = false;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  auto events = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto event) { return event.type == SDLRDP_CONNECTED; });
  }, false, &client);
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end()) << logs.Text();
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_RAW);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "GFX confirmation timed out"));
  EXPECT_FALSE(logs.Contains("GFX confirmed"));
  auto pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  RecordProperty("trace", "GFX DVC opened; CapsAdvertise withheld; deadline emits legacy connected; raw frame decodes");
}
}

namespace {
TEST_F(RoundFive, GraphicsCodecSwitchPreservesUndamagedTiles) {
  Open(640, 480, {}, SDLRDP_CODEC_RAW);
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  Connect(client);
  auto pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_PROGRESSIVE), 0);
  sdlrdp_rect damage{18, 45, 1, 1};
  pixels[45 * 640 + 18] = 0x0000ee00;
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 640 * 4, 640, 480, &damage, 1), 0);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
  RecordProperty("trace", "raw picture; switch to progressive with one pixel of damage; entire decoded picture preserved");
}
}

namespace {
TEST_F(RoundFive, PipelinedLegacyPresent) {
  Open(320, 200);
  Client client(sdlrdp_port(backend.get()), true);
  Connect(client);
  FrameObserver observer(client);
  std::vector<UINT32> pixels(320 * 200, 0x123456);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 1; }));
  Present(pixels, 320, 200);
  ASSERT_TRUE(client.Until([&] { return observer.ids.size() == 2; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  auto update = client.instance->context->update;
  ASSERT_TRUE(update->SurfaceFrameAcknowledge(update->context, observer.ids.front()));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
}
TEST_F(RoundFive, PipelinedGraphicsPresent) {
  Open(320, 200, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> pixels(320 * 200, 0x123456);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 1; }));
  Present(pixels, 320, 200);
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 2; }));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  ASSERT_TRUE(observer.AckFrame(0, 0));
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
}
}

namespace {
TEST_F(RoundFive, GraphicsFrameStatistics) {
  Open(320, 200, {}, SDLRDP_CODEC_PROGRESSIVE);
  Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  observer.automatic = false;
  Connect(client);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> pixels(320 * 200, 0x123456);
  for (unsigned count = 1; count <= 2; ++count) {
    Present(pixels, 320, 200);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() == count; }));
  }
  for (unsigned count = 0; count < 3; ++count) Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
  ASSERT_TRUE(observer.AckFrame(0, 0));
  ASSERT_TRUE(client.Until([&] { return observer.frames.size() == 3; }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  {
    std::scoped_lock lock(backend->state->frame_guard);
    EXPECT_EQ(backend->state->current->ack_count, 3u);
  }
  freerdp_disconnect(client.instance.get());
  backend.reset();
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "Frames: 3 sent, 2 coalesced; encode ")) << logs.Text(true);
  auto text = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(std::regex_search(text, match, std::regex(R"(acknowledgement [0-9.]+ ms mean, [0-9.]+ ms max, ([0-9]+) over 100 ms\.)"))) << text;
  EXPECT_EQ(match[1], "0");
  RecordProperty("statistics", logs.Text(true));
}
}
