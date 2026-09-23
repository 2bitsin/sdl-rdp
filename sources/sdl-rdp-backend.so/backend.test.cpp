#include <system_error>
#include <cstddef>
#include <algorithm>

#include "_detail/test-backend.hpp"

namespace BackendGate {
TEST(CopyRows, PaddedRows)
{
  std::array<BYTE, 8> source     { 1, 2, 9, 9, 3, 4, 9, 9 };
  std::array<BYTE, 6> destination{ 8, 8, 8, 8, 8, 8       };
  Backend::CopyRows(source, 4, destination, 3, 2, 2);
  EXPECT_EQ(destination, (std::array<BYTE, 6>{ 1, 2, 8, 3, 4, 8 }));
  Backend::CopyRows(source, 4, destination, 3, 2, 2, true);
  EXPECT_EQ(destination, (std::array<BYTE, 6>{ 3, 4, 8, 1, 2, 8 }));
  Backend::CopyRows({}, 0, {}, 0, 0, 0);
}
TEST(Errors, WidthAndBind)
{
  CertificateDirectory const certificates;
  sdlrdp_config              config      { "192.0.2.1", 0, certificates.path.c_str(), 0, 200, 0 };
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

TEST(Measurement, FullFrames1024x768)
{
  CertificateDirectory const certificates;
  Logs                       logs;
  std::vector<UINT32>        pixels(1024uz * 768);
  std::ranges::generate(pixels, [index = 0u]() mutable { return (index++ * 2654435761u) & 0x00ffffff; });
  for (auto codec : { SDLRDP_CODEC_RAW, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX, SDLRDP_CODEC_NSCODEC }) {
    sdlrdp_config config{ "127.0.0.1", 0, certificates.path.c_str(), 1024, 768, 0, Logs::Collect, &logs };
    config.codec          = codec;
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> const backend(handle, sdlrdp_close);
    Client                                                        client(sdlrdp_port(handle), true, 1024, 768);
    client.tolerance = codec == SDLRDP_CODEC_REMOTEFX ? 40 : codec == SDLRDP_CODEC_NSCODEC ? 3
                                                                                           : 0;
    ASSERT_TRUE(freerdp_connect(client.instance.get())) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
    FrameCounter      counter(client);
    auto              bytes   = client.Received();
    auto              started = Clock::now();
    sdlrdp_rect const area   { 0, 0, 1024, 768 };
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
TEST(Planar, SignedDelta64Rows)
{
  constexpr unsigned width  = 64;
  constexpr unsigned height = 64;
  std::vector<UINT32> pixels(static_cast<std::size_t>(width) * height);
  std::vector<UINT32> decoded(pixels.size());
  std::ranges::generate(pixels, [index = 0u]() mutable { return 0xff000000u | ((200u - index++ / width) * 0x00010101u); });
  std::unique_ptr<BITMAP_PLANAR_CONTEXT, Backend::Releases<freerdp_bitmap_planar_context_free>>
      encoder(freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE, width, height));
  std::unique_ptr<BITMAP_PLANAR_CONTEXT, Backend::Releases<freerdp_bitmap_planar_context_free>>
      decoder(freerdp_bitmap_planar_context_new(0, width, height));
  ASSERT_TRUE(encoder && decoder);
  freerdp_planar_topdown_image(encoder.get(), TRUE);
  std::vector<BYTE> compressed((pixels.size() * 4) + 1024);
  UINT32 size = compressed.size();
  ASSERT_NE(freerdp_bitmap_compress_planar(encoder.get(), reinterpret_cast<BYTE*>(pixels.data()),
                                           PIXEL_FORMAT_BGRA32, width, height, width * 4, compressed.data(), &size),
            nullptr);
  ASSERT_NE(compressed.front() & PLANAR_FORMAT_HEADER_RLE, 0);
  ASSERT_TRUE(planar_decompress(decoder.get(), compressed.data(), size, width, height,
                                reinterpret_cast<BYTE*>(decoded.data()), PIXEL_FORMAT_BGRA32, width * 4,
                                0, 0, width, height, FALSE));
  EXPECT_EQ(decoded.front(), pixels.front());
  // FreeRDP 3.15 planar.c:1477 tests unsigned s2c >= 0, misencoding negative deltas.
  EXPECT_NE(decoded, pixels);
  EXPECT_NE(decoded[width], pixels[width]);
}
TEST(Logging, ListenerCallback)
{
  CertificateDirectory const certificates;
  Logs                       logs;
  ASSERT_EQ(setenv("WLOG_LEVEL", "INFO", 1), 0);
  sdlrdp_config const config{ "127.0.0.1", 0, certificates.path.c_str(), 320, 200, 0, Logs::Collect, &logs };
  sdlrdp_handle*      raw    = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &raw), 0);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> const backend(raw, sdlrdp_close);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "Listening on socket"));
  EXPECT_EQ(unsetenv("WLOG_LEVEL"), 0);
}
TEST(Logging, NewestHandleRoutesAndClears)
{
  CertificateDirectory const certificates;
  Logs                       first;
  Logs                       second;
  sdlrdp_config              config      { "127.0.0.1", 0, certificates.path.c_str(), 320, 200, 0, Logs::Collect, &first };
  sdlrdp_handle*             raw          = nullptr;
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
TEST(Logging, NoFreerdpStdout)
{
  std::array<int, 2> pipefd{ };
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
  Headless::Descriptor const input { pipefd[0] };
  auto                       output = Headless::ReadText(input.value);
  int                        status = 0;
  ASSERT_EQ(waitpid(child, &status, 0), child);
  EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0) << output;
  EXPECT_FALSE(output.contains("com.freerdp")) << output;
}

