#include "_detail/copy-rows.hpp"
#include "_detail/test-backend-events.hpp"
#include "_detail/test-certificate-directory.hpp"
#include "_detail/test-frame-counter.hpp"
#include "_detail/test-has-cookie.hpp"
#include "_detail/test-io.hpp"
#include "support.test/child-process.hpp"

#include <gtest/gtest.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <future>
#include <span>
#include <system_error>

namespace BackendGate {
namespace {
using PlanarContext = std::unique_ptr<BITMAP_PLANAR_CONTEXT, Backend::Releases<freerdp_bitmap_planar_context_free>>;
auto ThenCertificateLifetime(X509* cert) -> void {
  int days    = 0;
  int seconds = 0;
  ASSERT_TRUE(ASN1_TIME_diff(&days, &seconds, X509_get0_notBefore(cert), X509_get0_notAfter(cert)));
  EXPECT_EQ(days, 3650);
}
auto ThenLoggingChild(Headless::ChildProcess& child, std::string const& output) -> void {
  EXPECT_TRUE(child.ExitedCleanly()) << output;
  EXPECT_FALSE(output.contains("com.freerdp")) << output;
}
auto ThenSignedDelta(std::vector<UINT32> const& decoded, std::vector<UINT32> const& pixels) -> void {
  EXPECT_EQ(decoded.front(), pixels.front());
  // FreeRDP 3.15 planar.c:1477 tests unsigned s2c >= 0, misencoding negative deltas.
  EXPECT_NE(decoded, pixels);
  EXPECT_NE(decoded[64], pixels[64]);
}
auto ThenCertificatePermissions(std::filesystem::path const& data) -> void {
  using Perm = std::filesystem::perms;
  EXPECT_EQ(std::filesystem::status(data / "sdl-rdp").permissions() & Perm::mask, Perm::owner_all);
  EXPECT_EQ(std::filesystem::status(data / "sdl-rdp/server.key").permissions() & Perm::mask,
            Perm::owner_read | Perm::owner_write);
}
auto ThenNewestRoute(Logs& first, Logs& second) -> void {
  WLog_Print(WLog_GetRoot(), WLOG_WARN, "latest handle marker");
  EXPECT_FALSE(first.Contains("latest handle marker"));
  EXPECT_TRUE(second.Contains(SDLRDP_LOG_WARN, "latest handle marker"));
}
auto RunLogChild(int output) -> int {
  if (dup2(output, STDOUT_FILENO) < 0) return 125;
  setenv("WLOG_LEVEL", "INFO", 1);
  execl("/proc/self/exe", "sdl-rdp-backend-tests", "--gtest_filter=Logging.ListenerCallback", "--gtest_repeat=1",
        "--gtest_output=", nullptr);
  return 126;
}
auto LoggingChild(Backend::Descriptor output) -> Headless::ChildProcess {
  return Headless::ChildProcess{ [&output] { return RunLogChild(output.Get()); } };
}
}
TEST(CopyRows, PaddedRows) {
  std::array<BYTE, 8> source     { 1, 2, 9, 9, 3, 4, 9, 9 };
  std::array<BYTE, 6> destination{ 8, 8, 8, 8, 8, 8       };
  Backend::CopyRows({ .bytes = source, .pitch = 4 }, { .bytes = destination, .pitch = 3 },
                    { .rows = 2, .row_bytes = 2 });
  EXPECT_EQ(destination, (std::array<BYTE, 6>{ 1, 2, 8, 3, 4, 8 }));
  Backend::CopyRows({ .bytes = source, .pitch = 4 }, { .bytes = destination, .pitch = 3 },
                    { .rows = 2, .row_bytes = 2 }, true);
  EXPECT_EQ(destination, (std::array<BYTE, 6>{ 3, 4, 8, 1, 2, 8 }));
  Backend::CopyRows({ }, { }, { });
}
TEST(Errors, WidthAndBind) {
  CertificateDirectory const certificates;
  sdlrdp_config              config       { "192.0.2.1", 0, certificates.Path().c_str(), 0, 200, 0 };
  sdlrdp_handle*             handle       = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), -1);
  EXPECT_EQ(handle, nullptr);
  EXPECT_TRUE(std::string_view(sdlrdp_last_error()).contains("width"));
  config.width = 320;
  ASSERT_EQ(sdlrdp_open(&config, &handle), -1);
  EXPECT_TRUE(std::string_view(sdlrdp_last_error()).contains(std::system_category().message(EADDRNOTAVAIL)));
  auto other = std::async(std::launch::async, [] { return std::string(sdlrdp_last_error()); });
  EXPECT_TRUE(other.get().empty());
}

