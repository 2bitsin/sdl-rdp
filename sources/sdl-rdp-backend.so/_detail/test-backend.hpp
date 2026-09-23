#pragma once
#include "headless-client.hpp"
#include "headless-tls.hpp"
#include "headless-gfx.hpp"
#include "state.hpp"
#include "sdl-rdp-backend.h"
#include "contract.hpp"
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
#include "test-io.hpp"
#include "test-logs.hpp"
#include "test-pattern.hpp"
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
#include "rect.hpp"
#include "copy-rows.hpp"
#include "encoder.hpp"


namespace BackendGate {
using Clock = std::chrono::steady_clock;
using utilities::Expects;
using Headless::Client;
using Headless::DisplayClient;
using Headless::GraphicsScene;
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
inline std::size_t ResidentBytes()
{
  auto statm = Headless::ReadText("/proc/self/statm");
  std::size_t total = 0, resident = 0;
  auto first = std::from_chars(statm.data(), statm.data() + statm.size(), total);
  Expects(first.ec == std::errc() && first.ptr != statm.data() + statm.size(), "total pages readable");
  auto second = std::from_chars(first.ptr + 1, statm.data() + statm.size(), resident);
  Expects(second.ec == std::errc(), "resident pages readable");
  return resident * std::size_t(sysconf(_SC_PAGESIZE));
}
inline bool Listening(unsigned port)
{
  auto tcp = Headless::ReadText("/proc/net/tcp");
  auto address = std::format("0100007F:{:04X}", port);
  return std::ranges::any_of(tcp | std::views::split('\n'), [&](auto row) {
    std::string_view line(row.begin(), row.end());
    return line.contains(address) && line.contains(" 0A ");
  });
}
using Headless::Logs;

struct Socket {
  int descriptor = socket(AF_INET, SOCK_STREAM, 0);
  ~Socket() { if (descriptor >= 0) close(descriptor); }
};
inline void InitializeTls(sdlrdp_config config)
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
inline bool HasCookie(Client const& client)
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
inline std::string ModeName(testing::TestParamInfo<Mode> const& info)
{
  Expects(info.param.codec >= SDLRDP_CODEC_AUTO && info.param.codec <= SDLRDP_CODEC_AVC420, "known codec");
  constexpr std::array names{"Auto", "Planar", "RemoteFX", "NSCodec", "Raw", "Progressive", "Avc420"};
  return std::string(names[info.param.codec]) + (info.param.surface ? "Surface" : "Bitmap");
}
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
}
