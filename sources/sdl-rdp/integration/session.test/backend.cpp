#include <sdl-rdp/session/backend.hpp>

#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/client/has-cookie.hpp>
#include <sdl-rdp/headless-client.test/codec/mode.hpp>
#include <sdl-rdp/headless-client.test/frame/counter.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/utilities/child-process.hpp>
#include <sdl-rdp/headless-client.test/utilities/io.hpp>
#include <sdl-rdp/utilities/copy-rows.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <sdl-rdp/utilities/rect.hpp>

#include <gtest/gtest.h>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <oxbox/platform/scratch-area.hpp>
#include <oxbox/utilities/span.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <filesystem>
#include <future>
#include <span>
#include <system_error>
#include <utility>

namespace sdl_rdp::integration::session_test::detail::backend {
using oxbox::platform::ScratchArea;
using sdl_rdp::configuration::Codec;
using sdl_rdp::configuration::Setup;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::Bio;
using sdl_rdp::freerdp_facade::Certificate;
using sdl_rdp::freerdp_facade::Settings;
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Clock;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::HasCookie;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::codec::CodecTolerance;
using sdl_rdp::headless_client_test::frame::FrameCounter;
using sdl_rdp::headless_client_test::frame::HashPattern;
using sdl_rdp::headless_client_test::utilities::ChildProcess;
using sdl_rdp::headless_client_test::utilities::ReadText;
using sdl_rdp::utilities::CopyRows;
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Releases;

namespace {
using PlanarContext = std::unique_ptr<BITMAP_PLANAR_CONTEXT, Releases<freerdp_bitmap_planar_context_free>>;
auto ThenCertificateLifetime(X509 const& cert) -> void {
  int days    = 0;
  int seconds = 0;
  ASSERT_TRUE(ASN1_TIME_diff(&days, &seconds, X509_get0_notBefore(&cert), X509_get0_notAfter(&cert)));
  EXPECT_EQ(days, 3650);
}
auto ThenLoggingChild(ChildProcess& child, std::string const& output) -> void {
  EXPECT_TRUE(child.ExitedCleanly()) << output;
  EXPECT_FALSE(output.contains("com.freerdp")) << output;
}
auto ThenCertificatePermissions(std::filesystem::path const& data) -> void {
  using Perm = std::filesystem::perms;
  EXPECT_EQ(std::filesystem::status(data / "sdl-rdp").permissions() & Perm::mask, Perm::owner_all);
  EXPECT_EQ(std::filesystem::status(data / "sdl-rdp/server.key").permissions() & Perm::mask,
            Perm::owner_read | Perm::owner_write);
}
auto ThenNewestRoute(Logs& first, Logs& second) -> void {
  WLog_Print(WLog_GetRoot(), WLOG_WARN, "latest backend marker");
  EXPECT_FALSE(first.Contains("latest backend marker"));
  EXPECT_TRUE(second.Contains(LogLevel::Warn, "latest backend marker"));
}
auto RunLogChild(int output) -> int {
  if (dup2(output, STDOUT_FILENO) < 0) return 125;
  setenv("WLOG_LEVEL", "INFO", 1);
  execl("/proc/self/exe", "sdl-rdp-integration-tests", "--gtest_filter=Logging.ListenerCallback", "--gtest_repeat=1",
        "--gtest_output=", nullptr);
  return 126;
}
auto LoggingChild(Descriptor output) -> ChildProcess {
  return ChildProcess{ [&output] { return RunLogChild(output.Get()); } };
}
}
TEST(CopyRows, PaddedRows) {
  std::array<std::uint8_t, 8> source     { 1, 2, 9, 9, 3, 4, 9, 9 };
  std::array<std::uint8_t, 6> destination{ 8, 8, 8, 8, 8, 8       };
  CopyRows({ .bytes = source, .pitch = 4 }, { .bytes = destination, .pitch = 3 }, { .rows = 2, .row_bytes = 2 });
  EXPECT_EQ(destination, (std::array<std::uint8_t, 6>{ 1, 2, 8, 3, 4, 8 }));
  CopyRows({ .bytes = source, .pitch = 4 }, { .bytes = destination, .pitch = 3 }, { .rows = 2, .row_bytes = 2 }, true);
  EXPECT_EQ(destination, (std::array<std::uint8_t, 6>{ 3, 4, 8, 1, 2, 8 }));
  CopyRows({ }, { }, { });
}
namespace {
// 192.0.2.1 is TEST-NET-1 (RFC 5737), an address no interface on the box carries.
auto UnroutableConfig(std::filesystem::path const& certificates, Extent size) -> Setup {
  auto config = LoopbackConfig(certificates, size);
  config.bind = "192.0.2.1";
  return config;
}
}
TEST(Errors, WidthAndBind) {
  ScratchArea const certificates { "errors", "sdl-rdp" };
  Logs              logs;
  auto              config       = UnroutableConfig(certificates.Path(), { .width = 0, .height = 200 });
  BackendInstance   backend;
  auto const        width        = backend.TryOpen(config, logs);
  ASSERT_FALSE(width.has_value()) << "a zero width is refused";
  EXPECT_FALSE(backend);
  EXPECT_TRUE(width.error().contains("width")) << width.error();
  config.width = 320;
  auto const bind = backend.TryOpen(config, logs);
  ASSERT_FALSE(bind.has_value()) << "an unroutable bind address is refused";
  EXPECT_TRUE(bind.error().contains(std::system_category().message(EADDRNOTAVAIL))) << bind.error();
}

namespace {
auto MeasureFullFrame(BackendInstance const& backend, Client& client, Pixels const& pixels, Codec codec) -> void {
  FrameCounter counter(client);
  auto         bytes   = client.Received();
  auto         started = Clock::now();
  Rect const   area    { .x = 0, .y = 0, .w = 1024, .h = 768 };
  backend.Present(pixels, 1024, 768, area);
  ASSERT_TRUE(client.Until([&] { return counter.Frames() == 1; }));
  auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
  EXPECT_TRUE(client.Matches(pixels)) << client.MaxError(pixels);
  auto name = std::to_string(std::to_underlying(codec));
  testing::Test::RecordProperty("codec_" + name + "_bytes", std::to_string(client.Received() - bytes));
  testing::Test::RecordProperty("codec_" + name + "_ms", std::to_string(elapsed));
  testing::Test::RecordProperty("codec_" + name + "_frames", std::to_string(counter.Frames()));
}
auto WhenFullFrameMeasured(ScratchArea const& certificates, Logs& logs, Pixels const& pixels, Codec codec) -> void {
  auto config = LoopbackConfig(certificates.Path(), { .width = 1024, .height = 768 });
  config.codec = codec;
  BackendInstance backend;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
  Client client(backend.Port(), true, 1024, 768);
  client.Tolerance(CodecTolerance(codec, true));
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  MeasureFullFrame(backend, client, pixels, codec);
}
auto CompressSignedDelta(BITMAP_PLANAR_CONTEXT& encoder, Pixels& pixels, std::vector<std::uint8_t>& compressed,
                         std::uint32_t& size) -> void {
  auto const source = oxbox::utilities::SpanCast<std::uint8_t const>(std::span(pixels));
  ASSERT_NE(freerdp_bitmap_compress_planar(&encoder, source.data(), PIXEL_FORMAT_BGRA32, 64, 64, 64 * 4,
                                           compressed.data(), &size),
            nullptr);
  ASSERT_NE(compressed.front() & PLANAR_FORMAT_HEADER_RLE, 0);
}
auto ThenCertificate(std::string const& first, std::filesystem::path const& data) -> void {
  Bio const         bio(BIO_new_mem_buf(first.data(), Narrowed<int>(first.size())));
  Certificate const cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  ASSERT_TRUE(cert);
  std::array<char, 256> hostname{ };
  ASSERT_EQ(gethostname(hostname.data(), hostname.size()), 0);
  EXPECT_EQ(X509_check_host(cert.get(), hostname.data(), 0, 0, nullptr), 1);
  ASSERT_NO_FATAL_FAILURE(ThenCertificateLifetime(*cert));
  ThenCertificatePermissions(data);
}
}
TEST(Measurement, FullFrames1024x768) {
  ScratchArea const certificates{ "measurement", "sdl-rdp" };
  Logs              logs;
  Pixels            pixels(1024uz * 768);
  HashPattern(pixels);
  for (auto codec : { Codec::Raw, Codec::Planar, Codec::RemoteFx, Codec::NsCodec }) {
    ASSERT_NO_FATAL_FAILURE(WhenFullFrameMeasured(certificates, logs, pixels, codec));
  }
}
TEST(Planar, SignedDelta64Rows) {
  constexpr std::uint32_t width   = 64;
  constexpr std::uint32_t height  = 64;
  Pixels                  pixels(static_cast<std::size_t>(width) * height);
  Pixels                  decoded(pixels.size());
  std::ranges::generate(pixels,
                        [index = 0u]() mutable { return 0xff000000u | ((200u - index++ / width) * 0x00010101u); });
  PlanarContext const encoder(freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE, width, height));
  PlanarContext const decoder(freerdp_bitmap_planar_context_new(0, width, height));
  ASSERT_TRUE(encoder && decoder);
  freerdp_planar_topdown_image(encoder.get(), true);
  std::vector<std::uint8_t> compressed((pixels.size() * 4) + 1024);
  auto                      size       = Narrowed<std::uint32_t>(compressed.size());
  ASSERT_NO_FATAL_FAILURE(CompressSignedDelta(*encoder, pixels, compressed, size));
  auto const target = oxbox::utilities::SpanCast<std::uint8_t>(std::span(decoded));
  ASSERT_TRUE(freerdp_bitmap_decompress_planar(decoder.get(), compressed.data(), size, width, height, target.data(),
                                               PIXEL_FORMAT_BGRA32, width * 4, 0, 0, width, height, false));
  EXPECT_EQ(decoded, pixels);
}
TEST(Logging, ListenerCallback) {
  ScratchArea const certificates{ "listener", "sdl-rdp" };
  Logs              logs;
  ASSERT_EQ(setenv("WLOG_LEVEL", "INFO", 1), 0);
  auto const      config  = LoopbackConfig(certificates.Path());
  BackendInstance backend;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
  EXPECT_TRUE(logs.Contains(LogLevel::Info, "Listening on socket"));
  EXPECT_EQ(unsetenv("WLOG_LEVEL"), 0);
}
TEST(Logging, NewestBackendRoutesAndClears) {
  ScratchArea const certificates { "routing", "sdl-rdp" };
  Logs              first;
  Logs              second;
  auto const        config       = LoopbackConfig(certificates.Path());
  BackendInstance   a;
  BackendInstance   b;
  ASSERT_NO_FATAL_FAILURE(a.Open(config, first));
  ASSERT_NO_FATAL_FAILURE(b.Open(config, second));
  ThenNewestRoute(first, second);
  a.Close();
  WLog_Print(WLog_GetRoot(), WLOG_ERROR, "older close marker");
  EXPECT_TRUE(second.Contains(LogLevel::Error, "older close marker"));
  b.Close();
  WLog_Print(WLog_GetRoot(), WLOG_WARN, "closed backend marker");
  EXPECT_FALSE(second.Contains("closed backend marker"));
}
TEST(Logging, NoFreerdpStdout) {
  std::array<int, 2> pipefd{ };
  ASSERT_EQ(pipe2(pipefd.data(), O_CLOEXEC), 0);
  Descriptor const input  { pipefd[0] };
  auto             child  = LoggingChild(Descriptor{ pipefd[1] });
  auto             output = ReadText(input.Get());
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
namespace {
// No certificate directory: the backend generates its pair under the data home.
auto DefaultCertificateConfig() -> Setup {
  return { .bind = "127.0.0.1", .width = 320, .height = 200 };
}
}
TEST(Certificate, StableDefaultAndPermissions) {
  ScratchArea const        temporary { "certificate", "sdl-rdp" };
  ProcessEnvironment const restore;
  auto                     data      = temporary.Path() / "data";
  ASSERT_EQ(setenv("XDG_DATA_HOME", data.c_str(), 1), 0);
  std::string first;
  for (auto const& directory : { temporary.Path() / "one", temporary.Path() / "two" }) {
    std::filesystem::create_directory(directory);
    std::filesystem::current_path(directory);
    Logs            logs;
    BackendInstance backend;
    ASSERT_NO_FATAL_FAILURE(backend.Open(DefaultCertificateConfig(), logs));
    backend.Close();
    auto certificate = ReadText((data / "sdl-rdp/server.crt").c_str());
    if (first.empty())
      first = certificate;
    else
      EXPECT_EQ(first, certificate);
  }
  ThenCertificate(first, data);
}

TEST(Planar, Noisy640Rows) {
  Settings const settings(freerdp_settings_new(0));
  ASSERT_TRUE(freerdp_settings_set_uint32(settings.get(), FreeRDP_ColorDepth, 32));
  PlanarContext const encoder(
      freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE | PLANAR_FORMAT_HEADER_NA, 1, 1));
  ASSERT_TRUE(freerdp_bitmap_planar_context_reset(encoder.get(), 640, 1));
  std::vector<std::uint8_t> payload((640 * 4) + 1024);
  PlanarContext const       decoder(freerdp_bitmap_planar_context_new(0, 640, 1));
  Pixels                    pixels(640);
  Pixels                    decoded(640);
  auto const                source  = oxbox::utilities::SpanCast<std::uint8_t const>(std::span(pixels));
  auto const                target  = oxbox::utilities::SpanCast<std::uint8_t>(std::span(decoded));
  for (std::uint32_t y = 0; y < 480; ++y) {
    HashPattern(pixels, y * 640);
    auto size = Narrowed<std::uint32_t>(payload.size());
    ASSERT_TRUE(freerdp_bitmap_compress_planar(encoder.get(), source.data(), PIXEL_FORMAT_BGRA32, 640, 1, 2560,
                                               payload.data(), &size));
    ASSERT_TRUE(freerdp_bitmap_decompress_planar(decoder.get(), payload.data(), size, 640, 1, target.data(),
                                                 PIXEL_FORMAT_BGRX32, 2560, 0, 0, 640, 1, true))
        << y;
  }
}
}