namespace {
auto MeasureFullFrame(sdlrdp_handle* handle, Client& client, std::vector<UINT32> const& pixels, sdlrdp_codec codec)
    -> void {
  FrameCounter      counter(client);
  auto              bytes   = client.Received();
  auto              started = Clock::now();
  sdlrdp_rect const area    { 0, 0, 1024, 768 };
  ASSERT_EQ(sdlrdp_present(handle, pixels.data(), 4096, 1024, 768, &area, 1), 0);
  ASSERT_TRUE(client.Until([&] { return counter.Frames() == 1; }));
  auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  EXPECT_TRUE(client.Matches(pixels)) << client.MaxError(pixels);
  auto name = std::to_string(codec);
  testing::Test::RecordProperty("codec_" + name + "_bytes", std::to_string(client.Received() - bytes));
  testing::Test::RecordProperty("codec_" + name + "_ms", std::to_string(elapsed));
  testing::Test::RecordProperty("codec_" + name + "_frames", std::to_string(counter.Frames()));
}
auto WhenFullFrameMeasured(CertificateDirectory const& certificates, Logs& logs, std::vector<UINT32> const& pixels,
                           sdlrdp_codec codec) -> void {
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), 1024, 768, 0, Logs::Collect, &logs };
  config.codec = codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> const backend(handle, sdlrdp_close);
  Client client(sdlrdp_port(handle), true, 1024, 768);
  client.Tolerance(codec == SDLRDP_CODEC_REMOTEFX ? 40 : codec == SDLRDP_CODEC_NSCODEC ? 3 : 0);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  MeasureFullFrame(handle, client, pixels, codec);
}
auto CompressSignedDelta(BITMAP_PLANAR_CONTEXT* encoder, std::vector<UINT32>& pixels, std::vector<BYTE>& compressed,
                         UINT32& size) -> void {
  auto const source = oxbox::utilities::SpanCast<std::uint8_t const>(std::span(pixels));
  ASSERT_NE(freerdp_bitmap_compress_planar(encoder, source.data(), PIXEL_FORMAT_BGRA32, 64, 64, 64 * 4,
                                           compressed.data(), &size),
            nullptr);
  ASSERT_NE(compressed.front() & PLANAR_FORMAT_HEADER_RLE, 0);
}
auto ThenCertificate(std::string const& first, std::filesystem::path const& data) -> void {
  std::unique_ptr<BIO, Backend::Releases<BIO_free>> const   bio(BIO_new_mem_buf(first.data(), int(first.size())));
  std::unique_ptr<X509, Backend::Releases<X509_free>> const cert(
      PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  ASSERT_TRUE(cert);
  std::array<char, 256> hostname{ };
  ASSERT_EQ(gethostname(hostname.data(), hostname.size()), 0);
  EXPECT_EQ(X509_check_host(cert.get(), hostname.data(), 0, 0, nullptr), 1);
  ThenCertificateLifetime(cert.get());
  if (::testing::Test::HasFatalFailure()) return;
  ThenCertificatePermissions(data);
}
}
TEST(Measurement, FullFrames1024x768) {
  CertificateDirectory const certificates;
  Logs                       logs;
  std::vector<UINT32>        pixels(1024uz * 768);
  std::ranges::generate(pixels, [index = 0u]() mutable { return (index++ * 2654435761u) & 0x00ffffff; });
  for (auto codec : { SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC }) {
    WhenFullFrameMeasured(certificates, logs, pixels, codec);
    if (::testing::Test::HasFatalFailure()) return;
  }
}
TEST(Planar, SignedDelta64Rows) {
  constexpr unsigned  width   = 64;
  constexpr unsigned  height  = 64;
  std::vector<UINT32> pixels(static_cast<std::size_t>(width) * height);
  std::vector<UINT32> decoded(pixels.size());
  std::ranges::generate(pixels,
                        [index = 0u]() mutable { return 0xff000000u | ((200u - index++ / width) * 0x00010101u); });
  PlanarContext const encoder(freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE, width, height));
  PlanarContext const decoder(freerdp_bitmap_planar_context_new(0, width, height));
  ASSERT_TRUE(encoder && decoder);
  freerdp_planar_topdown_image(encoder.get(), TRUE);
  std::vector<BYTE> compressed((pixels.size() * 4) + 1024);
  UINT32            size       = compressed.size();
  CompressSignedDelta(encoder.get(), pixels, compressed, size);
  if (::testing::Test::HasFatalFailure()) return;
  auto const target = oxbox::utilities::SpanCast<std::uint8_t>(std::span(decoded));
  ASSERT_TRUE(planar_decompress(decoder.get(), compressed.data(), size, width, height, target.data(),
                                PIXEL_FORMAT_BGRA32, width * 4, 0, 0, width, height, FALSE));
  ThenSignedDelta(decoded, pixels);
}
TEST(Logging, ListenerCallback) {
  CertificateDirectory const certificates;
  Logs                       logs;
  ASSERT_EQ(setenv("WLOG_LEVEL", "INFO", 1), 0);
  sdlrdp_config const config { "127.0.0.1", 0, certificates.Path().c_str(), 320, 200, 0, Logs::Collect, &logs };
  sdlrdp_handle*      raw    = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &raw), 0);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> const backend(raw, sdlrdp_close);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "Listening on socket"));
  EXPECT_EQ(unsetenv("WLOG_LEVEL"), 0);
}
TEST(Logging, NewestHandleRoutesAndClears) {
  CertificateDirectory const certificates;
  Logs                       first;
  Logs                       second;
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), 320, 200, 0, Logs::Collect, &first };
  sdlrdp_handle*             raw          = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &raw), 0);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> a(raw, sdlrdp_close);
  config.log_user = &second;
  ASSERT_EQ(sdlrdp_open(&config, &raw), 0);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> b(raw, sdlrdp_close);
  ThenNewestRoute(first, second);
  a.reset();
  WLog_Print(WLog_GetRoot(), WLOG_ERROR, "older close marker");
  EXPECT_TRUE(second.Contains(SDLRDP_LOG_ERROR, "older close marker"));
  b.reset();
  WLog_Print(WLog_GetRoot(), WLOG_WARN, "closed handle marker");
  EXPECT_FALSE(second.Contains("closed handle marker"));
}
TEST(Logging, NoFreerdpStdout) {
  std::array<int, 2> pipefd{ };
  ASSERT_EQ(pipe2(pipefd.data(), O_CLOEXEC), 0);
  Backend::Descriptor const input  { pipefd[0] };
  auto                      child  = LoggingChild(Backend::Descriptor{ pipefd[1] });
  auto                      output = Headless::ReadText(input.Get());
  ThenLoggingChild(child, output);
}

