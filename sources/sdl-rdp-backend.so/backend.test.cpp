#include "_detail/headless-client.hpp"
#include "sdl-rdp-backend.h"
#include "_detail/contract.hpp"
#include <gtest/gtest.h>
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
#include <thread>
#include "_detail/test-io.hpp"
#include <charconv>
#include <format>
#include <mutex>
#include <winpr/wlog.h>
#include <sys/wait.h>
#include <cstring>
#include <cerrno>
#include <openssl/ssl.h>
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
struct Logs {
  std::mutex guard;
  std::vector<std::pair<sdlrdp_log_level, std::string>> lines;
  static void Collect(void* user, sdlrdp_log_level level, const char* text) {
    auto& self = *static_cast<Logs*>(user);
    std::scoped_lock lock(self.guard);
    self.lines.emplace_back(level, text);
  }
  std::string Text() {
    std::scoped_lock lock(guard);
    return lines | std::views::filter([](auto const& line) { return line.first != SDLRDP_LOG_INFO; })
      | std::views::transform([](auto const& line) -> auto const& { return line.second; })
      | std::views::join_with('\n') | std::ranges::to<std::string>();
  }
  bool Contains(sdlrdp_log_level level, std::string_view text) {
    std::scoped_lock lock(guard);
    return std::ranges::any_of(lines, [=](auto const& line) {
      return line.first == level && line.second.contains(text);
    });
  }
  bool Contains(std::string_view text) {
    std::scoped_lock lock(guard);
    return std::ranges::any_of(lines, [=](auto const& line) { return line.second.contains(text); });
  }
};
TEST(CopyRows, PaddedRows) {
  std::array<BYTE, 8> source{1, 2, 9, 9, 3, 4, 9, 9};
  std::array<BYTE, 6> destination{8, 8, 8, 8, 8, 8};
  Backend::CopyRows(source, 4, destination, 3, 2, 2);
  EXPECT_EQ(destination, (std::array<BYTE, 6>{1, 2, 8, 3, 4, 8}));
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
  Socket socket;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(sdlrdp_port(raw));
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  Expects(socket.descriptor >= 0 && connect(socket.descriptor,
    reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "TLS socket connects");
  std::array<unsigned char, 19> negotiation{3, 0, 0, 19, 14, 224, 0, 0, 0, 0, 0, 1, 0, 8, 0, 1, 0, 0, 0};
  Expects(send(socket.descriptor, negotiation.data(), negotiation.size(), 0) == 19
    && recv(socket.descriptor, negotiation.data(), negotiation.size(), MSG_WAITALL) == 19,
    "RDP TLS negotiation completes");
  std::unique_ptr<SSL_CTX, decltype(&SSL_CTX_free)> context(SSL_CTX_new(TLS_client_method()), SSL_CTX_free);
  Expects(context != nullptr, "TLS context allocated");
  std::unique_ptr<SSL, decltype(&SSL_free)> tls(SSL_new(context.get()), SSL_free);
  Expects(tls != nullptr, "TLS session allocated");
  SSL_set_verify(tls.get(), SSL_VERIFY_NONE, nullptr);
  Expects(SSL_set_fd(tls.get(), socket.descriptor) == 1 && SSL_connect(tls.get()) == 1,
          "TLS initialization handshake completes");
  SSL_shutdown(tls.get());
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
class Gate : public testing::TestWithParam<Mode> {
protected:
  CertificateDirectory certificates;
  Logs logs;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend{nullptr, sdlrdp_close};
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
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  }
  std::vector<sdlrdp_event> Events(unsigned wanted) {
    std::vector<sdlrdp_event> result;
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (result.size() < wanted && Clock::now() < deadline) {
      sdlrdp_wait(backend.get(), 50);
      std::array<sdlrdp_event, 16> batch{};
      auto count = sdlrdp_poll(backend.get(), batch.data(), batch.size());
      result.insert(result.end(), batch.begin(), batch.begin() + count);
    }
    return result;
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
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  auto events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  ASSERT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 320u);
  EXPECT_EQ(events[0].connected.height, 200u);
  EXPECT_EQ(events[0].connected.bpp, 32u);
  EXPECT_EQ(events[0].connected.codec, GetParam().codec);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  FrameCounter counter(client);
  auto bytes = client.Received();
  auto started = Clock::now();
  ASSERT_NO_FATAL_FAILURE(Frame(client, {0, 0, 320, 200}));
  if (GetParam().codec == SDLRDP_CODEC_PLANAR)
    EXPECT_LE(counter.bitmap_pdus, 1 + (client.Received() - bytes) / 0xFFFF);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
  RecordProperty("wire_bytes", std::to_string(client.Received() - bytes));
  RecordProperty("codec", std::to_string(GetParam().codec));
  RecordProperty("surface", bool(GetParam()) ? "true" : "false");
  EXPECT_LT(Clock::now() - started, std::chrono::seconds(2));
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
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed"));
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
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 320u);
  EXPECT_EQ(events[1].type, SDLRDP_RESIZE);
  EXPECT_EQ(events[1].resize.width, 320u);
  EXPECT_EQ(events[1].resize.height, 200u);
  EXPECT_EQ(sdlrdp_wait(handle, 1), 0);
  auto waiter = std::async(std::launch::async, [=] { return sdlrdp_wait(handle, 1000); });
  auto deadline = Clock::now() + std::chrono::milliseconds(500);
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
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  auto before = ResidentBytes();
  for (unsigned frame = 0; frame < 200; ++frame) {
    std::ranges::fill(pixels, 0x00010101u * (frame + 1));
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  }
  auto after = ResidentBytes();
  RecordProperty("burst_rss_growth", std::to_string(std::int64_t(after) - std::int64_t(before)));
  EXPECT_LE(after, before + pixels.size() * 4);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
}
TEST_P(Gate, ClipsToDesktop) {
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
  Backend::CopyRows({reinterpret_cast<BYTE const*>(frame.data()), frame.size() * 4}, 640 * 4,
    {reinterpret_cast<BYTE*>(pixels.data()), pixels.size() * 4}, 320 * 4, 200, 320 * 4);
  Client client(sdlrdp_port(handle), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  EXPECT_EQ(client.instance->context->gdi->width, 320);
  EXPECT_EQ(client.instance->context->gdi->height, 200);
  EXPECT_FALSE(logs.Contains("failed"));
  area = {400, 300, 40, 30};
  std::ranges::fill(frame, 0x00010203u);
  ASSERT_EQ(sdlrdp_present(handle, frame.data(), 640 * 4, 640, 480, &area, 1), 0);
  auto deadline = Clock::now() + std::chrono::milliseconds(100);
  do {
    ASSERT_TRUE(client.Pump());
    ASSERT_TRUE(client.Matches(pixels));
  } while (Clock::now() < deadline);
  EXPECT_FALSE(logs.Contains("failed"));
}
TEST_P(Gate, WaitForClient) {
  auto port = sdlrdp_port(backend.get());
  backend.reset();
  sdlrdp_config config{"127.0.0.1", port, certificates.path.c_str(), 320, 200, 1};
  config.codec = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  auto opening = std::async(std::launch::async, [&] { return sdlrdp_open(&config, &handle); });
  auto deadline = Clock::now() + std::chrono::seconds(3);
  while (!Listening(port) && Clock::now() < deadline) std::this_thread::yield();
  EXPECT_EQ(opening.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  Client client(port, GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_EQ(opening.wait_for(std::chrono::seconds(3)), std::future_status::ready);
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
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  auto events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  ASSERT_EQ(events.front().type, SDLRDP_CONNECTED);
  pixels.resize(2048 * 1536);
  std::generate(pixels.begin(), pixels.end(), [index = 0u]() mutable {
    return (index++ * 2654435761u) & 0x00ffffff; });
  sdlrdp_rect area{0, 0, 2048, 1536};
  auto started = Clock::now();
  ASSERT_EQ(sdlrdp_present(handle, pixels.data(), 2048 * 4, 2048, 1536, &area, 1), 0);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  auto matches = client.Until([&] { return client.Matches(pixels); });
  EXPECT_TRUE(matches) << "maximum channel error " << client.MaxError(pixels);
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
  RecordProperty("blocked_present_ms", std::to_string(elapsed.count()));
  EXPECT_LT(elapsed, std::chrono::seconds(2));
}
TEST_P(Gate, NewestClientTakesOver) {
  Client first(sdlrdp_port(backend.get()), GetParam());
  ASSERT_TRUE(freerdp_connect(first.instance.get()));
  ASSERT_EQ(Events(1).size(), 1u);
  Client second(sdlrdp_port(backend.get()), GetParam(), 400, 240);
  ASSERT_TRUE(freerdp_connect(second.instance.get()));
  auto events = Events(3);
  ASSERT_EQ(events.size(), 3u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  EXPECT_EQ(events[1].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[1].connected.width, 400u);
  EXPECT_EQ(events[1].connected.height, 240u);
  EXPECT_EQ(events[2].type, SDLRDP_RESIZE);
  freerdp_input_send_keyboard_event(first.instance->context->input, KBD_FLAGS_DOWN, 0x30);
  auto deadline = Clock::now() + std::chrono::seconds(3);
  bool connected = true;
  while (connected && Clock::now() < deadline) connected = first.Pump();
  ASSERT_FALSE(connected);
  ASSERT_NO_FATAL_FAILURE(Input(second));
  ASSERT_TRUE(freerdp_disconnect(second.instance.get()));
  events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  EXPECT_EQ(sdlrdp_wait(backend.get(), 0), 0);
}
TEST_P(Gate, LiveCodecChange) {
  Client client(sdlrdp_port(backend.get()), GetParam());
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_EQ(Events(1).size(), 1u);
  auto previous = GetParam().codec;
  for (auto codec : {SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX,
                     SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_AUTO}) {
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), codec), 0);
    client.tolerance = codec == SDLRDP_CODEC_REMOTEFX && GetParam().surface ? 40 : 0;
    std::ranges::fill(pixels, 0x00404040u + unsigned(codec) * 0x00040404u);
    ASSERT_NO_FATAL_FAILURE(Frame(client, {0, 0, 320, 200}));
    auto expected = codec == SDLRDP_CODEC_AUTO || (!GetParam().surface
      && (codec == SDLRDP_CODEC_REMOTEFX || codec == SDLRDP_CODEC_NSCODEC)) ? SDLRDP_CODEC_PLANAR : codec;

    std::array<sdlrdp_event, 4> events{};
    auto count = sdlrdp_poll(backend.get(), events.data(), events.size());
    ASSERT_EQ(count, expected == previous ? 0u : 1u);
    if (count) {
      ASSERT_EQ(count, 1u);
      EXPECT_EQ(events[0].type, SDLRDP_CODEC_CHANGED);
      EXPECT_EQ(events[0].codec_changed.codec, expected);
    }
    previous = expected;
  }
  EXPECT_EQ(sdlrdp_set_codec(backend.get(), sdlrdp_codec(99)), -1);
}
TEST_P(Gate, ExactFlatColour) {
  Client client(sdlrdp_port(backend.get()), GetParam());
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_EQ(Events(1).size(), 1u);
  std::ranges::fill(pixels, 0x00ffffffu);
  ASSERT_NO_FATAL_FAILURE(Frame(client, {0, 0, 320, 200}));
}
TEST_P(Gate, TinyDamage) {
  Client client(sdlrdp_port(backend.get()), GetParam());
  client.tolerance = GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0;
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
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
  auto deadline = Clock::now() + std::chrono::seconds(3);
  while (!logs.Contains(SDLRDP_LOG_INFO, "Connection closed before activation.")
         && Clock::now() < deadline) std::this_thread::yield();
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "Connection closed before activation."));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed"));
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
    ASSERT_TRUE(freerdp_connect(client.instance.get()));
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
  config.user = &second;
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
  Expects(info.param.codec >= SDLRDP_CODEC_AUTO && info.param.codec <= SDLRDP_CODEC_RAW, "known codec");
  constexpr std::array names{"Auto", "Planar", "RemoteFX", "NSCodec", "Raw"};
  return std::string(names[info.param.codec]) + (info.param.surface ? "Surface" : "Bitmap");
}
INSTANTIATE_TEST_SUITE_P(Codec, Gate, testing::Values(
  Mode{true, SDLRDP_CODEC_RAW}, Mode{true, SDLRDP_CODEC_PLANAR},
  Mode{true, SDLRDP_CODEC_REMOTEFX}, Mode{true, SDLRDP_CODEC_NSCODEC},
  Mode{false, SDLRDP_CODEC_RAW}, Mode{false, SDLRDP_CODEC_PLANAR}), ModeName);
}
