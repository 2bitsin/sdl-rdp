#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/certificate-directory.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/client/has-cookie.hpp>
#include <sdl-rdp/headless-client.test/frame/counter.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::integration::video_test::detail::padded_pitch {
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::CertificateDirectory;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::HasCookie;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::FrameCounter;
using sdl_rdp::headless_client_test::frame::HashPattern;

namespace {
constexpr std::uint32_t Width         = 320;
constexpr std::uint32_t Height        = 200;
constexpr std::size_t   PaddingPixels = 16;
constexpr std::size_t   Stride        = Width + PaddingPixels;
constexpr std::uint32_t Padding       = 0xdeadbeef;
// The caller's buffer ends at the last row's last pixel, as an SDL surface's does.
auto Padded(std::span<std::uint32_t const> rows) -> Pixels {
  Pixels source((Stride * (Height - 1)) + Width, Padding);
  for (auto const y : std::views::iota(0uz, std::size_t{ Height }))
    std::ranges::copy(rows.subspan(y * Width, Width), source.begin() + static_cast<std::ptrdiff_t>(y * Stride));
  return source;
}
}
TEST(PaddedPitch, ClientFrameEqualsTheSource) {
  CertificateDirectory const certificates;
  Logs                       logs;
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), Width, Height, 0, Logs::Collect, &logs };
  config.codec = SDLRDP_CODEC_RAW;
  BackendInstance backend;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config));
  Client client(sdlrdp_port(&*backend), true, Width, Height);
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  Pixels expected(std::size_t{ Width } * Height);
  HashPattern(expected);
  auto const        source  = Padded(expected);
  FrameCounter      counter(client);
  sdlrdp_rect const area    { 0, 0, int{ Width }, int{ Height } };
  ASSERT_EQ(sdlrdp_present(&*backend, source.data(), int{ Stride * sizeof(std::uint32_t) }, Width, Height, &area, 1), 0)
      << sdlrdp_last_error();
  ASSERT_TRUE(client.Until([&] { return counter.Frames() == 1; }));
  EXPECT_TRUE(client.Matches(expected)) << client.MaxError(expected);
}
}