struct ProcessEnvironment {
public:
  ProcessEnvironment(ProcessEnvironment const&) = delete;
  ProcessEnvironment(ProcessEnvironment&&)      = delete;
  ProcessEnvironment() {
    if (auto* value = getenv("XDG_DATA_HOME")) data = value;
  }
  ~ProcessEnvironment() {
    std::filesystem::current_path(cwd);
    if (data)
      setenv("XDG_DATA_HOME", data->c_str(), 1);
    else
      unsetenv("XDG_DATA_HOME");
  }
  auto operator=(ProcessEnvironment const&) -> ProcessEnvironment& = delete;
  auto operator=(ProcessEnvironment&&)      -> ProcessEnvironment& = delete;

private:
  std::filesystem::path      cwd  = std::filesystem::current_path();
  std::optional<std::string> data;
};
TEST(Certificate, StableDefaultAndPermissions) {
  CertificateDirectory const temporary;
  ProcessEnvironment const   restore;
  auto                       data      = temporary.Path() / "data";
  ASSERT_EQ(setenv("XDG_DATA_HOME", data.c_str(), 1), 0);
  std::string first;
  for (auto const& directory : { temporary.Path() / "one", temporary.Path() / "two" }) {
    std::filesystem::create_directory(directory);
    std::filesystem::current_path(directory);
    sdlrdp_config const config { "127.0.0.1", 0, nullptr, 320, 200, 0 };
    sdlrdp_handle*      handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    sdlrdp_close(handle);
    auto certificate = Headless::ReadText((data / "sdl-rdp/server.crt").c_str());
    if (first.empty())
      first = certificate;
    else
      EXPECT_EQ(first, certificate);
  }
  ThenCertificate(first, data);
}

TEST(Planar, Noisy640Rows) {
  std::unique_ptr<rdpSettings, Backend::Releases<freerdp_settings_free>> const settings(freerdp_settings_new(0));
  ASSERT_TRUE(freerdp_settings_set_uint32(settings.get(), FreeRDP_ColorDepth, 32));
  PlanarContext const encoder(
      freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE | PLANAR_FORMAT_HEADER_NA, 1, 1));
  ASSERT_TRUE(freerdp_bitmap_planar_context_reset(encoder.get(), 640, 1));
  std::vector<BYTE>   payload((640 * 4) + 1024);
  PlanarContext const decoder(freerdp_bitmap_planar_context_new(0, 640, 1));
  std::vector<UINT32> pixels(640);
  std::vector<UINT32> decoded(640);
  auto const          source  = oxbox::utilities::SpanCast<std::uint8_t const>(std::span(pixels));
  auto const          target  = oxbox::utilities::SpanCast<std::uint8_t>(std::span(decoded));
  for (unsigned y = 0; y < 480; ++y) {
    std::ranges::generate(pixels, [i = y * 640]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    UINT32 size = payload.size();
    ASSERT_TRUE(freerdp_bitmap_compress_planar(encoder.get(), source.data(), PIXEL_FORMAT_BGRA32, 640, 1, 2560,
                                               payload.data(), &size));
    ASSERT_TRUE(planar_decompress(decoder.get(), payload.data(), size, 640, 1, target.data(), PIXEL_FORMAT_BGRX32, 2560,
                                  0, 0, 640, 1, TRUE))
        << y;
  }
}
}