struct ProcessEnvironment {
public:
  ProcessEnvironment(ProcessEnvironment const&)            = delete;
  ProcessEnvironment& operator=(ProcessEnvironment const&) = delete;
  ProcessEnvironment(ProcessEnvironment&&)                 = delete;
  ProcessEnvironment& operator=(ProcessEnvironment&&)      = delete;
  ProcessEnvironment()
  {
    if (auto* value = getenv("XDG_DATA_HOME")) data = value;
  }
  ~ProcessEnvironment()
  {
    std::filesystem::current_path(cwd);
    if (data) setenv("XDG_DATA_HOME", data->c_str(), 1);
    else unsetenv("XDG_DATA_HOME");
  }

private:
  std::filesystem::path      cwd  = std::filesystem::current_path();
  std::optional<std::string> data;
};
TEST(Certificate, StableDefaultAndPermissions)
{
  CertificateDirectory const temporary;
  ProcessEnvironment const   restore;
  auto                       data      = temporary.path / "data";
  ASSERT_EQ(setenv("XDG_DATA_HOME", data.c_str(), 1), 0);
  std::string first;
  for (const auto& directory : { temporary.path / "one", temporary.path / "two" }) {
    std::filesystem::create_directory(directory);
    std::filesystem::current_path(directory);
    sdlrdp_config const config{ "127.0.0.1", 0, nullptr, 320, 200, 0 };
    sdlrdp_handle*      handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    sdlrdp_close(handle);
    auto certificate = Headless::ReadText((data / "sdl-rdp/server.crt").c_str());
    if (first.empty()) first = certificate;
    else EXPECT_EQ(first, certificate);
  }
  std::unique_ptr<BIO, Backend::Releases<BIO_free>> const   bio(BIO_new_mem_buf(first.data(), first.size()));
  std::unique_ptr<X509, Backend::Releases<X509_free>> const cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
  ASSERT_TRUE(cert);
  std::array<char, 256> hostname{ };
  ASSERT_EQ(gethostname(hostname.data(), hostname.size()), 0);
  EXPECT_EQ(X509_check_host(cert.get(), hostname.data(), 0, 0, nullptr), 1);
  int days    = 0;
  int seconds = 0;
  ASSERT_TRUE(ASN1_TIME_diff(&days, &seconds, X509_get0_notBefore(cert.get()), X509_get0_notAfter(cert.get())));
  EXPECT_EQ(days, 3650);
  using Perm = std::filesystem::perms;
  EXPECT_EQ(std::filesystem::status(data / "sdl-rdp").permissions() & Perm::mask, Perm::owner_all);
  EXPECT_EQ(std::filesystem::status(data / "sdl-rdp/server.key").permissions() & Perm::mask,
            Perm::owner_read | Perm::owner_write);
}

TEST(Planar, Noisy640Rows)
{
  std::unique_ptr<rdpSettings, Backend::Releases<freerdp_settings_free>> const settings(freerdp_settings_new(0));
  ASSERT_TRUE(freerdp_settings_set_uint32(settings.get(), FreeRDP_ColorDepth, 32));
  std::unique_ptr<BITMAP_PLANAR_CONTEXT, Backend::Releases<freerdp_bitmap_planar_context_free>> const encoder(freerdp_bitmap_planar_context_new(PLANAR_FORMAT_HEADER_RLE | PLANAR_FORMAT_HEADER_NA, 1, 1));
  ASSERT_TRUE(freerdp_bitmap_planar_context_reset(encoder.get(), 640, 1));
  std::vector<BYTE>                                                                                   payload((640 * 4) + 1024);
  std::unique_ptr<BITMAP_PLANAR_CONTEXT, Backend::Releases<freerdp_bitmap_planar_context_free>> const decoder(freerdp_bitmap_planar_context_new(0, 640, 1));
  std::vector<UINT32>                                                                                 pixels(640);
  std::vector<UINT32>                                                                                 decoded(640);
  for (unsigned y = 0; y < 480; ++y) {
    std::ranges::generate(pixels, [i = y * 640]() mutable { return (i++ * 2654435761u) & 0xffffff; });
    UINT32 size = payload.size();
    ASSERT_TRUE(freerdp_bitmap_compress_planar(encoder.get(), reinterpret_cast<BYTE const*>(pixels.data()),
                                               PIXEL_FORMAT_BGRA32, 640, 1, 2560, payload.data(), &size));
    ASSERT_TRUE(planar_decompress(decoder.get(), payload.data(), size, 640, 1,
                                  reinterpret_cast<BYTE*>(decoded.data()), PIXEL_FORMAT_BGRX32, 2560, 0, 0, 640, 1, TRUE))
        << y;
  }
}
}
