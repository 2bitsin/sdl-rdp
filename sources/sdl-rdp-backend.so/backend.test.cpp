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
#include <iostream>
#include <memory>
#include <span>
#include <vector>
#include <unistd.h>
#include <future>
#include <thread>
#include <fstream>
#include <format>
#include <mutex>
#include <cstring>
#include <cerrno>
#include <openssl/ssl.h>
#include <arpa/inet.h>
#include "_detail/rect.hpp"
#include "_detail/copy-rows.hpp"

namespace {
using Clock = std::chrono::steady_clock;
using utilities::Expects;
struct ReleaseClient {
  void operator()(freerdp* instance) const {
    freerdp_disconnect(instance);
    gdi_free(instance);
    freerdp_context_free(instance);
    freerdp_free(instance);
  }
};
class Client {
public:
  std::unique_ptr<freerdp, ReleaseClient> instance{freerdp_new()};
  explicit Client(unsigned port, bool surface, unsigned width = 320, unsigned height = 200) {
    Expects(instance != nullptr, "client allocated");
    instance->PostConnect = [](freerdp* client) { return gdi_init(client, PIXEL_FORMAT_BGRX32); };
    Expects(freerdp_context_new(instance.get()), "client context allocated");
    auto settings = instance->context->settings;
    Expects(freerdp_settings_set_string(settings, FreeRDP_ServerHostname, "127.0.0.1")
      && freerdp_settings_set_string(settings, FreeRDP_Username, "test")
      && freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, port)
      && freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, width)
      && freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, height)
      && freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32)
      && freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE)
      && freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE)
      && freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, FALSE), "client configured");
    if (!surface) Expects(freerdp_settings_set_uint32(settings, FreeRDP_SurfaceCommandsSupported, 0),
                         "surface commands disabled");
  }
  bool Pump() {
    std::array<HANDLE, 64> handles{};
    auto count = freerdp_get_event_handles(instance->context, handles.data(), handles.size());
    return count && WaitForMultipleObjects(count, handles.data(), FALSE, 10) != WAIT_FAILED
      && freerdp_check_event_handles(instance->context);
  }
  bool Matches(std::vector<UINT32> const& pixels) {
    auto gdi = instance->context->gdi;
    return gdi->stride == gdi->width * 4
      && std::memcmp(pixels.data(), gdi->primary_buffer, pixels.size() * 4) == 0;
  }
  bool Until(auto ready) {
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!ready() && Clock::now() < deadline) if (!Pump()) return false;
    return ready();
  }
};
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
  std::ifstream statm("/proc/self/statm");
  std::size_t total = 0, resident = 0;
  Expects(bool(statm >> total >> resident), "resident pages readable");
  return resident * std::size_t(sysconf(_SC_PAGESIZE));
}
bool Listening(unsigned port)
{
  std::ifstream tcp("/proc/net/tcp");
  auto address = std::format("0100007F:{:04X}", port);
  std::string line;
  while (std::getline(tcp, line))
    if (line.contains(address) && line.contains(" 0A ")) return true;
  return false;
}
struct Logs {
  std::mutex guard;
  std::vector<std::string> lines;
  static void Collect(void* user, sdlrdp_log_level, const char* text) {
    auto& self = *static_cast<Logs*>(user);
    std::scoped_lock lock(self.guard);
    self.lines.emplace_back(text);
  }
  bool Contains(std::string_view text) {
    std::scoped_lock lock(guard);
    return std::ranges::any_of(lines, [=](auto const& line) { return line.contains(text); });
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
  std::cout << sdlrdp_last_error() << '\n';
  config.width = 320;
  ASSERT_EQ(sdlrdp_open(&config, &handle), -1);
  EXPECT_TRUE(std::string_view(sdlrdp_last_error()).contains(std::strerror(EADDRNOTAVAIL)));
  std::cout << sdlrdp_last_error() << '\n';
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
class Gate : public testing::TestWithParam<bool> {
protected:
  CertificateDirectory certificates;
  Logs logs;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend{nullptr, sdlrdp_close};
  std::vector<UINT32> pixels = std::vector<UINT32>(320 * 200);
  void SetUp() override {
    sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 320, 200, 0, Logs::Collect, &logs};
    static std::once_flag tls_initialized;
    std::call_once(tls_initialized, [&] { InitializeTls(config); });
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
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
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
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  auto events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  ASSERT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 320u);
  EXPECT_EQ(events[0].connected.height, 200u);
  EXPECT_EQ(events[0].connected.bpp, 32u);
  auto started = Clock::now();
  ASSERT_NO_FATAL_FAILURE(Frame(client, {0, 0, 320, 200}));
  std::cout << "first matching frame: " << std::chrono::duration_cast<std::chrono::milliseconds>(
    Clock::now() - started).count() << " ms\n";
  for (unsigned y = 51; y < 81; ++y) std::fill_n(pixels.begin() + y * 320 + 73, 40, 0x00020202);
  ASSERT_NO_FATAL_FAILURE(Frame(client, {73, 51, 40, 30}));
  auto cookie = static_cast<ARC_SC_PRIVATE_PACKET const*>(freerdp_settings_get_pointer(
    client.instance->context->settings, FreeRDP_ServerAutoReconnectCookie));
  ASSERT_NE(cookie, nullptr);
  EXPECT_EQ(cookie->cbLen, 28u);
  ASSERT_NO_FATAL_FAILURE(Input(client));
  ASSERT_TRUE(freerdp_disconnect(client.instance.get()));
  events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  backend.reset();
  EXPECT_TRUE(logs.Contains("accepted"));
  EXPECT_TRUE(logs.Contains("disconnected"));
}
TEST_P(Gate, ResizeAndWakeup) {
  backend.reset();
  sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 640, 480, 0};
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  Client client(sdlrdp_port(handle), GetParam());
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
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  auto before = ResidentBytes();
  for (unsigned frame = 0; frame < 200; ++frame) {
    std::ranges::fill(pixels, 0x00010101u * (frame + 1));
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  }
  auto after = ResidentBytes();
  std::cout << "burst RSS: before=" << before << " after=" << after
            << " growth=" << (std::int64_t(after) - std::int64_t(before))
            << " framebuffer=" << pixels.size() * 4 << " bytes\n";
  EXPECT_LE(after, before + pixels.size() * 4);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
}
TEST_P(Gate, ClipsToDesktop) {
  backend.reset();
  sdlrdp_config config{"127.0.0.1", 0, certificates.path.c_str(), 640, 480, 0, Logs::Collect, &logs};
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
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
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
  sdlrdp_handle* handle = nullptr;
  auto opening = std::async(std::launch::async, [&] { return sdlrdp_open(&config, &handle); });
  auto deadline = Clock::now() + std::chrono::seconds(3);
  while (!Listening(port) && Clock::now() < deadline) std::this_thread::yield();
  EXPECT_EQ(opening.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  Client client(port, GetParam());
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
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  Client client(sdlrdp_port(handle), GetParam(), 2048, 1536);
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
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
  auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - started);
  std::cout << "blocked single present: " << pixels.size() * 4 << " bytes, "
            << elapsed.count() << " ms (includes 300 ms without pumping)\n";
  EXPECT_LT(elapsed, std::chrono::seconds(2));
}
INSTANTIATE_TEST_SUITE_P(Transport, Gate, testing::Values(true, false));
}
