#include <oxbox/utilities/span.hpp>
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/logs.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/client/has-cookie.hpp>
#include <sdl-rdp/headless-client.test/frame/counter.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/picture/frame-layout.hpp>
#include <sdl-rdp/utilities/rect.hpp>

#include <gtest/gtest.h>
#include <oxbox/platform/scratch-area.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <vector>

namespace sdl_rdp::integration::video_test::detail::padded_pitch {
using oxbox::platform::ScratchArea;
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::BackendInstance;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::Client;
using sdl_rdp::headless_client_test::client::HasCookie;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::FrameCounter;
using sdl_rdp::headless_client_test::frame::HashPattern;
using sdl_rdp::picture::FrameLayout;
using sdl_rdp::utilities::Rect;

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
  ScratchArea const certificates { "padded-pitch", "sdl-rdp" };
  Logs              logs;
  auto              config       = LoopbackConfig(certificates.Path(), { .width = Width, .height = Height });
  config.codec = Codec::Raw;
  BackendInstance backend;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
  Client client(backend.Port(), true, Width, Height);
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  Pixels expected(std::size_t{ Width } * Height);
  HashPattern(expected);
  auto const        source  = Padded(expected);
  FrameCounter      counter(client);
  Rect const        area    { .x = 0, .y = 0, .w = int{ Width }, .h = int{ Height } };
  FrameLayout const layout  { Width, Height, int{ Stride * sizeof(std::uint32_t) }  };
  backend.Present(source, layout, std::span{ &area, 1 });
  ASSERT_TRUE(client.Until([&] { return counter.Frames() == 1; }));
  EXPECT_TRUE(client.Matches(expected)) << client.MaxError(expected);
}
}
